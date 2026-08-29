#!/usr/bin/env python3

import importlib.util
import pathlib
import re
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
                match = re.fullmatch(r"translate\(([-0-9.]+) ([-0-9.]+)\)",
                                     transform)
                self.assertIsNotNone(match)
                min_x, _min_y, max_x, _max_y = map(
                    float, path.attrib["data-bbox"].split(","))
                tx = float(match.group(1))
                xs.extend((tx + min_x, tx + max_x))
            expected_min = min(xs)
            expected_max = max(xs)
            self.assertAlmostEqual(expected_min, float(line.attrib["x1"]),
                                   delta=0.01)
            self.assertAlmostEqual(expected_max, float(line.attrib["x2"]),
                                   delta=0.01)

    def test_glyph_data_has_source_font_digest(self):
        self.assertTrue(GLYPH_DATA.exists(), "shaped glyph data is missing")
        data = GLYPH_DATA.read_text()
        self.assertRegex(data, r'"source_font_sha256"\s*:\s*"[0-9a-f]{64}"')
        self.assertEqual(64, len(re.search(
            r'"source_font_sha256"\s*:\s*"([0-9a-f]{64})"', data).group(1)))


if __name__ == "__main__":
    unittest.main()
