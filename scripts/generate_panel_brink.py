#!/usr/bin/env python3
"""Generate the Brink panel SVG and C++ coordinate header.

The panel geometry is kept in this module so the Rack widget and its artwork
share one deterministic set of millimetre coordinates.  Running the script
from any directory writes the generated files into this repository.
"""

from pathlib import Path


# Panel dimensions
HP = 12
WIDTH_MM = HP * 5.08
HEIGHT_MM = 128.5

# Mirrored channel centres and the shared logic row
CHANNEL_A_X = WIDTH_MM / 4
CHANNEL_B_X = WIDTH_MM * 3 / 4
LOGIC_X = (8.0, 23.0, 38.0, 53.0)

# Fixed vertical layout, in millimetres
Y_CHANNEL_HEADER = 15.0
Y_KNOBS = 24.0
Y_SIGNAL_POSITION = 38.0
Y_CENTER_CV = 50.0
Y_WIDTH_CV = 62.0
Y_STATE_GATES = 74.0
Y_EVENTS_UP = 88.0
Y_EVENTS_DOWN = 100.0
Y_LOGIC = 114.0

# Shared visual reservations used by the NanoVG widget
TITLE_Y = 8.0
LOGO_BASELINE_Y = 124.5
LOGO_UNDERLINE_Y = 127.0
POSITION_RAIL_HEIGHT = 10.0

PAIR_OFFSET = 7.24
CHANNEL_A_LEFT_X = CHANNEL_A_X - PAIR_OFFSET
CHANNEL_A_RIGHT_X = CHANNEL_A_X + PAIR_OFFSET
CHANNEL_B_LEFT_X = CHANNEL_B_X - PAIR_OFFSET
CHANNEL_B_RIGHT_X = CHANNEL_B_X + PAIR_OFFSET

# Geometry for the generated structural artwork
SMALL_KNOB_RADIUS = 2.5
TRIMPOT_RADIUS = 2.0
PORT_RADIUS = 3.2
RAIL_WIDTH = 2.0
OUTLINE_MARGIN = 2.0


# Every coordinate consumed by the Rack widget.  Keeping the names in a
# tuple, rather than depending on dictionary ordering, also keeps output
# stable on older Python versions.
COORDINATE_NAMES = (
    "A_CENTER_KNOB", "A_WIDTH_KNOB", "A_SIGNAL", "A_POSITION",
    "A_CENTER_CV", "A_CENTER_ATTEN", "A_WIDTH_CV", "A_WIDTH_ATTEN",
    "A_INSIDE", "A_OUTSIDE", "A_LOW_UP", "A_HIGH_UP",
    "A_LOW_DOWN", "A_HIGH_DOWN", "A_POSITION_RAIL",
    "B_CENTER_KNOB", "B_WIDTH_KNOB", "B_SIGNAL", "B_POSITION",
    "B_CENTER_CV", "B_CENTER_ATTEN", "B_WIDTH_CV", "B_WIDTH_ATTEN",
    "B_INSIDE", "B_OUTSIDE", "B_LOW_UP", "B_HIGH_UP",
    "B_LOW_DOWN", "B_HIGH_DOWN", "B_POSITION_RAIL",
    "AND_OUTPUT", "OR_OUTPUT", "XOR_OUTPUT", "STATE_OUTPUT",
)


