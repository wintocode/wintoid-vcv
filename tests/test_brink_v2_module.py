#!/usr/bin/env python3

import json
import pathlib
import re
import unittest
import xml.etree.ElementTree as ET


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE_PATH = ROOT / "src" / "BrinkV2" / "BrinkV2.cpp"
V1_SOURCE_PATH = ROOT / "src" / "Brink" / "Brink.cpp"
PLUGIN_HEADER_PATH = ROOT / "src" / "plugin.hpp"
PLUGIN_SOURCE_PATH = ROOT / "src" / "plugin.cpp"
MANIFEST_PATH = ROOT / "plugin.json"
README_PATH = ROOT / "README.md"
COMPATIBILITY_PATH = ROOT / "docs" / "metamodule-compatibility.md"
PANEL_PATH = ROOT / "res" / "BrinkV2.svg"

RACK_PIXELS_PER_MM = 15.0 / 5.08
RACK_PORT_RADIUS = 23.7 / (2.0 * RACK_PIXELS_PER_MM)
SOCKET_RADIUS_TOLERANCE = 0.02


def enum_declarations(source, enum_name):
    match = re.search(
        rf"enum {enum_name}\s*\{{(?P<body>.*?)\n\s*\}};",
        source,
        re.DOTALL,
    )
    if not match:
        raise AssertionError(f"missing enum {enum_name}")
    body = re.sub(r"//[^\n]*|/\*.*?\*/", "", match.group("body"), flags=re.DOTALL)
    declarations = []
    for declaration in body.split(","):
        declaration = re.sub(r"\s+", " ", declaration.strip())
        if declaration:
            declarations.append(declaration)
    return declarations


def constructor_body(source, class_name):
    marker = f"{class_name}()"
    start = source.index(marker)
    brace_start = source.index("{", start)
    depth = 0
    for index in range(brace_start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace_start + 1:index]
    raise AssertionError(f"unclosed {class_name} constructor")


def struct_body(source, declaration):
    start = source.index(declaration)
    brace_start = source.index("{", start)
    depth = 0
    for index in range(brace_start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace_start + 1:index]
    raise AssertionError(f"unclosed struct {declaration!r}")


def configuration_calls(source, class_name):
    body = constructor_body(source, class_name)
    calls = re.findall(
        r"\b(configParam|configInput|configOutput|configLight)\s*(?:<[^>]+>)?\s*"
        r"\((.*?);",
        body,
        re.DOTALL,
    )
    normalized = []
    for kind, args in calls:
        normalized_args = re.sub(r"\s+", " ", args).strip()
        normalized.append(f"{kind}({normalized_args})")
    return normalized


