#ifndef WINTOID_FOUR_V2_DSP_H
#define WINTOID_FOUR_V2_DSP_H

// Pure DSP functions for the FourV2 PM synthesizer.
// No VCV Rack API dependencies — testable on desktop.

#include <math.h>
#include <stdint.h>

#include "model.h"

namespace four_v2 {

static constexpr float TWO_PI = 6.283185307179586f;

// Denormal protection: flush subnormals to zero.
inline void flush_denormal(float& x)
{
    if (fabsf(x) < 1e-10f)
        x = 0.0f;
}

// DC blocker: 1-pole highpass filter at ~20Hz.
struct DCBlocker
{
    float prevInput = 0.0f;
    float prevOutput = 0.0f;
    float R = 0.999f;

    float process(float input)
    {
        float output = input - prevInput + R * prevOutput;
        prevInput = input;
        prevOutput = output;
        flush_denormal(output);
        return output;
    }
};

// Compute sine from normalized phase [0, 1).
inline float oscillator_sine(float phase)
{
    return sinf(phase * TWO_PI);
}

// Advance phase by increment, wrapping to [0, 1).
inline void phase_advance(float& phase, float increment)
{
    phase += increment;
    phase -= floorf(phase);
}

// Frequency in ratio mode: base Hz * ratio * fine multiplier.
inline float calc_frequency_ratio(float baseHz, float ratio, float fineMult)
{
    return baseHz * ratio * fineMult;
}

// Frequency in fixed mode: fixed Hz * fine multiplier.
inline float calc_frequency_fixed(float fixedHz, float fineMult)
{
    return fixedHz * fineMult;
}

// V/OCT to frequency. 0V = C4 (261.63Hz), 1V/octave.
inline float voct_to_freq(float voltage)
{
    return 261.63f * exp2f(voltage);
}

// MIDI note to frequency. Note 69 = A4 = 440Hz.
inline float midi_note_to_freq(uint8_t note)
{
    return 440.0f * exp2f(((float)note - 69.0f) / 12.0f);
}

// Raw waveform generators from normalized phase [0, 1).
inline float waveform_triangle(float phase)
{
    if (phase < 0.25f)
        return phase * 4.0f;
    else if (phase < 0.75f)
        return 2.0f - phase * 4.0f;
    else
        return phase * 4.0f - 4.0f;
}

inline float waveform_saw(float phase)
{
    return 2.0f * phase - 1.0f;
}

inline float waveform_pulse(float phase)
{
    return phase < 0.5f ? 1.0f : -1.0f;
}

// Wave warp: morph sine -> triangle -> saw -> pulse.
inline float wave_warp(float phase, float warp)
{
    if (warp <= 0.0f)
        return oscillator_sine(phase);

    float sine = oscillator_sine(phase);

    if (warp <= 1.0f / 3.0f)
    {
        float t = warp * 3.0f;
        float tri = waveform_triangle(phase);
        return sine + t * (tri - sine);
    }
    else if (warp <= 2.0f / 3.0f)
    {
        float t = (warp - 1.0f / 3.0f) * 3.0f;
        float tri = waveform_triangle(phase);
        float saw = waveform_saw(phase);
        return tri + t * (saw - tri);
    }
    else
    {
        float t = (warp - 2.0f / 3.0f) * 3.0f;
        float saw = waveform_saw(phase);
        float pls = waveform_pulse(phase);
        return saw + t * (pls - saw);
    }
}

// Soft clipping function (tanh approximation, fast).
inline float soft_clip(float x)
{
    if (x < -3.0f)
        return -1.0f;
    if (x > 3.0f)
        return 1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Triangle-wave fold with period 4 and amplitude 1.
inline float triangle_fold(float x)
{
    float t = x + 1.0f;
    t = t - 4.0f * floorf(t * 0.25f);
    return (t < 2.0f) ? (t - 1.0f) : (3.0f - t);
}

// Symmetric fold: triangle fold that wraps signal back within [-1, 1].
inline float fold_symmetric(float x)
{
    return triangle_fold(x);
}

// Asymmetric fold: positive folds, negative clips.
inline float fold_asymmetric(float x)
{
    if (x >= 0.0f)
        return triangle_fold(x);
    else
        return soft_clip(x);
}

// Wave fold: drive input, then select symmetric, asymmetric, or soft fold.
// foldType is clamped to 0=symmetric, 1=asymmetric, 2=soft.
inline float wave_fold(float signal, float amount, int foldType)
{
    if (amount <= 0.0f)
        return signal;

    if (foldType < 0)
        foldType = 0;
    else if (foldType > 2)
        foldType = 2;

    float driven = signal * (1.0f + amount * 4.0f);
    switch (foldType)
    {
    case 0:
        return fold_symmetric(driven);
    case 1:
        return fold_asymmetric(driven);
    case 2:
        return soft_clip(driven);
    }

    return soft_clip(driven);
}

// Gather phase modulation for a target operator from all source operators.
inline float gather_modulation(
    int target,
    const float opOut[4],
    const float output[4],
    float pmDepth,
    const Algorithm& algorithm)
{
    float pm = 0.0f;
    for (int src = 0; src < 4; ++src)
    {
        if (algorithm.mod[src][target])
            pm += opOut[src] * output[src] * pmDepth;
    }
    return pm;
}

// Sum carrier outputs without normalizing for carrier count.
inline float sum_carriers(
    const float opOut[4],
    const float output[4],
    const Algorithm& algorithm)
{
    float mix = 0.0f;
    for (int op = 0; op < 4; ++op)
    {
        if (algorithm.carrier[op])
            mix += opOut[op] * output[op];
    }
    return mix;
}

// Calculate feedback contribution from the previous output.
inline float calc_feedback(float prevOutput, float amount)
{
    return soft_clip(prevOutput * amount);
}

// Simple 2x downsampler (half-band average).
inline float downsample_2x(float s0, float s1)
{
    return (s0 + s1) * 0.5f;
}

// PolyBLEP correction for discontinuities.
inline float polyblep(float phase, float phaseIncrement)
{
    if (phase < phaseIncrement)
    {
        float t = phase / phaseIncrement;
        return t + t - t * t - 1.0f;
    }
    else if (phase > 1.0f - phaseIncrement)
    {
        float t = (phase - 1.0f) / phaseIncrement;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

// PolyBLEP-corrected saw.
inline float waveform_saw_blep(float phase, float phaseIncrement)
{
    return waveform_saw(phase) - polyblep(phase, phaseIncrement);
}

// PolyBLEP-corrected pulse.
inline float waveform_pulse_blep(float phase, float phaseIncrement)
{
    float pulse = waveform_pulse(phase);
    pulse += polyblep(phase, phaseIncrement);
    float shifted = phase + 0.5f;
    if (shifted >= 1.0f)
        shifted -= 1.0f;
    pulse -= polyblep(shifted, phaseIncrement);
    return pulse;
}

// Wave warp with PolyBLEP correction for saw/pulse portions.
inline float wave_warp_blep(float phase, float warp, float phaseIncrement)
{
    if (warp <= 0.0f)
        return oscillator_sine(phase);

    float sine = oscillator_sine(phase);

    if (warp <= 1.0f / 3.0f)
    {
        float t = warp * 3.0f;
        float tri = waveform_triangle(phase);
        return sine + t * (tri - sine);
    }
    else if (warp <= 2.0f / 3.0f)
    {
        float t = (warp - 1.0f / 3.0f) * 3.0f;
        float tri = waveform_triangle(phase);
        float saw = waveform_saw_blep(phase, phaseIncrement);
        return tri + t * (saw - tri);
    }
    else
    {
        float t = (warp - 2.0f / 3.0f) * 3.0f;
        float saw = waveform_saw_blep(phase, phaseIncrement);
        float pulse = waveform_pulse_blep(phase, phaseIncrement);
        return saw + t * (pulse - saw);
    }
}

} // namespace four_v2

#endif // WINTOID_FOUR_V2_DSP_H
