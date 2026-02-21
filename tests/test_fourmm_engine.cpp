#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Reuse test macros
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
               __FILE__, __LINE__, _a, _b, (float)(eps)); \
        exit(1); \
    } } while(0)

#include "../src/FourMM/engine.h"

// --- Single operator sine tests ---

// Algorithm 7 (index 7) = all 4 ops are independent carriers
// With only op 1 (index 0) level > 0, output is just op 1's sine.

TEST(single_op_sine_produces_output)
{
    fourmm::EngineState state;
    fourmm::EngineParams params;

    // All defaults: algo 0, baseFreq 261.63, all levels 1.0, all warp/fold/fb 0
    // Algo 0: 4->3->2->1, carrier is op 1 only
    // With no modulation (xm=0), all ops produce independent sines,
    // but only op 1 (index 0) is a carrier.
    params.modMaster = 0.f;
    float sampleTime = 1.f / 48000.f;

    // Run a few samples — output should be non-zero after first sample
    float out = 0.f;
    for ( int i = 0; i < 100; i++ )
        out = fourmm::engine_process( state, params, sampleTime, 0.f );

    ASSERT( fabsf(out) > 0.001f );
}

TEST(single_op_sine_frequency)
{
    // Run engine for one full cycle of 261.63 Hz at 48kHz.
    // One cycle = 48000/261.63 ≈ 183.5 samples.
    // Count zero crossings (positive→negative transitions) — expect ~1 per cycle.
    fourmm::EngineState state;
    fourmm::EngineParams params;
    params.modMaster = 0.f;
    params.algorithm = 7; // All carriers, no modulation
    // Zero out levels for ops 2-4 so only op 1 sounds
    params.opLevel[1] = 0.f;
    params.opLevel[2] = 0.f;
    params.opLevel[3] = 0.f;

    float sampleTime = 1.f / 48000.f;
    int zeroCrossings = 0;
    float prev = 0.f;

    // Run 48000 samples = 1 second
    for ( int i = 0; i < 48000; i++ )
    {
        float out = fourmm::engine_process( state, params, sampleTime, 0.f );
        if ( prev > 0.f && out <= 0.f )
            zeroCrossings++;
        prev = out;
    }

    // 261.63 Hz → ~261-262 zero crossings per second
    ASSERT( zeroCrossings >= 259 );
    ASSERT( zeroCrossings <= 264 );
}

TEST(single_op_vca_zero)
{
    fourmm::EngineState state;
    fourmm::EngineParams params;
    params.globalVCA = 0.f;
    float sampleTime = 1.f / 48000.f;

    for ( int i = 0; i < 100; i++ )
    {
        float out = fourmm::engine_process( state, params, sampleTime, 0.f );
        ASSERT_NEAR( out, 0.f, 1e-10f );
    }
}

TEST(single_op_coarse_ratio_2x)
{
    // Op 1 at coarse ratio 2.0 should produce double the base frequency
    fourmm::EngineState state;
    fourmm::EngineParams params;
    params.algorithm = 7;
    params.opLevel[1] = 0.f;
    params.opLevel[2] = 0.f;
    params.opLevel[3] = 0.f;
    params.opCoarse[0] = 2.0f;  // 2× ratio
    params.modMaster = 0.f;

    float sampleTime = 1.f / 48000.f;
    int zeroCrossings = 0;
    float prev = 0.f;

    for ( int i = 0; i < 48000; i++ )
    {
        float out = fourmm::engine_process( state, params, sampleTime, 0.f );
        if ( prev > 0.f && out <= 0.f )
            zeroCrossings++;
        prev = out;
    }

    // 261.63 * 2 = 523.26 Hz → ~523 crossings
    ASSERT( zeroCrossings >= 520 );
    ASSERT( zeroCrossings <= 526 );
}

// --- Warp, Fold, Feedback tests ---

