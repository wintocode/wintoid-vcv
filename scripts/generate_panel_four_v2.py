#!/usr/bin/env python3
"""Generate the FourV2 panel SVG and C++ coordinate header.

Run from the project root or from any other working directory::

    python3 scripts/generate_panel_four_v2.py

Outputs:
    res/FourV2.svg       -- deterministic ivory structural artwork
    res/FourV2FrequencyMode_*.svg and res/FourV2FoldType_*.svg
                         -- labeled ParamWidget switch frames
    src/FourV2/layout.h  -- generated millimetre coordinates for Rack widgets

All geometry in this file is millimetres.  The SVG is intentionally a quiet
structural guide: Rack supplies the live controls and displays, while the
static labels, framed hierarchy, and canonical outlined wintoid mark remain
visible in the checked-in panel asset.
"""

from __future__ import annotations

from html import escape
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
LOGO_PATH = ROOT / "res" / "WintoidLogo.svg"
SVG_PATH = ROOT / "res" / "FourV2.svg"
HEADER_PATH = ROOT / "src" / "FourV2" / "layout.h"
GLYPH_DATA_PATH = ROOT / "scripts" / "assets" / "wintoid_logo_glyphs.json"
STATE_SWITCH_ASSETS = (
    ("FourV2FrequencyMode_Ratio.svg", "RATIO"),
    ("FourV2FrequencyMode_Fixed.svg", "FIXED"),
    ("FourV2FoldType_Symmetric.svg", "SYM"),
    ("FourV2FoldType_Asymmetric.svg", "ASYM"),
    ("FourV2FoldType_SoftClip.svg", "SOFT"),
)
STATE_SWITCH_WIDTH = 10.0
STATE_SWITCH_HEIGHT = 5.0
STATE_SWITCH_CELL_SIZE = 0.30
STATE_SWITCH_CELL_GAP = 0.03
STATE_SWITCH_LETTER_GAP = 0.10
STATE_SWITCH_GLYPHS = {
    "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "F": ("11111", "10000", "10000", "11110", "10000", "10000", "10000"),
    "I": ("11111", "00100", "00100", "00100", "00100", "00100", "11111"),
    "M": ("10001", "11011", "10101", "10001", "10001", "10001", "10001"),
    "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
    "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    "X": ("10001", "10001", "01010", "00100", "01010", "10001", "10001"),
    "Y": ("10001", "10001", "01010", "00100", "00100", "00100", "00100"),
}


# Panel dimensions and SEM-instrument palette.
HP = 32
WIDTH_MM = HP * 5.08
HEIGHT_MM = 128.5
PANEL_IVORY = "#ece8d9"
LEGEND_CHARCOAL = "#242522"
SECTION_BLUE_GREY = "#556d80"
FUNCTION_ORANGE = "#b7693c"
LOGO_BLUE = "#155f91"
LOGO_ORANGE = "#ed5b22"
DISPLAY_CHARCOAL = "#242522"
DISPLAY_TEXT = PANEL_IVORY
SECTION_FILL = "#e3e0d1"
SECTION_FILL_ALT = "#e7e3d4"
CONTROL_FILL = "#242522"
CONTROL_STROKE = "#556d80"
CONTROL_ACCENT = "#b7693c"
ROUTING_MODULATION = "#b7693c"
ROUTING_CARRIER = "#9c7a35"


# Real Rack envelope measurements used by the geometry tests and by the
# generated layout.  These are deliberately larger than the quiet guide
# circles drawn by the SVG.
PIXELS_PER_MM = 15.0 / 5.08
RACK_SMALL_KNOB_RADIUS = 22.67581 / (2.0 * PIXELS_PER_MM)
RACK_PORT_RADIUS = 23.7 / (2.0 * PIXELS_PER_MM)
SMALL_KNOB_RADIUS = RACK_SMALL_KNOB_RADIUS
PORT_RADIUS = RACK_PORT_RADIUS
SWITCH_RADIUS = STATE_SWITCH_WIDTH / 2.0
LIGHT_RADIUS = 1.2

OUTPUT_RING_WIDTH = 0.45
OUTPUT_STROKE_WIDTH = 0.55
OUTPUT_BACKPLATE_RADIUS = RACK_PORT_RADIUS + OUTPUT_RING_WIDTH
OUTPUT_BACKPLATE_FILL = "#39445f"
OUTPUT_BACKPLATE_STROKE = "#dfe7f3"

MINIMUM_EDGE_CLEARANCE_MM = 4.0
MINIMUM_LABEL_CLEARANCE_MM = 0.25


# Header and logo reservations.
TITLE_X = 6.0
TITLE_Y = 7.0
TITLE_FONT_SIZE = 6.6
LOGO_TARGET_X = 125.0
LOGO_TARGET_Y = 1.8
LOGO_SCALE = 0.06


# Global routing/control band.
ROUTING_SECTION = (4.0, 10.5, WIDTH_MM - 8.0, 25.5)
ROUTING_DISPLAY = (17.5, 13.0, 48.0, 21.0)
ROUTING_DISPLAY_INSET = 1.3
GLOBAL_CONTROLS = {
    "algorithm_knob": (8.5, 23.5),
    "tune_knob": (70.5, 17.5),
    "pm_depth_knob": (82.5, 17.5),
    "master_knob": (94.5, 17.5),
    "pm_depth_cv_jack": (119.0, 17.5),
    "pm_depth_cv_atten": (131.0, 17.5),
    "external_pm_jack": (106.5, 29.0),
    "external_pm_atten": (118.5, 29.0),
    "voct_jack": (8.5, 14.5),
    "main_output": (153.0, 17.5),
    "over_light": (145.0, 29.0),
}
ALGORITHM_LABEL_Y = 29.2
GLOBAL_LABEL_Y = 12.8
EXTERNAL_PM_LABEL_Y = 35.1
MAIN_OUTPUT_LABEL_Y = 24.5


