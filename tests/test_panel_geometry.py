#!/usr/bin/env python3

import unittest

from panel_geometry import (
    centered_stroke_outer_radius,
    visible_output_material,
)


class PanelGeometryTest(unittest.TestCase):
    def test_only_half_of_a_centered_stroke_extends_outward(self):
        self.assertAlmostEqual(
            4.30,
            centered_stroke_outer_radius(4.0, 0.60),
        )

    def test_visible_material_excludes_the_obscuring_widget(self):
        self.assertAlmostEqual(
            0.725,
            visible_output_material(4.45, 0.55, 4.0),
        )


if __name__ == "__main__":
    unittest.main()
