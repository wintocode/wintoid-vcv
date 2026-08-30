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
_HEADER_FLOAT_RE = re.compile(
    rf"constexpr float (?P<name>[A-Z0-9_]+) = (?P<value>{_FLOAT_TOKEN})f;"
)


def _emitted_header_floats(source):
    return {
        match.group("name"): float(match.group("value"))
        for match in _HEADER_FLOAT_RE.finditer(source)
    }


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
        self.assertEqual("#155f91", panel.LOGO_BLUE)
        self.assertEqual("#ed5b22", panel.LOGO_ORANGE)

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
        self.assertEqual({"#155f91", "#ed5b22"}, colours)

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
                self.assertGreaterEqual(float(heading.attrib["font-size"]), 3.4)
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
            ("PM CV", "pm_depth_cv_jack"),
            ("ATTEN", "pm_depth_cv_atten"),
            ("EXT PM", "external_pm_jack"),
            ("ATTEN", "external_pm_atten"),
            ("MAIN OUT", "main_output"),
            ("OVER", "over_light"),
        )
        for text, component_name in label_specs:
            component_x, component_y = panel.GLOBAL_CONTROLS[component_name]
            matches = [
                node for node in root.iter()
                if node.tag.endswith("text")
                and node.text == text
                and abs(float(node.attrib["x"]) - component_x) < 0.01
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
                self.assertEqual("middle", label.attrib["text-anchor"])
                self.assertGreaterEqual(
                    component_y - radius - label_bottom,
                    MINIMUM_LABEL_CLEARANCE_MM,
                )
                self.assertGreaterEqual(
                    baseline - font_size,
                    panel.SHARED_IO_SECTION[1],
                )

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

    def test_integrated_cv_headers_align_with_every_cv_control_pair(self):
        panel = self.require_panel()
        root = ET.fromstring(PANEL_SVG.read_text(encoding="utf-8"))
        header = _emitted_header_floats(
            LAYOUT_HEADER.read_text(encoding="utf-8")
        )
        labels = [
            node for node in root.iter()
            if node.tag.endswith("text")
            and node.text in {"CV", "ATTEN"}
            and abs(float(node.attrib["y"]) -
                    panel.OPERATOR_PARAMETER_HEADER_Y) < 0.01
        ]
        self.assertEqual(8, len(labels))

        circles = [
            node for node in root.iter()
            if node.tag.endswith("circle")
            and "cx" in node.attrib
            and "cy" in node.attrib
        ]
        for index in range(1, 5):
            input_prefix = f"OP{index}_OUTPUT_CV_INPUT"
            atten_prefix = f"OP{index}_OUTPUT_CV_ATTEN"
            input_x = header[f"{input_prefix}_X"]
            atten_x = header[f"{atten_prefix}_X"]
            for role, x in (("CV", input_x), ("ATTEN", atten_x)):
                matches = [
                    label for label in labels
                    if label.text == role
                    and abs(float(label.attrib["x"]) - x) < 0.01
                ]
                self.assertEqual(1, len(matches))
                self.assertAlmostEqual(
                    header["OPERATOR_PARAMETER_HEADER_Y"],
                    float(matches[0].attrib["y"]),
                )
            for row in panel.PATCHBAY_ROWS:
                slug = row.upper()
                for role in ("CV_INPUT", "CV_ATTEN"):
                    prefix = f"OP{index}_{slug}_{role}"
                    x = header[f"{prefix}_X"]
                    y = header[f"{prefix}_Y"]
                    matches = [
                        circle for circle in circles
                        if float(circle.attrib["cx"]) == x
                        and float(circle.attrib["cy"]) == y
                    ]
                    self.assertEqual(
                        1,
                        len(matches),
                        f"missing emitted SVG component for {prefix}",
                    )
                    self.assertAlmostEqual(
                        header[f"OP{index}_{slug}_CV_INPUT_Y"], y
                    )

    def test_operator_labels_clear_their_real_control_envelopes(self):
        panel = self.require_panel()
        root = ET.fromstring(PANEL_SVG.read_text(encoding="utf-8"))
        labels = [
            node for node in root.iter()
            if node.tag.endswith("text") and node.text in {
                "COARSE", "MODE", "FINE", "OUTPUT", "WARP", "FOLD", "TYPE",
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
            "OUTPUT": "output",
            "WARP": "warp",
            "FOLD": "fold",
            "TYPE": "fold_type",
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
            label_top = baseline - font_size
            label_bottom = baseline + font_size * 0.25
            above_clearance = component_y - radius - label_bottom
            below_clearance = label_top - (component_y + radius)
            with self.subTest(operator=operator, label=label.text):
                self.assertGreaterEqual(
                    above_clearance + 1e-9,
                    MINIMUM_LABEL_CLEARANCE_MM,
                )

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
        for expected in ("FourV2", "OP1", "OP2", "OP3", "OP4",
                         "OUTPUT", "WARP", "FOLD", "FEEDBACK", "CV",
                         "ATTEN", "PM DEPTH", "MASTER", "OVER"):
            with self.subTest(label=expected):
                self.assertIn(expected, labels)
        for removed in ("ROUTING", "ALGORITHM", "CV PATCHBAY"):
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
            '"FourV2"',
            '"COARSE"',
            '"MODE"',
            '"CV"',
            '"FEEDBACK"',
            '"MAIN OUT"',
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, body)
        for removed in ('"ROUTING"', '"ALGORITHM"', '"CV PATCHBAY"'):
            with self.subTest(removed_label=removed):
                self.assertNotIn(removed, body)
        self.assertRegex(source, r"addChild\(\s*labels\s*\)")

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
