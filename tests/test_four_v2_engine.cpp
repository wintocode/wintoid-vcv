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
    do { float _a=(a), _b=(b); if (!isfinite(_a) || !isfinite(_b) || fabsf(_a-_b) > (eps)) { \
        printf("FAIL\n    %s:%d: %f != %f (eps=%f)\n", \
               __FILE__, __LINE__, (double)_a, (double)_b, (double)(eps)); \
        exit(1); \
    } } while (0)

#include "../src/FourV2/engine.h"

static four_v2::EngineParams sine_params()
{
    four_v2::EngineParams params;
    params.baseFreq = 261.63f;
    params.algorithm = 0;
    params.pmDepth = 1.f;
    params.master = 1.f;
    for (int op = 0; op < four_v2::OPERATOR_COUNT; ++op)
    {
        params.opCoarse[op] = 5.f;
        params.opFine[op] = 1.f;
        params.opOutput[op] = op == 0 ? 1.f : 0.f;
        params.opWarp[op] = 0.f;
        params.opFold[op] = 0.f;
        params.opFeedback[op] = 0.f;
        params.opFreqMode[op] = four_v2::RATIO_MODE;
        params.opFoldType[op] = 0;
    }
    return params;
}

static float process_samples(four_v2::EngineState& state,
                             const four_v2::EngineParams& params,
                             int count,
                             float sampleTime = 1.f / 48000.f,
                             float externalPmCycles = 0.f)
{
    float result = 0.f;
    for (int i = 0; i < count; ++i)
        result = four_v2::engine_process(state, params, sampleTime,
                                          externalPmCycles);
    return result;
}

TEST(external_pm_is_added_only_to_carriers)
{
    float out[4] = {};
    float levels[4] = {1.f, 1.f, 1.f, 1.f};
    float previous[4] = {};
    float feedbackAmount[4] = {};
    const four_v2::Algorithm& a = four_v2::ALGORITHMS[0];
    ASSERT_NEAR(four_v2::operator_pm_cycles(
        0, out, levels, previous, feedbackAmount, 1.f, 0.25f, a),
        0.25f, 1e-6f);
    ASSERT_NEAR(four_v2::operator_pm_cycles(
        1, out, levels, previous, feedbackAmount, 1.f, 0.25f, a),
        0.f, 1e-6f);
}

TEST(all_carriers_receive_the_same_external_pm)
{
    const four_v2::Algorithm& a = four_v2::ALGORITHMS[6];
    float out[4] = {};
    float levels[4] = {1.f, 1.f, 1.f, 1.f};
    float previous[4] = {};
    float feedbackAmount[4] = {};
    for (int op = 0; op < 4; ++op)
        ASSERT_NEAR(four_v2::operator_pm_cycles(
            op, out, levels, previous, feedbackAmount, 0.f, -0.5f, a),
            -0.5f, 1e-6f);
}

TEST(pm_depth_scales_routed_inter_operator_pm_after_source_output)
{
    const four_v2::Algorithm& algorithm = four_v2::ALGORITHMS[0];
    float opOut[4] = {0.f, 0.8f, 0.f, 0.f};
    float output[4] = {1.f, 0.5f, 0.f, 0.f};
    float previous[4] = {};
    float feedbackAmount[4] = {};
    ASSERT_NEAR(four_v2::operator_pm_cycles(
        0, opOut, output, previous, feedbackAmount,
        0.25f, 0.f, algorithm), 0.1f, 1e-6f);
}

TEST(pm_depth_scales_self_feedback)
{
    const four_v2::Algorithm& algorithm = four_v2::ALGORITHMS[6];
    float opOut[4] = {};
    float output[4] = {1.f, 1.f, 1.f, 1.f};
    float previous[4] = {0.8f, 0.f, 0.f, 0.f};
    float feedbackAmount[4] = {0.5f, 0.f, 0.f, 0.f};
    ASSERT_NEAR(four_v2::operator_pm_cycles(
        0, opOut, output, previous, feedbackAmount,
        0.25f, 0.f, algorithm), 0.0954993f, 1e-6f);
    ASSERT_NEAR(four_v2::operator_pm_cycles(
        0, opOut, output, previous, feedbackAmount,
        0.f, 0.f, algorithm), 0.f, 1e-6f);
}

