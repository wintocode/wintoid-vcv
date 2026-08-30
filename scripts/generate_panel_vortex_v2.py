#!/usr/bin/env python3
"""Generate deterministic artwork and widget coordinates for VortexV2.

Run from the project root or any other working directory with::

    python3 scripts/generate_panel_vortex_v2.py

All geometry in this file is millimetres. Rack widget code consumes the
generated coordinates through ``mm2px()``; the SVG supplies only static panel
artwork and structural guides.
"""

from __future__ import annotations

import math
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SVG_PATH = ROOT / "res" / "VortexV2.svg"
HEADER_PATH = ROOT / "src" / "VortexV2" / "layout.h"

# Panel dimensions and FourV2 palette.
HP = 20
WIDTH_MM = HP * 5.08
HEIGHT_MM = 128.5
PANEL_IVORY = "#ece8d9"
LEGEND_CHARCOAL = "#242522"
SECTION_BLUE_GREY = "#556d80"
FUNCTION_ORANGE = "#b7693c"
LOGO_BLUE = "#1a1a2e"
LOGO_ORANGE = "#ff4d00"

# Actual Rack widget envelopes, kept separate from the quiet SVG guides.
PIXELS_PER_MM = 15.0 / 5.08
RACK_SMALL_KNOB_RADIUS = 22.67581 / (2.0 * PIXELS_PER_MM)
RACK_PORT_RADIUS = 23.7 / (2.0 * PIXELS_PER_MM)
SMALL_KNOB_RADIUS = RACK_SMALL_KNOB_RADIUS
PORT_RADIUS = RACK_PORT_RADIUS
OUTPUT_RING_WIDTH = 0.45
OUTPUT_STROKE_WIDTH = 0.55
OUTPUT_BACKPLATE_RADIUS = RACK_PORT_RADIUS + OUTPUT_RING_WIDTH
OUTPUT_BACKPLATE_FILL = "#39445f"
OUTPUT_BACKPLATE_STROKE = "#dfe7f3"

MINIMUM_EDGE_CLEARANCE_MM = 4.0
MINIMUM_LABEL_CLEARANCE_MM = 0.25

# Identity, control, and output section geometry.
TITLE_X = 6.0
TITLE_Y = 8.0
TITLE_FONT_SIZE = 6.6
LOGO_X = 79.0
LOGO_Y = 8.0
LOGO_FONT_SIZE = 3.0
SECTION_STROKE_WIDTH = 0.35
# SVG strokes are centred on their geometry. Keep the visible frame boundary
# within the same 4 mm safe edge clearance as the real Rack widget envelopes.
SECTION_HORIZONTAL_INSET = MINIMUM_EDGE_CLEARANCE_MM + SECTION_STROKE_WIDTH / 2.0
SECTION_WIDTH = WIDTH_MM - 2.0 * SECTION_HORIZONTAL_INSET
GLOBAL_CONTROLS_SECTION = (SECTION_HORIZONTAL_INSET, 13.0, SECTION_WIDTH, 34.0)
OUTPUTS_SECTION = (SECTION_HORIZONTAL_INSET, 53.0, SECTION_WIDTH, 68.0)

# First-fit widget coordinate contract.
CONTROL_XS = (17.0, 42.5, 68.0)
CONTROL_KNOB_Y = 24.0
CONTROL_CV_Y = 39.0
CONTROL_ATTEN_OFFSET_X = 8.5
AUDIO_IN_X = 91.0
AUDIO_IN_Y = 39.0

OUTPUT_COLUMN_XS = (17.0, 50.8, 84.6)
OUTPUT_ROW_YS = (64.0, 79.0, 94.0, 109.0)
OUTPUT_LABEL_OFFSET = 5.5
OUTPUT_LABELS = (
    "LP 6dB", "LP 12dB", "LP 24dB",
    "HP 6dB", "HP 12dB", "HP 24dB",
    "BP", "BP+", "Notch", "Notch+", "AP", "AP+",
)
OUTPUT_COMPONENTS = tuple(
    (label, x, y)
    for y, labels in zip(OUTPUT_ROW_YS, (OUTPUT_LABELS[i:i + 3] for i in range(0, len(OUTPUT_LABELS), 3)))
    for label, x in zip(labels, OUTPUT_COLUMN_XS)
)

CONTROL_NAMES = ("CUTOFF", "RESONANCE", "DRIVE")
COMPONENTS = (
    tuple((f"{name.lower()}_knob", x, CONTROL_KNOB_Y)
          for name, x in zip(CONTROL_NAMES, CONTROL_XS))
    + tuple((f"{name.lower()}_cv", x, CONTROL_CV_Y)
            for name, x in zip(CONTROL_NAMES, CONTROL_XS))
    + tuple((f"{name.lower()}_atten", x + CONTROL_ATTEN_OFFSET_X, CONTROL_CV_Y)
            for name, x in zip(CONTROL_NAMES, CONTROL_XS))
    + (("audio_in", AUDIO_IN_X, AUDIO_IN_Y),)
    + OUTPUT_COMPONENTS
)
COMPONENT_RADII = {
    **{f"{name.lower()}_knob": RACK_SMALL_KNOB_RADIUS for name in CONTROL_NAMES},
    **{f"{name.lower()}_cv": RACK_PORT_RADIUS for name in CONTROL_NAMES},
    **{f"{name.lower()}_atten": RACK_SMALL_KNOB_RADIUS for name in CONTROL_NAMES},
    "audio_in": RACK_PORT_RADIUS,
    **{label: RACK_PORT_RADIUS for label in OUTPUT_LABELS},
}


