#!/usr/bin/env python3
"""Generate FourMM panel SVG and C++ coordinate header.

Run from project root:
    python3 scripts/generate_panel.py

Outputs:
    res/FourMM.svg  — panel SVG background
    src/layout.h    — C++ header with constexpr float coordinates in mm

Layout: 26 HP, 5 operator rows per column.
Row 0 (Freq):  Coarse knob + freq-mode toggle + Fine trimpot
Rows 1-4 (Level, Warp, Fold, FB): knob + CV jack + CV attenuverter
"""

import os

# ─────────────────────────────────────────────────────────────
#  Panel dimensions
# ─────────────────────────────────────────────────────────────

HP = 26
WIDTH_MM = HP * 5.08   # 132.08mm
HEIGHT_MM = 128.5

# Component sizes (for SVG placeholder circles)
SMALL_KNOB_RADIUS = 2.5    # RoundSmallBlackKnob / Trimpot
JACK_RADIUS = 3.2           # PJ301MPort
TOGGLE_W = 2.5              # CKSS half-width
TOGGLE_H = 5.0              # CKSS half-height

# ─────────────────────────────────────────────────────────────
#  Vertical layout
# ─────────────────────────────────────────────────────────────

Y_TITLE = 6.5
Y_ALGO = 15.0
Y_GLOBAL_ROW1 = 26.0       # V/Oct, Fine, VCA, Main
Y_GLOBAL_ROW2 = 36.0       # XM, FM
Y_OP_HEADER = 46.0          # "OP1" ... "OP4" column headers
Y_OPS_START = 55.0           # first operator row
Y_OPS_ROW_SPACING = 15.0    # 5 rows: 55, 70, 85, 100, 115

# ─────────────────────────────────────────────────────────────
#  Horizontal layout
# ─────────────────────────────────────────────────────────────

MARGIN_LEFT = 4.0
MARGIN_RIGHT = 4.0
LABEL_COL_X = 17.0          # right edge of row labels (right-aligned text)

OP_SECTION_LEFT = 20.0
OP_COL_PITCH = (WIDTH_MM - MARGIN_RIGHT - OP_SECTION_LEFT) / 4  # ~27.02mm


def op_knob_x(op_idx):
    """X center for operator knob (left sub-column, 0-3)."""
    return OP_SECTION_LEFT + OP_COL_PITCH * op_idx + 5.0


# Sub-column offsets from knob X
KNOB_TO_MIDDLE_DX = 8.5     # toggle (row 0) or CV jack (rows 1-4)
KNOB_TO_RIGHT_DX = 17.0     # Fine trimpot (row 0) or CV atten (rows 1-4)

# ─────────────────────────────────────────────────────────────
#  Global control positions
# ─────────────────────────────────────────────────────────────

GLOBAL_CONTROLS = {
    'algo_display': (WIDTH_MM / 2, Y_ALGO),
    # Row 1 (Y_GLOBAL_ROW1 = 22.0)
    'voct_jack':     (20.0, Y_GLOBAL_ROW1),
    'fine_tune_knob': (WIDTH_MM / 2, Y_GLOBAL_ROW1),
    'vca_knob':      (WIDTH_MM - 27.0, Y_GLOBAL_ROW1),   # moved left to make room for output
    'main_output':   (WIDTH_MM - 18.0, Y_GLOBAL_ROW1),   # right of VCA
    # Row 2 (Y_GLOBAL_ROW2 = 33.0)
    'xm_knob':       (20.0, Y_GLOBAL_ROW2),
    'xm_cv_jack':    (29.0, Y_GLOBAL_ROW2),
    'xm_cv_atten':   (37.0, Y_GLOBAL_ROW2),
    'fm_cv_jack':    (WIDTH_MM - 18.0 - 9.0, Y_GLOBAL_ROW2),
    'fm_cv_atten':   (WIDTH_MM - 18.0, Y_GLOBAL_ROW2),
}

# ─────────────────────────────────────────────────────────────
#  Operator layout
# ─────────────────────────────────────────────────────────────

# 5 knob rows (Coarse shares row 0 with toggle + Fine)
OP_ROW_NAMES = ['Freq', 'Level', 'Warp', 'Fold', 'FB']

# Knob names per row (for header comments)
OP_KNOB_NAMES = ['Coarse', 'Level', 'Warp', 'Fold', 'FB']

# Which rows have CV inputs (indices 1-4 = Level, Warp, Fold, FB)
CV_ROWS = [1, 2, 3, 4]


# ─────────────────────────────────────────────────────────────
#  SVG generation
# ─────────────────────────────────────────────────────────────

def _circle(x, y, r, fill, stroke):
    return (f'  <circle cx="{x:.1f}" cy="{y:.1f}" r="{r}" '
            f'fill="{fill}" stroke="{stroke}" stroke-width="0.3" />')


