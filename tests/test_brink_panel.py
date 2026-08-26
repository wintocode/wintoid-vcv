import importlib.util
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "generate_panel_brink.py"


class BrinkPanelTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("brink_panel", SCRIPT)
        cls.panel = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.panel)

    def test_dimensions_are_twelve_hp(self):
        self.assertEqual(self.panel.HP, 12)
        self.assertAlmostEqual(self.panel.WIDTH_MM, 60.96)
        self.assertAlmostEqual(self.panel.HEIGHT_MM, 128.5)

    def test_channel_centres_are_mirrored(self):
        self.assertAlmostEqual(self.panel.CHANNEL_A_X + self.panel.CHANNEL_B_X,
                               self.panel.WIDTH_MM)

    def test_generated_outputs_have_required_identity(self):
        svg = self.panel.generate_svg()
        header = self.panel.generate_header()
        self.assertIn('width="60.96mm"', svg)
        self.assertIn('height="128.5mm"', svg)
        self.assertIn('fill="#1a1a2e"', svg)
        self.assertIn('constexpr float PANEL_WIDTH = 60.96f;', header)
        self.assertNotIn("WORKING TITLE", svg)

    def test_generation_is_deterministic(self):
        self.assertEqual(self.panel.generate_svg(), self.panel.generate_svg())
        self.assertEqual(self.panel.generate_header(), self.panel.generate_header())

    def test_components_clear_edges_and_logo(self):
        for _name, x, y in self.panel.COMPONENTS:
            self.assertGreaterEqual(x, 4.0)
            self.assertLessEqual(x, self.panel.WIDTH_MM - 4.0)
            self.assertGreaterEqual(y, 14.0)
            self.assertLessEqual(y, 116.0)

    def test_channel_fields_have_no_full_width_inner_separators(self):
        svg = self.panel.generate_svg()
        separators = re.findall(
            r'<line x1="4\.00" y1="[^"]+" x2="56\.96" y2="[^"]+"',
            svg,
        )
        self.assertEqual([], separators)

    def test_channel_fields_end_before_shared_logic_row(self):
        svg = self.panel.generate_svg()
        fields = re.findall(
            r'<rect x="[^"]+" y="([^"]+)" width="[^"]+" '
            r'height="([^"]+)" fill="#1d2036"',
            svg,
        )
        self.assertEqual(2, len(fields))

        pixels_per_mm = 15.0 / 5.08
        rack_port_radius = 23.7 / (2.0 * pixels_per_mm)
        last_channel_port_bottom = self.panel.Y_EVENTS_DOWN + rack_port_radius
        shared_logic_port_top = self.panel.Y_LOGIC - rack_port_radius
        for top, height in fields:
            bottom = float(top) + float(height)
            self.assertGreater(bottom, last_channel_port_bottom)
            self.assertLess(bottom, shared_logic_port_top)

    def test_position_rails_span_knob_top_to_width_cv_bottom(self):
        pixels_per_mm = 15.0 / 5.08
        knob_radius = 22.67581 / (2.0 * pixels_per_mm)
        port_radius = 23.7 / (2.0 * pixels_per_mm)
        expected_top = self.panel.Y_KNOBS - knob_radius
        expected_bottom = self.panel.Y_WIDTH_CV + port_radius

        for name in ("A_POSITION_RAIL", "B_POSITION_RAIL"):
            with self.subTest(name=name):
                _x, center_y = self.panel.COORDINATES[name]
                actual_top = center_y - self.panel.POSITION_RAIL_HEIGHT / 2.0
                actual_bottom = center_y + self.panel.POSITION_RAIL_HEIGHT / 2.0
                self.assertAlmostEqual(expected_top, actual_top, places=2)
                self.assertAlmostEqual(expected_bottom, actual_bottom, places=2)

    def test_signal_marker_is_a_thin_contrasting_line(self):
        marker_spec = getattr(self.panel, "signal_marker_spec", None)
        marker_path_width = getattr(
            self.panel, "round_capped_marker_path_width", None
        )
        self.assertIsNotNone(marker_spec)
        self.assertIsNotNone(marker_path_width)
        if marker_spec is None or marker_path_width is None:
            return

        for channel in range(2):
            with self.subTest(channel=channel):
                spec = marker_spec(channel)
                path_width = marker_path_width(spec)
                self.assertAlmostEqual(2.0, spec["line_width"])
                self.assertAlmostEqual(0.35, spec["line_depth"])
                self.assertAlmostEqual(
                    spec["line_width"], path_width + spec["line_depth"]
                )
                self.assertNotEqual(spec["marker_rgb"], spec["window_rgb"])
                self.assertTrue(all(component >= 230
                                    for component in spec["marker_rgb"]))

    def test_checked_in_generated_files_are_current(self):
        expected_svg = self.panel.generate_svg() + "\n"
        expected_header = self.panel.generate_header()
        self.assertEqual(expected_svg,
                         (ROOT / "res" / "Brink.svg").read_text())
        self.assertEqual(expected_header,
                         (ROOT / "src" / "Brink" / "layout.h").read_text())

    def test_label_offsets_clear_the_actual_rack_widgets(self):
        pixels_per_mm = 15.0 / 5.08
        minimum_gap_mm = 0.25
        cases = (
            (
                "small knob",
                getattr(self.panel, "KNOB_LABEL_OFFSET", 0.0),
                22.67581 / (2.0 * pixels_per_mm),
                6.5 / (2.0 * pixels_per_mm),
            ),
            (
                "port",
                getattr(self.panel, "PORT_LABEL_OFFSET", 0.0),
                23.7 / (2.0 * pixels_per_mm),
                6.5 / (2.0 * pixels_per_mm),
            ),
            (
                "event port",
                getattr(self.panel, "EVENT_LABEL_OFFSET", 0.0),
                23.7 / (2.0 * pixels_per_mm),
                6.0 / (2.0 * pixels_per_mm),
            ),
        )
        for name, offset, radius, half_text_height in cases:
            with self.subTest(name=name):
                clearance = offset - radius - half_text_height
                self.assertGreaterEqual(clearance, minimum_gap_mm)

    def test_direction_arrow_tips_match_crossing_direction(self):
        arrow_offsets = getattr(self.panel, "direction_arrow_y_offsets", None)
        self.assertIsNotNone(arrow_offsets)
        if arrow_offsets is None:
            return

        up_base, up_tip = arrow_offsets(up=True)
        down_base, down_tip = arrow_offsets(up=False)
        self.assertLess(up_tip, up_base)
        self.assertGreater(down_tip, down_base)


if __name__ == "__main__":
    unittest.main()