def _fmt(value: float, digits: int = 3) -> str:
    """Format finite SVG/header coordinates deterministically."""
    if not math.isfinite(value):
        raise ValueError(f"non-finite panel coordinate: {value!r}")
    text = f"{value:.{digits}f}".rstrip("0").rstrip(".")
    return text if text and text != "-0" else "0"


def _cpp_float(value: float) -> str:
    """Format a valid C++11 float literal without changing SVG formatting."""
    text = _fmt(value)
    return text if "." in text else f"{text}.0"


def _circle(x: float, y: float, radius: float, fill: str, stroke: str,
            stroke_width: float) -> str:
    return (
        f'  <circle cx="{_fmt(x)}" cy="{_fmt(y)}" r="{_fmt(radius)}" '
        f'fill="{fill}" stroke="{stroke}" stroke-width="{_fmt(stroke_width)}" />'
    )


def _rect(rect: tuple[float, float, float, float], fill: str, stroke: str) -> str:
    x, y, width, height = rect
    return (
        f'  <rect x="{_fmt(x)}" y="{_fmt(y)}" width="{_fmt(width)}" '
        f'height="{_fmt(height)}" rx="1.5" fill="{fill}" stroke="{stroke}" '
        f'stroke-width="{_fmt(SECTION_STROKE_WIDTH)}" />'
    )


def _text(x: float, y: float, content: str, size: float, fill: str,
          anchor: str = "middle") -> str:
    return (
        f'  <text x="{_fmt(x)}" y="{_fmt(y)}" fill="{fill}" '
        f'font-family="sans-serif" font-size="{_fmt(size)}" '
        f'font-weight="bold" text-anchor="{anchor}">{content}</text>'
    )


def _control_guide(name: str, x: float, y: float) -> str:
    if name.endswith("_knob"):
        return _circle(x, y, 3.4, LEGEND_CHARCOAL, SECTION_BLUE_GREY, 0.30)
    if name.endswith("_atten"):
        return _circle(x, y, 2.2, FUNCTION_ORANGE, SECTION_BLUE_GREY, 0.30)
    return _circle(x, y, 3.1, PANEL_IVORY, SECTION_BLUE_GREY, 0.30)


def _label_clearance(label: str, x: float, y: float) -> dict[str, float]:
    """Return the vertical label-to-port clearance for the output label."""
    label_baseline_y = y - OUTPUT_LABEL_OFFSET
    return {
        "label_x": x,
        "label_y": label_baseline_y,
        "clearance_mm": y - RACK_PORT_RADIUS - label_baseline_y,
    }


LABEL_CLEARANCES = {
    label: _label_clearance(label, x, y)
    for label, x, y in OUTPUT_COMPONENTS
}


def generate_svg() -> str:
    """Return the complete deterministic static VortexV2 panel SVG."""
    lines = [
        '<svg xmlns="http://www.w3.org/2000/svg" '
        f'width="{_fmt(WIDTH_MM)}mm" height="{_fmt(HEIGHT_MM)}mm" '
        f'viewBox="0 0 {_fmt(WIDTH_MM)} {_fmt(HEIGHT_MM)}">',
        f'  <rect width="{_fmt(WIDTH_MM)}" height="{_fmt(HEIGHT_MM)}" fill="{PANEL_IVORY}" />',
        '  <!-- Static identity and structural hierarchy; Rack draws live widgets. -->',
        _text(TITLE_X, TITLE_Y, "VORTEX", TITLE_FONT_SIZE, LOGO_BLUE, "start"),
        _text(LOGO_X, LOGO_Y, "wintoid", LOGO_FONT_SIZE, LOGO_ORANGE, "start"),
        _rect(GLOBAL_CONTROLS_SECTION, "none", SECTION_BLUE_GREY),
        _text(GLOBAL_CONTROLS_SECTION[0] + 3.0, GLOBAL_CONTROLS_SECTION[1] + 4.4,
              "GLOBAL CONTROLS", 2.4, FUNCTION_ORANGE, "start"),
        _rect(OUTPUTS_SECTION, "none", SECTION_BLUE_GREY),
        _text(OUTPUTS_SECTION[0] + 3.0, OUTPUTS_SECTION[1] + 4.4,
              "FILTER OUTPUTS", 2.4, FUNCTION_ORANGE, "start"),
    ]
    for name, x in zip(CONTROL_NAMES, CONTROL_XS):
        lines.append(_text(x, 18.0, name, 2.0, LEGEND_CHARCOAL))
        lines.append(_control_guide(f"{name.lower()}_knob", x, CONTROL_KNOB_Y))
    for name, x in zip(CONTROL_NAMES, CONTROL_XS):
        lines.append(_text(x, 34.0, "CV", 1.8, LEGEND_CHARCOAL))
        lines.append(_control_guide(f"{name.lower()}_cv", x, CONTROL_CV_Y))
        lines.append(_control_guide(f"{name.lower()}_atten", x + CONTROL_ATTEN_OFFSET_X, CONTROL_CV_Y))
    lines.append(_text(AUDIO_IN_X, 34.0, "AUDIO IN", 1.8, LEGEND_CHARCOAL))
    lines.append(_control_guide("audio_in", AUDIO_IN_X, AUDIO_IN_Y))
    for label, x, y in OUTPUT_COMPONENTS:
        lines.append(_text(x, y - OUTPUT_LABEL_OFFSET, label, 1.8, LEGEND_CHARCOAL))
        lines.append(_circle(x, y, OUTPUT_BACKPLATE_RADIUS, OUTPUT_BACKPLATE_FILL,
                             OUTPUT_BACKPLATE_STROKE, OUTPUT_STROKE_WIDTH))
    lines.append('</svg>')
    return "\n".join(lines) + "\n"


