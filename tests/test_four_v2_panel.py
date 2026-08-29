#!/usr/bin/env python3

import importlib.util
import json
import math
import pathlib
import re
import tempfile
import unittest
import xml.etree.ElementTree as ET

from panel_geometry import centered_stroke_outer_radius


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "generate_panel_four_v2.py"
LOGO_SCRIPT = ROOT / "scripts" / "generate_wintoid_logo.py"
PANEL_SVG = ROOT / "res" / "FourV2.svg"
LOGO_SVG = ROOT / "res" / "WintoidLogo.svg"
LAYOUT_HEADER = ROOT / "src" / "FourV2" / "layout.h"
GLYPH_DATA = ROOT / "scripts" / "assets" / "wintoid_logo_glyphs.json"

RACK_PIXELS_PER_MM = 15.0 / 5.08
RACK_SMALL_KNOB_RADIUS_MM = 22.67581 / (2.0 * RACK_PIXELS_PER_MM)
RACK_PORT_RADIUS_MM = 23.7 / (2.0 * RACK_PIXELS_PER_MM)
MINIMUM_EDGE_CLEARANCE_MM = 4.0
MINIMUM_LABEL_CLEARANCE_MM = 0.25

_FLOAT_TOKEN = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
_PATH_TOKEN_RE = re.compile(rf"[A-Za-z]|{_FLOAT_TOKEN}")
_HEADER_FLOAT_RE = re.compile(
    rf"constexpr float (?P<name>[A-Z0-9_]+) = (?P<value>{_FLOAT_TOKEN})f;"
)


def _emitted_header_floats(source):
    return {
        match.group("name"): float(match.group("value"))
        for match in _HEADER_FLOAT_RE.finditer(source)
    }


def _quadratic_x_values(start, control, end):
    values = [start, end]
    denominator = start - 2.0 * control + end
    if denominator:
        t = (start - control) / denominator
        if 0.0 < t < 1.0:
            values.append(
                (1.0 - t) ** 2 * start
                + 2.0 * (1.0 - t) * t * control
                + t ** 2 * end
            )
    return values


def _cubic_value(start, control_one, control_two, end, t):
    inverse = 1.0 - t
    return (
        inverse ** 3 * start
        + 3.0 * inverse ** 2 * t * control_one
        + 3.0 * inverse * t ** 2 * control_two
        + t ** 3 * end
    )


def _cubic_x_values(start, control_one, control_two, end):
    values = [start, end]
    a = -start + 3.0 * control_one - 3.0 * control_two + end
    b = 2.0 * (start - 2.0 * control_one + control_two)
    c = control_one - start
    if abs(a) < 1e-12:
        roots = [-c / b] if abs(b) >= 1e-12 else []
    else:
        discriminant = b * b - 4.0 * a * c
        if discriminant < 0.0:
            roots = []
        else:
            root = math.sqrt(discriminant)
            roots = [
                (-b + root) / (2.0 * a),
                (-b - root) / (2.0 * a),
            ]
    for t in roots:
        if 0.0 < t < 1.0:
            values.append(_cubic_value(start, control_one, control_two, end, t))
    return values


