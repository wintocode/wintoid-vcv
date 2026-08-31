#!/usr/bin/env python3

import importlib.util
import math
import pathlib
import unittest
import xml.etree.ElementTree as ET

from panel_geometry import centered_text_clearance


ROOT = pathlib.Path(__file__).resolve().parents[1]
V1_SCRIPT = ROOT / "scripts" / "generate_panel_brink.py"
SCRIPT = ROOT / "scripts" / "generate_panel_brink_v2.py"
PANEL_SVG = ROOT / "res" / "BrinkV2.svg"
LAYOUT_HEADER = ROOT / "src" / "BrinkV2" / "layout.h"

RACK_PIXELS_PER_MM = 15.0 / 5.08
RACK_SMALL_KNOB_RADIUS_MM = 22.67581 / (2.0 * RACK_PIXELS_PER_MM)
RACK_TRIMPOT_RADIUS_MM = 18.0 / (2.0 * RACK_PIXELS_PER_MM)
RACK_PORT_RADIUS_MM = 23.7 / (2.0 * RACK_PIXELS_PER_MM)
MINIMUM_EDGE_CLEARANCE_MM = 4.0
MINIMUM_LABEL_CLEARANCE_MM = 0.25

EXPECTED_INPUT_NAMES = (
    "A_SIGNAL", "A_CENTER_CV", "A_WIDTH_CV",
    "B_SIGNAL", "B_CENTER_CV", "B_WIDTH_CV",
)
EXPECTED_CHANNEL_OUTPUT_NAMES = (
    "A_INSIDE", "A_OUTSIDE", "A_POSITION",
    "A_LOW_UP", "A_HIGH_UP", "A_LOW_DOWN", "A_HIGH_DOWN",
    "B_INSIDE", "B_OUTSIDE", "B_POSITION",
    "B_LOW_UP", "B_HIGH_UP", "B_LOW_DOWN", "B_HIGH_DOWN",
)
EXPECTED_LOGIC_NAMES = ("AND_OUTPUT", "OR_OUTPUT", "XOR_OUTPUT", "STATE_OUTPUT")
EXPECTED_OUTPUT_NAMES = EXPECTED_CHANNEL_OUTPUT_NAMES + EXPECTED_LOGIC_NAMES
EXPECTED_CONTROL_NAMES = (
    "A_CENTER_KNOB", "A_WIDTH_KNOB", "A_CENTER_ATTEN", "A_WIDTH_ATTEN",
    "B_CENTER_KNOB", "B_WIDTH_KNOB", "B_CENTER_ATTEN", "B_WIDTH_ATTEN",
)
EXPECTED_COMPONENT_NAMES = (
    EXPECTED_CONTROL_NAMES + EXPECTED_INPUT_NAMES + EXPECTED_OUTPUT_NAMES
)


def load_generator(path, module_name):
    if not path.exists():
        return None
    spec = importlib.util.spec_from_file_location(module_name, path)
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


def shape_center(node):
    tag = node.tag.rsplit("}", 1)[-1]
    if tag in {"circle", "ellipse"}:
        return float(node.attrib["cx"]), float(node.attrib["cy"])
    if tag == "rect":
        return (
            float(node.attrib["x"]) + float(node.attrib["width"]) / 2.0,
            float(node.attrib["y"]) + float(node.attrib["height"]) / 2.0,
        )
    return None


def shape_bounds(node):
    tag = node.tag.rsplit("}", 1)[-1]
    if tag == "circle":
        x = float(node.attrib["cx"])
        y = float(node.attrib["cy"])
        radius = float(node.attrib["r"])
        return x - radius, y - radius, x + radius, y + radius
    if tag == "ellipse":
        x = float(node.attrib["cx"])
        y = float(node.attrib["cy"])
        radius_x = float(node.attrib["rx"])
        radius_y = float(node.attrib["ry"])
        return x - radius_x, y - radius_y, x + radius_x, y + radius_y
    if tag == "rect":
        x = float(node.attrib["x"])
        y = float(node.attrib["y"])
        return x, y, x + float(node.attrib["width"]), y + float(node.attrib["height"])
    return None


