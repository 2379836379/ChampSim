#include "my_perceptron.h"
#include "instruction.h"
#include <limits>

bool my_perceptron::predict_branch(champsim::address pc)
{
  const auto pc_value = pc.to<uint64_t>();
  const auto pc_hash = ((pc_value >> 2) ^ (pc_value >> 15) ^ (pc_value >> 29)) & (BASE_TABLE_SIZE - 1);

  tage_result result;
  result.base_index = pc_hash;
  result.base_prediction = base_table[result.base_index].value() >= 2;
  result.tage_prediction = result.base_prediction;
  result.pc_tag = pc_value;
  result.loop_index = ((pc_value >> 4) ^ (pc_value >> 16) ^ (pc_value >> 28)) & (LOOP_TABLE_SIZE - 1);

  for (std::size_t table = TAGE_TABLES; table-- > 0;) {
    const auto index_history = index_histories[table].value();
    const auto tag_history = tag_histories[table].value();
    const auto path_mix = path_history ^ (path_history >> (table + 1)) ^ (path_history_alt >> ((table * 3) + 1));
    const auto index = (pc_value ^ (pc_value >> (table + 2)) ^ index_history ^ path_mix) & (TAGE_TABLE_SIZE - 1);
    const auto tag = static_cast<uint16_t>(((pc_value >> 2) ^ (pc_value >> (table + 7)) ^ tag_history ^ (index_history >> 3) ^ (path_mix >> 4)) & TAGE_TAG_MASK) + uint16_t{1};
    result.indices[table] = static_cast<uint32_t>(index);
    result.tags[table] = static_cast<uint16_t>(tag);

    if (tage_tags[table][index] == tag) {
      if (result.provider == TAGE_TABLES) {
        result.provider = table;
        result.provider_prediction = tage_counters[table][index].value() >= 0;
      } else if (result.alternate == TAGE_TABLES) {
        result.alternate = table;
        result.alternate_prediction = tage_counters[table][index].value() >= 0;
      }
    }
  }

  const bool alternate_prediction = result.alternate == TAGE_TABLES ? result.base_prediction : result.alternate_prediction;
  result.alternate_prediction = alternate_prediction;
  if (result.provider != TAGE_TABLES) {
    const auto provider_counter = tage_counters[result.provider][result.indices[result.provider]].value();
    result.provider_weak = provider_counter == 0 || provider_counter == -1;
    result.provider_unuseful = tage_usefulness[result.provider][result.indices[result.provider]].value() == 0;
    result.alt_selector_index = (pc_hash ^ (pc_hash >> 7) ^ (result.provider * 0x9e37u)) & (ALT_SELECTOR_SIZE - 1);
    result.used_alternate = result.provider_weak && result.provider_unuseful && use_alt_on_na[result.alt_selector_index].value() >= 0;
    result.tage_prediction = result.used_alternate ? alternate_prediction : result.provider_prediction;
  }

  for (std::size_t table = 0; table < SC_TABLES; ++table) {
    const auto history = sc_histories[table].value();
    result.sc_indices[table] = static_cast<uint32_t>((pc_value ^ (pc_value >> (table + 3)) ^ history) & (SC_TABLE_SIZE - 1));
  }
  const auto local_pc_hash = pc_value ^ (pc_value >> 13) ^ (pc_value >> 26);
  result.sc_local_slot = static_cast<uint32_t>(local_pc_hash & (SC_TABLE_SIZE - 1));
  const auto local_history = sc_local_histories[result.sc_local_slot];
  for (std::size_t i = 0; i < sc_local_lengths.size(); ++i) {
    const auto recent = local_history & ((uint32_t{1} << sc_local_lengths[i]) - 1);
    const auto folded = recent ^ (recent >> champsim::msl::lg2(SC_TABLE_SIZE));
    result.sc_indices[SC_TABLES + i] = static_cast<uint32_t>((local_pc_hash ^ folded) & (SC_TABLE_SIZE - 1));
  }
  const auto alt_local_pc_hash = (pc_value >> 3) ^ (pc_value >> 11) ^ (pc_value >> 24);
  result.sc_alt_local_slot = static_cast<uint32_t>(alt_local_pc_hash & (SC_TABLE_SIZE - 1));
  const auto alt_local_history = sc_alt_local_histories[result.sc_alt_local_slot];
  for (std::size_t i = 0; i < sc_alt_local_lengths.size(); ++i) {
    const auto recent = alt_local_history & ((uint32_t{1} << sc_alt_local_lengths[i]) - 1);
    const auto folded = recent ^ (recent >> champsim::msl::lg2(SC_TABLE_SIZE));
    result.sc_indices[SC_TABLES + sc_local_lengths.size() + i] = static_cast<uint32_t>((alt_local_pc_hash ^ folded) & (SC_TABLE_SIZE - 1));
  }
  for (std::size_t table = 0; table < SC_TOTAL_TABLES; ++table)
    result.sc_sum += static_cast<int>(sc_counters[table][result.sc_indices[table]].value());
  result.sc_prediction = result.sc_sum >= 0;
  result.sc_selector_index = ((pc_value >> 2) ^ (pc_value >> 13) ^ (pc_value >> 23)) & (SC_SELECTOR_SIZE - 1);
  result.sc_disagrees = result.sc_prediction != result.tage_prediction;
  if (std::abs(result.sc_sum) >= SC_THRESHOLD && result.sc_disagrees) {
    result.used_sc = sc_selectors[result.sc_selector_index].value() >= 0;
  }
  if (result.used_sc) {
    result.tage_prediction = result.sc_prediction;
  }
  result.pre_loop_prediction = result.tage_prediction;

  const auto& loop = loop_table[result.loop_index];
  if (loop.valid && loop.tag == result.pc_tag && loop.trip_count >= 2) {
    result.loop_prediction_valid = true;
    result.loop_prediction = loop.current_count < loop.trip_count;
    result.use_loop = loop.confidence.value() >= 1 && loop.chooser.value() >= 1;
  }

  last_result = result;
  if (result.use_loop) {
    return result.loop_prediction;
  }
  return result.pre_loop_prediction;
}

