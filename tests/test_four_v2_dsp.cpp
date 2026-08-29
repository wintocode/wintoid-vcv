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
    } } while (0)

#define ASSERT_NEAR(a, b, eps) \
    do { float _a = (a), _b = (b); if (fabsf(_a - _b) > (eps)) { \
        printf("FAIL\n    %s:%d: %f != %f (eps=%f)\n", \
               __FILE__, __LINE__, (double)_a, (double)_b, (double)(eps)); \
        exit(1); \
    } } while (0)

#include "../src/FourV2/dsp.h"

TEST(oscillator_sine_landmarks)
{
    ASSERT_NEAR(four_v2::oscillator_sine(0.0f), 0.0f, 1e-6f);
    ASSERT_NEAR(four_v2::oscillator_sine(0.25f), 1.0f, 1e-6f);
    ASSERT_NEAR(four_v2::oscillator_sine(0.5f), 0.0f, 1e-6f);
}

TEST(phase_advance_wraps)
{
    float phase = 0.999f;
    four_v2::phase_advance(phase, 0.01f);
    ASSERT(phase >= 0.0f && phase < 1.0f);
    ASSERT_NEAR(phase, 0.009f, 1e-6f);
}

TEST(frequency_helpers_preserve_v1_behavior)
{
    ASSERT_NEAR(four_v2::calc_frequency_ratio(440.0f, 2.0f, 1.0f),
                880.0f, 0.01f);
    ASSERT_NEAR(four_v2::calc_frequency_fixed(1000.0f, 1.0f),
                1000.0f, 0.01f);
}

TEST(warp_landmarks_are_preserved)
{
    ASSERT_NEAR(four_v2::wave_warp(0.25f, 0.f), 1.f, 1e-5f);
    ASSERT_NEAR(four_v2::wave_warp(0.25f, 1.f / 3.f), 1.f, 0.15f);
    ASSERT(four_v2::wave_warp(0.99f, 2.f / 3.f) > 0.5f);
    ASSERT(four_v2::wave_warp(0.25f, 1.f) > 0.9f);
    ASSERT(four_v2::wave_warp(0.75f, 1.f) < -0.9f);
}

TEST(warp_zero_is_sine_passthrough)
{
    for (float phase = 0.0f; phase < 1.0f; phase += 0.1f)
        ASSERT_NEAR(four_v2::wave_warp(phase, 0.0f),
                    four_v2::oscillator_sine(phase), 1e-5f);
}

TEST(warp_morphs_through_triangle_saw_and_pulse)
{
    const float triangle = 1.0f / 3.0f;
    const float saw = 2.0f / 3.0f;
    ASSERT_NEAR(four_v2::wave_warp(0.25f, triangle), 1.0f, 0.15f);
    ASSERT_NEAR(four_v2::wave_warp(0.75f, triangle), -1.0f, 0.15f);
    ASSERT(four_v2::wave_warp(0.01f, saw) < -0.5f);
    ASSERT_NEAR(four_v2::wave_warp(0.5f, saw), 0.0f, 0.15f);
    ASSERT(four_v2::wave_warp(0.99f, saw) > 0.5f);
}

TEST(warp_outputs_stay_normalized)
{
    for (int i = 0; i < 100; ++i)
    {
        const float phase = (float)i / 100.0f;
        for (int j = 0; j <= 100; ++j)
        {
            const float warp = (float)j / 100.0f;
            const float output = four_v2::wave_warp(phase, warp);
            ASSERT(output >= -1.001f && output <= 1.001f);
        }
    }
}

TEST(fold_zero_is_passthrough)
{
    ASSERT_NEAR(four_v2::wave_fold(0.5f, 0.0f, 0), 0.5f, 1e-6f);
    ASSERT_NEAR(four_v2::wave_fold(-0.3f, 0.0f, 1), -0.3f, 1e-6f);
    ASSERT_NEAR(four_v2::wave_fold(0.7f, 0.0f, 2), 0.7f, 1e-6f);
}

TEST(fold_symmetric_stays_bounded)
{
    for (float input = -1.0f; input <= 1.0f; input += 0.1f)
    {
        const float output = four_v2::wave_fold(input, 1.0f, 0);
        ASSERT(output >= -1.01f && output <= 1.01f);
    }
}

TEST(fold_asymmetric_stays_bounded)
{
    for (float input = -1.0f; input <= 1.0f; input += 0.1f)
    {
        const float output = four_v2::wave_fold(input, 1.0f, 1);
        ASSERT(output >= -1.01f && output <= 1.01f);
    }
}

TEST(fold_soft_clip_stays_bounded)
{
    for (float input = -1.0f; input <= 1.0f; input += 0.1f)
    {
        const float output = four_v2::wave_fold(input, 1.0f, 2);
        ASSERT(output >= -1.01f && output <= 1.01f);
    }
}

TEST(fold_type_is_clamped_to_the_supported_range)
{
    ASSERT_NEAR(four_v2::wave_fold(0.8f, 0.5f, -1),
                four_v2::wave_fold(0.8f, 0.5f, 0), 1e-6f);
    ASSERT_NEAR(four_v2::wave_fold(0.8f, 0.5f, 3),
                four_v2::wave_fold(0.8f, 0.5f, 2), 1e-6f);
}

