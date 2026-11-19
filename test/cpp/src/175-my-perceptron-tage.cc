#include <catch.hpp>

#include "../../../branch/my_perceptron/my_perceptron.h"
#include "instruction.h"

struct my_perceptron_test_access {
  static constexpr champsim::address pc{0x123456};

  static void install(my_perceptron& p, std::size_t bank, int counter, int usefulness = 0)
  {
    p.predict_branch(pc);
    const auto index = p.last_result.indices[bank];
    p.tage_tags[bank][index] = p.last_result.tags[bank];
    p.tage_counters[bank][index] = counter;
    p.tage_usefulness[bank][index] = usefulness;
  }

  static void update(my_perceptron& p, bool taken)
  {
    p.last_branch_result(pc, champsim::address{}, taken, BRANCH_CONDITIONAL);
  }

  static void selection()
  {
    my_perceptron p{nullptr};
    install(p, 2, 2);
    REQUIRE(p.predict_branch(pc)); // Strong provider beats the initially NT base.
    REQUIRE_FALSE(p.last_result.used_alternate);

    install(p, 2, 0);
    REQUIRE_FALSE(p.predict_branch(pc)); // Initially favor alternate for weak u=0.
    REQUIRE(p.last_result.used_alternate);
    const auto selector_index = p.last_result.alt_selector_index;
    update(p, true);
    REQUIRE(p.use_alt_on_na[selector_index].value() < 0);
    // The correct provider should mature without redundant allocation.
    for (std::size_t bank = 3; bank < p.TAGE_TABLES; ++bank)
      REQUIRE(p.tage_tags[bank][p.last_result.indices[bank]] == 0);

    install(p, 2, 0);
    REQUIRE(p.predict_branch(pc)); // Learned to trust weak provider.
    REQUIRE_FALSE(p.last_result.used_alternate);
    update(p, false);
    REQUIRE(p.use_alt_on_na[selector_index].value() == 0);

    // A useful provider must bypass even a strongly alternate-biased selector.
    install(p, p.TAGE_TABLES - 1, 0, 1);
    p.use_alt_on_na[p.last_result.alt_selector_index] = 7;
    REQUIRE(p.predict_branch(pc));
    REQUIRE_FALSE(p.last_result.used_alternate);
  }

  static void alternate_training()
  {
    my_perceptron p{nullptr};
    install(p, 1, -2);
    install(p, 3, 0);
    REQUIRE_FALSE(p.predict_branch(pc));
    REQUIRE(p.last_result.alternate == 1);
    const auto saved = p.last_result;
    update(p, true);
    REQUIRE(p.tage_counters[3][saved.indices[3]].value() == 1);
    REQUIRE(p.tage_counters[1][saved.indices[1]].value() == -1);
    REQUIRE(p.base_table[saved.base_index].value() == 0); // No double training of base.
    REQUIRE(p.tage_usefulness[3][saved.indices[3]].value() == 1);

    my_perceptron unproven{nullptr};
    install(unproven, 1, -2);
    install(unproven, 3, 2);
    REQUIRE(unproven.predict_branch(pc));
    REQUIRE_FALSE(unproven.last_result.used_alternate);
    const auto unproven_saved = unproven.last_result;
    update(unproven, true);
    REQUIRE(unproven.tage_counters[1][unproven_saved.indices[1]].value() == -2);

    my_perceptron useful{nullptr};
    install(useful, 1, -2);
    install(useful, 3, 2, 2);
    useful.predict_branch(pc);
    const auto useful_saved = useful.last_result;
    update(useful, true);
    REQUIRE(useful.tage_counters[1][useful_saved.indices[1]].value() == -2);

    my_perceptron fallback{nullptr};
    install(fallback, 2, 0);
    fallback.predict_branch(pc);
    const auto base_index = fallback.last_result.base_index;
    update(fallback, true);
    REQUIRE(fallback.base_table[base_index].value() == 1);
  }

