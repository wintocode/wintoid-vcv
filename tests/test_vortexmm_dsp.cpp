#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Test macros (same pattern as four)
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

#include "../src/VortexMM/dsp.h"

// --- Utility tests ---

TEST(soft_clip_zero)
{
    ASSERT_NEAR(vortex::soft_clip(0.0f), 0.0f, 1e-6f);
}

TEST(soft_clip_unity)
{
    // soft_clip(1) = 1*(27+1)/(27+9) = 28/36 = 0.7778
    ASSERT_NEAR(vortex::soft_clip(1.0f), 28.0f / 36.0f, 1e-4f);
}

TEST(soft_clip_symmetry)
{
    ASSERT_NEAR(vortex::soft_clip(-0.5f), -vortex::soft_clip(0.5f), 1e-6f);
}

TEST(soft_clip_saturation)
{
    // At x=3, soft_clip converges to exactly 1.0 (3x compression)
    ASSERT_NEAR(vortex::soft_clip(3.0f), 1.0f, 1e-4f);
}

TEST(midi_note_to_freq_a4)
{
    ASSERT_NEAR(vortex::midi_note_to_freq(69.0f), 440.0f, 0.01f);
}

TEST(midi_note_to_freq_c4)
{
    ASSERT_NEAR(vortex::midi_note_to_freq(60.0f), 261.63f, 0.01f);
}

TEST(voct_to_freq_0v)
{
    // 0V = C4 = 261.63 Hz
    ASSERT_NEAR(vortex::voct_to_freq(0.0f), 261.63f, 0.01f);
}

TEST(voct_to_freq_1v)
{
    // 1V = C5 = 523.25 Hz
    ASSERT_NEAR(vortex::voct_to_freq(1.0f), 523.25f, 0.1f);
}

TEST(voct_to_mult_zero)
{
    ASSERT_NEAR(vortex::voct_to_mult(0.0f), 1.0f, 1e-6f);
}

TEST(voct_to_mult_one)
{
    ASSERT_NEAR(vortex::voct_to_mult(1.0f), 2.0f, 1e-6f);
}

TEST(flush_denormal_normal)
{
    ASSERT_NEAR(vortex::flush_denormal(1.0f), 1.0f, 1e-6f);
}

TEST(flush_denormal_zero)
{
    ASSERT_NEAR(vortex::flush_denormal(0.0f), 0.0f, 1e-6f);
}

TEST(cutoff_param_to_hz_min)
{
    // param 0 -> 20 Hz
    ASSERT_NEAR(vortex::cutoff_param_to_hz(0), 20.0f, 0.1f);
}

TEST(cutoff_param_to_hz_mid)
{
    // param 500 -> ~632 Hz (20 * 1000^0.5)
    ASSERT_NEAR(vortex::cutoff_param_to_hz(500), 632.46f, 1.0f);
}

TEST(cutoff_param_to_hz_max)
{
    // param 1000 -> 20000 Hz
    ASSERT_NEAR(vortex::cutoff_param_to_hz(1000), 20000.0f, 1.0f);
}

TEST(resonance_to_damping_zero)
{
    // 0% resonance = Butterworth damping (0.707)
    ASSERT_NEAR(vortex::resonance_to_damping(0), 0.707f, 0.001f);
}

TEST(resonance_to_damping_max)
{
    // 100% resonance = near self-oscillation
    float d = vortex::resonance_to_damping(1000);
    ASSERT(d > 0.0f && d < 0.02f);
}

// --- First-order filter tests ---

TEST(filter1_lp_passes_dc)
{
    // Low-pass should pass DC (1.0 in -> 1.0 out after settling)
    vortex::Filter1 f;
    vortex::filter1_configure_lp(f, 48000.0f, 1000.0f);
    float out = 0.0f;
    for (int i = 0; i < 4800; i++)  // 100ms settling
        out = f.process_lp(1.0f);
    ASSERT_NEAR(out, 1.0f, 0.001f);
}

TEST(filter1_hp_blocks_dc)
{
    // High-pass should block DC (1.0 in -> 0.0 out after settling)
    vortex::Filter1 f;
    vortex::filter1_configure_hp(f, 48000.0f, 1000.0f);
    float out = 1.0f;
    for (int i = 0; i < 4800; i++)
        out = f.process_hp(1.0f);
    ASSERT_NEAR(out, 0.0f, 0.001f);
}