void my_perceptron::last_branch_result(champsim::address pc, champsim::address branch_target, bool taken, uint8_t branch_type)
{
  if (branch_type != BRANCH_CONDITIONAL && branch_type != BRANCH_OTHER) {
    return;
  }

  for (auto& history : index_histories) {
    history.push_back(taken);
  }
  for (auto& history : tag_histories) {
    history.push_back(taken);
  }
  for (auto& history : sc_histories) {
    history.push_back(taken);
  }
  const auto pc_value = pc.to<uint64_t>();
  path_history = (path_history << 7) ^ (pc_value >> 2) ^ branch_target.to<uint64_t>();
  path_history_alt = (path_history_alt << 11) ^ (pc_value * 0x9e3779b97f4a7c15ULL) ^ (branch_target.to<uint64_t>() >> 3);
  auto& local_history = sc_local_histories[last_result.sc_local_slot];
  local_history = (local_history << 1) | static_cast<uint32_t>(taken);
  auto& alt_local_history = sc_alt_local_histories[last_result.sc_alt_local_slot];
  alt_local_history = (alt_local_history << 1) | static_cast<uint32_t>(taken);

  if (++usefulness_clock == (1u << 18)) {
    for (auto& table : tage_usefulness) {
      for (auto& usefulness : table) {
        if (usefulness.value() != 0) {
          usefulness--;
        }
      }
    }
    usefulness_clock = 0;
  }

  if (last_result.provider == TAGE_TABLES) {
    base_table[last_result.base_index] += taken ? 1 : -1;
  } else {
    const auto provider = last_result.provider;
    const auto index = last_result.indices[provider];
    auto& counter = tage_counters[provider][index];
    counter += taken ? 1 : -1;

    // Use prediction-time state: training/aging may change weakness or u. The
    // alternate is trained only when it was actually selected for this branch.
    if (last_result.used_alternate) {
      if (last_result.alternate != TAGE_TABLES) {
        tage_counters[last_result.alternate][last_result.indices[last_result.alternate]] += taken ? 1 : -1;
      } else {
        base_table[last_result.base_index] += taken ? 1 : -1;
      }
    }

    if (last_result.provider_weak && last_result.provider_unuseful &&
        last_result.provider_prediction != last_result.alternate_prediction) {
      use_alt_on_na[last_result.alt_selector_index] += last_result.alternate_prediction == taken ? 1 : -1;
    }

    if (last_result.provider_prediction != last_result.alternate_prediction) {
      auto& usefulness = tage_usefulness[provider][index];
      if (last_result.provider_prediction == taken) {
        usefulness++;
      } else {
        usefulness--;
      }
    }
  }

  const auto sc_mispredicted = last_result.sc_prediction != taken;
  const auto sc_near_threshold = std::abs(last_result.sc_sum) < SC_THRESHOLD;
  if (sc_mispredicted || sc_near_threshold) {
    for (std::size_t table = 0; table < SC_TOTAL_TABLES; ++table) {
      sc_counters[table][last_result.sc_indices[table]] += taken ? 1 : -1;
    }
  }
  if (std::abs(last_result.sc_sum) >= SC_THRESHOLD && last_result.sc_disagrees) {
    sc_selectors[last_result.sc_selector_index] += last_result.sc_prediction == taken ? 1 : -1;
  }

  // Allocate on a provider miss, even when the alternate happened to rescue
  // the current prediction. That longer-history context can learn the pattern
  // before the shorter provider becomes destructive again.
  const bool provider_mispredicted = last_result.provider == TAGE_TABLES ? last_result.base_prediction != taken : last_result.provider_prediction != taken;
  if (provider_mispredicted) {
    const auto first_table = last_result.provider == TAGE_TABLES ? 0 : last_result.provider + 1;
    if (first_table < TAGE_TABLES) {
      const auto candidate_count = TAGE_TABLES - first_table;
      allocation_random ^= allocation_random << 13;
      allocation_random ^= allocation_random >> 17;
      allocation_random ^= allocation_random << 5;
      // Usually start at the next longer table; skip one bank on 1/4 of
      // attempts. Try the skipped bank last before declaring failure.
      const std::size_t skip = candidate_count > 1 && (allocation_random & 3u) == 0 ? 1 : 0;
      std::size_t allocations = 0;
      for (std::size_t offset = 0; offset < candidate_count; ++offset) {
        const auto table = first_table + ((skip + offset) % candidate_count);
        const auto index = last_result.indices[table];
        if (tage_tags[table][index] == 0 || tage_usefulness[table][index].value() == 0) {
          tage_tags[table][index] = last_result.tags[table];
          tage_counters[table][index] = taken ? 0 : -1;
          tage_usefulness[table][index] = 0;
          if (++allocations == 2) {
            break;
          }
        }
      }
      if (allocations == 0) {
        // Release stale protection under allocation pressure as well as via
        // periodic aging. No useful entry is overwritten on this attempt.
        for (auto table = first_table; table < TAGE_TABLES; ++table) {
          tage_usefulness[table][last_result.indices[table]]--;
        }
      }
    }
  }

  auto& loop = loop_table[last_result.loop_index];
  if (!loop.valid || loop.tag != last_result.pc_tag) {
    loop = loop_entry{};
    loop.valid = true;
    loop.tag = last_result.pc_tag;
  }

  if (last_result.loop_prediction_valid && last_result.loop_prediction != last_result.pre_loop_prediction) {
    if (last_result.loop_prediction == taken) {
      loop.chooser++;
    } else {
      loop.chooser--;
    }
  }

  if (taken) {
    if (loop.trip_count != 0 && loop.current_count >= loop.trip_count) {
      loop.confidence = 0;
    }
    if (loop.current_count < std::numeric_limits<uint32_t>::max()) {
      ++loop.current_count;
    }
  } else {
    if (loop.current_count >= 2 && loop.trip_count == loop.current_count) {
      if (last_result.loop_prediction_valid && !last_result.loop_prediction) {
        loop.confidence++;
      }
    } else {
      loop.trip_count = loop.current_count;
      loop.confidence = 0;
    }
    if (loop.current_count < 2) {
      loop.trip_count = 0;
      loop.confidence = 0;
    }
    loop.current_count = 0;
  }
}