  static void allocation()
  {
    my_perceptron p{nullptr};
    p.predict_branch(pc);
    const auto saved = p.last_result;
    update(p, true); // Cold miss must allocate in the first or second bank.
    unsigned allocated = 0;
    for (std::size_t bank = 0; bank < p.TAGE_TABLES; ++bank) {
      if (p.tage_tags[bank][saved.indices[bank]] != 0) {
        REQUIRE(bank <= 1);
        REQUIRE(p.tage_counters[bank][saved.indices[bank]].value() == 0);
        ++allocated;
      }
    }
    REQUIRE(allocated == 2);

    my_perceptron blocked{nullptr};
    install(blocked, 2, -2);
    blocked.predict_branch(pc);
    const auto blocked_saved = blocked.last_result;
    for (std::size_t bank = 3; bank < blocked.TAGE_TABLES; ++bank) {
      const auto index = blocked_saved.indices[bank];
      blocked.tage_tags[bank][index] = static_cast<uint16_t>(blocked_saved.tags[bank] + 1);
      blocked.tage_usefulness[bank][index] = 2;
    }
    update(blocked, true);
    for (std::size_t bank = 3; bank < blocked.TAGE_TABLES; ++bank) {
      const auto index = blocked_saved.indices[bank];
      REQUIRE(blocked.tage_usefulness[bank][index].value() == 1);
      REQUIRE(blocked.tage_tags[bank][index] == blocked_saved.tags[bank] + 1);
    }

    my_perceptron longest{nullptr};
    constexpr auto last_bank = my_perceptron::TAGE_TABLES - 1;
    install(longest, last_bank, -2);
    longest.predict_branch(pc);
    const auto longest_saved = longest.last_result;
    update(longest, true); // No longer bank: no out-of-range access or modulo zero.
    REQUIRE(longest.tage_counters[last_bank][longest_saved.indices[last_bank]].value() == -1);
    for (std::size_t bank = 0; bank < last_bank; ++bank)
      REQUIRE(longest.tage_tags[bank][longest_saved.indices[bank]] == 0);
  }

  static void statistical_corrector()
  {
    my_perceptron p{nullptr};
    REQUIRE_FALSE(p.predict_branch(pc)); // Cold TAGE/base predicts not-taken.
    auto saved = p.last_result;
    for (std::size_t table = 0; table < p.SC_TABLES; ++table)
      p.sc_counters[table][saved.sc_indices[table]] = 2;
    for (std::size_t table = p.SC_TABLES; table < p.SC_TOTAL_TABLES; ++table)
      p.sc_counters[table][saved.sc_indices[table]] = 1;
    REQUIRE(p.predict_branch(pc)); // Strong short-history vote overrides TAGE.
    REQUIRE(p.last_result.sc_sum == 29);
    REQUIRE(p.last_result.used_sc);

    p.sc_selectors[p.last_result.sc_selector_context][p.last_result.sc_selector_index] = -7;
    REQUIRE_FALSE(p.predict_branch(pc)); // A trained selector can reject SC.
    REQUIRE_FALSE(p.last_result.used_sc);

    my_perceptron learning{nullptr};
    REQUIRE_FALSE(learning.predict_branch(pc));
    saved = learning.last_result;
    for (std::size_t table = 0; table < learning.SC_TABLES; ++table)
      learning.sc_counters[table][saved.sc_indices[table]] = 2;
    REQUIRE(learning.predict_branch(pc));
    saved = learning.last_result;
    update(learning, false);
    for (std::size_t table = 0; table < learning.SC_TABLES; ++table)
      REQUIRE(learning.sc_counters[table][saved.sc_indices[table]].value() == 1);
    REQUIRE(learning.sc_selectors[saved.sc_selector_context][saved.sc_selector_index].value() == -1);
  }

  static void interleaved_local_pattern()
  {
    my_perceptron p{nullptr};
    constexpr champsim::address noise{0x8000};
    p.predict_branch(pc);
    const auto cold = p.last_result;
    for (unsigned i = 0; i < 64; ++i) {
      // Flush every global SC history, without changing the target's local history.
      for (unsigned j = 0; j < 256; ++j) {
        p.predict_branch(noise);
        p.last_branch_result(noise, champsim::address{}, false, BRANCH_CONDITIONAL);
      }
      const bool taken = i % 2 == 0;
      p.predict_branch(pc);
      if (i >= 56) {
        REQUIRE(std::equal(cold.sc_indices.begin(), cold.sc_indices.begin() + p.SC_TABLES, p.last_result.sc_indices.begin()));
        REQUIRE(p.last_result.sc_prediction == taken);
        REQUIRE(std::abs(p.last_result.sc_sum) >= p.SC_THRESHOLD);
      }
      update(p, taken);
    }
  }