def _path_x_bounds(path_data):
    """Measure absolute/relative M/L/Q/C/Z SVG path geometry locally."""
    tokens = _PATH_TOKEN_RE.findall(path_data)
    remainder = _PATH_TOKEN_RE.sub("", path_data).replace(",", "")
    if remainder.strip():
        raise ValueError(f"unsupported SVG path syntax: {remainder!r}")

    arities = {"M": 2, "L": 2, "Q": 4, "C": 6}
    cursor = 0
    command = None
    current = (0.0, 0.0)
    contour_start = None
    xs = []
    while cursor < len(tokens):
        token = tokens[cursor]
        if token.isalpha():
            command = token
            cursor += 1
            if command.upper() == "Z":
                if contour_start is None:
                    raise ValueError("closed SVG path has no contour start")
                xs.extend((current[0], contour_start[0]))
                current = contour_start
                contour_start = None
                command = None
            continue
        if command is None or command.upper() == "Z":
            raise ValueError("SVG path numbers are missing a command")
        operation = command.upper()
        if operation not in arities:
            raise ValueError(f"unsupported SVG path command: {command}")
        arity = arities[operation]
        if cursor + arity > len(tokens):
            raise ValueError(f"incomplete SVG path command: {command}")
        arguments = tokens[cursor:cursor + arity]
        if any(argument.isalpha() for argument in arguments):
            raise ValueError(f"incomplete SVG path command: {command}")
        values = [float(argument) for argument in arguments]
        relative = command.islower()

        def point(x, y):
            if relative:
                return current[0] + x, current[1] + y
            return x, y

        if operation == "M":
            current = point(values[0], values[1])
            contour_start = current
            xs.append(current[0])
            command = "l" if relative else "L"
        elif operation == "L":
            current = point(values[0], values[1])
            xs.append(current[0])
        elif operation == "Q":
            control = point(values[0], values[1])
            end = point(values[2], values[3])
            xs.extend(_quadratic_x_values(current[0], control[0], end[0]))
            current = end
        elif operation == "C":
            control_one = point(values[0], values[1])
            control_two = point(values[2], values[3])
            end = point(values[4], values[5])
            xs.extend(
                _cubic_x_values(
                    current[0], control_one[0], control_two[0], end[0]
                )
            )
            current = end
        cursor += arity
    if not xs:
        raise ValueError("SVG path has no geometry")
    return min(xs), max(xs)


def _text_envelope(node):
    """Return a conservative SVG text envelope from rendered attributes."""
    if node.attrib.get("lengthAdjust") != "spacingAndGlyphs":
        raise AssertionError("patchbay labels must expose a rendered text envelope")
    if "textLength" not in node.attrib:
        raise AssertionError("patchbay labels must expose textLength")
    x = float(node.attrib["x"])
    width = float(node.attrib["textLength"])
    if width <= 0.0:
        raise AssertionError("patchbay labels must have positive textLength")
    baseline = float(node.attrib["y"])
    font_size = float(node.attrib["font-size"])
    anchor = node.attrib.get("text-anchor", "start")
    if anchor == "start":
        left, right = x, x + width
    elif anchor == "end":
        left, right = x - width, x
    elif anchor == "middle":
        left, right = x - width / 2.0, x + width / 2.0
    else:
        raise AssertionError(f"unsupported text anchor: {anchor}")
    # The top bound uses the full em; the bottom bound includes a conservative
    # quarter-em descender allowance without falsely reaching the lower shared
    # I/O port row.
    return left, baseline - font_size, right, baseline + font_size * 0.25


def _rectangle_circle_clearance(rectangle, cx, cy, radius):
    left, top, right, bottom = rectangle
    dx = max(left - cx, 0.0, cx - right)
    dy = max(top - cy, 0.0, cy - bottom)
    return math.hypot(dx, dy) - radius


