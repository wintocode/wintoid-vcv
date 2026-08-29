#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void test_##name(); \
    static void run_##name() { \
        tests_run++; \
        printf("  %s ... ", #name); \
        test_##name(); \
        tests_passed++; \
        printf("PASS\n"); \
    } \
    static void test_##name()

#define ASSERT(cond) \
    do { if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } } while(0)

#define ASSERT_NEAR(a, b, eps) \
    do { float _a=(a), _b=(b); if (!isfinite(_a) || !isfinite(_b) || fabsf(_a-_b) > (eps)) { \
        printf("FAIL\n    %s:%d: %f != %f (eps=%f)\n", \
               __FILE__, __LINE__, (double)_a, (double)_b, (double)(eps)); \
        exit(1); \
    } } while(0)

#include "../src/FourV2/model.h"

TEST(algorithm_table_has_exact_edges_and_carriers)
{
    ASSERT(four_v2::ALGORITHM_COUNT == 11);
    static const four_v2::Algorithm expected[11] = {
        {{{0,0,0,0},{1,0,0,0},{0,1,0,0},{0,0,1,0}}, {1,0,0,0}},
        {{{0,0,0,0},{1,0,0,0},{0,1,0,0},{0,1,0,0}}, {1,0,0,0}},
        {{{0,0,0,0},{1,0,0,0},{1,0,0,0},{0,1,0,0}}, {1,0,0,0}},
        {{{0,0,0,0},{1,0,0,0},{1,0,0,0},{0,0,1,0}}, {1,0,0,0}},
        {{{0,0,0,0},{1,0,0,0},{0,0,0,0},{0,0,1,0}}, {1,0,1,0}},
        {{{0,0,0,0},{0,0,0,0},{0,0,0,0},{1,1,1,0}}, {1,1,1,0}},
        {{{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,1,0}}, {1,1,1,0}},
        {{{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}}, {1,1,1,1}},
        {{{0,0,0,0},{0,0,0,0},{1,1,0,0},{0,0,1,0}}, {1,1,0,0}},
        {{{0,0,0,0},{0,0,0,0},{1,1,0,0},{1,1,0,0}}, {1,1,0,0}},
        {{{0,0,0,0},{1,0,0,0},{1,0,0,0},{1,0,0,0}}, {1,0,0,0}}
    };
    for (int a = 0; a < four_v2::ALGORITHM_COUNT; ++a) {
        for (int src = 0; src < four_v2::OPERATOR_COUNT; ++src) {
            ASSERT(four_v2::ALGORITHMS[a].carrier[src] == expected[a].carrier[src]);
            for (int dst = 0; dst < four_v2::OPERATOR_COUNT; ++dst)
                ASSERT(four_v2::ALGORITHMS[a].mod[src][dst] == expected[a].mod[src][dst]);
        }
    }
}

TEST(ratios_are_exact_and_descend)
{
    ASSERT(four_v2::RATIO_COUNT == 15);
    static const char* expected[] = {
        "4:1", "3:1", "2:1", "3:2", "4:3", "1:1", "3:4", "2:3",
        "1:2", "1:3", "1:4", "1:5", "1:6", "1:7", "1:8"
    };
    for (int i = 0; i < four_v2::RATIO_COUNT; ++i) {
        ASSERT(strcmp(four_v2::ratio_label((float)i), expected[i]) == 0);
        if (i > 0)
            ASSERT(four_v2::ratio_value((float)i - 1.f) >
                   four_v2::ratio_value((float)i));
    }
    ASSERT(strcmp(four_v2::ratio_label(3.f), "3:2") == 0);
    ASSERT(strcmp(four_v2::ratio_label(7.f), "2:3") == 0);
    ASSERT(strcmp(four_v2::ratio_label(10.f), "1:4") == 0);
    ASSERT(four_v2::frequency_label(3.f, four_v2::RATIO_MODE) == "3:2");
    ASSERT(four_v2::frequency_label(7.f, four_v2::RATIO_MODE) == "2:3");
    ASSERT(four_v2::frequency_label(10.f, four_v2::RATIO_MODE) == "1:4");
    ASSERT_NEAR(four_v2::ratio_value(5.f), 1.f, 1e-6f);
}

TEST(ratio_positions_round_and_clamp)
{
    ASSERT(four_v2::ratio_index(-100.f) == 0);
    ASSERT(four_v2::ratio_index(0.49f) == 0);
    ASSERT(four_v2::ratio_index(0.50f) == 1);
    ASSERT(four_v2::ratio_index(13.50f) == 14);
    ASSERT(four_v2::ratio_index(100.f) == 14);
    ASSERT(four_v2::ratio_index(NAN) == four_v2::DEFAULT_RATIO_INDEX);
}

TEST(fixed_frequency_is_continuous_and_exponential)
{
    ASSERT_NEAR(four_v2::fixed_hz(0.f), 1.f, 1e-5f);
    ASSERT_NEAR(four_v2::fixed_hz(14.f), 10000.f, 0.1f);
    ASSERT(four_v2::fixed_hz(7.1f) > four_v2::fixed_hz(7.f));
    ASSERT(four_v2::fixed_hz(NAN) == four_v2::fixed_hz(5.f));
}

TEST(indices_modes_and_labels_are_defensive)
{
    ASSERT(four_v2::algorithm_index(-1.f) == 0);
    ASSERT(four_v2::algorithm_index(5.5f) == 6);
    ASSERT(four_v2::algorithm_index(99.f) == 10);
    ASSERT(four_v2::algorithm_index(NAN) == 0);
    ASSERT(four_v2::clamp_mode(-1.f) == four_v2::RATIO_MODE);
    ASSERT(four_v2::clamp_mode(1.f) == four_v2::FIXED_MODE);
    ASSERT(four_v2::clamp_mode(NAN) == four_v2::RATIO_MODE);
    ASSERT(four_v2::frequency_label(5.f, four_v2::RATIO_MODE) == "1:1");
    ASSERT(four_v2::frequency_label(0.f, four_v2::FIXED_MODE) == "1.0 Hz");
    ASSERT(four_v2::frequency_label(10.49f, four_v2::FIXED_MODE) == "993.4 Hz");
    ASSERT(four_v2::frequency_label(10.51f, four_v2::FIXED_MODE) == "1.0 kHz");
}

int main()
{
    printf("Four V2 model tests:\n");
    run_algorithm_table_has_exact_edges_and_carriers();
    run_ratios_are_exact_and_descend();
    run_ratio_positions_round_and_clamp();
    run_fixed_frequency_is_continuous_and_exponential();
    run_indices_modes_and_labels_are_defensive();

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
