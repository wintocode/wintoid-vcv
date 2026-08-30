#!/usr/bin/env python3

import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE_PATH = ROOT / "src" / "VortexV2" / "VortexV2.cpp"
PLUGIN_HEADER_PATH = ROOT / "src" / "plugin.hpp"
PLUGIN_SOURCE_PATH = ROOT / "src" / "plugin.cpp"
MANIFEST_PATH = ROOT / "plugin.json"
README_PATH = ROOT / "README.md"


PARAM_IDS = [
    "CUTOFF_PARAM", "RESONANCE_PARAM", "DRIVE_PARAM",
    "CUTOFF_CV_ATTEN_PARAM", "RESONANCE_CV_ATTEN_PARAM",
    "DRIVE_CV_ATTEN_PARAM",
]
INPUT_IDS = [
    "AUDIO_INPUT", "CUTOFF_CV_INPUT", "RESONANCE_CV_INPUT",
    "DRIVE_CV_INPUT",
]
OUTPUT_IDS = [
    "LP6_OUTPUT", "LP12_OUTPUT", "LP24_OUTPUT",
    "HP6_OUTPUT", "HP12_OUTPUT", "HP24_OUTPUT",
    "BP_OUTPUT", "BP_PLUS_OUTPUT", "NOTCH_OUTPUT",
    "NOTCH_PLUS_OUTPUT", "AP_OUTPUT", "AP_PLUS_OUTPUT",
]
OUTPUT_LABELS = [
    "LP 6dB", "LP 12dB", "LP 24dB",
    "HP 6dB", "HP 12dB", "HP 24dB",
    "BP", "BP+", "Notch", "Notch+", "AP", "AP+",
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


def assert_ordered_names(test_case, body, names):
    positions = []
    for name in names:
        match = re.search(rf"\b{re.escape(name)}\b", body)
        test_case.assertIsNotNone(match, f"missing enum identifier {name}")
        positions.append(match.start())
    test_case.assertEqual(sorted(positions), positions)


class VortexV2ModuleContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (
            SOURCE_PATH.read_text(encoding="utf-8")
            if SOURCE_PATH.exists()
            else ""
        )
        cls.plugin_header = PLUGIN_HEADER_PATH.read_text(encoding="utf-8")
        cls.plugin_source = PLUGIN_SOURCE_PATH.read_text(encoding="utf-8")
        cls.manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        cls.readme = README_PATH.read_text(encoding="utf-8")

    def require_source(self):
        self.assertTrue(
            SOURCE_PATH.exists(),
            "VortexV2 module source is missing: src/VortexV2/VortexV2.cpp",
        )
        return self.source

    def test_module_source_exists_and_declares_model(self):
        source = self.require_source()
        self.assertIn("struct VortexV2 : Module", source)
        self.assertIn("struct VortexV2Widget : ModuleWidget", source)
        self.assertIn('createModel<VortexV2, VortexV2Widget>("VortexV2")', source)

    def test_parameter_input_and_output_order_is_frozen(self):
        source = self.require_source()
        assert_ordered_names(
            self, enum_body(source, "ParamId"), PARAM_IDS + ["PARAMS_LEN"]
        )
        assert_ordered_names(
            self, enum_body(source, "InputId"), INPUT_IDS + ["INPUTS_LEN"]
        )
        assert_ordered_names(
            self, enum_body(source, "OutputId"), OUTPUT_IDS + ["OUTPUTS_LEN"]
        )
        self.assertEqual(len(PARAM_IDS), enum_body(source, "ParamId").count("_PARAM"))
        self.assertEqual(len(INPUT_IDS), enum_body(source, "InputId").count("_INPUT"))
        self.assertEqual(len(OUTPUT_IDS), enum_body(source, "OutputId").count("_OUTPUT"))

    def test_outputs_have_the_full_static_labels(self):
        source = self.require_source()
        for label in OUTPUT_LABELS:
            with self.subTest(label=label):
                self.assertIn(f'"{label}"', source)
        self.assertIn("configOutput", source)
        self.assertIn("outputLabels", source)

    def test_connection_gating_contract_is_explicit(self):
        source = self.require_source()
        for marker in (
            "isConnected()",
            "previousOutputConnected",
            "setChannels(channels)",
            "process_branch",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, source)
        for forbidden in ("MODE_PARAM", "ModeDisplay", "onButton(", "Filter Mode"):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, source)

    def test_widget_uses_generated_layout_and_standard_controls(self):
        source = self.require_source()
        self.assertIn('#include "layout.h"', source)
        for contract in (
            "RoundSmallBlackKnob",
            "Trimpot",
            "PJ301MPort",
            "OUTPUT_COLUMN_XS",
            "OUTPUT_ROW_YS",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, source)

        output_loop = re.search(
            r"for\s*\(int output\s*=\s*0;.*?\n\s*\}", source, re.DOTALL
        )
        self.assertIsNotNone(output_loop, "missing output widget loop")
        output_body = output_loop.group(0)
        self.assertIn("OUTPUT_COLUMN_XS", output_body)
        self.assertIn("OUTPUT_ROW_YS", output_body)
        self.assertNotRegex(
            output_body,
            r"createOutputCentered<[^>]+>\s*\(\s*mm2px\(Vec\([^)]*[0-9]f",
        )

    def test_model_is_registered_and_metadata_is_documented(self):
        module = next(
            (item for item in self.manifest["modules"] if item.get("slug") == "VortexV2"),
            None,
        )
        self.assertIsNotNone(module, "VortexV2 metadata is missing from plugin.json")
        self.assertEqual("VortexV2", module["name"])
        self.assertIn("12-output", module["description"])
        self.assertEqual(
            "https://github.com/wintocode/wintoid-vcv#vortexv2",
            module["manualUrl"],
        )
        self.assertEqual(["Filter", "Effect", "Polyphonic"], module["tags"])
        self.assertIn("### VortexV2", self.readme)
        self.assertIn("LP 6/12/24dB", self.readme)
        self.assertIn("BP, BP+, Notch, Notch+, AP, AP+", self.readme)
        self.assertIn("extern Model* modelVortexV2;", self.plugin_header)
        self.assertIn("p->addModel(modelVortexV2);", self.plugin_source)


if __name__ == "__main__":
    unittest.main()