def text_horizontal_bounds(node):
    if "textLength" not in node.attrib:
        raise AssertionError(
            f"SVG label {node.text!r} must declare deterministic textLength"
        )
    if node.attrib.get("lengthAdjust") != "spacingAndGlyphs":
        raise AssertionError(
            f"SVG label {node.text!r} must use lengthAdjust=spacingAndGlyphs"
        )
    x = float(node.attrib["x"])
    width = float(node.attrib["textLength"])
    anchor = node.attrib.get("text-anchor", "start")
    if anchor == "middle":
        return x - width / 2.0, x + width / 2.0
    if anchor == "end":
        return x - width, x
    if anchor == "start":
        return x, x + width
    raise AssertionError(f"unsupported SVG text-anchor {anchor!r}")


def text_clearance_mm(node, component_x, component_y, component_radius):
    if node.attrib.get("dominant-baseline") != "middle":
        raise AssertionError(
            f"SVG label {node.text!r} must use dominant-baseline=middle"
        )
    font_size_px = float(node.attrib["font-size"]) * RACK_PIXELS_PER_MM
    return centered_text_clearance(
        abs(float(node.attrib["y"]) - component_y),
        component_radius,
        font_size_px,
        RACK_PIXELS_PER_MM,
    )


class BrinkV2PanelTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.v1 = load_generator(V1_SCRIPT, "brink_v1_panel")
        cls.panel = load_generator(SCRIPT, "brink_v2_panel")

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
        components = tuple(panel.COMPONENTS)
        component_names = [name for name, _x, _y in components]
        component_by_name = {
            name: (x, y) for name, x, y in components
        }

        self.assertEqual(EXPECTED_INPUT_NAMES,
                         tuple(name for name, _x, _y in inputs))
        self.assertEqual(EXPECTED_OUTPUT_NAMES,
                         tuple(name for name, _x, _y in outputs))
        self.assertEqual(EXPECTED_LOGIC_NAMES,
                         tuple(name for name, _x, _y in panel.LOGIC_COMPONENTS))
        self.assertEqual(24, len(sockets))
        self.assertEqual(24, len({name for name, _x, _y in sockets}))
        self.assertEqual(len(EXPECTED_COMPONENT_NAMES), len(components))
        self.assertEqual(len(component_names), len(set(component_names)))
        self.assertEqual(set(EXPECTED_COMPONENT_NAMES), set(component_names))
        for name, x, y in sockets:
            with self.subTest(socket=name):
                self.assertEqual(1, component_names.count(name))
                self.assertEqual((x, y), component_by_name[name])

        self.assertIsNotNone(self.v1)
        self.assertEqual(set(EXPECTED_OUTPUT_NAMES),
                         set(self.v1.OUTPUT_COMPONENT_NAMES))
        self.assertEqual(set(EXPECTED_COMPONENT_NAMES),
                         {name for name, _x, _y in self.v1.COMPONENTS})

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
        components = tuple(panel.COMPONENTS)
        self.assertEqual(set(EXPECTED_COMPONENT_NAMES),
                         {name for name, _x, _y in components})
        self.assertEqual(set(EXPECTED_COMPONENT_NAMES),
                         set(panel.COMPONENT_RADII))
        for name in EXPECTED_COMPONENT_NAMES:
            expected_radius = (
                RACK_TRIMPOT_RADIUS_MM if name.endswith("_ATTEN")
                else RACK_SMALL_KNOB_RADIUS_MM
                if name.endswith("_KNOB") else RACK_PORT_RADIUS_MM
            )
            with self.subTest(component=name, constraint="real radius"):
                self.assertAlmostEqual(expected_radius,
                                       panel.COMPONENT_RADII[name], places=6)
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
        self.assertAlmostEqual(RACK_PORT_RADIUS_MM, panel.PORT_RADIUS, places=6)
        self.assertFalse(hasattr(panel, "OUTPUT_BACKPLATE"))
        generator_source = SCRIPT.read_text(encoding="utf-8")
        self.assertNotIn("OUTPUT_BACKPLATE", generator_source)
        self.assertNotRegex(generator_source, r"(?i)output[_-]?ring")

        svg = panel.generate_svg()
        root = ET.fromstring(svg)
        circles = [node for node in root.iter() if node.tag.endswith("circle")]
        sockets = tuple(panel.INPUT_COMPONENTS) + tuple(panel.OUTPUT_COMPONENTS)
        socket_coordinates = [(x, y) for _name, x, y in sockets]
        socket_styles = []
        for name, x, y in sockets:
            matching = [circle for circle in circles
                        if abs(float(circle.attrib["cx"]) - x) < 0.001
                        and abs(float(circle.attrib["cy"]) - y) < 0.001]
            with self.subTest(socket=name):
                self.assertEqual(1, len(matching))
                circle = matching[0]
                self.assertAlmostEqual(RACK_PORT_RADIUS_MM,
                                       float(circle.attrib["r"]), places=6)
                self.assertNotIn(circle.attrib["fill"], {"#39445f", "#dfe7f3"})
                self.assertNotIn(circle.attrib["stroke"], {"#39445f", "#dfe7f3"})
                socket_styles.append(tuple(circle.attrib[key] for key in
                                           ("r", "fill", "stroke", "stroke-width")))
        self.assertEqual(1, len(set(socket_styles)))

        socket_circle_ids = {id(circle) for circle in circles
                             if any(abs(float(circle.attrib["cx"]) - x) < 0.001
                                    and abs(float(circle.attrib["cy"]) - y) < 0.001
                                    for x, y in socket_coordinates)}
        for node in root.iter():
            centre = shape_center(node)
            if centre is None:
                continue
            is_socket_centre = any(
                abs(centre[0] - x) < 0.001 and abs(centre[1] - y) < 0.001
                for x, y in socket_coordinates
            )
            if is_socket_centre:
                with self.subTest(shape=node.tag, centre=centre):
                    self.assertEqual("circle", node.tag.rsplit("}", 1)[-1])
                    self.assertIn(id(node), socket_circle_ids)

        logo = next(node for node in root.iter()
                    if node.attrib.get("id") == "wintoid-logo")
        logo_element_ids = {id(node) for node in logo.iter()}
        structural_rect_ids = {
            "channel-a-section", "channel-b-section",
        }
        structural_rect_ids.update(rail[0] for rail in panel.POSITION_RAILS)
        background = next(node for node in root if node.tag.endswith("rect"))
        output_coordinates = [(x, y) for name, x, y in panel.OUTPUT_COMPONENTS]
        for node in root.iter():
            tag = node.tag.rsplit("}", 1)[-1]
            if tag in {"ellipse", "polygon", "polyline"}:
                self.fail(f"unsupported non-socket SVG shape: {tag}")
            if tag == "path":
                self.assertIn(id(node), logo_element_ids,
                              "non-logo paths could hide an output ring")
            if tag == "rect" and node is not background \
                    and node.attrib.get("id") not in structural_rect_ids:
                bounds = shape_bounds(node)
                if any(bounds[0] <= x <= bounds[2]
                       and bounds[1] <= y <= bounds[3]
                       for x, y in output_coordinates):
                    self.fail("non-structural SVG rectangle encloses an output socket")

    def test_svg_has_sem_sections_title_and_canonical_logo(self):
        panel = self.require_panel()
        svg = panel.generate_svg()
        root = ET.fromstring(svg)
        background = next(node for node in root if node.tag.endswith("rect"))
        self.assertEqual("#ece8d9", background.attrib["fill"])
        self.assertNotEqual("#1a1a2e", background.attrib["fill"])
        self.assertEqual("#e3e0d1", panel.SECTION_FILL)
        self.assertEqual("#e7e3d4", panel.SECTION_FILL_ALT)
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
        root = ET.fromstring(panel.generate_svg())
        labels = [node for node in root.iter()
                  if node.tag.endswith("text") and node.text]
        positions = {name: (x, y) for name, x, y in panel.COMPONENTS}

        self.assertTrue(panel.LABEL_COMPONENTS)
        for label, component_name in panel.LABEL_COMPONENTS.items():
            with self.subTest(label=label):
                matches = [node for node in labels
                           if node.text == label
                           and abs(float(node.attrib["x"]) -
                                   positions[component_name][0]) < 0.001]
                self.assertEqual(1, len(matches))

        for node in labels:
            label_left, label_right = text_horizontal_bounds(node)
            label_y = float(node.attrib["y"])
            for component_name, component_x, component_y in panel.COMPONENTS:
                radius = panel.COMPONENT_RADII[component_name]
                component_left = component_x - radius
                component_right = component_x + radius
                if label_right < component_left or label_left > component_right:
                    continue
                with self.subTest(label=node.text, component=component_name):
                    self.assertGreaterEqual(
                        text_clearance_mm(node, component_x, component_y, radius),
                        MINIMUM_LABEL_CLEARANCE_MM,
                    )

        event_labels = (
            ("A_LOW_UP", "LOW", None),
            ("A_HIGH_UP", "HIGH", None),
            ("A_LOW_DOWN", "LOW", "A_LOW_UP"),
            ("A_HIGH_DOWN", "HIGH", "A_HIGH_UP"),
            ("B_LOW_UP", "LOW", None),
            ("B_HIGH_UP", "HIGH", None),
            ("B_LOW_DOWN", "LOW", "B_LOW_UP"),
            ("B_HIGH_DOWN", "HIGH", "B_HIGH_UP"),
        )
        for event_port, label, preceding_port in event_labels:
            event_x, event_y = positions[event_port]
            with self.subTest(event_port=event_port, event_label=label):
                matching = [node for node in labels
                            if node.text == label
                            and abs(float(node.attrib["x"]) - event_x) < 0.001
                            and abs(float(node.attrib["y"]) -
                                    (event_y - panel.EVENT_LABEL_OFFSET)) < 0.001]
                self.assertEqual(1, len(matching))
                if preceding_port is None or len(matching) != 1:
                    continue
                preceding_x, preceding_y = positions[preceding_port]
                self.assertGreaterEqual(
                    text_clearance_mm(
                        matching[0],
                        preceding_x,
                        preceding_y,
                        panel.COMPONENT_RADII[preceding_port],
                    ),
                    MINIMUM_LABEL_CLEARANCE_MM,
                )

    def test_position_rails_span_the_real_knob_and_width_cv_envelopes(self):
        panel = self.require_panel()
        for prefix, rail in zip(("A", "B"), panel.POSITION_RAILS):
            name, declared_x, declared_y, declared_width, declared_height = rail
            rectangle = element_by_id(panel.generate_svg(), name)
            self.assertEqual("rect", rectangle.tag.rsplit("}", 1)[-1])
            self.assertAlmostEqual(declared_x, float(rectangle.attrib["x"]), places=6)
            self.assertAlmostEqual(declared_y, float(rectangle.attrib["y"]), places=6)
            self.assertAlmostEqual(declared_width,
                                   float(rectangle.attrib["width"]), places=6)
            self.assertAlmostEqual(declared_height,
                                   float(rectangle.attrib["height"]), places=6)
            self.assertGreater(declared_width, 0.0)
            self.assertGreater(declared_height, 0.0)
            rail_top = float(rectangle.attrib["y"])
            rail_bottom = rail_top + float(rectangle.attrib["height"])
            component_y = dict((name, y) for name, _x, y in panel.COMPONENTS)
            knob_y = component_y[
                f"{prefix}_CENTER_KNOB"
            ]
            cv_y = component_y[
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
