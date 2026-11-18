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
    install(p, 11, 0, 1);
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
    install(longest, 11, -2);
    longest.predict_branch(pc);
    const auto longest_saved = longest.last_result;
    update(longest, true); // No longer bank: no out-of-range access or modulo zero.
    REQUIRE(longest.tage_counters[11][longest_saved.indices[11]].value() == -1);
    for (std::size_t bank = 0; bank < 11; ++bank)
      REQUIRE(longest.tage_tags[bank][longest_saved.indices[bank]] == 0);
  }

  static void statistical_corrector()
  {
    my_perceptron p{nullptr};
    REQUIRE_FALSE(p.predict_branch(pc)); // Cold TAGE/base predicts not-taken.
    auto saved = p.last_result;
    for (std::size_t table = 0; table < p.SC_TABLES; ++table)
      p.sc_counters[table][saved.sc_indices[table]] = 2;
    REQUIRE(p.predict_branch(pc)); // Strong short-history vote overrides TAGE.
    REQUIRE(p.last_result.sc_sum == 16);
    REQUIRE(p.last_result.used_sc);

    p.sc_selectors[p.last_result.sc_selector_index] = -7;
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
    REQUIRE(learning.sc_selectors[saved.sc_selector_index].value() == -1);
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
