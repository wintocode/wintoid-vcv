#pragma once

// Rack-independent stroke geometry for custom widgets.
//
// MetaModule clips custom drawing to the widget box, so stroked outlines
// whose path touches the box edge lose half a stroke width on that edge.
// These helpers inset stroked geometry so the visible stroke stays inside
// the box on every host.  Pure C++11, no SDK includes, so the contract is
// testable on the host without Rack or MetaModule.

namespace wintoid {
namespace ui {

// Clamp a value to the nonnegative range; negative stroke widths are
// meaningless and are treated as zero everywhere below.
inline float nonnegative(float value)
{
    return value < 0.f ? 0.f : value;
}

// Distance from a box edge to the centre line of a stroke drawn just
// inside that edge: half the stroke width.
inline float stroke_inset(float strokeWidth)
{
    return 0.5f * nonnegative(strokeWidth);
}

// Remaining extent of an axis after reserving the full stroke width
// (half on each side); never negative.
inline float inset_extent(float extent, float strokeWidth)
{
    const float remaining = extent - nonnegative(strokeWidth);
    return remaining < 0.f ? 0.f : remaining;
}

// Clamp a stroked line's centre coordinate so the whole stroke stays
// within [0, extent] on that axis.  Degenerate boxes whose extent cannot
// hold the stroke centre deterministically at the box centre.
inline float clamp_stroke_center(float position, float extent, float strokeWidth)
{
    const float inset = stroke_inset(strokeWidth);
    const float upper = extent - inset;
    if (upper < inset)
        return 0.5f * extent;
    if (position < inset)
        return inset;
    if (position > upper)
        return upper;
    return position;
}

} // namespace ui
} // namespace wintoid
