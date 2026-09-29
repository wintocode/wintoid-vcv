#pragma once

#include "../Vortex/dsp.h"

namespace vortex_v2 {

static const int OUTPUT_COUNT = 12;
static const float MIN_CUTOFF_HZ = 20.0f;
static const float MAX_CUTOFF_HZ = 20000.0f;

// Normalized Cutoff knob position to frequency. The logarithmic response
// gives each octave the same amount of knob travel.
inline float cutoff_param_to_hz(float param)
{
    if (!std::isfinite(param))
        param = 0.0f;
    param = fminf(1.0f, fmaxf(0.0f, param));
    return MIN_CUTOFF_HZ * powf(MAX_CUTOFF_HZ / MIN_CUTOFF_HZ, param);
}

inline float cutoff_hz_to_param(float hz)
{
    if (!std::isfinite(hz))
        hz = MIN_CUTOFF_HZ;
    hz = fminf(MAX_CUTOFF_HZ, fmaxf(MIN_CUTOFF_HZ, hz));
    return logf(hz / MIN_CUTOFF_HZ)
        / logf(MAX_CUTOFF_HZ / MIN_CUTOFF_HZ);
}

inline float cutoff_with_voct(float cutoff, float voltage)
{
    if (!std::isfinite(cutoff))
        cutoff = MIN_CUTOFF_HZ;
    if (!std::isfinite(voltage))
        voltage = 0.0f;
    const float shifted = cutoff * vortex::voct_to_mult(voltage);
    if (!std::isfinite(shifted))
        return voltage < 0.0f ? MIN_CUTOFF_HZ : MAX_CUTOFF_HZ;
    return fminf(MAX_CUTOFF_HZ, fmaxf(MIN_CUTOFF_HZ, shifted));
}

// V/Oct and Cutoff CV are both octave offsets. Sum them before the single
// range limit so opposing modulation cancels even when either alone would
// push the cutoff past 20 Hz or 20 kHz.
inline float cutoff_with_modulation(float cutoff, float voct, float cutoffCv)
{
    if (!std::isfinite(voct))
        voct = 0.0f;
    if (!std::isfinite(cutoffCv))
        cutoffCv = 0.0f;
    return cutoff_with_voct(cutoff, voct + cutoffCv);
}

enum OutputMode {
    LP6 = 0, LP12, LP24,
    HP6, HP12, HP24,
    BP, BP_PLUS,
    NOTCH, NOTCH_PLUS,
    AP, AP_PLUS
};

static const OutputMode OUTPUT_MODES[OUTPUT_COUNT] = {
    LP6, LP12, LP24, HP6, HP12, HP24,
    BP, BP_PLUS, NOTCH, NOTCH_PLUS, AP, AP_PLUS
};

// Drive-stage saturation for VortexV2. vortex::soft_clip tracks a tanh curve
// up to about |x| = 3, then continues linearly as x/9 without bound, so hot
// inputs (and feedback builds) pass through nearly at gain. Bound the output
// at +/-3 while keeping the curve identical to V1 below that point.
inline float drive_saturate(float x)
{
    const float y = vortex::soft_clip(x);
    // A non-finite y must fall back to silence, not a latched DC value
    if (!std::isfinite(y))
        return 0.f;
    return fminf(3.f, fmaxf(-3.f, y));
}

struct BranchState {
    vortex::Filter1 f1;
    vortex::Filter2 f2a;
    vortex::Filter2 f2b;
    float configuredSampleRate = 0.f;
    float configuredCutoff = 0.f;
    float configuredDamping = 0.f;
    int configuredMode = -1;

    void reset() {
        f1.reset();
        f2a.reset();
        f2b.reset();
        configuredMode = -1;
    }
};

struct VoiceState {
    BranchState branches[OUTPUT_COUNT];