# Four equal operator fields.  Frequency controls occupy two compact rows;
# the four sound controls then share rows with their CV input and attenuverter.
OPERATOR_SECTION_TOP = 37.5
OPERATOR_SECTION_HEIGHT = 87.0
OPERATOR_SECTION_LEFT = 4.0
OPERATOR_SECTION_GAP = 1.5
OPERATOR_SECTION_WIDTH = (
    WIDTH_MM - 2.0 * OPERATOR_SECTION_LEFT - 3.0 * OPERATOR_SECTION_GAP
) / 4.0
OPERATOR_SECTION_RECTS = tuple(
    (
        OPERATOR_SECTION_LEFT
        + index * (OPERATOR_SECTION_WIDTH + OPERATOR_SECTION_GAP),
        OPERATOR_SECTION_TOP,
        OPERATOR_SECTION_WIDTH,
        OPERATOR_SECTION_HEIGHT,
    )
    for index in range(4)
)
OPERATOR_CENTRES_X = tuple(
    rect[0] + rect[2] / 2.0 for rect in OPERATOR_SECTION_RECTS
)

OPERATOR_ROW_YS = {
    "coarse_mode": 52.0,
    "fine_fold_type": 64.5,
    "output": 78.0,
    "warp": 91.5,
    "fold": 105.0,
    "feedback": 118.5,
}
OPERATOR_LABEL_YS = {
    "coarse_mode": 57.85,
    "fine_fold_type": 70.35,
    "output": 73.0,
    "warp": 86.5,
    "fold": 100.0,
    "feedback": 113.5,
}
OPERATOR_HEADING_Y = 40.8
OPERATOR_LABEL_SIZE = 1.60
OPERATOR_MODE_LABEL_SIZE = 1.45
OPERATOR_X_OFFSETS = {
    "coarse": -8.75,
    "freq_mode": 8.75,
    "fine": -8.75,
    "fold_type": 8.75,
    "output": -10.5,
    "warp": -10.5,
    "fold": -10.5,
    "feedback": -10.5,
}
OPERATOR_PARAMETER_X_OFFSETS = {
    "knob": -10.5,
    "cv_input": 0.0,
    "cv_atten": 10.5,
}
OPERATOR_PARAMETER_HEADER_Y = 73.0
OPERATOR_PARAMETER_HEADER_SIZE = 1.35
FREQUENCY_DISPLAY_HEIGHT = 4.0
FREQUENCY_DISPLAY_WIDTH = OPERATOR_SECTION_WIDTH - 8.0
FREQUENCY_DISPLAY_RECTS = tuple(
    (
        rect[0] + 4.0,
        41.5,
        FREQUENCY_DISPLAY_WIDTH,
        FREQUENCY_DISPLAY_HEIGHT,
    )
    for rect in OPERATOR_SECTION_RECTS
)


# CV controls remain a logical four-row matrix for the generated coordinates,
# but are now integrated into each operator field rather than framed below it.
PATCHBAY_COLUMN_XS = OPERATOR_CENTRES_X
PATCHBAY_ROWS = ("Output", "Warp", "Fold", "Feedback")
PATCHBAY_ROW_YS = tuple(OPERATOR_ROW_YS[row.lower()] for row in PATCHBAY_ROWS)
PATCHBAY_WIDGET_OFFSET = OPERATOR_PARAMETER_X_OFFSETS["cv_atten"]
PATCHBAY_SECTION = (
    OPERATOR_SECTION_LEFT,
    OPERATOR_SECTION_TOP,
    WIDTH_MM - 2.0 * OPERATOR_SECTION_LEFT,
    OPERATOR_SECTION_HEIGHT,
)
PATCHBAY_SECTION_TOP = OPERATOR_SECTION_TOP
PATCHBAY_SECTION_HEIGHT = OPERATOR_SECTION_HEIGHT
# Kept as an aligned logical alias for callers that used the old generated
# geometry contract; no separate patchbay rectangles are emitted anymore.
PATCHBAY_SECTION_RECTS = OPERATOR_SECTION_RECTS
PATCHBAY_CELLS = {
    row: tuple((x, y) for x in PATCHBAY_COLUMN_XS)
    for row, y in zip(PATCHBAY_ROWS, PATCHBAY_ROW_YS)
}


# Shared I/O is integrated into the global band so the operator CV bays can
# remain complete, repeated fields down to their bottom borders.
SHARED_IO_SECTION = ROUTING_SECTION
VOCT_LABEL_X = 13.0
MAIN_OUTPUT_LABEL_X = GLOBAL_CONTROLS["main_output"][0]
SHARED_IO_LABEL_Y = 14.5
OVER_LABEL_Y = 35.1
SHARED_IO = {
    "voct_jack": GLOBAL_CONTROLS["voct_jack"],
    "main_output": GLOBAL_CONTROLS["main_output"],
    "over_light": GLOBAL_CONTROLS["over_light"],
}


def _fmt(value: float, digits: int = 3) -> str:
    """Format a finite coordinate without platform-dependent noise."""
    if not math.isfinite(value):
        raise ValueError(f"non-finite panel coordinate: {value!r}")
    result = f"{value:.{digits}f}".rstrip("0").rstrip(".")
    return result if result and result != "-0" else "0"


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
    stroke_width: float = 0.30,
) -> str:
    id_attribute = f' id="{identifier}"' if identifier else ""
    radius_attribute = f' rx="{_fmt(radius)}"' if radius is not None else ""
    return (
        f'  <rect{id_attribute} x="{_fmt(x)}" y="{_fmt(y)}" '
        f'width="{_fmt(width)}" height="{_fmt(height)}" '
        f'fill="{fill}" stroke="{stroke}" '
        f'stroke-width="{_fmt(stroke_width)}"{radius_attribute} />'
    )


def _circle(
    x: float,
    y: float,
    radius: float,
    fill: str,
    stroke: str,
    stroke_width: float = 0.30,
) -> str:
    return (
        f'  <circle cx="{_fmt(x)}" cy="{_fmt(y)}" r="{_fmt(radius)}" '
        f'fill="{fill}" stroke="{stroke}" '
        f'stroke-width="{_fmt(stroke_width)}" />'
    )


