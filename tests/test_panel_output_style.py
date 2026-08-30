#!/usr/bin/env python3

import importlib.util
import pathlib
import unittest
import xml.etree.ElementTree as ET

from panel_geometry import visible_output_material


ROOT = pathlib.Path(__file__).resolve().parents[1]
OUTPUT_FILL = "#39445f"
OUTPUT_STROKE = "#dfe7f3"
RACK_PIXELS_PER_MM = 15.0 / 5.08
RACK_PORT_RADIUS_MM = 23.7 / (2.0 * RACK_PIXELS_PER_MM)
MINIMUM_VISIBLE_OUTPUT_RING_MM = 0.60


def load_generator(name):
    path = ROOT / "scripts" / f"generate_panel_{name}.py"
    spec = importlib.util.spec_from_file_location(f"panel_{name}", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def circles_by_position(svg):
    root = ET.fromstring(svg)
    return [circle for circle in root if circle.tag.endswith("circle")]


def circle_at(circles, coordinate):
    x, y = coordinate
    for circle in circles:
        if (abs(float(circle.attrib["cx"]) - x) < 0.1 and
                abs(float(circle.attrib["cy"]) - y) < 0.1):
            return circle
    raise AssertionError(f"no circle found at ({x}, {y})")


class PanelOutputStyleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.four = load_generator("four")
        cls.four_v2 = load_generator("four_v2")
        cls.vortex = load_generator("vortex")
        cls.vortex_v2 = load_generator("vortex_v2")
        cls.brink = load_generator("brink")

    def assert_output_style(self, circles, module, coordinate):
        circle = circle_at(circles, coordinate)
        self.assertEqual(OUTPUT_FILL, circle.attrib["fill"])
        self.assertEqual(OUTPUT_STROKE, circle.attrib["stroke"])
        visible_material_mm = visible_output_material(
            float(circle.attrib["r"]),
            float(circle.attrib["stroke-width"]),
            RACK_PORT_RADIUS_MM,
        )
        # Brink's existing SVG formatter rounds its radius to two decimals;
        # permit the resulting half-step (0.005 mm) precision tolerance.
        self.assertAlmostEqual(0.725, visible_material_mm, delta=0.005)
        self.assertGreaterEqual(
            visible_material_mm, MINIMUM_VISIBLE_OUTPUT_RING_MM)

    def assert_input_style(self, circles, coordinate):
        circle = circle_at(circles, coordinate)
        self.assertNotEqual(OUTPUT_FILL, circle.attrib["fill"])
        self.assertNotEqual(OUTPUT_STROKE, circle.attrib["stroke"])

    def test_four_main_output_has_inverted_backplate(self):
        circles = circles_by_position(self.four.generate_svg())
        self.assert_output_style(
            circles, self.four, self.four.GLOBAL_CONTROLS["main_output"])
        self.assert_input_style(
            circles, self.four.GLOBAL_CONTROLS["voct_jack"])

    def test_four_v2_main_output_has_inverted_backplate(self):
        circles = circles_by_position(self.four_v2.generate_svg())
        self.assert_output_style(
            circles, self.four_v2, self.four_v2.OUTPUT_COMPONENTS[0][1:])
        self.assert_input_style(
            circles, self.four_v2.INPUT_COMPONENTS[0][1:])

    def test_vortex_audio_output_has_inverted_backplate(self):
        circles = circles_by_position(self.vortex.generate_svg())
        self.assert_output_style(
            circles, self.vortex,
            (self.vortex.AUDIO_OUT_X, self.vortex.Y_AUDIO_IO))
        self.assert_input_style(
            circles, (self.vortex.AUDIO_IN_X, self.vortex.Y_AUDIO_IO))

    def test_vortex_v2_outputs_have_inverted_backplates(self):
        circles = circles_by_position(self.vortex_v2.generate_svg())
        for name, x, y in self.vortex_v2.OUTPUT_COMPONENTS:
            with self.subTest(output=name):
                self.assert_output_style(circles, self.vortex_v2, (x, y))

    def test_generators_preserve_current_panel_layout_contracts(self):
        self.assertAlmostEqual(16.0, self.four.Y_ALGO)
        self.assertEqual(6, self.vortex.HP)
        self.assertAlmostEqual(30.48, self.vortex.WIDTH_MM)
        vortex_header = self.vortex.generate_coords_header()
        self.assertIn("CV_CUTOFF_JACK_X", vortex_header)
        self.assertIn("constexpr float AUDIO_OUT_X = 21.5f;", vortex_header)

    def test_checked_in_panel_artifacts_match_generators(self):
        generated = (
            ("Four", self.four, ""),
            ("FourV2", self.four_v2, ""),
            ("Brink", self.brink, "\n"),
            ("Vortex", self.vortex, ""),
            ("VortexV2", self.vortex_v2, ""),
        )
        for name, module, svg_suffix in generated:
            with self.subTest(panel=name):
                self.assertEqual(
                    module.generate_svg() + svg_suffix,
                    (ROOT / "res" / f"{name}.svg").read_text(),
                )
                self.assertEqual(
                    module.generate_coords_header(),
                    (ROOT / "src" / name / "layout.h").read_text(),
                )

    def test_brink_outputs_have_inverted_backplates_without_recolouring_inputs(self):
        circles = circles_by_position(self.brink.generate_svg())
        outputs = (
            "A_POSITION", "A_INSIDE", "A_OUTSIDE", "A_LOW_UP",
            "A_HIGH_UP", "A_LOW_DOWN", "A_HIGH_DOWN",
            "B_POSITION", "B_INSIDE", "B_OUTSIDE", "B_LOW_UP",
            "B_HIGH_UP", "B_LOW_DOWN", "B_HIGH_DOWN",
            "AND_OUTPUT", "OR_OUTPUT", "XOR_OUTPUT", "STATE_OUTPUT",
        )
        for name in outputs:
            with self.subTest(name=name):
                self.assert_output_style(
                    circles, self.brink, self.brink.COORDINATES[name])

        for name in ("A_SIGNAL", "A_CENTER_CV", "A_WIDTH_CV",
                     "B_SIGNAL", "B_CENTER_CV", "B_WIDTH_CV"):
            with self.subTest(name=name):
                self.assert_input_style(circles, self.brink.COORDINATES[name])


if __name__ == "__main__":
    unittest.main()