def _rect(x, y, w, h, fill, stroke):
    return (f'  <rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" '
            f'fill="{fill}" stroke="{stroke}" stroke-width="0.3" />')


def _line(x1, y1, x2, y2, stroke='#404060', width=0.2):
    return (f'  <line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" '
            f'stroke="{stroke}" stroke-width="{width}" />')


def generate_svg():
    """Generate the panel SVG string."""
    lines = []
    lines.append(f'<svg xmlns="http://www.w3.org/2000/svg" '
                 f'width="{WIDTH_MM}mm" height="{HEIGHT_MM}mm" '
                 f'viewBox="0 0 {WIDTH_MM} {HEIGHT_MM}">')

    # Background
    lines.append(f'  <rect width="{WIDTH_MM}" height="{HEIGHT_MM}" fill="#1a1a2e" />')

    # Algorithm display area rectangle (NanoVG draws the actual text)
    ax, ay = GLOBAL_CONTROLS['algo_display']
    algo_w = WIDTH_MM - 2 * 16
    lines.append(f'  <rect x="{ax - algo_w/2:.1f}" y="{ay - 4:.1f}" '
                 f'width="{algo_w:.1f}" height="8" '
                 f'rx="1" fill="#0a0a1a" stroke="#404060" stroke-width="0.3" />')

    # Global controls — placeholder circles
    for name, (x, y) in GLOBAL_CONTROLS.items():
        if name == 'algo_display':
            continue
        if 'jack' in name or 'output' in name:
            r, fill, stroke = JACK_RADIUS, '#222', '#888'
        elif 'atten' in name:
            r, fill, stroke = SMALL_KNOB_RADIUS, '#333', '#666'
        else:
            r, fill, stroke = SMALL_KNOB_RADIUS, '#333', '#aaa'
        lines.append(_circle(x, y, r, fill, stroke))

    # ── Operator section ──

    # Separator line above op section
    sep_y = Y_OP_HEADER - 3
    lines.append(_line(MARGIN_LEFT, sep_y, WIDTH_MM - MARGIN_RIGHT, sep_y))

    # Vertical separator lines between operator columns
    # Calculate midpoints between column centers
    col_mid_x = [op_knob_x(i) + KNOB_TO_MIDDLE_DX for i in range(4)]
    # Start from top edge of controls in first row (toggle height = 10mm total, so -5mm from center)
    v_sep_top = Y_OPS_START - TOGGLE_H
    # End at bottom edge of FB knobs (row 4) - visual knob radius is ~4mm in VCV Rack
    v_sep_bottom = Y_OPS_START + 4 * Y_OPS_ROW_SPACING + 4.0
    for i in range(3):  # 3 lines between 4 columns
        x = (col_mid_x[i] + col_mid_x[i + 1]) / 2
        lines.append(_line(x, v_sep_top, x, v_sep_bottom))

    # 5 rows x 4 columns
    for row in range(5):
        y = Y_OPS_START + row * Y_OPS_ROW_SPACING
        for col in range(4):
            kx = op_knob_x(col)
            mx = kx + KNOB_TO_MIDDLE_DX
            rx = kx + KNOB_TO_RIGHT_DX

            if row == 0:
                # Freq row: Coarse knob + toggle rect + Fine trimpot
                lines.append(_circle(kx, y, SMALL_KNOB_RADIUS, '#333', '#aaa'))
                lines.append(_rect(mx - TOGGLE_W, y - TOGGLE_H,
                                   TOGGLE_W * 2, TOGGLE_H * 2, '#444', '#888'))
                lines.append(_circle(rx, y, SMALL_KNOB_RADIUS, '#333', '#666'))
            else:
                # CV rows: knob + jack + atten
                lines.append(_circle(kx, y, SMALL_KNOB_RADIUS, '#333', '#aaa'))
                lines.append(_circle(mx, y, JACK_RADIUS, '#222', '#888'))
                lines.append(_circle(rx, y, SMALL_KNOB_RADIUS, '#333', '#666'))

    lines.append('</svg>')
    return '\n'.join(lines)


# ─────────────────────────────────────────────────────────────
#  C++ header generation
# ─────────────────────────────────────────────────────────────

