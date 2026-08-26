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


if __name__ == "__main__":
    unittest.main()
