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
EXPECTED_LIGHT_NAMES = (
    "A_INSIDE_LIGHT", "A_OUTSIDE_LIGHT", "A_LOW_UP_LIGHT",
    "A_HIGH_UP_LIGHT", "A_LOW_DOWN_LIGHT", "A_HIGH_DOWN_LIGHT",
    "B_INSIDE_LIGHT", "B_OUTSIDE_LIGHT", "B_LOW_UP_LIGHT",
    "B_HIGH_UP_LIGHT", "B_LOW_DOWN_LIGHT", "B_HIGH_DOWN_LIGHT",
    "AND_LIGHT", "OR_LIGHT", "XOR_LIGHT", "STATE_LIGHT",
)
STATUS_LIGHT_RADIUS_MM = 1.0


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


def stroked_line_bounds(node):
    if node.tag.rsplit("}", 1)[-1] != "line":
        return None
    x1 = float(node.attrib["x1"])
    y1 = float(node.attrib["y1"])
    x2 = float(node.attrib["x2"])
    y2 = float(node.attrib["y2"])
    half_stroke = abs(float(node.attrib.get("stroke-width", "0"))) / 2.0
    return (
        min(x1, x2) - half_stroke,
        min(y1, y2) - half_stroke,
        max(x1, x2) + half_stroke,
        max(y1, y2) + half_stroke,
    )


def line_descendant_bounds(node):
    bounds = [stroked_line_bounds(child) for child in node.iter()]
    bounds = [value for value in bounds if value is not None]
    if not bounds:
        return None
    return (
        min(value[0] for value in bounds),
        min(value[1] for value in bounds),
        max(value[2] for value in bounds),
        max(value[3] for value in bounds),
    )