    void reset() {
        for (int output = 0; output < OUTPUT_COUNT; ++output)
            branches[output].reset();
    }
};

inline void configure_filter2_branch(BranchState& branch,
                                     float sampleRate,
                                     float cutoff,
                                     float damping,
                                     vortex::Filter2Type type,
                                     bool cascade)
{
    vortex::filter2_configure(
        branch.f2a, sampleRate, cutoff, damping, type);
    if (cascade)
        vortex::filter2_configure(
            branch.f2b, sampleRate, cutoff, damping, type);
}

inline bool configure_branch(BranchState& branch,
                             OutputMode mode,
                             float sampleRate,
                             float cutoff,
                             float damping)
{
    switch (mode) {
    case LP6:
        vortex::filter1_configure_lp(branch.f1, sampleRate, cutoff);
        break;
    case HP6:
        vortex::filter1_configure_hp(branch.f1, sampleRate, cutoff);
        break;
    case LP12:
    case LP24:
        configure_filter2_branch(branch, sampleRate, cutoff, damping,
            vortex::F2_LP, mode == LP24);
        break;
    case HP12:
    case HP24:
        configure_filter2_branch(branch, sampleRate, cutoff, damping,
            vortex::F2_HP, mode == HP24);
        break;
    case BP:
    case BP_PLUS:
        configure_filter2_branch(branch, sampleRate, cutoff, damping,
            vortex::F2_BP, mode == BP_PLUS);
        break;
    case NOTCH:
    case NOTCH_PLUS:
        configure_filter2_branch(branch, sampleRate, cutoff, damping,
            vortex::F2_NOTCH, mode == NOTCH_PLUS);
        break;
    case AP:
    case AP_PLUS:
        configure_filter2_branch(branch, sampleRate, cutoff, damping,
            vortex::F2_AP, mode == AP_PLUS);
        break;
    default:
        return false;
    }

    return true;
}

inline float process_branch(BranchState& branch,
                            OutputMode mode,
                            float signal,
                            float sampleRate,
                            float cutoff,
                            float damping,
                            bool reuseCoefficients = true)
{
    const bool coefficientsMatch =
        reuseCoefficients && branch.configuredMode == (int)mode &&
        branch.configuredSampleRate == sampleRate &&
        branch.configuredCutoff == cutoff &&
        branch.configuredDamping == damping;
    if (!coefficientsMatch) {
        if (!configure_branch(
                branch, mode, sampleRate, cutoff, damping))
            return 0.f;
        if (cutoff > 0.f && std::isfinite(sampleRate) &&
            ((mode == LP6 || mode == HP6) || std::isfinite(damping))) {
            branch.configuredSampleRate = sampleRate;
            branch.configuredCutoff = cutoff;
            branch.configuredDamping = damping;
            branch.configuredMode = (int)mode;
        }
    }

    switch (mode) {
    case LP6:
        return branch.f1.process_lp(signal);
    case LP12:
        return vortex::filter2_process(branch.f2a, signal, vortex::F2_LP);
    case LP24:
        return vortex::filter2_process(branch.f2b,
               vortex::filter2_process(branch.f2a, signal, vortex::F2_LP),
               vortex::F2_LP);
    case HP6:
        return branch.f1.process_hp(signal);
    case HP12:
        return vortex::filter2_process(branch.f2a, signal, vortex::F2_HP);
    case HP24:
        return vortex::filter2_process(branch.f2b,
               vortex::filter2_process(branch.f2a, signal, vortex::F2_HP),
               vortex::F2_HP);
    case BP:
        return vortex::filter2_process(branch.f2a, signal, vortex::F2_BP);
    case BP_PLUS:
        return vortex::filter2_process(branch.f2b,
               vortex::filter2_process(branch.f2a, signal, vortex::F2_BP),
               vortex::F2_BP);
    case NOTCH:
        return vortex::filter2_process(branch.f2a, signal, vortex::F2_NOTCH);
    case NOTCH_PLUS:
        return vortex::filter2_process(branch.f2b,
               vortex::filter2_process(branch.f2a, signal, vortex::F2_NOTCH),
               vortex::F2_NOTCH);
    case AP:
        return vortex::filter2_process(branch.f2a, signal, vortex::F2_AP);
    case AP_PLUS:
        return vortex::filter2_process(branch.f2b,
               vortex::filter2_process(branch.f2a, signal, vortex::F2_AP),
               vortex::F2_AP);
    default:
        return 0.f;
    }
}

} // namespace vortex_v2
