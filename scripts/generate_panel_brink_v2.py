#!/usr/bin/env python3
"""Generate the deterministic SEM BrinkV2 panel SVG and layout header.

All geometry in this module is expressed in millimetres.  The future Rack
widget consumes the generated values with ``mm2px()``, while this generator
uses the same values to make the checked-in static faceplate.
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
SVG_PATH = ROOT / "res" / "BrinkV2.svg"
HEADER_PATH = ROOT / "src" / "BrinkV2" / "layout.h"

# SEM palette and panel dimensions.
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
CONTROL_FILL = "#242522"
CONTROL_STROKE = "#556d80"
PORT_FILL = "#ece8d9"
PORT_STROKE = "#556d80"

PIXELS_PER_MM = 15.0 / 5.08
RACK_SMALL_KNOB_RADIUS = 22.67581 / (2.0 * PIXELS_PER_MM)
RACK_TRIMPOT_RADIUS = 18.0 / (2.0 * PIXELS_PER_MM)
RACK_PORT_RADIUS = 23.7 / (2.0 * PIXELS_PER_MM)
SMALL_KNOB_RADIUS = RACK_SMALL_KNOB_RADIUS
TRIMPOT_RADIUS = RACK_TRIMPOT_RADIUS
PORT_RADIUS = RACK_PORT_RADIUS
CONTROL_STROKE_WIDTH = 0.30
PORT_STROKE_WIDTH = 0.30

MINIMUM_EDGE_CLEARANCE_MM = 4.0
MINIMUM_LABEL_CLEARANCE_MM = 0.25

# Header and canonical outlined logo placement follow VortexV2.
TITLE_X = 6.0
TITLE_Y = 7.0
TITLE_FONT_SIZE = 6.6
LOGO_TARGET_X = WIDTH_MM - 18.0
LOGO_TARGET_Y = 1.8
LOGO_SCALE = 0.06

# V1 is the starting physical layout.  The 7.22 mm pair offset and the two
# outer logic positions make the 4 mm real-PJ301MPort edge clearance exact
# without changing port ordering or any row position.
CHANNEL_A_X = WIDTH_MM / 4.0
CHANNEL_B_X = WIDTH_MM * 3.0 / 4.0
PAIR_OFFSET = 7.22
LOGIC_X = (8.02, 23.0, 38.0, 52.94)
Y_CHANNEL_HEADER = 16.0
Y_KNOBS = 25.0
Y_SIGNAL_POSITION = 39.0
Y_CENTER_CV = 51.0
Y_WIDTH_CV = 63.0
Y_STATE_GATES = 75.0
Y_EVENTS_UP = 89.0
Y_EVENTS_DOWN = 101.0
Y_LOGIC = 114.0

SECTION_HORIZONTAL_INSET = MINIMUM_EDGE_CLEARANCE_MM
SECTION_GAP = 4.0
SECTION_Y = 12.0
SECTION_BOTTOM = 106.0
SECTION_WIDTH = (WIDTH_MM - 2.0 * SECTION_HORIZONTAL_INSET - SECTION_GAP) / 2.0
SECTION_HEIGHT = SECTION_BOTTOM - SECTION_Y
CHANNEL_SECTION_RECTS = (
    ("channel-a-section", SECTION_HORIZONTAL_INSET, SECTION_Y,
     SECTION_WIDTH, SECTION_HEIGHT),
    ("channel-b-section", WIDTH_MM - SECTION_HORIZONTAL_INSET - SECTION_WIDTH,
     SECTION_Y, SECTION_WIDTH, SECTION_HEIGHT),
)
SECTION_STROKE_WIDTH = 0.35
SECTION_RADIUS = 1.4
LOGIC_DIVIDER_X = SECTION_HORIZONTAL_INSET
LOGIC_DIVIDER_Y = 107.0
LOGIC_DIVIDER_WIDTH = WIDTH_MM - 2.0 * SECTION_HORIZONTAL_INSET
LOGIC_DIVIDER_STROKE_WIDTH = 0.35

RAIL_WIDTH = 2.0
POSITION_RAIL_TOP = Y_KNOBS - RACK_SMALL_KNOB_RADIUS
POSITION_RAIL_BOTTOM = Y_WIDTH_CV + RACK_PORT_RADIUS
POSITION_RAIL_HEIGHT = POSITION_RAIL_BOTTOM - POSITION_RAIL_TOP
Y_POSITION_RAIL = (POSITION_RAIL_TOP + POSITION_RAIL_BOTTOM) / 2.0
POSITION_RAILS = (
    ("a-position-rail", CHANNEL_A_X - RAIL_WIDTH / 2.0, POSITION_RAIL_TOP,
     RAIL_WIDTH, POSITION_RAIL_HEIGHT),
    ("b-position-rail", CHANNEL_B_X - RAIL_WIDTH / 2.0, POSITION_RAIL_TOP,
     RAIL_WIDTH, POSITION_RAIL_HEIGHT),
)

KNOB_LABEL_OFFSET = 6.3
PORT_LABEL_OFFSET = 6.35
EVENT_LABEL_OFFSET = 6.0
CONTROL_LABEL_FONT_SIZE = 2.35
PORT_LABEL_FONT_SIZE = 2.15
EVENT_LABEL_FONT_SIZE = 2.2
CHANNEL_HEADING_FONT_SIZE = 3.0
LOGIC_LABEL_FONT_SIZE = 2.15

CHANNEL_A_ACCENT_RGB = (85, 109, 128)
CHANNEL_B_ACCENT_RGB = (183, 105, 60)


def _channel_coordinates(prefix: str, channel_x: float) -> tuple[tuple[str, float, float], ...]:
    """Return one mirrored V1 channel with the normalled inputs inward."""
    left = channel_x - PAIR_OFFSET
    right = channel_x + PAIR_OFFSET
    input_x = right if prefix == "A" else left
    output_x = left if prefix == "A" else right
    return (
        (f"{prefix}_CENTER_KNOB", left, Y_KNOBS),
        (f"{prefix}_WIDTH_KNOB", right, Y_KNOBS),
        (f"{prefix}_SIGNAL", input_x, Y_SIGNAL_POSITION),
        (f"{prefix}_POSITION", output_x, Y_SIGNAL_POSITION),
        (f"{prefix}_CENTER_CV", input_x, Y_CENTER_CV),
        (f"{prefix}_CENTER_ATTEN", output_x, Y_CENTER_CV),
        (f"{prefix}_WIDTH_CV", input_x, Y_WIDTH_CV),
        (f"{prefix}_WIDTH_ATTEN", output_x, Y_WIDTH_CV),
        (f"{prefix}_INSIDE", left, Y_STATE_GATES),
        (f"{prefix}_OUTSIDE", right, Y_STATE_GATES),
        (f"{prefix}_LOW_UP", left, Y_EVENTS_UP),
        (f"{prefix}_HIGH_UP", right, Y_EVENTS_UP),
        (f"{prefix}_LOW_DOWN", left, Y_EVENTS_DOWN),
        (f"{prefix}_HIGH_DOWN", right, Y_EVENTS_DOWN),
        (f"{prefix}_POSITION_RAIL", channel_x, Y_POSITION_RAIL),
    )


_CHANNEL_A_COORDINATES = _channel_coordinates("A", CHANNEL_A_X)
_CHANNEL_B_COORDINATES = _channel_coordinates("B", CHANNEL_B_X)
COORDINATES = {
    name: (x, y)
    for name, x, y in (
        _CHANNEL_A_COORDINATES + _CHANNEL_B_COORDINATES + (
            ("AND_OUTPUT", LOGIC_X[0], Y_LOGIC),
            ("OR_OUTPUT", LOGIC_X[1], Y_LOGIC),
            ("XOR_OUTPUT", LOGIC_X[2], Y_LOGIC),
            ("STATE_OUTPUT", LOGIC_X[3], Y_LOGIC),
        )
    )
}

CONTROL_NAMES = (
    "A_CENTER_KNOB", "A_WIDTH_KNOB", "A_CENTER_ATTEN", "A_WIDTH_ATTEN",
    "B_CENTER_KNOB", "B_WIDTH_KNOB", "B_CENTER_ATTEN", "B_WIDTH_ATTEN",
)
INPUT_NAMES = (
    "A_SIGNAL", "A_CENTER_CV", "A_WIDTH_CV",
    "B_SIGNAL", "B_CENTER_CV", "B_WIDTH_CV",
)
CHANNEL_OUTPUT_NAMES = (
    "A_INSIDE", "A_OUTSIDE", "A_POSITION", "A_LOW_UP", "A_HIGH_UP",
    "A_LOW_DOWN", "A_HIGH_DOWN", "B_INSIDE", "B_OUTSIDE", "B_POSITION",
    "B_LOW_UP", "B_HIGH_UP", "B_LOW_DOWN", "B_HIGH_DOWN",
)
LOGIC_OUTPUT_NAMES = ("AND_OUTPUT", "OR_OUTPUT", "XOR_OUTPUT", "STATE_OUTPUT")
OUTPUT_NAMES = CHANNEL_OUTPUT_NAMES + LOGIC_OUTPUT_NAMES

CONTROL_COMPONENTS = tuple((name, *COORDINATES[name]) for name in CONTROL_NAMES)
INPUT_COMPONENTS = tuple((name, *COORDINATES[name]) for name in INPUT_NAMES)
OUTPUT_COMPONENTS = tuple((name, *COORDINATES[name]) for name in OUTPUT_NAMES)
LOGIC_COMPONENTS = tuple((name, *COORDINATES[name]) for name in LOGIC_OUTPUT_NAMES)
COMPONENTS = CONTROL_COMPONENTS + INPUT_COMPONENTS + OUTPUT_COMPONENTS
COMPONENT_RADII = {
    **{name: RACK_SMALL_KNOB_RADIUS for name in CONTROL_NAMES if name.endswith("_KNOB")},
    **{name: RACK_TRIMPOT_RADIUS for name in CONTROL_NAMES if name.endswith("_ATTEN")},
    **{name: RACK_PORT_RADIUS for name in INPUT_NAMES + OUTPUT_NAMES},
}

# Labels tracked directly by the public test; repeated event labels have a
# dedicated ordering/clearance check below and are intentionally not mapped.
LABEL_COMPONENTS = {
    "CENTER": "A_CENTER_KNOB",
    "WIDTH": "A_WIDTH_KNOB",
    "IN": "A_SIGNAL",
    "POS": "A_POSITION",
    "CENTER CV": "A_CENTER_CV",
    "CENTER AMT": "A_CENTER_ATTEN",
    "WIDTH CV": "A_WIDTH_CV",
    "WIDTH AMT": "A_WIDTH_ATTEN",
    "INSIDE": "A_INSIDE",
    "OUTSIDE": "A_OUTSIDE",
    "AND": "AND_OUTPUT",
    "OR": "OR_OUTPUT",
    "XOR": "XOR_OUTPUT",
    "STATE": "STATE_OUTPUT",
}


def _fmt(value: float, digits: int = 6) -> str:
    """Format finite coordinates reproducibly for SVG and C++ output."""
    if not math.isfinite(value):
        raise ValueError(f"non-finite panel coordinate: {value!r}")
    text = f"{value:.{digits}f}".rstrip("0").rstrip(".")
    return text if text and text != "-0" else "0"


def _cpp_float(value: float) -> str:
    text = _fmt(value)
    return text if "." in text else f"{text}.0"


def _circle(x: float, y: float, radius: float, fill: str, stroke: str, stroke_width: float) -> str:
    return (
        f'  <circle cx="{_fmt(x)}" cy="{_fmt(y)}" r="{_fmt(radius)}" '
        f'fill="{fill}" stroke="{stroke}" stroke-width="{_fmt(stroke_width)}" />'
    )


def _rect(x: float, y: float, width: float, height: float, fill: str, stroke: str, *, identifier: str, radius: float = 0.0) -> str:
    return (
        f'  <rect id="{identifier}" x="{_fmt(x)}" y="{_fmt(y)}" '
        f'width="{_fmt(width)}" height="{_fmt(height)}" fill="{fill}" '
        f'stroke="{stroke}" stroke-width="{_fmt(SECTION_STROKE_WIDTH)}" '
        f'rx="{_fmt(radius)}" />'
    )


def _line(x1: float, y1: float, x2: float, y2: float, *, identifier: str, stroke: str = SECTION_BLUE_GREY, stroke_width: float = 0.30) -> str:
    return (
        f'  <line id="{identifier}" x1="{_fmt(x1)}" y1="{_fmt(y1)}" '
        f'x2="{_fmt(x2)}" y2="{_fmt(y2)}" stroke="{stroke}" '
        f'stroke-width="{_fmt(stroke_width)}" />'
    )


def _text(x: float, y: float, value: str, *, size: float, fill: str = LEGEND_CHARCOAL, anchor: str = "middle", weight: str = "400") -> str:
    # Explicit text length keeps layout reproducible across SVG consumers.
    text_length = max(size * 0.7, len(value) * size * 0.62)
    return (
        f'  <text x="{_fmt(x)}" y="{_fmt(y)}" text-anchor="{anchor}" '
        f'textLength="{_fmt(text_length)}" lengthAdjust="spacingAndGlyphs" '
        f'dominant-baseline="middle" font-family="DejaVu Sans" '
        f'font-size="{_fmt(size)}" font-weight="{weight}" fill="{fill}">'
        f"{escape(value)}</text>"
    )


def _port_guide(x: float, y: float) -> str:
    """Return the sole real-PJ301MPort guide used for every socket."""
    return _circle(x, y, PORT_RADIUS, PORT_FILL, PORT_STROKE, PORT_STROKE_WIDTH)


def _control_guide(name: str, x: float, y: float) -> str:
    radius = SMALL_KNOB_RADIUS if name.endswith("_KNOB") else TRIMPOT_RADIUS
    return _circle(x, y, radius, CONTROL_FILL, CONTROL_STROKE, CONTROL_STROKE_WIDTH)


def _logo_elements() -> list[str]:
    """Copy the verified canonical outlined logo geometry without namespaces."""
    if not LOGO_PATH.exists():
        raise RuntimeError(f"missing canonical logo asset: {LOGO_PATH}")
    root = ET.parse(LOGO_PATH).getroot()
    glyph_data = json.loads(GLYPH_DATA_PATH.read_text(encoding="utf-8"))
    expected_digest = glyph_data.get("source_font_sha256")
    actual_digest = root.attrib.get("data-source-font-sha256")
    if actual_digest != expected_digest:
        raise RuntimeError("canonical logo source-font digest mismatch")
    elements = []
    for identifier in ("wint-glyphs", "wint-underline", "oid-glyphs", "oid-underline"):
        match = next((element for element in root.iter() if element.attrib.get("id") == identifier), None)
        if match is None:
            raise RuntimeError(f"canonical logo is missing {identifier}")
        clone = ET.fromstring(ET.tostring(match, encoding="unicode"))
        for element in clone.iter():
            if "}" in element.tag:
                element.tag = element.tag.split("}", 1)[1]
            element.text = None
            element.tail = None
        elements.append(ET.tostring(clone, encoding="unicode", short_empty_elements=True))
    return elements


def _embedded_logo() -> list[str]:
    root = ET.parse(LOGO_PATH).getroot()
    view_x, view_y, _width, _height = (float(value) for value in root.attrib["viewBox"].split())
    transform = (
        f'translate({_fmt(LOGO_TARGET_X - LOGO_SCALE * view_x)} '
        f'{_fmt(LOGO_TARGET_Y - LOGO_SCALE * view_y)}) scale({_fmt(LOGO_SCALE, 4)})'
    )
    return [
        f'  <g id="wintoid-logo" transform="{transform}">',
        *(f"    {element}" for element in _logo_elements()),
        "  </g>",
    ]


def _component_label_lines(prefix: str) -> list[str]:
    coordinates = COORDINATES
    labels = (
        ("CENTER_KNOB", "CENTER", KNOB_LABEL_OFFSET, CONTROL_LABEL_FONT_SIZE),
        ("WIDTH_KNOB", "WIDTH", KNOB_LABEL_OFFSET, CONTROL_LABEL_FONT_SIZE),
        ("SIGNAL", "IN", PORT_LABEL_OFFSET, PORT_LABEL_FONT_SIZE),
        ("POSITION", "POS", PORT_LABEL_OFFSET, PORT_LABEL_FONT_SIZE),
        ("CENTER_CV", "CENTER CV", PORT_LABEL_OFFSET, PORT_LABEL_FONT_SIZE),
        ("CENTER_ATTEN", "CENTER AMT", PORT_LABEL_OFFSET, PORT_LABEL_FONT_SIZE),
        ("WIDTH_CV", "WIDTH CV", PORT_LABEL_OFFSET, PORT_LABEL_FONT_SIZE),
        ("WIDTH_ATTEN", "WIDTH AMT", PORT_LABEL_OFFSET, PORT_LABEL_FONT_SIZE),
        ("INSIDE", "INSIDE", PORT_LABEL_OFFSET, PORT_LABEL_FONT_SIZE),
        ("OUTSIDE", "OUTSIDE", PORT_LABEL_OFFSET, PORT_LABEL_FONT_SIZE),
    )
    lines = []
    for suffix, label, offset, size in labels:
        x, y = coordinates[f"{prefix}_{suffix}"]
        lines.append(_text(x, y - offset, label, size=size))
    for suffix, label, y in (
        ("LOW_UP", "LOW", Y_EVENTS_UP - EVENT_LABEL_OFFSET),
        ("HIGH_UP", "HIGH", Y_EVENTS_UP - EVENT_LABEL_OFFSET),
        ("LOW_DOWN", "LOW", Y_EVENTS_DOWN - EVENT_LABEL_OFFSET),
        ("HIGH_DOWN", "HIGH", Y_EVENTS_DOWN - EVENT_LABEL_OFFSET),
    ):
        x, _port_y = coordinates[f"{prefix}_{suffix}"]
        lines.append(_text(x, y, label, size=EVENT_LABEL_FONT_SIZE, fill=FUNCTION_ORANGE, weight="700"))
    arrow_x = CHANNEL_A_X if prefix == "A" else CHANNEL_B_X
    lines.append(_text(arrow_x, Y_EVENTS_UP - EVENT_LABEL_OFFSET, "↑", size=EVENT_LABEL_FONT_SIZE, fill=FUNCTION_ORANGE, weight="700"))
    lines.append(_text(arrow_x, Y_EVENTS_DOWN - EVENT_LABEL_OFFSET, "↓", size=EVENT_LABEL_FONT_SIZE, fill=FUNCTION_ORANGE, weight="700"))
    return lines


def generate_svg() -> str:
    """Return the deterministic static SEM BrinkV2 faceplate."""
    lines = [
        '<svg xmlns="http://www.w3.org/2000/svg" '
        f'width="{_fmt(WIDTH_MM, 2)}mm" height="{_fmt(HEIGHT_MM, 1)}mm" '
        f'viewBox="0 0 {_fmt(WIDTH_MM, 2)} {_fmt(HEIGHT_MM, 1)}">',
        f'  <rect x="0" y="0" width="{_fmt(WIDTH_MM, 2)}" height="{_fmt(HEIGHT_MM, 1)}" fill="{PANEL_IVORY}" stroke="{PANEL_IVORY}" stroke-width="0" />',
    ]
    lines.extend(_embedded_logo())
    lines.append(_text(TITLE_X, TITLE_Y, "Brink V2", size=TITLE_FONT_SIZE, anchor="start", weight="700"))
    for (identifier, x, y, width, height), fill in zip(CHANNEL_SECTION_RECTS, (SECTION_FILL, SECTION_FILL_ALT)):
        lines.append(_rect(x, y, width, height, fill, SECTION_BLUE_GREY, identifier=identifier, radius=SECTION_RADIUS))
    lines.append(_line(LOGIC_DIVIDER_X, LOGIC_DIVIDER_Y, LOGIC_DIVIDER_X + LOGIC_DIVIDER_WIDTH, LOGIC_DIVIDER_Y, identifier="logic-divider", stroke_width=LOGIC_DIVIDER_STROKE_WIDTH))
    for identifier, x, y, width, height in POSITION_RAILS:
        lines.append(_rect(x, y, width, height, PANEL_IVORY, SECTION_BLUE_GREY, identifier=identifier, radius=0.8))
    lines.append(_text(CHANNEL_A_X, Y_CHANNEL_HEADER, "CHANNEL A", size=CHANNEL_HEADING_FONT_SIZE, fill=SECTION_BLUE_GREY, weight="700"))
    lines.append(_text(CHANNEL_B_X, Y_CHANNEL_HEADER, "CHANNEL B", size=CHANNEL_HEADING_FONT_SIZE, fill=SECTION_BLUE_GREY, weight="700"))
    for y in (Y_SIGNAL_POSITION, Y_CENTER_CV, Y_WIDTH_CV):
        a_input_x = COORDINATES["A_SIGNAL" if y == Y_SIGNAL_POSITION else "A_CENTER_CV" if y == Y_CENTER_CV else "A_WIDTH_CV"][0]
        b_input_x = COORDINATES["B_SIGNAL" if y == Y_SIGNAL_POSITION else "B_CENTER_CV" if y == Y_CENTER_CV else "B_WIDTH_CV"][0]
        lines.append(_line(a_input_x + 2.0, y, b_input_x - 2.0, y, identifier=f"normalisation-{_fmt(y)}", stroke=FUNCTION_ORANGE))
        tip_x = WIDTH_MM / 2.0 + 1.2
        lines.append(_line(tip_x - 1.0, y - 0.8, tip_x, y, identifier=f"normalisation-arrow-top-{_fmt(y)}", stroke=FUNCTION_ORANGE))
        lines.append(_line(tip_x - 1.0, y + 0.8, tip_x, y, identifier=f"normalisation-arrow-bottom-{_fmt(y)}", stroke=FUNCTION_ORANGE))
    lines.extend(_component_label_lines("A"))
    lines.extend(_component_label_lines("B"))
    for name, x, y in CONTROL_COMPONENTS:
        lines.append(_control_guide(name, x, y))
    for _name, x, y in INPUT_COMPONENTS + OUTPUT_COMPONENTS:
        lines.append(_port_guide(x, y))
    for label, name in (("AND", "AND_OUTPUT"), ("OR", "OR_OUTPUT"), ("XOR", "XOR_OUTPUT"), ("STATE", "STATE_OUTPUT")):
        x, y = COORDINATES[name]
        lines.append(_text(x, y - PORT_LABEL_OFFSET, label, size=LOGIC_LABEL_FONT_SIZE, fill=SECTION_BLUE_GREY, weight="700"))
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def _header_float(name: str, value: float) -> str:
    return f"constexpr float {name} = {_cpp_float(value)}f;"


def generate_coords_header() -> str:
    """Return the deterministic C++11 millimetre layout contract."""
    lines = [
        "#pragma once",
        "// Auto-generated by scripts/generate_panel_brink_v2.py; do not edit manually.",
        "// All panel coordinates and dimensions are millimetres for use with mm2px().",
        "",
        "namespace brink_v2_layout {",
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
        "// Alternating SEM channel fields and shared logic divider.",
    ]
    for identifier, x, y, width, height in CHANNEL_SECTION_RECTS:
        prefix = identifier.replace("-", "_").upper()
        lines.extend((_header_float(f"{prefix}_X", x), _header_float(f"{prefix}_Y", y), _header_float(f"{prefix}_WIDTH", width), _header_float(f"{prefix}_HEIGHT", height)))
    lines.extend((
        _header_float("LOGIC_DIVIDER_X", LOGIC_DIVIDER_X),
        _header_float("LOGIC_DIVIDER_Y", LOGIC_DIVIDER_Y),
        _header_float("LOGIC_DIVIDER_WIDTH", LOGIC_DIVIDER_WIDTH),
        _header_float("KNOB_LABEL_OFFSET", KNOB_LABEL_OFFSET),
        _header_float("PORT_LABEL_OFFSET", PORT_LABEL_OFFSET),
        _header_float("EVENT_LABEL_OFFSET", EVENT_LABEL_OFFSET),
        _header_float("CONTROL_LABEL_FONT_SIZE", CONTROL_LABEL_FONT_SIZE),
        _header_float("PORT_LABEL_FONT_SIZE", PORT_LABEL_FONT_SIZE),
        _header_float("EVENT_LABEL_FONT_SIZE", EVENT_LABEL_FONT_SIZE),
        _header_float("CHANNEL_HEADING_FONT_SIZE", CHANNEL_HEADING_FONT_SIZE),
        _header_float("LOGIC_LABEL_FONT_SIZE", LOGIC_LABEL_FONT_SIZE),
        "",
        "// Read-only window rail bounds.",
        _header_float("RAIL_WIDTH", RAIL_WIDTH),
        _header_float("POSITION_RAIL_TOP", POSITION_RAIL_TOP),
        _header_float("POSITION_RAIL_BOTTOM", POSITION_RAIL_BOTTOM),
        _header_float("POSITION_RAIL_HEIGHT", POSITION_RAIL_HEIGHT),
        _header_float("Y_POSITION_RAIL", Y_POSITION_RAIL),
        "",
        "// V2 light and marker accents.",
        f"constexpr int SECTION_BLUE_GREY_R = {CHANNEL_A_ACCENT_RGB[0]};",
        f"constexpr int SECTION_BLUE_GREY_G = {CHANNEL_A_ACCENT_RGB[1]};",
        f"constexpr int SECTION_BLUE_GREY_B = {CHANNEL_A_ACCENT_RGB[2]};",
        f"constexpr int FUNCTION_ORANGE_R = {CHANNEL_B_ACCENT_RGB[0]};",
        f"constexpr int FUNCTION_ORANGE_G = {CHANNEL_B_ACCENT_RGB[1]};",
        f"constexpr int FUNCTION_ORANGE_B = {CHANNEL_B_ACCENT_RGB[2]};",
        f"constexpr int CHANNEL_A_ACCENT_R = {CHANNEL_A_ACCENT_RGB[0]};",
        f"constexpr int CHANNEL_A_ACCENT_G = {CHANNEL_A_ACCENT_RGB[1]};",
        f"constexpr int CHANNEL_A_ACCENT_B = {CHANNEL_A_ACCENT_RGB[2]};",
        f"constexpr int CHANNEL_B_ACCENT_R = {CHANNEL_B_ACCENT_RGB[0]};",
        f"constexpr int CHANNEL_B_ACCENT_G = {CHANNEL_B_ACCENT_RGB[1]};",
        f"constexpr int CHANNEL_B_ACCENT_B = {CHANNEL_B_ACCENT_RGB[2]};",
        "",
    ))
    coordinate_order = tuple(name for name, _x, _y in _CHANNEL_A_COORDINATES + _CHANNEL_B_COORDINATES) + LOGIC_OUTPUT_NAMES
    for name in coordinate_order:
        x, y = COORDINATES[name]
        lines.append(_header_float(f"{name}_X", x))
        lines.append(_header_float(f"{name}_Y", y))
    lines.extend(("", "} // namespace brink_v2_layout", ""))
    return "\n".join(lines)


# Keep the public name used by the other panel generators available.
generate_header = generate_coords_header


def main() -> None:
    SVG_PATH.parent.mkdir(parents=True, exist_ok=True)
    HEADER_PATH.parent.mkdir(parents=True, exist_ok=True)
    SVG_PATH.write_text(generate_svg(), encoding="utf-8")
    HEADER_PATH.write_text(generate_coords_header(), encoding="utf-8")
    print(f"Wrote {SVG_PATH}")
    print(f"Wrote {HEADER_PATH}")


if __name__ == "__main__":
    main()
