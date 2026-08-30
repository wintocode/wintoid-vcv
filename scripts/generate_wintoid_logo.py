#!/usr/bin/env python3
"""Capture and deterministically generate the outlined lowercase wintoid mark.

The one-time ``--capture-font`` path is deliberately separate from normal
generation.  It limits Fontconfig to the supplied font directory, verifies
the selected file with ``fc-match``, asks Pango to shape the complete word as
one run, and converts the resulting FreeType outlines into checked-in JSON.
Normal generation reads only that JSON, so a build never depends on a host
font installation or on Pango's layout decisions.
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile
from xml.sax.saxutils import escape


ROOT = Path(__file__).resolve().parents[1]
DATA_PATH = ROOT / "scripts" / "assets" / "wintoid_logo_glyphs.json"
SVG_PATH = ROOT / "res" / "WintoidLogo.svg"
WORD = "wintoid"
FONT_FAMILY = "DejaVu Sans"
FONT_SIZE_PX = 64.0
PANGO_SCALE = 1024.0
BASELINE_Y = 70.0
UNDERLINE_Y = BASELINE_Y + 4.0
VIEW_PADDING = 2.0
LOGO_BLUE = "#1a1a2e"
LOGO_ORANGE = "#ff4d00"


def _number(value: float, digits: int = 4) -> str:
    """Format a finite SVG number without platform-dependent noise."""
    if not math.isfinite(value):
        raise ValueError(f"non-finite SVG number: {value!r}")
    result = f"{value:.{digits}f}".rstrip("0").rstrip(".")
    return result if result and result != "-0" else "0"


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _fontconfig_environment(font_path: Path, temporary_root: Path):
    """Return an environment whose only Fontconfig font directory is ours."""
    font_dir = font_path.parent.resolve()
    cache_dir = temporary_root / "fontconfig-cache"
    cache_dir.mkdir()
    config_path = temporary_root / "fonts.conf"
    # No system include or default directory is present.  The cache directory
    # is writable process state, not a font search path.
    config_path.write_text(
        "<?xml version=\"1.0\"?>\n"
        "<fontconfig>\n"
        f"  <dir>{escape(str(font_dir))}</dir>\n"
        f"  <cachedir>{escape(str(cache_dir))}</cachedir>\n"
        "</fontconfig>\n",
        encoding="utf-8",
    )
    environment = os.environ.copy()
    environment["FONTCONFIG_FILE"] = str(config_path)
    environment.pop("FONTCONFIG_PATH", None)
    return environment


def _assert_fontconfig_match(font_path: Path, environment: dict[str, str]) -> None:
    """Use fc-match as the gate before any Pango process is started."""
    result = subprocess.run(
        ["fc-match", "--format=%{file}\n", FONT_FAMILY],
        check=True,
        capture_output=True,
        text=True,
        env=environment,
    )
    matches = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if not matches:
        raise RuntimeError("fc-match returned no font for DejaVu Sans")
    matched = Path(matches[0]).resolve()
    expected = font_path.resolve()
    if matched != expected:
        raise RuntimeError(
            "Fontconfig did not resolve DejaVu Sans to the supplied font: "
            f"expected {expected}, got {matched}"
        )


def _shape_with_pango(font_path: Path, environment: dict[str, str], output: Path):
    """Ask Pango for one complete, positioned lowercase word run."""
    subprocess.run(
        [
            "pango-view",
            "--backend=ft2",
            "--no-display",
            "--single-par",
            "--pixels",
            "--font",
            f"{FONT_FAMILY} {FONT_SIZE_PX:g}",
            "--text",
            WORD,
            "--serialize-to",
            str(output),
        ],
        check=True,
        capture_output=True,
        text=True,
        env=environment,
    )
    shaped = json.loads(output.read_text(encoding="utf-8"))
    if shaped.get("text") != WORD:
        raise RuntimeError("Pango did not serialize the complete lowercase word")
    lines = shaped.get("output", {}).get("lines", [])
    if len(lines) != 1:
        raise RuntimeError(f"expected one Pango line, got {len(lines)}")
    runs = lines[0].get("runs", [])
    if len(runs) != 1 or runs[0].get("text") != WORD:
        raise RuntimeError(
            "Pango shaped wintoid into more than one run; independent word "
            "spacing is not allowed"
        )
    glyphs = runs[0].get("glyphs", [])
    if len(glyphs) != len(WORD):
        raise RuntimeError(
            f"expected seven positioned glyphs, got {len(glyphs)}"
        )
    description = runs[0].get("font", {}).get("description", "")
    if not description.startswith(FONT_FAMILY):
        raise RuntimeError(f"Pango selected an unexpected font: {description}")
    return shaped, glyphs


class _FTGeneric(ctypes.Structure):
    _fields_ = [("data", ctypes.c_void_p), ("finalizer", ctypes.c_void_p)]


class _FTVector(ctypes.Structure):
    _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]


class _FTBBox(ctypes.Structure):
    _fields_ = [
        ("x_min", ctypes.c_long),
        ("y_min", ctypes.c_long),
        ("x_max", ctypes.c_long),
        ("y_max", ctypes.c_long),
    ]


class _FTBitmap(ctypes.Structure):
    _fields_ = [
        ("rows", ctypes.c_uint),
        ("width", ctypes.c_uint),
        ("pitch", ctypes.c_int),
        ("buffer", ctypes.c_void_p),
        ("num_grays", ctypes.c_ushort),
        ("pixel_mode", ctypes.c_ubyte),
        ("palette_mode", ctypes.c_ubyte),
        ("palette", ctypes.c_void_p),
    ]


class _FTOutline(ctypes.Structure):
    _fields_ = [
        ("n_contours", ctypes.c_ushort),
        ("n_points", ctypes.c_ushort),
        ("points", ctypes.POINTER(_FTVector)),
        ("tags", ctypes.POINTER(ctypes.c_ubyte)),
        ("contours", ctypes.POINTER(ctypes.c_ushort)),
        ("flags", ctypes.c_int),
    ]


class _FTGlyphMetrics(ctypes.Structure):
    _fields_ = [
        ("width", ctypes.c_long),
        ("height", ctypes.c_long),
        ("hori_bearing_x", ctypes.c_long),
        ("hori_bearing_y", ctypes.c_long),
        ("hori_advance", ctypes.c_long),
        ("vert_bearing_x", ctypes.c_long),
        ("vert_bearing_y", ctypes.c_long),
        ("vert_advance", ctypes.c_long),
    ]


class _FTGlyphSlotRec(ctypes.Structure):
    _fields_ = [
        ("library", ctypes.c_void_p),
        ("face", ctypes.c_void_p),
        ("next", ctypes.c_void_p),
        ("glyph_index", ctypes.c_uint),
        ("generic", _FTGeneric),
        ("metrics", _FTGlyphMetrics),
        ("linear_hori_advance", ctypes.c_long),
        ("linear_vert_advance", ctypes.c_long),
        ("advance", _FTVector),
        ("format", ctypes.c_int),
        ("bitmap", _FTBitmap),
        ("bitmap_left", ctypes.c_int),
        ("bitmap_top", ctypes.c_int),
        ("outline", _FTOutline),
    ]


class _FTFaceRec(ctypes.Structure):
    _fields_ = [
        ("num_faces", ctypes.c_long),
        ("face_index", ctypes.c_long),
        ("face_flags", ctypes.c_long),
        ("style_flags", ctypes.c_long),
        ("num_glyphs", ctypes.c_long),
        ("family_name", ctypes.c_void_p),
        ("style_name", ctypes.c_void_p),
        ("num_fixed_sizes", ctypes.c_int),
        ("available_sizes", ctypes.c_void_p),
        ("num_charmaps", ctypes.c_int),
        ("charmaps", ctypes.c_void_p),
        ("generic", _FTGeneric),
        ("bbox", _FTBBox),
        ("units_per_em", ctypes.c_ushort),
        ("ascender", ctypes.c_short),
        ("descender", ctypes.c_short),
        ("height", ctypes.c_short),
        ("max_advance_width", ctypes.c_short),
        ("max_advance_height", ctypes.c_short),
        ("underline_position", ctypes.c_short),
        ("underline_thickness", ctypes.c_short),
        ("glyph", ctypes.c_void_p),
    ]


class _FreeType:
    """Small ctypes wrapper for the outline-only FreeType API."""

    LOAD_NO_SCALE = 1 << 0
    LOAD_NO_HINTING = 1 << 1
    LOAD_NO_BITMAP = 1 << 3

    def __init__(self, font_path: Path):
        library_name = ctypes.util.find_library("freetype")
        candidates = [
            library_name,
            "libfreetype.6.dylib",
            "libfreetype.so.6",
            "/opt/homebrew/lib/libfreetype.6.dylib",
            "/usr/local/lib/libfreetype.6.dylib",
        ]
        for candidate in candidates:
            if not candidate:
                continue
            try:
                self.library = ctypes.CDLL(candidate)
                break
            except OSError:
                continue
        else:
            raise RuntimeError("could not load the available FreeType library")

        self.library.FT_Init_FreeType.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
        self.library.FT_Init_FreeType.restype = ctypes.c_int
        self.library.FT_New_Face.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_long,
            ctypes.POINTER(ctypes.c_void_p),
        ]
        self.library.FT_New_Face.restype = ctypes.c_int
        self.library.FT_Load_Glyph.argtypes = [
            ctypes.c_void_p,
            ctypes.c_uint,
            ctypes.c_int,
        ]
        self.library.FT_Load_Glyph.restype = ctypes.c_int
        self.library.FT_Done_Face.argtypes = [ctypes.c_void_p]
        self.library.FT_Done_Face.restype = ctypes.c_int
        self.library.FT_Done_FreeType.argtypes = [ctypes.c_void_p]
        self.library.FT_Done_FreeType.restype = ctypes.c_int

        self.handle = ctypes.c_void_p()
        error = self.library.FT_Init_FreeType(ctypes.byref(self.handle))
        if error:
            raise RuntimeError(f"FT_Init_FreeType failed with {error}")
        self.face = ctypes.c_void_p()
        error = self.library.FT_New_Face(
            self.handle, os.fsencode(str(font_path)), 0, ctypes.byref(self.face)
        )
        if error:
            self.library.FT_Done_FreeType(self.handle)
            raise RuntimeError(f"FT_New_Face failed with {error}")
        self.face_rec = ctypes.cast(self.face, ctypes.POINTER(_FTFaceRec)).contents
        self.units_per_em = int(self.face_rec.units_per_em)
        if not self.units_per_em:
            self.close()
            raise RuntimeError("the supplied font has no units-per-em value")

    def outline(self, glyph_index: int):
        error = self.library.FT_Load_Glyph(
            self.face,
            int(glyph_index),
            self.LOAD_NO_SCALE | self.LOAD_NO_HINTING | self.LOAD_NO_BITMAP,
        )
        if error:
            raise RuntimeError(
                f"FT_Load_Glyph({glyph_index}) failed with error {error}"
            )
        slot = ctypes.cast(
            self.face_rec.glyph, ctypes.POINTER(_FTGlyphSlotRec)
        ).contents
        outline = slot.outline
        points = [
            (float(outline.points[index].x), float(outline.points[index].y))
            for index in range(outline.n_points)
        ]
        tags = [int(outline.tags[index]) & 3 for index in range(outline.n_points)]
        contours = [int(outline.contours[index]) for index in range(outline.n_contours)]
        return points, tags, contours

    def close(self):
        if getattr(self, "face", None):
            self.library.FT_Done_Face(self.face)
            self.face = None
        if getattr(self, "handle", None):
            self.library.FT_Done_FreeType(self.handle)
            self.handle = None

    def __enter__(self):
        return self

    def __exit__(self, _exception_type, _exception, _traceback):
        self.close()


def _midpoint(first, second):
    return ((first[0] + second[0]) / 2.0, (first[1] + second[1]) / 2.0)


def _outline_commands(points, tags, contours, units_per_em):
    """Convert TrueType contours to quadratic/cubic SVG path commands."""
    commands = []
    first_point = 0
    for contour_end in contours:
        contour_points = points[first_point:contour_end + 1]
        contour_tags = tags[first_point:contour_end + 1]
        if not contour_points:
            first_point = contour_end + 1
            continue

        if contour_tags[0] & 1:
            start = contour_points[0]
            sequence = contour_points[1:] + [contour_points[0]]
            sequence_tags = contour_tags[1:] + [contour_tags[0]]
        elif contour_tags[-1] & 1:
            start = contour_points[-1]
            sequence = contour_points + [contour_points[-1]]
            sequence_tags = contour_tags + [contour_tags[-1]]
        else:
            start = _midpoint(contour_points[-1], contour_points[0])
            sequence = contour_points + [start]
            sequence_tags = contour_tags + [1]

        scale = FONT_SIZE_PX / float(units_per_em)

        def convert(point):
            return (point[0] * scale, -point[1] * scale)

        commands.append(("M", *convert(start)))
        index = 0
        while index < len(sequence) - 1:
            current = sequence[index]
            tag = sequence_tags[index] & 3
            if tag & 1:
                commands.append(("L", *convert(current)))
                index += 1
                continue
            if tag == 2:
                if index + 2 >= len(sequence):
                    raise RuntimeError("malformed cubic TrueType contour")
                control2 = sequence[index + 1]
                end = sequence[index + 2]
                commands.append(("C", *convert(current), *convert(control2),
                                 *convert(end)))
                index += 3
                continue

            next_point = sequence[index + 1]
            next_tag = sequence_tags[index + 1] & 3
            if next_tag & 1:
                commands.append(("Q", *convert(current), *convert(next_point)))
                index += 2
            elif next_tag == 2:
                raise RuntimeError("malformed conic TrueType contour")
            else:
                middle = _midpoint(current, next_point)
                commands.append(("Q", *convert(current), *convert(middle)))
                index += 1
        commands.append(("Z",))
        first_point = contour_end + 1
    return commands


def _quadratic_extrema(start, control, end):
    values = [start, end]
    denominator = start - 2.0 * control + end
    if denominator:
        t = (start - control) / denominator
        if 0.0 < t < 1.0:
            values.append(
                (1.0 - t) ** 2 * start +
                2.0 * (1.0 - t) * t * control +
                t ** 2 * end
            )
    return values


def _cubic_value(start, control1, control2, end, t):
    inverse = 1.0 - t
    return (
        inverse ** 3 * start +
        3.0 * inverse ** 2 * t * control1 +
        3.0 * inverse * t ** 2 * control2 +
        t ** 3 * end
    )


def _cubic_extrema(start, control1, control2, end):
    values = [start, end]
    a = -start + 3.0 * control1 - 3.0 * control2 + end
    b = 2.0 * (start - 2.0 * control1 + control2)
    c = control1 - start
    if abs(a) < 1e-12:
        roots = [-c / b] if abs(b) >= 1e-12 else []
    else:
        discriminant = b * b - 4.0 * a * c
        if discriminant < 0.0:
            roots = []
        else:
            root = math.sqrt(discriminant)
            roots = [(-b + root) / (2.0 * a), (-b - root) / (2.0 * a)]
    for t in roots:
        if 0.0 < t < 1.0:
            values.append(_cubic_value(start, control1, control2, end, t))
    return values


def _path_bounds(commands):
    xs = []
    ys = []
    current = (0.0, 0.0)
    for command in commands:
        if command[0] == "M" or command[0] == "L":
            current = (command[1], command[2])
            xs.extend((current[0],))
            ys.extend((current[1],))
        elif command[0] == "Q":
            end = (command[3], command[4])
            xs.extend(_quadratic_extrema(current[0], command[1], end[0]))
            ys.extend(_quadratic_extrema(current[1], command[2], end[1]))
            current = end
        elif command[0] == "C":
            end = (command[5], command[6])
            xs.extend(_cubic_extrema(current[0], command[1], command[3], end[0]))
            ys.extend(_cubic_extrema(current[1], command[2], command[4], end[1]))
            current = end
    if not xs:
        return (0.0, 0.0, 0.0, 0.0)
    return min(xs), min(ys), max(xs), max(ys)


def _path_string(commands):
    parts = []
    for command in commands:
        kind = command[0]
        if kind == "Z":
            parts.append("Z")
        else:
            parts.append(kind + " " + " ".join(_number(value) for value in command[1:]))
    return " ".join(parts)


def _capture(font_path: Path) -> dict:
    font_path = font_path.expanduser().resolve()
    if not font_path.is_file():
        raise FileNotFoundError(font_path)

    with tempfile.TemporaryDirectory(prefix="wintoid-fontconfig-") as directory:
        temporary_root = Path(directory)
        environment = _fontconfig_environment(font_path, temporary_root)
        # This assertion is intentionally before the Pango subprocess.
        _assert_fontconfig_match(font_path, environment)
        pango_output = temporary_root / "wintoid-pango.json"
        shaped, glyphs = _shape_with_pango(font_path, environment, pango_output)

        glyph_indices = [int(glyph["glyph"]) for glyph in glyphs]
        uses = []
        pen_x = 0.0
        for index, (character, glyph) in enumerate(zip(WORD, glyphs)):
            x_offset = float(glyph.get("x-offset", 0)) / PANGO_SCALE
            y_offset = float(glyph.get("y-offset", 0)) / PANGO_SCALE
            advance = float(glyph["width"]) / PANGO_SCALE
            uses.append({
                "index": index,
                "character": character,
                "glyph_index": glyph_indices[index],
                "x": pen_x + x_offset,
                "y": y_offset,
                "advance": advance,
                "group": "wint" if index < 4 else "oid",
            })
            pen_x += advance

        definitions = {}
        with _FreeType(font_path) as freetype:
            for glyph_index in sorted(set(glyph_indices)):
                points, tags, contours = freetype.outline(glyph_index)
                commands = _outline_commands(
                    points, tags, contours, freetype.units_per_em
                )
                definitions[str(glyph_index)] = {
                    "glyph_index": glyph_index,
                    "path": _path_string(commands),
                    "bbox": list(_path_bounds(commands)),
                }

        return {
            "schema_version": 1,
            "word": WORD,
            "font_family": FONT_FAMILY,
            "font_size_px": FONT_SIZE_PX,
            "source_font": font_path.name,
            "source_font_path": str(font_path),
            "source_font_sha256": _sha256(font_path),
            "pango_layout": {
                "font_description": shaped["output"]["lines"][0]["runs"][0]["font"]["description"],
                "width_pango_units": shaped["output"]["width"],
                "height_pango_units": shaped["output"]["height"],
                "pango_scale": PANGO_SCALE,
            },
            "glyph_definitions": definitions,
            "glyph_uses": uses,
        }


def _load_data(path: Path = DATA_PATH) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise RuntimeError(f"missing checked-in glyph data: {path}") from error
    if data.get("schema_version") != 1:
        raise RuntimeError("unsupported wintoid glyph-data schema")
    if data.get("word") != WORD:
        raise RuntimeError("glyph data is not for lowercase wintoid")
    digest = data.get("source_font_sha256")
    if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise RuntimeError("glyph data has no valid source-font SHA-256")
    definitions = data.get("glyph_definitions")
    uses = data.get("glyph_uses")
    if not isinstance(definitions, dict) or not isinstance(uses, list):
        raise RuntimeError("glyph data is missing definitions or uses")
    if len(uses) != len(WORD):
        raise RuntimeError("glyph data must contain seven positioned uses")
    for index, use in enumerate(uses):
        if use.get("character") != WORD[index]:
            raise RuntimeError("glyph uses are not in lowercase word order")
        if use.get("group") != ("wint" if index < 4 else "oid"):
            raise RuntimeError("glyph colour boundary is not between t and o")
        key = str(use.get("glyph_index"))
        definition = definitions.get(key)
        if not isinstance(definition, dict) or not definition.get("path"):
            raise RuntimeError(f"missing path definition for glyph {key}")
        if len(definition.get("bbox", [])) != 4:
            raise RuntimeError(f"missing bounds for glyph {key}")
    return data


def _group_bounds(data, group):
    definitions = data["glyph_definitions"]
    bounds = []
    for use in data["glyph_uses"]:
        if use["group"] != group:
            continue
        bbox = definitions[str(use["glyph_index"])]["bbox"]
        bounds.append((float(use["x"]) + float(bbox[0]),
                       float(use["x"]) + float(bbox[2])))
    return min(value[0] for value in bounds), max(value[1] for value in bounds)


def generate_svg(data: dict | None = None) -> str:
    """Generate the canonical outlined SVG using checked-in data only."""
    data = _load_data() if data is None else data
    definitions = data["glyph_definitions"]
    uses = data["glyph_uses"]
    all_bounds = []
    for use in uses:
        bbox = definitions[str(use["glyph_index"])]["bbox"]
        all_bounds.append((float(use["x"]) + float(bbox[0]),
                           BASELINE_Y + float(use["y"]) + float(bbox[1]),
                           float(use["x"]) + float(bbox[2]),
                           BASELINE_Y + float(use["y"]) + float(bbox[3])))
    min_x = min(bound[0] for bound in all_bounds)
    min_y = min(bound[1] for bound in all_bounds)
    max_x = max(bound[2] for bound in all_bounds)
    max_y = max(max(bound[3] for bound in all_bounds), UNDERLINE_Y)
    view_x = min_x - VIEW_PADDING
    view_y = min_y - VIEW_PADDING
    view_width = max_x - min_x + 2.0 * VIEW_PADDING
    view_height = max_y - min_y + 2.0 * VIEW_PADDING

    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{_number(view_width)}" '
        f'height="{_number(view_height)}" viewBox="{_number(view_x)} '
        f'{_number(view_y)} {_number(view_width)} {_number(view_height)}" '
        f'data-source-font-sha256="{data["source_font_sha256"]}">',
        '  <title>lowercase wintoid outlined logo</title>',
        f'  <g id="wint-glyphs" fill="{LOGO_BLUE}">',
    ]
    for use in uses[:4]:
        definition = definitions[str(use["glyph_index"])]
        bbox = ",".join(_number(float(value)) for value in definition["bbox"])
        lines.append(
            f'    <path data-glyph="{use["character"]}" '
            f'data-glyph-index="{use["glyph_index"]}" data-bbox="{bbox}" '
            f'transform="translate({_number(float(use["x"]))} '
            f'{_number(BASELINE_Y + float(use["y"]))})" '
            f'fill="{LOGO_BLUE}" d="{definition["path"]}" />'
        )
    lines.append('  </g>')
    wint_min, wint_max = _group_bounds(data, "wint")
    lines.append(
        f'  <line id="wint-underline" x1="{_number(wint_min)}" '
        f'y1="{_number(UNDERLINE_Y)}" x2="{_number(wint_max)}" '
        f'y2="{_number(UNDERLINE_Y)}" stroke="{LOGO_BLUE}" '
        'stroke-width="1.4" stroke-linecap="square" />'
    )
    lines.append(f'  <g id="oid-glyphs" fill="{LOGO_ORANGE}">')
    for use in uses[4:]:
        definition = definitions[str(use["glyph_index"])]
        bbox = ",".join(_number(float(value)) for value in definition["bbox"])
        lines.append(
            f'    <path data-glyph="{use["character"]}" '
            f'data-glyph-index="{use["glyph_index"]}" data-bbox="{bbox}" '
            f'transform="translate({_number(float(use["x"]))} '
            f'{_number(BASELINE_Y + float(use["y"]))})" '
            f'fill="{LOGO_ORANGE}" d="{definition["path"]}" />'
        )
    lines.append('  </g>')
    oid_min, oid_max = _group_bounds(data, "oid")
    lines.append(
        f'  <line id="oid-underline" x1="{_number(oid_min)}" '
        f'y1="{_number(UNDERLINE_Y)}" x2="{_number(oid_max)}" '
        f'y2="{_number(UNDERLINE_Y)}" stroke="{LOGO_ORANGE}" '
        'stroke-width="1.4" stroke-linecap="square" />'
    )
    lines.append('</svg>')
    return "\n".join(lines) + "\n"


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--capture-font",
        metavar="PATH",
        help="one-time exact-font Pango capture; writes checked-in glyph JSON",
    )
    args = parser.parse_args()
    if args.capture_font:
        data = _capture(Path(args.capture_font))
        _write(DATA_PATH, json.dumps(data, indent=2, sort_keys=True) + "\n")
        print(f"Wrote {DATA_PATH}")
        return
    _write(SVG_PATH, generate_svg())
    print(f"Wrote {SVG_PATH}")


if __name__ == "__main__":
    main()
