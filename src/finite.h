#pragma once

#include <cmath>

namespace wintoid {

// Substitute a fallback for non-finite values (NaN, +/-inf). A single NaN
// sample from an upstream module otherwise latches permanently in recursive
// filter state and phase accumulators, silencing the lane until re-init.
// Mirrors finite_or() in src/FourV2/model.h for the module input boundary.
inline float finite_or(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

} // namespace wintoid