  static void context_selection()
  {
    my_perceptron p{nullptr};
    p.predict_branch(pc);
    const auto cold = p.last_result;
    // At one PC, reject SC for strong NT but allow it for weak NT.
    p.sc_counters[0][cold.sc_indices[0]] = p.SC_THRESHOLD;
    REQUIRE(p.predict_branch(pc));
    update(p, false);
    p.sc_counters[0][cold.sc_indices[0]] = 31;
    REQUIRE_FALSE(p.predict_branch(pc));
    REQUIRE(p.last_result.sc_disagrees);
    REQUIRE(std::abs(p.last_result.sc_sum) >= p.SC_THRESHOLD);
    REQUIRE_FALSE(p.last_result.used_sc);
    p.base_table[cold.base_index] = 1;
    REQUIRE(p.predict_branch(pc));
    REQUIRE(p.last_result.used_sc);

    // All four contexts use the same PC slot but learn independently.
    for (unsigned base = 0; base < 4; ++base) {
      p.base_table[cold.base_index] = base;
      p.predict_branch(pc);
      constexpr std::array<unsigned, 4> expected{0, 1, 3, 2};
      REQUIRE(p.last_result.sc_selector_context == expected[base]);
      REQUIRE(p.last_result.sc_selector_index == cold.sc_selector_index);
    }

    install(p, 1, 3);  // Strong taken alternate.
    install(p, 3, -1); // Weak NT provider.
    p.sc_counters[0][cold.sc_indices[0]] = -32;
    REQUIRE_FALSE(p.predict_branch(pc));
    REQUIRE(p.last_result.used_alternate);
    REQUIRE(p.last_result.used_sc);
    REQUIRE(p.last_result.sc_selector_context == 2);
    update(p, true);
    REQUIRE(p.sc_selectors[2][cold.sc_selector_index].value() == -1);
    REQUIRE(p.sc_selectors[1][cold.sc_selector_index].value() == 0);
  }

  static void path_history_separates_contexts()
  {
    my_perceptron p{nullptr};
    p.predict_branch(pc);
    const auto first = p.last_result;
    update(p, true);
    REQUIRE(p.path_history != 0);
    REQUIRE(p.path_history_alt != 0);
    REQUIRE(p.path_history != p.path_history_alt);
    p.predict_branch(pc);
    const auto second = p.last_result;
    bool changed = false;
    for (std::size_t bank = 0; bank < p.TAGE_TABLES; ++bank)
      changed = changed || first.indices[bank] != second.indices[bank] || first.tags[bank] != second.tags[bank];
    REQUIRE(changed);
  }

  static void intermediate_history()
  {
    my_perceptron left{nullptr}, right{nullptr};
    // Identical addresses/path and last 8 outcomes; the ninth outcome differs.
    for (int i = 0; i < 9; ++i) {
      left.predict_branch(pc);
      right.predict_branch(pc);
      update(left, i == 0);
      update(right, false);
    }
    left.predict_branch(pc);
    right.predict_branch(pc);
    REQUIRE(left.last_result.indices[1] == right.last_result.indices[1]);
    REQUIRE(left.last_result.tags[1] == right.last_result.tags[1]);
    REQUIRE(left.last_result.indices[2] != right.last_result.indices[2]);
    REQUIRE(left.last_result.tags[2] != right.last_result.tags[2]);

    // The new 12-outcome bank can provide a prediction over an 8-outcome alternate.
    my_perceptron p{nullptr};
    install(p, 1, -2);
    install(p, 2, 2);
    REQUIRE(p.predict_branch(pc));
    REQUIRE(p.last_result.provider == 2);
    REQUIRE(p.last_result.alternate == 1);
    const auto saved = p.last_result;
    update(p, false);
    unsigned allocated = 0;
    for (std::size_t bank = 3; bank < p.TAGE_TABLES; ++bank)
      allocated += p.tage_tags[bank][saved.indices[bank]] == saved.tags[bank] ? 1u : 0u;
    REQUIRE(allocated == 2);
  }

  // Set a controlled SC margin without bypassing prediction-time bookkeeping.
  static void set_sc_score(my_perceptron& p, int score)
  {
    p.predict_branch(pc);
    for (std::size_t table = 0; table < p.SC_TOTAL_TABLES; ++table)
      p.sc_counters[table][p.last_result.sc_indices[table]] = 0;
    p.sc_counters[0][p.last_result.sc_indices[0]] = score;
    p.predict_branch(pc);
  }

