#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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
    do { float _a=(a), _b=(b); if (!(fabsf(_a-_b) <= (eps))) { \
        printf("FAIL\n    %s:%d: %f != %f (eps=%f)\n", \
               __FILE__, __LINE__, (double)_a, (double)_b, (double)(eps)); \
        exit(1); \
    } } while(0)

#include "../src/text_entry.h"
#include "../src/FourV2/model.h"
#include "../src/Four/dsp.h"

using wintoid::text_entry::parse_frequency_hz;
using wintoid::text_entry::parse_ratio;

static float hz_of(const char* text)
{
    float hz = NAN;
    ASSERT(parse_frequency_hz(text, hz));
    return hz;
}

static float ratio_of(const char* text)
{
    float ratio = NAN;
    ASSERT(parse_ratio(text, ratio));
    return ratio;
}

TEST(frequency_accepts_bare_numbers_and_displayed_units)
{
    ASSERT_NEAR(hz_of("2000"), 2000.f, 0.001f);
    ASSERT_NEAR(hz_of("500.0 Hz"), 500.f, 0.001f);
    ASSERT_NEAR(hz_of("  440hz "), 440.f, 0.001f);
    ASSERT_NEAR(hz_of("2.00 kHz"), 2000.f, 0.001f);
    ASSERT_NEAR(hz_of("1.5KHZ"), 1500.f, 0.001f);
    ASSERT_NEAR(hz_of("2.5k"), 2500.f, 0.001f);
}

TEST(frequency_rejects_other_text)
{
    float hz = 123.f;
    ASSERT(!parse_frequency_hz("", hz));
    ASSERT(!parse_frequency_hz("kHz", hz));
    ASSERT(!parse_frequency_hz("440 cents", hz));
    ASSERT(!parse_frequency_hz("nan", hz));
    ASSERT(!parse_frequency_hz("inf Hz", hz));
    ASSERT(hz == 123.f);
}

TEST(ratio_accepts_labels_and_plain_multipliers)
{
    ASSERT_NEAR(ratio_of("2:1"), 2.f, 1e-6f);
    ASSERT_NEAR(ratio_of("3 : 2"), 1.5f, 1e-6f);
    ASSERT_NEAR(ratio_of("1:8"), 0.125f, 1e-6f);
    ASSERT_NEAR(ratio_of("0.25:1"), 0.25f, 1e-6f);
    ASSERT_NEAR(ratio_of("1.5"), 1.5f, 1e-6f);
}

TEST(ratio_rejects_other_text)
{
    float ratio = 7.f;
    ASSERT(!parse_ratio("", ratio));
    ASSERT(!parse_ratio("2:", ratio));
    ASSERT(!parse_ratio("2:0", ratio));
    ASSERT(!parse_ratio("-2:1", ratio));
    ASSERT(!parse_ratio("0", ratio));
    ASSERT(!parse_ratio("440 Hz", ratio));
    ASSERT(ratio == 7.f);
}

TEST(four_v2_every_ratio_label_selects_its_own_position)
{
    for (int i = 0; i < four_v2::RATIO_COUNT; ++i)
        ASSERT_NEAR(four_v2::coarse_from_ratio(
                        ratio_of(four_v2::RATIOS[i].label)),
                    (float)i, 0.f);
    ASSERT_NEAR(four_v2::coarse_from_ratio(2.f), 2.f, 0.f);
    ASSERT_NEAR(four_v2::coarse_from_ratio(100.f), 0.f, 0.f);
    ASSERT_NEAR(four_v2::coarse_from_ratio(0.001f), 14.f, 0.f);
}

TEST(four_v2_fixed_entry_inverts_coarse_and_fine)
{
    const float fines[] = {-100.f, 0.f, 37.f, 100.f};
    for (float fine : fines) {
        for (float coarse = 1.f; coarse <= 13.f; coarse += 0.37f) {
            const float hz = four_v2::fixed_frequency(coarse, fine);
            ASSERT_NEAR(four_v2::coarse_from_fixed_frequency(hz, fine),
                        coarse, 1e-4f);
        }
    }
    const float coarse = four_v2::coarse_from_fixed_frequency(440.f, 0.f);
    ASSERT_NEAR(four_v2::fixed_frequency(coarse, 0.f), 440.f, 0.01f);
    ASSERT_NEAR(four_v2::coarse_from_fixed_frequency(0.f, 0.f), 0.f, 0.f);
    ASSERT_NEAR(four_v2::coarse_from_fixed_frequency(1e9f, 0.f), 14.f, 0.f);
    ASSERT_NEAR(four_v2::coarse_from_fixed_frequency(NAN, 0.f), 0.f, 0.f);
}

TEST(four_v2_displayed_fixed_label_round_trips_within_its_precision)
{
    const float coarse = 7.3f;
    const float fine = 25.f;
    const std::string label = four_v2::frequency_label(
        coarse, four_v2::FIXED_MODE, fine);
    const float hz = hz_of(label.c_str());
    const float back = four_v2::coarse_from_fixed_frequency(hz, fine);
    ASSERT(four_v2::frequency_label(back, four_v2::FIXED_MODE, fine)
           == label);
}

TEST(four_v1_every_ratio_selects_its_own_index)
{
    for (int idx = 0; idx <= 64; ++idx)
        ASSERT_NEAR(four::coarse_index_from_ratio(
                        four::coarse_ratio_from_index(idx)),
                    (float)idx, 0.f);
    ASSERT_NEAR(four::coarse_index_from_ratio(ratio_of("2:1")), 5.f, 0.f);
    ASSERT_NEAR(four::coarse_index_from_ratio(ratio_of("0.25:1")), 0.f, 0.f);
}

TEST(four_v1_fixed_entry_inverts_the_exponential_knob)
{
    for (float param = 0.f; param <= 64.f; param += 1.7f)
        ASSERT_NEAR(four::coarse_param_from_fixed(
                        four::coarse_fixed_from_param(param)),
                    param, 1e-3f);
    ASSERT_NEAR(four::coarse_fixed_from_param(
                    four::coarse_param_from_fixed(hz_of("440"))),
                440.f, 0.01f);
    ASSERT_NEAR(four::coarse_param_from_fixed(0.f), 0.f, 0.f);
    ASSERT_NEAR(four::coarse_param_from_fixed(1e9f), 64.f, 0.f);
}

int main()
{
    printf("Text entry tests:\n");
    run_frequency_accepts_bare_numbers_and_displayed_units();
    run_frequency_rejects_other_text();
    run_ratio_accepts_labels_and_plain_multipliers();
    run_ratio_rejects_other_text();
    run_four_v2_every_ratio_label_selects_its_own_position();
    run_four_v2_fixed_entry_inverts_coarse_and_fine();
    run_four_v2_displayed_fixed_label_round_trips_within_its_precision();
    run_four_v1_every_ratio_selects_its_own_index();
    run_four_v1_fixed_entry_inverts_the_exponential_knob();

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