TEST(warp_changes_timbre)
{
    // With warp > 0, output should differ from pure sine.
    // Run two engines: one with warp=0, one with warp=0.5.
    // Compare RMS — they should be measurably different.
    fourmm::EngineState state0, state1;
    fourmm::EngineParams params;
    params.algorithm = 7;
    params.opLevel[1] = 0.f;
    params.opLevel[2] = 0.f;
    params.opLevel[3] = 0.f;
    params.modMaster = 0.f;
    fourmm::EngineParams paramsWarp = params;
    paramsWarp.opWarp[0] = 0.5f;

    float sampleTime = 1.f / 48000.f;
    float sumDiff2 = 0.f;

    for ( int i = 0; i < 4800; i++ )
    {
        float a = fourmm::engine_process( state0, params, sampleTime, 0.f );
        float b = fourmm::engine_process( state1, paramsWarp, sampleTime, 0.f );
        float d = a - b;
        sumDiff2 += d * d;
    }

    float rmsDiff = sqrtf( sumDiff2 / 4800.f );
    ASSERT( rmsDiff > 0.01f );
}

TEST(fold_increases_harmonics)
{
    // Fold should increase RMS (driven signal has more energy).
    fourmm::EngineState state0, state1;
    fourmm::EngineParams params;
    params.algorithm = 7;
    params.opLevel[1] = 0.f;
    params.opLevel[2] = 0.f;
    params.opLevel[3] = 0.f;
    params.modMaster = 0.f;
    fourmm::EngineParams paramsFold = params;
    paramsFold.opFold[0] = 0.8f;

    float sampleTime = 1.f / 48000.f;
    float rms0 = 0.f, rms1 = 0.f;

    for ( int i = 0; i < 4800; i++ )
    {
        float a = fourmm::engine_process( state0, params, sampleTime, 0.f );
        float b = fourmm::engine_process( state1, paramsFold, sampleTime, 0.f );
        rms0 += a * a;
        rms1 += b * b;
    }

    // With fold, waveform shape changes — RMS should differ
    rms0 = sqrtf( rms0 / 4800.f );
    rms1 = sqrtf( rms1 / 4800.f );
    ASSERT( fabsf( rms0 - rms1 ) > 0.01f );
}

TEST(feedback_modulates_phase)
{
    // Feedback should change the timbre compared to no feedback.
    fourmm::EngineState state0, state1;
    fourmm::EngineParams params;
    params.algorithm = 7;
    params.opLevel[1] = 0.f;
    params.opLevel[2] = 0.f;
    params.opLevel[3] = 0.f;
    params.modMaster = 0.f;
    fourmm::EngineParams paramsFb = params;
    paramsFb.opFeedback[0] = 0.5f;

    float sampleTime = 1.f / 48000.f;
    float sumDiff2 = 0.f;

    for ( int i = 0; i < 4800; i++ )
    {
        float a = fourmm::engine_process( state0, params, sampleTime, 0.f );
        float b = fourmm::engine_process( state1, paramsFb, sampleTime, 0.f );
        float d = a - b;
        sumDiff2 += d * d;
    }

    float rmsDiff = sqrtf( sumDiff2 / 4800.f );
    ASSERT( rmsDiff > 0.01f );
}

// --- Algorithm routing tests ---

// Helper: count zero crossings over 1 second
static int count_zero_crossings( fourmm::EngineParams& params )
{
    fourmm::EngineState state;
    float sampleTime = 1.f / 48000.f;
    int zc = 0;
    float prev = 0.f;
    for ( int i = 0; i < 48000; i++ )
    {
        float out = fourmm::engine_process( state, params, sampleTime, 0.f );
        if ( prev > 0.f && out <= 0.f ) zc++;
        prev = out;
    }
    return zc;
}

TEST(algo0_deep_chain_has_fm_character)
{
    // Algo 0: 4->3->2->1. With xm > 0, output should differ from pure sine.
    fourmm::EngineParams params;
    params.algorithm = 0;
    params.modMaster = 0.5f;

    fourmm::EngineParams paramsClean;
    paramsClean.algorithm = 0;
    paramsClean.modMaster = 0.f;

    fourmm::EngineState s0, s1;
    float sampleTime = 1.f / 48000.f;
    float sumDiff2 = 0.f;

    for ( int i = 0; i < 4800; i++ )
    {
        float a = fourmm::engine_process( s0, paramsClean, sampleTime, 0.f );
        float b = fourmm::engine_process( s1, params, sampleTime, 0.f );
        float d = a - b;
        sumDiff2 += d * d;
    }

    ASSERT( sqrtf( sumDiff2 / 4800.f ) > 0.01f );
}