  static void adaptive_training()
  {
    my_perceptron p{nullptr};
    REQUIRE(p.sc_train_threshold == 24);
    set_sc_score(p, 11);
    REQUIRE_FALSE(p.last_result.used_sc);
    set_sc_score(p, 12);
    REQUIRE(p.last_result.used_sc); // Override still uses 12, not theta.
    auto saved = p.last_result;
    update(p, true);
    for (std::size_t table = 0; table < p.SC_TOTAL_TABLES; ++table)
      REQUIRE(p.sc_counters[table][saved.sc_indices[table]].value() == (table == 0 ? 13 : 1));
    REQUIRE(p.sc_threshold_signal == -1);

    set_sc_score(p, 24);
    saved = p.last_result;
    update(p, true); // Correct at the margin: no training or adaptation.
    REQUIRE(p.sc_counters[0][saved.sc_indices[0]].value() == 24);
    REQUIRE(p.sc_threshold_signal == -1);
    set_sc_score(p, -31);
    saved = p.last_result;
    update(p, true); // Wrong even beyond theta: always train.
    REQUIRE(p.sc_counters[0][saved.sc_indices[0]].value() == -30);
    REQUIRE(p.sc_threshold_signal == 0); // Opposite evidence cancels.

    auto repeat = [&](int score, int count) {
      for (int i = 0; i < count; ++i) {
        set_sc_score(p, score);
        update(p, true);
      }
    };
    repeat(-31, p.SC_ADAPT_PERIOD - 1);
    REQUIRE(p.sc_train_threshold == 24);
    repeat(-31, 1);
    REQUIRE(p.sc_train_threshold == 25);
    REQUIRE(p.sc_threshold_signal == 0);
    repeat(1, p.SC_ADAPT_PERIOD);
    REQUIRE(p.sc_train_threshold == 24);

    p.sc_train_threshold = p.SC_TRAIN_MAX;
    repeat(-31, p.SC_ADAPT_PERIOD * 2);
    REQUIRE(p.sc_train_threshold == p.SC_TRAIN_MAX);
    REQUIRE(p.sc_threshold_signal == 0);
    p.sc_train_threshold = p.SC_TRAIN_MIN;
    repeat(1, p.SC_ADAPT_PERIOD * 2);
    REQUIRE(p.sc_train_threshold == p.SC_TRAIN_MIN);
    REQUIRE(p.sc_threshold_signal == 0);
  }

  static void run_length_context()
  {
    my_perceptron p{nullptr};
    auto step = [&](bool taken) {
      p.predict_branch(pc);
      update(p, taken);
    };
    for (int i = 0; i < 30; ++i)
      step(true);
    p.predict_branch(pc);
    const auto at30 = p.last_result;
    for (int i = 0; i < 30; ++i)
      step(true);
    p.predict_branch(pc);
    const auto at60 = p.last_result;
    REQUIRE(p.sc_run_lengths[at60.sc_local_slot] == 60);
    // Existing local features cannot distinguish these two positions.
    for (std::size_t table = p.SC_TABLES; table < p.SC_RUN_OFFSET; ++table)
      REQUIRE(at30.sc_indices[table] == at60.sc_indices[table]);
    for (std::size_t table = p.SC_RUN_OFFSET; table < p.SC_PATH_OFFSET; ++table)
      REQUIRE(at30.sc_indices[table] != at60.sc_indices[table]);

    // Training must use prediction-time indices, before the direction resets.
    set_sc_score(p, 1);
    const auto saved = p.last_result;
    update(p, false);
    REQUIRE(p.sc_run_lengths[saved.sc_local_slot] == 1);
    for (std::size_t table = p.SC_RUN_OFFSET; table < p.SC_TOTAL_TABLES; ++table)
      REQUIRE(p.sc_counters[table][saved.sc_indices[table]].value() == -1);
    step(false);
    REQUIRE(p.sc_run_lengths[saved.sc_local_slot] == 2);
    for (unsigned i = 0; i < p.SC_RUN_MAX + 10u; ++i)
      step(false);
    REQUIRE(p.sc_run_lengths[saved.sc_local_slot] == p.SC_RUN_MAX);
    step(true);
    REQUIRE(p.sc_run_lengths[saved.sc_local_slot] == 1);
  }