TEST(self_feedback_is_independent_of_operator_output)
{
    // Documented contract: Output scales an operator's outgoing audio and
    // modulation, but its own Feedback uses the unscaled waveform.
    const four_v2::Algorithm& algorithm = four_v2::ALGORITHMS[6];
    float opOut[4] = {};
    float previous[4] = {0.8f, 0.f, 0.f, 0.f};
    float feedbackAmount[4] = {0.5f, 0.f, 0.f, 0.f};
    const float levels[] = {0.f, 0.25f, 1.f};
    for (float level : levels) {
        float output[4] = {level, 1.f, 1.f, 1.f};
        ASSERT_NEAR(four_v2::operator_pm_cycles(
            0, opOut, output, previous, feedbackAmount,
            1.f, 0.f, algorithm), 0.381997f, 1e-6f);
        ASSERT_NEAR(four_v2::prepared_operator_pm_cycles(
            0, opOut, output, previous, feedbackAmount,
            1.f, 0.f, algorithm), 0.381997f, 1e-6f);
    }
}

TEST(pm_depth_does_not_scale_carrier_amplitude)
{
    four_v2::EngineParams zeroDepth = sine_params();
    zeroDepth.algorithm = 6;
    zeroDepth.baseFreq = 0.f;
    zeroDepth.pmDepth = 0.f;

    four_v2::EngineParams reducedDepth = zeroDepth;
    reducedDepth.pmDepth = 0.25f;

    four_v2::EngineState zeroState;
    four_v2::EngineState reducedState;
    zeroState.ops[0].phase = 0.25f;
    reducedState.ops[0].phase = 0.25f;
    ASSERT_NEAR(four_v2::engine_process(zeroState, zeroDepth, 0.f), 1.f, 1e-5f);
    ASSERT_NEAR(four_v2::engine_process(reducedState, reducedDepth, 0.f), 1.f, 1e-5f);
}

TEST(pm_depth_does_not_scale_external_pm)
{
    const four_v2::Algorithm& algorithm = four_v2::ALGORITHMS[6];
    float opOut[4] = {};
    float output[4] = {1.f, 1.f, 1.f, 1.f};
    float previous[4] = {};
    float feedbackAmount[4] = {};
    ASSERT_NEAR(four_v2::operator_pm_cycles(
        0, opOut, output, previous, feedbackAmount,
        0.f, -0.375f, algorithm), -0.375f, 1e-6f);
    ASSERT_NEAR(four_v2::operator_pm_cycles(
        0, opOut, output, previous, feedbackAmount,
        0.25f, -0.375f, algorithm), -0.375f, 1e-6f);
}

TEST(over_detector_holds_for_250_ms)
{
    four_v2::OverDetector over;
    ASSERT(over.process(10.f, 0.001f));
    for (int i = 0; i < 249; ++i)
        ASSERT(over.process(0.f, 0.001f));
    ASSERT(!over.process(0.f, 0.001f));
}

TEST(algorithm_seven_sums_four_aligned_carriers_raw)
{
    four_v2::EngineParams params = sine_params();
    params.algorithm = 6;
    params.opOutput[0] = 1.f;
    params.opOutput[1] = 1.f;
    params.opOutput[2] = 1.f;
    params.opOutput[3] = 1.f;
    params.baseFreq = 0.f;

    four_v2::EngineState state;
    state.ops[0].phase = 0.25f;
    state.ops[1].phase = 0.25f;
    state.ops[2].phase = 0.25f;
    state.ops[3].phase = 0.25f;
    const float output = four_v2::engine_process(state, params, 0.f);
    ASSERT_NEAR(output, 4.f, 1e-5f);
}

TEST(master_scales_raw_carrier_sum)
{
    four_v2::EngineParams params = sine_params();
    params.algorithm = 6;
    for (int op = 0; op < 4; ++op)
        params.opOutput[op] = 1.f;
    params.master = 0.25f;
    params.baseFreq = 0.f;

    four_v2::EngineState state;
    for (int op = 0; op < 4; ++op)
        state.ops[op].phase = 0.25f;
    ASSERT_NEAR(four_v2::engine_process(state, params, 0.f), 1.f, 1e-5f);
}