def load_generator(path, name):
    if not path.exists():
        return None
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FourV2PanelTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.panel = load_generator(SCRIPT, "four_v2_panel")

    def require_panel(self):
        self.assertIsNotNone(
            self.panel,
            "Task 4 generator does not exist yet: scripts/generate_panel_four_v2.py",
        )
        return self.panel

    def test_dimensions_are_32_hp(self):
        panel = self.require_panel()
        self.assertEqual(32, panel.HP)
        self.assertAlmostEqual(162.56, panel.WIDTH_MM)
        self.assertAlmostEqual(128.5, panel.HEIGHT_MM)

    def test_sem_instrument_palette_is_exact(self):
        panel = self.require_panel()
        self.assertEqual("#ece8d9", panel.PANEL_IVORY)
        self.assertEqual("#242522", panel.LEGEND_CHARCOAL)
        self.assertEqual("#556d80", panel.SECTION_BLUE_GREY)
        self.assertEqual("#b7693c", panel.FUNCTION_ORANGE)
        self.assertEqual("#155f91", panel.LOGO_BLUE)
        self.assertEqual("#ed5b22", panel.LOGO_ORANGE)

    def test_four_operator_sections_and_patchbay_are_framed(self):
        panel = self.require_panel()
        self.assertEqual(4, len(panel.OPERATOR_SECTION_RECTS))
        self.assertIn("CV_PATCHBAY", panel.SECTION_RECTS)
        self.assertEqual(4, len(panel.PATCHBAY_COLUMN_XS))
        self.assertEqual(
            ("Output", "Warp", "Fold", "Feedback"),
            tuple(panel.PATCHBAY_ROWS),
        )

    def test_logo_is_outlined_and_split_only_between_glyph_groups(self):
        self.assertTrue(LOGO_SCRIPT.exists(), "logo generator is missing")
        self.assertTrue(LOGO_SVG.exists(), "canonical logo SVG is missing")
        logo = ET.parse(LOGO_SVG).getroot()
        self.assertEqual([], [node for node in logo.iter()
                              if node.tag.endswith("text")])
        self.assertEqual([], [node for node in logo.iter()
                              if node.tag.endswith("use")])
        ids = {node.attrib.get("id") for node in logo.iter()}
        self.assertTrue({"wint-glyphs", "oid-glyphs",
                         "wint-underline", "oid-underline"}.issubset(ids))
        self.assertEqual(7, len([node for node in logo.iter()
                                 if node.tag.endswith("path") and
                                 node.attrib.get("data-glyph")]))
        colours = {node.attrib.get("fill") for node in logo.iter()
                   if node.tag.endswith("path")}
        self.assertEqual({"#155f91", "#ed5b22"}, colours)

    def test_all_physical_components_clear_panel_edges(self):
        panel = self.require_panel()
        for name, x, y in panel.COMPONENTS:
            radius = panel.COMPONENT_RADII[name]
            with self.subTest(name=name):
                self.assertGreaterEqual(x - radius, MINIMUM_EDGE_CLEARANCE_MM)
                self.assertLessEqual(
                    x + radius, panel.WIDTH_MM - MINIMUM_EDGE_CLEARANCE_MM)
                self.assertGreaterEqual(y - radius, MINIMUM_EDGE_CLEARANCE_MM)
                self.assertLessEqual(
                    y + radius, panel.HEIGHT_MM - MINIMUM_EDGE_CLEARANCE_MM)

    def test_adjacent_real_rack_widget_envelopes_do_not_overlap(self):
        panel = self.require_panel()
        components = list(panel.COMPONENTS)
        for index, (name_a, ax, ay) in enumerate(components):
            ar = panel.COMPONENT_RADII[name_a]
            for name_b, bx, by in components[index + 1:]:
                br = panel.COMPONENT_RADII[name_b]
                distance = ((ax - bx) ** 2 + (ay - by) ** 2) ** 0.5
                with self.subTest(first=name_a, second=name_b):
                    self.assertGreaterEqual(distance + 1e-9, ar + br)

    def test_labels_clear_component_envelopes(self):
        panel = self.require_panel()
        self.assertGreaterEqual(panel.MINIMUM_LABEL_CLEARANCE_MM,
                                MINIMUM_LABEL_CLEARANCE_MM)
        for label, spec in panel.LABEL_CLEARANCES.items():
            with self.subTest(label=label):
                self.assertGreaterEqual(spec["clearance_mm"],
                                        MINIMUM_LABEL_CLEARANCE_MM)

    def test_routing_display_is_approximately_21_mm_high(self):
        panel = self.require_panel()
        x, y, width, height = panel.DISPLAY_RECTS["ROUTING"]
        self.assertGreaterEqual(width, 45.0)
        self.assertAlmostEqual(21.0, height, delta=1.0)
        self.assertGreaterEqual(x, 0.0)
        self.assertGreaterEqual(y, 0.0)
        self.assertLessEqual(x + width, panel.WIDTH_MM)
        self.assertLessEqual(y + height, panel.HEIGHT_MM)

    def test_frequency_displays_have_equal_dimensions_and_fit(self):
        panel = self.require_panel()
        rectangles = tuple(panel.FREQUENCY_DISPLAY_RECTS)
        self.assertEqual(4, len(rectangles))
        dimensions = {(round(rect[2], 6), round(rect[3], 6))
                      for rect in rectangles}
        self.assertEqual(1, len(dimensions))
        for x, y, width, height in rectangles:
            self.assertGreater(width, 0.0)
            self.assertGreater(height, 0.0)
            self.assertGreaterEqual(x, 0.0)
            self.assertGreaterEqual(y, 0.0)
            self.assertLessEqual(x + width, panel.WIDTH_MM)
            self.assertLessEqual(y + height, panel.HEIGHT_MM)

    def test_patchbay_rows_and_columns_align_to_operator_centres(self):
        panel = self.require_panel()
        self.assertEqual(tuple(panel.OPERATOR_CENTRES_X),
                         tuple(panel.PATCHBAY_COLUMN_XS))
        for row in panel.PATCHBAY_ROWS:
            self.assertEqual(4, len(panel.PATCHBAY_CELLS[row]))
            for cell, x in zip(panel.PATCHBAY_CELLS[row],
                               panel.PATCHBAY_COLUMN_XS):
                self.assertAlmostEqual(x, cell[0])

    def test_patchbay_row_labels_clear_every_real_rack_widget_envelope(self):
        panel = self.require_panel()
        root = ET.fromstring(PANEL_SVG.read_text(encoding="utf-8"))
        header = _emitted_header_floats(
            LAYOUT_HEADER.read_text(encoding="utf-8")
        )
        labels = {
            node.text: node
            for node in root.iter()
            if node.tag.endswith("text") and node.text in panel.PATCHBAY_ROWS
        }
        self.assertEqual(set(panel.PATCHBAY_ROWS), set(labels))

        circles = [
            node for node in root.iter()
            if node.tag.endswith("circle")
            and "cx" in node.attrib
            and "cy" in node.attrib
        ]
        components = []
        for row in panel.PATCHBAY_ROWS:
            slug = row.upper()
            for index in range(1, 5):
                for role, radius_name in (
                    ("CV_INPUT", "RACK_PORT_RADIUS"),
                    ("CV_ATTEN", "RACK_SMALL_KNOB_RADIUS"),
                ):
                    prefix = f"OP{index}_{slug}_{role}"
                    x = header[f"{prefix}_X"]
                    y = header[f"{prefix}_Y"]
                    matches = [
                        circle for circle in circles
                        if float(circle.attrib["cx"]) == x
                        and float(circle.attrib["cy"]) == y
                    ]
                    self.assertEqual(
                        1,
                        len(matches),
                        f"missing emitted SVG component for {prefix}",
                    )
                    components.append(
                        (prefix, x, y, header[radius_name])
                    )

        for row in panel.PATCHBAY_ROWS:
            rectangle = _text_envelope(labels[row])
            with self.subTest(label=row, boundary="left gutter"):
                self.assertGreaterEqual(rectangle[0], panel.PATCHBAY_SECTION[0])
                self.assertLessEqual(
                    rectangle[2],
                    panel.PATCHBAY_SECTION[0] + panel.PATCHBAY_SECTION[2],
                )
            for name, x, y, radius in components:
                clearance = _rectangle_circle_clearance(
                    rectangle, x, y, radius
                )
                with self.subTest(label=row, component=name):
                    self.assertGreaterEqual(
                        clearance + 1e-9, MINIMUM_LABEL_CLEARANCE_MM
                    )

    def test_main_output_uses_established_inverted_backplate(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())
        circles = [node for node in root.iter()
                   if node.tag.endswith("circle")]
        output = next(circle for circle in circles
                      if abs(float(circle.attrib["cx"]) -
                             panel.OUTPUT_COMPONENTS[0][1]) < 0.01 and
                         abs(float(circle.attrib["cy"]) -
                             panel.OUTPUT_COMPONENTS[0][2]) < 0.01)
        self.assertEqual("#39445f", output.attrib["fill"])
        self.assertEqual("#dfe7f3", output.attrib["stroke"])
        visible = (float(output.attrib["r"]) +
                   float(output.attrib["stroke-width"]) / 2.0 -
                   RACK_PORT_RADIUS_MM)
        self.assertAlmostEqual(0.725, visible, delta=0.01)

    def test_static_labels_and_section_headings_are_emitted(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())
        labels = {node.text for node in root.iter()
                  if node.tag.endswith("text") and node.text}
        for expected in ("FourV2", "ROUTING", "OP1", "OP2", "OP3", "OP4",
                         "CV PATCHBAY", "Output", "Warp", "Fold",
                         "Feedback", "PM DEPTH", "MASTER", "OVER"):
            with self.subTest(label=expected):
                self.assertIn(expected, labels)

    def test_generated_artifacts_match_generators(self):
        panel = self.require_panel()
        for path in (PANEL_SVG, LAYOUT_HEADER):
            self.assertTrue(path.exists(), f"missing generated file: {path}")
        self.assertEqual(panel.generate_svg(), PANEL_SVG.read_text())
        self.assertEqual(panel.generate_coords_header(), LAYOUT_HEADER.read_text())

    def test_panel_embeds_logo_groups_without_external_reference(self):
        panel = self.require_panel()
        svg = panel.generate_svg()
        self.assertIn('id="wint-glyphs"', svg)
        self.assertIn('id="oid-glyphs"', svg)
        self.assertNotIn("WintoidLogo.svg", svg)
        self.assertNotIn("<image", svg)

    def test_logo_underlines_match_measured_group_bounds(self):
        self.assertTrue(LOGO_SVG.exists(), "canonical logo SVG is missing")
        root = ET.parse(LOGO_SVG).getroot()
        for group_id, line_id in (("wint-glyphs", "wint-underline"),
                                  ("oid-glyphs", "oid-underline")):
            group = next(node for node in root.iter()
                         if node.attrib.get("id") == group_id)
            line = next(node for node in root.iter()
                        if node.attrib.get("id") == line_id)
            glyph_paths = [node for node in group.iter()
                           if node.tag.endswith("path") and
                           node.attrib.get("data-glyph")]
            self.assertTrue(glyph_paths)
            xs = []
            for path in glyph_paths:
                transform = path.attrib["transform"]
                match = re.fullmatch(
                    rf"translate\(({_FLOAT_TOKEN}) ({_FLOAT_TOKEN})\)",
                    transform,
                )
                self.assertIsNotNone(match)
                path_min_x, path_max_x = _path_x_bounds(path.attrib["d"])
                metadata_min_x, _metadata_min_y, metadata_max_x, _metadata_max_y = map(
                    float, path.attrib["data-bbox"].split(",")
                )
                tx = float(match.group(1))
                self.assertAlmostEqual(path_min_x, metadata_min_x, delta=0.01)
                self.assertAlmostEqual(path_max_x, metadata_max_x, delta=0.01)
                xs.extend((tx + path_min_x, tx + path_max_x))
            expected_min = min(xs)
            expected_max = max(xs)
            self.assertAlmostEqual(expected_min, float(line.attrib["x1"]),
                                   delta=0.01)
            self.assertAlmostEqual(expected_max, float(line.attrib["x2"]),
                                   delta=0.01)

    def test_logo_source_font_digest_matches_checked_in_glyph_data(self):
        self.assertTrue(GLYPH_DATA.exists(), "shaped glyph data is missing")
        data = json.loads(GLYPH_DATA.read_text(encoding="utf-8"))
        digest = data.get("source_font_sha256")
        self.assertIsInstance(digest, str)
        self.assertRegex(digest, r"[0-9a-f]{64}")
        logo_root = ET.parse(LOGO_SVG).getroot()
        self.assertEqual(digest, logo_root.attrib.get("data-source-font-sha256"))

    def test_panel_generator_rejects_logo_source_font_digest_mismatch(self):
        panel = self.require_panel()
        original_logo_path = panel.LOGO_PATH
        with tempfile.TemporaryDirectory() as directory:
            temporary_logo = pathlib.Path(directory) / "WintoidLogo.svg"
            source = LOGO_SVG.read_text(encoding="utf-8")
            digest = json.loads(GLYPH_DATA.read_text(encoding="utf-8"))["source_font_sha256"]
            temporary_logo.write_text(
                source.replace(
                    f'data-source-font-sha256="{digest}"',
                    f'data-source-font-sha256="{"0" * 64}"',
                    1,
                ),
                encoding="utf-8",
            )
            panel.LOGO_PATH = temporary_logo
            try:
                with self.assertRaisesRegex(RuntimeError, "source-font digest"):
                    panel._logo_elements()
            finally:
                panel.LOGO_PATH = original_logo_path


if __name__ == "__main__":
    unittest.main()
