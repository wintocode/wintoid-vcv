#!/usr/bin/env python3

import importlib.util
import pathlib
import unittest
import xml.etree.ElementTree as ET


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "generate_panel_vortex_v2.py"
PANEL_SVG = ROOT / "res" / "VortexV2.svg"
LAYOUT_HEADER = ROOT / "src" / "VortexV2" / "layout.h"
OUTPUT_FILL = "#39445f"
OUTPUT_STROKE = "#dfe7f3"
MINIMUM_LABEL_CLEARANCE_MM = 0.25


def load_generator(path, name):
    if not path.exists():
        return None
    spec = importlib.util.spec_from_file_location(f"panel_{name}", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def circles_by_position(svg):
    root = ET.fromstring(svg)
    return [node for node in root.iter() if node.tag.endswith("circle")]


def text_nodes(svg):
    root = ET.fromstring(svg)
    return [node for node in root.iter() if node.tag.endswith("text")]


def element_by_id(svg, identifier):
    root = ET.fromstring(svg)
    for node in root.iter():
        if node.attrib.get("id") == identifier:
            return node
    raise AssertionError(f"no SVG element found with id {identifier!r}")


def circle_at(circles, coordinate):
    x, y = coordinate
    for circle in circles:
        if (abs(float(circle.attrib["cx"]) - x) < 0.001 and
                abs(float(circle.attrib["cy"]) - y) < 0.001):
            return circle
    raise AssertionError(f"no output circle found at ({x}, {y})")


class VortexV2PanelTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.panel = load_generator(SCRIPT, "vortex_v2")

    def require_panel(self):
        self.assertIsNotNone(
            self.panel,
            "Task 3 generator is missing: "
            "scripts/generate_panel_vortex_v2.py",
        )
        return self.panel

    def test_dimensions_are_12_hp(self):
        panel = self.require_panel()
        self.assertEqual(12, panel.HP)
        self.assertAlmostEqual(60.96, panel.WIDTH_MM)
        self.assertAlmostEqual(128.5, panel.HEIGHT_MM)

    def test_control_triplets_are_horizontal_and_keep_their_order(self):
        panel = self.require_panel()
        expected = (
            ("cutoff", 30.48, 40.96, 48.96, 20.0),
            ("resonance", 30.48, 40.96, 48.96, 32.0),
            ("drive", 30.48, 40.96, 48.96, 44.0),
        )
        actual = tuple(
            (
                name,
                knob[0],
                cv[0],
                atten[0],
                knob[1],
            )
            for name, knob, cv, atten in panel.CONTROL_GROUPS
        )
        self.assertEqual(expected, actual)
        for _name, knob, cv, atten in panel.CONTROL_GROUPS:
            self.assertEqual(knob[1], cv[1])
            self.assertEqual(cv[1], atten[1])

    def test_audio_input_is_top_left_of_the_shifted_control_block(self):
        panel = self.require_panel()
        self.assertEqual((12.0, 20.0), (panel.AUDIO_IN_X, panel.AUDIO_IN_Y))
        self.assertEqual(30.48, panel.CONTROL_KNOB_X)
        self.assertEqual(40.96, panel.CONTROL_CV_X)
        self.assertEqual(48.96, panel.CONTROL_ATTEN_X)
        self.assertEqual(panel.OUTPUT_COLUMN_XS[0], panel.AUDIO_IN_X)
        self.assertEqual(panel.OUTPUT_COLUMN_XS[1], panel.CONTROL_KNOB_X)
        self.assertLess(panel.AUDIO_IN_X, panel.CONTROL_KNOB_X)
        self.assertLess(panel.AUDIO_IN_Y, panel.CONTROL_ROW_YS[1])

    def test_outputs_are_the_twelve_modes_in_row_major_order(self):
        panel = self.require_panel()
        expected_labels = (
            "LP 6dB", "LP 12dB", "LP 24dB",
            "HP 6dB", "HP 12dB", "HP 24dB",
            "BP", "BP+", "NOTCH", "NOTCH+", "AP", "AP+",
        )
        self.assertEqual(expected_labels, tuple(panel.OUTPUT_LABELS))
        self.assertEqual(
            expected_labels,
            tuple(name for name, _x, _y in panel.OUTPUT_COMPONENTS),
        )
        self.assertEqual(12, len(panel.OUTPUT_COMPONENTS))

    def test_output_grid_is_three_columns_by_four_rows(self):
        panel = self.require_panel()
        positions = [(round(x, 3), round(y, 3))
                     for _, x, y in panel.OUTPUT_COMPONENTS]
        self.assertEqual(12, len(positions))
        self.assertEqual(12, len(set(positions)))
        columns = sorted({x for x, _ in positions})
        rows = sorted({y for _, y in positions})
        self.assertEqual(3, len(columns))
        self.assertEqual(4, len(rows))
        self.assertEqual([12.0, 30.48, 48.96], columns)
        self.assertEqual(
            [(x, y) for y in rows for x in columns],
            positions,
        )

    def test_control_spacing_expands_into_the_top_of_the_output_section(self):
        panel = self.require_panel()
        self.assertEqual((20.0, 32.0, 44.0), panel.CONTROL_ROW_YS)
        self.assertEqual(
            (4.0, 11.0, 52.96, 39.5),
            panel.CONTROL_SECTION,
        )
        self.assertEqual(
            (4.0, 54.0, 52.96, 70.5),
            panel.OUTPUT_SECTION,
        )
        self.assertAlmostEqual(
            panel.CONTROL_SECTION[1] + panel.CONTROL_SECTION[3] + 3.5,
            panel.OUTPUT_SECTION[1],
        )
        self.assertEqual((67.0, 82.0, 97.0, 112.0), panel.OUTPUT_ROW_YS)
        self.assertAlmostEqual(
            panel.OUTPUT_SECTION[1] + panel.OUTPUT_SECTION[3],
            panel.HEIGHT_MM - 4.0,
        )

    def test_all_components_clear_edges_and_each_other(self):
        panel = self.require_panel()
        for name, x, y in panel.COMPONENTS:
            radius = panel.COMPONENT_RADII[name]
            with self.subTest(name=name):
                self.assertGreaterEqual(x - radius, 4.0)
                self.assertLessEqual(x + radius, panel.WIDTH_MM - 4.0)
                self.assertGreaterEqual(y - radius, 4.0)
                self.assertLessEqual(y + radius, panel.HEIGHT_MM - 4.0)
        for index, (name_a, ax, ay) in enumerate(panel.COMPONENTS):
            for name_b, bx, by in panel.COMPONENTS[index + 1:]:
                distance = ((ax - bx) ** 2 + (ay - by) ** 2) ** 0.5
                with self.subTest(first=name_a, second=name_b):
                    self.assertGreaterEqual(
                        distance,
                        panel.COMPONENT_RADII[name_a]
                        + panel.COMPONENT_RADII[name_b],
                    )

    def test_output_labels_clear_port_envelopes(self):
        panel = self.require_panel()
        self.assertEqual(set(panel.OUTPUT_LABELS),
                         set(panel.LABEL_CLEARANCES))
        for label in panel.OUTPUT_LABELS:
            with self.subTest(label=label):
                self.assertGreaterEqual(
                    panel.LABEL_CLEARANCES[label]["clearance_mm"],
                    MINIMUM_LABEL_CLEARANCE_MM,
                )

    def test_selective_one_point_typography_preserves_output_ring_clearance(self):
        panel = self.require_panel()
        self.assertAlmostEqual(2.25, panel.CONTROL_LABEL_FONT_SIZE)
        self.assertAlmostEqual(2.25, panel.AUDIO_IN_LABEL_FONT_SIZE)
        self.assertAlmostEqual(2.35, panel.OUTPUT_LABEL_FONT_SIZE)
        self.assertAlmostEqual(5.6, panel.OUTPUT_LABEL_OFFSET)
        self.assertAlmostEqual(6.6, panel.TITLE_FONT_SIZE)

        output_outer_radius = (
            panel.OUTPUT_BACKPLATE_RADIUS
            + panel.OUTPUT_STROKE_WIDTH / 2.0
        )
        for label, _x, y in panel.OUTPUT_COMPONENTS:
            baseline = y - panel.OUTPUT_LABEL_OFFSET
            label_bottom = baseline + panel.OUTPUT_LABEL_FONT_SIZE * 0.25
            with self.subTest(label=label):
                self.assertGreaterEqual(
                    y - output_outer_radius - label_bottom,
                    MINIMUM_LABEL_CLEARANCE_MM,
                )

    def test_output_circles_use_the_inverted_output_palette(self):
        panel = self.require_panel()
        circles = circles_by_position(panel.generate_svg())
        for label, x, y in panel.OUTPUT_COMPONENTS:
            with self.subTest(label=label):
                circle = circle_at(circles, (x, y))
                self.assertEqual(OUTPUT_FILL, circle.attrib["fill"])
                self.assertEqual(OUTPUT_STROKE, circle.attrib["stroke"])

    def test_control_pairs_have_four_v2_style_rounded_boxes(self):
        panel = self.require_panel()
        svg = panel.generate_svg()
        expected_ids = {
            "cutoff-cv-group": panel.CONTROL_GROUPS[0],
            "resonance-cv-group": panel.CONTROL_GROUPS[1],
            "drive-cv-group": panel.CONTROL_GROUPS[2],
        }
        self.assertEqual(
            set(expected_ids),
            set(panel.PAIR_GROUP_RECT_BY_ID),
        )
        for identifier, (_name, _knob, cv, atten) in expected_ids.items():
            group = element_by_id(svg, identifier)
            self.assertEqual("rect", group.tag.rsplit("}", 1)[-1])
            self.assertEqual("none", group.attrib["fill"])
            self.assertEqual("#556d80", group.attrib["stroke"])
            self.assertGreater(float(group.attrib["rx"]), 0.0)
            x = float(group.attrib["x"])
            y = float(group.attrib["y"])
            width = float(group.attrib["width"])
            height = float(group.attrib["height"])
            cv_x, cv_y = cv
            atten_x, atten_y = atten
            self.assertLessEqual(x, cv_x - panel.RACK_PORT_RADIUS)
            self.assertGreaterEqual(
                x + width,
                atten_x + panel.RACK_SMALL_KNOB_RADIUS,
            )
            self.assertLessEqual(y, cv_y - panel.RACK_PORT_RADIUS)
            self.assertGreaterEqual(
                y + height,
                atten_y + panel.RACK_SMALL_KNOB_RADIUS,
            )

    def test_svg_matches_four_v2_identity_and_removes_section_headings(self):
        panel = self.require_panel()
        svg = panel.generate_svg()
        labels = text_nodes(svg)
        title = next(node for node in labels if node.text == "Vortex V2")
        self.assertEqual("700", title.attrib.get("font-weight"))
        label_text = {node.text for node in labels if node.text}
        self.assertNotIn("GLOBAL CONTROLS", label_text)
        self.assertNotIn("FILTER OUTPUTS", label_text)
        for expected in ("Vortex V2", "CUTOFF", "RESO", "DRIVE", "IN",
                          *panel.OUTPUT_LABELS):
            with self.subTest(label=expected):
                self.assertIn(expected, label_text)

    def test_svg_omits_cv_captions(self):
        panel = self.require_panel()
        svg = panel.generate_svg()
        label_text = {node.text for node in text_nodes(svg) if node.text}
        self.assertNotIn("CV", label_text)
        self.assertNotIn(">CV<", svg)

    def test_svg_embeds_the_canonical_four_v2_logo(self):
        panel = self.require_panel()
        svg = panel.generate_svg()
        self.assertIn('id="wintoid-logo"', svg)
        self.assertIn('id="wint-glyphs"', svg)
        self.assertIn('id="oid-glyphs"', svg)
        self.assertIn('id="wint-underline"', svg)
        self.assertIn('id="oid-underline"', svg)
        self.assertNotIn("WintoidLogo.svg", svg)
        self.assertNotIn("<image", svg)

    def test_svg_uses_filled_four_v2_style_sections(self):
        panel = self.require_panel()
        svg = panel.generate_svg()
        for identifier, fill in (
            ("controls-section", "#e3e0d1"),
            ("outputs-section", "#e7e3d4"),
        ):
            with self.subTest(section=identifier):
                section = element_by_id(svg, identifier)
                self.assertEqual(fill, section.attrib["fill"])
                self.assertEqual("#556d80", section.attrib["stroke"])
                self.assertGreater(float(section.attrib["rx"]), 0.0)

    def test_generated_artifacts_match_checked_in_files(self):
        panel = self.require_panel()
        self.assertTrue(PANEL_SVG.exists(),
                        "Task 3 panel SVG is missing: res/VortexV2.svg")
        self.assertTrue(
            LAYOUT_HEADER.exists(),
            "Task 3 layout header is missing: src/VortexV2/layout.h",
        )
        self.assertEqual(panel.generate_svg(),
                         PANEL_SVG.read_text(encoding="utf-8"))
        self.assertEqual(panel.generate_coords_header(),
                         LAYOUT_HEADER.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