TEST(filter1_lp_attenuates_high_freq)
{
    // LP at 100Hz should significantly attenuate a 10kHz sine
    vortex::Filter1 f;
    vortex::filter1_configure_lp(f, 48000.0f, 100.0f);

    // Run 10kHz sine for 480 samples (10ms), measure last cycle amplitude
    float maxOut = 0.0f;
    for (int i = 0; i < 4800; i++) {
        float in = sinf(2.0f * vortex::PI * 10000.0f * (float)i / 48000.0f);
        float out = f.process_lp(in);
        if (i > 4320)  // last 10% (settled)
            if (fabsf(out) > maxOut) maxOut = fabsf(out);
    }
    // Should be well attenuated (< -20dB)
    ASSERT(maxOut < 0.1f);
}

TEST(filter1_hp_attenuates_low_freq)
{
    // HP at 5kHz should significantly attenuate a 100Hz sine
    vortex::Filter1 f;
    vortex::filter1_configure_hp(f, 48000.0f, 5000.0f);

    float maxOut = 0.0f;
    for (int i = 0; i < 4800; i++) {
        float in = sinf(2.0f * vortex::PI * 100.0f * (float)i / 48000.0f);
        float out = f.process_hp(in);
        if (i > 4320)
            if (fabsf(out) > maxOut) maxOut = fabsf(out);
    }
    ASSERT(maxOut < 0.1f);
}

TEST(filter1_reset)
{
    vortex::Filter1 f;
    vortex::filter1_configure_lp(f, 48000.0f, 1000.0f);
    // Process some signal
    for (int i = 0; i < 100; i++) f.process_lp(1.0f);
    f.reset();
    ASSERT_NEAR(f.z, 0.0f, 1e-6f);
}

// --- Second-order filter tests ---

TEST(filter2_lp_passes_dc)
{
    vortex::Filter2 f;
    vortex::filter2_configure(f, 48000.0f, 1000.0f, 0.707f, vortex::F2_LP);
    float out = 0.0f;
    for (int i = 0; i < 4800; i++)
        out = vortex::filter2_process(f, 1.0f, vortex::F2_LP);
    ASSERT_NEAR(out, 1.0f, 0.001f);
}

TEST(filter2_hp_blocks_dc)
{
    vortex::Filter2 f;
    vortex::filter2_configure(f, 48000.0f, 1000.0f, 0.707f, vortex::F2_HP);
    float out = 1.0f;
    for (int i = 0; i < 4800; i++)
        out = vortex::filter2_process(f, 1.0f, vortex::F2_HP);
    ASSERT_NEAR(out, 0.0f, 0.001f);
}

TEST(filter2_bp_blocks_dc)
{
    vortex::Filter2 f;
    vortex::filter2_configure(f, 48000.0f, 1000.0f, 0.707f, vortex::F2_BP);
    float out = 1.0f;
    for (int i = 0; i < 4800; i++)
        out = vortex::filter2_process(f, 1.0f, vortex::F2_BP);
    ASSERT_NEAR(out, 0.0f, 0.01f);
}

TEST(filter2_notch_passes_dc)
{
    // Notch passes DC (only rejects at center frequency)
    vortex::Filter2 f;
    vortex::filter2_configure(f, 48000.0f, 1000.0f, 0.707f, vortex::F2_NOTCH);
    float out = 0.0f;
    for (int i = 0; i < 4800; i++)
        out = vortex::filter2_process(f, 1.0f, vortex::F2_NOTCH);
    ASSERT_NEAR(out, 1.0f, 0.001f);
}

TEST(filter2_ap_passes_dc)
{
    // Allpass passes DC (unity gain, only phase shifts)
    vortex::Filter2 f;
    vortex::filter2_configure(f, 48000.0f, 1000.0f, 0.707f, vortex::F2_AP);
    float out = 0.0f;
    for (int i = 0; i < 4800; i++)
        out = vortex::filter2_process(f, 1.0f, vortex::F2_AP);
    ASSERT_NEAR(out, 1.0f, 0.001f);
}

TEST(filter2_lp_attenuates_high_freq)
{
    vortex::Filter2 f;
    vortex::filter2_configure(f, 48000.0f, 100.0f, 0.707f, vortex::F2_LP);
    float maxOut = 0.0f;
    for (int i = 0; i < 4800; i++) {
        float in = sinf(2.0f * vortex::PI * 10000.0f * (float)i / 48000.0f);
        float out = vortex::filter2_process(f, in, vortex::F2_LP);
        if (i > 4320)
            if (fabsf(out) > maxOut) maxOut = fabsf(out);
    }
    // 12dB/oct should attenuate more than 6dB/oct
    ASSERT(maxOut < 0.01f);
}

