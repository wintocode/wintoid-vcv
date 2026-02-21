#ifndef FOURMM_ENGINE_H
#define FOURMM_ENGINE_H

#include "dsp.h"
#include <algorithm>

namespace fourmm {

struct OperatorState
{
    float phase = 0.f;
    float prevOutput = 0.f;
};

struct EngineState
{
    OperatorState ops[4];
    DCBlocker dcBlocker;
};

struct EngineParams
{
    int algorithm = 0;          // 0-10
    float modMaster = 0.f;     // 0.0-1.0 global modulation depth
    float extPmDepth = 0.f;    // 0.0-1.0 external PM depth
    float globalVCA = 1.f;     // 0.0-1.0

    float baseFreq = 261.63f;  // Hz, from V/OCT + global fine tune

    // Per-operator (indexed 0-3 for ops 1-4)
    float opCoarse[4] = { 1.f, 1.f, 1.f, 1.f };     // ratio value or Hz
    float opFine[4] = { 1.f, 1.f, 1.f, 1.f };        // multiplier (from cents)
    float opLevel[4] = { 1.f, 1.f, 1.f, 1.f };       // 0.0-1.0
    float opWarp[4] = {};       // 0.0-1.0
    float opFold[4] = {};       // 0.0-1.0
    float opFeedback[4] = {};   // 0.0-1.0
    int opFreqMode[4] = {};     // 0=ratio, 1=fixed
    int opFoldType[4] = {};     // 0=sym, 1=asym, 2=soft
};

// Process one sample. Internally runs 2x oversampled.
// sampleTime: 1.0 / sampleRate (the VCV sample period, NOT oversampled)
// extPm: external phase modulation amount (audio rate, typically +/- 5V)
// Returns output sample in range roughly [-1, 1] before VCA.
inline float engine_process( EngineState& state, const EngineParams& params, float sampleTime, float extPm = 0.f )
{
    const float osTime = sampleTime * 0.5f;
    const Algorithm& algo = algorithms[params.algorithm];
    float result[2];

    for ( int pass = 0; pass < 2; pass++ )
    {
        float opOut[4] = {};

        // Compute operators in fixed order: 4, 3, 2, 1 (index 3, 2, 1, 0)
        for ( int op = 3; op >= 0; op-- )
        {
            // Compute operator frequency
            float freq;
            if ( params.opFreqMode[op] == 0 )
                freq = calc_frequency_ratio( params.baseFreq, params.opCoarse[op], params.opFine[op] );
            else
                freq = calc_frequency_fixed( params.opCoarse[op], params.opFine[op] );

            float inc = freq * osTime;

            // Advance phase (clean, without modulation)
            phase_advance( state.ops[op].phase, inc );

            // Gather phase modulation from higher operators
            float pm = gather_modulation( op, opOut, params.opLevel, params.modMaster, algo );

            // Add self-feedback
            pm += calc_feedback( state.ops[op].prevOutput, params.opFeedback[op] );

            // Compute modulated phase for waveform generation
            float modulatedPhase = state.ops[op].phase + pm;
            modulatedPhase -= floorf( modulatedPhase );
            if ( modulatedPhase < 0.f ) modulatedPhase += 1.f;

            // Generate waveform with PolyBLEP
            float out = wave_warp_blep( modulatedPhase, params.opWarp[op], inc );

            // Apply wave fold
            out = wave_fold( out, params.opFold[op], params.opFoldType[op] );

            opOut[op] = out;
            state.ops[op].prevOutput = out;
        }

        result[pass] = sum_carriers( opOut, params.opLevel, algo );
    }

    float out = downsample_2x( result[0], result[1] );
    out = state.dcBlocker.process( out );

    // External PM: treat synth output as a sine wave, apply phase modulation
    // When extPmDepth = 0, output is unchanged (identity mapping)
    // Interpret output as sin(phase), apply PM as sin(phase + modulation)
    if ( params.extPmDepth > 0.f && fabsf( extPm ) > 1e-6f )
    {
        // Map output [-1, 1] back to phase [-π/2, π/2], then normalize to [0, 1)
        float carrierPhase = (asinf( std::max( -1.f, std::min( 1.f, out ) ) ) / TWO_PI) + 0.25f;
        float pmAmount = extPm * params.extPmDepth;  // Scale by depth
        out = oscillator_sine( carrierPhase + pmAmount );
    }

    out *= params.globalVCA;
    return out;
}

} // namespace fourmm

#endif // FOURMM_ENGINE_H
