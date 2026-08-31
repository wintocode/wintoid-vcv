#!/usr/bin/env python3

import importlib.util
import json
import math
import pathlib
import re
import tempfile
import unittest
import xml.etree.ElementTree as ET

from panel_geometry import centered_stroke_outer_radius


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "generate_panel_four_v2.py"
LOGO_SCRIPT = ROOT / "scripts" / "generate_wintoid_logo.py"
PANEL_SVG = ROOT / "res" / "FourV2.svg"
LOGO_SVG = ROOT / "res" / "WintoidLogo.svg"
LAYOUT_HEADER = ROOT / "src" / "FourV2" / "layout.h"
GLYPH_DATA = ROOT / "scripts" / "assets" / "wintoid_logo_glyphs.json"
SOURCE_CPP = ROOT / "src" / "FourV2" / "FourV2.cpp"

RACK_PIXELS_PER_MM = 15.0 / 5.08
RACK_SMALL_KNOB_RADIUS_MM = 22.67581 / (2.0 * RACK_PIXELS_PER_MM)
RACK_PORT_RADIUS_MM = 23.7 / (2.0 * RACK_PIXELS_PER_MM)
MINIMUM_EDGE_CLEARANCE_MM = 4.0
MINIMUM_LABEL_CLEARANCE_MM = 0.25

_FLOAT_TOKEN = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
_PATH_TOKEN_RE = re.compile(rf"[A-Za-z]|{_FLOAT_TOKEN}")
def _quadratic_x_values(start, control, end):
    values = [start, end]
    denominator = start - 2.0 * control + end
    if denominator:
        t = (start - control) / denominator
        if 0.0 < t < 1.0:
            values.append(
                (1.0 - t) ** 2 * start
                + 2.0 * (1.0 - t) * t * control
                + t ** 2 * end
            )
    return values


def _cubic_value(start, control_one, control_two, end, t):
    inverse = 1.0 - t
    return (
        inverse ** 3 * start
        + 3.0 * inverse ** 2 * t * control_one
        + 3.0 * inverse * t ** 2 * control_two
        + t ** 3 * end
    )


def _cubic_x_values(start, control_one, control_two, end):
    values = [start, end]
    a = -start + 3.0 * control_one - 3.0 * control_two + end
    b = 2.0 * (start - 2.0 * control_one + control_two)
    c = control_one - start
    if abs(a) < 1e-12:
        roots = [-c / b] if abs(b) >= 1e-12 else []
    else:
        discriminant = b * b - 4.0 * a * c
        if discriminant < 0.0:
            roots = []
        else:
            root = math.sqrt(discriminant)
            roots = [
                (-b + root) / (2.0 * a),
                (-b - root) / (2.0 * a),
            ]
    for t in roots:
        if 0.0 < t < 1.0:
            values.append(_cubic_value(start, control_one, control_two, end, t))
    return values


def _path_x_bounds(path_data):
    """Measure absolute/relative M/L/Q/C/Z SVG path geometry locally."""
    tokens = _PATH_TOKEN_RE.findall(path_data)
    remainder = _PATH_TOKEN_RE.sub("", path_data).replace(",", "")
    if remainder.strip():
        raise ValueError(f"unsupported SVG path syntax: {remainder!r}")

    arities = {"M": 2, "L": 2, "Q": 4, "C": 6}
    cursor = 0
    command = None
    current = (0.0, 0.0)
    contour_start = None
    xs = []
    while cursor < len(tokens):
        token = tokens[cursor]
        if token.isalpha():
            command = token
            cursor += 1
            if command.upper() == "Z":
                if contour_start is None:
                    raise ValueError("closed SVG path has no contour start")
                xs.extend((current[0], contour_start[0]))
                current = contour_start
                contour_start = None
                command = None
            continue
        if command is None or command.upper() == "Z":
            raise ValueError("SVG path numbers are missing a command")
        operation = command.upper()
        if operation not in arities:
            raise ValueError(f"unsupported SVG path command: {command}")
        arity = arities[operation]
        if cursor + arity > len(tokens):
            raise ValueError(f"incomplete SVG path command: {command}")
        arguments = tokens[cursor:cursor + arity]
        if any(argument.isalpha() for argument in arguments):
            raise ValueError(f"incomplete SVG path command: {command}")
        values = [float(argument) for argument in arguments]
        relative = command.islower()

        def point(x, y):
            if relative:
                return current[0] + x, current[1] + y
            return x, y

        if operation == "M":
            current = point(values[0], values[1])
            contour_start = current
            xs.append(current[0])
            command = "l" if relative else "L"
        elif operation == "L":
            current = point(values[0], values[1])
            xs.append(current[0])
        elif operation == "Q":
            control = point(values[0], values[1])
            end = point(values[2], values[3])
            xs.extend(_quadratic_x_values(current[0], control[0], end[0]))
            current = end
        elif operation == "C":
            control_one = point(values[0], values[1])
            control_two = point(values[2], values[3])
            end = point(values[4], values[5])
            xs.extend(
                _cubic_x_values(
                    current[0], control_one[0], control_two[0], end[0]
                )
            )
            current = end
        cursor += arity
    if not xs:
        raise ValueError("SVG path has no geometry")
    return min(xs), max(xs)


def _extract_struct_body(source, marker):
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise AssertionError(f"unterminated struct: {marker}")