class BrinkV2ModuleContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE_PATH.read_text(encoding="utf-8") if SOURCE_PATH.exists() else ""
        cls.panel = PANEL_PATH.read_text(encoding="utf-8") if PANEL_PATH.exists() else ""
        cls.v1_source = V1_SOURCE_PATH.read_text(encoding="utf-8")
        cls.plugin_header = PLUGIN_HEADER_PATH.read_text(encoding="utf-8")
        cls.plugin_source = PLUGIN_SOURCE_PATH.read_text(encoding="utf-8")
        cls.manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        cls.readme = README_PATH.read_text(encoding="utf-8")
        cls.compatibility = COMPATIBILITY_PATH.read_text(encoding="utf-8")

    def require_source(self):
        self.assertTrue(SOURCE_PATH.exists(), "BrinkV2 module source is missing: src/BrinkV2/BrinkV2.cpp")
        return self.source

    def require_panel_root(self):
        self.assertTrue(PANEL_PATH.exists(), "BrinkV2 panel asset is missing: res/BrinkV2.svg")
        return ET.fromstring(self.panel)

    def test_module_source_declares_independent_model(self):
        source = self.require_source()
        self.assertIn("struct BrinkV2 : Module", source)
        self.assertIn("struct BrinkV2Widget : ModuleWidget", source)
        self.assertIn('createModel<BrinkV2, BrinkV2Widget>("BrinkV2")', source)
        for marker in (
            '#include "../Brink/dsp.h"',
            '#include "../ui_geometry.h"',
            "brink::process_window",
            "brink::process_logic",
            '#include "layout.h"',
            'res/BrinkV2.svg',
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, source)

    def test_processing_uses_only_the_shared_brink_namespace(self):
        source = self.require_source()
        self.assertNotRegex(source, r"\bnamespace\s+brink\b")
        for function in (
            "effective_channels",
            "logic_channels",
            "broadcast_lane",
            "process_window",
            "process_logic",
        ):
            with self.subTest(function=function):
                self.assertGreater(source.count(f"brink::{function}("), 0)
                self.assertEqual(
                    source.count(f"{function}("),
                    source.count(f"brink::{function}("),
                    f"{function} must only be called through the shared brink namespace",
                )

    def test_process_contract_preserves_v1_normalisation_polyphony_and_state(self):
        source = self.require_source()
        module_body = struct_body(source, "struct BrinkV2 : Module")
        process_body = struct_body(module_body, "void process(const ProcessArgs& args) override")

        for normalisation in (
            "inputs[B_SIGNAL_INPUT].isConnected()",
            "? inputs[B_SIGNAL_INPUT] : inputs[A_SIGNAL_INPUT]",
            "inputs[B_CENTER_CV_INPUT].isConnected()",
            "? inputs[B_CENTER_CV_INPUT] : inputs[A_CENTER_CV_INPUT]",
            "inputs[B_WIDTH_CV_INPUT].isConnected()",
            "? inputs[B_WIDTH_CV_INPUT] : inputs[A_WIDTH_CV_INPUT]",
        ):
            with self.subTest(normalisation=normalisation):
                self.assertIn(normalisation, process_body)

        for contract in (
            "displayRateLimiter.should_publish()",
            "args.sampleRate != previousSampleRate",
            "clearRuntimeState()",
            "prepareWindowLanes(channel, signalChannels[channel])",
            "prepareLogicLanes(logicChannels)",
            "brink::effective_channels(signalInput.getChannels())",
            "brink::process_window(",
            "brink::process_logic(",
            "out.inside ? 10.f : 0.f",
            "out.inside ? 0.f : 10.f",
            "setVoltage(out.position, lane)",
            "active ? 10.f : 0.f",
            "values[output] ? 10.f : 0.f",
            "lane == 0 && publishDisplay",
            "displayFrames[channel].store(out.frame)",
            "setSmoothBrightness(",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, process_body)

        first_channel_count = process_body.index(".setChannels(signalChannels[channel])")
        first_channel_write = process_body.index(".setVoltage(")
        self.assertLess(first_channel_count, first_channel_write)
        logic_count = process_body.index(".setChannels(logicChannels)")
        logic_write = process_body.index(".setVoltage(values[output]")
        self.assertLess(logic_count, logic_write)

    def test_runtime_reset_contract_preserves_silent_lane_activation(self):
        source = self.require_source()
        module_body = struct_body(source, "struct BrinkV2 : Module")
        for contract in (
            "brink::reset(windowStates[channel][lane])",
            "insideStates[channel][lane] = false",
            "brink::reset(logicStates[lane])",
            "previousSignalChannels[channel] = 0",
            "previousLogicChannels = 0",
            "previousSampleRate = 0.f",
            "displayRateLimiter.reset(0.f)",
            "resetWindowLane(windowStates[channel][lane], insideStates[channel][lane])",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, module_body)

    def test_window_rail_is_read_only_layer_one_and_clip_safe(self):
        source = self.require_source()
        rail_body = struct_body(source, "struct BrinkV2WindowRail : Widget")
        for contract in (
            "brink_v2_layout::RAIL_WIDTH",
            "brink_v2_layout::POSITION_RAIL_HEIGHT",
            "void drawLayer(const DrawArgs& args, int layer) override",
            "layer != 1",
            "displayFrames[channel].load()",
            "brink::normalize_display_voltage",
            "wintoid::ui::stroke_inset",
            "wintoid::ui::inset_extent",
            "wintoid::ui::clamp_stroke_center",
            "brink_v2_layout::CHANNEL_A_ACCENT_R",
            "brink_v2_layout::CHANNEL_B_ACCENT_R",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, rail_body)
        self.assertGreaterEqual(rail_body.count("clamp_stroke_center"), 4)
        for forbidden in ("onButton(", "onDrag(", "appendContextMenu("):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, rail_body)

    def test_label_overlay_is_static_generated_and_metamodule_safe(self):
        source = self.require_source()
        labels_body = struct_body(source, "struct BrinkV2PanelLabels : Widget")
        for contract in (
            "void drawLayer(const DrawArgs& args, int layer) override",
            "layer != 1",
            'asset::system("res/fonts/DejaVuSans.ttf")',
            "if (!font)",
            "brink_v2_layout::PANEL_WIDTH",
            "brink_v2_layout::PANEL_HEIGHT",
            "brink_v2_layout::TITLE_X",
            "brink_v2_layout::TITLE_Y",
            "brink_v2_layout::TITLE_FONT_SIZE",
            "brink_v2_layout::KNOB_LABEL_OFFSET",
            "brink_v2_layout::PORT_LABEL_OFFSET",
            "brink_v2_layout::EVENT_LABEL_OFFSET",
            '"Brink V2"',
            '"CHANNEL A"',
            '"CHANNEL B"',
            '"CENTER"',
            '"WIDTH"',
            '"SIGNAL"',
            '"POSITION"',
            '"CTR CV"',
            '"WID CV"',
            '"INSIDE"',
            '"OUTSIDE"',
            '"AND"',
            '"OR"',
            '"XOR"',
            '"TOGGLE"',
            "drawDirectionArrow",
            "normalisationPoints",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, labels_body)
        self.assertNotIn('"wint"', source)
        self.assertNotIn('"oid"', source)
        for forbidden in (
            "onButton(",
            "onDrag(",
            "appendContextMenu(",
            ".setValue(",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, source)

    def test_enum_declarations_match_brink_v1_in_order(self):
        source = self.require_source()
        for enum_name in ("ParamId", "InputId", "OutputId", "LightId"):
            with self.subTest(enum_name=enum_name):
                self.assertEqual(
                    enum_declarations(self.v1_source, enum_name),
                    enum_declarations(source, enum_name),
                )

    def test_constructor_configuration_matches_brink_v1(self):
        source = self.require_source()
        self.assertEqual(
            configuration_calls(self.v1_source, "Brink"),
            configuration_calls(source, "BrinkV2"),
        )

    def test_panel_uses_sem_v2_standard_widgets_and_uniform_socket_guides(self):
        source = self.require_source()
        for marker in (
            "RoundSmallBlackKnob",
            "Trimpot",
            "PJ301MPort",
            "GrayModuleLightWidget",
            "createLightCentered",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, source)

        widget_body = struct_body(source, "struct BrinkV2Widget : ModuleWidget")
        input_widget_types = re.findall(
            r"addInput\s*\(\s*createInputCentered\s*<\s*([^>]+?)\s*>\s*\(",
            widget_body,
            re.DOTALL,
        )
        output_widget_types = re.findall(
            r"addOutput\s*\(\s*createOutputCentered\s*<\s*([^>]+?)\s*>\s*\(",
            widget_body,
            re.DOTALL,
        )
        self.assertEqual(
            len(re.findall(r"\baddInput\s*\(", widget_body)),
            len(input_widget_types),
        )
        self.assertEqual(
            len(re.findall(r"\baddOutput\s*\(", widget_body)),
            len(output_widget_types),
        )
        self.assertEqual(["PJ301MPort"], sorted(set(input_widget_types)))
        self.assertEqual(["PJ301MPort"], sorted(set(output_widget_types)))
        for enum_name in ("InputId", "OutputId"):
            expected_ids = [
                declaration.split("=", 1)[0].strip()
                for declaration in enum_declarations(self.v1_source, enum_name)
                if not declaration.endswith("_LEN")
            ]
            for identifier in expected_ids:
                with self.subTest(port_id=identifier):
                    self.assertRegex(widget_body, rf"\b{re.escape(identifier)}\b")

        coordinate_names = (
            "A_CENTER_KNOB", "A_WIDTH_KNOB", "A_SIGNAL", "A_POSITION",
            "A_CENTER_CV", "A_CENTER_ATTEN", "A_WIDTH_CV", "A_WIDTH_ATTEN",
            "A_INSIDE", "A_OUTSIDE", "A_LOW_UP", "A_HIGH_UP",
            "A_LOW_DOWN", "A_HIGH_DOWN", "A_POSITION_RAIL",
            "B_CENTER_KNOB", "B_WIDTH_KNOB", "B_SIGNAL", "B_POSITION",
            "B_CENTER_CV", "B_CENTER_ATTEN", "B_WIDTH_CV", "B_WIDTH_ATTEN",
            "B_INSIDE", "B_OUTSIDE", "B_LOW_UP", "B_HIGH_UP",
            "B_LOW_DOWN", "B_HIGH_DOWN", "B_POSITION_RAIL",
            "AND_OUTPUT", "OR_OUTPUT", "XOR_OUTPUT", "STATE_OUTPUT",
        )
        for coordinate in coordinate_names:
            with self.subTest(coordinate=coordinate):
                self.assertIn(f"brink_v2_layout::{coordinate}_X", source)
                self.assertIn(f"brink_v2_layout::{coordinate}_Y", source)

        self.assertIn("new BrinkV2PanelLabels()", widget_body)
        self.assertIn("new BrinkV2WindowRail()", widget_body)

        root = self.require_panel_root()
        title_nodes = [
            element for element in root.iter()
            if element.tag.rsplit("}", 1)[-1] == "text"
            and (element.text or "").strip() == "Brink V2"
        ]
        self.assertEqual(1, len(title_nodes), "panel title must be exactly Brink V2")

        socket_guides = [
            element for element in root.iter()
            if element.tag.rsplit("}", 1)[-1] == "circle"
            and float(element.attrib["r"]) >= RACK_PORT_RADIUS - SOCKET_RADIUS_TOLERANCE
        ]
        self.assertEqual(
            24,
            len(socket_guides),
            "six inputs plus eighteen outputs must have socket guide circles",
        )
        socket_styles = {
            (
                circle.attrib["r"],
                circle.attrib["fill"],
                circle.attrib["stroke"],
                circle.attrib["stroke-width"],
            )
            for circle in socket_guides
        }
        self.assertEqual(
            1,
            len(socket_styles),
            "every BrinkV2 input and output must use one socket-guide style",
        )
        for forbidden in ("OUTPUT_BACKPLATE", "OUTPUT_RING", "#39445f", "#dfe7f3"):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, source)
                self.assertNotIn(forbidden, self.panel)

    def test_registration_and_documentation_are_frozen(self):
        source = self.require_source()
        self.assertIn("extern Model* modelBrinkV2;", self.plugin_header)
        self.assertIn("p->addModel(modelBrinkV2);", self.plugin_source)
        self.assertEqual(1, sum(module.get("slug") == "BrinkV2" for module in self.manifest["modules"]))
        module = next(module for module in self.manifest["modules"] if module.get("slug") == "BrinkV2")
        for phrase in ("16-channel polyphonic", "dual-window", "logic"):
            with self.subTest(description=phrase):
                self.assertIn(phrase, module["description"])
        self.assertEqual("https://github.com/wintocode/wintoid-vcv#brinkv2", module["manualUrl"])
        self.assertEqual(["Logic", "Polyphonic", "Utility"], module["tags"])
        self.assertRegex(self.readme, r"(?m)^### BrinkV2$")
        self.assertIn("Brink remains", self.readme)
        self.assertIn("BrinkV2", self.compatibility)


if __name__ == "__main__":
    unittest.main()
