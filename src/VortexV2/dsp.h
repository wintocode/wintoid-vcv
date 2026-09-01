#pragma once

#include "../Vortex/dsp.h"

namespace vortex_v2 {

static const int OUTPUT_COUNT = 12;

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

    void reset() {
        f1.reset();
        f2a.reset();
        f2b.reset();
    }
};

struct VoiceState {
    BranchState branches[OUTPUT_COUNT];

    void reset() {
        for (int output = 0; output < OUTPUT_COUNT; ++output)
            branches[output].reset();
    }
};

inline float process_branch(BranchState& branch,
                            OutputMode mode,
                            float signal,
                            float sampleRate,
                            float cutoff,
                            float damping)
{
    switch (mode) {
    case LP6:
        vortex::filter1_configure_lp(branch.f1, sampleRate, cutoff);
        return branch.f1.process_lp(signal);
    case LP12:
        vortex::filter2_configure(branch.f2a, sampleRate, cutoff, damping, vortex::F2_LP);
        return vortex::filter2_process(branch.f2a, signal, vortex::F2_LP);
    case LP24:
        vortex::filter2_configure(branch.f2a, sampleRate, cutoff, damping, vortex::F2_LP);
        vortex::filter2_configure(branch.f2b, sampleRate, cutoff, damping, vortex::F2_LP);
        return vortex::filter2_process(branch.f2b,
               vortex::filter2_process(branch.f2a, signal, vortex::F2_LP),
               vortex::F2_LP);
    case HP6:
        vortex::filter1_configure_hp(branch.f1, sampleRate, cutoff);
        return branch.f1.process_hp(signal);
    case HP12:
        vortex::filter2_configure(branch.f2a, sampleRate, cutoff, damping, vortex::F2_HP);
        return vortex::filter2_process(branch.f2a, signal, vortex::F2_HP);
    case HP24:
        vortex::filter2_configure(branch.f2a, sampleRate, cutoff, damping, vortex::F2_HP);
        vortex::filter2_configure(branch.f2b, sampleRate, cutoff, damping, vortex::F2_HP);
        return vortex::filter2_process(branch.f2b,
               vortex::filter2_process(branch.f2a, signal, vortex::F2_HP),
               vortex::F2_HP);
    case BP:
        vortex::filter2_configure(branch.f2a, sampleRate, cutoff, damping, vortex::F2_BP);
        return vortex::filter2_process(branch.f2a, signal, vortex::F2_BP);
    case BP_PLUS:
        vortex::filter2_configure(branch.f2a, sampleRate, cutoff, damping, vortex::F2_BP);
        vortex::filter2_configure(branch.f2b, sampleRate, cutoff, damping, vortex::F2_BP);
        return vortex::filter2_process(branch.f2b,
               vortex::filter2_process(branch.f2a, signal, vortex::F2_BP),
               vortex::F2_BP);
    case NOTCH:
        vortex::filter2_configure(branch.f2a, sampleRate, cutoff, damping, vortex::F2_NOTCH);
        return vortex::filter2_process(branch.f2a, signal, vortex::F2_NOTCH);
    case NOTCH_PLUS:
        vortex::filter2_configure(branch.f2a, sampleRate, cutoff, damping, vortex::F2_NOTCH);
        vortex::filter2_configure(branch.f2b, sampleRate, cutoff, damping, vortex::F2_NOTCH);
        return vortex::filter2_process(branch.f2b,
               vortex::filter2_process(branch.f2a, signal, vortex::F2_NOTCH),
               vortex::F2_NOTCH);
    case AP:
        vortex::filter2_configure(branch.f2a, sampleRate, cutoff, damping, vortex::F2_AP);
        return vortex::filter2_process(branch.f2a, signal, vortex::F2_AP);
    case AP_PLUS:
        vortex::filter2_configure(branch.f2a, sampleRate, cutoff, damping, vortex::F2_AP);
        vortex::filter2_configure(branch.f2b, sampleRate, cutoff, damping, vortex::F2_AP);
        return vortex::filter2_process(branch.f2b,
               vortex::filter2_process(branch.f2a, signal, vortex::F2_AP),
               vortex::F2_AP);
    default:
        return 0.f;
    }
}

} // namespace vortex_v2
