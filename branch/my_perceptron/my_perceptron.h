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

  constexpr static std::size_t BASE_TABLE_SIZE = 1 << 16;
  constexpr static std::size_t GSHARE_TABLE_SIZE = 1 << 17;
  constexpr static std::size_t BASE_CHOOSER_SIZE = 1 << 16;
  constexpr static std::size_t ALT_SELECTOR_SIZE = 1 << 16;
  constexpr static std::size_t TAGE_TABLES = 20;
  static_assert(3 * (TAGE_TABLES - 1) + 1 < 64, "Path hash shifts must fit uint64_t");
  constexpr static std::size_t TAGE_TABLE_SIZE = 1 << 14;
  constexpr static std::size_t SC_TABLES = 8;
  constexpr static std::array<unsigned, 3> sc_local_lengths = {4, 10, 20};
  constexpr static std::array<unsigned, 4> sc_alt_local_lengths = {3, 7, 14, 28};
  constexpr static std::array<unsigned, 3> sc_run_bits = {4, 7, 10};
  constexpr static std::size_t SC_RUN_OFFSET = SC_TABLES + sc_local_lengths.size() + sc_alt_local_lengths.size();
  constexpr static std::size_t SC_PATH_OFFSET = SC_RUN_OFFSET + sc_run_bits.size();
  // Four address-hash bits per branch: windows of 4, 8 and 16 branches.
  constexpr static std::array<unsigned, 3> sc_path_bits = {16, 32, 64};
  constexpr static std::size_t SC_TOTAL_TABLES = SC_PATH_OFFSET + sc_path_bits.size();
  constexpr static uint16_t SC_RUN_MAX = (1u << 10) - 1;
  constexpr static std::size_t SC_WEIGHT_TABLE_SIZE = 1 << 17;
  constexpr static std::size_t SC_LOCAL_HISTORY_SIZE = 1 << 14;
  constexpr static std::size_t SC_SELECTOR_SIZE = 1 << 14;
  constexpr static std::size_t SC_SELECTOR_CONTEXTS = 4;
  constexpr static int SC_THRESHOLD = 12;
  constexpr static int SC_TRAIN_MIN = 8;
  constexpr static int SC_TRAIN_MAX = 96;
  constexpr static int SC_ADAPT_PERIOD = 32;
  constexpr static bits TAGE_INDEX_BITS{champsim::msl::lg2(TAGE_TABLE_SIZE)};
  constexpr static bits TAGE_TAG_BITS{15};
  constexpr static auto TAGE_TAG_MASK = champsim::msl::bitmask(TAGE_TAG_BITS);
  constexpr static std::size_t LOOP_TABLE_SIZE = 1 << 12;

  constexpr static std::array<bits, TAGE_TABLES> history_lengths = {
      bits{4},   bits{8},   bits{12},  bits{16},  bits{24},   bits{32},   bits{48},
      bits{64},  bits{96},  bits{128}, bits{192}, bits{256},  bits{384},  bits{512},
      bits{768}, bits{1024}, bits{2048}, bits{3072}, bits{4096}, bits{8192}};
  constexpr static std::array<bits, SC_TABLES> sc_history_lengths = {
      bits{0}, bits{4}, bits{8}, bits{16}, bits{32}, bits{64}, bits{128}, bits{256}};

  using tage_index_history = folded_shift_register<TAGE_INDEX_BITS>;
  using tage_tag_history = folded_shift_register<TAGE_TAG_BITS>;
  using sc_index_history = folded_shift_register<bits{champsim::msl::lg2(SC_WEIGHT_TABLE_SIZE)}>;

  // All large tables are heap-backed so generated_environment compilation does
  // not materialize multi-megabyte predictor objects in template code.
  using tage_counter_table = std::array<champsim::msl::sfwcounter<3>, TAGE_TABLE_SIZE>;
  using tage_tag_table = std::array<uint16_t, TAGE_TABLE_SIZE>;
  using tage_usefulness_table = std::array<champsim::msl::fwcounter<2>, TAGE_TABLE_SIZE>;
  using sc_counter_table = std::array<champsim::msl::sfwcounter<6>, SC_WEIGHT_TABLE_SIZE>;
  using sc_selector_table = std::array<champsim::msl::sfwcounter<4>, SC_SELECTOR_SIZE>;

  std::vector<champsim::msl::fwcounter<2>> base_table = std::vector<champsim::msl::fwcounter<2>>(BASE_TABLE_SIZE);
  std::vector<champsim::msl::fwcounter<2>> gshare_table = std::vector<champsim::msl::fwcounter<2>>(GSHARE_TABLE_SIZE);
  std::vector<champsim::msl::sfwcounter<4>> base_chooser = std::vector<champsim::msl::sfwcounter<4>>(BASE_CHOOSER_SIZE);
  // Nonnegative prefers alternate, but only for weak, unproven providers.
  // Indexing by PC and provider prevents unrelated branches from training one
  // global use-alt decision in opposite directions.
  std::vector<champsim::msl::sfwcounter<4>> use_alt_on_na = std::vector<champsim::msl::sfwcounter<4>>(ALT_SELECTOR_SIZE);
  std::vector<tage_counter_table> tage_counters = std::vector<tage_counter_table>(TAGE_TABLES);
  std::vector<tage_tag_table> tage_tags = std::vector<tage_tag_table>(TAGE_TABLES);
  std::vector<tage_usefulness_table> tage_usefulness = std::vector<tage_usefulness_table>(TAGE_TABLES);
  std::vector<sc_counter_table> sc_counters = std::vector<sc_counter_table>(SC_TOTAL_TABLES);
  std::vector<uint32_t> sc_local_histories = std::vector<uint32_t>(SC_LOCAL_HISTORY_SIZE);
  std::vector<uint16_t> sc_run_lengths = std::vector<uint16_t>(SC_LOCAL_HISTORY_SIZE);
  std::vector<uint32_t> sc_alt_local_histories = std::vector<uint32_t>(SC_LOCAL_HISTORY_SIZE);
  // Separate trust for taken/not-taken and weak/strong selected TAGE predictions.
  std::vector<sc_selector_table> sc_selectors = std::vector<sc_selector_table>(SC_SELECTOR_CONTEXTS);

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
  std::array<sc_index_history, SC_TABLES> sc_histories = []() {
    decltype(sc_histories) retval;
    std::transform(std::cbegin(sc_history_lengths), std::cend(sc_history_lengths), std::begin(retval), [](const auto length) {
      return sc_index_history{length};
    });
    return retval;
  }();
  uint32_t usefulness_clock = 0;
  uint32_t allocation_random = 0x9e3779b9u;
  uint64_t path_history = 0;
  uint64_t path_history_alt = 0;
  uint64_t sc_path_history = 0;
  int sc_train_threshold = 24;
  int sc_threshold_signal = 0;

  struct loop_entry {
    uint64_t tag = 0;
    uint32_t trip_count = 0;
    uint32_t current_count = 0;
    champsim::msl::fwcounter<2> confidence{};
    champsim::msl::fwcounter<2> chooser{};
    bool valid = false;
    bool body_direction = false;

    bool predict() const { return current_count == trip_count ? !body_direction : body_direction; }
    void observe(bool taken);
  };
  std::array<loop_entry, LOOP_TABLE_SIZE> loop_table{};

  struct tage_result {
    uint64_t base_index = 0;
    uint64_t gshare_index = 0;
    uint64_t base_chooser_index = 0;
    bool base_prediction = false;
    bool bimodal_prediction = false;
    bool gshare_prediction = false;
    bool tage_prediction = false;
    bool provider_weak = false;
    bool provider_unuseful = false;
    bool used_alternate = false;
    uint64_t alt_selector_index = 0;
    std::array<uint32_t, SC_TOTAL_TABLES> sc_indices{};
    uint32_t sc_local_slot = 0;
    uint32_t sc_alt_local_slot = 0;
    uint64_t sc_selector_index = 0;
    std::size_t sc_selector_context = 0;
    int sc_sum = 0;
    bool sc_prediction = false;
    bool sc_disagrees = false;
    bool used_sc = false;
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