TEST(algo7_additive_four_carriers)
{
    // Algo 7: 1+2+3+4, all carriers, no modulation.
    // With all ops at same freq and level, output should be ~4x a single op.
    fourmm::EngineState stateAll, stateOne;
    fourmm::EngineParams paramsAll;
    paramsAll.algorithm = 7;
    paramsAll.modMaster = 0.f;

    fourmm::EngineParams paramsOne;
    paramsOne.algorithm = 7;
    paramsOne.modMaster = 0.f;
    paramsOne.opLevel[1] = 0.f;
    paramsOne.opLevel[2] = 0.f;
    paramsOne.opLevel[3] = 0.f;

    float sampleTime = 1.f / 48000.f;
    // Skip transient
    for ( int i = 0; i < 100; i++ )
    {
        fourmm::engine_process( stateAll, paramsAll, sampleTime, 0.f );
        fourmm::engine_process( stateOne, paramsOne, sampleTime, 0.f );
    }

    float outAll = fourmm::engine_process( stateAll, paramsAll, sampleTime, 0.f );
    float outOne = fourmm::engine_process( stateOne, paramsOne, sampleTime, 0.f );

    // All 4 carriers at same freq/phase should sum to ~4x one carrier
    ASSERT_NEAR( outAll, outOne * 4.f, 0.05f );
}

TEST(algo4_two_carrier_pairs)
{
    // Algo 4: (4->3) + (2->1), carriers: {1, 3}
    // With xm=0, carriers 1 and 3 should both contribute.
    // Muting op 3 should halve the output.
    fourmm::EngineParams paramsFull;
    paramsFull.algorithm = 4;
    paramsFull.modMaster = 0.f;

    fourmm::EngineParams paramsNoOp3;
    paramsNoOp3.algorithm = 4;
    paramsNoOp3.modMaster = 0.f;
    paramsNoOp3.opLevel[2] = 0.f;  // Mute op 3 (index 2)

    fourmm::EngineState s0, s1;
    float sampleTime = 1.f / 48000.f;

    for ( int i = 0; i < 100; i++ )
    {
        fourmm::engine_process( s0, paramsFull, sampleTime, 0.f );
        fourmm::engine_process( s1, paramsNoOp3, sampleTime, 0.f );
    }

    float full = fourmm::engine_process( s0, paramsFull, sampleTime, 0.f );
    float half = fourmm::engine_process( s1, paramsNoOp3, sampleTime, 0.f );

    // Full should be ~2x half (two carriers vs one)
    ASSERT_NEAR( fabsf(full), fabsf(half) * 2.f, 0.1f );
}

TEST(all_algorithms_produce_output)
{
    // Every algorithm should produce non-zero output with default params.
    float sampleTime = 1.f / 48000.f;

    for ( int a = 0; a < 11; a++ )
    {
        fourmm::EngineState state;
        fourmm::EngineParams params;
        params.algorithm = a;
        params.modMaster = 0.f;

        float maxAbs = 0.f;
        for ( int i = 0; i < 200; i++ )
        {
            float out = fourmm::engine_process( state, params, sampleTime, 0.f );
            if ( fabsf(out) > maxAbs ) maxAbs = fabsf(out);
        }

        ASSERT( maxAbs > 0.001f );
    }
}

TEST(fm_modulation_changes_spectrum)
{
    // For algorithms with modulation (0-6, 8-10), xm > 0 should
    // change the output compared to xm = 0.
    float sampleTime = 1.f / 48000.f;
    int algosWithMod[] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10 };

    for ( int ai = 0; ai < 10; ai++ )
    {
        int a = algosWithMod[ai];
        fourmm::EngineState s0, s1;
        fourmm::EngineParams p0, p1;
        p0.algorithm = a; p0.modMaster = 0.f;
        p1.algorithm = a; p1.modMaster = 0.8f;

        float sumDiff2 = 0.f;
        for ( int i = 0; i < 4800; i++ )
        {
            float a0 = fourmm::engine_process( s0, p0, sampleTime, 0.f );
            float a1 = fourmm::engine_process( s1, p1, sampleTime, 0.f );
            float d = a0 - a1;
            sumDiff2 += d * d;
        }

        float rmsDiff = sqrtf( sumDiff2 / 4800.f );
        ASSERT( rmsDiff > 0.001f );
    }
}