def _line(
    x1: float,
    y1: float,
    x2: float,
    y2: float,
    stroke: str,
    width: float = 0.30,
) -> str:
    return (
        f'  <line x1="{_fmt(x1)}" y1="{_fmt(y1)}" '
        f'x2="{_fmt(x2)}" y2="{_fmt(y2)}" stroke="{stroke}" '
        f'stroke-width="{_fmt(width)}" />'
    )


def _path(data: str, stroke: str, width: float = 0.30, fill: str = "none") -> str:
    return (
        f'  <path d="{data}" fill="{fill}" stroke="{stroke}" '
        f'stroke-width="{_fmt(width)}" stroke-linecap="round" '
        'stroke-linejoin="round" />'
    )


def _state_switch_label_path(label: str) -> str:
    """Return a NanoSVG-compatible block glyph path for one label."""
    glyph_width = 5.0 * STATE_SWITCH_CELL_SIZE + 4.0 * STATE_SWITCH_CELL_GAP
    label_width = (
        len(label) * glyph_width
        + max(0, len(label) - 1) * STATE_SWITCH_LETTER_GAP
    )
    glyph_height = 7.0 * STATE_SWITCH_CELL_SIZE + 6.0 * STATE_SWITCH_CELL_GAP
    start_x = (STATE_SWITCH_WIDTH - label_width) / 2.0
    start_y = (STATE_SWITCH_HEIGHT - glyph_height) / 2.0
    commands = []
    for character in label:
        glyph = STATE_SWITCH_GLYPHS[character]
        for row, line in enumerate(glyph):
            for column, filled in enumerate(line):
                if filled != "1":
                    continue
                x = start_x + column * (STATE_SWITCH_CELL_SIZE + STATE_SWITCH_CELL_GAP)
                y = start_y + row * (STATE_SWITCH_CELL_SIZE + STATE_SWITCH_CELL_GAP)
                x2 = x + STATE_SWITCH_CELL_SIZE
                y2 = y + STATE_SWITCH_CELL_SIZE
                commands.append(
                    f"M {_fmt(x)} {_fmt(y)} L {_fmt(x2)} {_fmt(y)} "
                    f"L {_fmt(x2)} {_fmt(y2)} L {_fmt(x)} {_fmt(y2)} Z"
                )
        start_x += glyph_width + STATE_SWITCH_LETTER_GAP
    return (
        f'  <path data-label="{escape(label)}" '
        f'd="{" ".join(commands)}" fill="{DISPLAY_TEXT}" />'
    )


def generate_state_switch_svg(label: str) -> str:
    """Return one labeled, asset-backed switch frame."""
    labels = {choice for _filename, choice in STATE_SWITCH_ASSETS}
    if label not in labels:
        raise ValueError(f"unknown state switch label: {label!r}")
    if any(character not in STATE_SWITCH_GLYPHS for character in label):
        raise ValueError(f"no vector glyphs for state switch label: {label!r}")

    inset = 0.25
    lines = [
        '<svg xmlns="http://www.w3.org/2000/svg" '
        f'width="{_fmt(STATE_SWITCH_WIDTH, 1)}mm" '
        f'height="{_fmt(STATE_SWITCH_HEIGHT, 1)}mm" '
        f'viewBox="0 0 {_fmt(STATE_SWITCH_WIDTH, 1)} '
        f'{_fmt(STATE_SWITCH_HEIGHT, 1)}">',
        _rect(
            inset,
            inset,
            STATE_SWITCH_WIDTH - inset * 2.0,
            STATE_SWITCH_HEIGHT - inset * 2.0,
            DISPLAY_CHARCOAL,
            CONTROL_ACCENT,
            radius=0.9,
            stroke_width=0.50,
        ),
        _state_switch_label_path(label),
        "</svg>",
    ]
    return "\n".join(lines) + "\n"


def _text(
    x: float,
    y: float,
    value: str,
    *,
    size: float = 2.4,
    fill: str = LEGEND_CHARCOAL,
    anchor: str = "middle",
    weight: str = "400",
    letter_spacing: float | None = None,
    text_length: float | None = None,
) -> str:
    spacing = (
        f' letter-spacing="{_fmt(letter_spacing)}"'
        if letter_spacing is not None
        else ""
    )
    rendered_length = (
        f' textLength="{_fmt(text_length)}" lengthAdjust="spacingAndGlyphs"'
        if text_length is not None
        else ""
    )
    return (
        f'  <text x="{_fmt(x)}" y="{_fmt(y)}" text-anchor="{anchor}" '
        f'font-family="DejaVu Sans" font-size="{_fmt(size)}" '
        f'font-weight="{weight}" fill="{fill}"{spacing}{rendered_length}>'
        f"{escape(value)}"
        "</text>"
    )


def _component_radius(name: str) -> float:
    if name == "main_output":
        return OUTPUT_BACKPLATE_RADIUS
    if name == "over_light":
        return LIGHT_RADIUS
    if name.endswith("_jack") or name.endswith("_input"):
        return PORT_RADIUS
    if name.endswith("_freq_mode") or name.endswith("_fold_type"):
        return SWITCH_RADIUS
    return SMALL_KNOB_RADIUS


def _component_kind(name: str) -> str:
    if name == "main_output":
        return "output"
    if name == "over_light":
        return "light"
    if name.endswith("_jack") or name.endswith("_input"):
        return "input"
    if name.endswith("_freq_mode") or name.endswith("_fold_type"):
        return "switch"
    return "knob"


def _append_component(lines: list[str], name: str, x: float, y: float) -> None:
    kind = _component_kind(name)
    if kind == "output":
        lines.append(
            _circle(
                x,
                y,
                OUTPUT_BACKPLATE_RADIUS,
                OUTPUT_BACKPLATE_FILL,
                OUTPUT_BACKPLATE_STROKE,
                OUTPUT_STROKE_WIDTH,
            )
        )
    elif kind == "input":
        lines.append(_circle(x, y, PORT_RADIUS, CONTROL_FILL, CONTROL_STROKE))
    elif kind == "switch":
        lines.append(
            _rect(
                x - STATE_SWITCH_WIDTH / 2.0,
                y - STATE_SWITCH_HEIGHT / 2.0,
                STATE_SWITCH_WIDTH,
                STATE_SWITCH_HEIGHT,
                CONTROL_FILL,
                CONTROL_ACCENT,
                radius=0.9,
                stroke_width=0.50,
            )
        )
    elif kind == "light":
        lines.append(_circle(x, y, LIGHT_RADIUS, "#a93636", LEGEND_CHARCOAL, 0.25))
    else:
        stroke = CONTROL_ACCENT if "atten" in name else CONTROL_STROKE
        lines.append(_circle(x, y, SMALL_KNOB_RADIUS, CONTROL_FILL, stroke))


