#pragma once

#include <cmath>

namespace brink {

static const float MIN_WIDTH = 0.001f;
static const float MAX_WIDTH = 20.f;
static const float HYSTERESIS = 0.001f;
static const int MAX_CHANNELS = 16;

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

} // namespace brink
