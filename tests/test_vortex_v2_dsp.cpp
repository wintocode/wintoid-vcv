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
    do { float _a=(a), _b=(b); if (fabsf(_a-_b) > (eps)) { \
        printf("FAIL\n    %s:%d: %f != %f (eps=%f)\n", \
               __FILE__, __LINE__, (double)_a, (double)_b, (double)(eps)); \
        exit(1); \
    } } while(0)

#include "../src/VortexV2/dsp.h"

TEST(cutoff_knob_travels_exponentially_across_the_audio_range)
{
    ASSERT_NEAR(vortex_v2::cutoff_param_to_hz(0.0f), 20.0f, 0.01f);
    ASSERT_NEAR(vortex_v2::cutoff_param_to_hz(0.5f), 632.4555f, 0.01f);
    ASSERT_NEAR(vortex_v2::cutoff_param_to_hz(1.0f), 20000.0f, 0.1f);
}

TEST(one_khz_sits_at_the_correct_logarithmic_knob_position)
{
    ASSERT_NEAR(vortex_v2::cutoff_hz_to_param(1000.0f), 0.5663233f, 0.000001f);
}

TEST(cutoff_voct_shifts_frequency_by_octaves_and_clamps)
{
    ASSERT_NEAR(vortex_v2::cutoff_with_voct(1000.0f, 0.0f), 1000.0f, 0.01f);
    ASSERT_NEAR(vortex_v2::cutoff_with_voct(1000.0f, 1.0f), 2000.0f, 0.01f);
    ASSERT_NEAR(vortex_v2::cutoff_with_voct(1000.0f, -1.0f), 500.0f, 0.01f);
    ASSERT_NEAR(vortex_v2::cutoff_with_voct(19000.0f, 1.0f), 20000.0f, 0.01f);
    ASSERT_NEAR(vortex_v2::cutoff_with_voct(30.0f, -1.0f), 20.0f, 0.01f);
}

static float reference_branch(vortex::Filter1& f1,
                              vortex::Filter2& f2a,
                              vortex::Filter2& f2b,
                              int mode,
                              float signal,
                              float sampleRate,
                              float cutoff,
                              float damping)
{
    switch (mode) {
    case 0:
        vortex::filter1_configure_lp(f1, sampleRate, cutoff);
        return f1.process_lp(signal);
    case 1:
        vortex::filter2_configure(f2a, sampleRate, cutoff, damping, vortex::F2_LP);
        return vortex::filter2_process(f2a, signal, vortex::F2_LP);
    case 2:
        vortex::filter2_configure(f2a, sampleRate, cutoff, damping, vortex::F2_LP);
        vortex::filter2_configure(f2b, sampleRate, cutoff, damping, vortex::F2_LP);
        return vortex::filter2_process(f2b,
               vortex::filter2_process(f2a, signal, vortex::F2_LP),
               vortex::F2_LP);
    case 3:
        vortex::filter1_configure_hp(f1, sampleRate, cutoff);
        return f1.process_hp(signal);
    case 4:
        vortex::filter2_configure(f2a, sampleRate, cutoff, damping, vortex::F2_HP);
        return vortex::filter2_process(f2a, signal, vortex::F2_HP);
    case 5:
        vortex::filter2_configure(f2a, sampleRate, cutoff, damping, vortex::F2_HP);
        vortex::filter2_configure(f2b, sampleRate, cutoff, damping, vortex::F2_HP);
        return vortex::filter2_process(f2b,
               vortex::filter2_process(f2a, signal, vortex::F2_HP),
               vortex::F2_HP);
    case 6:
        vortex::filter2_configure(f2a, sampleRate, cutoff, damping, vortex::F2_BP);
        return vortex::filter2_process(f2a, signal, vortex::F2_BP);
    case 7:
        vortex::filter2_configure(f2a, sampleRate, cutoff, damping, vortex::F2_BP);
        vortex::filter2_configure(f2b, sampleRate, cutoff, damping, vortex::F2_BP);
        return vortex::filter2_process(f2b,
               vortex::filter2_process(f2a, signal, vortex::F2_BP),
               vortex::F2_BP);
    case 8:
        vortex::filter2_configure(f2a, sampleRate, cutoff, damping, vortex::F2_NOTCH);
        return vortex::filter2_process(f2a, signal, vortex::F2_NOTCH);
    case 9:
        vortex::filter2_configure(f2a, sampleRate, cutoff, damping, vortex::F2_NOTCH);
        vortex::filter2_configure(f2b, sampleRate, cutoff, damping, vortex::F2_NOTCH);
        return vortex::filter2_process(f2b,
               vortex::filter2_process(f2a, signal, vortex::F2_NOTCH),
               vortex::F2_NOTCH);
    case 10:
        vortex::filter2_configure(f2a, sampleRate, cutoff, damping, vortex::F2_AP);
        return vortex::filter2_process(f2a, signal, vortex::F2_AP);
    case 11:
        vortex::filter2_configure(f2a, sampleRate, cutoff, damping, vortex::F2_AP);
        vortex::filter2_configure(f2b, sampleRate, cutoff, damping, vortex::F2_AP);
        return vortex::filter2_process(f2b,
               vortex::filter2_process(f2a, signal, vortex::F2_AP),
               vortex::F2_AP);
    default:
        return 0.f;
    }
}