def is_structural_line(node, panel):
    """Allow only recognizable long structure or channel-arrow strokes."""
    x1 = float(node.attrib["x1"])
    y1 = float(node.attrib["y1"])
    x2 = float(node.attrib["x2"])
    y2 = float(node.attrib["y2"])
    length = math.hypot(x2 - x1, y2 - y1)
    identifier = node.attrib.get("id", "").lower()

    # Full-width/height rules are section or divider structure, never a
    # socket-sized backplate.  The small allowance preserves the 4 mm edge
    # inset used by the SEM fields.
    if length >= panel.WIDTH_MM - 8.0 or length >= panel.HEIGHT_MM - 8.0:
        return True

    # Normalisation arrows occupy the three input rows and point between the
    # two channel input columns.  Arrowheads are short strokes at the panel
    # centre on those same rows.
    input_rows = {
        next(y for name, _x, y in panel.INPUT_COMPONENTS
             if name == input_name)
        for input_name in ("A_SIGNAL", "A_CENTER_CV", "A_WIDTH_CV")
    }
    if abs(y1 - y2) < 0.001 and any(abs(y1 - row) < 0.001 for row in input_rows):
        left = min(x1, x2)
        right = max(x1, x2)
        inner_a = panel.CHANNEL_A_X + panel.PAIR_OFFSET
        inner_b = panel.CHANNEL_B_X - panel.PAIR_OFFSET
        if left >= inner_a - 2.0 and right <= inner_b + 2.0:
            return True
    if ("arrow" in identifier or "normal" in identifier) and any(
        abs((y1 + y2) / 2.0 - row) < 1.5 for row in input_rows
    ):
        return True

    # A named rail stroke is structural only when it lies on a channel rail;
    # this prevents an output line from passing by borrowing a rail name.
    if "rail" in identifier:
        midpoint_x = (x1 + x2) / 2.0
        return any(abs(midpoint_x - channel_x) < 0.01
                   for channel_x in (panel.CHANNEL_A_X, panel.CHANNEL_B_X))
    return False


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
    if node.attrib.get("dominant-baseline") not in {"middle", "alphabetic"}:
        raise AssertionError(
            f"SVG label {node.text!r} must use a supported dominant baseline"
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
        self.assertEqual(16, panel.HP)
        self.assertAlmostEqual(81.28, panel.WIDTH_MM)
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
            # Brink V1 keeps each gate/event pair on its physical left/right
            # sides.  Treating all seven outputs as one outer column would
            # put distinct real PJ301MPort envelopes on the same centre and
            # contradict the non-overlap contract below.
            output_x_by_suffix = {
                "POSITION": outer_x,
                "INSIDE": panel.CHANNEL_A_X - panel.PAIR_OFFSET
                if prefix == "A" else panel.CHANNEL_B_X - panel.PAIR_OFFSET,
                "OUTSIDE": panel.CHANNEL_A_X + panel.PAIR_OFFSET
                if prefix == "A" else panel.CHANNEL_B_X + panel.PAIR_OFFSET,
                "LOW_UP": panel.CHANNEL_A_X - panel.PAIR_OFFSET
                if prefix == "A" else panel.CHANNEL_B_X - panel.PAIR_OFFSET,
                "HIGH_UP": panel.CHANNEL_A_X + panel.PAIR_OFFSET
                if prefix == "A" else panel.CHANNEL_B_X + panel.PAIR_OFFSET,
                "LOW_DOWN": panel.CHANNEL_A_X - panel.PAIR_OFFSET
                if prefix == "A" else panel.CHANNEL_B_X - panel.PAIR_OFFSET,
                "HIGH_DOWN": panel.CHANNEL_A_X + panel.PAIR_OFFSET
                if prefix == "A" else panel.CHANNEL_B_X + panel.PAIR_OFFSET,
            }
            for suffix, expected_x in output_x_by_suffix.items():
                with self.subTest(channel=prefix, output=suffix):
                    self.assertAlmostEqual(expected_x,
                                           positions[f"{prefix}_{suffix}"][0])

    def test_channel_pairs_are_centered_with_equal_group_box_clearance(self):
        panel = self.require_panel()
        for prefix, section_index in (("A", 0), ("B", 1)):
            _identifier, section_x, _section_y, section_width, _section_height = (
                panel.CHANNEL_SECTION_RECTS[section_index]
            )
            section_left = section_x - panel.SECTION_STROKE_WIDTH / 2.0
            section_right = (
                section_x + section_width + panel.SECTION_STROKE_WIDTH / 2.0
            )
            low_x = panel.COORDINATES[f"{prefix}_LOW_UP"][0]
            high_x = panel.COORDINATES[f"{prefix}_HIGH_UP"][0]
            channel_center = (low_x + high_x) / 2.0
            section_center = (section_left + section_right) / 2.0
            port_envelope = panel.PORT_RADIUS + panel.PORT_STROKE_WIDTH / 2.0
            left_clearance = low_x - port_envelope - section_left
            right_clearance = section_right - high_x - port_envelope

            with self.subTest(channel=prefix, constraint="center"):
                self.assertAlmostEqual(section_center, channel_center)
            with self.subTest(channel=prefix, constraint="equal clearance"):
                self.assertAlmostEqual(left_clearance, right_clearance)
            with self.subTest(channel=prefix, constraint="positive clearance"):
                self.assertGreater(left_clearance, 0.0)
                self.assertGreater(right_clearance, 0.0)

    def test_logic_outputs_follow_event_columns_with_equal_pitch(self):
        panel = self.require_panel()
        event_columns = tuple(
            panel.COORDINATES[name][0]
            for name in ("A_LOW_UP", "A_HIGH_UP", "B_LOW_UP", "B_HIGH_UP")
        )
        logic_columns = tuple(
            panel.COORDINATES[name][0]
            for name in panel.LOGIC_OUTPUT_NAMES
        )
        for event_x, logic_x in zip(event_columns, logic_columns):
            with self.subTest(event_x=event_x, logic_x=logic_x):
                self.assertAlmostEqual(event_x, logic_x)

        pitches = tuple(
            right - left
            for left, right in zip(logic_columns, logic_columns[1:])
        )
        self.assertTrue(all(pitch > 0.0 for pitch in pitches))
        for pitch in pitches[1:]:
            self.assertAlmostEqual(pitches[0], pitch)

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

    def test_status_lights_use_generated_non_overlapping_coordinates(self):
        panel = self.require_panel()
        lights = tuple(panel.LIGHT_COMPONENTS)
        self.assertEqual(
            EXPECTED_LIGHT_NAMES,
            tuple(name for name, _x, _y in lights),
        )
        self.assertEqual(len(lights), len({name for name, _x, _y in lights}))

        socket_positions = {
            name: (x, y)
            for name, x, y in panel.INPUT_COMPONENTS + panel.OUTPUT_COMPONENTS
        }
        light_to_socket = {
            name: name.removesuffix("_LIGHT")
            for name in EXPECTED_LIGHT_NAMES
        }
        light_to_socket.update({
            "AND_LIGHT": "AND_OUTPUT",
            "OR_LIGHT": "OR_OUTPUT",
            "XOR_LIGHT": "XOR_OUTPUT",
            "STATE_LIGHT": "STATE_OUTPUT",
        })
        for name, x, y in lights:
            socket_name = light_to_socket[name]
            socket_x, socket_y = socket_positions[socket_name]
            with self.subTest(light=name, socket=socket_name):
                self.assertGreaterEqual(
                    math.hypot(x - socket_x, y - socket_y),
                    RACK_PORT_RADIUS_MM + STATUS_LIGHT_RADIUS_MM,
                )
                self.assertGreaterEqual(x - STATUS_LIGHT_RADIUS_MM, 0.0)
                self.assertLessEqual(x + STATUS_LIGHT_RADIUS_MM, panel.WIDTH_MM)
                self.assertGreaterEqual(y - STATUS_LIGHT_RADIUS_MM, 0.0)
                self.assertLessEqual(y + STATUS_LIGHT_RADIUS_MM, panel.HEIGHT_MM)

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
        logo_line_ids = {
            id(node) for node in logo.iter()
            if node.tag.rsplit("}", 1)[-1] == "line"
        }
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
            if tag == "line" and id(node) not in logo_line_ids:
                bounds = stroked_line_bounds(node)
                midpoint = (
                    (float(node.attrib["x1"]) + float(node.attrib["x2"])) / 2.0,
                    (float(node.attrib["y1"]) + float(node.attrib["y2"])) / 2.0,
                )
                for output_x, output_y in output_coordinates:
                    local_radius = RACK_PORT_RADIUS_MM + 1.0
                    is_touching = (
                        bounds[0] <= output_x + local_radius
                        and bounds[2] >= output_x - local_radius
                        and bounds[1] <= output_y + local_radius
                        and bounds[3] >= output_y - local_radius
                    )
                    is_centered = (
                        abs(midpoint[0] - output_x) < 0.001
                        and abs(midpoint[1] - output_y) < 0.001
                    )
                    if is_touching or is_centered:
                        with self.subTest(output=(output_x, output_y),
                                          shape="line"):
                            self.assertTrue(
                                is_structural_line(node, panel),
                                "non-structural line artwork touches or extends "
                                "from an output socket",
                            )

        for node in root.iter():
            if node is root or node is logo:
                continue
            if node.tag.rsplit("}", 1)[-1] != "g":
                continue
            line_bounds = line_descendant_bounds(node)
            if line_bounds is None:
                continue
            bounds_centre = (
                (line_bounds[0] + line_bounds[2]) / 2.0,
                (line_bounds[1] + line_bounds[3]) / 2.0,
            )
            for output_x, output_y in output_coordinates:
                local_radius = RACK_PORT_RADIUS_MM + 1.0
                is_local = (
                    output_x - local_radius <= line_bounds[0]
                    and line_bounds[2] <= output_x + local_radius
                    and output_y - local_radius <= line_bounds[1]
                    and line_bounds[3] <= output_y + local_radius
                )
                is_centered = (
                    abs(bounds_centre[0] - output_x) < 0.001
                    and abs(bounds_centre[1] - output_y) < 0.001
                )
                if is_local or is_centered:
                    self.fail(
                        "non-structural line group is centered on or contained "
                        f"by output socket ({output_x}, {output_y})"
                    )

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
        for group_id, colour in (
            ("wint-glyphs", "#155f91"),
            ("oid-glyphs", "#ed5b22"),
        ):
            group = next(node for node in root.iter()
                         if node.attrib.get("id") == group_id)
            self.assertEqual("none", group.attrib["fill"])
            self.assertEqual(colour, group.attrib["stroke"])
            self.assertEqual("5.5", group.attrib["stroke-width"])
        for line_id, colour in (
            ("wint-underline", "#155f91"),
            ("oid-underline", "#ed5b22"),
        ):
            line = next(node for node in root.iter()
                        if node.attrib.get("id") == line_id)
            self.assertEqual(colour, line.attrib["stroke"])
            self.assertEqual("5.5", line.attrib["stroke-width"])
            self.assertEqual("butt", line.attrib["stroke-linecap"])
        self.assertIn("Brink V2", {node.text for node in root.iter()
                                    if node.tag.endswith("text")})

    def test_generated_overlay_schema_exactly_matches_svg_labels_and_lines(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())

        labels = [node for node in root.iter() if node.tag.endswith("text")]
        schema_labels = tuple(panel.PANEL_LABELS)
        self.assertEqual(len(schema_labels), len(labels))
        expected_text = (
            "Brink V2", "CHANNEL A", "CHANNEL B",
            "CENTER", "WIDTH", "SIGNAL", "POSITION", "CTR CV",
            "WID CV", "INSIDE", "OUTSIDE",
            "LOW", "HIGH", "LOW", "HIGH", "↑", "↓",
            "CENTER", "WIDTH", "SIGNAL", "POSITION", "CTR CV",
            "WID CV", "INSIDE", "OUTSIDE",
            "LOW", "HIGH", "LOW", "HIGH", "↑", "↓",
            "AND", "OR", "XOR", "TOGGLE",
        )
        self.assertEqual(expected_text, tuple(label.text for label in schema_labels))
        for spec, node in zip(schema_labels, labels):
            with self.subTest(label=spec.identifier):
                self.assertEqual(spec.text, node.text)
                self.assertAlmostEqual(spec.x, float(node.attrib["x"]), places=6)
                self.assertAlmostEqual(spec.y, float(node.attrib["y"]), places=6)
                self.assertAlmostEqual(spec.size,
                                       float(node.attrib["font-size"]), places=6)
                self.assertEqual(spec.fill, node.attrib["fill"])
                self.assertEqual(spec.anchor, node.attrib["text-anchor"])
                self.assertEqual(spec.weight, node.attrib["font-weight"])
                expected_baseline = (
                    "alphabetic" if getattr(spec, "vertical_align", "middle") == "baseline"
                    else "middle"
                )
                self.assertEqual(expected_baseline,
                                 node.attrib["dominant-baseline"])

        schema_lines = tuple(panel.PANEL_LINES)
        svg_lines = [
            node for node in root.iter()
            if node.tag.endswith("line")
            and node.attrib.get("id", "").startswith("normalisation-")
        ]
        self.assertEqual(len(schema_lines), len(svg_lines))
        for spec, node in zip(schema_lines, svg_lines):
            with self.subTest(line=spec.identifier):
                self.assertEqual(spec.identifier, node.attrib["id"])
                for attribute in ("x1", "y1", "x2", "y2"):
                    self.assertAlmostEqual(
                        getattr(spec, attribute),
                        float(node.attrib[attribute]),
                        places=6,
                    )
                self.assertEqual(spec.stroke, node.attrib["stroke"])
                self.assertAlmostEqual(
                    spec.stroke_width,
                    float(node.attrib["stroke-width"]),
                    places=6,
                )

        event_specs = [
            label for label in schema_labels
            if label.text in {"LOW", "HIGH", "↑", "↓"}
        ]
        self.assertTrue(event_specs)
        self.assertEqual({panel.LEGEND_CHARCOAL},
                         {label.fill for label in event_specs})
        event_labels = [label for label in event_specs
                        if label.text in {"LOW", "HIGH"}]
        arrow_labels = [label for label in event_specs
                        if label.text in {"↑", "↓"}]
        self.assertEqual({"400"}, {label.weight for label in event_labels})
        self.assertEqual({"700"}, {label.weight for label in arrow_labels})
        self.assertEqual({panel.EVENT_LABEL_FONT_SIZE},
                         {label.size for label in event_labels})
        self.assertTrue(all(label.size > panel.EVENT_LABEL_FONT_SIZE
                            for label in arrow_labels))
        self.assertEqual({panel.FUNCTION_ORANGE},
                         {line.stroke for line in schema_lines})

    def test_event_arrows_are_larger_and_centered_between_event_sockets(self):
        panel = self.require_panel()
        positions = {
            name: (x, y)
            for name, x, y in panel.COMPONENTS
        }
        labels = {
            label.identifier: label
            for label in panel.PANEL_LABELS
            if label.text in {"↑", "↓"}
        }
        self.assertGreater(panel.EVENT_ARROW_FONT_SIZE, 3.0)
        for prefix in ("A", "B"):
            for direction, suffix, arrow_y in (
                ("up", "UP", panel.Y_EVENTS_UP),
                ("down", "DOWN", panel.Y_EVENTS_DOWN),
            ):
                low_x, low_y = positions[f"{prefix}_LOW_{suffix}"]
                high_x, high_y = positions[f"{prefix}_HIGH_{suffix}"]
                arrow = labels[f"{prefix.lower()}-event-{direction}-arrow"]
                with self.subTest(channel=prefix, direction=direction,
                                  alignment="horizontal"):
                    self.assertAlmostEqual(
                        (low_x + high_x) / 2.0,
                        arrow.x,
                    )
                with self.subTest(channel=prefix, direction=direction,
                                  alignment="vertical"):
                    self.assertAlmostEqual(arrow_y, low_y)
                    self.assertAlmostEqual(arrow_y, high_y)
                    self.assertAlmostEqual(arrow_y, arrow.y)
                with self.subTest(channel=prefix, direction=direction,
                                  typography="size"):
                    self.assertAlmostEqual(
                        panel.EVENT_ARROW_FONT_SIZE,
                        arrow.size,
                    )

    def test_labels_favor_their_associated_control_or_socket_below(self):
        panel = self.require_panel()
        labels = {label.identifier: label for label in panel.PANEL_LABELS}
        positions = {name: (x, y) for name, x, y in panel.COMPONENTS}

        # These rows are 12 mm apart.  The label midpoint should be below the
        # midpoint between the preceding and associated component, making it
        # visually nearer to the item it names.
        for prefix in ("A", "B"):
            for suffix, preceding_suffix in (
                ("CENTER_CV", "SIGNAL"),
                ("WIDTH_CV", "CENTER_CV"),
                ("INSIDE", "WIDTH_ATTEN"),
                ("OUTSIDE", "WIDTH_CV"),
                ("LOW_DOWN", "LOW_UP"),
                ("HIGH_DOWN", "HIGH_UP"),
            ):
                component_name = f"{prefix}_{suffix}"
                preceding_name = f"{prefix}_{preceding_suffix}"
                label_id = (
                    f"{prefix.lower()}-{suffix.lower().replace('_', '-')}-label"
                )
                component_y = positions[component_name][1]
                preceding_y = positions[preceding_name][1]
                label_y = labels[label_id].y
                with self.subTest(channel=prefix, label=label_id):
                    self.assertLess(component_y - label_y,
                                    label_y - preceding_y)

        # The first-row control labels receive the same small downward nudge;
        # retain enough separation from the group headings above them.
        self.assertLess(panel.KNOB_LABEL_OFFSET, 6.0)
        self.assertLess(panel.PORT_LABEL_OFFSET, 6.0)
        self.assertLess(panel.EVENT_LABEL_OFFSET, 6.0)

    def test_channel_headings_are_bold_in_svg_and_rack_schema(self):
        panel = self.require_panel()
        headings = {
            label.identifier: label
            for label in panel.PANEL_LABELS
            if label.identifier in {"channel-a-heading", "channel-b-heading"}
        }
        self.assertEqual({"700"}, {label.weight for label in headings.values()})

        svg_labels = [
            node for node in ET.fromstring(panel.generate_svg()).iter()
            if node.tag.endswith("text")
        ]
        for identifier, heading in headings.items():
            with self.subTest(heading=identifier):
                matches = [
                    node for node in svg_labels
                    if node.text == heading.text
                    and abs(float(node.attrib["x"]) - heading.x) < 0.001
                ]
                self.assertEqual(1, len(matches))
                self.assertEqual("700", matches[0].attrib["font-weight"])

        header = panel.generate_coords_header()
        for text in ("CHANNEL A", "CHANNEL B"):
            with self.subTest(schema=text):
                self.assertIn(
                    f'"{text}", LABEL_ALIGN_CENTER, LABEL_VERTICAL_MIDDLE, true',
                    header,
                )

    def test_title_uses_the_shared_v2_baseline_and_logic_divider_is_absent(self):
        panel = self.require_panel()
        self.assertEqual("baseline",
                         getattr(panel.PANEL_LABELS[0], "vertical_align", None))
        self.assertTrue(all(
            getattr(label, "vertical_align", "middle") == "middle"
            for label in panel.PANEL_LABELS[1:]
        ))
        self.assertNotIn('id="logic-divider"', panel.generate_svg())
        self.assertNotIn("LOGIC_DIVIDER", panel.generate_coords_header())

    def test_svg_painted_sections_and_socket_guides_keep_edge_margin(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())
        clearances = []

        for identifier in ("channel-a-section", "channel-b-section"):
            section = element_by_id(panel.generate_svg(), identifier)
            half_stroke = abs(float(section.attrib.get("stroke-width", "0"))) / 2.0
            x = float(section.attrib["x"])
            y = float(section.attrib["y"])
            width = float(section.attrib["width"])
            height = float(section.attrib["height"])
            clearances.extend((
                x - half_stroke,
                y - half_stroke,
                panel.WIDTH_MM - x - width - half_stroke,
                panel.HEIGHT_MM - y - height - half_stroke,
            ))

        socket_coordinates = {
            (x, y) for _name, x, y in
            tuple(panel.INPUT_COMPONENTS) + tuple(panel.OUTPUT_COMPONENTS)
        }
        for circle in root.iter():
            if circle.tag.rsplit("}", 1)[-1] != "circle":
                continue
            centre = (float(circle.attrib["cx"]), float(circle.attrib["cy"]))
            if centre not in socket_coordinates:
                continue
            painted_radius = (
                float(circle.attrib["r"])
                + abs(float(circle.attrib.get("stroke-width", "0"))) / 2.0
            )
            clearances.extend((
                centre[0] - painted_radius,
                centre[1] - painted_radius,
                panel.WIDTH_MM - centre[0] - painted_radius,
                panel.HEIGHT_MM - centre[1] - painted_radius,
            ))

        self.assertGreaterEqual(min(clearances), MINIMUM_EDGE_CLEARANCE_MM)

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
