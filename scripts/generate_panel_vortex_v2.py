#!/usr/bin/env python3
"""Generate the VortexV2 panel SVG and C++ coordinate header.

Run from the project root or from any other working directory with::

    python3 scripts/generate_panel_vortex_v2.py

All geometry in this file is millimetres.  Rack widget code consumes the
generated coordinates through ``mm2px()``.  The SVG follows the FourV2 panel
system: it owns the static hierarchy, labels, and canonical outlined logo;
the Rack overlay redraws the labels for hosts that do not render SVG text.
"""

from __future__ import annotations

from html import escape
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
LOGO_PATH = ROOT / "res" / "WintoidLogo.svg"
GLYPH_DATA_PATH = ROOT / "scripts" / "assets" / "wintoid_logo_glyphs.json"
SVG_PATH = ROOT / "res" / "VortexV2.svg"
HEADER_PATH = ROOT / "src" / "VortexV2" / "layout.h"

# Panel dimensions and FourV2 palette.
HP = 12
WIDTH_MM = HP * 5.08
HEIGHT_MM = 128.5
PANEL_IVORY = "#ece8d9"
LEGEND_CHARCOAL = "#242522"
SECTION_BLUE_GREY = "#556d80"
FUNCTION_ORANGE = "#b7693c"
LOGO_BLUE = "#1a1a2e"
LOGO_ORANGE = "#ff4d00"
SECTION_FILL = "#e3e0d1"
SECTION_FILL_ALT = "#e7e3d4"

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

# Identity and canonical outlined logo placement.  LOGO_TARGET_X is the
# visible path bound, matching the placement convention used by FourV2.
TITLE_X = 6.0
TITLE_Y = 7.0
TITLE_FONT_SIZE = 6.6
LOGO_TARGET_X = WIDTH_MM - 18.0
LOGO_TARGET_Y = 1.8
LOGO_SCALE = 0.06

# FourV2-style filled sections with no section-heading labels.
SECTION_HORIZONTAL_INSET = MINIMUM_EDGE_CLEARANCE_MM
SECTION_WIDTH = WIDTH_MM - 2.0 * SECTION_HORIZONTAL_INSET
CONTROL_SECTION = (SECTION_HORIZONTAL_INSET, 11.0, SECTION_WIDTH, 39.5)
OUTPUT_SECTION = (SECTION_HORIZONTAL_INSET, 54.0, SECTION_WIDTH, 70.5)
SECTION_STROKE_WIDTH = 0.35

# Each filter control is a horizontal knob + CV/attenuverter row.  The
# rounded box encloses only the socket/attenuverter pair, as in FourV2.
CONTROL_NAMES = ("CUTOFF", "RESONANCE", "DRIVE")
CONTROL_LABELS = ("CUTOFF", "RESO", "DRIVE")
CONTROL_KNOB_X = 13.0
CONTROL_CV_X = 27.0
CONTROL_ATTEN_X = 35.5
CONTROL_ROW_YS = (20.0, 32.0, 44.0)
CONTROL_LABEL_OFFSET = 5.1
CONTROL_LABEL_FONT_SIZE = 2.25
CONTROL_LABEL_YS = tuple(y - CONTROL_LABEL_OFFSET for y in CONTROL_ROW_YS)

CONTROL_GROUPS = tuple(
    (
        name.lower(),
        (CONTROL_KNOB_X, y),
        (CONTROL_CV_X, y),
        (CONTROL_ATTEN_X, y),
    )
    for name, y in zip(CONTROL_NAMES, CONTROL_ROW_YS)
)

AUDIO_IN_X = 48.96
AUDIO_IN_Y = 44.0
AUDIO_IN_LABEL_Y = AUDIO_IN_Y - CONTROL_LABEL_OFFSET
AUDIO_IN_LABEL_FONT_SIZE = 2.25