  static void path_windows()
  {
    my_perceptron left{nullptr}, right{nullptr};
    auto step = [](my_perceptron& p, champsim::address where) {
      p.predict_branch(where);
      p.last_branch_result(where, champsim::address{}, false, BRANCH_CONDITIONAL);
    };
    step(left, champsim::address{0x4000});
    step(right, champsim::address{0x4010});
    for (unsigned age = 0; age <= 16; ++age) {
      left.predict_branch(pc);
      right.predict_branch(pc);
      const auto& a = left.last_result.sc_indices;
      const auto& b = right.last_result.sc_indices;
      REQUIRE(std::equal(a.begin(), a.begin() + left.SC_PATH_OFFSET, b.begin()));
      for (std::size_t i = 0; i < left.sc_path_bits.size(); ++i) {
        const auto bank = left.SC_PATH_OFFSET + i;
        REQUIRE((a[bank] != b[bank]) == (age < left.sc_path_bits[i] / 4));
      }
      step(left, champsim::address{0x8020});
      step(right, champsim::address{0x8020});
    }
  }

  static void long_loops()
  {
    for (bool body : {false, true}) {
      for (unsigned period : {256u, 65536u, 65538u}) {
        CAPTURE(body, period);
        my_perceptron::loop_entry e{};
        e.body_direction = !body; // Cold start on the exception.
        e.observe(!body);
        for (int cycle = 0; cycle < 3; ++cycle) {
          bool correct_body = true;
          for (unsigned i = 1; i < period; ++i) {
            if (cycle == 2 && e.predict() != body)
              correct_body = false;
            e.observe(body);
          }
          REQUIRE(correct_body);
          if (cycle == 2)
            REQUIRE(e.predict() == !body);
          e.observe(!body);
          REQUIRE(e.trip_count == period - 1);
          REQUIRE(e.confidence.value() == cycle + 1);
        }

        // An early exception seeds a new candidate but cannot override yet.
        e.observe(body);
        e.observe(body);
        e.observe(!body);
        REQUIRE(e.trip_count == 2);
        REQUIRE(e.confidence.value() == 1);
        e.observe(body);
        e.observe(body);
        e.observe(body); // Missing exception invalidates even the candidate.
        REQUIRE(e.trip_count == 0);
        e.observe(!body);
        e.observe(!body);
        REQUIRE(e.body_direction == !body);
        REQUIRE(e.current_count == 2);
        REQUIRE(e.trip_count == 0);
        e.current_count = std::numeric_limits<uint32_t>::max();
        e.observe(!body);
        e.observe(body);
        REQUIRE(e.trip_count == 0);
        REQUIRE(e.current_count == 0);
      }
    }
  }

  static void loop_override()
  {
    for (bool body : {false, true}) {
      my_perceptron p{nullptr};
      auto step = [&](bool taken) {
        p.predict_branch(pc);
        update(p, taken);
      };
      for (int cycle = 0; cycle < 3; ++cycle) {
        for (int i = 0; i < 7; ++i)
          step(body);
        // Force the underlying prediction to miss the exception so that the
        // loop chooser can earn trust on a disagreement, even in shadow mode.
        set_sc_score(p, body ? 31 : -32);
        p.sc_selectors[p.last_result.sc_selector_context][p.last_result.sc_selector_index] = 7;
        const auto prediction = p.predict_branch(pc);
        REQUIRE(p.last_result.pre_loop_prediction == body);
        REQUIRE(p.last_result.use_loop == (cycle == 2));
        REQUIRE(prediction == (cycle == 2 ? !body : body));
        update(p, cycle == 2 ? body : !body); // Last exception is missing.
      }
      const auto after_miss = p.predict_branch(pc);
      REQUIRE_FALSE(p.last_result.use_loop);
      REQUIRE(after_miss == p.last_result.pre_loop_prediction);
      update(p, !body);
      constexpr champsim::address alias{0x123457};
      const auto slot = p.last_result.loop_index;
      p.predict_branch(alias);
      REQUIRE(p.last_result.loop_index == slot);
      REQUIRE_FALSE(p.last_result.loop_prediction_valid);
      p.last_branch_result(alias, {}, !body, BRANCH_CONDITIONAL);
      REQUIRE(p.loop_table[slot].current_count == 1);
      REQUIRE(p.loop_table[slot].body_direction == !body);
      REQUIRE(p.loop_table[slot].trip_count == 0);
      REQUIRE(p.loop_table[slot].chooser.value() == 0);
    }
  }

