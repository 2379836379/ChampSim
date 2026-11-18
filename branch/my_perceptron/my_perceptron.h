#ifndef BRANCH_MY_PERCEPTRON_H
#define BRANCH_MY_PERCEPTRON_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "folded_shift_register.h"
#include "modules.h"
#include "msl/bits.h"
#include "msl/fwcounter.h"

// The module name is kept for configuration compatibility; the implementation
// is a standalone TAGE predictor with a bimodal base table.
class my_perceptron : champsim::modules::branch_predictor
{
  friend struct my_perceptron_test_access;
  using bits = champsim::data::bits;

  constexpr static std::size_t BASE_TABLE_SIZE = 1 << 14;
  constexpr static std::size_t ALT_SELECTOR_SIZE = 1 << 14;
  constexpr static std::size_t TAGE_TABLES = 12;
  constexpr static std::size_t TAGE_TABLE_SIZE = 1 << 13;
  constexpr static std::size_t SC_TABLES = 8;
  constexpr static std::size_t SC_TABLE_SIZE = 1 << 13;
  constexpr static int SC_THRESHOLD = 12;
  constexpr static bits TAGE_INDEX_BITS{champsim::msl::lg2(TAGE_TABLE_SIZE)};
  constexpr static bits TAGE_TAG_BITS{12};
  constexpr static std::size_t TAGE_TAG_MASK = (1 << 12) - 1;
  constexpr static std::size_t LOOP_TABLE_SIZE = 1 << 12;

  constexpr static std::array<bits, TAGE_TABLES> history_lengths = {
      bits{4},    bits{8},    bits{16},   bits{32},   bits{64},   bits{128},
      bits{256},  bits{512},  bits{1024}, bits{2048}, bits{4096}, bits{8192}};
  constexpr static std::array<bits, SC_TABLES> sc_history_lengths = {
      bits{0}, bits{4}, bits{8}, bits{16}, bits{32}, bits{64}, bits{128}, bits{256}};

  using tage_index_history = folded_shift_register<TAGE_INDEX_BITS>;
  using tage_tag_history = folded_shift_register<TAGE_TAG_BITS>;

  // All large tables are heap-backed so generated_environment compilation does
  // not materialize multi-megabyte predictor objects in template code.
  using tage_counter_table = std::array<champsim::msl::sfwcounter<3>, TAGE_TABLE_SIZE>;
  using tage_tag_table = std::array<uint16_t, TAGE_TABLE_SIZE>;
  using tage_usefulness_table = std::array<champsim::msl::fwcounter<2>, TAGE_TABLE_SIZE>;
  using sc_counter_table = std::array<champsim::msl::sfwcounter<6>, SC_TABLE_SIZE>;

  std::vector<champsim::msl::fwcounter<2>> base_table = std::vector<champsim::msl::fwcounter<2>>(BASE_TABLE_SIZE);
  // Nonnegative prefers alternate, but only for weak, unproven providers.
  // Indexing by PC and provider prevents unrelated branches from training one
  // global use-alt decision in opposite directions.
  std::vector<champsim::msl::sfwcounter<4>> use_alt_on_na = std::vector<champsim::msl::sfwcounter<4>>(ALT_SELECTOR_SIZE);
  std::vector<tage_counter_table> tage_counters = std::vector<tage_counter_table>(TAGE_TABLES);
  std::vector<tage_tag_table> tage_tags = std::vector<tage_tag_table>(TAGE_TABLES);
  std::vector<tage_usefulness_table> tage_usefulness = std::vector<tage_usefulness_table>(TAGE_TABLES);
  std::vector<sc_counter_table> sc_counters = std::vector<sc_counter_table>(SC_TABLES);

  std::array<tage_index_history, TAGE_TABLES> index_histories = []() {
    decltype(index_histories) retval;
    std::transform(std::cbegin(history_lengths), std::cend(history_lengths), std::begin(retval), [](const auto length) {
      return tage_index_history{length};
    });
    return retval;
  }();
  std::array<tage_tag_history, TAGE_TABLES> tag_histories = []() {
    decltype(tag_histories) retval;
    std::transform(std::cbegin(history_lengths), std::cend(history_lengths), std::begin(retval), [](const auto length) {
      return tage_tag_history{length};
    });
    return retval;
  }();
  std::array<tage_index_history, SC_TABLES> sc_histories = []() {
    decltype(sc_histories) retval;
    std::transform(std::cbegin(sc_history_lengths), std::cend(sc_history_lengths), std::begin(retval), [](const auto length) {
      return tage_index_history{length};
    });
    return retval;
  }();
  uint32_t usefulness_clock = 0;
  uint32_t allocation_random = 0x9e3779b9u;

  struct loop_entry {
    uint64_t tag = 0;
    uint32_t trip_count = 0;
    uint32_t current_count = 0;
    champsim::msl::fwcounter<2> confidence{};
    champsim::msl::fwcounter<2> chooser{};
    bool valid = false;
  };
  std::array<loop_entry, LOOP_TABLE_SIZE> loop_table{};

  struct tage_result {
    uint64_t base_index = 0;
    bool base_prediction = false;
    bool tage_prediction = false;
    bool provider_weak = false;
    bool provider_unuseful = false;
    bool used_alternate = false;
    uint64_t alt_selector_index = 0;
    std::array<uint32_t, SC_TABLES> sc_indices{};
    int sc_sum = 0;
    bool sc_prediction = false;
    std::array<uint32_t, TAGE_TABLES> indices{};
    std::array<uint16_t, TAGE_TABLES> tags{};
    std::size_t provider = TAGE_TABLES;
    std::size_t alternate = TAGE_TABLES;
    bool provider_prediction = false;
    bool alternate_prediction = false;
    bool pre_loop_prediction = false;
    uint64_t loop_index = 0;
    uint64_t pc_tag = 0;
    bool loop_prediction_valid = false;
    bool loop_prediction = false;
    bool use_loop = false;
  };

  tage_result last_result{};

public:
  using branch_predictor::branch_predictor;
  bool predict_branch(champsim::address pc);
  void last_branch_result(champsim::address pc, champsim::address branch_target, bool taken, uint8_t branch_type);
};

#endif
