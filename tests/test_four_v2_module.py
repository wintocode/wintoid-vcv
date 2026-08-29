#!/usr/bin/env python3

import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE_PATH = ROOT / "src" / "FourV2" / "FourV2.cpp"
PLUGIN_HEADER_PATH = ROOT / "src" / "plugin.hpp"
PLUGIN_SOURCE_PATH = ROOT / "src" / "plugin.cpp"
MANIFEST_PATH = ROOT / "plugin.json"
README_PATH = ROOT / "README.md"
COMPATIBILITY_PATH = ROOT / "docs" / "metamodule-compatibility.md"


PARAM_IDS = [
    "ALGORITHM_PARAM", "TUNE_PARAM", "PM_DEPTH_PARAM",
    "PM_DEPTH_CV_ATTEN_PARAM", "MASTER_PARAM", "EXT_PM_ATTEN_PARAM",
    "OP1_COARSE_PARAM", "OP2_COARSE_PARAM", "OP3_COARSE_PARAM",
    "OP4_COARSE_PARAM", "OP1_FINE_PARAM", "OP2_FINE_PARAM",
    "OP3_FINE_PARAM", "OP4_FINE_PARAM", "OP1_OUTPUT_PARAM",
    "OP2_OUTPUT_PARAM", "OP3_OUTPUT_PARAM", "OP4_OUTPUT_PARAM",
    "OP1_WARP_PARAM", "OP2_WARP_PARAM", "OP3_WARP_PARAM",
    "OP4_WARP_PARAM", "OP1_FOLD_PARAM", "OP2_FOLD_PARAM",
    "OP3_FOLD_PARAM", "OP4_FOLD_PARAM", "OP1_FEEDBACK_PARAM",
    "OP2_FEEDBACK_PARAM", "OP3_FEEDBACK_PARAM", "OP4_FEEDBACK_PARAM",
    "OP1_FREQ_MODE_PARAM", "OP2_FREQ_MODE_PARAM",
    "OP3_FREQ_MODE_PARAM", "OP4_FREQ_MODE_PARAM",
    "OP1_FOLD_TYPE_PARAM", "OP2_FOLD_TYPE_PARAM",
    "OP3_FOLD_TYPE_PARAM", "OP4_FOLD_TYPE_PARAM",
    "OP1_OUTPUT_CV_ATTEN_PARAM", "OP2_OUTPUT_CV_ATTEN_PARAM",
    "OP3_OUTPUT_CV_ATTEN_PARAM", "OP4_OUTPUT_CV_ATTEN_PARAM",
    "OP1_WARP_CV_ATTEN_PARAM", "OP2_WARP_CV_ATTEN_PARAM",
    "OP3_WARP_CV_ATTEN_PARAM", "OP4_WARP_CV_ATTEN_PARAM",
    "OP1_FOLD_CV_ATTEN_PARAM", "OP2_FOLD_CV_ATTEN_PARAM",
    "OP3_FOLD_CV_ATTEN_PARAM", "OP4_FOLD_CV_ATTEN_PARAM",
    "OP1_FEEDBACK_CV_ATTEN_PARAM", "OP2_FEEDBACK_CV_ATTEN_PARAM",
    "OP3_FEEDBACK_CV_ATTEN_PARAM", "OP4_FEEDBACK_CV_ATTEN_PARAM",
]

INPUT_IDS = [
    "VOCT_INPUT", "PM_DEPTH_CV_INPUT", "EXT_PM_INPUT",
    "OP1_OUTPUT_CV_INPUT", "OP2_OUTPUT_CV_INPUT",
    "OP3_OUTPUT_CV_INPUT", "OP4_OUTPUT_CV_INPUT",
    "OP1_WARP_CV_INPUT", "OP2_WARP_CV_INPUT",
    "OP3_WARP_CV_INPUT", "OP4_WARP_CV_INPUT",
    "OP1_FOLD_CV_INPUT", "OP2_FOLD_CV_INPUT",
    "OP3_FOLD_CV_INPUT", "OP4_FOLD_CV_INPUT",
    "OP1_FEEDBACK_CV_INPUT", "OP2_FEEDBACK_CV_INPUT",
    "OP3_FEEDBACK_CV_INPUT", "OP4_FEEDBACK_CV_INPUT",
]