TEST(default_raw_coarse_uses_one_to_one_ratio_and_fixed_hz_mapping)
{
    four_v2::EngineParams ratio = sine_params();
    ratio.baseFreq = 100.f;
    ratio.opOutput[0] = 1.f;
    ratio.opFreqMode[0] = four_v2::RATIO_MODE;

    four_v2::EngineParams fixed = ratio;
    fixed.opFreqMode[0] = four_v2::FIXED_MODE;

    four_v2::EngineState ratioState;
    four_v2::EngineState fixedState;
    four_v2::engine_process(ratioState, ratio, 1.f / 48000.f);
    four_v2::engine_process(fixedState, fixed, 1.f / 48000.f);
    ASSERT_NEAR(ratioState.ops[0].phase, 100.f / 48000.f, 1e-6f);
    ASSERT_NEAR(fixedState.ops[0].phase,
                four_v2::fixed_hz(5.f) / 48000.f, 1e-6f);
    ASSERT(fabsf(ratioState.ops[0].phase - fixedState.ops[0].phase) > 0.001f);
}

TEST(external_pm_is_signed_and_not_scaled_by_pm_depth)
{
    four_v2::EngineParams params = sine_params();
    params.baseFreq = 0.f;
    params.pmDepth = 0.f;

    four_v2::EngineState positive;
    four_v2::EngineState negative;
    positive.ops[0].phase = 0.f;
    negative.ops[0].phase = 0.f;
    const float plus = four_v2::engine_process(positive, params, 0.f, 0.25f);
    const float minus = four_v2::engine_process(negative, params, 0.f, -0.25f);
    ASSERT_NEAR(plus, 1.f, 1e-5f);
    ASSERT_NEAR(minus, -1.f, 1e-5f);

    four_v2::EngineState shifted;
    shifted.ops[0].phase = 0.f;
    const float noExternal = four_v2::engine_process(shifted, params, 0.f, 0.f);
    ASSERT_NEAR(noExternal, 0.f, 1e-5f);
}

TEST(warp_and_fold_are_applied_after_external_pm)
{
    four_v2::EngineParams sine = sine_params();
    sine.baseFreq = 0.f;
    sine.opOutput[0] = 1.f;
    sine.opWarp[0] = 1.f / 3.f;
    sine.opFold[0] = 0.5f;
    sine.opFoldType[0] = 0;

    four_v2::EngineState state;
    state.ops[0].phase = 0.f;
    const float output = four_v2::engine_process(state, sine, 0.f, 0.25f);
    const float expected = four_v2::wave_fold(
        four_v2::wave_warp_blep(0.25f, 1.f / 3.f, 0.f), 0.5f, 0);
    ASSERT_NEAR(output, expected, 1e-5f);
    ASSERT_NEAR(output, -1.f, 1e-5f);
}

TEST(zero_output_does_not_stop_phase_advancement)
{
    four_v2::EngineParams silent = sine_params();
    silent.opOutput[0] = 0.f;
    four_v2::EngineState state;
    const float before = state.ops[0].phase;
    process_samples(state, silent, 8);
    ASSERT(state.ops[0].phase > before);
}

TEST(invalid_controls_produce_finite_audio_and_preserve_safe_state)
{
    four_v2::EngineParams params = sine_params();
    params.algorithm = 999;
    params.baseFreq = NAN;
    params.master = NAN;
    params.pmDepth = NAN;
    for (int op = 0; op < 4; ++op)
    {
        params.opCoarse[op] = NAN;
        params.opFine[op] = NAN;
        params.opOutput[op] = NAN;
        params.opWarp[op] = NAN;
        params.opFold[op] = NAN;
        params.opFeedback[op] = NAN;
        params.opFreqMode[op] = 99;
        params.opFoldType[op] = 99;
    }

    four_v2::EngineState state;
    state.ops[0].phase = NAN;
    state.ops[1].prevOutput = INFINITY;
    const float output = four_v2::engine_process(state, params, NAN, NAN);
    ASSERT(isfinite(output));
    for (int op = 0; op < 4; ++op)
    {
        ASSERT(isfinite(state.ops[op].phase));
        ASSERT(isfinite(state.ops[op].prevOutput));
    }
}

TEST(extreme_frequencies_are_clamped_before_phase_increment)
{
    four_v2::EngineParams params = sine_params();
    params.baseFreq = 1e30f;
    params.opOutput[0] = 1.f;
    params.opFine[0] = 1e30f;
    params.opFreqMode[0] = four_v2::FIXED_MODE;
    params.opCoarse[0] = 14.f;

    four_v2::EngineState state;
    const float output = four_v2::engine_process(state, params, 1.f / 48000.f);
    ASSERT(isfinite(output));
    ASSERT(state.ops[0].phase >= 0.f && state.ops[0].phase < 1.f);
}