// --- Fixed freq mode and fine tuning ---

TEST(fixed_freq_mode)
{
    // Op 1 in fixed mode at 440 Hz should produce 440 Hz regardless of baseFreq.
    fourmm::EngineParams params;
    params.algorithm = 7;
    params.opLevel[1] = 0.f;
    params.opLevel[2] = 0.f;
    params.opLevel[3] = 0.f;
    params.modMaster = 0.f;

    params.opFreqMode[0] = 1;  // Fixed mode
    params.opCoarse[0] = 440.f;  // 440 Hz
    params.opFine[0] = 1.f;     // No fine offset
    params.baseFreq = 100.f;    // Should be ignored in fixed mode

    int zc = count_zero_crossings( params );
    // 440 Hz -> ~440 zero crossings
    ASSERT( zc >= 437 );
    ASSERT( zc <= 443 );
}

TEST(fine_tune_shifts_pitch)
{
    // Fine = +100 cents (1 semitone up) on 261.63 Hz -> ~277.18 Hz
    fourmm::EngineParams params;
    params.algorithm = 7;
    params.opLevel[1] = 0.f;
    params.opLevel[2] = 0.f;
    params.opLevel[3] = 0.f;
    params.modMaster = 0.f;
    params.opFine[0] = exp2f( 100.f / 1200.f );  // +100 cents

    int zc = count_zero_crossings( params );
    // 261.63 * 2^(100/1200) ~ 277.18 Hz
    ASSERT( zc >= 274 );
    ASSERT( zc <= 280 );
}

// --- Main ---

// --- DC blocker and output bounds ---

TEST(dc_blocker_removes_offset)
{
    // Asymmetric fold + feedback can introduce DC offset.
    // After DC blocker, mean should be near zero.
    fourmm::EngineState state;
    fourmm::EngineParams params;
    params.algorithm = 7;
    params.opLevel[1] = 0.f;
    params.opLevel[2] = 0.f;
    params.opLevel[3] = 0.f;
    params.modMaster = 0.f;
    params.opFeedback[0] = 0.8f;
    params.opFold[0] = 0.5f;
    params.opFoldType[0] = 1;  // Asymmetric fold can introduce DC

    float sampleTime = 1.f / 48000.f;
    float sum = 0.f;
    int N = 48000;

    for ( int i = 0; i < N; i++ )
        sum += fourmm::engine_process( state, params, sampleTime, 0.f );

    float mean = sum / (float)N;
    ASSERT( fabsf(mean) < 0.05f );
}

TEST(output_bounded)
{
    // Even with extreme settings, output should stay bounded.
    fourmm::EngineState state;
    fourmm::EngineParams params;
    params.algorithm = 0;
    params.modMaster = 1.0f;
    params.opFeedback[0] = 1.0f;
    params.opFeedback[1] = 1.0f;
    params.opFeedback[2] = 1.0f;
    params.opFeedback[3] = 1.0f;
    params.opFold[0] = 1.0f;
    params.opWarp[0] = 1.0f;

    float sampleTime = 1.f / 48000.f;
    float maxAbs = 0.f;

    for ( int i = 0; i < 48000; i++ )
    {
        float out = fourmm::engine_process( state, params, sampleTime, 0.f );
        if ( fabsf(out) > maxAbs ) maxAbs = fabsf(out);
    }

    ASSERT( maxAbs < 10.f );
}

int main()
{
    printf("Engine tests:\n");
    run_single_op_sine_produces_output();
    run_single_op_sine_frequency();
    run_single_op_vca_zero();
    run_single_op_coarse_ratio_2x();
    run_warp_changes_timbre();
    run_fold_increases_harmonics();
    run_feedback_modulates_phase();
    run_algo0_deep_chain_has_fm_character();
    run_algo7_additive_four_carriers();
    run_algo4_two_carrier_pairs();
    run_all_algorithms_produce_output();
    run_fm_modulation_changes_spectrum();
    run_fixed_freq_mode();
    run_fine_tune_shifts_pitch();
    run_dc_blocker_removes_offset();
    run_output_bounded();

    printf("\n%d/%d engine tests passed.\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