TEST(filter2_resonance_peak)
{
    // High resonance (low damping) should create a peak at cutoff
    vortex::Filter2 f;
    float cutoff = 1000.0f;
    vortex::filter2_configure(f, 48000.0f, cutoff, 0.05f, vortex::F2_LP);

    // Feed sine at cutoff frequency, should resonate
    float maxOut = 0.0f;
    for (int i = 0; i < 9600; i++) {
        float in = sinf(2.0f * vortex::PI * cutoff * (float)i / 48000.0f) * 0.1f;
        float out = vortex::filter2_process(f, in, vortex::F2_LP);
        if (i > 4800)
            if (fabsf(out) > maxOut) maxOut = fabsf(out);
    }
    // Output should be boosted above input amplitude (0.1)
    ASSERT(maxOut > 0.2f);
}

TEST(filter2_cascade_steeper)
{
    // 4-pole (two cascaded 2nd-order) should attenuate more than single 2nd-order
    float fs = 48000.0f;
    float fc = 500.0f;
    float testFreq = 8000.0f;

    // Single stage
    vortex::Filter2 f1;
    vortex::filter2_configure(f1, fs, fc, 0.707f, vortex::F2_LP);
    float maxSingle = 0.0f;
    for (int i = 0; i < 4800; i++) {
        float in = sinf(2.0f * vortex::PI * testFreq * (float)i / fs);
        float out = vortex::filter2_process(f1, in, vortex::F2_LP);
        if (i > 4320)
            if (fabsf(out) > maxSingle) maxSingle = fabsf(out);
    }

    // Two cascaded stages
    vortex::Filter2 f2a, f2b;
    vortex::filter2_configure(f2a, fs, fc, 0.707f, vortex::F2_LP);
    vortex::filter2_configure(f2b, fs, fc, 0.707f, vortex::F2_LP);
    float maxCascade = 0.0f;
    for (int i = 0; i < 4800; i++) {
        float in = sinf(2.0f * vortex::PI * testFreq * (float)i / fs);
        float mid = vortex::filter2_process(f2a, in, vortex::F2_LP);
        float out = vortex::filter2_process(f2b, mid, vortex::F2_LP);
        if (i > 4320)
            if (fabsf(out) > maxCascade) maxCascade = fabsf(out);
    }

    // Cascade should be significantly more attenuated
    ASSERT(maxCascade < maxSingle * 0.5f);
}

TEST(filter2_reset)
{
    vortex::Filter2 f;
    vortex::filter2_configure(f, 48000.0f, 1000.0f, 0.707f, vortex::F2_LP);
    for (int i = 0; i < 100; i++) vortex::filter2_process(f, 1.0f, vortex::F2_LP);
    f.reset();
    ASSERT_NEAR(f.z0, 0.0f, 1e-6f);
    ASSERT_NEAR(f.z1, 0.0f, 1e-6f);
}

int main()
{
    printf("Vortex DSP Tests\n");
    printf("================\n\n");

    printf("Utilities:\n");
    run_soft_clip_zero();
    run_soft_clip_unity();
    run_soft_clip_symmetry();
    run_soft_clip_saturation();
    run_midi_note_to_freq_a4();
    run_midi_note_to_freq_c4();
    run_voct_to_freq_0v();
    run_voct_to_freq_1v();
    run_voct_to_mult_zero();
    run_voct_to_mult_one();
    run_flush_denormal_normal();
    run_flush_denormal_zero();
    run_cutoff_param_to_hz_min();
    run_cutoff_param_to_hz_mid();
    run_cutoff_param_to_hz_max();
    run_resonance_to_damping_zero();
    run_resonance_to_damping_max();

    printf("\nFirst-order filter:\n");
    run_filter1_lp_passes_dc();
    run_filter1_hp_blocks_dc();
    run_filter1_lp_attenuates_high_freq();
    run_filter1_hp_attenuates_low_freq();
    run_filter1_reset();

    printf("\nSecond-order filter:\n");
    run_filter2_lp_passes_dc();
    run_filter2_hp_blocks_dc();
    run_filter2_bp_blocks_dc();
    run_filter2_notch_passes_dc();
    run_filter2_ap_passes_dc();
    run_filter2_lp_attenuates_high_freq();
    run_filter2_resonance_peak();
    run_filter2_cascade_steeper();
    run_filter2_reset();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