static void assert_branch_state_finite(const vortex_v2::BranchState& branch)
{
    ASSERT(isfinite(branch.f1.z));
    ASSERT(isfinite(branch.f1.b0));
    ASSERT(isfinite(branch.f1.b1));
    ASSERT(isfinite(branch.f2a.z0));
    ASSERT(isfinite(branch.f2a.z1));
    ASSERT(isfinite(branch.f2a.b0));
    ASSERT(isfinite(branch.f2a.b1));
    ASSERT(isfinite(branch.f2a.b2));
    ASSERT(isfinite(branch.f2a.b3));
    ASSERT(isfinite(branch.f2b.z0));
    ASSERT(isfinite(branch.f2b.z1));
    ASSERT(isfinite(branch.f2b.b0));
    ASSERT(isfinite(branch.f2b.b1));
    ASSERT(isfinite(branch.f2b.b2));
    ASSERT(isfinite(branch.f2b.b3));
}

TEST(each_output_matches_the_equivalent_vortex_mode)
{
    for (int mode = 0; mode < vortex_v2::OUTPUT_COUNT; ++mode) {
        vortex_v2::BranchState actual;
        vortex::Filter1 f1;
        vortex::Filter2 f2a;
        vortex::Filter2 f2b;
        for (int sample = 0; sample < 512; ++sample) {
            const float signal = sinf(0.013f * sample) * 0.7f;
            const float expected = reference_branch(
                f1, f2a, f2b, mode, signal, 48000.f, 1000.f, 0.35f);
            const float received = vortex_v2::process_branch(
                actual, vortex_v2::OUTPUT_MODES[mode], signal,
                48000.f, 1000.f, 0.35f);
            ASSERT_NEAR(received, expected, 1e-6f);
        }
    }
}

TEST(voice_state_reset_clears_all_twelve_branches)
{
    vortex_v2::VoiceState voice;
    for (int output = 0; output < vortex_v2::OUTPUT_COUNT; ++output)
        for (int sample = 0; sample < 8; ++sample)
            vortex_v2::process_branch(voice.branches[output],
                vortex_v2::OUTPUT_MODES[output], 0.5f, 48000.f, 1000.f, 0.35f);

    voice.reset();
    for (int output = 0; output < vortex_v2::OUTPUT_COUNT; ++output) {
        ASSERT(voice.branches[output].f1.z == 0.0f);
        ASSERT(voice.branches[output].f2a.z0 == 0.0f);
        ASSERT(voice.branches[output].f2a.z1 == 0.0f);
        ASSERT(voice.branches[output].f2b.z0 == 0.0f);
        ASSERT(voice.branches[output].f2b.z1 == 0.0f);
    }
}

TEST(branches_remain_independent_when_interleaved)
{
    vortex_v2::BranchState actual_lp6;
    vortex_v2::BranchState actual_ap_plus;
    vortex::Filter1 reference_lp6_f1;
    vortex::Filter2 reference_ap_plus_f2a;
    vortex::Filter2 reference_ap_plus_f2b;
    for (int sample = 0; sample < 256; ++sample) {
        const float signal_lp6 = sinf(0.017f * sample) * 0.4f;
        const float signal_ap_plus = cosf(0.009f * sample) * 0.6f;
        const float expected_lp6 = reference_branch(reference_lp6_f1,
            reference_ap_plus_f2a, reference_ap_plus_f2b, 0,
            signal_lp6, 48000.f, 1000.f, 0.35f);
        const float received_lp6 = vortex_v2::process_branch(actual_lp6,
            vortex_v2::LP6, signal_lp6, 48000.f, 1000.f, 0.35f);
        ASSERT_NEAR(received_lp6, expected_lp6, 1e-6f);

        vortex::Filter1 unused_f1;
        const float expected_ap_plus = reference_branch(unused_f1,
            reference_ap_plus_f2a, reference_ap_plus_f2b, 11,
            signal_ap_plus, 48000.f, 1000.f, 0.35f);
        const float received_ap_plus = vortex_v2::process_branch(actual_ap_plus,
            vortex_v2::AP_PLUS, signal_ap_plus, 48000.f, 1000.f, 0.35f);
        ASSERT_NEAR(received_ap_plus, expected_ap_plus, 1e-6f);
    }
}

