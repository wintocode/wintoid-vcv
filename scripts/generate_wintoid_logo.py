#!/usr/bin/env python3
"""Generate the approved D1 round-dot wintoid wordmark.

The bitmap, grid layout, colour, and dot radius live in the checked-in JSON
asset.  This script turns that data into a deterministic SVG with no font or
external-image dependency, so every V2 panel can embed the exact same mark.
"""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DATA_PATH = ROOT / "scripts" / "assets" / "wintoid_logo_glyphs.json"
SVG_PATH = ROOT / "res" / "WintoidLogo.svg"

SCHEMA_VERSION = 3
MARK = "D1"
EXPECTED_GRID_COLUMNS = 33
EXPECTED_GRID_ROWS = 7
EXPECTED_DOT_COUNT = 77


def _number(value: float, digits: int = 4) -> str:
    """Format a finite SVG number deterministically."""
    if not math.isfinite(value):
        raise ValueError(f"non-finite SVG number: {value!r}")
    result = f"{value:.{digits}f}".rstrip("0").rstrip(".")
    return result if result and result != "-0" else "0"


def _sha256_bytes(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def _write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def _load_logo_data() -> dict[str, Any]:
    try:
        data = json.loads(DATA_PATH.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise RuntimeError(f"missing checked-in logo data: {DATA_PATH}") from error
    except json.JSONDecodeError as error:
        raise RuntimeError(f"invalid checked-in logo data: {DATA_PATH}") from error

    if not isinstance(data, dict):
        raise RuntimeError("logo data must be a JSON object")
    if data.get("schema_version") != SCHEMA_VERSION:
        raise RuntimeError("logo data has an unsupported schema version")
    if data.get("mark") != MARK:
        raise RuntimeError("logo data is not the approved D1 mark")

    grid = data.get("grid")
    if not isinstance(grid, dict):
        raise RuntimeError("logo data has no grid definition")
    if grid.get("columns") != EXPECTED_GRID_COLUMNS:
        raise RuntimeError("D1 logo grid must be 33 columns wide")
    if grid.get("rows") != EXPECTED_GRID_ROWS:
        raise RuntimeError("D1 logo grid must be 7 rows high")
    ink = grid.get("ink")
    if ink != "#242522":
        raise RuntimeError("D1 logo ink must be #242522")
    radius = grid.get("dot_radius")
    if not isinstance(radius, (int, float)) or isinstance(radius, bool):
        raise RuntimeError("D1 logo dot radius must be numeric")
    if radius != 0.38:
        raise RuntimeError("D1 logo dot radius must be 0.38 grid units")

    glyphs = data.get("glyphs")
    if not isinstance(glyphs, dict):
        raise RuntimeError("logo data has no glyph bitmaps")
    expected_widths = {"w": 5, "i": 1, "n": 5, "t": 5, "o": 5, "d": 5}
    if set(glyphs) != set(expected_widths):
        raise RuntimeError("D1 logo data has an unexpected glyph set")
    for character, width in expected_widths.items():
        rows = glyphs[character]
        if not isinstance(rows, list) or len(rows) != EXPECTED_GRID_ROWS:
            raise RuntimeError(f"D1 glyph {character!r} must have 7 rows")
        for row in rows:
            if (not isinstance(row, str) or len(row) != width
                    or any(cell not in "01" for cell in row)):
                raise RuntimeError(f"D1 glyph {character!r} has an invalid bitmap row")

    layout = data.get("layout")
    if not isinstance(layout, dict):
        raise RuntimeError("logo data has no glyph layout")
    order = layout.get("order")
    starts = layout.get("starts")
    if order != ["w", "i", "n", "t", "o", "i", "d"]:
        raise RuntimeError("D1 logo glyph order is invalid")
    if starts != [0, 6, 8, 14, 20, 26, 28]:
        raise RuntimeError("D1 logo glyph starts are invalid")
    dot_count = 0
    for start, character in zip(starts, order):
        width = expected_widths[character]
        if start < 0 or start + width > EXPECTED_GRID_COLUMNS:
            raise RuntimeError(f"D1 glyph {character!r} is outside the grid")
        dot_count += sum(row.count("1") for row in glyphs[character])
    if dot_count != EXPECTED_DOT_COUNT:
        raise RuntimeError("D1 logo must contain exactly 77 dots")
    return data


def _generate_svg(data: dict[str, Any]) -> str:
    grid = data["grid"]
    glyphs = data["glyphs"]
    order = data["layout"]["order"]
    starts = data["layout"]["starts"]
    ink = grid["ink"]
    radius = float(grid["dot_radius"])
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<svg xmlns="http://www.w3.org/2000/svg" width="33" height="7" '
        'viewBox="0 0 33 7">',
        '  <title>lowercase wintoid D1 round-dot wordmark</title>',
        f'  <g id="wintoid-dots" fill="{ink}">',
    ]
    for start, character in zip(starts, order):
        for row, bitmap_row in enumerate(glyphs[character]):
            for column, cell in enumerate(bitmap_row):
                if cell == "1":
                    lines.append(
                        f'    <circle cx="{_number(start + column + 0.5)}" '
                        f'cy="{_number(row + 0.5)}" '
                        f'r="{_number(radius)}" fill="{ink}" />'
                    )
    lines.extend(("  </g>", "</svg>"))
    return "\n".join(lines) + "\n"


def generate_svg() -> str:
    """Return the canonical D1 SVG generated from the checked-in bitmap."""
    return _generate_svg(_load_logo_data())


def _metadata(data: dict[str, Any], svg: str) -> dict[str, Any]:
    """Bind the D1 source data to the exact canonical SVG file bytes."""
    metadata = dict(data)
    metadata["canonical_svg_sha256"] = _sha256_bytes(svg.encode("utf-8"))
    return metadata


def main() -> None:
    data = _load_logo_data()
    svg = _generate_svg(data)
    _write(SVG_PATH, svg)
    _write(DATA_PATH, json.dumps(_metadata(data, svg), indent=2) + "\n")
    print(f"Wrote {SVG_PATH}")
    print(f"Wrote {DATA_PATH}")


if __name__ == "__main__":
    main()
