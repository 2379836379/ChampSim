/*
 * Part 4: branch-heavy, deterministic weighted subset checksum.
 *
 * Build: gcc -O2 -std=c11 hard_workload.c -o hard_workload
 * Run:   ./hard_workload [rounds [seed]]
 * Defaults: 10,000,000 rounds, seed 0x6d2b79f5.
 *
 * Each xorshift32 output chooses a subset of 32 fixed weights. We add the
 * selected weights to a checksum, modulo 2^32. Each selection is implemented
 * by a separate conditional branch, with approximately equally likely outcomes.
 * Generating all 32 choices together keeps useful unpredictable branches dense.
 * The printed checksum makes the computation observable and reproducible.
 *
 * GCC's function-local options below preserve actual branches even with -O2;
 * there is no inline assembly. Inspect run_workload with objdump after changing
 * compiler/version/options. Other compilers may convert the conditions to
 * branchless operations and are not the intended trace-generation toolchain.
 *
 * With the intended GCC build, the default run comfortably exceeds 200 million
 * instructions. Small round counts are useful only for correctness checks.
 * Submit a 200M-instruction PIN trace; measure hardness using bimodal with 2M
 * warmup + 100M simulation. The source alone cannot guarantee a particular MPKI.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Reject signs, whitespace, zero, overflow, and partially parsed arguments. */
static int parse_positive(const char *text, uint64_t maximum, uint64_t *value)
{
    char *end;
    uintmax_t parsed;

    if (text[0] < '0' || text[0] > '9')
        return 0;
    errno = 0;
    /* Decimal by default; an explicit 0x prefix selects hexadecimal. */
    parsed = strtoumax(text, &end,
                      text[0] == '0' && (text[1] == 'x' || text[1] == 'X')
                          ? 16 : 10);
    if (errno == ERANGE || *end != '\0' || parsed == 0 || parsed > maximum)
        return 0;
    *value = (uint64_t)parsed;
    return 1;
}

#if defined(__GNUC__) && !defined(__clang__)
__attribute__((noinline, optimize("no-if-conversion", "no-if-conversion2")))
#endif
static uint32_t run_workload(uint64_t rounds, uint32_t random_state)
{
    uint32_t checksum = 0;

    do {
        /* Nonzero xorshift32 state has period 2^32 - 1. All shifts are unsigned. */
        random_state ^= random_state << 13;
        random_state ^= random_state >> 17;
        random_state ^= random_state << 5;

        /* Expand at distinct instruction addresses, without a per-bit loop. */
#define SELECT_WEIGHT(bit, weight)                    \
        do {                                         \
            if (random_state & (UINT32_C(1) << (bit))) \
                checksum += UINT32_C(weight);        \
        } while (0)

        SELECT_WEIGHT( 0, 0x243f6a89);
        SELECT_WEIGHT( 1, 0x05a308d3);
        SELECT_WEIGHT( 2, 0x13198a2f);
        SELECT_WEIGHT( 3, 0x03707345);
        SELECT_WEIGHT( 4, 0x24093823);
        SELECT_WEIGHT( 5, 0x299f31d1);
        SELECT_WEIGHT( 6, 0x082efa99);
        SELECT_WEIGHT( 7, 0x2c4e6c89);
        SELECT_WEIGHT( 8, 0x052821e7);
        SELECT_WEIGHT( 9, 0x38d01377);
        SELECT_WEIGHT(10, 0x3e5466cf);
        SELECT_WEIGHT(11, 0x34e90c6d);
        SELECT_WEIGHT(12, 0x00ac29b7);
        SELECT_WEIGHT(13, 0x097c50dd);
        SELECT_WEIGHT(14, 0x3f84d5b5);
        SELECT_WEIGHT(15, 0x35470917);
        SELECT_WEIGHT(16, 0x1216d5d9);
        SELECT_WEIGHT(17, 0x0979fb1b);
        SELECT_WEIGHT(18, 0x11310ba7);
        SELECT_WEIGHT(19, 0x18dfb5ad);
        SELECT_WEIGHT(20, 0x2ffd72db);
        SELECT_WEIGHT(21, 0x101adfb7);
        SELECT_WEIGHT(22, 0x38e1afed);
        SELECT_WEIGHT(23, 0x2a267e97);
        SELECT_WEIGHT(24, 0x3a9045f1);
        SELECT_WEIGHT(25, 0x2c7f9923);
        SELECT_WEIGHT(26, 0x24a19947);
        SELECT_WEIGHT(27, 0x3158efc1);
        SELECT_WEIGHT(28, 0x0801f2e3);
        SELECT_WEIGHT(29, 0x2858fc17);
        SELECT_WEIGHT(30, 0x236920d9);
        SELECT_WEIGHT(31, 0x31574e69);

#undef SELECT_WEIGHT
    } while (--rounds != 0);

    return checksum;
}

int main(int argc, char **argv)
{
    uint64_t rounds = UINT64_C(10000000);
    uint64_t seed = UINT32_C(0x6d2b79f5);

    if (argc > 3 ||
        (argc > 1 && !parse_positive(argv[1], UINT64_MAX, &rounds)) ||
        (argc > 2 && !parse_positive(argv[2], UINT32_MAX, &seed))) {
        fprintf(stderr,
                "Usage: %s [rounds [seed]]\n"
                "  rounds: 1..18446744073709551615 (default: 10000000)\n"
                "  seed:   1..4294967295 (default: 0x6d2b79f5)\n"
                "  Values may be decimal or prefixed with 0x.\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    const uint32_t checksum = run_workload(rounds, (uint32_t)seed);
    printf("rounds=%" PRIu64 " seed=0x%08" PRIx32 " checksum=0x%08" PRIx32 "\n",
           rounds, (uint32_t)seed, checksum);
    return EXIT_SUCCESS;
}
