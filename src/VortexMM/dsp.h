#pragma once

#include <cmath>
#include <cstdint>

namespace vortex {

// --- Constants ---
// (Replacing C++20 std::numbers with C++11 constexpr)

static const float PI       = 3.14159265358979323846f;
static const float TWO_PI   = 6.28318530717958647692f;
static const float SQRT2    = 1.41421356237309504880f;
static const float INV_PI   = 0.31830988618379067154f;
static const float INV_SQRT2 = 0.70710678118654752440f;

// --- Utility functions ---

// Flush denormals to zero (prevents FPU slowdown on ARM)
inline float flush_denormal(float x)
{
    union { float f; uint32_t i; } u;
    u.f = x;
    if ((u.i & 0x7F800000) == 0 && (u.i & 0x007FFFFF) != 0)
        u.f = 0.0f;
    return u.f;
}

// Soft-clip saturation: x*(27+x^2)/(27+9x^2)
// Smooth saturator approaching +/-1/3 at extremes
inline float soft_clip(float x)
{
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// MIDI note to frequency (note 69 = A4 = 440 Hz)
inline float midi_note_to_freq(float note)
{
    return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
}

// V/OCT to frequency (0V = C4 = 261.63 Hz)
inline float voct_to_freq(float voltage)
{
    return 261.6255653f * powf(2.0f, voltage);
}

// V/OCT to frequency multiplier (0V = 1x, 1V = 2x)
inline float voct_to_mult(float voltage)
{
    return powf(2.0f, voltage);
}

// Cutoff parameter (0-1000) to Hz (20-20000, exponential)
// freq = 20 * 1000^(param/1000)
inline float cutoff_param_to_hz(int param)
{
    return 20.0f * powf(1000.0f, (float)param / 1000.0f);
}

// Resonance parameter (0-1000) to damping factor
// 0 = Butterworth (damping=0.707), 1000 = near self-oscillation (damping=0.01)
inline float resonance_to_damping(int param)
{
    float t = (float)param / 1000.0f;
    return 0.707f * (1.0f - t) + 0.01f * t;
}

// ============================================================
// First-order state-space filter (6 dB/oct)
// Ported from ivantsov-filters by Yuriy Ivantsov (C++20 -> C++11)
// Reference: https://github.com/yIvantsov/ivantsov-filters
// ============================================================

struct Filter1
{
    float z;        // state variable
    float b0, b1;   // coefficients

    Filter1() : z(0.0f), b0(0.0f), b1(0.0f) {}

    void reset() { z = 0.0f; }

    // Low-pass output: theta*b1 + z
    float process_lp(float x)
    {
        float theta = (x - z) * b0;
        float y = theta * b1 + z;
        z += theta;
        return y;
    }

    // High-pass output: theta*b1
    float process_hp(float x)
    {
        float theta = (x - z) * b0;
        float y = theta * b1;
        z += theta;
        return y;
    }
};

// Configure first-order low-pass coefficients
// Uses Sigma frequency warping for audio-rate modulation quality
inline void filter1_configure_lp(Filter1& f, float sample_rate, float cutoff_hz)
{
    float w = sample_rate / (2.0f * PI * cutoff_hz);
    float sigma = INV_PI;
    if (w > INV_PI)
        sigma = 0.40824999f * (0.05843357f - w * w) / (0.04593294f - w * w);
    float v = sqrtf(w * w + sigma * sigma);
    f.b0 = 1.0f / (0.5f + v);
    f.b1 = 0.5f + sigma;
}

// Configure first-order high-pass coefficients
inline void filter1_configure_hp(Filter1& f, float sample_rate, float cutoff_hz)
{
    float w = sample_rate / (2.0f * PI * cutoff_hz);
    float sigma = INV_PI;
    if (w > INV_PI)
        sigma = 0.40824999f * (0.05843357f - w * w) / (0.04593294f - w * w);
    float v = sqrtf(w * w + sigma * sigma);
    f.b0 = 1.0f / (0.5f + v);
    f.b1 = w;
}

// ============================================================
// Second-order state-space filter (12 dB/oct)
// Ported from ivantsov-filters by Yuriy Ivantsov (C++20 -> C++11)
// Reference: https://github.com/yIvantsov/ivantsov-filters
// ============================================================

enum Filter2Type
{
    F2_LP = 0,   // Low-pass
    F2_HP,       // High-pass
    F2_BP,       // Band-pass
    F2_NOTCH,    // Notch (band reject)
    F2_AP        // All-pass
};

struct Filter2
{
    float z0, z1;           // state variables
    float b0, b1, b2, b3;   // coefficients

    Filter2() : z0(0.0f), z1(0.0f), b0(0.0f), b1(0.0f), b2(0.0f), b3(0.0f) {}

    void reset() { z0 = z1 = 0.0f; }

    // Process for LP, Notch, AllPass (output includes z0 term)
    float process_lna(float x)
    {
        float theta = (x - z0 - z1 * b1) * b0;
        float y = theta * b3 + z1 * b2 + z0;
        z0 += theta;
        z1 = -z1 - theta * b1;
        return y;
    }

    // Process for HP, BP (output excludes z0 term)
    float process_hb(float x)
    {
        float theta = (x - z0 - z1 * b1) * b0;
        float y = theta * b3 + z1 * b2;
        z0 += theta;
        z1 = -z1 - theta * b1;
        return y;
    }
};

// Configure second-order filter coefficients
// Uses Sigma frequency warping for audio-rate modulation quality
// damping = 1/(2*Q), e.g. 0.707 = Butterworth, lower = more resonant
inline void filter2_configure(Filter2& f, float sample_rate, float cutoff_hz,
                               float damping, Filter2Type type)
{
    float w = sample_rate / (SQRT2 * PI * cutoff_hz);
    float sigma = SQRT2 * INV_PI;
    if (w > INV_PI * SQRT2)
        sigma = 0.57735268f * (0.11686715f - w * w) / (0.09186588f - w * w);

    float w_sq = w * w;
    float sigma_sq = sigma * sigma;
    float zeta_sq = damping * damping;

    // vk computation (state-space eigenvalue decomposition)
    float t = w_sq * (2.0f * zeta_sq - 1.0f);
    float v = sqrtf(w_sq * w_sq + sigma_sq * (2.0f * t + sigma_sq));
    float k = t + sigma_sq;

    f.b0 = 1.0f / (v + sqrtf(v + k) + 0.5f);
    f.b1 = sqrtf(2.0f * v);

    switch (type)
    {
    case F2_LP:
        f.b2 = 2.0f * sigma_sq / f.b1;
        f.b3 = 0.5f + sigma_sq + SQRT2 * sigma;
        break;
    case F2_HP:
        f.b2 = 2.0f * w_sq / f.b1;
        f.b3 = w_sq;
        break;
    case F2_BP:
        f.b2 = 4.0f * w * damping * sigma / f.b1;
        f.b3 = 2.0f * w * damping * (sigma + INV_SQRT2);
        break;
    case F2_NOTCH:
        f.b2 = 2.0f * (w_sq - sigma_sq) / f.b1;
        f.b3 = 0.5f + w_sq - sigma_sq;
        break;
    case F2_AP:
        f.b2 = f.b1;
        f.b3 = 0.5f + v - sqrtf(v + k);
        break;
    }
}

// Process one sample through a second-order filter
inline float filter2_process(Filter2& f, float x, Filter2Type type)
{
    if (type == F2_HP || type == F2_BP)
        return f.process_hb(x);
    else
        return f.process_lna(x);
}

} // namespace vortex
