#ifndef BRANCH_MY_PERCEPTRON_H
#define BRANCH_MY_PERCEPTRON_H

#include <array>
#include <cstdint>
#include <tuple>
#include <vector>

#include "folded_shift_register.h"
#include "modules.h"
#include "msl/bits.h"
#include "msl/fwcounter.h"

class my_perceptron : champsim::modules::branch_predictor
{
  using bits = champsim::data::bits;                 // saves some typing
  constexpr static std::size_t NTABLES = 16;         // this many tables
  constexpr static bits MAXHIST{232};                // maximum history length
  constexpr static bits MINHIST{3};                  // minimum history length (for table 1; table 0 is biases)
  constexpr static std::size_t TABLE_SIZE = 1 << 14; // 14-bit indices reduce aliasing between long histories
  constexpr static bits TABLE_INDEX_BITS{champsim::msl::lg2(TABLE_SIZE)};
  constexpr static int THRESHOLD = 1;

  // A small per-PC local history predictor complements the global perceptron.
  constexpr static std::size_t LOCAL_HISTORY_SIZE = 1 << 12;
  constexpr static bits LOCAL_HISTORY_INDEX_BITS{champsim::msl::lg2(LOCAL_HISTORY_SIZE)};
  constexpr static std::size_t LOCAL_PATTERN_SIZE = 1 << 12;
  constexpr static std::size_t CHOOSER_SIZE = TABLE_SIZE;
  constexpr static std::size_t LOOP_TABLE_SIZE = 1 << 12;

  constexpr static std::array<bits, NTABLES> history_lengths = {
      bits{},   MINHIST,  bits{4},  bits{6},  bits{8},  bits{10},  bits{14},  bits{19},
      bits{26}, bits{36}, bits{49}, bits{67}, bits{91}, bits{125}, bits{170}, MAXHIST}; // geometric global history lengths

  // tables of 8-bit weights
  std::array<std::array<champsim::msl::sfwcounter<8>, TABLE_SIZE>, NTABLES> tables{};

  // words that store the global history
  using history_type = folded_shift_register<TABLE_INDEX_BITS>;
  std::array<history_type, NTABLES> ghist_words = []() {
    decltype(ghist_words) retval;
    std::transform(std::cbegin(history_lengths), std::cend(history_lengths), std::begin(retval), [](const auto len) { return history_type{len}; });
    return retval;
  }();

  std::array<uint16_t, LOCAL_HISTORY_SIZE> local_histories{};
  std::array<champsim::msl::sfwcounter<3>, LOCAL_PATTERN_SIZE> local_pattern_counters{};
  std::array<champsim::msl::sfwcounter<3>, CHOOSER_SIZE> bias_counters{};
  std::array<champsim::msl::fwcounter<3>, CHOOSER_SIZE> chooser{};

  struct loop_entry {
    uint64_t tag = 0;
    uint32_t trip_count = 0;
    uint32_t current_count = 0;
    champsim::msl::fwcounter<2> confidence{};
    champsim::msl::fwcounter<2> chooser{};
    bool valid = false;
  };
  std::array<loop_entry, LOOP_TABLE_SIZE> loop_table{};

  int theta = 10;
  int tc = 0; // counter for threshold setting algorithm

  struct perceptron_result {
    std::array<uint64_t, std::tuple_size_v<decltype(history_lengths)>> indices = {}; // remember the indices into the tables from prediction to update
    int yout = 0;                                                                    // global perceptron sum
    uint64_t local_history_index = 0;
    uint64_t local_pattern_index = 0;
    uint64_t chooser_index = 0;
    uint64_t bias_index = 0;
    uint16_t local_history_value = 0;
    uint64_t loop_index = 0;
    uint64_t pc_tag = 0;
    bool global_prediction = false;
    bool bias_prediction = false;
    bool local_prediction = false;
    bool base_prediction = false;
    bool use_local = false;
    bool loop_prediction_valid = false;
    bool loop_prediction = false;
    bool use_loop = false;
  };

  perceptron_result last_result{};

public:
  using branch_predictor::branch_predictor;
  bool predict_branch(champsim::address pc);
  void last_branch_result(champsim::address pc, champsim::address branch_target, bool taken, uint8_t branch_type);
  void adjust_threshold(bool correct);
};

#endif