TEST(cached_branches_follow_audio_rate_cutoff_and_resonance_changes)
{
    vortex_v2::BranchState actual;
    vortex::Filter1 unused;
    vortex::Filter2 referenceA;
    vortex::Filter2 referenceB;
    for (int sample = 0; sample < 512; ++sample) {
        const float signal = sinf(0.017f * sample) * 0.7f;
        const float cutoff = 200.f + (sample % 97) * 150.f;
        const float damping = 0.01f + (sample % 31) * 0.02f;
        const float expected = reference_branch(unused, referenceA, referenceB,
            vortex_v2::NOTCH_PLUS, signal, 48000.f, cutoff, damping);
        const float received = vortex_v2::process_branch(actual,
            vortex_v2::NOTCH_PLUS, signal, 48000.f, cutoff, damping);
        ASSERT_NEAR(received, expected, 1e-6f);
    }
}

TEST(all_valid_branches_remain_finite)
{
    vortex_v2::VoiceState voice;
    for (int sample = 0; sample < 2048; ++sample) {
        const float signal = sinf(0.021f * sample) * 0.75f;
        for (int output = 0; output < vortex_v2::OUTPUT_COUNT; ++output) {
            const float result = vortex_v2::process_branch(
                voice.branches[output], vortex_v2::OUTPUT_MODES[output],
                signal, 48000.f, 1000.f, 0.35f);
            ASSERT(isfinite(result));
            assert_branch_state_finite(voice.branches[output]);
        }
    }
}

TEST(branch_recovers_after_single_nan_sample)
{
    vortex_v2::BranchState branch;
    const float sampleRate = 48000.f, cutoff = 1000.f, damping = 0.35f;
    float y = 0.f;
    for (int i = 0; i < 100; ++i)
        y = vortex_v2::process_branch(
            branch, vortex_v2::LP12, 0.5f, sampleRate, cutoff, damping);
    (void)vortex_v2::process_branch(
        branch, vortex_v2::LP12, NAN, sampleRate, cutoff, damping);
    for (int i = 0; i < 10; ++i)
        y = vortex_v2::process_branch(
            branch, vortex_v2::LP12, 0.5f, sampleRate, cutoff, damping);
    ASSERT(isfinite(y));
}

TEST(drive_saturate_preserves_v1_curve_below_the_bound)
{
    // Identical to vortex::soft_clip across the musical range
    static const float points[] = { 0.f, 0.5f, 1.f, 3.f, 10.f, 20.f };
    for (int i = 0; i < 6; ++i)
        ASSERT_NEAR(
            vortex_v2::drive_saturate(points[i]),
            vortex::soft_clip(points[i]), 1e-6f);
}

TEST(drive_saturate_is_bounded)
{
    ASSERT_NEAR(vortex_v2::drive_saturate(100.f), 3.f, 1e-6f);
    ASSERT_NEAR(vortex_v2::drive_saturate(-100.f), -3.f, 1e-6f);
    ASSERT_NEAR(vortex_v2::drive_saturate(1e6f), 3.f, 1e-6f);
    ASSERT_NEAR(vortex_v2::drive_saturate(-1e6f), -3.f, 1e-6f);
    // Non-finite input maps to silence, not a latched DC value
    ASSERT(vortex_v2::drive_saturate(NAN) == 0.f);
}

int main()
{
    run_cutoff_knob_travels_exponentially_across_the_audio_range();
    run_one_khz_sits_at_the_correct_logarithmic_knob_position();
    run_cutoff_voct_shifts_frequency_by_octaves_and_clamps();
    run_each_output_matches_the_equivalent_vortex_mode();
    run_voice_state_reset_clears_all_twelve_branches();
    run_branches_remain_independent_when_interleaved();
    run_cached_branches_follow_audio_rate_cutoff_and_resonance_changes();
    run_all_valid_branches_remain_finite();
    run_branch_recovers_after_single_nan_sample();
    run_drive_saturate_preserves_v1_curve_below_the_bound();
    run_drive_saturate_is_bounded();
    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
