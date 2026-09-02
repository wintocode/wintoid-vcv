#!/usr/bin/env python3
"""Generate the approved W6 wintoid wordmark.

The mark is deliberately hand-authored SVG geometry.  It has no font, glyph,
or external-image dependency, so the generated asset can be embedded by each
panel generator and converted by downstream panel consumers without needing
the VCV Rack font environment.
"""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA_PATH = ROOT / "scripts" / "assets" / "wintoid_logo_glyphs.json"
SVG_PATH = ROOT / "res" / "WintoidLogo.svg"

LOGO_BLUE = "#155f91"
LOGO_ORANGE = "#ed5b22"
STROKE_WIDTH = 5.5

# The viewBox includes two SVG units of clear space beyond the supplied
# visible bounds.  The panel generators use its x origin when deriving the
# shared right-edge placement.
VIEWBOX = (-4.75, -44.75, 189.5, 57.5)


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


def generate_svg() -> str:
    """Return the canonical W6 SVG, using only the approved geometry."""
    view_x, view_y, view_width, view_height = VIEWBOX
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" '
        f'width="{_number(view_width)}" height="{_number(view_height)}" '
        f'viewBox="{_number(view_x)} {_number(view_y)} '
        f'{_number(view_width)} {_number(view_height)}">',
        '  <title>lowercase wintoid W6 wordmark</title>',
        f'  <g id="wint-glyphs" fill="none" stroke="{LOGO_BLUE}" '
        f'stroke-width="{_number(STROKE_WIDTH)}" stroke-linecap="round" '
        'stroke-linejoin="round">',
        '    <path data-mark="w" d="M 0 -30 C 5 -30 5 0 10 0 C 15 0 15 -30 20 -30 C 25 -30 25 0 30 0 C 35 0 35 -30 40 -30" />',
        '    <path data-mark="i" d="M 48 0 L 48 -30" />',
        '    <path data-mark="n" d="M 60 0 L 60 -30 M 60 -20 C 60 -28 64 -30 68 -30 C 73 -30 76 -26.5 76 -20 L 76 0" />',
        '    <path data-mark="t" d="M 94 0 L 94 -34 M 87 -22 L 101 -22" />',
        '    <circle data-mark="wint-i-dot" cx="48" cy="-39" r="3.75" '
        f'fill="{LOGO_BLUE}" stroke="none" />',
        '  </g>',
        f'  <line id="wint-underline" x1="0" y1="8" x2="102" y2="8" '
        f'stroke="{LOGO_BLUE}" stroke-width="{_number(STROKE_WIDTH)}" '
        'stroke-linecap="butt" />',
        f'  <g id="oid-glyphs" fill="none" stroke="{LOGO_ORANGE}" '
        f'stroke-width="{_number(STROKE_WIDTH)}" stroke-linecap="round" '
        'stroke-linejoin="round">',
        '    <circle data-mark="oid-o" cx="118" cy="-15" r="15" '
        f'fill="none" stroke="{LOGO_ORANGE}" />',
        '    <path data-mark="i" d="M 143 0 L 143 -30" />',
        '    <circle data-mark="oid-i-dot" cx="143" cy="-39" r="3.75" '
        f'fill="{LOGO_ORANGE}" stroke="none" />',
        '    <circle data-mark="oid-d" cx="164.5" cy="-15" r="15" '
        f'fill="none" stroke="{LOGO_ORANGE}" />',
        '    <path data-mark="d-stem" d="M 179.5 0 L 179.5 -40" />',
        '  </g>',
        f'  <line id="oid-underline" x1="102" y1="8" x2="180" y2="8" '
        f'stroke="{LOGO_ORANGE}" stroke-width="{_number(STROKE_WIDTH)}" '
        'stroke-linecap="butt" />',
        '</svg>',
    ]
    return "\n".join(lines) + "\n"


def _metadata(svg: str) -> dict[str, object]:
    """Describe the generated asset and bind it to its exact file bytes."""
    return {
        "schema_version": 2,
        "mark": "W6",
        "canonical_svg_sha256": _sha256_bytes(svg.encode("utf-8")),
    }


def main() -> None:
    svg = generate_svg()
    _write(SVG_PATH, svg)
    _write(DATA_PATH, json.dumps(_metadata(svg), indent=2) + "\n")
    print(f"Wrote {SVG_PATH}")
    print(f"Wrote {DATA_PATH}")


if __name__ == "__main__":
    main()
