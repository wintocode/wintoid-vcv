#ifndef FOURMM_DSP_H
#define FOURMM_DSP_H

// Pure DSP functions for FourMM FM synthesizer.
// No VCV Rack API dependencies — testable on desktop.

#include <math.h>
#include <stdint.h>

namespace fourmm {

static constexpr float TWO_PI = 6.283185307179586f;

// Denormal protection: flush subnormals to zero
inline void flush_denormal( float& x )
{
    if ( fabsf( x ) < 1e-10f )
        x = 0.0f;
}

// DC blocker: 1-pole highpass filter at ~20Hz
// state: previous input sample, returns output
struct DCBlocker
{
    float prevInput = 0.0f;
    float prevOutput = 0.0f;
    float R = 0.999f;  // Pole for ~20Hz at 48kHz

    float process( float input )
    {
        float output = input - prevInput + R * prevOutput;
        prevInput = input;
        prevOutput = output;
        flush_denormal( output );
        return output;
    }
};

// Simple 2× downsampler (half-band average)

// Compute sine from normalized phase [0, 1)
inline float oscillator_sine( float phase )
{
    return sinf( phase * TWO_PI );
}

// Advance phase by increment, wrap to [0, 1)
inline void phase_advance( float& phase, float increment )
{
    phase += increment;
    phase -= floorf( phase );
}

// Frequency in ratio mode: base_hz * coarse_ratio * fine_multiplier
inline float calc_frequency_ratio( float base_hz, float coarse, float fine_mult )
{
    return base_hz * coarse * fine_mult;
}

// Frequency in fixed mode: coarse_hz * fine_multiplier
inline float calc_frequency_fixed( float coarse_hz, float fine_mult )
{
    return coarse_hz * fine_mult;
}

// V/OCT to frequency. 0V = C4 (261.63Hz), 1V/octave.
inline float voct_to_freq( float voltage )
{
    return 261.63f * exp2f( voltage );
}

// MIDI note to frequency. Note 69 = A4 = 440Hz.
inline float midi_note_to_freq( uint8_t note )
{
    return 440.0f * exp2f( ( (float)note - 69.0f ) / 12.0f );
}

// Raw waveform generators from normalized phase [0, 1)
inline float waveform_triangle( float phase )
{
    if ( phase < 0.25f )
        return phase * 4.0f;
    else if ( phase < 0.75f )
        return 2.0f - phase * 4.0f;
    else
        return phase * 4.0f - 4.0f;
}

inline float waveform_saw( float phase )
{
    return 2.0f * phase - 1.0f;
}

inline float waveform_pulse( float phase )
{
    return phase < 0.5f ? 1.0f : -1.0f;
}

// Wave warp: morph sine → triangle → saw → pulse
// phase: normalized [0, 1), warp: 0.0-1.0
inline float wave_warp( float phase, float warp )
{
    if ( warp <= 0.0f )
        return oscillator_sine( phase );

    float sine = oscillator_sine( phase );

    if ( warp <= 1.0f / 3.0f )
    {
        float t = warp * 3.0f;
        float tri = waveform_triangle( phase );
        return sine + t * ( tri - sine );
    }
    else if ( warp <= 2.0f / 3.0f )
    {
        float t = ( warp - 1.0f / 3.0f ) * 3.0f;
        float tri = waveform_triangle( phase );
        float saw = waveform_saw( phase );
        return tri + t * ( saw - tri );
    }
    else
    {
        float t = ( warp - 2.0f / 3.0f ) * 3.0f;
        float saw = waveform_saw( phase );
        float pls = waveform_pulse( phase );
        return saw + t * ( pls - saw );
    }
}

// Soft clipping function (tanh approximation, fast)
inline float soft_clip( float x )
{
    if ( x < -3.0f ) return -1.0f;
    if ( x >  3.0f ) return  1.0f;
    float x2 = x * x;
    return x * ( 27.0f + x2 ) / ( 27.0f + 9.0f * x2 );
}

// Triangle-wave fold: wraps signal smoothly into [-1, 1] with no discontinuities.
// Maps x into a triangle wave of period 4 and amplitude 1.
inline float triangle_fold( float x )
{
    // Shift so that x=0 maps to the rising zero-crossing
    float t = x + 1.0f;
    // Mod into [0, 4) range
    t = t - 4.0f * floorf( t * 0.25f );
    // Triangle: rise 0→2, then fall 2→4
    return ( t < 2.0f ) ? ( t - 1.0f ) : ( 3.0f - t );
}

// Symmetric fold: triangle fold that wraps signal back within [-1, 1]
inline float fold_symmetric( float x )
{
    return triangle_fold( x );
}

// Asymmetric fold: positive folds, negative clips
inline float fold_asymmetric( float x )
{
    if ( x >= 0.0f )
        return triangle_fold( x );
    else
        return soft_clip( x );
}

// Wave fold: applies drive based on fold amount, then folds
// input: signal [-1, 1], amount: 0.0-1.0, type: 0=sym, 1=asym, 2=soft
inline float wave_fold( float input, float amount, int type )
{
    if ( amount <= 0.0f )
        return input;

    // Drive: scale input by 1 + amount * 4 (up to 5× drive at max)
    float driven = input * ( 1.0f + amount * 4.0f );

    switch ( type )
    {
    case 0:  return fold_symmetric( driven );
    case 1:  return fold_asymmetric( driven );
    case 2:  return soft_clip( driven );
    default: return soft_clip( driven );
    }
}

struct Algorithm
{
    bool mod[4][4];     // mod[src][dst]: src modulates dst
    bool carrier[4];    // carrier[op]: outputs to mix
};

// 11 FM algorithms (0-indexed)
static const Algorithm algorithms[11] = {
    // Algo 1: 4→3→2→1, carriers: {1}
    { { {0,0,0,0}, {1,0,0,0}, {0,1,0,0}, {0,0,1,0} },
      {true, false, false, false} },

    // Algo 2: (3+4)→2→1, carriers: {1}
    { { {0,0,0,0}, {1,0,0,0}, {0,1,0,0}, {0,1,0,0} },
      {true, false, false, false} },

    // Algo 3: (4→2→1) + (3→1), carriers: {1}
    { { {0,0,0,0}, {1,0,0,0}, {1,0,0,0}, {0,1,0,0} },
      {true, false, false, false} },

    // Algo 4: (4→3→1) + (2→1), carriers: {1}
    { { {0,0,0,0}, {1,0,0,0}, {1,0,0,0}, {0,0,1,0} },
      {true, false, false, false} },

    // Algo 5: (4→3) + (2→1), carriers: {1, 3}
    { { {0,0,0,0}, {1,0,0,0}, {0,0,0,0}, {0,0,1,0} },
      {true, false, true, false} },

    // Algo 6: 4→(1,2,3), carriers: {1, 2, 3}
    { { {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {1,1,1,0} },
      {true, true, true, false} },

    // Algo 7: (4→3) + 2 + 1, carriers: {1, 2, 3}
    { { {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,1,0} },
      {true, true, true, false} },

    // Algo 8: 1+2+3+4, carriers: all
    { { {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} },
      {true, true, true, true} },

    // Algo 9: 4→3→(1,2), carriers: {1, 2}
    { { {0,0,0,0}, {0,0,0,0}, {1,1,0,0}, {0,0,1,0} },
      {true, true, false, false} },

    // Algo 10: (3+4)→(1,2), carriers: {1, 2}
    { { {0,0,0,0}, {0,0,0,0}, {1,1,0,0}, {1,1,0,0} },
      {true, true, false, false} },

    // Algo 11: (2+3+4)→1, carriers: {1}
    { { {0,0,0,0}, {1,0,0,0}, {1,0,0,0}, {1,0,0,0} },
      {true, false, false, false} },
};

// Gather phase modulation for a target operator from all sources
inline float gather_modulation(
    int target,
    const float opOut[4],
    const float level[4],
    float modMaster,
    const Algorithm& algo )
{
    float pm = 0.0f;
    for ( int src = 0; src < 4; ++src )
    {
        if ( algo.mod[src][target] )
            pm += opOut[src] * level[src] * modMaster;
    }
    return pm;
}

// Sum carrier outputs
inline float sum_carriers(
    const float opOut[4],
    const float level[4],
    const Algorithm& algo )
{
    float mix = 0.0f;
    for ( int op = 0; op < 4; ++op )
    {
        if ( algo.carrier[op] )
            mix += opOut[op] * level[op];
    }
    return mix;
}

// Calculate feedback contribution from previous output
// prev_output: previous sample output, amount: 0.0-1.0
// Returns phase modulation amount (bounded)
inline float calc_feedback( float prev_output, float amount )
{
    return soft_clip( prev_output * amount );
}

// Simple 2× downsampler (half-band average)
// s0: first sample (even), s1: second sample (odd)
inline float downsample_2x( float s0, float s1 )
{
    return ( s0 + s1 ) * 0.5f;
}

// PolyBLEP correction for discontinuities
// phase: normalized [0, 1), dt: phase increment per sample
// Returns correction to subtract from waveform at discontinuity points
inline float polyblep( float phase, float dt )
{
    // Near phase = 0 (beginning of cycle)
    if ( phase < dt )
    {
        float t = phase / dt;
        return t + t - t * t - 1.0f;
    }
    // Near phase = 1 (end of cycle)
    else if ( phase > 1.0f - dt )
    {
        float t = ( phase - 1.0f ) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

// PolyBLEP-corrected saw
inline float waveform_saw_blep( float phase, float dt )
{
    return waveform_saw( phase ) - polyblep( phase, dt );
}

// PolyBLEP-corrected pulse
inline float waveform_pulse_blep( float phase, float dt )
{
    float p = waveform_pulse( phase );
    p += polyblep( phase, dt );               // Rising edge at 0
    float shifted = phase + 0.5f;
    if ( shifted >= 1.0f ) shifted -= 1.0f;
    p -= polyblep( shifted, dt );             // Falling edge at 0.5
    return p;
}

// Wave warp with optional PolyBLEP (for anti-aliasing saw/pulse)
inline float wave_warp_blep( float phase, float warp, float dt )
{
    if ( warp <= 0.0f )
        return oscillator_sine( phase );

    float sine = oscillator_sine( phase );

    if ( warp <= 1.0f / 3.0f )
    {
        float t = warp * 3.0f;
        float tri = waveform_triangle( phase );
        return sine + t * ( tri - sine );
    }
    else if ( warp <= 2.0f / 3.0f )
    {
        float t = ( warp - 1.0f / 3.0f ) * 3.0f;
        float tri = waveform_triangle( phase );
        float saw = waveform_saw_blep( phase, dt );
        return tri + t * ( saw - tri );
    }
    else
    {
        float t = ( warp - 2.0f / 3.0f ) * 3.0f;
        float saw = waveform_saw_blep( phase, dt );
        float pls = waveform_pulse_blep( phase, dt );
        return saw + t * ( pls - saw );
    }
}

// Coarse ratio from knob index (0-64).
// 0=0.25, 1=0.5, 2=0.75, then 1.0-32.0 in 0.5 steps (matching Four).
inline float coarse_ratio_from_index( int idx )
{
    if ( idx == 0 ) return 0.25f;
    if ( idx == 1 ) return 0.5f;
    if ( idx == 2 ) return 0.75f;
    return (float)( idx - 1 ) * 0.5f;
}

// Coarse fixed Hz from continuous param value (0.0-64.0).
// Maps exponentially: 0→1 Hz, 64→9999 Hz.
inline float coarse_fixed_from_param( float param )
{
    return expf( param / 64.0f * logf( 9999.0f ) );
}

// Algorithm display strings (matching Four)
static const char* algorithmStrings[11] = {
    "4 => 3 => 2 => 1",
    "(3+4) => 2 => 1",
    "4 => 2 => 1, 3 => 1",
    "4 => 3 => 1, 2 => 1",
    "4 => 3, 2 => 1",
    "4 => (1, 2, 3)",
    "4 => 3, 2, 1",
    "1, 2, 3, 4",
    "4 => 3 => (1, 2)",
    "(3+4) => (1, 2)",
    "(2+3+4) => 1",
};

} // namespace fourmm

#endif // FOURMM_DSP_H
