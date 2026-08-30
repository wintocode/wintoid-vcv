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
MODULES = ("Four", "Vortex", "Brink", "FourV2")


def extract_struct(source, marker):
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated struct: {marker}")


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

    def test_extract_struct_handles_nested_braces(self):
        source = (
            "struct Demo { void run() { if (true) { int value = 1; } } }; "
            "struct Next { int value; };"
        )
        body = extract_struct(source, "struct Demo")
        self.assertIn("if (true) { int value = 1; }", body)
        self.assertNotIn("struct Next", body)

    def test_compatibility_doc_records_png_faceplate_handoff(self):
        self.assertIn("assets/*.png", self.compatibility)
        self.assertIn("240 px", self.compatibility)
        self.assertIn("does not render `res/*.svg`", self.compatibility)
        self.assertIn("SvgToPng.py", self.compatibility)

    def test_custom_widgets_keep_drawing_on_layer_one_and_use_geometry_helpers(self):
        widgets = (
            ("Four", "struct AlgoDisplay"),
            ("Four", "struct FoldTypeDisplay"),
            ("Vortex", "struct ModeDisplay"),
            ("Brink", "struct WindowRail"),
            ("FourV2", "struct AlgorithmRoutingDisplay"),
            ("FourV2", "struct OperatorFrequencyDisplay"),
            ("FourV2", "struct FourV2FrequencyControlGroups"),
        )

        for name, marker in widgets:
            with self.subTest(module=name, widget=marker):
                body = extract_struct(self.sources[name], marker)
                self.assertIn("drawLayer", body)
                self.assertIn("layer != 1", body)
                self.assertIn("wintoid::ui::", body)

    def test_custom_widgets_select_the_supported_font(self):
        widgets = (
            ("Four", "struct AlgoDisplay"),
            ("Four", "struct FoldTypeDisplay"),
            ("Vortex", "struct ModeDisplay"),
            ("FourV2", "struct AlgorithmRoutingDisplay"),
            ("FourV2", "struct OperatorFrequencyDisplay"),
        )

        for name, marker in widgets:
            with self.subTest(module=name, widget=marker):
                body = extract_struct(self.sources[name], marker)
                self.assertIn("res/fonts/DejaVuSans.ttf", body)
                self.assertIn("nvgFontFaceId", body)
                self.assertIn("stroke_inset", body)
                self.assertIn("inset_extent", body)

    def test_four_v2_displays_are_read_only_and_use_algorithm_table(self):
        for marker in ("struct AlgorithmRoutingDisplay",
                       "struct OperatorFrequencyDisplay"):
            with self.subTest(widget=marker):
                body = extract_struct(self.sources["FourV2"], marker)
                self.assertIn("drawLayer", body)
                self.assertIn("layer != 1", body)
                self.assertIn("res/fonts/DejaVuSans.ttf", body)
                self.assertIn("nvgFontFaceId", body)
                self.assertIn("wintoid::ui::stroke_inset", body)
                self.assertNotIn("onButton", body)
                self.assertNotIn("onDrag", body)
                self.assertNotIn("appendContextMenu", body)
                self.assertNotIn("setValue", body)

        routing = extract_struct(
            self.sources["FourV2"], "struct AlgorithmRoutingDisplay")
        self.assertIn("four_v2::ALGORITHMS", routing)
        self.assertIn("four_v2::algorithm_index", routing)
        self.assertIn("inset_extent", routing)

        frequency = extract_struct(
            self.sources["FourV2"], "struct OperatorFrequencyDisplay")
        self.assertIn("four_v2::clamp_mode", frequency)
        self.assertIn("four_v2::ratio_label(coarse)", frequency)
        self.assertIn("four_v2::frequency_label(coarse, mode, fine)", frequency)
        self.assertIn("getParamQuantity(coarseParamId)->getValue()", frequency)
        self.assertIn("getParamQuantity(freqModeParamId)->getValue()", frequency)
        self.assertIn("getParamQuantity(fineParamId)->getValue()", frequency)

    def test_window_rail_uses_all_stroke_geometry_helpers(self):
        rail = extract_struct(self.sources["Brink"], "struct WindowRail")
        for helper in ("clamp_stroke_center", "stroke_inset", "inset_extent"):
            with self.subTest(helper=helper):
                self.assertIn(helper, rail)

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
