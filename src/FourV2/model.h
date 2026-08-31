#ifndef WINTOID_FOUR_V2_MODEL_H
#define WINTOID_FOUR_V2_MODEL_H

#include <math.h>
#include <stdio.h>
#include <string>

namespace four_v2 {

static const int OPERATOR_COUNT = 4;
static const int ALGORITHM_COUNT = 11;
static const int RATIO_COUNT = 15;
static const int DEFAULT_RATIO_INDEX = 5;
static const float COARSE_MIN = 0.f;
static const float COARSE_MAX = 14.f;

enum FrequencyMode {
    RATIO_MODE = 0,
    FIXED_MODE = 1
};

struct Algorithm {
    bool mod[4][4];
    bool carrier[4];
};

struct Ratio {
    int numerator;
    int denominator;
    const char* label;
};

static const Ratio RATIOS[RATIO_COUNT] = {
    {4,1,"4:1"}, {3,1,"3:1"}, {2,1,"2:1"}, {3,2,"3:2"},
    {4,3,"4:3"}, {1,1,"1:1"}, {3,4,"3:4"}, {2,3,"2:3"},
    {1,2,"1:2"}, {1,3,"1:3"}, {1,4,"1:4"}, {1,5,"1:5"},
    {1,6,"1:6"}, {1,7,"1:7"}, {1,8,"1:8"}
};

static const Algorithm ALGORITHMS[ALGORITHM_COUNT] = {
    {{{0,0,0,0},{1,0,0,0},{0,1,0,0},{0,0,1,0}}, {1,0,0,0}},
    {{{0,0,0,0},{1,0,0,0},{0,1,0,0},{0,1,0,0}}, {1,0,0,0}},
    {{{0,0,0,0},{1,0,0,0},{1,0,0,0},{0,0,1,0}}, {1,0,0,0}},
    {{{0,0,0,0},{1,0,0,0},{1,0,0,0},{0,0,1,0}}, {1,0,0,0}},
    {{{0,0,0,0},{1,0,0,0},{0,0,0,0},{0,0,1,0}}, {1,0,1,0}},
    {{{0,0,0,0},{0,0,0,0},{0,0,0,0},{1,1,1,0}}, {1,1,1,0}},
    {{{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,1,0}}, {1,1,1,0}},
    {{{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}}, {1,1,1,1}},
    {{{0,0,0,0},{0,0,0,0},{1,1,0,0},{0,0,1,0}}, {1,1,0,0}},
    {{{0,0,0,0},{0,0,0,0},{1,1,0,0},{1,1,0,0}}, {1,1,0,0}},
    {{{0,0,0,0},{1,0,0,0},{1,0,0,0},{1,0,0,0}}, {1,0,0,0}}
};

inline float finite_or(float value, float fallback) {
    return isfinite(value) ? value : fallback;
}

inline int clamp_mode(float value) {
    value = finite_or(value, (float)RATIO_MODE);
    return value >= 0.5f ? FIXED_MODE : RATIO_MODE;
}

inline int algorithm_index(float value) {
    value = finite_or(value, 1.f);
    value = fmaxf(1.f, fminf(11.f, value));
    return (int)floorf(value + 0.5f) - 1;
}

inline int ratio_index(float coarse) {
    coarse = finite_or(coarse, (float)DEFAULT_RATIO_INDEX);
    coarse = fmaxf(COARSE_MIN, fminf(COARSE_MAX, coarse));
    return (int)floorf(coarse + 0.5f);
}

inline float ratio_value(float coarse) {
    const Ratio& ratio = RATIOS[ratio_index(coarse)];
    return (float)ratio.numerator / (float)ratio.denominator;
}

inline const char* ratio_label(float coarse) {
    return RATIOS[ratio_index(coarse)].label;
}

inline float fixed_hz(float coarse) {
    coarse = finite_or(coarse, (float)DEFAULT_RATIO_INDEX);
    coarse = fmaxf(COARSE_MIN, fminf(COARSE_MAX, coarse));
    return expf(coarse / COARSE_MAX * logf(10000.f));
}

inline float fine_multiplier(float cents) {
    cents = finite_or(cents, 0.f);
    return finite_or(exp2f(cents / 1200.f), 1.f);
}

inline float fixed_frequency(float coarse, float fineCents) {
    return finite_or(fixed_hz(coarse) * fine_multiplier(fineCents), 0.f);
}

inline std::string frequency_label(float coarse, int mode,
                                   float fineCents = 0.f) {
    char buffer[32];
    if (clamp_mode((float)mode) == RATIO_MODE)
        return std::string(ratio_label(coarse));

    const float hz = fixed_frequency(coarse, fineCents);
    if (hz < 1000.f)
        snprintf(buffer, sizeof(buffer), "%.1f Hz", (double)hz);
    else
        snprintf(buffer, sizeof(buffer), "%.1f kHz", (double)(hz / 1000.f));
    return std::string(buffer);
}

} // namespace four_v2

#endif