TEST(feedback_bounds_are_preserved)
{
    ASSERT_NEAR(four_v2::calc_feedback(0.5f, 0.0f), 0.0f, 1e-6f);
    const float positive = four_v2::calc_feedback(0.8f, 1.0f);
    ASSERT(positive > 0.0f && positive <= 1.0f);
    const float negative = four_v2::calc_feedback(-0.8f, 1.0f);
    ASSERT(negative < 0.0f && negative >= -1.0f);
    const float extreme = four_v2::calc_feedback(10.0f, 1.0f);
    ASSERT(extreme >= -1.0f && extreme <= 1.0f);
}

TEST(algorithm_six_fans_operator_four_to_three_destinations)
{
    float out[4] = {0.f, 0.f, 0.f, 0.5f};
    float level[4] = {1.f, 1.f, 1.f, 0.8f};
    const four_v2::Algorithm& a = four_v2::ALGORITHMS[5];
    ASSERT_NEAR(four_v2::gather_modulation(0, out, level, 1.f, a), 0.4f, 1e-6f);
    ASSERT_NEAR(four_v2::gather_modulation(1, out, level, 1.f, a), 0.4f, 1e-6f);
    ASSERT_NEAR(four_v2::gather_modulation(2, out, level, 1.f, a), 0.4f, 1e-6f);
    ASSERT_NEAR(four_v2::gather_modulation(3, out, level, 1.f, a), 0.0f, 1e-6f);
}

TEST(carriers_sum_raw_without_normalisation)
{
    float out[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    float level[4] = {1.f, 1.f, 1.f, 1.f};
    ASSERT_NEAR(four_v2::sum_carriers(
        out, level, four_v2::ALGORITHMS[7]), 2.f, 1e-6f);
}

TEST(downsample_2x_averages_the_two_samples)
{
    ASSERT_NEAR(four_v2::downsample_2x(0.8f, 0.6f), 0.7f, 1e-6f);
}

TEST(polyblep_corrects_both_cycle_edges)
{
    const float dt = 440.0f / 48000.0f;
    ASSERT(fabsf(four_v2::polyblep(0.001f, dt)) > 0.0f);
    ASSERT(fabsf(four_v2::polyblep(0.999f, dt)) > 0.0f);
    ASSERT_NEAR(four_v2::polyblep(0.5f, dt), 0.0f, 1e-6f);
}

TEST(polyblep_saw_reduces_the_reset_jump)
{
    const float dt = 440.0f / 48000.0f;
    const float raw = four_v2::waveform_saw(0.999f) -
                      four_v2::waveform_saw(0.001f);
    const float corrected = four_v2::waveform_saw_blep(0.999f, dt) -
                            four_v2::waveform_saw_blep(0.001f, dt);
    ASSERT(fabsf(raw) > 1.5f);
    ASSERT(fabsf(corrected) < fabsf(raw));
}

TEST(wave_warp_blep_keeps_landmarks_and_corrects_edges)
{
    const float dt = 440.0f / 48000.0f;
    ASSERT_NEAR(four_v2::wave_warp_blep(0.25f, 0.0f, dt), 1.0f, 1e-5f);
    ASSERT(four_v2::wave_warp_blep(0.99f, 2.0f / 3.0f, dt) > 0.5f);
    ASSERT(fabsf(four_v2::wave_warp_blep(0.001f, 1.0f, dt) -
                 four_v2::wave_warp(0.001f, 1.0f)) > 0.0f);
}

TEST(dc_blocker_removes_dc_and_passes_ac)
{
    four_v2::DCBlocker dc;
    float output = 0.0f;
    for (int i = 0; i < 10000; ++i)
        output = dc.process(1.0f);
    ASSERT(fabsf(output) < 0.01f);

    four_v2::DCBlocker ac;
    float max_output = 0.0f;
    for (int i = 0; i < 2000; ++i)
    {
        const float input = sinf((float)i * 440.0f / 48000.0f * 6.283185f);
        const float sample = ac.process(input);
        if (i > 500 && fabsf(sample) > max_output)
            max_output = fabsf(sample);
    }
    ASSERT(max_output > 0.9f);
}

TEST(flush_denormal_handles_tiny_values)
{
    float zero = 0.0f;
    four_v2::flush_denormal(zero);
    ASSERT_NEAR(zero, 0.0f, 0.0f);

    float tiny = 1e-20f;
    four_v2::flush_denormal(tiny);
    ASSERT_NEAR(tiny, 0.0f, 0.0f);

    float normal = 0.5f;
    four_v2::flush_denormal(normal);
    ASSERT_NEAR(normal, 0.5f, 0.0f);
}

int main()
{
    printf("Four V2 DSP tests:\n");
    run_oscillator_sine_landmarks();
    run_phase_advance_wraps();
    run_frequency_helpers_preserve_v1_behavior();
    run_warp_landmarks_are_preserved();
    run_warp_zero_is_sine_passthrough();
    run_warp_morphs_through_triangle_saw_and_pulse();
    run_warp_outputs_stay_normalized();
    run_fold_zero_is_passthrough();
    run_fold_symmetric_stays_bounded();
    run_fold_asymmetric_stays_bounded();
    run_fold_soft_clip_stays_bounded();
    run_fold_type_is_clamped_to_the_supported_range();
    run_feedback_bounds_are_preserved();
    run_algorithm_six_fans_operator_four_to_three_destinations();
    run_carriers_sum_raw_without_normalisation();
    run_downsample_2x_averages_the_two_samples();
    run_polyblep_corrects_both_cycle_edges();
    run_polyblep_saw_reduces_the_reset_jump();
    run_wave_warp_blep_keeps_landmarks_and_corrects_edges();
    run_dc_blocker_removes_dc_and_passes_ac();
    run_flush_denormal_handles_tiny_values();
    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return 0;
}
