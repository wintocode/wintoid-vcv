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

int main()
{
    run_each_output_matches_the_equivalent_vortex_mode();
    run_voice_state_reset_clears_all_twelve_branches();
    run_branches_remain_independent_when_interleaved();
    run_all_valid_branches_remain_finite();
    printf("%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
