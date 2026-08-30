#!/usr/bin/env python3

import importlib.util
import pathlib
import unittest

from panel_geometry import centered_stroke_outer_radius


ROOT = pathlib.Path(__file__).resolve().parents[1]
PIXELS_PER_MM = 15.0 / 5.08
RACK_SMALL_KNOB_RADIUS_MM = 22.67581 / (2.0 * PIXELS_PER_MM)
RACK_PORT_RADIUS_MM = 23.7 / (2.0 * PIXELS_PER_MM)
MINIMUM_LABEL_CLEARANCE_MM = 1.0


def load_generator(name):
    path = ROOT / "scripts" / f"generate_panel_{name}.py"
    spec = importlib.util.spec_from_file_location(f"panel_{name}", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class PanelLabelTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.four = load_generator("four")
        cls.vortex = load_generator("vortex")
        cls.vortex_v2 = load_generator("vortex_v2")
        cls.four_source = (ROOT / "src" / "Four" / "Four.cpp").read_text()
        cls.vortex_source = (ROOT / "src" / "Vortex" / "Vortex.cpp").read_text()
        cls.vortex_v2_source = (
            ROOT / "src" / "VortexV2" / "VortexV2.cpp"
        ).read_text()

    def test_four_global_label_offsets_clear_the_real_controls(self):
        self.assertEqual(5.0, self.four.GLOBAL_KNOB_LABEL_OFFSET)
        self.assertEqual(5.1, self.four.GLOBAL_PORT_LABEL_OFFSET)
        self.assertGreaterEqual(
            self.four.GLOBAL_KNOB_LABEL_OFFSET - RACK_SMALL_KNOB_RADIUS_MM,
            MINIMUM_LABEL_CLEARANCE_MM,
        )
        self.assertGreaterEqual(
            self.four.GLOBAL_PORT_LABEL_OFFSET - RACK_PORT_RADIUS_MM,
            MINIMUM_LABEL_CLEARANCE_MM,
        )

    def test_four_emits_the_label_offsets_in_its_header(self):
        header = self.four.generate_coords_header()
        self.assertIn("constexpr float GLOBAL_KNOB_LABEL_OFFSET = 5.0f;", header)
        self.assertIn("constexpr float GLOBAL_PORT_LABEL_OFFSET = 5.1f;", header)

    def test_four_emits_and_consumes_label_geometry_constants(self):
        self.assertEqual(8.0, self.four.Y_TITLE)
        self.assertEqual(124.5, self.four.LOGO_BASELINE_Y)
        self.assertEqual(2.5, self.four.LOGO_UNDERLINE_OFFSET)
        self.assertEqual(10.0, self.four.LOGO_FONT_SIZE)

        header = self.four.generate_coords_header()
        self.assertIn("constexpr float TITLE_Y = 8.0f;", header)
        self.assertIn("constexpr float LOGO_BASELINE_Y = 124.5f;", header)
        self.assertIn(
            "constexpr float LOGO_UNDERLINE_OFFSET = 2.5f;",
            header,
        )
        self.assertIn("constexpr float LOGO_FONT_SIZE = 10.0f;", header)

        for expression in (
            "mm2px(TITLE_Y)",
            "mm2px(LOGO_BASELINE_Y)",
            "mm2px(LOGO_UNDERLINE_OFFSET)",
            "nvgFontSize(args.vg, LOGO_FONT_SIZE)",
        ):
            with self.subTest(expression=expression):
                self.assertIn(expression, self.four_source)

        self.assertNotIn("mm2px(8.0f)", self.four_source)
        self.assertNotIn("mm2px(124.5f)", self.four_source)
        self.assertNotIn("mm2px(2.5f)", self.four_source)
        self.assertNotIn("nvgFontSize(args.vg, 10)", self.four_source)

    def test_four_consumes_generated_offsets_and_labels_its_output(self):
        self.assertIn("mm2px(GLOBAL_KNOB_LABEL_OFFSET)", self.four_source)
        self.assertIn("mm2px(GLOBAL_PORT_LABEL_OFFSET)", self.four_source)
        self.assertIn("NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE", self.four_source)
        self.assertIn("mm2px(MAIN_OUTPUT_X) + portOff", self.four_source)
        self.assertIn('"Out"', self.four_source)

    def test_four_output_label_allowance_stays_inside_the_panel(self):
        main_output_x = self.four.GLOBAL_CONTROLS["main_output"][0]
        conservative_right_edge = (
            main_output_x + self.four.GLOBAL_PORT_LABEL_OFFSET + 7.0
        )
        self.assertLessEqual(conservative_right_edge, self.four.WIDTH_MM)

    def test_four_output_label_clears_the_complete_ring(self):
        outer_radius = centered_stroke_outer_radius(
            self.four.OUTPUT_BACKPLATE_RADIUS,
            self.four.OUTPUT_STROKE_WIDTH,
        )
        self.assertGreaterEqual(
            self.four.GLOBAL_PORT_LABEL_OFFSET - outer_radius,
            0.25,
        )

    def test_vortex_audio_labels_clear_the_complete_ring(self):
        outer_radius = centered_stroke_outer_radius(
            self.vortex.OUTPUT_BACKPLATE_RADIUS,
            self.vortex.OUTPUT_STROKE_WIDTH,
        )
        self.assertGreaterEqual(
            self.vortex.AUDIO_LABEL_OFFSET - outer_radius,
            0.25,
        )

        vortex_header = self.vortex.generate_coords_header()
        self.assertIn(
            "constexpr float AUDIO_LABEL_OFFSET = 5.0f;",
            vortex_header,
        )
        self.assertIn("AUDIO_IN_Y - AUDIO_LABEL_OFFSET", self.vortex_source)
        self.assertIn("AUDIO_OUT_Y - AUDIO_LABEL_OFFSET", self.vortex_source)

    def test_vortex_logo_font_size_is_generator_owned(self):
        self.assertEqual(10.0, self.vortex.LOGO_FONT_SIZE)
        self.assertIn("constexpr float LOGO_FONT_SIZE = 10.0f;",
                      self.vortex.generate_coords_header())

    def test_vortex_emits_and_consumes_label_geometry_constants(self):
        self.assertEqual(8.0, self.vortex.TITLE_Y)
        self.assertEqual(124.5, self.vortex.LOGO_BASELINE_Y)
        self.assertEqual(2.5, self.vortex.LOGO_UNDERLINE_OFFSET)
        self.assertEqual(6.0, self.vortex.KNOB_LABEL_OFFSET)

        header = self.vortex.generate_coords_header()
        self.assertIn("constexpr float TITLE_Y = 8.0f;", header)
        self.assertIn("constexpr float LOGO_BASELINE_Y = 124.5f;", header)
        self.assertIn(
            "constexpr float LOGO_UNDERLINE_OFFSET = 2.5f;",
            header,
        )
        self.assertIn("constexpr float KNOB_LABEL_OFFSET = 6.0f;", header)

        for expression in (
            "mm2px(TITLE_Y)",
            "mm2px(LOGO_BASELINE_Y)",
            "mm2px(LOGO_UNDERLINE_OFFSET)",
            "CUTOFF_KNOB_Y - KNOB_LABEL_OFFSET",
            "RESONANCE_KNOB_Y - KNOB_LABEL_OFFSET",
            "DRIVE_KNOB_Y - KNOB_LABEL_OFFSET",
        ):
            with self.subTest(expression=expression):
                self.assertIn(expression, self.vortex_source)

        self.assertNotIn("mm2px(8.0f)", self.vortex_source)
        self.assertNotIn("mm2px(124.5f)", self.vortex_source)
        self.assertNotIn("mm2px(2.5f)", self.vortex_source)
        self.assertNotIn("KNOB_Y - 6.0f", self.vortex_source)

    def test_all_logos_share_one_size(self):
        # Four and Vortex use their generated constants; Brink's existing
        # source remains the third 10 px logo implementation.
        self.assertEqual(10.0, self.four.LOGO_FONT_SIZE)
        self.assertEqual(10.0, self.vortex.LOGO_FONT_SIZE)
        self.assertIn("nvgFontSize(args.vg, LOGO_FONT_SIZE)",
                      self.four_source)
        self.assertIn("nvgFontSize(args.vg, LOGO_FONT_SIZE)",
                      self.vortex_source)
        brink_source = (ROOT / "src" / "Brink" / "Brink.cpp").read_text()
        self.assertIn("nvgFontSize(args.vg, 10);", brink_source)

    def test_modules_do_not_draw_decorative_screws(self):
        for name in ("Four", "Vortex", "VortexV2", "Brink"):
            with self.subTest(module=name):
                source = (ROOT / "src" / name / f"{name}.cpp").read_text()
                self.assertNotIn("ScrewSilver", source)
                self.assertNotIn("ScrewBlack", source)

    def test_vortex_uses_the_generated_logo_font_size(self):
        self.assertIn("LOGO_FONT_SIZE", self.vortex_source)
        self.assertNotIn("nvgFontSize(args.vg, 10)", self.vortex_source)

    def test_vortex_v2_uses_the_generated_logo_font_size(self):
        self.assertEqual(3.0, self.vortex_v2.LOGO_FONT_SIZE)
        self.assertIn("constexpr float LOGO_FONT_SIZE = 3.0f;",
                      self.vortex_v2.generate_coords_header())
        self.assertIn("drawLabel(args, LOGO_X, LOGO_Y, LOGO_FONT_SIZE,",
                      self.vortex_v2_source)

    def test_four_generator_documentation_names_the_real_paths(self):
        for path in ("scripts/generate_panel_four.py",
                     "res/Four.svg",
                     "src/Four/layout.h"):
            with self.subTest(path=path):
                self.assertIn(path, self.four.__doc__)

    def test_generated_headers_name_their_generators(self):
        self.assertIn("scripts/generate_panel_four.py",
                      self.four.generate_coords_header())
        self.assertIn("scripts/generate_panel_vortex.py",
                      self.vortex.generate_coords_header())
        self.assertIn("scripts/generate_panel_vortex_v2.py",
                      self.vortex_v2.generate_coords_header())


if __name__ == "__main__":
    unittest.main()