TEST(external_pm_is_injected_into_each_carrier_phase)
{
    four_v2::EngineParams params = sine_params();
    params.algorithm = 6;
    params.baseFreq = 0.f;
    for (int op = 0; op < 4; ++op)
        params.opOutput[op] = 1.f;

    four_v2::EngineState state;
    for (int op = 0; op < 4; ++op)
        state.ops[op].phase = 0.f;
    const float output = four_v2::engine_process(state, params, 0.f, 0.25f);
    ASSERT_NEAR(output, 4.f, 1e-5f);
}

TEST(interleaved_lane_states_match_separately_processed_reference_states)
{
    four_v2::EngineParams params = sine_params();
    params.baseFreq = 220.f;
    params.opOutput[0] = 1.f;
    params.opOutput[1] = 0.3f;
    params.opWarp[0] = 0.2f;
    params.opFeedback[0] = 0.4f;

    four_v2::EngineState laneA;
    four_v2::EngineState laneB;
    four_v2::EngineState referenceA;
    four_v2::EngineState referenceB;
    laneA.ops[0].phase = 0.1f;
    laneB.ops[0].phase = 0.7f;
    referenceA = laneA;
    referenceB = laneB;

    const float dt = 1.f / 48000.f;
    for (int i = 0; i < 24; ++i)
    {
        four_v2::engine_process(laneA, params, dt, 0.1f);
        four_v2::engine_process(laneB, params, dt, -0.2f);
    }
    for (int i = 0; i < 24; ++i)
    {
        four_v2::engine_process(referenceA, params, dt, 0.1f);
        four_v2::engine_process(referenceB, params, dt, -0.2f);
    }

    for (int op = 0; op < 4; ++op)
    {
        ASSERT_NEAR(laneA.ops[op].phase, referenceA.ops[op].phase, 1e-7f);
        ASSERT_NEAR(laneA.ops[op].prevOutput,
                    referenceA.ops[op].prevOutput, 1e-7f);
        ASSERT_NEAR(laneB.ops[op].phase, referenceB.ops[op].phase, 1e-7f);
        ASSERT_NEAR(laneB.ops[op].prevOutput,
                    referenceB.ops[op].prevOutput, 1e-7f);
    }
}

TEST(reset_clears_operator_and_filter_state)
{
    four_v2::EngineState state;
    state.ops[0].phase = 0.5f;
    state.ops[0].prevOutput = 0.25f;
    state.dcBlocker.prevInput = 0.75f;
    state.dcBlocker.prevOutput = -0.25f;
    four_v2::reset(state);
    for (int op = 0; op < 4; ++op)
    {
        ASSERT_NEAR(state.ops[op].phase, 0.f, 0.f);
        ASSERT_NEAR(state.ops[op].prevOutput, 0.f, 0.f);
    }
    ASSERT_NEAR(state.dcBlocker.prevInput, 0.f, 0.f);
    ASSERT_NEAR(state.dcBlocker.prevOutput, 0.f, 0.f);
}

int main()
{
    printf("Four V2 engine tests:\n");
    run_external_pm_is_added_only_to_carriers();
    run_all_carriers_receive_the_same_external_pm();
    run_pm_depth_scales_routed_inter_operator_pm_after_source_output();
    run_pm_depth_scales_self_feedback();
    run_self_feedback_is_independent_of_operator_output();
    run_pm_depth_does_not_scale_carrier_amplitude();
    run_pm_depth_does_not_scale_external_pm();
    run_over_detector_holds_for_250_ms();
    run_algorithm_seven_sums_four_aligned_carriers_raw();
    run_master_scales_raw_carrier_sum();
    run_default_raw_coarse_uses_one_to_one_ratio_and_fixed_hz_mapping();
    run_external_pm_is_signed_and_not_scaled_by_pm_depth();
    run_warp_and_fold_are_applied_after_external_pm();
    run_zero_output_does_not_stop_phase_advancement();
    run_invalid_controls_produce_finite_audio_and_preserve_safe_state();
    run_extreme_frequencies_are_clamped_before_phase_increment();
    run_external_pm_is_injected_into_each_carrier_phase();
    run_interleaved_lane_states_match_separately_processed_reference_states();
    run_reset_clears_operator_and_filter_state();
    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