  static void gshare_base()
  {
    my_perceptron p{nullptr};
    REQUIRE_FALSE(p.predict_branch(pc)); // Tie starts with the original bimodal base.
    const auto initial = p.last_result;
    REQUIRE_FALSE(initial.bimodal_prediction);
    REQUIRE_FALSE(initial.gshare_prediction);
    REQUIRE(initial.base_prediction == initial.bimodal_prediction);

    p.gshare_table[initial.gshare_index] = 2;
    REQUIRE_FALSE(p.predict_branch(pc)); // Chooser still trusts bimodal.
    update(p, true);
    REQUIRE(p.base_chooser[initial.base_chooser_index].value() == 1);
    REQUIRE(p.base_table[initial.base_index].value() == 1);
    REQUIRE(p.gshare_table[initial.gshare_index].value() == 3);

    // Clear TAGE allocations so the next prediction exercises its base fallback.
    for (auto& table : p.tage_tags)
      table.fill(0);
    p.predict_branch(pc);
    const auto next_index = p.last_result.gshare_index;
    REQUIRE(next_index != initial.gshare_index);
    REQUIRE(p.gshare_table[next_index].value() == 0);
    p.gshare_table[next_index] = 2;
    p.predict_branch(pc);
    REQUIRE(p.last_result.gshare_prediction);
    REQUIRE(p.last_result.base_prediction == p.last_result.gshare_prediction);

    // A correct bimodal disagreement trains the chooser back the other way.
    update(p, false);
    REQUIRE(p.base_chooser[initial.base_chooser_index].value() == 0);

    my_perceptron fallback{nullptr};
    install(fallback, 2, -1);
    fallback.predict_branch(pc);
    const auto alt = fallback.last_result;
    REQUIRE(alt.used_alternate);
    REQUIRE(alt.alternate == fallback.TAGE_TABLES);
    fallback.gshare_table[alt.gshare_index] = 2;
    fallback.predict_branch(pc);
    update(fallback, true);
    REQUIRE(fallback.gshare_table[alt.gshare_index].value() == 3);
    REQUIRE(fallback.base_chooser[alt.base_chooser_index].value() == 1);
  }
};

TEST_CASE("TAGE learns alternate selection only for weak unproven providers", "[my_perceptron]")
{
  my_perceptron_test_access::selection();
}

TEST_CASE("TAGE trains the appropriate alternate using prediction-time state", "[my_perceptron]")
{
  my_perceptron_test_access::alternate_training();
}

TEST_CASE("TAGE allocates nearby and ages protected candidates on failure", "[my_perceptron]")
{
  my_perceptron_test_access::allocation();
}

TEST_CASE("TAGE statistical corrector overrides and learns short-history patterns", "[my_perceptron]")
{
  my_perceptron_test_access::statistical_corrector();
}

TEST_CASE("SC learns alternating local outcomes across identical global contexts", "[my_perceptron]")
{
  my_perceptron_test_access::interleaved_local_pattern();
}

TEST_CASE("SC chooser separates direction and confidence of the selected TAGE source", "[my_perceptron]")
{
  my_perceptron_test_access::context_selection();
}

TEST_CASE("TAGE path history separates identical outcome contexts", "[my_perceptron]")
{
  my_perceptron_test_access::path_history_separates_contexts();
}

TEST_CASE("Intermediate TAGE histories distinguish and learn beyond shorter contexts", "[my_perceptron]")
{
  my_perceptron_test_access::intermediate_history();
}

TEST_CASE("SC adapts training margin with bounded accumulated evidence", "[my_perceptron]")
{
  my_perceptron_test_access::adaptive_training();
}

TEST_CASE("SC distinguishes long run positions and trains pre-update contexts", "[my_perceptron]")
{
  my_perceptron_test_access::run_length_context();
}

TEST_CASE("SC path windows remember predecessor addresses for 4, 8 and 16 branches", "[my_perceptron]")
{
  my_perceptron_test_access::path_windows();
}

TEST_CASE("TAGE base fallback selects and trains bimodal or gshare", "[my_perceptron]")
{
  my_perceptron_test_access::gshare_base();
}

TEST_CASE("Loop entries learn long periods in either direction", "[my_perceptron]")
{
  my_perceptron_test_access::long_loops();
}

TEST_CASE("Loop chooser overrides on confirmed periods and falls back on a miss", "[my_perceptron]")
{
  my_perceptron_test_access::loop_override();
}