def _channel_coordinates(prefix, channel_x):
    """Return the mirrored widget centres for one channel."""
    left = channel_x - PAIR_OFFSET
    right = channel_x + PAIR_OFFSET
    return (
        (f"{prefix}_CENTER_KNOB", (left, Y_KNOBS)),
        (f"{prefix}_WIDTH_KNOB", (right, Y_KNOBS)),
        (f"{prefix}_SIGNAL", (left, Y_SIGNAL_POSITION)),
        (f"{prefix}_POSITION", (right, Y_SIGNAL_POSITION)),
        (f"{prefix}_CENTER_CV", (left, Y_CENTER_CV)),
        (f"{prefix}_CENTER_ATTEN", (right, Y_CENTER_CV)),
        (f"{prefix}_WIDTH_CV", (left, Y_WIDTH_CV)),
        (f"{prefix}_WIDTH_ATTEN", (right, Y_WIDTH_CV)),
        (f"{prefix}_INSIDE", (left, Y_STATE_GATES)),
        (f"{prefix}_OUTSIDE", (right, Y_STATE_GATES)),
        (f"{prefix}_LOW_UP", (left, Y_EVENTS_UP)),
        (f"{prefix}_HIGH_UP", (right, Y_EVENTS_UP)),
        (f"{prefix}_LOW_DOWN", (left, Y_EVENTS_DOWN)),
        (f"{prefix}_HIGH_DOWN", (right, Y_EVENTS_DOWN)),
        (f"{prefix}_POSITION_RAIL", (channel_x, Y_SIGNAL_POSITION)),
    )


_coordinate_pairs = (
    _channel_coordinates("A", CHANNEL_A_X)
    + _channel_coordinates("B", CHANNEL_B_X)
    + (
        ("AND_OUTPUT", (LOGIC_X[0], Y_LOGIC)),
        ("OR_OUTPUT", (LOGIC_X[1], Y_LOGIC)),
        ("XOR_OUTPUT", (LOGIC_X[2], Y_LOGIC)),
        ("STATE_OUTPUT", (LOGIC_X[3], Y_LOGIC)),
    )
)
COORDINATES = dict(_coordinate_pairs)

# Physical widget centres.  Position rails are drawn separately and are
# intentionally excluded from this contract.
COMPONENTS = tuple(
    (name, x, y)
    for name, (x, y) in _coordinate_pairs
    if not name.endswith("_POSITION_RAIL")
)


def _fmt(value):
    """Format a coordinate without introducing platform-dependent output."""
    return f"{value:.2f}"


def _circle(x, y, radius, fill, stroke):
    return (
        f'  <circle cx="{_fmt(x)}" cy="{_fmt(y)}" r="{_fmt(radius)}" '
        f'fill="{fill}" stroke="{stroke}" stroke-width="0.30" />'
    )


def _rect(x, y, width, height, fill, stroke, radius=None):
    rounded = f' rx="{_fmt(radius)}"' if radius is not None else ""
    return (
        f'  <rect x="{_fmt(x)}" y="{_fmt(y)}" width="{_fmt(width)}" '
        f'height="{_fmt(height)}" fill="{fill}" stroke="{stroke}" '
        f'stroke-width="0.30"{rounded} />'
    )


def _line(x1, y1, x2, y2, stroke="#3b4668", width=0.20):
    return (
        f'  <line x1="{_fmt(x1)}" y1="{_fmt(y1)}" '
        f'x2="{_fmt(x2)}" y2="{_fmt(y2)}" stroke="{stroke}" '
        f'stroke-width="{_fmt(width)}" />'
    )


def _component_style(name):
    if "_ATTEN" in name:
        return TRIMPOT_RADIUS, "#30364d", "#68718e"
    if any(token in name for token in ("_KNOB",)):
        return SMALL_KNOB_RADIUS, "#30364d", "#aab3c8"
    return PORT_RADIUS, "#202538", "#77819c"


