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
    body = re.sub(r"//[^\n]*|/\*.*?\*/", "", body, flags=re.DOTALL)
    actual = []
    for declaration in body.split(","):
        declaration = declaration.strip()
        if not declaration:
            continue
        match = re.fullmatch(
            r"([A-Za-z_]\w*)\s*(?:=.*)?", declaration, re.DOTALL
        )
        test_case.assertIsNotNone(
            match, f"could not parse enum declaration {declaration!r}"
        )
        actual.append(match.group(1))
    test_case.assertEqual(names, actual)


def block_body(source, start):
    brace_start = source.index("{", start)
    depth = 0
    for index in range(brace_start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace_start + 1:index]
    raise AssertionError("unclosed source block")


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
        self.assertEqual(
            len(OUTPUT_IDS), enum_body(source, "OutputId").count("_OUTPUT")
        )

    def test_outputs_are_configured_with_their_exact_labels_in_order(self):
        source = self.require_source()
        positions = []
        for output_id, label in zip(OUTPUT_IDS, OUTPUT_LABELS):
            pattern = (
                rf"configOutput\s*\(\s*{re.escape(output_id)}\s*,\s*"
                rf"\"{re.escape(label)}\"\s*\)"
            )
            with self.subTest(output_id=output_id, label=label):
                match = re.search(pattern, source)
                self.assertIsNotNone(
                    match,
                    f"missing configOutput association for {output_id}: {label}",
                )
                positions.append(match.start())
        self.assertEqual(sorted(positions), positions)

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

    def test_disconnected_branches_are_reset_and_connected_states_flush(self):
        """Removing a cable must reset only its branch; active branches flush.

        A change that removes the disconnect-transition reset or any of the
        denormal flushes would otherwise allow stale state or denormals to
        survive in one independently gated output branch.
        """
        source = self.require_source()
        for marker in (
            "voiceStates[lane].branches[output].reset()",
            "voiceStates[lane].branches[output].f1.z",
            "voiceStates[lane].branches[output].f2a.z0",
            "voiceStates[lane].branches[output].f2a.z1",
            "voiceStates[lane].branches[output].f2b.z0",
            "voiceStates[lane].branches[output].f2b.z1",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, source)

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

        output_loop_pattern = re.compile(
            r"""
            for\s*\(\s*int\s+output\s*=\s*0\s*;
            \s*output\s*<\s*vortex_v2::OUTPUT_COUNT\s*;
            \s*(?:\+\+\s*output|output\s*\+\+)\s*\)\s*\{
            """,
            re.DOTALL | re.VERBOSE,
        )
        output_bodies = [
            block_body(source, match.start())
            for match in output_loop_pattern.finditer(source)
        ]
        output_body = next(
            (
                body for body in output_bodies
                if "createOutputCentered" in body
                and "OUTPUT_COLUMN_XS" in body
                and "OUTPUT_ROW_YS" in body
            ),
            None,
        )
        self.assertIsNotNone(output_body, "missing twelve-output widget loop")
        self.assertIn("outputIds[output]", output_body)
        self.assertRegex(
            output_body,
            r"OUTPUT_COLUMN_XS\s*\[\s*(?:column|output\s*%\s*3)\s*\]",
        )
        self.assertRegex(
            output_body,
            r"OUTPUT_ROW_YS\s*\[\s*(?:row|output\s*/\s*3)\s*\]",
        )
        has_derived_grid = (
            re.search(r"\bcolumn\s*=\s*output\s*%\s*3\b", output_body)
            and re.search(r"\brow\s*=\s*output\s*/\s*3\b", output_body)
        )
        has_direct_grid = (
            re.search(r"OUTPUT_COLUMN_XS\s*\[\s*output\s*%\s*3\s*\]", output_body)
            and re.search(r"OUTPUT_ROW_YS\s*\[\s*output\s*/\s*3\s*\]", output_body)
        )
        self.assertTrue(
            has_derived_grid or has_direct_grid,
            "output loop must derive a 3x4 grid index from output",
        )
        self.assertNotRegex(
            output_body,
            r"mm2px\s*\(\s*Vec\s*\([^)]*(?:\d+(?:\.\d*)?|\.\d+)",
        )

    def test_model_is_registered_and_metadata_is_documented(self):
        module = next(
            (
                item for item in self.manifest["modules"]
                if item.get("slug") == "VortexV2"
            ),
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