def generate_coords_header() -> str:
    """Return the C++ millimetre coordinate header consumed through mm2px()."""
    lines = [
        '#pragma once',
        '// Auto-generated by scripts/generate_panel_vortex_v2.py; do not edit manually.',
        '// All values are millimetres for mm2px() in VCV Rack widget code.',
        '',
        'namespace vortex_v2_layout {',
        '',
        f'constexpr int PANEL_HP = {HP};',
        f'constexpr float PANEL_WIDTH = {_cpp_float(WIDTH_MM)}f;',
        f'constexpr float PANEL_HEIGHT = {_cpp_float(HEIGHT_MM)}f;',
        f'constexpr float TITLE_X = {_cpp_float(TITLE_X)}f;',
        f'constexpr float TITLE_Y = {_cpp_float(TITLE_Y)}f;',
        f'constexpr float TITLE_FONT_SIZE = {_cpp_float(TITLE_FONT_SIZE)}f;',
        f'constexpr float LOGO_X = {_cpp_float(LOGO_X)}f;',
        f'constexpr float LOGO_Y = {_cpp_float(LOGO_Y)}f;',
        f'constexpr float LOGO_FONT_SIZE = {_cpp_float(LOGO_FONT_SIZE)}f;',
        '',
    ]
    for prefix, rect in (("GLOBAL_CONTROLS_SECTION", GLOBAL_CONTROLS_SECTION),
                         ("OUTPUTS_SECTION", OUTPUTS_SECTION)):
        for suffix, value in zip(("X", "Y", "WIDTH", "HEIGHT"), rect):
            lines.append(f'constexpr float {prefix}_{suffix} = {_cpp_float(value)}f;')
        lines.append('')
    for name, x in zip(CONTROL_NAMES, CONTROL_XS):
        constant = name.replace(" ", "_")
        lines.append(f'constexpr float {constant}_KNOB_X = {_cpp_float(x)}f;')
        lines.append(f'constexpr float {constant}_KNOB_Y = {_cpp_float(CONTROL_KNOB_Y)}f;')
        lines.append(f'constexpr float {constant}_CV_X = {_cpp_float(x)}f;')
        lines.append(f'constexpr float {constant}_CV_Y = {_cpp_float(CONTROL_CV_Y)}f;')
        lines.append(f'constexpr float {constant}_ATTEN_X = {_cpp_float(x + CONTROL_ATTEN_OFFSET_X)}f;')
        lines.append(f'constexpr float {constant}_ATTEN_Y = {_cpp_float(CONTROL_CV_Y)}f;')
        lines.append('')
    lines.extend([
        f'constexpr float AUDIO_IN_X = {_cpp_float(AUDIO_IN_X)}f;',
        f'constexpr float AUDIO_IN_Y = {_cpp_float(AUDIO_IN_Y)}f;',
        '',
        'constexpr float OUTPUT_COLUMN_XS[3] = {'
        + ', '.join(f'{_cpp_float(x)}f' for x in OUTPUT_COLUMN_XS) + '};',
        'constexpr float OUTPUT_ROW_YS[4] = {'
        + ', '.join(f'{_cpp_float(y)}f' for y in OUTPUT_ROW_YS) + '};',
        f'constexpr float OUTPUT_LABEL_OFFSET = {_cpp_float(OUTPUT_LABEL_OFFSET)}f;',
        '',
        '} // namespace vortex_v2_layout',
        '',
    ])
    return "\n".join(lines)


def main() -> None:
    SVG_PATH.parent.mkdir(parents=True, exist_ok=True)
    SVG_PATH.write_text(generate_svg(), encoding="utf-8")
    print(f"Wrote {SVG_PATH}")
    HEADER_PATH.parent.mkdir(parents=True, exist_ok=True)
    HEADER_PATH.write_text(generate_coords_header(), encoding="utf-8")
    print(f"Wrote {HEADER_PATH}")


if __name__ == "__main__":
    main()
