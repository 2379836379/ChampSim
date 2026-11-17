#include "my_perceptron.h"
#include "instruction.h"
#include <numeric>

bool my_perceptron::predict_branch(champsim::address pc)
{
  auto get_index = [pc_slice = pc.slice_lower<TABLE_INDEX_BITS>().to<uint64_t>()](const auto& hist) {
    return hist.value() ^ pc_slice; // seed in the PC to spread accesses around (like gshare) XOR in the last word
  };
  perceptron_result result;
  std::transform(std::cbegin(ghist_words), std::cend(ghist_words), std::begin(result.indices), get_index);

  // add the selected weights to the perceptron sum
  result.yout = std::inner_product(std::begin(tables), std::end(tables), std::begin(result.indices), 0, std::plus<>{},
                                   [](const auto& table, const auto& index) { return table.at(index).value(); });

  result.local_history_index = pc.slice_lower<LOCAL_HISTORY_INDEX_BITS>().to<uint64_t>();
  auto local_history = local_histories[result.local_history_index];
  result.local_history_value = local_history;
  result.local_pattern_index = (local_history ^ result.local_history_index) & (LOCAL_PATTERN_SIZE - 1);
  result.chooser_index = pc.slice_lower<TABLE_INDEX_BITS>().to<uint64_t>();
  result.global_prediction = result.yout >= THRESHOLD;
  result.local_prediction = local_pattern_counters[result.local_pattern_index].value() >= 0;
  result.use_local = chooser[result.chooser_index].value() >= 2;
  last_result = result;
  return result.use_local ? result.local_prediction : result.global_prediction;
}

void my_perceptron::last_branch_result(champsim::address pc, champsim::address branch_target, bool taken, uint8_t branch_type)
{
  if (branch_type != BRANCH_CONDITIONAL && branch_type != BRANCH_OTHER) {
    return;
  }

  for (auto& hist : ghist_words) {
    hist.push_back(taken);
  }

  const bool local_correct = (last_result.local_prediction == taken);
  const bool global_correct = (last_result.global_prediction == taken);
  if (taken) {
    local_pattern_counters[last_result.local_pattern_index] += 1;
  } else {
    local_pattern_counters[last_result.local_pattern_index] -= 1;
  }
  if (last_result.local_prediction != last_result.global_prediction) {
    if (local_correct && !global_correct) {
      chooser[last_result.chooser_index] += 1;
    } else if (global_correct && !local_correct) {
      chooser[last_result.chooser_index] -= 1;
    }
  }
  local_histories[last_result.local_history_index] = static_cast<uint16_t>(((last_result.local_history_value << 1) | (taken ? 1u : 0u)) & (LOCAL_HISTORY_SIZE - 1));

  // perceptron learning rule: train if misprediction or weak correct prediction
  bool prediction_correct = (taken == (last_result.yout >= THRESHOLD));
  bool prediction_weak = (std::abs(last_result.yout) < theta);
  if (!prediction_correct || prediction_weak) {
    for (std::size_t i = 0; i < std::size(tables); i++)
      tables[i][last_result.indices[i]] += taken ? 1 : -1; // update weights
    adjust_threshold(prediction_correct);
  }
}

// dynamic threshold setting from Seznec's O-GEHL paper
void my_perceptron::adjust_threshold(bool correct)
{
  constexpr int SPEED = 18; // speed for dynamic threshold setting
  if (!correct) {
    // increase theta after enough mispredictions
    tc++;
    if (tc >= SPEED) {
      theta++;
      tc = 0;
    }
  } else {
    // decrease theta after enough weak but correct predictions
    tc--;
    if (tc <= -SPEED) {
      theta--;
      tc = 0;
    }
  }
}