def _operator_component_coordinates() -> tuple[tuple[str, float, float], ...]:
    components = []
    for index, centre_x in enumerate(OPERATOR_CENTRES_X, start=1):
        components.extend(
            (
                (f"op{index}_coarse", centre_x + OPERATOR_X_OFFSETS["coarse"], OPERATOR_ROW_YS["coarse_mode"]),
                (f"op{index}_freq_mode", centre_x + OPERATOR_X_OFFSETS["freq_mode"], OPERATOR_ROW_YS["coarse_mode"]),
                (f"op{index}_fine", centre_x + OPERATOR_X_OFFSETS["fine"], OPERATOR_ROW_YS["fine_fold_type"]),
                (f"op{index}_fold_type", centre_x + OPERATOR_X_OFFSETS["fold_type"], OPERATOR_ROW_YS["fine_fold_type"]),
                (f"op{index}_output", centre_x + OPERATOR_X_OFFSETS["output"], OPERATOR_ROW_YS["output"]),
                (f"op{index}_warp", centre_x + OPERATOR_X_OFFSETS["warp"], OPERATOR_ROW_YS["warp"]),
                (f"op{index}_fold", centre_x + OPERATOR_X_OFFSETS["fold"], OPERATOR_ROW_YS["fold"]),
                (f"op{index}_feedback", centre_x + OPERATOR_X_OFFSETS["feedback"], OPERATOR_ROW_YS["feedback"]),
            )
        )
    return tuple(components)


def _patchbay_component_coordinates() -> tuple[tuple[str, float, float], ...]:
    components = []
    for row in PATCHBAY_ROWS:
        slug = row.lower()
        for index, (centre_x, y) in enumerate(PATCHBAY_CELLS[row], start=1):
            components.extend(
                (
                    (
                        f"op{index}_{slug}_cv_input",
                        centre_x + OPERATOR_PARAMETER_X_OFFSETS["cv_input"],
                        y,
                    ),
                    (
                        f"op{index}_{slug}_cv_atten",
                        centre_x + OPERATOR_PARAMETER_X_OFFSETS["cv_atten"],
                        y,
                    ),
                )
            )
    return tuple(components)


GLOBAL_COMPONENTS = tuple(
    (name, coordinate[0], coordinate[1])
    for name, coordinate in GLOBAL_CONTROLS.items()
)
OPERATOR_COMPONENTS = _operator_component_coordinates()
PATCHBAY_COMPONENTS = _patchbay_component_coordinates()
# Shared I/O is already represented in GLOBAL_CONTROLS so its names and
# coordinates are emitted once in COMPONENTS.
SHARED_IO_COMPONENTS = ()

# This is the public geometry contract consumed by the panel tests and by the
# later Rack module implementation.  The main output remains the first output
# tuple, matching the established V1/Vortex style tests.
COMPONENTS = GLOBAL_COMPONENTS + OPERATOR_COMPONENTS + PATCHBAY_COMPONENTS + SHARED_IO_COMPONENTS
COMPONENT_RADII = {name: _component_radius(name) for name, _x, _y in COMPONENTS}
OUTPUT_COMPONENTS = (("main_output", *GLOBAL_CONTROLS["main_output"]),)
INPUT_COMPONENTS = tuple(
    component
    for component in COMPONENTS
    if _component_kind(component[0]) == "input"
)
OUTPUT_COMPONENT_NAMES = frozenset({"main_output"})


DISPLAY_RECTS = {
    "ROUTING": ROUTING_DISPLAY,
    **{
        f"OP{index}_FREQUENCY": rectangle
        for index, rectangle in enumerate(FREQUENCY_DISPLAY_RECTS, start=1)
    },
}
SECTION_RECTS = {
    "ROUTING": ROUTING_SECTION,
    **{
        f"OP{index}": rectangle
        for index, rectangle in enumerate(OPERATOR_SECTION_RECTS, start=1)
    },
    "SHARED_IO": SHARED_IO_SECTION,
}


