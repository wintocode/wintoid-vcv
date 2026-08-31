#!/usr/bin/env python3

import importlib.util
import math
import pathlib
import unittest
import xml.etree.ElementTree as ET

from panel_geometry import centered_text_clearance


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "generate_panel_brink_v2.py"
PANEL_SVG = ROOT / "res" / "BrinkV2.svg"
LAYOUT_HEADER = ROOT / "src" / "BrinkV2" / "layout.h"

RACK_PIXELS_PER_MM = 15.0 / 5.08
RACK_SMALL_KNOB_RADIUS_MM = 22.67581 / (2.0 * RACK_PIXELS_PER_MM)
RACK_TRIMPOT_RADIUS_MM = 18.0 / (2.0 * RACK_PIXELS_PER_MM)
RACK_PORT_RADIUS_MM = 23.7 / (2.0 * RACK_PIXELS_PER_MM)
MINIMUM_EDGE_CLEARANCE_MM = 4.0
MINIMUM_LABEL_CLEARANCE_MM = 0.25


def load_generator(path):
    if not path.exists():
        return None
    spec = importlib.util.spec_from_file_location("brink_v2_panel", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def svg_elements(svg, tag):
    root = ET.fromstring(svg)
    return [node for node in root.iter() if node.tag.endswith(tag)]


def element_by_id(svg, identifier):
    for node in ET.fromstring(svg).iter():
        if node.attrib.get("id") == identifier:
            return node
    raise AssertionError(f"no SVG element found with id {identifier!r}")


class BrinkV2PanelTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.panel = load_generator(SCRIPT)

    def require_panel(self):
        self.assertIsNotNone(
            self.panel,
            "Task 3 generator is missing: scripts/generate_panel_brink_v2.py",
        )
        return self.panel

    def test_dimensions_and_section_inventory_are_frozen(self):
        panel = self.require_panel()
        self.assertEqual(12, panel.HP)
        self.assertAlmostEqual(60.96, panel.WIDTH_MM)
        self.assertAlmostEqual(128.5, panel.HEIGHT_MM)
        self.assertAlmostEqual(panel.WIDTH_MM,
                               panel.CHANNEL_A_X + panel.CHANNEL_B_X)
        self.assertEqual(2, len(panel.CHANNEL_SECTION_RECTS))
        self.assertEqual(2, len(panel.POSITION_RAILS))
        self.assertEqual(4, len(panel.LOGIC_COMPONENTS))

    def test_all_wrapper_sockets_appear_once_in_the_physical_inventory(self):
        panel = self.require_panel()
        inputs = tuple(panel.INPUT_COMPONENTS)
        outputs = tuple(panel.OUTPUT_COMPONENTS)
        sockets = inputs + outputs
        component_names = [name for name, _x, _y in panel.COMPONENTS]

        self.assertEqual(6, len(inputs))
        self.assertEqual(18, len(outputs))
        self.assertEqual(24, len(sockets))
        self.assertEqual(24, len({name for name, _x, _y in sockets}))
        for name, _x, _y in sockets:
            with self.subTest(socket=name):
                self.assertEqual(1, component_names.count(name))

    def test_channel_ports_follow_the_mirrored_v1_signal_flow(self):
        panel = self.require_panel()
        positions = {name: (x, y) for name, x, y in panel.COMPONENTS}
        for prefix, inner_x, outer_x in (
            ("A", panel.CHANNEL_A_X + panel.PAIR_OFFSET,
             panel.CHANNEL_A_X - panel.PAIR_OFFSET),
            ("B", panel.CHANNEL_B_X - panel.PAIR_OFFSET,
             panel.CHANNEL_B_X + panel.PAIR_OFFSET),
        ):
            for suffix in ("SIGNAL", "CENTER_CV", "WIDTH_CV"):
                with self.subTest(channel=prefix, input=suffix):
                    self.assertAlmostEqual(inner_x, positions[f"{prefix}_{suffix}"][0])
            for suffix in (
                "POSITION", "INSIDE", "OUTSIDE", "LOW_UP", "HIGH_UP",
                "LOW_DOWN", "HIGH_DOWN",
            ):
                with self.subTest(channel=prefix, output=suffix):
                    self.assertAlmostEqual(outer_x, positions[f"{prefix}_{suffix}"][0])

    def test_real_rack_component_envelopes_clear_edges_and_each_other(self):
        panel = self.require_panel()
        expected_radii = {
            "knob": RACK_SMALL_KNOB_RADIUS_MM,
            "trimpot": RACK_TRIMPOT_RADIUS_MM,
            "port": RACK_PORT_RADIUS_MM,
        }
        self.assertEqual(expected_radii, panel.REAL_COMPONENT_RADII)
        components = tuple(panel.COMPONENTS)
        for name, x, y in components:
            radius = panel.COMPONENT_RADII[name]
            with self.subTest(component=name, constraint="edge margin"):
                self.assertGreaterEqual(x - radius, MINIMUM_EDGE_CLEARANCE_MM)
                self.assertLessEqual(x + radius,
                                     panel.WIDTH_MM - MINIMUM_EDGE_CLEARANCE_MM)
                self.assertGreaterEqual(y - radius, MINIMUM_EDGE_CLEARANCE_MM)
                self.assertLessEqual(y + radius,
                                     panel.HEIGHT_MM - MINIMUM_EDGE_CLEARANCE_MM)
        for index, (name_a, ax, ay) in enumerate(components):
            for name_b, bx, by in components[index + 1:]:
                distance = math.hypot(ax - bx, ay - by)
                with self.subTest(first=name_a, second=name_b):
                    self.assertGreaterEqual(distance + 1e-9,
                                            panel.COMPONENT_RADII[name_a]
                                            + panel.COMPONENT_RADII[name_b])

    def test_sem_palette_and_uniform_socket_guides_replace_v1_output_backplates(self):
        panel = self.require_panel()
        self.assertEqual("#ece8d9", panel.PANEL_IVORY)
        self.assertEqual("#242522", panel.LEGEND_CHARCOAL)
        self.assertEqual("#556d80", panel.SECTION_BLUE_GREY)
        self.assertEqual("#b7693c", panel.FUNCTION_ORANGE)
        self.assertFalse(hasattr(panel, "OUTPUT_BACKPLATE"))
        self.assertNotIn("OUTPUT_BACKPLATE", SCRIPT.read_text(encoding="utf-8"))

        svg = panel.generate_svg()
        circles = svg_elements(svg, "circle")
        socket_styles = []
        for name, x, y in tuple(panel.INPUT_COMPONENTS) + tuple(panel.OUTPUT_COMPONENTS):
            matching = [circle for circle in circles
                        if abs(float(circle.attrib["cx"]) - x) < 0.001
                        and abs(float(circle.attrib["cy"]) - y) < 0.001]
            with self.subTest(socket=name):
                self.assertEqual(1, len(matching))
                circle = matching[0]
                self.assertNotIn(circle.attrib["fill"], {"#39445f", "#dfe7f3"})
                self.assertNotIn(circle.attrib["stroke"], {"#39445f", "#dfe7f3"})
                socket_styles.append(tuple(circle.attrib[key] for key in
                                           ("r", "fill", "stroke", "stroke-width")))
        self.assertEqual(1, len(set(socket_styles)))

    def test_svg_has_sem_sections_title_and_canonical_logo(self):
        panel = self.require_panel()
        svg = panel.generate_svg()
        root = ET.fromstring(svg)
        background = next(node for node in root if node.tag.endswith("rect"))
        self.assertEqual("#ece8d9", background.attrib["fill"])
        self.assertNotEqual("#1a1a2e", background.attrib["fill"])
        for identifier, fill in (
            ("channel-a-section", "#e3e0d1"),
            ("channel-b-section", "#e7e3d4"),
        ):
            with self.subTest(section=identifier):
                section = element_by_id(svg, identifier)
                self.assertEqual(fill, section.attrib["fill"])
                self.assertEqual("#556d80", section.attrib["stroke"])
        ids = {node.attrib.get("id") for node in root.iter()}
        self.assertTrue({"wintoid-logo", "wint-glyphs", "oid-glyphs",
                         "wint-underline", "oid-underline"}.issubset(ids))
        self.assertIn("Brink V2", {node.text for node in root.iter()
                                    if node.tag.endswith("text")})

    def test_label_and_event_baselines_clear_real_widget_envelopes(self):
        panel = self.require_panel()
        svg = panel.generate_svg()
        labels = {node.text: node for node in svg_elements(svg, "text") if node.text}
        positions = {name: (x, y) for name, x, y in panel.COMPONENTS}
        for label, component_name in panel.LABEL_COMPONENTS.items():
            with self.subTest(label=label):
                node = labels[label]
                font_size = float(node.attrib["font-size"])
                baseline = float(node.attrib["y"])
                _x, component_y = positions[component_name]
                clearance = component_y - panel.COMPONENT_RADII[component_name] - (
                    baseline + font_size * 0.25
                )
                self.assertGreaterEqual(clearance, MINIMUM_LABEL_CLEARANCE_MM)
        for label, preceding_port in panel.EVENT_LABEL_PRECEDING_PORTS.items():
            with self.subTest(event_label=label):
                node = labels[label]
                font_size = float(node.attrib["font-size"])
                baseline = float(node.attrib["y"])
                _x, port_y = positions[preceding_port]
                self.assertGreaterEqual(
                    (baseline - font_size)
                    - (port_y + panel.COMPONENT_RADII[preceding_port]),
                    MINIMUM_LABEL_CLEARANCE_MM,
                )

    def test_position_rails_span_the_real_knob_and_width_cv_envelopes(self):
        panel = self.require_panel()
        for prefix, rail in zip(("A", "B"), panel.POSITION_RAILS):
            name, _x, _y, _width, _height = rail
            rectangle = element_by_id(panel.generate_svg(), name)
            rail_top = float(rectangle.attrib["y"])
            rail_bottom = rail_top + float(rectangle.attrib["height"])
            knob_y = dict((name, y) for name, _x, y in panel.COMPONENTS)[
                f"{prefix}_CENTER_KNOB"
            ]
            cv_y = dict((name, y) for name, _x, y in panel.COMPONENTS)[
                f"{prefix}_WIDTH_CV"
            ]
            with self.subTest(channel=prefix):
                self.assertAlmostEqual(knob_y - RACK_SMALL_KNOB_RADIUS_MM,
                                       rail_top, places=2)
                self.assertAlmostEqual(cv_y + RACK_PORT_RADIUS_MM,
                                       rail_bottom, places=2)

    def test_generation_is_deterministic_and_matches_checked_in_artifacts(self):
        panel = self.require_panel()
        self.assertEqual(panel.generate_svg(), panel.generate_svg())
        self.assertEqual(panel.generate_header(), panel.generate_header())
        self.assertTrue(PANEL_SVG.exists(),
                        "Task 3 panel SVG is missing: res/BrinkV2.svg")
        self.assertTrue(LAYOUT_HEADER.exists(),
                        "Task 3 layout header is missing: src/BrinkV2/layout.h")
        self.assertEqual(panel.generate_svg(), PANEL_SVG.read_text(encoding="utf-8"))
        self.assertEqual(panel.generate_header(),
                         LAYOUT_HEADER.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