OUTPUT_COLUMN_XS = (12.0, 30.48, 48.96)
OUTPUT_ROW_YS = (67.0, 82.0, 97.0, 112.0)
OUTPUT_LABEL_OFFSET = 5.6
OUTPUT_LABEL_FONT_SIZE = 2.35
OUTPUT_LABELS = (
    "LP 6dB", "LP 12dB", "LP 24dB",
    "HP 6dB", "HP 12dB", "HP 24dB",
    "BP", "BP+", "NOTCH", "NOTCH+", "AP", "AP+",
)
OUTPUT_COMPONENTS = tuple(
    (label, x, y)
    for y, labels in zip(
        OUTPUT_ROW_YS,
        (OUTPUT_LABELS[index:index + 3]
         for index in range(0, len(OUTPUT_LABELS), 3)),
    )
    for label, x in zip(labels, OUTPUT_COLUMN_XS)
)

# Small outline-only enclosures make each socket/attenuverter pair read as
# one control without changing the established FourV2 spacing pattern.
PAIR_GROUP_PADDING_X = 1.25
PAIR_GROUP_PADDING_TOP = 0.35
PAIR_GROUP_PADDING_BOTTOM = 0.75
PAIR_GROUP_RADIUS = 1.0
PAIR_GROUP_STROKE_WIDTH = 0.25
PAIR_GROUP_FILL = "none"
PAIR_GROUP_STROKE = SECTION_BLUE_GREY


def _pair_group_rect(input_x: float, atten_x: float, y: float) -> tuple[float, ...]:
    left = input_x - PORT_RADIUS - PAIR_GROUP_PADDING_X
    right = atten_x + SMALL_KNOB_RADIUS + PAIR_GROUP_PADDING_X
    top = y - PORT_RADIUS - PAIR_GROUP_PADDING_TOP
    bottom = y + PORT_RADIUS + PAIR_GROUP_PADDING_BOTTOM
    return left, top, right - left, bottom - top


PAIR_GROUP_RECTS = tuple(
    (
        f"{name}-cv-group",
        _pair_group_rect(cv[0], atten[0], cv[1]),
    )
    for name, _knob, cv, atten in CONTROL_GROUPS
)
PAIR_GROUP_RECT_BY_ID = dict(PAIR_GROUP_RECTS)