# Label clearance is deliberately a public contract.  The values are the
# reserved nearest-edge margins after accounting for the real Rack envelope,
# not the much smaller structural circles in the SVG.
LABEL_CLEARANCES = {
    "title": {"clearance_mm": 0.50},
    "algorithm": {"clearance_mm": 0.35},
    "tune": {"clearance_mm": 0.35},
    "pm_depth": {"clearance_mm": 0.35},
    "master": {"clearance_mm": 0.35},
    "voct": {"clearance_mm": 0.35},
    "pm_depth_cv": {"clearance_mm": 0.35},
    "external_pm": {"clearance_mm": 0.35},
    "main_output": {"clearance_mm": 0.35},
    "over": {"clearance_mm": 0.35},
    **{
        f"op{index}_heading": {"clearance_mm": 0.35}
        for index in range(1, 5)
    },
    **{
        f"op{index}_{label.lower()}": {"clearance_mm": 0.30}
        for index in range(1, 5)
        for label in (
            "coarse", "mode", "fine", "fold_type", "output",
            "warp", "fold", "feedback", "cv", "atten",
        )
    },
    "shared_io": {"clearance_mm": 0.30},
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
            (element for element in root.iter() if element.attrib.get("id") == identifier),
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
    # The canonical logo viewBox is intentionally read from the generated
    # asset instead of duplicated here.  The target position is the visible
    # path bound, so its built-in padding remains intact.
    root = ET.parse(LOGO_PATH).getroot()
    view_box = [float(value) for value in root.attrib["viewBox"].split()]
    view_x, view_y, _view_width, _view_height = view_box
    translate_x = LOGO_TARGET_X - LOGO_SCALE * view_x
    translate_y = LOGO_TARGET_Y - LOGO_SCALE * view_y
    transform = (
        f'translate({_fmt(translate_x)} {_fmt(translate_y)}) '
        f'scale({_fmt(LOGO_SCALE, 4)})'
    )
    elements = _logo_elements()
    lines = [f'  <g id="wintoid-logo" transform="{transform}">']
    lines.extend(f"    {element}" for element in elements)
    lines.append("  </g>")
    return lines


def _append_routing_artwork(lines: list[str]) -> None:
    x, y, width, height = ROUTING_DISPLAY
    inset = ROUTING_DISPLAY_INSET
    inner_x = x + inset
    inner_y = y + inset
    inner_width = width - 2.0 * inset
    inner_height = height - 2.0 * inset
    lines.append(
        _rect(
            x,
            y,
            width,
            height,
            DISPLAY_CHARCOAL,
            SECTION_BLUE_GREY,
            identifier="routing-display",
            radius=1.0,
            stroke_width=0.35,
        )
    )
    lines.append('  <g id="routing-display-art">')
    node_y = inner_y + inner_height * 0.42
    node_xs = tuple(inner_x + inner_width * fraction for fraction in (0.10, 0.37, 0.64, 0.90))
    # A restrained serial graph gives the static panel a useful visual cue;
    # the live Rack display owns the selected algorithm and redraws this box.
    for source, destination in zip(node_xs, node_xs[1:]):
        lines.append(
            _path(
                f"M {_fmt(source + 2.0)} {_fmt(node_y)} "
                f"C {_fmt(source + 7.0)} {_fmt(node_y - 4.0)}, "
                f"{_fmt(destination - 7.0)} {_fmt(node_y + 4.0)}, "
                f"{_fmt(destination - 2.0)} {_fmt(node_y)}",
                ROUTING_MODULATION,
                0.45,
            )
        )
    rail_y = inner_y + inner_height * 0.76
    lines.append(_line(node_xs[0], rail_y, node_xs[-1], rail_y, ROUTING_CARRIER, 0.45))
    for index, node_x in enumerate(node_xs, start=1):
        lines.append(
            f'    <circle cx="{_fmt(node_x)}" cy="{_fmt(node_y)}" r="1.65" '
            f'fill="{DISPLAY_CHARCOAL}" stroke="{PANEL_IVORY}" stroke-width="0.35" />'
        )
        lines.append(
            f'    <text x="{_fmt(node_x)}" y="{_fmt(node_y + 0.8)}" '
            'text-anchor="middle" font-family="DejaVu Sans" font-size="1.8" '
            f'fill="{PANEL_IVORY}">{index}</text>'
        )
    lines.append("  </g>")


def generate_svg() -> str:
    """Return the deterministic FourV2 panel SVG."""
    lines = [
        '<svg xmlns="http://www.w3.org/2000/svg" '
        f'width="{_fmt(WIDTH_MM, 2)}mm" height="{_fmt(HEIGHT_MM, 1)}mm" '
        f'viewBox="0 0 {_fmt(WIDTH_MM, 2)} {_fmt(HEIGHT_MM, 1)}">',
        _rect(0.0, 0.0, WIDTH_MM, HEIGHT_MM, PANEL_IVORY, PANEL_IVORY, stroke_width=0.0),
    ]

    lines.extend(_embedded_logo())
    lines.append(
        _text(
            TITLE_X,
            TITLE_Y,
            "FourV2",
            size=TITLE_FONT_SIZE,
            fill=LEGEND_CHARCOAL,
            anchor="start",
            weight="600",
        )
    )

    # Global routing/control section.
    lines.append(
        _rect(
            *ROUTING_SECTION,
            SECTION_FILL,
            SECTION_BLUE_GREY,
            identifier="routing-section",
            radius=1.4,
            stroke_width=0.35,
        )
    )
    lines.append(
        _text(
            GLOBAL_CONTROLS["algorithm_knob"][0],
            ALGORITHM_LABEL_Y,
            "ALGO",
            size=1.55,
            weight="600",
        )
    )
    _append_routing_artwork(lines)
    for value, x in (
        ("TUNE", GLOBAL_CONTROLS["tune_knob"][0]),
        ("PM DEPTH", GLOBAL_CONTROLS["pm_depth_knob"][0]),
        ("MASTER", GLOBAL_CONTROLS["master_knob"][0]),
        ("PM CV", GLOBAL_CONTROLS["pm_depth_cv_jack"][0]),
        ("ATTEN", GLOBAL_CONTROLS["pm_depth_cv_atten"][0]),
    ):
        lines.append(_text(x, GLOBAL_LABEL_Y, value, size=1.55))
    lines.append(
        _text(
            GLOBAL_CONTROLS["external_pm_jack"][0],
            EXTERNAL_PM_LABEL_Y,
            "EXT PM",
            size=1.65,
        )
    )
    lines.append(
        _text(
            GLOBAL_CONTROLS["external_pm_atten"][0],
            EXTERNAL_PM_LABEL_Y,
            "ATTEN",
            size=1.55,
        )
    )

    # Four framed operator fields.
    for index, (x, y, width, height) in enumerate(OPERATOR_SECTION_RECTS, start=1):
        lines.append(
            _rect(
                x,
                y,
                width,
                height,
                SECTION_FILL_ALT if index % 2 else SECTION_FILL,
                SECTION_BLUE_GREY,
                identifier=f"operator-section-{index}",
                radius=1.2,
                stroke_width=0.30,
            )
        )
        centre_x = OPERATOR_CENTRES_X[index - 1]
        lines.append(
            _text(
                centre_x,
                OPERATOR_HEADING_Y,
                f"OP{index}",
                size=2.4,
                weight="600",
                letter_spacing=0.25,
            )
        )
        display_x, display_y, display_width, display_height = FREQUENCY_DISPLAY_RECTS[index - 1]
        lines.append(
            _rect(
                display_x,
                display_y,
                display_width,
                display_height,
                DISPLAY_CHARCOAL,
                SECTION_BLUE_GREY,
                identifier=f"op{index}-frequency-display",
                radius=0.6,
                stroke_width=0.25,
            )
        )
        # These small legends are static; the display text itself is supplied
        # by the host widget at run time.
        lines.append(_text(display_x + 1.5, display_y + 2.7, "FREQ", size=1.45, fill=SECTION_BLUE_GREY, anchor="start"))
        lines.append(
            _text(
                centre_x + OPERATOR_X_OFFSETS["coarse"],
                OPERATOR_LABEL_YS["coarse_mode"],
                "COARSE",
                size=OPERATOR_LABEL_SIZE,
            )
        )
        lines.append(
            _text(
                centre_x + OPERATOR_X_OFFSETS["freq_mode"],
                OPERATOR_LABEL_YS["coarse_mode"],
                "MODE",
                size=OPERATOR_MODE_LABEL_SIZE,
            )
        )
        lines.append(
            _text(
                centre_x + OPERATOR_X_OFFSETS["fine"],
                OPERATOR_LABEL_YS["fine_fold_type"],
                "FINE",
                size=OPERATOR_LABEL_SIZE,
            )
        )
        lines.append(
            _text(
                centre_x + OPERATOR_X_OFFSETS["fold_type"],
                OPERATOR_LABEL_YS["fine_fold_type"],
                "TYPE",
                size=OPERATOR_LABEL_SIZE,
            )
        )
        for parameter in ("output", "warp", "fold", "feedback"):
            lines.append(
                _text(
                    centre_x + OPERATOR_X_OFFSETS[parameter],
                    OPERATOR_LABEL_YS[parameter],
                    parameter.upper(),
                    size=OPERATOR_LABEL_SIZE,
                )
            )
        lines.append(
            _text(
                centre_x + OPERATOR_PARAMETER_X_OFFSETS["cv_input"],
                OPERATOR_PARAMETER_HEADER_Y,
                "CV",
                size=OPERATOR_PARAMETER_HEADER_SIZE,
            )
        )
        lines.append(
            _text(
                centre_x + OPERATOR_PARAMETER_X_OFFSETS["cv_atten"],
                OPERATOR_PARAMETER_HEADER_Y,
                "ATTEN",
                size=OPERATOR_PARAMETER_HEADER_SIZE,
            )
        )

    # Operator-specific CV controls share the sound-control rows above.  The
    # component guides are emitted below with the rest of the Rack geometry.

    # Shared I/O labels use the open spaces around the controls in the global
    # band rather than spending another row beneath the operator fields.
    lines.append(
        _text(
            VOCT_LABEL_X,
            SHARED_IO_LABEL_Y,
            "V/OCT",
            size=1.55,
            anchor="start",
            text_length=4.5,
        )
    )
    lines.append(
        _text(
            MAIN_OUTPUT_LABEL_X,
            MAIN_OUTPUT_LABEL_Y,
            "MAIN OUT",
            size=1.55,
            anchor="middle",
            text_length=9.5,
        )
    )
    lines.append(
        _text(
            GLOBAL_CONTROLS["over_light"][0],
            OVER_LABEL_Y,
            "OVER",
            size=1.55,
            fill=FUNCTION_ORANGE,
        )
    )

    # Rack widget guide circles/rectangles are direct children so existing
    # output-style tests can locate the main output by its coordinate.
    for name, x, y in COMPONENTS:
        _append_component(lines, name, x, y)

    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def _header_float(name: str, value: float, digits: int = 3) -> str:
    formatted = _fmt(value, digits)
    if "." not in formatted and "e" not in formatted.lower():
        formatted += ".0"
    return f"constexpr float {name} = {formatted}f;"


def generate_coords_header() -> str:
    """Return the generated FourV2 C++11 coordinate header."""
    lines = [
        "#pragma once",
        "// Auto-generated by scripts/generate_panel_four_v2.py; do not edit manually.",
        "// All panel coordinates and dimensions are millimetres.",
        "",
        "namespace four_v2_layout {",
        "",
        f"constexpr int PANEL_HP = {HP};",
        _header_float("PANEL_WIDTH", WIDTH_MM, 2),
        _header_float("PANEL_HEIGHT", HEIGHT_MM, 1),
        _header_float("TITLE_X", TITLE_X),
        _header_float("TITLE_Y", TITLE_Y),
        _header_float("TITLE_FONT_SIZE", TITLE_FONT_SIZE),
        _header_float("LOGO_TARGET_X", LOGO_TARGET_X),
        _header_float("LOGO_TARGET_Y", LOGO_TARGET_Y),
        _header_float("LOGO_SCALE", LOGO_SCALE, 4),
        _header_float("MINIMUM_EDGE_CLEARANCE_MM", MINIMUM_EDGE_CLEARANCE_MM),
        _header_float("MINIMUM_LABEL_CLEARANCE_MM", MINIMUM_LABEL_CLEARANCE_MM),
        _header_float("ALGORITHM_LABEL_Y", ALGORITHM_LABEL_Y),
        _header_float("GLOBAL_LABEL_Y", GLOBAL_LABEL_Y),
        _header_float("EXTERNAL_PM_LABEL_Y", EXTERNAL_PM_LABEL_Y),
        "",
        "// Global routing display rectangle",
    ]
    routing_x, routing_y, routing_width, routing_height = ROUTING_DISPLAY
    lines.extend(
        (
            _header_float("ROUTING_DISPLAY_X", routing_x),
            _header_float("ROUTING_DISPLAY_Y", routing_y),
            _header_float("ROUTING_DISPLAY_WIDTH", routing_width),
            _header_float("ROUTING_DISPLAY_HEIGHT", routing_height),
        )
    )
    lines.extend(("", "// Global controls"))
    for name, (x, y) in GLOBAL_CONTROLS.items():
        upper = name.upper()
        lines.append(_header_float(f"{upper}_X", x))
        lines.append(_header_float(f"{upper}_Y", y))
    lines.extend(
        (
            "",
            "// Friendly aliases used by the Rack module implementation",
            "constexpr float ALGORITHM_X = ALGORITHM_KNOB_X;",
            "constexpr float ALGORITHM_Y = ALGORITHM_KNOB_Y;",
            "constexpr float TUNE_X = TUNE_KNOB_X;",
            "constexpr float TUNE_Y = TUNE_KNOB_Y;",
            "constexpr float PM_DEPTH_X = PM_DEPTH_KNOB_X;",
            "constexpr float PM_DEPTH_Y = PM_DEPTH_KNOB_Y;",
            "constexpr float MASTER_X = MASTER_KNOB_X;",
            "constexpr float MASTER_Y = MASTER_KNOB_Y;",
            "constexpr float EXT_PM_X = EXTERNAL_PM_JACK_X;",
            "constexpr float EXT_PM_Y = EXTERNAL_PM_JACK_Y;",
            "",
            "// Operator fields and control centres",
            _header_float("OPERATOR_SECTION_TOP", OPERATOR_SECTION_TOP),
            _header_float("OPERATOR_SECTION_HEIGHT", OPERATOR_SECTION_HEIGHT),
            _header_float("OPERATOR_SECTION_WIDTH", OPERATOR_SECTION_WIDTH),
            _header_float("OPERATOR_SECTION_GAP", OPERATOR_SECTION_GAP),
            _header_float("OPERATOR_HEADING_Y", OPERATOR_HEADING_Y),
            _header_float("OPERATOR_COARSE_MODE_Y", OPERATOR_ROW_YS["coarse_mode"]),
            _header_float("OPERATOR_FINE_FOLD_TYPE_Y", OPERATOR_ROW_YS["fine_fold_type"]),
            _header_float("OPERATOR_OUTPUT_Y", OPERATOR_ROW_YS["output"]),
            _header_float("OPERATOR_WARP_Y", OPERATOR_ROW_YS["warp"]),
            _header_float("OPERATOR_FOLD_Y", OPERATOR_ROW_YS["fold"]),
            _header_float("OPERATOR_FEEDBACK_Y", OPERATOR_ROW_YS["feedback"]),
            _header_float("OPERATOR_COARSE_MODE_LABEL_Y", OPERATOR_LABEL_YS["coarse_mode"]),
            _header_float("OPERATOR_FINE_FOLD_TYPE_LABEL_Y", OPERATOR_LABEL_YS["fine_fold_type"]),
            _header_float("OPERATOR_OUTPUT_LABEL_Y", OPERATOR_LABEL_YS["output"]),
            _header_float("OPERATOR_WARP_LABEL_Y", OPERATOR_LABEL_YS["warp"]),
            _header_float("OPERATOR_FOLD_LABEL_Y", OPERATOR_LABEL_YS["fold"]),
            _header_float("OPERATOR_FEEDBACK_LABEL_Y", OPERATOR_LABEL_YS["feedback"]),
            _header_float("OPERATOR_LABEL_SIZE", OPERATOR_LABEL_SIZE),
            _header_float("OPERATOR_MODE_LABEL_SIZE", OPERATOR_MODE_LABEL_SIZE),
            _header_float("OPERATOR_PARAMETER_HEADER_Y", OPERATOR_PARAMETER_HEADER_Y),
            _header_float("OPERATOR_PARAMETER_HEADER_SIZE", OPERATOR_PARAMETER_HEADER_SIZE),
            _header_float("FREQUENCY_DISPLAY_WIDTH", FREQUENCY_DISPLAY_WIDTH),
            _header_float("FREQUENCY_DISPLAY_HEIGHT", FREQUENCY_DISPLAY_HEIGHT),
            "constexpr float OPERATOR_FREQUENCY_Y = OPERATOR_COARSE_MODE_Y;",
            "constexpr float OPERATOR_OUTPUT_WARP_Y = OPERATOR_OUTPUT_Y;",
            "constexpr float OPERATOR_FREQUENCY_LABEL_Y = OPERATOR_COARSE_MODE_LABEL_Y;",
            "constexpr float OPERATOR_OUTPUT_WARP_LABEL_Y = OPERATOR_OUTPUT_LABEL_Y;",
        )
    )
    for index, (section, centre_x, display) in enumerate(
        zip(OPERATOR_SECTION_RECTS, OPERATOR_CENTRES_X, FREQUENCY_DISPLAY_RECTS),
        start=1,
    ):
        section_x, section_y, section_width, section_height = section
        display_x, display_y, display_width, display_height = display
        lines.extend(
            (
                _header_float(f"OP{index}_CENTER_X", centre_x),
                _header_float(f"OP{index}_CENTER_Y", OPERATOR_SECTION_TOP + OPERATOR_SECTION_HEIGHT / 2.0),
                _header_float(f"OP{index}_SECTION_X", section_x),
                _header_float(f"OP{index}_SECTION_Y", section_y),
                _header_float(f"OP{index}_SECTION_WIDTH", section_width),
                _header_float(f"OP{index}_SECTION_HEIGHT", section_height),
                _header_float(f"OP{index}_FREQUENCY_DISPLAY_X", display_x),
                _header_float(f"OP{index}_FREQUENCY_DISPLAY_Y", display_y),
                _header_float(f"OP{index}_FREQUENCY_DISPLAY_WIDTH", display_width),
                _header_float(f"OP{index}_FREQUENCY_DISPLAY_HEIGHT", display_height),
            )
        )
        for control, offset in OPERATOR_X_OFFSETS.items():
            if control in ("coarse", "freq_mode"):
                y = OPERATOR_ROW_YS["coarse_mode"]
            elif control in ("fine", "fold_type"):
                y = OPERATOR_ROW_YS["fine_fold_type"]
            else:
                y = OPERATOR_ROW_YS[control]
            control_name = control.upper()
            lines.append(_header_float(f"OP{index}_{control_name}_X", centre_x + offset))
            lines.append(_header_float(f"OP{index}_{control_name}_Y", y))
        lines.extend(
            (
                f"constexpr float OP{index}_FREQ_SWITCH_X = OP{index}_FREQ_MODE_X;",
                f"constexpr float OP{index}_FREQ_SWITCH_Y = OP{index}_FREQ_MODE_Y;",
                f"constexpr float OP{index}_COARSE_KNOB_X = OP{index}_COARSE_X;",
                f"constexpr float OP{index}_COARSE_KNOB_Y = OP{index}_COARSE_Y;",
                f"constexpr float OP{index}_FINE_KNOB_X = OP{index}_FINE_X;",
                f"constexpr float OP{index}_FINE_KNOB_Y = OP{index}_FINE_Y;",
                f"constexpr float OP{index}_FREQ_DISPLAY_X = OP{index}_FREQUENCY_DISPLAY_X;",
                f"constexpr float OP{index}_FREQ_DISPLAY_Y = OP{index}_FREQUENCY_DISPLAY_Y;",
                f"constexpr float OP{index}_FREQ_DISPLAY_WIDTH = OP{index}_FREQUENCY_DISPLAY_WIDTH;",
                f"constexpr float OP{index}_FREQ_DISPLAY_HEIGHT = OP{index}_FREQUENCY_DISPLAY_HEIGHT;",
            )
        )
    lines.extend(("", "// CV patchbay alignment and cell centres"))
    lines.extend(
        (
            _header_float("PATCHBAY_X", PATCHBAY_SECTION[0]),
            _header_float("PATCHBAY_Y", PATCHBAY_SECTION[1]),
            _header_float("PATCHBAY_WIDTH", PATCHBAY_SECTION[2]),
            _header_float("PATCHBAY_HEIGHT", PATCHBAY_SECTION[3]),
            _header_float("PATCHBAY_SECTION_TOP", PATCHBAY_SECTION_TOP),
            _header_float("PATCHBAY_SECTION_HEIGHT", PATCHBAY_SECTION_HEIGHT),
            _header_float("PATCHBAY_WIDGET_OFFSET", PATCHBAY_WIDGET_OFFSET),
        )
    )
    for index, x in enumerate(PATCHBAY_COLUMN_XS, start=1):
        lines.extend(
            (
                _header_float(f"OP{index}_PATCHBAY_SECTION_X", OPERATOR_SECTION_RECTS[index - 1][0]),
                _header_float(f"OP{index}_PATCHBAY_SECTION_Y", OPERATOR_SECTION_TOP),
                _header_float(f"OP{index}_PATCHBAY_SECTION_WIDTH", OPERATOR_SECTION_RECTS[index - 1][2]),
                _header_float(f"OP{index}_PATCHBAY_SECTION_HEIGHT", OPERATOR_SECTION_HEIGHT),
            )
        )
        lines.append(_header_float(f"PATCHBAY_COLUMN_{index}_X", x))
        lines.append(_header_float(f"OP{index}_PATCHBAY_X", x))
    for row, y in zip(PATCHBAY_ROWS, PATCHBAY_ROW_YS):
        slug = row.upper()
        lines.append(_header_float(f"PATCHBAY_{slug}_Y", y))
        for index, centre_x in enumerate(PATCHBAY_COLUMN_XS, start=1):
            lines.extend(
                (
                    _header_float(f"OP{index}_{slug}_CV_INPUT_X", centre_x + OPERATOR_PARAMETER_X_OFFSETS["cv_input"]),
                    _header_float(f"OP{index}_{slug}_CV_INPUT_Y", y),
                    _header_float(f"OP{index}_{slug}_CV_ATTEN_X", centre_x + OPERATOR_PARAMETER_X_OFFSETS["cv_atten"]),
                    _header_float(f"OP{index}_{slug}_CV_ATTEN_Y", y),
                )
            )
    lines.extend(("", "// Shared I/O and component geometry"))
    lines.extend(
        (
            _header_float("SHARED_IO_X", SHARED_IO_SECTION[0]),
            _header_float("SHARED_IO_Y", SHARED_IO_SECTION[1]),
            _header_float("SHARED_IO_WIDTH", SHARED_IO_SECTION[2]),
            _header_float("SHARED_IO_HEIGHT", SHARED_IO_SECTION[3]),
            _header_float("VOCT_LABEL_X", VOCT_LABEL_X),
            _header_float("MAIN_OUTPUT_LABEL_X", MAIN_OUTPUT_LABEL_X),
            _header_float("SHARED_IO_LABEL_Y", SHARED_IO_LABEL_Y),
            _header_float("MAIN_OUTPUT_LABEL_Y", MAIN_OUTPUT_LABEL_Y),
            _header_float("OVER_LABEL_Y", OVER_LABEL_Y),
            _header_float("RACK_SMALL_KNOB_RADIUS", RACK_SMALL_KNOB_RADIUS, 6),
            _header_float("RACK_PORT_RADIUS", RACK_PORT_RADIUS, 6),
            _header_float("OUTPUT_BACKPLATE_RADIUS", OUTPUT_BACKPLATE_RADIUS, 6),
            _header_float("OUTPUT_STROKE_WIDTH", OUTPUT_STROKE_WIDTH),
            _header_float("GLOBAL_LABEL_OFFSET", 5.0),
            _header_float("OPERATOR_LABEL_OFFSET", 5.5),
        )
    )
    lines.extend(("", "} // namespace four_v2_layout", ""))
    return "\n".join(lines)


generate_header = generate_coords_header


def write_state_switch_assets() -> None:
    for filename, label in STATE_SWITCH_ASSETS:
        (ROOT / "res" / filename).write_text(
            generate_state_switch_svg(label),
            encoding="utf-8",
        )


def main() -> None:
    SVG_PATH.parent.mkdir(parents=True, exist_ok=True)
    HEADER_PATH.parent.mkdir(parents=True, exist_ok=True)
    SVG_PATH.write_text(generate_svg(), encoding="utf-8")
    HEADER_PATH.write_text(generate_coords_header(), encoding="utf-8")
    write_state_switch_assets()
    print(f"Wrote {SVG_PATH}")
    print(f"Wrote {HEADER_PATH}")
    for filename, _label in STATE_SWITCH_ASSETS:
        print(f"Wrote {ROOT / 'res' / filename}")


if __name__ == "__main__":
    main()
