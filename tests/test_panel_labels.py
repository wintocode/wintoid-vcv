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
        cls.four_v2 = load_generator("four_v2")
        cls.vortex = load_generator("vortex")
        cls.vortex_v2 = load_generator("vortex_v2")
        cls.brink_v2 = load_generator("brink_v2")
        cls.four_source = (ROOT / "src" / "Four" / "Four.cpp").read_text()
        cls.vortex_source = (ROOT / "src" / "Vortex" / "Vortex.cpp").read_text()
        cls.vortex_v2_source = (
            ROOT / "src" / "VortexV2" / "VortexV2.cpp"
        ).read_text()
        cls.brink_v2_source = (
            ROOT / "src" / "BrinkV2" / "BrinkV2.cpp"
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
        for name in ("Four", "Vortex", "VortexV2", "Brink", "BrinkV2"):
            with self.subTest(module=name):
                source = (ROOT / "src" / name / f"{name}.cpp").read_text()
                self.assertNotIn("ScrewSilver", source)
                self.assertNotIn("ScrewBlack", source)

    def test_vortex_uses_the_generated_logo_font_size(self):
        self.assertIn("LOGO_FONT_SIZE", self.vortex_source)
        self.assertNotIn("nvgFontSize(args.vg, 10)", self.vortex_source)

    def test_vortex_v2_uses_the_canonical_logo_scale(self):
        self.assertEqual(0.4142, self.vortex_v2.LOGO_SCALE)
        self.assertIn("constexpr float LOGO_SCALE = 0.4142f;",
                      self.vortex_v2.generate_coords_header())
        self.assertNotIn('"wint"', self.vortex_v2_source)
        self.assertNotIn('"oid"', self.vortex_v2_source)

    def test_brink_v2_uses_the_canonical_logo_and_generated_label_schema(self):
        self.assertEqual(0.4142, self.brink_v2.LOGO_SCALE)
        self.assertEqual(7.0, self.brink_v2.TITLE_Y)
        self.assertEqual("Brink V2", self.brink_v2.PANEL_LABELS[0].text)
        self.assertEqual(
            "baseline",
            getattr(self.brink_v2.PANEL_LABELS[0], "vertical_align", None),
        )

        header = self.brink_v2.generate_coords_header()
        for contract in (
            "scripts/generate_panel_brink_v2.py",
            "constexpr float LOGO_SCALE = 0.4142f;",
            "constexpr float TITLE_Y = 7.0f;",
            "enum LabelVerticalAlign",
            "LABEL_VERTICAL_BASELINE",
            "static const LabelSpec PANEL_LABELS[]",
            "constexpr int PANEL_LABEL_COUNT",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, header)

        for contract in (
            "brink_v2_layout::PANEL_LABELS",
            "brink_v2_layout::PANEL_LABEL_COUNT",
            "label.vertical",
            "NVG_ALIGN_BASELINE",
        ):
            with self.subTest(source_contract=contract):
                self.assertIn(contract, self.brink_v2_source)
        self.assertNotIn('"wint"', self.brink_v2_source)
        self.assertNotIn('"oid"', self.brink_v2_source)

    def test_v2_branding_tracks_outer_group_edges_and_shared_top(self):
        logo_viewbox_x = 0.0
        logo_path_right_x = 33.0
        self.assertEqual(0.4142, self.four_v2.LOGO_SCALE)
        brink_left = self.brink_v2.CHANNEL_SECTION_RECTS[0]
        brink_right = self.brink_v2.CHANNEL_SECTION_RECTS[-1]
        panels = (
            ("Four V2", self.four_v2, self.four_v2.ROUTING_SECTION),
            ("Vortex V2", self.vortex_v2, self.vortex_v2.CONTROL_SECTION),
            (
                "Brink V2",
                self.brink_v2,
                (
                    brink_left[1],
                    brink_left[2],
                    brink_right[1] + brink_right[3] - brink_left[1],
                    brink_left[4],
                ),
            ),
        )
        top_edges = {round(section[1], 6) for _name, _panel, section in panels}
        self.assertEqual({10.3}, top_edges)

        for name, panel, section in panels:
            group_left = section[0]
            group_right = section[0] + section[2]
            logo_right = (
                panel.LOGO_TARGET_X
                + panel.LOGO_SCALE * (logo_path_right_x - logo_viewbox_x)
            )
            with self.subTest(module=name, edge="left"):
                self.assertAlmostEqual(group_left, panel.TITLE_X, places=6)
            with self.subTest(module=name, edge="right"):
                self.assertAlmostEqual(group_right, logo_right, places=6)

    def test_v2_logo_height_clears_the_shared_title_baseline(self):
        for name, panel in (
            ("Four V2", self.four_v2),
            ("Vortex V2", self.vortex_v2),
            ("Brink V2", self.brink_v2),
        ):
            with self.subTest(module=name):
                self.assertLess(
                    panel.LOGO_TARGET_Y + 7.0 * panel.LOGO_SCALE,
                    panel.TITLE_Y,
                )

    def test_vortex_v2_emits_and_consumes_label_geometry_constants(self):
        self.assertEqual(7.0, self.vortex_v2.TITLE_Y)
        self.assertEqual((20.0, 32.0, 44.0),
                         self.vortex_v2.CONTROL_ROW_YS)
        self.assertEqual(5.6, self.vortex_v2.OUTPUT_LABEL_OFFSET)

        header = self.vortex_v2.generate_coords_header()
        for contract in (
            "constexpr int PANEL_HP = 12;",
            "constexpr float PANEL_WIDTH = 60.96f;",
            "constexpr float TITLE_Y = 7.0f;",
            "constexpr float CUTOFF_KNOB_X = 30.48f;",
            "constexpr float CUTOFF_CV_X = 40.96f;",
            "constexpr float CUTOFF_ATTEN_X = 48.96f;",
            "constexpr float CUTOFF_KNOB_Y = 20.0f;",
            "constexpr float RESONANCE_KNOB_Y = 32.0f;",
            "constexpr float DRIVE_KNOB_Y = 44.0f;",
            "constexpr float OUTPUT_COLUMN_XS[3] = {12.0f, 30.48f, 48.96f};",
            "constexpr float OUTPUT_LABEL_OFFSET = 5.6f;",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, header)

        for expression in (
            "CUTOFF_KNOB_X",
            "CUTOFF_CV_X",
            "CUTOFF_ATTEN_X",
            "CUTOFF_KNOB_Y",
            "CUTOFF_CV_Y",
            "CUTOFF_ATTEN_Y",
            "RESONANCE_KNOB_Y",
            "RESONANCE_CV_Y",
            "RESONANCE_ATTEN_Y",
            "DRIVE_KNOB_Y",
            "DRIVE_CV_Y",
            "DRIVE_ATTEN_Y",
            "AUDIO_IN_LABEL_Y",
            "OUTPUT_LABEL_OFFSET",
        ):
            with self.subTest(expression=expression):
                self.assertIn(expression, self.vortex_v2_source)

        for removed in (
            "GLOBAL_SECTION_LABEL_X",
            "GLOBAL_SECTION_LABEL_Y",
            "OUTPUT_SECTION_LABEL_X",
            "OUTPUT_SECTION_LABEL_Y",
            "GLOBAL CONTROLS",
            "FILTER OUTPUTS",
        ):
            with self.subTest(removed=removed):
                self.assertNotIn(removed, self.vortex_v2_source)

        self.assertNotIn('"CV"', self.vortex_v2_source)

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
        self.assertIn("scripts/generate_panel_brink_v2.py",
                      self.brink_v2.generate_coords_header())


if __name__ == "__main__":
    unittest.main()
