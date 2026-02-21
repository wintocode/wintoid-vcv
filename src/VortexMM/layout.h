#pragma once
// All coordinates in mm (use mm2px() in VCV widget code).

namespace vortexmm_layout {

constexpr float PANEL_WIDTH  = 30.48f;
constexpr float PANEL_HEIGHT = 128.5f;

// Mode display (Y matches FourMM ALGO_DISPLAY_Y)
constexpr float MODE_DISPLAY_X = 15.24f;
constexpr float MODE_DISPLAY_Y = 16.0f;

// Centered column X for knobs and CV jacks
constexpr float CENTER_X = 15.24f;

// Attenuverter trimpots: 8.5mm right of jack (matches FourMM CV jack→atten gap)
constexpr float ATTEN_X = 23.75f;

// --- Cutoff group (knob → CV jack + trimpot) ---
constexpr float CUTOFF_KNOB_X       = CENTER_X;
constexpr float CUTOFF_KNOB_Y       = 32.0f;
constexpr float CV_CUTOFF_JACK_X    = CENTER_X;
constexpr float CV_CUTOFF_JACK_Y    = 44.0f;
constexpr float CV_CUTOFF_ATTEN_X   = ATTEN_X;
constexpr float CV_CUTOFF_ATTEN_Y   = 44.0f;

// --- Resonance group ---
constexpr float RESONANCE_KNOB_X       = CENTER_X;
constexpr float RESONANCE_KNOB_Y       = 60.0f;
constexpr float CV_RESONANCE_JACK_X    = CENTER_X;
constexpr float CV_RESONANCE_JACK_Y    = 72.0f;
constexpr float CV_RESONANCE_ATTEN_X   = ATTEN_X;
constexpr float CV_RESONANCE_ATTEN_Y   = 72.0f;

// --- Drive group ---
constexpr float DRIVE_KNOB_X       = CENTER_X;
constexpr float DRIVE_KNOB_Y       = 88.0f;
constexpr float CV_DRIVE_JACK_X    = CENTER_X;
constexpr float CV_DRIVE_JACK_Y    = 100.0f;
constexpr float CV_DRIVE_ATTEN_X   = ATTEN_X;
constexpr float CV_DRIVE_ATTEN_Y   = 100.0f;

// Audio I/O (same height as FB row on FourMM)
constexpr float AUDIO_IN_X  = 9.0f;
constexpr float AUDIO_IN_Y  = 115.0f;
constexpr float AUDIO_OUT_X = 21.5f;
constexpr float AUDIO_OUT_Y = 115.0f;

} // namespace vortexmm_layout
