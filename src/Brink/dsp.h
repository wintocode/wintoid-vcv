#pragma once

#include "../polyphony.h"
#include <atomic>
#include <cmath>
#include <cstdint>

namespace brink {

static const float MIN_WIDTH = 0.001f;
static const float MAX_WIDTH = 20.f;
static const float HYSTERESIS = 0.001f;
static const float DISPLAY_MIN_VOLTAGE = -10.f;
static const float DISPLAY_MAX_VOLTAGE = 10.f;
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

struct WindowDisplayFrame {
    float signal;
    float center;
    float lower;
    float upper;
};

struct AtomicDisplayFrame {
    static constexpr std::memory_order WRITER_BEGIN_ORDER =
        std::memory_order_acq_rel;
    static constexpr std::memory_order WRITER_PAYLOAD_FENCE_ORDER =
        std::memory_order_release;
    static constexpr std::memory_order WRITER_END_ORDER =
        std::memory_order_release;
    static constexpr std::memory_order READER_BEGIN_ORDER =
        std::memory_order_acquire;
    static constexpr std::memory_order READER_VALIDATE_FENCE_ORDER =
        std::memory_order_acquire;
    static constexpr std::memory_order READER_END_ORDER =
        std::memory_order_relaxed;

    std::atomic<std::uint32_t> sequence;
    std::atomic<float> signal;
    std::atomic<float> center;
    std::atomic<float> lower;
    std::atomic<float> upper;

    AtomicDisplayFrame()
        : sequence(0), signal(0.f), center(0.f), lower(-2.5f), upper(2.5f) {}

    void store(const WindowFrame& frame) noexcept
    {
        sequence.fetch_add(1, WRITER_BEGIN_ORDER);
        // Pairs with the reader's validation fence: a reader that sees any
        // payload store below must also see the odd marker above.
        std::atomic_thread_fence(WRITER_PAYLOAD_FENCE_ORDER);
        signal.store(frame.signal, std::memory_order_relaxed);
        center.store(frame.center, std::memory_order_relaxed);
        lower.store(frame.lower, std::memory_order_relaxed);
        upper.store(frame.upper, std::memory_order_relaxed);
        sequence.fetch_add(1, WRITER_END_ORDER);
    }

    WindowDisplayFrame load() const noexcept
    {
        for (;;) {
            const std::uint32_t before = sequence.load(READER_BEGIN_ORDER);
            if (before & 1u) continue;

            WindowDisplayFrame frame;
            frame.signal = signal.load(std::memory_order_relaxed);
            frame.center = center.load(std::memory_order_relaxed);
            frame.lower = lower.load(std::memory_order_relaxed);
            frame.upper = upper.load(std::memory_order_relaxed);

            std::atomic_thread_fence(READER_VALIDATE_FENCE_ORDER);
            const std::uint32_t after = sequence.load(READER_END_ORDER);
            if (before == after) return frame;
        }
    }
};

constexpr float DISPLAY_UPDATE_HZ = 60.f;

struct DisplayRateLimiter {
    int sampleInterval;
    int samplesUntilUpdate;

    DisplayRateLimiter() : sampleInterval(1), samplesUntilUpdate(0) {}

    void reset(float sampleRate) noexcept
    {
        if (!std::isfinite(sampleRate) || sampleRate <= 0.f) {
            sampleInterval = 1;
        }
        else {
            const float roundedInterval = sampleRate / DISPLAY_UPDATE_HZ + 0.5f;
            sampleInterval = roundedInterval < 1.f
                ? 1
                : static_cast<int>(roundedInterval);
        }
        samplesUntilUpdate = 0;
    }

    bool should_publish() noexcept
    {
        if (samplesUntilUpdate <= 1) {
            samplesUntilUpdate = sampleInterval;
            return true;
        }
        --samplesUntilUpdate;
        return false;
    }
};

inline float sanitize(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

inline float clampf(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

inline float normalize_display_voltage(float voltage)
{
    const float safeVoltage = clampf(
        sanitize(voltage, 0.f), DISPLAY_MIN_VOLTAGE, DISPLAY_MAX_VOLTAGE);
    return (safeVoltage - DISPLAY_MIN_VOLTAGE)
        / (DISPLAY_MAX_VOLTAGE - DISPLAY_MIN_VOLTAGE);
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

inline WindowDisplayFrame make_display_frame(float signal,
                                             float center,
                                             float rawWidth)
{
    const WindowFrame frame = make_window(signal, center, rawWidth);
    WindowDisplayFrame display;
    display.signal = frame.signal;
    display.center = frame.center;
    display.lower = frame.lower;
    display.upper = frame.upper;
    return display;
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
    WindowFrame frame;
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
    output.frame = frame;
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
