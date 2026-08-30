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

    def test_dimensions_are_20_hp(self):
        panel = self.require_panel()
        self.assertEqual(20, panel.HP)
        self.assertAlmostEqual(101.6, panel.WIDTH_MM)
        self.assertAlmostEqual(128.5, panel.HEIGHT_MM)

    def test_outputs_are_the_twelve_modes_in_row_major_order(self):
        panel = self.require_panel()
        expected_labels = (
            "LP 6dB", "LP 12dB", "LP 24dB",
            "HP 6dB", "HP 12dB", "HP 24dB",
            "BP", "BP+", "Notch", "Notch+", "AP", "AP+",
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
        self.assertEqual(
            [(x, y) for y in rows for x in columns],
            positions,
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

    def test_output_circles_use_the_inverted_output_palette(self):
        panel = self.require_panel()
        circles = circles_by_position(panel.generate_svg())
        for label, x, y in panel.OUTPUT_COMPONENTS:
            with self.subTest(label=label):
                circle = circle_at(circles, (x, y))
                self.assertEqual(OUTPUT_FILL, circle.attrib["fill"])
                self.assertEqual(OUTPUT_STROKE, circle.attrib["stroke"])

    def test_svg_defers_all_labels_to_the_runtime_overlay(self):
        panel = self.require_panel()
        self.assertEqual([], text_nodes(panel.generate_svg()))

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
