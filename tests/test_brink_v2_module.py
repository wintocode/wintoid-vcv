#!/usr/bin/env python3

import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE_PATH = ROOT / "src" / "BrinkV2" / "BrinkV2.cpp"
V1_SOURCE_PATH = ROOT / "src" / "Brink" / "Brink.cpp"
PLUGIN_HEADER_PATH = ROOT / "src" / "plugin.hpp"
PLUGIN_SOURCE_PATH = ROOT / "src" / "plugin.cpp"
MANIFEST_PATH = ROOT / "plugin.json"
README_PATH = ROOT / "README.md"
COMPATIBILITY_PATH = ROOT / "docs" / "metamodule-compatibility.md"


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


def configuration_calls(source, class_name):
    body = constructor_body(source, class_name)
    calls = re.findall(
        r"\b(configParam|configInput|configOutput)\s*(?:<[^>]+>)?\s*"
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
        cls.v1_source = V1_SOURCE_PATH.read_text(encoding="utf-8")
        cls.plugin_header = PLUGIN_HEADER_PATH.read_text(encoding="utf-8")
        cls.plugin_source = PLUGIN_SOURCE_PATH.read_text(encoding="utf-8")
        cls.manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        cls.readme = README_PATH.read_text(encoding="utf-8")
        cls.compatibility = COMPATIBILITY_PATH.read_text(encoding="utf-8")

    def require_source(self):
        self.assertTrue(SOURCE_PATH.exists(), "BrinkV2 module source is missing: src/BrinkV2/BrinkV2.cpp")
        return self.source

    def test_module_source_declares_independent_model(self):
        source = self.require_source()
        self.assertIn("struct BrinkV2 : Module", source)
        self.assertIn("struct BrinkV2Widget : ModuleWidget", source)
        self.assertIn('createModel<BrinkV2, BrinkV2Widget>("BrinkV2")', source)
        for marker in (
            '#include "../Brink/dsp.h"',
            "brink::process_window",
            "brink::process_logic",
            '#include "layout.h"',
            'res/BrinkV2.svg',
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, source)

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

    def test_panel_uses_sem_v2_standard_widgets(self):
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