def load_generator(path, name):
    if not path.exists():
        return None
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FourV2PanelTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.panel = load_generator(SCRIPT, "four_v2_panel")
        cls.source = SOURCE_CPP.read_text(encoding="utf-8")

    def require_panel(self):
        self.assertIsNotNone(
            self.panel,
            "Task 4 generator does not exist yet: scripts/generate_panel_four_v2.py",
        )
        return self.panel

    def test_dimensions_are_32_hp(self):
        panel = self.require_panel()
        self.assertEqual(32, panel.HP)
        self.assertAlmostEqual(162.56, panel.WIDTH_MM)
        self.assertAlmostEqual(128.5, panel.HEIGHT_MM)

    def test_sem_instrument_palette_is_exact(self):
        panel = self.require_panel()
        self.assertEqual("#ece8d9", panel.PANEL_IVORY)
        self.assertEqual("#242522", panel.LEGEND_CHARCOAL)
        self.assertEqual("#556d80", panel.SECTION_BLUE_GREY)
        self.assertEqual("#b7693c", panel.FUNCTION_ORANGE)
        self.assertEqual("#1a1a2e", panel.LOGO_BLUE)
        self.assertEqual("#ff4d00", panel.LOGO_ORANGE)

    def test_four_operator_sections_are_framed(self):
        panel = self.require_panel()
        self.assertEqual(4, len(panel.OPERATOR_SECTION_RECTS))
        self.assertEqual(4, len(panel.PATCHBAY_COLUMN_XS))
        self.assertEqual(
            ("Output", "Warp", "Fold", "Feedback"),
            tuple(panel.PATCHBAY_ROWS),
        )

    def test_logo_is_outlined_and_split_only_between_glyph_groups(self):
        self.assertTrue(LOGO_SCRIPT.exists(), "logo generator is missing")
        self.assertTrue(LOGO_SVG.exists(), "canonical logo SVG is missing")
        logo = ET.parse(LOGO_SVG).getroot()
        self.assertEqual([], [node for node in logo.iter()
                              if node.tag.endswith("text")])
        self.assertEqual([], [node for node in logo.iter()
                              if node.tag.endswith("use")])
        ids = {node.attrib.get("id") for node in logo.iter()}
        self.assertTrue({"wint-glyphs", "oid-glyphs",
                         "wint-underline", "oid-underline"}.issubset(ids))
        self.assertEqual(7, len([node for node in logo.iter()
                                 if node.tag.endswith("path") and
                                 node.attrib.get("data-glyph")]))
        colours = {node.attrib.get("fill") for node in logo.iter()
                   if node.tag.endswith("path")}
        self.assertEqual({"#1a1a2e", "#ff4d00"}, colours)

    def test_panel_logo_is_rendered_at_half_size(self):
        panel = self.require_panel()
        self.assertAlmostEqual(0.06, panel.LOGO_SCALE)
        root = ET.fromstring(panel.generate_svg())
        logo = next(node for node in root.iter()
                    if node.attrib.get("id") == "wintoid-logo")
        self.assertRegex(logo.attrib["transform"], r"scale\(0\.06\)")

    def test_all_physical_components_clear_panel_edges(self):
        panel = self.require_panel()
        for name, x, y in panel.COMPONENTS:
            radius = panel.COMPONENT_RADII[name]
            with self.subTest(name=name):
                self.assertGreaterEqual(x - radius, MINIMUM_EDGE_CLEARANCE_MM)
                self.assertLessEqual(
                    x + radius, panel.WIDTH_MM - MINIMUM_EDGE_CLEARANCE_MM)
                self.assertGreaterEqual(y - radius, MINIMUM_EDGE_CLEARANCE_MM)
                self.assertLessEqual(
                    y + radius, panel.HEIGHT_MM - MINIMUM_EDGE_CLEARANCE_MM)

    def test_adjacent_real_rack_widget_envelopes_do_not_overlap(self):
        panel = self.require_panel()
        components = list(panel.COMPONENTS)
        for index, (name_a, ax, ay) in enumerate(components):
            ar = panel.COMPONENT_RADII[name_a]
            for name_b, bx, by in components[index + 1:]:
                br = panel.COMPONENT_RADII[name_b]
                distance = ((ax - bx) ** 2 + (ay - by) ** 2) ** 0.5
                with self.subTest(first=name_a, second=name_b):
                    self.assertGreaterEqual(distance + 1e-9, ar + br)

    def test_labels_clear_component_envelopes(self):
        panel = self.require_panel()
        self.assertGreaterEqual(panel.MINIMUM_LABEL_CLEARANCE_MM,
                                MINIMUM_LABEL_CLEARANCE_MM)
        for label, spec in panel.LABEL_CLEARANCES.items():
            with self.subTest(label=label):
                self.assertGreaterEqual(spec["clearance_mm"],
                                        MINIMUM_LABEL_CLEARANCE_MM)

    def test_routing_display_is_approximately_21_mm_high(self):
        panel = self.require_panel()
        x, y, width, height = panel.DISPLAY_RECTS["ROUTING"]
        self.assertGreaterEqual(width, 45.0)
        self.assertAlmostEqual(21.0, height, delta=1.0)
        self.assertGreaterEqual(x, 0.0)
        self.assertGreaterEqual(y, 0.0)
        self.assertLessEqual(x + width, panel.WIDTH_MM)
        self.assertLessEqual(y + height, panel.HEIGHT_MM)

    def test_algorithm_knob_is_left_of_and_inside_routing_display(self):
        panel = self.require_panel()
        display_x, display_y, _display_width, display_height = \
            panel.DISPLAY_RECTS["ROUTING"]
        algorithm_x, algorithm_y = panel.GLOBAL_CONTROLS["algorithm_knob"]
        algorithm_radius = panel.COMPONENT_RADII["algorithm_knob"]
        self.assertLess(algorithm_x + algorithm_radius, display_x)
        self.assertGreaterEqual(algorithm_y, display_y)
        self.assertLessEqual(algorithm_y, display_y + display_height)

    def test_frequency_displays_have_equal_dimensions_and_fit(self):
        panel = self.require_panel()
        rectangles = tuple(panel.FREQUENCY_DISPLAY_RECTS)
        self.assertEqual(4, len(rectangles))
        dimensions = {(round(rect[2], 6), round(rect[3], 6))
                      for rect in rectangles}
        self.assertEqual(1, len(dimensions))
        for x, y, width, height in rectangles:
            self.assertGreater(width, 0.0)
            self.assertGreater(height, 0.0)
            self.assertGreaterEqual(x, 0.0)
            self.assertGreaterEqual(y, 0.0)
            self.assertLessEqual(x + width, panel.WIDTH_MM)
            self.assertLessEqual(y + height, panel.HEIGHT_MM)

    def test_operator_headers_are_large_left_aligned_and_share_one_line(self):
        panel = self.require_panel()
        root = ET.fromstring(PANEL_SVG.read_text(encoding="utf-8"))
        for index, section in enumerate(panel.OPERATOR_SECTION_RECTS, start=1):
            heading = next(
                node for node in root.iter()
                if node.tag.endswith("text") and node.text == f"OP{index}"
            )
            display = next(
                node for node in root.iter()
                if node.tag.endswith("rect")
                and node.attrib.get("id") == f"op{index}-frequency-display"
            )
            heading_x = float(heading.attrib["x"])
            heading_y = float(heading.attrib["y"])
            display_x = float(display.attrib["x"])
            display_y = float(display.attrib["y"])
            display_height = float(display.attrib["height"])
            with self.subTest(operator=index, alignment="left"):
                self.assertEqual("start", heading.attrib["text-anchor"])
                self.assertAlmostEqual(section[0] + 4.0, heading_x)
            with self.subTest(operator=index, typography="heading"):
                self.assertAlmostEqual(5.0, float(heading.attrib["font-size"]))
            with self.subTest(operator=index, layout="same-line"):
                self.assertGreater(display_x, heading_x)
                self.assertLessEqual(display_y, heading_y)
                self.assertGreaterEqual(display_y + display_height, heading_y)
                self.assertGreaterEqual(display_height, 5.5)

        labels = {
            node.text for node in root.iter()
            if node.tag.endswith("text") and node.text
        }
        self.assertNotIn("FREQ", labels)

    def test_global_labels_are_above_their_controls(self):
        panel = self.require_panel()
        root = ET.fromstring(PANEL_SVG.read_text(encoding="utf-8"))
        label_specs = (
            ("V/OCT", "voct_jack"),
            ("ALGO", "algorithm_knob"),
            ("TUNE", "tune_knob"),
            ("PM DEPTH", "pm_depth_knob"),
            ("MASTER", "master_knob"),
            ("EXT PM", "external_pm_jack"),
            ("MAIN OUT", "main_output"),
        )
        expected_size = {
            text: 1.90 if text == "MAIN OUT" else 2.25
            for text, _component in label_specs
        }
        expected_baseline = {
            "V/OCT": 12.6,
            "ALGO": 24.1,
            "TUNE": 12.6,
            "PM DEPTH": 12.6,
            "MASTER": 12.6,
            "EXT PM": 29.0,
            "MAIN OUT": 12.3,
        }
        for text, component_name in label_specs:
            component_x, component_y = panel.GLOBAL_CONTROLS[component_name]
            expected_x = 100.737 if text == "EXT PM" else component_x
            matches = [
                node for node in root.iter()
                if node.tag.endswith("text")
                and node.text == text
                and abs(float(node.attrib["x"]) - expected_x) < 0.01
            ]
            with self.subTest(label=text, component=component_name,
                              contract="single matching label"):
                self.assertEqual(1, len(matches))
            if len(matches) != 1:
                continue
            label = matches[0]
            baseline = float(label.attrib["y"])
            font_size = float(label.attrib["font-size"])
            label_bottom = baseline + font_size * 0.25
            radius = panel.COMPONENT_RADII[component_name]
            with self.subTest(label=text, component=component_name):
                expected_anchor = "end" if text == "EXT PM" else "middle"
                self.assertEqual(expected_anchor, label.attrib["text-anchor"])
                self.assertAlmostEqual(expected_size[text], font_size)
                self.assertAlmostEqual(expected_baseline[text], baseline)
                if text == "EXT PM":
                    self.assertEqual("middle", label.attrib["dominant-baseline"])
                    self.assertLess(float(label.attrib["x"]), component_x)
                    continue
                self.assertGreaterEqual(
                    component_y - radius - label_bottom,
                    MINIMUM_LABEL_CLEARANCE_MM,
                )
                self.assertGreaterEqual(
                    baseline - font_size,
                    panel.SHARED_IO_SECTION[1],
                )

    def test_global_controls_match_the_revised_control_order(self):
        panel = self.require_panel()
        op1_left_column_x = (
            panel.OPERATOR_CENTRES_X[0] + panel.OPERATOR_X_OFFSETS["coarse"]
        )
        self.assertAlmostEqual(
            op1_left_column_x, panel.GLOBAL_CONTROLS["voct_jack"][0], delta=0.001
        )
        self.assertAlmostEqual(
            op1_left_column_x, panel.GLOBAL_CONTROLS["algorithm_knob"][0], delta=0.001
        )
        self.assertEqual(
            (94.5, 17.5), panel.GLOBAL_CONTROLS["pm_depth_knob"]
        )
        self.assertEqual(
            (106.5, 17.5), panel.GLOBAL_CONTROLS["pm_depth_cv_jack"]
        )
        self.assertEqual(
            (118.5, 17.5), panel.GLOBAL_CONTROLS["pm_depth_cv_atten"]
        )
        self.assertEqual(
            (106.5, 29.0), panel.GLOBAL_CONTROLS["external_pm_jack"]
        )
        self.assertEqual(
            (118.5, 29.0), panel.GLOBAL_CONTROLS["external_pm_atten"]
        )
        master_x, master_y = panel.GLOBAL_CONTROLS["master_knob"]
        output_x, output_y = panel.GLOBAL_CONTROLS["main_output"]
        over_x, over_y = panel.GLOBAL_CONTROLS["over_light"]
        self.assertEqual((141.0, 17.5), (master_x, master_y))
        pm_depth_x, pm_depth_y = panel.GLOBAL_CONTROLS["pm_depth_knob"]
        pm_cv_x, pm_cv_y = panel.GLOBAL_CONTROLS["pm_depth_cv_jack"]
        self.assertLess(pm_depth_x, pm_cv_x)
        self.assertAlmostEqual(pm_depth_y, pm_cv_y)
        self.assertLess(master_x, output_x)
        self.assertAlmostEqual(master_y, output_y)
        self.assertAlmostEqual(over_x, output_x)
        self.assertEqual((153.0, 24.0), (over_x, over_y))

    def test_four_v2_removes_pm_cv_caption_and_moves_operator_headings_up(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())
        labels = {node.text for node in root.iter()
                  if node.tag.endswith("text") and node.text}
        self.assertNotIn("PM CV", labels)
        self.assertNotIn("pm_depth_cv", panel.LABEL_CLEARANCES)
        self.assertAlmostEqual(42.7, panel.OPERATOR_HEADING_Y)

    def test_algorithm_routing_uses_orthogonal_paths(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())
        routing_art = next(
            node for node in root.iter()
            if node.attrib.get("id") == "routing-display-art"
        )
        paths = [node for node in routing_art if node.tag.endswith("path")]
        self.assertTrue(paths)
        for path in paths:
            with self.subTest(path=path.attrib["d"]):
                self.assertNotIn(" C ", path.attrib["d"])
                self.assertGreaterEqual(path.attrib["d"].count("L"), 2)
        body = _extract_struct_body(
            self.source, "struct AlgorithmRoutingDisplay"
        )
        self.assertNotIn("nvgBezierTo", body)
        self.assertIn("nvgLineTo", body)
        for arrow_contract in ("arrowLength", "arrowWidth", "arrowBaseX"):
            with self.subTest(arrow_contract=arrow_contract):
                self.assertNotIn(arrow_contract, body)
        self.assertIn("sourceTrunk", body)
        self.assertIn("outgoingCount > 1", body)

    def test_socket_attenuator_pairs_have_small_rounded_group_boxes(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())
        expected_pairs = {
            "pm-depth-cv-group": ("pm_depth_cv_jack", "pm_depth_cv_atten"),
            "external-pm-group": ("external_pm_jack", "external_pm_atten"),
        }
        for operator in range(1, 5):
            for parameter in ("output", "warp", "fold", "feedback"):
                expected_pairs[f"op{operator}-{parameter}-cv-group"] = (
                    f"op{operator}_{parameter}_cv_input",
                    f"op{operator}_{parameter}_cv_atten",
                )

        group_boxes = {
            node.attrib.get("id"): node
            for node in root.iter()
            if node.tag.endswith("rect")
            and node.attrib.get("id") in expected_pairs
        }
        self.assertEqual(set(expected_pairs), set(group_boxes))
        components = {
            name: (x, y)
            for name, x, y in panel.COMPONENTS
        }
        for identifier, (socket_name, atten_name) in expected_pairs.items():
            group = group_boxes[identifier]
            group_x = float(group.attrib["x"])
            group_y = float(group.attrib["y"])
            group_width = float(group.attrib["width"])
            group_height = float(group.attrib["height"])
            socket_x, socket_y = components[socket_name]
            atten_x, atten_y = components[atten_name]
            socket_radius = panel.COMPONENT_RADII[socket_name]
            atten_radius = panel.COMPONENT_RADII[atten_name]
            with self.subTest(group=identifier, style="outline"):
                self.assertEqual("none", group.attrib["fill"])
                self.assertEqual(panel.SECTION_BLUE_GREY,
                                 group.attrib["stroke"])
                self.assertGreater(float(group.attrib["rx"]), 0.0)
            with self.subTest(group=identifier, enclosure="horizontal"):
                self.assertLessEqual(group_x, socket_x - socket_radius)
                self.assertGreaterEqual(
                    group_x + group_width,
                    atten_x + atten_radius,
                )
            with self.subTest(group=identifier, enclosure="vertical"):
                self.assertAlmostEqual(socket_y, atten_y)
                self.assertLessEqual(group_y, socket_y - socket_radius)
                self.assertGreaterEqual(
                    group_y + group_height,
                    socket_y + socket_radius,
                )
            with self.subTest(group=identifier, size="small"):
                expected_width = (
                    22.353 if identifier in {
                        "pm-depth-cv-group", "external-pm-group"
                    } else 20.853
                )
                self.assertAlmostEqual(expected_width, group_width, delta=0.001)
                self.assertAlmostEqual(9.126, group_height, delta=0.001)

    def test_coarse_and_fine_controls_have_their_own_rounded_group_boxes(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())
        group_boxes = {
            node.attrib.get("id"): node
            for node in root.iter()
            if node.tag.endswith("rect")
            and node.attrib.get("id", "").endswith("-coarse-fine-group")
        }
        self.assertEqual(
            {f"op{operator}-coarse-fine-group" for operator in range(1, 5)},
            set(group_boxes),
        )
        components = {
            name: (x, y)
            for name, x, y in panel.COMPONENTS
        }
        for operator in range(1, 5):
            identifier = f"op{operator}-coarse-fine-group"
            group = group_boxes[identifier]
            group_x = float(group.attrib["x"])
            group_y = float(group.attrib["y"])
            group_width = float(group.attrib["width"])
            group_height = float(group.attrib["height"])
            coarse_x, coarse_y = components[f"op{operator}_coarse"]
            fine_x, fine_y = components[f"op{operator}_fine"]
            coarse_radius = panel.COMPONENT_RADII[f"op{operator}_coarse"]
            fine_radius = panel.COMPONENT_RADII[f"op{operator}_fine"]
            mode_x, _mode_y = components[f"op{operator}_freq_mode"]
            with self.subTest(operator=operator, style="outline"):
                self.assertEqual("none", group.attrib["fill"])
                self.assertEqual(panel.SECTION_BLUE_GREY,
                                 group.attrib["stroke"])
                self.assertGreater(float(group.attrib["rx"]), 0.0)
            with self.subTest(operator=operator, enclosure="controls"):
                self.assertLessEqual(group_x, coarse_x - coarse_radius)
                self.assertGreaterEqual(
                    group_x + group_width,
                    fine_x + fine_radius,
                )
                self.assertLessEqual(group_y, coarse_y - coarse_radius)
                self.assertGreaterEqual(
                    group_y + group_height,
                    fine_y + fine_radius,
                )
            with self.subTest(operator=operator, separation="mode"):
                self.assertLess(group_x + group_width,
                                mode_x - panel.STATE_SWITCH_WIDTH / 2.0)

    def test_enlarged_coarse_labels_keep_visible_group_padding(self):
        panel = self.require_panel()
        self.assertAlmostEqual(1.25,
                               panel.FREQUENCY_CONTROL_GROUP_PADDING_X)
        minimum_width = 2.0 * (
            panel.SMALL_KNOB_RADIUS
            + panel.FREQUENCY_CONTROL_GROUP_PADDING_X
        )
        for identifier, (_x, _y, width, _height) in panel.FREQUENCY_CONTROL_GROUP_RECTS:
            with self.subTest(group=identifier):
                self.assertGreaterEqual(width, minimum_width)

    def test_routing_display_uses_readable_nodes_and_branch_lanes(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())
        routing_art = next(
            node for node in root.iter()
            if node.attrib.get("id") == "routing-display-art"
        )
        circles = [
            node for node in routing_art if node.tag.endswith("circle")
        ]
        labels = [
            node for node in routing_art if node.tag.endswith("text")
        ]
        strokes = [
            node for node in routing_art
            if node.tag.endswith(("path", "line"))
        ]
        self.assertEqual(4, len(circles))
        self.assertEqual(4, len(labels))
        self.assertTrue(strokes)
        self.assertTrue(all(float(node.attrib["r"]) == 1.5
                            for node in circles))
        self.assertTrue(all(float(node.attrib["font-size"]) == 2.1
                            for node in labels))
        self.assertTrue(all(float(node.attrib["stroke-width"]) == 0.5
                            for node in strokes))
        self.assertAlmostEqual(1.5, panel.ROUTING_NODE_RADIUS)
        self.assertAlmostEqual(0.5, panel.ROUTING_EDGE_STROKE_WIDTH)
        self.assertAlmostEqual(0.4, panel.ROUTING_NODE_STROKE_WIDTH)
        self.assertAlmostEqual(2.1, panel.ROUTING_NODE_LABEL_SIZE)
        self.assertAlmostEqual(0.7, panel.ROUTING_BRANCH_OFFSET)
        routing_body = _extract_struct_body(
            self.source, "struct AlgorithmRoutingDisplay"
        )
        for contract in ("sourceOffset", "destinationOffset",
                         "ROUTING_BRANCH_OFFSET"):
            with self.subTest(contract=contract):
                self.assertIn(contract, routing_body)

    def test_operator_parameter_controls_share_rows_with_their_cv_controls(self):
        panel = self.require_panel()
        components = {
            name: (x, y)
            for name, x, y in panel.COMPONENTS
        }
        for operator in range(1, 5):
            coarse = components[f"op{operator}_coarse"]
            mode = components[f"op{operator}_freq_mode"]
            fine = components[f"op{operator}_fine"]
            fold_type = components[f"op{operator}_fold_type"]
            with self.subTest(operator=operator, row="coarse-mode"):
                self.assertAlmostEqual(coarse[1], mode[1])
                self.assertLess(coarse[0], mode[0])
            with self.subTest(operator=operator, row="fine-type"):
                self.assertAlmostEqual(fine[1], fold_type[1])
                self.assertLess(fine[0], fold_type[0])
                self.assertGreater(fine[1], coarse[1])

            parameter_y = []
            for parameter in ("output", "warp", "fold", "feedback"):
                knob = components[f"op{operator}_{parameter}"]
                cv_input = components[f"op{operator}_{parameter}_cv_input"]
                atten = components[f"op{operator}_{parameter}_cv_atten"]
                parameter_y.append(knob[1])
                with self.subTest(operator=operator, parameter=parameter):
                    self.assertAlmostEqual(knob[1], cv_input[1])
                    self.assertAlmostEqual(knob[1], atten[1])
                    self.assertLess(knob[0], cv_input[0])
                    self.assertLess(cv_input[0], atten[0])
            with self.subTest(operator=operator, alignment="left-column"):
                output_x = components[f"op{operator}_output"][0]
                self.assertAlmostEqual(coarse[0], output_x)
                self.assertAlmostEqual(fine[0], output_x)
            self.assertEqual(sorted(parameter_y), parameter_y)
            self.assertGreater(parameter_y[0], fine[1])

    def test_operator_sections_absorb_the_cv_bays(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())
        rect_ids = {
            node.attrib.get("id") for node in root.iter()
            if node.tag.endswith("rect")
        }
        self.assertEqual(
            [],
            sorted(identifier for identifier in rect_ids
                   if identifier and identifier.startswith("cv-patchbay-section-")),
        )
        self.assertEqual(4, len(panel.OPERATOR_SECTION_RECTS))
        for left, top, width, height in panel.OPERATOR_SECTION_RECTS:
            self.assertAlmostEqual(top, panel.OPERATOR_SECTION_TOP)
            self.assertGreater(height, 80.0)
            self.assertLessEqual(top + height, panel.HEIGHT_MM)

    def test_state_switch_assets_are_generated_for_every_static_choice(self):
        panel = self.require_panel()
        expected = (
            ("FourV2FrequencyMode_Ratio.svg", "RATIO"),
            ("FourV2FrequencyMode_Fixed.svg", "FIXED"),
            ("FourV2FoldType_Symmetric.svg", "SYM"),
            ("FourV2FoldType_Asymmetric.svg", "ASYM"),
            ("FourV2FoldType_SoftClip.svg", "SOFT"),
        )
        self.assertEqual(expected, tuple(getattr(panel, "STATE_SWITCH_ASSETS", ())))
        for filename, label in expected:
            path = ROOT / "res" / filename
            with self.subTest(asset=filename):
                self.assertTrue(path.exists(), f"missing switch asset: {path}")
                self.assertEqual(
                    panel.generate_state_switch_svg(label),
                    path.read_text(encoding="utf-8"),
                )
                root = ET.parse(path).getroot()
                self.assertEqual("10mm", root.attrib.get("width"))
                self.assertEqual("5mm", root.attrib.get("height"))
                frame = next(
                    node for node in root.iter()
                    if node.tag.endswith("rect")
                )
                self.assertEqual(panel.CONTROL_STROKE, frame.attrib["stroke"])

    def test_attenuator_guides_use_the_neutral_control_stroke(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())
        circles = tuple(
            node for node in root.iter()
            if node.tag.endswith("circle")
        )
        for name, x, y in panel.COMPONENTS:
            if not name.endswith("_atten"):
                continue
            matches = [
                circle for circle in circles
                if abs(float(circle.attrib["cx"]) - x) < 0.01
                and abs(float(circle.attrib["cy"]) - y) < 0.01
            ]
            self.assertEqual(1, len(matches), name)
            with self.subTest(component=name):
                self.assertEqual(panel.CONTROL_FILL, matches[0].attrib["fill"])
                self.assertEqual(panel.CONTROL_STROKE, matches[0].attrib["stroke"])

    def test_mode_and_fold_switches_right_edges_align_with_frequency_displays(self):
        panel = self.require_panel()
        components = {
            name: (x, y)
            for name, x, y in panel.COMPONENTS
        }
        for operator, display in enumerate(panel.FREQUENCY_DISPLAY_RECTS, start=1):
            display_right = display[0] + display[2]
            for control in ("freq_mode", "fold_type"):
                x, _y = components[f"op{operator}_{control}"]
                with self.subTest(operator=operator, control=control):
                    self.assertAlmostEqual(
                        display_right,
                        x + panel.STATE_SWITCH_WIDTH / 2.0,
                        delta=0.001,
                    )

    def test_mode_and_fold_switch_borders_match_frequency_display_stroke(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())
        for operator in range(1, 5):
            for control in ("freq_mode", "fold_type"):
                x, y = next(
                    (x, y) for name, x, y in panel.COMPONENTS
                    if name == f"op{operator}_{control}"
                )
                matches = [
                    node for node in root.iter()
                    if node.tag.endswith("rect")
                    and abs(float(node.attrib["x"]) -
                            (x - panel.STATE_SWITCH_WIDTH / 2.0)) < 0.01
                    and abs(float(node.attrib["y"]) -
                            (y - panel.STATE_SWITCH_HEIGHT / 2.0)) < 0.01
                    and abs(float(node.attrib["width"]) -
                            panel.STATE_SWITCH_WIDTH) < 0.01
                    and abs(float(node.attrib["height"]) -
                            panel.STATE_SWITCH_HEIGHT) < 0.01
                ]
                self.assertEqual(1, len(matches), f"op{operator}_{control}")
                with self.subTest(operator=operator, control=control):
                    self.assertEqual(
                        panel.CONTROL_STROKE,
                        matches[0].attrib["stroke"],
                    )

    def test_live_displays_use_generated_display_rectangles(self):
        self.require_panel()
        for contract in (
            "box.size = mm2px(Vec(ROUTING_DISPLAY_WIDTH, ROUTING_DISPLAY_HEIGHT))",
            "mm2px(Vec(ROUTING_DISPLAY_X, ROUTING_DISPLAY_Y))",
            "OP1_FREQUENCY_DISPLAY_X",
            "OP1_FREQUENCY_DISPLAY_Y",
            "OP2_FREQUENCY_DISPLAY_X",
            "OP2_FREQUENCY_DISPLAY_Y",
            "OP3_FREQUENCY_DISPLAY_X",
            "OP3_FREQUENCY_DISPLAY_Y",
            "OP4_FREQUENCY_DISPLAY_X",
            "OP4_FREQUENCY_DISPLAY_Y",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, self.source)

    def test_frequency_readout_matches_switch_label_scale(self):
        panel = self.require_panel()
        self.assertAlmostEqual(3.2, panel.FREQUENCY_DISPLAY_FONT_SIZE)
        self.assertIn("FREQUENCY_DISPLAY_FONT_SIZE", self.source)

    def test_integrated_cv_rows_and_columns_align_to_operator_centres(self):
        panel = self.require_panel()
        self.assertEqual(tuple(panel.OPERATOR_CENTRES_X),
                         tuple(panel.PATCHBAY_COLUMN_XS))
        for row in panel.PATCHBAY_ROWS:
            self.assertEqual(4, len(panel.PATCHBAY_CELLS[row]))
            for cell, x in zip(panel.PATCHBAY_CELLS[row],
                               panel.PATCHBAY_COLUMN_XS):
                self.assertAlmostEqual(x, cell[0])

    def test_integrated_cv_rows_have_no_horizontal_guide_lines(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())
        parameter_rows = {round(y, 3) for y in panel.PATCHBAY_ROW_YS}
        guide_lines = [
            node for node in root.iter()
            if node.tag.endswith("line")
            and round(float(node.attrib["y1"]), 3) in parameter_rows
            and round(float(node.attrib["y2"]), 3) in parameter_rows
        ]
        self.assertEqual([], guide_lines)

    def test_operator_cv_and_atten_columns_have_no_headings(self):
        panel = self.require_panel()
        root = ET.fromstring(PANEL_SVG.read_text(encoding="utf-8"))
        labels = [
            node for node in root.iter()
            if node.tag.endswith("text")
            and node.text in {"CV", "ATTEN"}
            and abs(float(node.attrib["y"]) -
                    panel.OPERATOR_LABEL_YS["output"]) < 0.01
        ]
        self.assertEqual([], labels)

    def test_operator_labels_clear_their_real_control_envelopes(self):
        panel = self.require_panel()
        root = ET.fromstring(PANEL_SVG.read_text(encoding="utf-8"))
        labels = [
            node for node in root.iter()
            if node.tag.endswith("text") and node.text in {
                "COARSE", "MODE", "FINE", "LEVEL", "WARP", "FOLD",
                "FOLD TYPE",
                "FEEDBACK",
            }
        ]
        self.assertEqual(32, len(labels))
        component_by_name = {
            name: (x, y, panel.COMPONENT_RADII[name])
            for name, x, y in panel.OPERATOR_COMPONENTS
        }
        label_components = {
            "COARSE": "coarse",
            "MODE": "freq_mode",
            "FINE": "fine",
            "LEVEL": "output",
            "WARP": "warp",
            "FOLD": "fold",
            "FOLD TYPE": "fold_type",
            "FEEDBACK": "feedback",
        }
        for label in labels:
            x = float(label.attrib["x"])
            operator = min(
                range(1, 5),
                key=lambda index: abs(
                    x - panel.OPERATOR_CENTRES_X[index - 1]
                ),
            )
            component_name = f"op{operator}_{label_components[label.text]}"
            component_x, component_y, radius = component_by_name[component_name]
            if component_name.endswith(("_freq_mode", "_fold_type")):
                # The labeled switches are rectangular; this vertical
                # clearance uses their actual half-height rather than the
                # conservative circular envelope used for pairwise checks.
                radius = panel.STATE_SWITCH_HEIGHT / 2.0
            self.assertAlmostEqual(component_x, x, delta=0.01)
            baseline = float(label.attrib["y"])
            font_size = float(label.attrib["font-size"])
            with self.subTest(operator=operator, label=label.text,
                              contract="readable size"):
                self.assertGreaterEqual(font_size, 1.4)
                expected_size = 2.25 if label.text in {"MODE", "FOLD TYPE"} else 2.35
                self.assertAlmostEqual(expected_size, font_size)
            label_top = baseline - font_size
            label_bottom = baseline + font_size * 0.25
            above_clearance = component_y - radius - label_bottom
            below_clearance = label_top - (component_y + radius)
            with self.subTest(operator=operator, label=label.text):
                self.assertGreaterEqual(
                    above_clearance + 1e-9,
                    MINIMUM_LABEL_CLEARANCE_MM,
                )

    def test_selective_one_point_typography_leaves_titles_and_readouts_unchanged(self):
        panel = self.require_panel()
        self.assertAlmostEqual(2.25, panel.GLOBAL_LABEL_SIZE)
        self.assertAlmostEqual(2.35, panel.OPERATOR_LABEL_SIZE)
        self.assertAlmostEqual(2.25, panel.OPERATOR_MODE_LABEL_SIZE)
        self.assertAlmostEqual(12.6, panel.GLOBAL_LABEL_Y)
        self.assertAlmostEqual(24.1, panel.ALGORITHM_LABEL_Y)
        self.assertAlmostEqual(12.3, panel.MAIN_OUTPUT_LABEL_Y)
        self.assertAlmostEqual(1.9, panel.MAIN_OUTPUT_LABEL_SIZE)
        self.assertAlmostEqual(6.6, panel.TITLE_FONT_SIZE)
        self.assertAlmostEqual(5.0, panel.OPERATOR_HEADING_SIZE)
        self.assertAlmostEqual(3.2, panel.FREQUENCY_DISPLAY_FONT_SIZE)
        self.assertAlmostEqual(2.1, panel.ROUTING_NODE_LABEL_SIZE)

    def test_algorithm_label_clears_both_left_hand_controls(self):
        panel = self.require_panel()
        root = ET.fromstring(PANEL_SVG.read_text(encoding="utf-8"))
        label = next(
            node for node in root.iter()
            if node.tag.endswith("text") and node.text == "ALGO"
        )
        baseline = float(label.attrib["y"])
        font_size = float(label.attrib["font-size"])
        label_top = baseline - font_size
        label_bottom = baseline + font_size * 0.25
        for component_name in ("algorithm_knob", "voct_jack"):
            _x, component_y = panel.GLOBAL_CONTROLS[component_name]
            radius = panel.COMPONENT_RADII[component_name]
            above_clearance = component_y - radius - label_bottom
            below_clearance = label_top - (component_y + radius)
            with self.subTest(component=component_name):
                self.assertGreaterEqual(
                    max(above_clearance, below_clearance) + 1e-9,
                    MINIMUM_LABEL_CLEARANCE_MM,
                )

    def test_operator_sections_are_single_integrated_fields_in_the_svg(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())
        identifiers = {
            node.attrib.get("id") for node in root.iter()
            if node.tag.endswith("rect")
        }
        self.assertTrue({
            "operator-section-1",
            "operator-section-2",
            "operator-section-3",
            "operator-section-4",
        }.issubset(identifiers))
        self.assertEqual([], [identifier for identifier in identifiers
                              if identifier and
                              identifier.startswith("cv-patchbay-section")])

    def test_main_output_uses_established_inverted_backplate(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())
        circles = [node for node in root.iter()
                   if node.tag.endswith("circle")]
        output = next(circle for circle in circles
                      if abs(float(circle.attrib["cx"]) -
                             panel.OUTPUT_COMPONENTS[0][1]) < 0.01 and
                         abs(float(circle.attrib["cy"]) -
                             panel.OUTPUT_COMPONENTS[0][2]) < 0.01)
        self.assertEqual("#39445f", output.attrib["fill"])
        self.assertEqual("#dfe7f3", output.attrib["stroke"])
        visible = (float(output.attrib["r"]) +
                   float(output.attrib["stroke-width"]) / 2.0 -
                   RACK_PORT_RADIUS_MM)
        self.assertAlmostEqual(0.725, visible, delta=0.01)

    def test_static_labels_and_section_headings_are_emitted(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())
        labels = {node.text for node in root.iter()
                  if node.tag.endswith("text") and node.text}
        for expected in ("Four V2", "OP1", "OP2", "OP3", "OP4",
                         "LEVEL", "WARP", "FOLD", "FEEDBACK",
                         "FOLD TYPE",
                         "PM DEPTH", "MASTER", "EXT PM"):
            with self.subTest(label=expected):
                self.assertIn(expected, labels)
        for removed in ("PM CV", "ROUTING", "ALGORITHM", "CV PATCHBAY",
                        "OUTPUT", "ATTEN", "OVER"):
            with self.subTest(removed_label=removed):
                self.assertNotIn(removed, labels)

    def test_rack_runtime_gets_static_labels_from_a_nvg_overlay(self):
        source = self.source
        self.assertIn(
            "struct FourV2PanelLabels : Widget",
            source,
            "FourV2 must provide a Rack-rendered label overlay; NanoSVG "
            "does not render SVG text nodes",
        )
        body = _extract_struct_body(source, "struct FourV2PanelLabels")
        for contract in (
            'asset::system("res/fonts/DejaVuSans.ttf")',
            "nvgFontFaceId",
            "nvgText",
            '"Four V2"',
            '"COARSE"',
            '"MODE"',
            '"FOLD TYPE"',
            '"FEEDBACK"',
            '"LEVEL"',
            '"EXT PM"',
            '"MAIN OUT"',
            "GLOBAL_LABEL_SIZE",
            "MAIN_OUTPUT_LABEL_SIZE",
            "EXTERNAL_PM_LABEL_X",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, body)
        for removed in ('"PM CV"', '"ROUTING"', '"ALGORITHM"', '"CV PATCHBAY"',
                        '"ATTEN"', '"OVER"'):
            with self.subTest(removed_label=removed):
                self.assertNotIn(removed, body)
        self.assertNotIn("OVER_LABEL_Y", body)
        self.assertNotIn(
            "const float cvX[]",
            body,
        )
        self.assertNotIn(
            "const float attenX[]",
            body,
        )
        self.assertIn('"FOLD TYPE"', body)
        self.assertNotIn('"TYPE"', body)
        self.assertRegex(source, r"addChild\(\s*labels\s*\)")

    def test_title_and_operator_headings_are_bold_in_the_static_panel(self):
        panel = self.require_panel()
        root = ET.fromstring(panel.generate_svg())
        for text in ("Four V2", "OP1", "OP2", "OP3", "OP4"):
            label = next(
                node for node in root.iter()
                if node.tag.endswith("text") and node.text == text
            )
            with self.subTest(label=text):
                self.assertEqual("700", label.attrib.get("font-weight"))

        body = _extract_struct_body(self.source, "struct FourV2PanelLabels")
        self.assertIn("bool bold", body)
        self.assertIn("label.bold", body)
        self.assertIn('"Four V2", true', body)

    def test_generated_artifacts_match_generators(self):
        panel = self.require_panel()
        for path in (PANEL_SVG, LAYOUT_HEADER):
            self.assertTrue(path.exists(), f"missing generated file: {path}")
        self.assertEqual(panel.generate_svg(), PANEL_SVG.read_text())
        self.assertEqual(panel.generate_coords_header(), LAYOUT_HEADER.read_text())

    def test_panel_embeds_logo_groups_without_external_reference(self):
        panel = self.require_panel()
        svg = panel.generate_svg()
        self.assertIn('id="wint-glyphs"', svg)
        self.assertIn('id="oid-glyphs"', svg)
        self.assertNotIn("WintoidLogo.svg", svg)
        self.assertNotIn("<image", svg)

    def test_logo_underlines_match_measured_group_bounds(self):
        self.assertTrue(LOGO_SVG.exists(), "canonical logo SVG is missing")
        root = ET.parse(LOGO_SVG).getroot()
        for group_id, line_id in (("wint-glyphs", "wint-underline"),
                                  ("oid-glyphs", "oid-underline")):
            group = next(node for node in root.iter()
                         if node.attrib.get("id") == group_id)
            line = next(node for node in root.iter()
                        if node.attrib.get("id") == line_id)
            glyph_paths = [node for node in group.iter()
                           if node.tag.endswith("path") and
                           node.attrib.get("data-glyph")]
            self.assertTrue(glyph_paths)
            xs = []
            for path in glyph_paths:
                transform = path.attrib["transform"]
                match = re.fullmatch(
                    rf"translate\(({_FLOAT_TOKEN}) ({_FLOAT_TOKEN})\)",
                    transform,
                )
                self.assertIsNotNone(match)
                path_min_x, path_max_x = _path_x_bounds(path.attrib["d"])
                metadata_min_x, _metadata_min_y, metadata_max_x, _metadata_max_y = map(
                    float, path.attrib["data-bbox"].split(",")
                )
                tx = float(match.group(1))
                self.assertAlmostEqual(path_min_x, metadata_min_x, delta=0.01)
                self.assertAlmostEqual(path_max_x, metadata_max_x, delta=0.01)
                xs.extend((tx + path_min_x, tx + path_max_x))
            expected_min = min(xs)
            expected_max = max(xs)
            self.assertAlmostEqual(expected_min, float(line.attrib["x1"]),
                                   delta=0.01)
            self.assertAlmostEqual(expected_max, float(line.attrib["x2"]),
                                   delta=0.01)

    def test_logo_source_font_digest_matches_checked_in_glyph_data(self):
        self.assertTrue(GLYPH_DATA.exists(), "shaped glyph data is missing")
        data = json.loads(GLYPH_DATA.read_text(encoding="utf-8"))
        digest = data.get("source_font_sha256")
        self.assertIsInstance(digest, str)
        self.assertRegex(digest, r"[0-9a-f]{64}")
        logo_root = ET.parse(LOGO_SVG).getroot()
        self.assertEqual(digest, logo_root.attrib.get("data-source-font-sha256"))

    def test_panel_generator_rejects_logo_source_font_digest_mismatch(self):
        panel = self.require_panel()
        original_logo_path = panel.LOGO_PATH
        with tempfile.TemporaryDirectory() as directory:
            temporary_logo = pathlib.Path(directory) / "WintoidLogo.svg"
            source = LOGO_SVG.read_text(encoding="utf-8")
            digest = json.loads(GLYPH_DATA.read_text(encoding="utf-8"))["source_font_sha256"]
            temporary_logo.write_text(
                source.replace(
                    f'data-source-font-sha256="{digest}"',
                    f'data-source-font-sha256="{"0" * 64}"',
                    1,
                ),
                encoding="utf-8",
            )
            panel.LOGO_PATH = temporary_logo
            try:
                with self.assertRaisesRegex(RuntimeError, "source-font digest"):
                    panel._logo_elements()
            finally:
                panel.LOGO_PATH = original_logo_path


if __name__ == "__main__":
    unittest.main()
