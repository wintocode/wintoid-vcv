#ifndef WINTOID_FOUR_V2_ENGINE_H
#define WINTOID_FOUR_V2_ENGINE_H

// Host-independent FourV2 operator engine.
// No VCV Rack API dependencies — testable on desktop and reusable by hosts.

#include <float.h>
#include <math.h>

#include "dsp.h"

namespace four_v2 {

struct OperatorState
{
    float phase = 0.f;
    float prevOutput = 0.f;
};

struct EngineState
{
    OperatorState ops[OPERATOR_COUNT];
    DCBlocker dcBlocker;
};

inline void reset(EngineState& state)
{
    state = EngineState();
}

struct EngineParams
{
    int algorithm = 0;
    float pmDepth = 1.f;
    float master = 1.f;
    float baseFreq = 261.63f;
    float opCoarse[4] = {5.f, 5.f, 5.f, 5.f};
    float opFine[4] = {1.f, 1.f, 1.f, 1.f};
    float opOutput[4] = {1.f, 0.f, 0.f, 0.f};
    float opWarp[4] = {};
    float opFold[4] = {};
    float opFeedback[4] = {};
    int opFreqMode[4] = {};
    int opFoldType[4] = {};
};

struct OverDetector
{
    float remaining = 0.f;

    void reset()
    {
        remaining = 0.f;
    }

