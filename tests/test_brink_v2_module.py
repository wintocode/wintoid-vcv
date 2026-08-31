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


CPP_TOKEN_RE = re.compile(
    r'''(?P<space>\s+)|(?P<comment>//[^\n]*|/\*.*?\*/)|'''
    r'''(?P<string>"(?:\\.|[^"\\])*")|(?P<char>'(?:\\.|[^'\\])*')|'''
    r'''(?P<identifier>[A-Za-z_]\w*)|'''
    r'''(?P<number>(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?[fFuUlL]*)|'''
    r'''(?P<operator>::|->|\+\+|--|==|!=|<=|>=|&&|\|\||'''
    r'''[{}()\[\];,.*&?:=+\-/%<>!])''',
    re.DOTALL | re.VERBOSE,
)


def cpp_tokens(source, replacements=None):
    """Return a deterministic C++ token projection with comments removed."""
    replacements = replacements or {}
    tokens = []
    position = 0
    for match in CPP_TOKEN_RE.finditer(source):
        if source[position:match.start()].strip():
            raise AssertionError(
                f"unrecognized C++ source near {source[position:match.start() + 20]!r}"
            )
        position = match.end()
        if match.lastgroup in {"space", "comment"}:
            continue
        token = match.group(0)
        if match.lastgroup == "identifier":
            token = replacements.get(token, token)
        tokens.append(token)
    if source[position:].strip():
        raise AssertionError(f"unrecognized C++ source suffix {source[position:]!r}")
    return tuple(tokens)