def enum_body(source, enum_name):
    match = re.search(
        rf"enum {enum_name}\s*\{{(?P<body>.*?)\n\s*\}};",
        source,
        re.DOTALL,
    )
    if not match:
        raise AssertionError(f"missing enum {enum_name}")
    return match.group("body")


def ordered_enum_names(body, expected):
    positions = []
    for name in expected:
        match = re.search(rf"\b{re.escape(name)}\b", body)
        if not match:
            raise AssertionError(f"missing enum identifier {name}")
        positions.append(match.start())
    return positions


class FourV2ModuleContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.assert_source_exists = cls._assert_source_exists
        cls.source = (
            SOURCE_PATH.read_text(encoding="utf-8")
            if SOURCE_PATH.exists()
            else ""
        )
        cls.plugin_header = PLUGIN_HEADER_PATH.read_text(encoding="utf-8")
        cls.plugin_source = PLUGIN_SOURCE_PATH.read_text(encoding="utf-8")
        cls.manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        cls.readme = README_PATH.read_text(encoding="utf-8")
        cls.compatibility = COMPATIBILITY_PATH.read_text(encoding="utf-8")

    @staticmethod
    def _assert_source_exists(test_case):
        test_case.assertTrue(
            SOURCE_PATH.exists(),
            "FourV2 module source is missing: src/FourV2/FourV2.cpp",
        )

    def require_source(self):
        self.assertTrue(SOURCE_PATH.exists(), "FourV2.cpp is missing")
        return self.source

    def test_module_source_exists_and_declares_model(self):
        source = self.require_source()
        self.assertIn("struct FourV2 : Module", source)
        self.assertIn("struct FourV2Widget : ModuleWidget", source)
        self.assertIn('createModel<FourV2, FourV2Widget>("FourV2")', source)

    def test_parameter_order_is_frozen(self):
        body = enum_body(self.require_source(), "ParamId")
        expected = PARAM_IDS + ["PARAMS_LEN"]
        positions = ordered_enum_names(body, expected)
        self.assertEqual(sorted(positions), positions)
        self.assertEqual(len(PARAM_IDS), body.count("_PARAM"))

    def test_input_output_and_light_order_is_frozen(self):
        source = self.require_source()
        input_body = enum_body(source, "InputId")
        input_positions = ordered_enum_names(
            input_body, INPUT_IDS + ["INPUTS_LEN"]
        )
        self.assertEqual(sorted(input_positions), input_positions)
        self.assertEqual(len(INPUT_IDS), input_body.count("_INPUT"))

        output_body = enum_body(source, "OutputId")
        self.assertEqual(
            ["MAIN_OUTPUT", "OUTPUTS_LEN"],
            re.findall(r"\b(?:MAIN_OUTPUT|OUTPUTS_LEN)\b", output_body),
        )
        light_body = enum_body(source, "LightId")
        self.assertEqual(
            ["OVER_LIGHT", "LIGHTS_LEN"],
            re.findall(r"\b(?:OVER_LIGHT|LIGHTS_LEN)\b", light_body),
        )

    def test_parameter_defaults_ranges_and_selector_metadata_are_explicit(self):
        source = self.require_source()
        for contract in (
            'configParam(ALGORITHM_PARAM, 0.f, 10.f, 0.f, "Algorithm")',
            'configParam(TUNE_PARAM, -100.f, 100.f, 0.f, "Tune", " cents")',
            'configParam(PM_DEPTH_PARAM, 0.f, 1.f, 1.f, "PM Depth"',
            'configParam(PM_DEPTH_CV_ATTEN_PARAM, -1.f, 1.f, 0.f',
            'configParam(MASTER_PARAM, 0.f, 1.f, 1.f, "Master"',
            'configParam(EXT_PM_ATTEN_PARAM, -1.f, 1.f, 0.f',
            'configParam<CoarseParamQuantity>(\n                coarse_ids[op], 0.f, 14.f, 5.f,',
            'configParam(fine_ids[op], -100.f, 100.f, 0.f',
            'configParam(output_ids[op], 0.f, 1.f, output_default',
            'configParam(warp_ids[op], 0.f, 1.f, 0.f',
            'configParam(fold_ids[op], 0.f, 1.f, 0.f',
            'configParam(feedback_ids[op], 0.f, 1.f, 0.f',
            'configSwitch(freq_mode_ids[op], 0.f, 1.f, 0.f',
            'configSwitch(fold_type_ids[op], 0.f, 2.f, 0.f',
            '"Ratio", "Fixed"',
            '"Symmetric", "Asymmetric", "Soft Clip"',
            'getParamQuantity(ALGORITHM_PARAM)->snapEnabled = true',
            'getParamQuantity(freq_mode_ids[op])->snapEnabled = true',
            'getParamQuantity(fold_type_ids[op])->snapEnabled = true',
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, source)

        self.assertIn("const float output_default = op == 0 ? 1.f : 0.f", source)
        self.assertGreaterEqual(source.count(
            'configParam(output_cv_atten_ids[op], -1.f, 1.f, 0.f'), 1)
        self.assertGreaterEqual(source.count(
            'configParam(warp_cv_atten_ids[op], -1.f, 1.f, 0.f'), 1)
        self.assertGreaterEqual(source.count(
            'configParam(fold_cv_atten_ids[op], -1.f, 1.f, 0.f'), 1)
        self.assertGreaterEqual(source.count(
            'configParam(feedback_cv_atten_ids[op], -1.f, 1.f, 0.f'), 1)

    def test_standard_controls_use_generated_layout_coordinates(self):
        source = self.require_source()
        for contract in (
            "RoundSmallBlackKnob",
            "Trimpot",
            "CKSS",
            "CKSSThree",
            "PJ301MPort",
            "RedLight",
            "OP1_COARSE_X",
            "PATCHBAY_FEEDBACK_Y",
            "OP1_OUTPUT_CV_INPUT_X",
            "OP1_FEEDBACK_CV_ATTEN_X",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, source)
        self.assertIn("for (int op = 0; op < 4; ++op)", source)

    def test_process_equations_cover_polyphony_tuning_patchbay_output_and_over(self):
        source = self.require_source()
        for contract in (
            "wintoid::polyphony::MAX_CHANNELS",
            "wintoid::polyphony::effective_channels",
            "wintoid::polyphony::broadcast_lane",
            "wintoid::polyphony::reset_changed_lanes",
            "const float global_tune = cents_multiplier(",
            "common.opFine[op] = cents_multiplier(",
            "exp2f(cents / 1200.f)",
            "external_pm_volts * external_pm_atten * 0.1f",
            "four_v2::engine_process",
            "const float volts = out * 5.f",
            "peakVolts = fmaxf(peakVolts, fabsf(volts))",
            "overDetector.process(peakVolts, args.sampleTime)",
            "lights[OVER_LIGHT].setBrightness",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, source)
        self.assertIn(
            "const float pm_cv_voltage = four_v2::finite_or(",
            source,
        )
        self.assertIn("pm_cv_atten / 10.f", source)
        self.assertIn("clamp(pm_depth + pm_cv, 0.f, 1.f)", source)
        self.assertIn("clamp(knob + cv * atten / 10.f, 0.f, 1.f)", source)
        for contract in (
            "four_v2::algorithm_index",
            "four_v2::clamp_mode",
            "fold_type_index",
            "four_v2::finite_or",
        ):
            with self.subTest(sanitizer=contract):
                self.assertIn(contract, source)

    def test_quantity_only_changes_display_text_and_displays_are_not_controls(self):
        source = self.require_source()
        self.assertIn("struct CoarseParamQuantity", source)
        self.assertIn("four_v2::frequency_label", source)
        self.assertNotIn("void setValue", source)
        self.assertNotIn("onButton(", source)
        self.assertNotIn("appendContextMenu(", source)

    def test_dynamic_displays_use_generated_rectangles_and_read_module_state(self):
        source = self.require_source()
        for contract in (
            "struct AlgorithmRoutingDisplay",
            "struct OperatorFrequencyDisplay",
            "ROUTING_DISPLAY_WIDTH",
            "ROUTING_DISPLAY_HEIGHT",
            "ROUTING_DISPLAY_X",
            "ROUTING_DISPLAY_Y",
            "OP1_FREQUENCY_DISPLAY_X",
            "OP1_FREQUENCY_DISPLAY_Y",
            "OP4_FREQUENCY_DISPLAY_X",
            "OP4_FREQUENCY_DISPLAY_Y",
            "display->box.pos",
            "four_v2::ALGORITHMS",
            "four_v2::ratio_label(coarse)",
            "four_v2::frequency_label(coarse, mode)",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, source)

        self.assertNotIn("onDrag(", source)
        self.assertNotIn("appendContextMenu(", source)

    def test_registration_and_manifest_are_added_after_existing_models(self):
        source = self.require_source()
        self.assertIn("extern Model* modelFourV2;", self.plugin_header)
        self.assertLess(
            self.plugin_source.index("p->addModel(modelFour);"),
            self.plugin_source.index("p->addModel(modelFourV2);"),
        )
        self.assertEqual(1, source.count("modelFourV2 = createModel"))

        four_v2_entries = [
            module for module in self.manifest["modules"]
            if module.get("slug") == "FourV2"
        ]
        self.assertEqual(1, len(four_v2_entries))
        self.assertEqual(
            {
                "slug": "FourV2",
                "name": "FourV2",
                "description": (
                    "16-channel polyphonic 4-operator phase-modulation "
                    "synthesizer with fixed routing display, waveshaping, "
                    "wavefolding, and feedback"
                ),
                "manualUrl": "https://github.com/wintocode/wintoid-vcv#fourv2",
                "tags": ["Oscillator", "Digital", "Waveshaper", "Polyphonic"],
            },
            four_v2_entries[0],
        )
        self.assertEqual("2.2.1", self.manifest["version"])

    def test_readme_documents_four_v2_without_rewriting_four(self):
        four_v2_start = self.readme.index("### FourV2")
        vortex_start = self.readme.index("### Vortex")
        self.assertLess(four_v2_start, vortex_start)
        four_v2 = self.readme[four_v2_start:vortex_start]

        for contract in (
            "11 algorithms",
            "15 curated harmonic ratios",
            (
                "`4:1`, `3:1`, `2:1`, `3:2`, `4:3`, `1:1`, `3:4`, `2:3`, "
                "`1:2`, `1:3`, `1:4`, `1:5`, `1:6`, `1:7`, `1:8`"
            ),
            "Ratio mode",
            "Fixed mode",
            "Output, Warp, Fold, and Feedback",
            "CV PATCHBAY",
            "PM DEPTH",
            "External PM affects every carrier",
            "signed",
            "raw carrier sum",
            "MASTER",
            "OVER",
            "16-channel polyphony",
            "voice count follows the **V/OCT** input",
            "broadcast lane 0",
            "Four remains",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, four_v2)

    def test_compatibility_doc_records_four_v2_handoff_boundary(self):
        for contract in (
            "FourV2",
            "src/FourV2/layout.h",
            "PNG",
            "same revision",
            "240 px",
            "AlgorithmRoutingDisplay",
            "OperatorFrequencyDisplay",
            "ordinary static parameters",
            "supported SDK",
            "memory",
            "refresh",
            "../wintoid-metamodule",
            "read-only",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, self.compatibility)

    def test_makefile_runs_this_contract(self):
        makefile = (ROOT / "tests" / "Makefile").read_text(encoding="utf-8")
        self.assertIn("test_four_v2_module", makefile)
        self.assertRegex(makefile, r"test_four_v2_module:\s*\n\s*python3 test_four_v2_module\.py")


if __name__ == "__main__":
    unittest.main()