def generate_svg():
    """Return the deterministic structural SVG for the Brink panel."""
    lines = [
        '<svg xmlns="http://www.w3.org/2000/svg" '
        f'width="{WIDTH_MM:.2f}mm" height="{HEIGHT_MM:.1f}mm" '
        f'viewBox="0 0 {WIDTH_MM:.2f} {HEIGHT_MM:.1f}">',
        f'  <rect width="{WIDTH_MM:.2f}" height="{HEIGHT_MM:.1f}" '
        'fill="#1a1a2e" />',
    ]

    # Two quiet channel fields preserve the mirrored hierarchy without
    # competing with the controls rendered by Rack.
    outline_y = 11.0
    outline_height = 108.0
    half_width = WIDTH_MM / 2
    outline_width = half_width - 4.0
    lines.append(_rect(
        OUTLINE_MARGIN,
        outline_y,
        outline_width,
        outline_height,
        "#1d2036",
        "#303957",
        1.5,
    ))
    lines.append(_rect(
        half_width + OUTLINE_MARGIN,
        outline_y,
        outline_width,
        outline_height,
        "#1d2036",
        "#303957",
        1.5,
    ))

    # Section separators leave the control rows readable at normal zoom.
    for y in (31.0, 44.0, 56.0, 68.0, 81.0, 94.0, 107.0):
        lines.append(_line(4.0, y, WIDTH_MM - 4.0, y))

    # Three small arrows indicate the A-to-B normalisation paths.
    for y in (Y_SIGNAL_POSITION, Y_CENTER_CV, Y_WIDTH_CV):
        lines.append(_line(CHANNEL_A_RIGHT_X + 2.0, y,
                           CHANNEL_B_LEFT_X - 2.0, y,
                           "#56627d", 0.30))
        arrow_x = WIDTH_MM / 2 + 1.5
        lines.append(_line(arrow_x - 1.2, y - 1.0, arrow_x, y,
                           "#56627d", 0.30))
        lines.append(_line(arrow_x - 1.2, y + 1.0, arrow_x, y,
                           "#56627d", 0.30))

    # Structural rail tracks are not physical ports and therefore do not
    # appear in COMPONENTS.
    for channel_x in (CHANNEL_A_X, CHANNEL_B_X):
        lines.append(_rect(
            channel_x - RAIL_WIDTH / 2,
            Y_SIGNAL_POSITION - POSITION_RAIL_HEIGHT / 2,
            RAIL_WIDTH,
            POSITION_RAIL_HEIGHT,
            "#111525",
            "#3b4668",
            0.8,
        ))

    # Control and port guides provide a quiet alignment reference in the
    # panel artwork.  NanoVG labels, lights, and active rail markers are
    # supplied by the widget.
    for name, x, y in COMPONENTS:
        radius, fill, stroke = _component_style(name)
        lines.append(_circle(x, y, radius, fill, stroke))

    lines.append('</svg>')
    return "\n".join(lines)


def generate_header():
    """Return the deterministic C++11 coordinate header."""
    lines = [
        "#pragma once",
        "// Generated by scripts/generate_panel_brink.py.",
        "// Coordinates are millimetres for use with mm2px().",
        "",
        "namespace brink_layout {",
        "",
        f"constexpr float PANEL_WIDTH = {WIDTH_MM:.2f}f;",
        f"constexpr float PANEL_HEIGHT = {HEIGHT_MM:.1f}f;",
        f"constexpr int PANEL_HP = {HP};",
        f"constexpr float TITLE_Y = {TITLE_Y:.1f}f;",
        f"constexpr float LOGO_BASELINE_Y = {LOGO_BASELINE_Y:.1f}f;",
        f"constexpr float LOGO_UNDERLINE_Y = {LOGO_UNDERLINE_Y:.1f}f;",
        f"constexpr float POSITION_RAIL_HEIGHT = {POSITION_RAIL_HEIGHT:.1f}f;",
        "",
    ]

    for name in COORDINATE_NAMES:
        x, y = COORDINATES[name]
        lines.append(f"constexpr float {name}_X = {_fmt(x)}f;")
        lines.append(f"constexpr float {name}_Y = {_fmt(y)}f;")

    lines.extend(("", "} // namespace brink_layout", ""))
    return "\n".join(lines)


# Keep the established helper name available to scripts that generate other
# portfolio panels, while exposing the shorter public function above.
generate_coords_header = generate_header


def main():
    root = Path(__file__).resolve().parents[1]
    svg_path = root / "res" / "Brink.svg"
    header_path = root / "src" / "Brink" / "layout.h"
    svg_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.parent.mkdir(parents=True, exist_ok=True)
    svg_path.write_text(generate_svg() + "\n", encoding="utf-8")
    header_path.write_text(generate_header(), encoding="utf-8")
    print(f"Wrote {svg_path}")
    print(f"Wrote {header_path}")


if __name__ == "__main__":
    main()