def initialized_declaration(source, marker):
    """Extract one initialized declaration through its terminating semicolon."""
    start = source.index(marker)
    brace_start = source.index("{", start)
    depth = 0
    for index in range(brace_start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                semicolon = source.index(";", index)
                return source[start:semicolon + 1]
    raise AssertionError(f"unclosed initialized declaration {marker!r}")


def token_sequence_count(source, snippet):
    source_tokens = cpp_tokens(source)
    snippet_tokens = cpp_tokens(snippet)
    width = len(snippet_tokens)
    return sum(
        source_tokens[index:index + width] == snippet_tokens
        for index in range(len(source_tokens) - width + 1)
    )


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

    def test_module_wrapper_is_a_normalized_v1_structural_equivalent(self):
        source = self.require_source()
        v1_module = struct_body(self.v1_source, "struct Brink : Module")
        v2_module = struct_body(source, "struct BrinkV2 : Module")
        self.assertEqual(
            cpp_tokens(v1_module),
            cpp_tokens(v2_module, {"BrinkV2": "Brink"}),
            "BrinkV2 must preserve every V1 module declaration, statement, "
            "loop bound, mapping, and write order after the class-name substitution",
        )

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
            "brink_v2_layout::PANEL_LABELS",
            "brink_v2_layout::PANEL_LABEL_COUNT",
            "brink_v2_layout::PANEL_LINES",
            "brink_v2_layout::PANEL_LINE_COUNT",
            "wintoid::ui::stroke_inset",
            "wintoid::ui::inset_extent",
            "wintoid::ui::clamp_stroke_center",
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

    def test_widget_id_coordinate_mapping_and_loop_bounds_are_exact(self):
        source = self.require_source()
        widget_body = struct_body(source, "struct BrinkV2Widget : ModuleWidget")

        layout_declaration = initialized_declaration(
            source,
            "static const BrinkV2ChannelLayout brinkV2ChannelLayouts[2]",
        )
        expected_layout = """
            static const BrinkV2ChannelLayout brinkV2ChannelLayouts[2] = {
                {
                    {brink_v2_layout::A_CENTER_KNOB_X, brink_v2_layout::A_CENTER_KNOB_Y},
                    {brink_v2_layout::A_WIDTH_KNOB_X, brink_v2_layout::A_WIDTH_KNOB_Y},
                    {brink_v2_layout::A_SIGNAL_X, brink_v2_layout::A_SIGNAL_Y},
                    {brink_v2_layout::A_POSITION_X, brink_v2_layout::A_POSITION_Y},
                    {brink_v2_layout::A_CENTER_CV_X, brink_v2_layout::A_CENTER_CV_Y},
                    {brink_v2_layout::A_CENTER_ATTEN_X, brink_v2_layout::A_CENTER_ATTEN_Y},
                    {brink_v2_layout::A_WIDTH_CV_X, brink_v2_layout::A_WIDTH_CV_Y},
                    {brink_v2_layout::A_WIDTH_ATTEN_X, brink_v2_layout::A_WIDTH_ATTEN_Y},
                    {brink_v2_layout::A_INSIDE_X, brink_v2_layout::A_INSIDE_Y},
                    {brink_v2_layout::A_OUTSIDE_X, brink_v2_layout::A_OUTSIDE_Y},
                    {brink_v2_layout::A_LOW_UP_X, brink_v2_layout::A_LOW_UP_Y},
                    {brink_v2_layout::A_HIGH_UP_X, brink_v2_layout::A_HIGH_UP_Y},
                    {brink_v2_layout::A_LOW_DOWN_X, brink_v2_layout::A_LOW_DOWN_Y},
                    {brink_v2_layout::A_HIGH_DOWN_X, brink_v2_layout::A_HIGH_DOWN_Y},
                    {brink_v2_layout::A_POSITION_RAIL_X, brink_v2_layout::A_POSITION_RAIL_Y}
                },
                {
                    {brink_v2_layout::B_CENTER_KNOB_X, brink_v2_layout::B_CENTER_KNOB_Y},
                    {brink_v2_layout::B_WIDTH_KNOB_X, brink_v2_layout::B_WIDTH_KNOB_Y},
                    {brink_v2_layout::B_SIGNAL_X, brink_v2_layout::B_SIGNAL_Y},
                    {brink_v2_layout::B_POSITION_X, brink_v2_layout::B_POSITION_Y},
                    {brink_v2_layout::B_CENTER_CV_X, brink_v2_layout::B_CENTER_CV_Y},
                    {brink_v2_layout::B_CENTER_ATTEN_X, brink_v2_layout::B_CENTER_ATTEN_Y},
                    {brink_v2_layout::B_WIDTH_CV_X, brink_v2_layout::B_WIDTH_CV_Y},
                    {brink_v2_layout::B_WIDTH_ATTEN_X, brink_v2_layout::B_WIDTH_ATTEN_Y},
                    {brink_v2_layout::B_INSIDE_X, brink_v2_layout::B_INSIDE_Y},
                    {brink_v2_layout::B_OUTSIDE_X, brink_v2_layout::B_OUTSIDE_Y},
                    {brink_v2_layout::B_LOW_UP_X, brink_v2_layout::B_LOW_UP_Y},
                    {brink_v2_layout::B_HIGH_UP_X, brink_v2_layout::B_HIGH_UP_Y},
                    {brink_v2_layout::B_LOW_DOWN_X, brink_v2_layout::B_LOW_DOWN_Y},
                    {brink_v2_layout::B_HIGH_DOWN_X, brink_v2_layout::B_HIGH_DOWN_Y},
                    {brink_v2_layout::B_POSITION_RAIL_X, brink_v2_layout::B_POSITION_RAIL_Y}
                }
            };
        """
        self.assertEqual(cpp_tokens(expected_layout), cpp_tokens(layout_declaration))

        logic_layout = initialized_declaration(
            source, "static const BrinkV2Point brinkV2LogicLayout[4]"
        )
        self.assertEqual(
            cpp_tokens("""
                static const BrinkV2Point brinkV2LogicLayout[4] = {
                    {brink_v2_layout::AND_OUTPUT_X, brink_v2_layout::AND_OUTPUT_Y},
                    {brink_v2_layout::OR_OUTPUT_X, brink_v2_layout::OR_OUTPUT_Y},
                    {brink_v2_layout::XOR_OUTPUT_X, brink_v2_layout::XOR_OUTPUT_Y},
                    {brink_v2_layout::STATE_OUTPUT_X, brink_v2_layout::STATE_OUTPUT_Y}
                };
            """),
            cpp_tokens(logic_layout),
        )
        channel_light_layout = initialized_declaration(
            source,
            "brinkV2ChannelLightLayouts[2][brink::EVENT_COUNT + 2]",
        )
        self.assertEqual(
            cpp_tokens("""
                brinkV2ChannelLightLayouts[2][brink::EVENT_COUNT + 2] = {
                    {
                        {brink_v2_layout::A_INSIDE_LIGHT_X, brink_v2_layout::A_INSIDE_LIGHT_Y},
                        {brink_v2_layout::A_OUTSIDE_LIGHT_X, brink_v2_layout::A_OUTSIDE_LIGHT_Y},
                        {brink_v2_layout::A_LOW_UP_LIGHT_X, brink_v2_layout::A_LOW_UP_LIGHT_Y},
                        {brink_v2_layout::A_HIGH_UP_LIGHT_X, brink_v2_layout::A_HIGH_UP_LIGHT_Y},
                        {brink_v2_layout::A_LOW_DOWN_LIGHT_X, brink_v2_layout::A_LOW_DOWN_LIGHT_Y},
                        {brink_v2_layout::A_HIGH_DOWN_LIGHT_X, brink_v2_layout::A_HIGH_DOWN_LIGHT_Y}
                    },
                    {
                        {brink_v2_layout::B_INSIDE_LIGHT_X, brink_v2_layout::B_INSIDE_LIGHT_Y},
                        {brink_v2_layout::B_OUTSIDE_LIGHT_X, brink_v2_layout::B_OUTSIDE_LIGHT_Y},
                        {brink_v2_layout::B_LOW_UP_LIGHT_X, brink_v2_layout::B_LOW_UP_LIGHT_Y},
                        {brink_v2_layout::B_HIGH_UP_LIGHT_X, brink_v2_layout::B_HIGH_UP_LIGHT_Y},
                        {brink_v2_layout::B_LOW_DOWN_LIGHT_X, brink_v2_layout::B_LOW_DOWN_LIGHT_Y},
                        {brink_v2_layout::B_HIGH_DOWN_LIGHT_X, brink_v2_layout::B_HIGH_DOWN_LIGHT_Y}
                    }
                };
            """),
            cpp_tokens(channel_light_layout),
        )
        logic_light_layout = initialized_declaration(
            source, "static const BrinkV2Point brinkV2LogicLightLayout[4]"
        )
        self.assertEqual(
            cpp_tokens("""
                static const BrinkV2Point brinkV2LogicLightLayout[4] = {
                    {brink_v2_layout::AND_LIGHT_X, brink_v2_layout::AND_LIGHT_Y},
                    {brink_v2_layout::OR_LIGHT_X, brink_v2_layout::OR_LIGHT_Y},
                    {brink_v2_layout::XOR_LIGHT_X, brink_v2_layout::XOR_LIGHT_Y},
                    {brink_v2_layout::STATE_LIGHT_X, brink_v2_layout::STATE_LIGHT_Y}
                };
            """),
            cpp_tokens(logic_light_layout),
        )

        declarations = {
            "const int centerParams[]": "{BrinkV2::A_CENTER_PARAM, BrinkV2::B_CENTER_PARAM}",
            "const int widthParams[]": "{BrinkV2::A_WIDTH_PARAM, BrinkV2::B_WIDTH_PARAM}",
            "const int centerAttenParams[]": "{BrinkV2::A_CENTER_ATTEN_PARAM, BrinkV2::B_CENTER_ATTEN_PARAM}",
            "const int widthAttenParams[]": "{BrinkV2::A_WIDTH_ATTEN_PARAM, BrinkV2::B_WIDTH_ATTEN_PARAM}",
            "const int signalInputs[]": "{BrinkV2::A_SIGNAL_INPUT, BrinkV2::B_SIGNAL_INPUT}",
            "const int centerCvInputs[]": "{BrinkV2::A_CENTER_CV_INPUT, BrinkV2::B_CENTER_CV_INPUT}",
            "const int widthCvInputs[]": "{BrinkV2::A_WIDTH_CV_INPUT, BrinkV2::B_WIDTH_CV_INPUT}",
            "const int logicOutputs[]": "{BrinkV2::AND_OUTPUT, BrinkV2::OR_OUTPUT, BrinkV2::XOR_OUTPUT, BrinkV2::STATE_OUTPUT}",
            "const int gateLights[2][2]": "{{BrinkV2::A_INSIDE_LIGHT, BrinkV2::A_OUTSIDE_LIGHT}, {BrinkV2::B_INSIDE_LIGHT, BrinkV2::B_OUTSIDE_LIGHT}}",
            "const int eventLights[2][brink::EVENT_COUNT]": "{{BrinkV2::A_LOW_UP_LIGHT, BrinkV2::A_HIGH_UP_LIGHT, BrinkV2::A_LOW_DOWN_LIGHT, BrinkV2::A_HIGH_DOWN_LIGHT}, {BrinkV2::B_LOW_UP_LIGHT, BrinkV2::B_HIGH_UP_LIGHT, BrinkV2::B_LOW_DOWN_LIGHT, BrinkV2::B_HIGH_DOWN_LIGHT}}",
            "const int logicLights[]": "{BrinkV2::AND_LIGHT, BrinkV2::OR_LIGHT, BrinkV2::XOR_LIGHT, BrinkV2::STATE_LIGHT}",
        }
        for marker, initializer in declarations.items():
            with self.subTest(declaration=marker):
                actual = initialized_declaration(widget_body, marker)
                expected = f"{marker} = {initializer};"
                self.assertEqual(cpp_tokens(expected), cpp_tokens(actual))

        loop_declarations = {
            "const int paramIds[]": "{centerParams[channel], widthParams[channel]}",
            "const BrinkV2Point knobPoints[]": "{layout.centerKnob, layout.widthKnob}",
            "const int inputIds[]": "{signalInputs[channel], centerCvInputs[channel], widthCvInputs[channel]}",
            "const BrinkV2Point inputPoints[]": "{layout.signal, layout.centerCv, layout.widthCv}",
            "const int attenIds[]": "{centerAttenParams[channel], widthAttenParams[channel]}",
            "const BrinkV2Point attenPoints[]": "{layout.centerAtten, layout.widthAtten}",
            "const BrinkV2Point outputPoints[]": "{layout.inside, layout.outside, layout.position, layout.lowUp, layout.highUp, layout.lowDown, layout.highDown}",
        }
        for marker, initializer in loop_declarations.items():
            with self.subTest(loop_declaration=marker):
                actual = initialized_declaration(widget_body, marker)
                expected = f"{marker} = {initializer};"
                self.assertEqual(cpp_tokens(expected), cpp_tokens(actual))

        channel_outputs = initialized_declaration(
            widget_body, "const int channelOutputs[2][brink::EVENT_COUNT + 3]"
        )
        self.assertEqual(
            cpp_tokens("""
                const int channelOutputs[2][brink::EVENT_COUNT + 3] = {
                    {BrinkV2::A_INSIDE_OUTPUT, BrinkV2::A_OUTSIDE_OUTPUT,
                     BrinkV2::A_POSITION_OUTPUT, BrinkV2::A_LOW_UP_OUTPUT,
                     BrinkV2::A_HIGH_UP_OUTPUT, BrinkV2::A_LOW_DOWN_OUTPUT,
                     BrinkV2::A_HIGH_DOWN_OUTPUT},
                    {BrinkV2::B_INSIDE_OUTPUT, BrinkV2::B_OUTSIDE_OUTPUT,
                     BrinkV2::B_POSITION_OUTPUT, BrinkV2::B_LOW_UP_OUTPUT,
                     BrinkV2::B_HIGH_UP_OUTPUT, BrinkV2::B_LOW_DOWN_OUTPUT,
                     BrinkV2::B_HIGH_DOWN_OUTPUT}
                };
            """),
            cpp_tokens(channel_outputs),
        )

        expected_loop_counts = {
            "for (int channel = 0; channel < 2; ++channel)": 3,
            "for (int knob = 0; knob < 2; ++knob)": 1,
            "for (int input = 0; input < 3; ++input)": 1,
            "for (int atten = 0; atten < 2; ++atten)": 1,
            "for (int output = 0; output < brink::EVENT_COUNT + 3; ++output)": 1,
            "for (int logic = 0; logic < 4; ++logic)": 2,
            "for (int gate = 0; gate < 2; ++gate)": 1,
            "for (int event = 0; event < brink::EVENT_COUNT; ++event)": 1,
        }
        for loop, expected_count in expected_loop_counts.items():
            with self.subTest(loop=loop):
                self.assertEqual(expected_count, token_sequence_count(widget_body, loop))

        bindings = (
            "Vec(knobPoints[knob].x, knobPoints[knob].y)), module, paramIds[knob]",
            "Vec(inputPoints[input].x, inputPoints[input].y)), module, inputIds[input]",
            "Vec(attenPoints[atten].x, attenPoints[atten].y)), module, attenIds[atten]",
            "Vec(outputPoints[output].x, outputPoints[output].y)), module, channelOutputs[channel][output]",
            "Vec(brinkV2LogicLayout[logic].x, brinkV2LogicLayout[logic].y)), module, logicOutputs[logic]",
            "brinkV2ChannelLightLayouts[channel][gate]",
            "brinkV2ChannelLightLayouts[channel][event + 2]",
            "brinkV2LogicLightLayout[logic]",
        )
        for binding in bindings:
            with self.subTest(binding=binding):
                self.assertGreater(token_sequence_count(widget_body, binding), 0)

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
