#!/usr/bin/env python3

"""Source-level regression contract for MetaModule-safe custom drawing.

MetaModule clips custom widgets to their box and exposes only the SDK
component-library fonts, so the shared sources must keep every stroked
box-edge primitive inside its widget and select fonts explicitly.  These
checks operate on the C++ sources because the failures they guard against
only appear on the MetaModule host, which the host test suite cannot run.
"""

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULES = ("Four", "Vortex", "Brink")


class MetaModuleGraphicsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sources = {
            name: (ROOT / "src" / name / f"{name}.cpp").read_text()
            for name in MODULES
        }
        cls.compatibility = (
            ROOT / "docs" / "metamodule-compatibility.md"
        ).read_text()

    def test_compatibility_doc_records_png_faceplate_handoff(self):
        self.assertIn("assets/*.png", self.compatibility)
        self.assertIn("240 px", self.compatibility)
        self.assertIn("does not render `res/*.svg`", self.compatibility)
        self.assertIn("SvgToPng.py", self.compatibility)

    def test_modules_include_and_consume_the_geometry_helpers(self):
        for name in MODULES:
            with self.subTest(module=name):
                source = self.sources[name]
                self.assertIn('#include "../ui_geometry.h"', source)
                self.assertIn("wintoid::ui::", source)

    def test_algo_and_mode_displays_select_the_supported_font(self):
        for name, marker in (("Four", "struct AlgoDisplay"),
                             ("Vortex", "struct ModeDisplay")):
            with self.subTest(module=name):
                source = self.sources[name]
                start = source.index(marker)
                end = source.index("void onButton", start)
                display = source[start:end]
                self.assertIn("res/fonts/DejaVuSans.ttf", display)
                self.assertIn("nvgFontFaceId", display)

    def test_custom_displays_keep_drawing_on_layer_one(self):
        for name in MODULES:
            with self.subTest(module=name):
                self.assertIn("drawLayer", self.sources[name])
                self.assertIn("layer != 1", self.sources[name])

    def test_no_display_rectangle_uses_the_full_box_form(self):
        full_box = "nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y,"
        for name in MODULES:
            with self.subTest(module=name):
                self.assertNotIn(full_box, self.sources[name])

    def test_brink_track_does_not_touch_the_box_edges(self):
        source = self.sources["Brink"]
        rounded_rects = re.findall(
            r"nvgRoundedRect\(args\.vg,[^;]+;", source)
        self.assertTrue(rounded_rects)
        for call in rounded_rects:
            with self.subTest(call=call):
                self.assertNotRegex(call, r",\s*0\.f,")


if __name__ == "__main__":
    unittest.main()
