#pragma once

#include "../polyphony.h"
#include <cmath>

namespace brink {

static const float MIN_WIDTH = 0.001f;
static const float MAX_WIDTH = 20.f;
static const float HYSTERESIS = 0.001f;
static const int MAX_CHANNELS = wintoid::polyphony::MAX_CHANNELS;

enum Region { BELOW = 0, INSIDE, ABOVE };

struct WindowFrame {
    float signal;
    float center;
    float width;
    float lower;
    float upper;
    float relative;
    float position;
};

inline float sanitize(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

inline float clampf(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

inline WindowFrame make_window(float signal, float center, float rawWidth)
{
    WindowFrame f;
    f.signal = sanitize(signal, 0.f);
    f.center = sanitize(center, 0.f);
    f.width = clampf(sanitize(rawWidth, MIN_WIDTH), MIN_WIDTH, MAX_WIDTH);
    f.lower = f.center - 0.5f * f.width;
    f.upper = f.center + 0.5f * f.width;
    f.relative = 10.f * (f.signal - f.center) / f.width;
    f.position = clampf(f.relative, -5.f, 5.f);
    return f;
}

inline Region classify_initial(const WindowFrame& f)
{
    if (f.signal < f.lower) return BELOW;
    if (f.signal > f.upper) return ABOVE;
    return INSIDE;
}

inline Region advance_region(Region previous, const WindowFrame& f)
{
    if (previous == BELOW) {
        if (f.signal >= f.upper + HYSTERESIS) return ABOVE;
        if (f.signal >= f.lower + HYSTERESIS) return INSIDE;
        return BELOW;
    }
    if (previous == INSIDE) {
        if (f.signal < f.lower - HYSTERESIS) return BELOW;
        if (f.signal > f.upper + HYSTERESIS) return ABOVE;
        return INSIDE;
    }
    if (f.signal <= f.lower - HYSTERESIS) return BELOW;
    if (f.signal <= f.upper - HYSTERESIS) return INSIDE;
    return ABOVE;
}

enum EventId { LOW_UP = 0, HIGH_UP, LOW_DOWN, HIGH_DOWN, EVENT_COUNT };

struct WindowState {
    bool initialized;
    Region region;
    float pulseRemaining[EVENT_COUNT];

    WindowState() : initialized(false), region(INSIDE)
    {
        for (int i = 0; i < EVENT_COUNT; ++i) pulseRemaining[i] = 0.f;
    }
};

struct WindowOutput {
    Region region;
    bool inside;
    float position;
    bool eventHigh[EVENT_COUNT];
};

inline void reset(WindowState& state)
{
    state = WindowState();
}

inline void start_pulse(WindowState& state, EventId event)
{
    state.pulseRemaining[event] = 0.001f;
}

inline void start_transition_pulses(WindowState& state, Region previous, Region current)
{
    if (previous == BELOW) {
        if (current == INSIDE) {
            start_pulse(state, LOW_UP);
        } else if (current == ABOVE) {
            start_pulse(state, LOW_UP);
            start_pulse(state, HIGH_UP);
        }
    } else if (previous == INSIDE) {
        if (current == BELOW) {
            start_pulse(state, LOW_DOWN);
        } else if (current == ABOVE) {
            start_pulse(state, HIGH_UP);
        }
    } else if (previous == ABOVE) {
        if (current == INSIDE) {
            start_pulse(state, HIGH_DOWN);
        } else if (current == BELOW) {
            start_pulse(state, HIGH_DOWN);
            start_pulse(state, LOW_DOWN);
        }
    }
}

inline WindowOutput process_window(WindowState& state,
                                   float signal,
                                   float center,
                                   float width,
                                   float sampleTime)
{
    const float dt = std::isfinite(sampleTime) && sampleTime >= 0.f ? sampleTime : 0.f;
    const WindowFrame frame = make_window(signal, center, width);

    if (!state.initialized) {
        state.region = classify_initial(frame);
        state.initialized = true;
    } else {
        const Region previous = state.region;
        state.region = advance_region(previous, frame);
        start_transition_pulses(state, previous, state.region);
    }

    WindowOutput output;
    output.region = state.region;
    output.inside = state.region == INSIDE;
    output.position = frame.position;
    for (int i = 0; i < EVENT_COUNT; ++i) {
        output.eventHigh[i] = state.pulseRemaining[i] > 0.f;
        state.pulseRemaining[i] -= dt;
        if (state.pulseRemaining[i] < 0.f) state.pulseRemaining[i] = 0.f;
    }
    return output;
}

struct LogicState {
    bool initialized;
    bool previousXor;
    bool toggle;
    LogicState() : initialized(false), previousXor(false), toggle(false) {}
};

struct LogicOutput {
    bool andGate;
    bool orGate;
    bool xorGate;
    bool stateGate;
};

inline void reset(LogicState& state)
{
    state = LogicState();
}

inline LogicOutput process_logic(LogicState& state, bool a, bool b)
{
    LogicOutput output;
    output.andGate = a && b;
    output.orGate = a || b;
    output.xorGate = a != b;

    if (!state.initialized) {
        state.initialized = true;
        state.previousXor = output.xorGate;
    } else {
        if (!state.previousXor && output.xorGate) state.toggle = !state.toggle;
        state.previousXor = output.xorGate;
    }
    output.stateGate = state.toggle;
    return output;
}

inline int effective_channels(int channels)
{
    return wintoid::polyphony::effective_channels(channels);
}

inline int logic_channels(int aChannels, int bChannels)
{
    int a = effective_channels(aChannels);
    int b = effective_channels(bChannels);
    return a > b ? a : b;
}

inline int broadcast_lane(int lane, int channels)
{
    return wintoid::polyphony::broadcast_lane(lane, channels);
}

} // namespace brink