COMPONENTS = (
    tuple(
        (f"{name}_knob", knob[0], knob[1])
        for name, knob, _cv, _atten in CONTROL_GROUPS
    )
    + tuple(
        (f"{name}_cv", cv[0], cv[1])
        for name, _knob, cv, _atten in CONTROL_GROUPS
    )
    + tuple(
        (f"{name}_atten", atten[0], atten[1])
        for name, _knob, _cv, atten in CONTROL_GROUPS
    )
    + (("audio_in", AUDIO_IN_X, AUDIO_IN_Y),)
    + OUTPUT_COMPONENTS
)
COMPONENT_RADII = {
    **{f"{name}_knob": RACK_SMALL_KNOB_RADIUS
       for name, _knob, _cv, _atten in CONTROL_GROUPS},
    **{f"{name}_cv": RACK_PORT_RADIUS
       for name, _knob, _cv, _atten in CONTROL_GROUPS},
    **{f"{name}_atten": RACK_SMALL_KNOB_RADIUS
       for name, _knob, _cv, _atten in CONTROL_GROUPS},
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


def _circle(
    x: float,
    y: float,
    radius: float,
    fill: str,
    stroke: str,
    stroke_width: float,
) -> str:
    return (
        f'  <circle cx="{_fmt(x)}" cy="{_fmt(y)}" r="{_fmt(radius)}" '
        f'fill="{fill}" stroke="{stroke}" '
        f'stroke-width="{_fmt(stroke_width)}" />'
    )


def _rect(
    x: float,
    y: float,
    width: float,
    height: float,
    fill: str,
    stroke: str,
    *,
    identifier: str | None = None,
    radius: float | None = None,
    stroke_width: float = SECTION_STROKE_WIDTH,
) -> str:
    id_attribute = f' id="{identifier}"' if identifier else ""
    radius_attribute = f' rx="{_fmt(radius)}"' if radius is not None else ""
    return (
        f'  <rect{id_attribute} x="{_fmt(x)}" y="{_fmt(y)}" '
        f'width="{_fmt(width)}" height="{_fmt(height)}" '
        f'fill="{fill}" stroke="{stroke}" '
        f'stroke-width="{_fmt(stroke_width)}"{radius_attribute} />'
    )


def _text(
    x: float,
    y: float,
    value: str,
    *,
    size: float,
    fill: str = LEGEND_CHARCOAL,
    anchor: str = "middle",
    weight: str = "400",
) -> str:
    return (
        f'  <text x="{_fmt(x)}" y="{_fmt(y)}" text-anchor="{anchor}" '
        f'font-family="DejaVu Sans" font-size="{_fmt(size)}" '
        f'font-weight="{weight}" fill="{fill}">'
        f"{escape(value)}"
        "</text>"
    )


def _control_guide(name: str, x: float, y: float) -> str:
    if name.endswith("_knob"):
        return _circle(x, y, 3.4, LEGEND_CHARCOAL, SECTION_BLUE_GREY, 0.30)
    if name.endswith("_atten"):
        return _circle(x, y, 2.2, FUNCTION_ORANGE, SECTION_BLUE_GREY, 0.30)
    return _circle(x, y, 3.1, PANEL_IVORY, SECTION_BLUE_GREY, 0.30)


def _label_clearance(label: str, x: float, y: float) -> dict[str, float]:
    """Return the vertical label-to-port clearance for an output label."""
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


def _logo_elements() -> list[str]:
    """Return canonical logo groups/underlines with XML namespaces removed."""
    if not LOGO_PATH.exists():
        raise RuntimeError(f"missing canonical logo asset: {LOGO_PATH}")
    root = ET.parse(LOGO_PATH).getroot()
    try:
        glyph_data = json.loads(GLYPH_DATA_PATH.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise RuntimeError(f"missing checked-in glyph data: {GLYPH_DATA_PATH}") from error
    expected_digest = glyph_data.get("source_font_sha256")
    if (
        not isinstance(expected_digest, str)
        or len(expected_digest) != 64
        or any(character not in "0123456789abcdef" for character in expected_digest)
    ):
        raise RuntimeError("glyph data has no valid source-font SHA-256")
    actual_digest = root.attrib.get("data-source-font-sha256")
    if actual_digest != expected_digest:
        raise RuntimeError(
            "canonical logo source-font digest mismatch: "
            f"expected {expected_digest}, got {actual_digest}"
        )
    wanted = []
    for identifier in ("wint-glyphs", "wint-underline", "oid-glyphs", "oid-underline"):
        match = next(
            (element for element in root.iter()
             if element.attrib.get("id") == identifier),
            None,
        )
        if match is None:
            raise RuntimeError(f"canonical logo is missing {identifier}")
        clone = ET.fromstring(ET.tostring(match, encoding="unicode"))
        for element in clone.iter():
            if "}" in element.tag:
                element.tag = element.tag.split("}", 1)[1]
            element.text = None
            element.tail = None
        wanted.append(ET.tostring(clone, encoding="unicode", short_empty_elements=True))
    return wanted


def _embedded_logo() -> list[str]:
    """Embed the same outlined logo geometry used by FourV2."""
    root = ET.parse(LOGO_PATH).getroot()
    view_x, view_y, _view_width, _view_height = (
        float(value) for value in root.attrib["viewBox"].split()
    )
    translate_x = LOGO_TARGET_X - LOGO_SCALE * view_x
    translate_y = LOGO_TARGET_Y - LOGO_SCALE * view_y
    transform = (
        f'translate({_fmt(translate_x)} {_fmt(translate_y)}) '
        f'scale({_fmt(LOGO_SCALE, 4)})'
    )
    lines = [f'  <g id="wintoid-logo" transform="{transform}">']
    lines.extend(f"    {element}" for element in _logo_elements())
    lines.append("  </g>")
    return lines


def generate_svg() -> str:
    """Return the deterministic FourV2-style VortexV2 panel SVG."""
    lines = [
        '<svg xmlns="http://www.w3.org/2000/svg" '
        f'width="{_fmt(WIDTH_MM, 2)}mm" height="{_fmt(HEIGHT_MM, 1)}mm" '
        f'viewBox="0 0 {_fmt(WIDTH_MM, 2)} {_fmt(HEIGHT_MM, 1)}">',
        _rect(0.0, 0.0, WIDTH_MM, HEIGHT_MM,
               PANEL_IVORY, PANEL_IVORY, stroke_width=0.0),
    ]
    lines.extend(_embedded_logo())
    lines.append(_text(
        TITLE_X, TITLE_Y, "Vortex V2", size=TITLE_FONT_SIZE,
        anchor="start", weight="700",
    ))

    control_x, control_y, control_width, control_height = CONTROL_SECTION
    lines.append(_rect(
        control_x, control_y, control_width, control_height,
        SECTION_FILL, SECTION_BLUE_GREY, identifier="controls-section",
        radius=1.4,
    ))
    output_x, output_y, output_width, output_height = OUTPUT_SECTION
    lines.append(_rect(
        output_x, output_y, output_width, output_height,
        SECTION_FILL_ALT, SECTION_BLUE_GREY, identifier="outputs-section",
        radius=1.4,
    ))

    for identifier, (group_x, group_y, group_width, group_height) in PAIR_GROUP_RECTS:
        lines.append(_rect(
            group_x, group_y, group_width, group_height,
            PAIR_GROUP_FILL, PAIR_GROUP_STROKE, identifier=identifier,
            radius=PAIR_GROUP_RADIUS, stroke_width=PAIR_GROUP_STROKE_WIDTH,
        ))

    for index, (name, knob, cv, atten) in enumerate(CONTROL_GROUPS):
        lines.append(_text(
            knob[0], CONTROL_LABEL_YS[index], CONTROL_LABELS[index],
            size=CONTROL_LABEL_FONT_SIZE,
        ))
        lines.append(_control_guide(f"{name}_knob", *knob))
        lines.append(_control_guide(f"{name}_cv", *cv))
        lines.append(_control_guide(f"{name}_atten", *atten))
    lines.append(_text(
        AUDIO_IN_X, AUDIO_IN_LABEL_Y, "IN", size=AUDIO_IN_LABEL_FONT_SIZE,
    ))
    lines.append(_control_guide("audio_in", AUDIO_IN_X, AUDIO_IN_Y))

    for label, x, y in OUTPUT_COMPONENTS:
        lines.append(_text(
            x, y - OUTPUT_LABEL_OFFSET, label,
            size=OUTPUT_LABEL_FONT_SIZE,
        ))
        lines.append(_circle(
            x, y, OUTPUT_BACKPLATE_RADIUS, OUTPUT_BACKPLATE_FILL,
            OUTPUT_BACKPLATE_STROKE, OUTPUT_STROKE_WIDTH,
        ))

    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def _header_float(name: str, value: float) -> str:
    return f"constexpr float {name} = {_cpp_float(value)}f;"


def generate_coords_header() -> str:
    """Return the generated C++ millimetre coordinate header."""
    lines = [
        "#pragma once",
        "// Auto-generated by scripts/generate_panel_vortex_v2.py; do not edit manually.",
        "// All panel coordinates and dimensions are millimetres.",
        "",
        "namespace vortex_v2_layout {",
        "",
        f"constexpr int PANEL_HP = {HP};",
        _header_float("PANEL_WIDTH", WIDTH_MM),
        _header_float("PANEL_HEIGHT", HEIGHT_MM),
        _header_float("TITLE_X", TITLE_X),
        _header_float("TITLE_Y", TITLE_Y),
        _header_float("TITLE_FONT_SIZE", TITLE_FONT_SIZE),
        _header_float("LOGO_TARGET_X", LOGO_TARGET_X),
        _header_float("LOGO_TARGET_Y", LOGO_TARGET_Y),
        _header_float("LOGO_SCALE", LOGO_SCALE),
        _header_float("MINIMUM_EDGE_CLEARANCE_MM", MINIMUM_EDGE_CLEARANCE_MM),
        _header_float("MINIMUM_LABEL_CLEARANCE_MM", MINIMUM_LABEL_CLEARANCE_MM),
        "",
        "// Filled FourV2-style sections",
        _header_float("CONTROL_SECTION_X", CONTROL_SECTION[0]),
        _header_float("CONTROL_SECTION_Y", CONTROL_SECTION[1]),
        _header_float("CONTROL_SECTION_WIDTH", CONTROL_SECTION[2]),
        _header_float("CONTROL_SECTION_HEIGHT", CONTROL_SECTION[3]),
        _header_float("OUTPUT_SECTION_X", OUTPUT_SECTION[0]),
        _header_float("OUTPUT_SECTION_Y", OUTPUT_SECTION[1]),
        _header_float("OUTPUT_SECTION_WIDTH", OUTPUT_SECTION[2]),
        _header_float("OUTPUT_SECTION_HEIGHT", OUTPUT_SECTION[3]),
        "",
        "// Horizontal control rows",
        _header_float("CONTROL_KNOB_X", CONTROL_KNOB_X),
        _header_float("CONTROL_CV_X", CONTROL_CV_X),
        _header_float("CONTROL_ATTEN_X", CONTROL_ATTEN_X),
        _header_float("CONTROL_LABEL_OFFSET", CONTROL_LABEL_OFFSET),
        _header_float("CONTROL_LABEL_FONT_SIZE", CONTROL_LABEL_FONT_SIZE),
    ]
    for name, row_y, label_y in zip(CONTROL_NAMES, CONTROL_ROW_YS, CONTROL_LABEL_YS):
        lines.extend((
            _header_float(f"{name}_KNOB_X", CONTROL_KNOB_X),
            _header_float(f"{name}_KNOB_Y", row_y),
            _header_float(f"{name}_CV_X", CONTROL_CV_X),
            _header_float(f"{name}_CV_Y", row_y),
            _header_float(f"{name}_ATTEN_X", CONTROL_ATTEN_X),
            _header_float(f"{name}_ATTEN_Y", row_y),
            _header_float(f"{name}_LABEL_Y", label_y),
        ))
    lines.extend((
        "",
        "// Audio input",
        _header_float("AUDIO_IN_X", AUDIO_IN_X),
        _header_float("AUDIO_IN_Y", AUDIO_IN_Y),
        _header_float("AUDIO_IN_LABEL_Y", AUDIO_IN_LABEL_Y),
        _header_float("AUDIO_IN_LABEL_FONT_SIZE", AUDIO_IN_LABEL_FONT_SIZE),
        "",
        "// Output matrix",
        "constexpr float OUTPUT_COLUMN_XS[3] = {"
        + ", ".join(f"{_cpp_float(x)}f" for x in OUTPUT_COLUMN_XS) + "};",
        "constexpr float OUTPUT_ROW_YS[4] = {"
        + ", ".join(f"{_cpp_float(y)}f" for y in OUTPUT_ROW_YS) + "};",
        _header_float("OUTPUT_LABEL_OFFSET", OUTPUT_LABEL_OFFSET),
        _header_float("OUTPUT_LABEL_FONT_SIZE", OUTPUT_LABEL_FONT_SIZE),
        "",
        "// CV/attenuverter pair boxes",
        _header_float("PAIR_GROUP_RADIUS", PAIR_GROUP_RADIUS),
        _header_float("PAIR_GROUP_STROKE_WIDTH", PAIR_GROUP_STROKE_WIDTH),
    ))
    for identifier, (x, y, width, height) in PAIR_GROUP_RECTS:
        constant = identifier.replace("-", "_").upper()
        lines.extend((
            _header_float(f"{constant}_X", x),
            _header_float(f"{constant}_Y", y),
            _header_float(f"{constant}_WIDTH", width),
            _header_float(f"{constant}_HEIGHT", height),
        ))
    lines.extend(("", "} // namespace vortex_v2_layout", ""))
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