def generate_coords_header():
    """Generate C++ header with widget coordinates in mm."""
    lines = []
    lines.append('#pragma once')
    lines.append('// Auto-generated by scripts/generate_panel.py -- do not edit manually.')
    lines.append('// All coordinates in mm (use mm2px() in VCV widget code).')
    lines.append('')
    lines.append('namespace fourmm_layout {')
    lines.append('')

    # Panel dimensions
    lines.append(f'constexpr float PANEL_WIDTH  = {WIDTH_MM:.2f}f;')
    lines.append(f'constexpr float PANEL_HEIGHT = {HEIGHT_MM:.1f}f;')
    lines.append('')

    # Label layout constants (for NanoVG text rendering)
    lines.append('// Label positions (for NanoVG rendering)')
    lines.append(f'constexpr float LABEL_COL_X   = {LABEL_COL_X:.1f}f;')
    lines.append(f'constexpr float OP_HEADER_Y   = {Y_OP_HEADER:.1f}f;')
    lines.append('')

    # Global controls
    lines.append('// Global controls')
    for name, (x, y) in GLOBAL_CONTROLS.items():
        cname = name.upper()
        lines.append(f'constexpr float {cname}_X = {x:.1f}f;')
        lines.append(f'constexpr float {cname}_Y = {y:.1f}f;')
    lines.append('')

    # Operator knob positions (5 rows: Coarse, Level, Warp, Fold, FB)
    lines.append('// Operator knob positions')
    lines.append('// Row index: 0=Coarse, 1=Level, 2=Warp, 3=Fold, 4=FB')
    for col in range(4):
        kx = op_knob_x(col)
        for row in range(5):
            y = Y_OPS_START + row * Y_OPS_ROW_SPACING
            lines.append(f'constexpr float OP{col+1}_KNOB{row}_X = {kx:.1f}f;')
            lines.append(f'constexpr float OP{col+1}_KNOB{row}_Y = {y:.1f}f;')
    lines.append('')

    # Freq-mode toggle positions (row 0, middle sub-column)
    lines.append('// Freq-mode toggle positions (row 0, middle sub-column)')
    for col in range(4):
        kx = op_knob_x(col)
        mx = kx + KNOB_TO_MIDDLE_DX
        y = Y_OPS_START  # row 0
        lines.append(f'constexpr float OP{col+1}_TOGGLE_X = {mx:.1f}f;')
        lines.append(f'constexpr float OP{col+1}_TOGGLE_Y = {y:.1f}f;')
    lines.append('')

    # Fine trimpot positions (row 0, right sub-column)
    lines.append('// Fine trimpot positions (row 0, right sub-column)')
    for col in range(4):
        kx = op_knob_x(col)
        rx = kx + KNOB_TO_RIGHT_DX
        y = Y_OPS_START  # row 0
        lines.append(f'constexpr float OP{col+1}_FINE_X = {rx:.1f}f;')
        lines.append(f'constexpr float OP{col+1}_FINE_Y = {y:.1f}f;')
    lines.append('')

    # Operator CV positions (rows 1-4, beside corresponding knob)
    lines.append('// Operator CV positions (jack + attenuverter beside knob)')
    lines.append('// CV index: 0=Level, 1=Warp, 2=Fold, 3=FB')
    lines.append('// CV row N is at the same Y as knob row N+1')
    for col in range(4):
        kx = op_knob_x(col)
        jx = kx + KNOB_TO_MIDDLE_DX
        ax = kx + KNOB_TO_RIGHT_DX
        for cv_idx, knob_row in enumerate(CV_ROWS):
            y = Y_OPS_START + knob_row * Y_OPS_ROW_SPACING
            lines.append(f'constexpr float OP{col+1}_CV{cv_idx}_JACK_X  = {jx:.1f}f;')
            lines.append(f'constexpr float OP{col+1}_CV{cv_idx}_JACK_Y  = {y:.1f}f;')
            lines.append(f'constexpr float OP{col+1}_CV{cv_idx}_ATTEN_X = {ax:.1f}f;')
            lines.append(f'constexpr float OP{col+1}_CV{cv_idx}_ATTEN_Y = {y:.1f}f;')
    lines.append('')

    # Fold type display positions (below fold knob, row 3)
    lines.append('// Fold type display positions (below fold knob)')
    fold_y = Y_OPS_START + 3 * Y_OPS_ROW_SPACING + 7.0  # 7mm below knob center
    for col in range(4):
        kx = op_knob_x(col)
        lines.append(f'constexpr float OP{col+1}_FOLD_TYPE_X = {kx:.1f}f;')
        lines.append(f'constexpr float OP{col+1}_FOLD_TYPE_Y = {fold_y:.1f}f;')
    lines.append('')

    # Middle sub-column X (for OP header centering)
    lines.append('// Middle sub-column X per operator (for OP header centering)')
    for col in range(4):
        mx = op_knob_x(col) + KNOB_TO_MIDDLE_DX
        lines.append(f'constexpr float OP{col+1}_MID_X = {mx:.1f}f;')
    lines.append('')

    lines.append('} // namespace fourmm_layout')
    lines.append('')
    return '\n'.join(lines)


# ─────────────────────────────────────────────────────────────
#  Main
# ─────────────────────────────────────────────────────────────

if __name__ == '__main__':
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)

    svg_path = os.path.join(project_dir, 'res', 'FourMM.svg')
    with open(svg_path, 'w') as f:
        f.write(generate_svg())
    print(f'Wrote {svg_path}')

    header_path = os.path.join(project_dir, 'src', 'FourMM', 'layout.h')
    with open(header_path, 'w') as f:
        f.write(generate_coords_header())
    print(f'Wrote {header_path}')