    bool process(float peakVolts, float sampleTime)
    {
        peakVolts = finite_or(peakVolts, 0.f);
        sampleTime = fmaxf(0.f, finite_or(sampleTime, 0.f));
        if (fabsf(peakVolts) >= 10.f)
            remaining = 0.25f;
        else
            remaining = fmaxf(0.f, remaining - sampleTime);
        return remaining > 0.f;
    }
};

inline float engine_clamp(float value, float low, float high)
{
    value = finite_or(value, low);
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

inline float engine_signal(float value)
{
    return engine_clamp(value, -1.f, 1.f);
}

inline float engine_unit(float value, float fallback)
{
    return engine_clamp(finite_or(value, fallback), 0.f, 1.f);
}

inline int engine_algorithm_index(int value)
{
    if (value < 0)
        return 0;
    if (value >= ALGORITHM_COUNT)
        return ALGORITHM_COUNT - 1;
    return value;
}

inline int engine_fold_type(int value)
{
    if (value < 0)
        return 0;
    if (value > 2)
        return 2;
    return value;
}

inline float wrap_phase(float phase)
{
    phase = finite_or(phase, 0.f);
    phase = fmodf(phase, 1.f);
    if (!isfinite(phase))
        return 0.f;
    if (phase < 0.f)
        phase += 1.f;
    return phase;
}

// Return internal and self-feedback PM, plus External PM for carriers only.
inline float operator_pm_cycles(
    int op,
    const float opOut[4],
    const float output[4],
    const float previous[4],
    const float feedbackAmount[4],
    float pmDepth,
    float externalPmCycles,
    const Algorithm& algorithm)
{
    if (op < 0)
        op = 0;
    else if (op >= OPERATOR_COUNT)
        op = OPERATOR_COUNT - 1;

    float safeOpOut[OPERATOR_COUNT] = {};
    float safeOutput[OPERATOR_COUNT] = {};
    float safePrevious[OPERATOR_COUNT] = {};
    float safeFeedbackAmount[OPERATOR_COUNT] = {};
    for (int i = 0; i < OPERATOR_COUNT; ++i)
    {
        safeOpOut[i] = engine_signal(opOut ? opOut[i] : 0.f);
        safeOutput[i] = engine_unit(output ? output[i] : 0.f, 0.f);
        safePrevious[i] = engine_signal(previous ? previous[i] : 0.f);
        safeFeedbackAmount[i] = engine_unit(
            feedbackAmount ? feedbackAmount[i] : 0.f, 0.f);
    }

    const float safePmDepth = engine_unit(pmDepth, 0.f);
    float pm = gather_modulation(op, safeOpOut, safeOutput,
                                 safePmDepth, algorithm);
    pm = finite_or(pm, 0.f);
    pm += calc_feedback(safePrevious[op], safeFeedbackAmount[op]);

    const float safeExternalPm = finite_or(externalPmCycles, 0.f);
    if (algorithm.carrier[op])
        pm += safeExternalPm;
    return finite_or(pm, 0.f);
}

// Fast path for arrays already sanitised by engine_process().
inline float prepared_operator_pm_cycles(
    int op,
    const float opOut[4],
    const float output[4],
    const float previous[4],
    const float feedbackAmount[4],
    float pmDepth,
    float externalPmCycles,
    const Algorithm& algorithm)
{
    float pm = gather_modulation(
        op, opOut, output, pmDepth, algorithm);
    pm += calc_feedback(previous[op], feedbackAmount[op]);
    if (algorithm.carrier[op])
        pm += externalPmCycles;
    return finite_or(pm, 0.f);
}

inline float engine_frequency(const EngineParams& params,
                              int op,
                              float safeBaseFreq)
{
    const int mode = clamp_mode((float)params.opFreqMode[op]);
    const float coarse = finite_or(params.opCoarse[op],
                                   (float)DEFAULT_RATIO_INDEX);
    const float fine = fmaxf(0.f, finite_or(params.opFine[op], 1.f));
    float frequency;
    if (mode == RATIO_MODE)
        frequency = calc_frequency_ratio(
            safeBaseFreq, ratio_value(coarse), fine);
    else
        frequency = calc_frequency_fixed(fixed_hz(coarse), fine);
    return fmaxf(0.f, finite_or(frequency, 0.f));
}

inline float engine_phase_increment(float frequency, float osTime)
{
    if (!(osTime > 0.f) || !isfinite(osTime))
        return 0.f;

    float maxFrequency = 0.5f / osTime;
    maxFrequency = finite_or(maxFrequency, FLT_MAX);
    frequency = engine_clamp(frequency, 0.f, maxFrequency);

    float increment = finite_or(frequency * osTime, 0.f);
    return engine_clamp(increment, 0.f, 0.5f);
}

// Process one host sample with two oversampled operator passes.
// The result is oscillator units: raw carrier sum, DC-blocked, then Master.
inline float engine_process(EngineState& state,
                            const EngineParams& params,
                            float sampleTime,
                            float externalPmCycles = 0.f)
{
    const float safeSampleTime = fmaxf(0.f, finite_or(sampleTime, 0.f));
    const float osTime = safeSampleTime * 0.5f;
    const float safeBaseFreq = fmaxf(
        0.f, finite_or(params.baseFreq, 261.63f));
    const float safePmDepth = engine_unit(params.pmDepth, 1.f);
    const float safeMaster = engine_unit(params.master, 1.f);
    const float safeExternalPm = finite_or(externalPmCycles, 0.f);
    const int algorithmIndex = engine_algorithm_index(params.algorithm);
    const Algorithm& algorithm = ALGORITHMS[algorithmIndex];

    float safeOutput[OPERATOR_COUNT] = {};
    float safeFeedback[OPERATOR_COUNT] = {};
    float increment[OPERATOR_COUNT] = {};
    float warp[OPERATOR_COUNT] = {};
    float fold[OPERATOR_COUNT] = {};
    int foldType[OPERATOR_COUNT] = {};
    for (int op = 0; op < OPERATOR_COUNT; ++op)
    {
        const float defaultOutput = op == 0 ? 1.f : 0.f;
        safeOutput[op] = engine_unit(params.opOutput[op], defaultOutput);
        safeFeedback[op] = engine_unit(params.opFeedback[op], 0.f);
        increment[op] = engine_phase_increment(
            engine_frequency(params, op, safeBaseFreq), osTime);
        warp[op] = engine_unit(params.opWarp[op], 0.f);
        fold[op] = engine_unit(params.opFold[op], 0.f);
        foldType[op] = engine_fold_type(params.opFoldType[op]);
        state.ops[op].phase = wrap_phase(state.ops[op].phase);
        state.ops[op].prevOutput = engine_signal(state.ops[op].prevOutput);
    }

    state.dcBlocker.prevInput = finite_or(state.dcBlocker.prevInput, 0.f);
    state.dcBlocker.prevOutput = finite_or(state.dcBlocker.prevOutput, 0.f);
    state.dcBlocker.R = engine_clamp(
        finite_or(state.dcBlocker.R, 0.999f), 0.f, 1.f);

    float result[2] = {};
    for (int pass = 0; pass < 2; ++pass)
    {
        float opOut[OPERATOR_COUNT] = {};
        float previous[OPERATOR_COUNT] = {};
        for (int op = 0; op < OPERATOR_COUNT; ++op)
            previous[op] = engine_signal(state.ops[op].prevOutput);

        // The immutable routing table only contains feed-forward edges from
        // higher-numbered operators to lower-numbered destinations.
        for (int op = OPERATOR_COUNT - 1; op >= 0; --op)
        {
            phase_advance(state.ops[op].phase, increment[op]);

            const float pm = prepared_operator_pm_cycles(
                op, opOut, safeOutput, previous, safeFeedback,
                safePmDepth, safeExternalPm, algorithm);
            const float modulatedPhase = wrap_phase(
                state.ops[op].phase + pm);

            float signal = wave_warp_blep(
                modulatedPhase, warp[op], increment[op]);
            signal = wave_fold(signal, fold[op], foldType[op]);
            signal = engine_signal(signal);
            opOut[op] = signal;
            state.ops[op].prevOutput = signal;
        }

        result[pass] = finite_or(
            sum_carriers(opOut, safeOutput, algorithm), 0.f);
    }

    float output = finite_or(downsample_2x(result[0], result[1]), 0.f);
    output = finite_or(state.dcBlocker.process(output), 0.f);
    output = finite_or(output * safeMaster, 0.f);
    return output;
}

} // namespace four_v2

#endif // WINTOID_FOUR_V2_ENGINE_H
