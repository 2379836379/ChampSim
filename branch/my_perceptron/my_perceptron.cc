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
  last_result = result;
  return result.yout >= THRESHOLD;
}

void my_perceptron::last_branch_result(champsim::address pc, champsim::address branch_target, bool taken, uint8_t branch_type)
{
  if (branch_type != BRANCH_CONDITIONAL && branch_type != BRANCH_OTHER) {
    return;
  }

  for (auto& hist : ghist_words) {
    hist.push_back(taken);
  }

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
