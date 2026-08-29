# Post-Review Remediation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the remaining review findings without changing module behavior, current output-ring artwork, or the external MetaModule repository.

**Architecture:** Keep panel geometry authoritative in the three generators and put centered-SVG-stroke arithmetic in one small test helper consumed by every geometry test. Tighten label contracts against the complete output-ring envelope, document the real SVG-to-PNG MetaModule handoff, record the approved live-GUI decisions, and harden the two regression suites that can currently pass for the wrong reason.

**Tech Stack:** Python 3 `unittest`, generated SVG/C++ headers, C++11, VCV Rack 2 SDK, 4ms MetaModule SDK/Cortex-A7 cross-toolchain, GNU Make.

**Spec:** `docs/superpowers/plans/2026-08-27-audit-remediation.md` plus the 2026-08-27 post-implementation review findings.

## Global Constraints

- Modify only `/Volumes/CODE/wintoid-vcv`; `/Volumes/CODE/wintoid-metamodule` remains read-only.
- Preserve slugs, parameter/input/output/light IDs, patch compatibility, DSP, output colors, tooltips, menus, 16-channel VCV polyphony, and every current port/control coordinate.
- Retain the approved screw-free panels, the 10 px Vortex logo, and Brink's current one-millimetre downward field/control refinement.
- Retain the current output artwork in all generators: `OUTPUT_RING_WIDTH = 0.45`, `OUTPUT_STROKE_WIDTH = 0.55`, fill `#39445f`, and stroke `#dfe7f3`.
- The visible output envelope is the SVG circle radius plus half its centered stroke width. With the current constants it extends approximately `0.725 mm` beyond the real `PJ301MPort` radius and must remain at least `0.60 mm`.
- Brink status lights remain at a 6.0 mm centre offset and must retain at least 0.25 mm clearance from the complete output envelope, adjacent lights, and panel edges.
- Shared C++ remains C++11 and compatible with both Rack and the MetaModule SDK.
- Never print, recreate, stage, or inspect the contents of `.superpowers/`; test only ignore/tracking behavior.
- Use generators as the sole authority for SVGs, generated headers, and label geometry. Never hand-edit generated artifacts.
- Capture Rack screenshots and temporary MetaModule integration files outside the repository.

---

### Task 1: Make centered-stroke geometry authoritative in tests

**Files:**

- Create: `tests/panel_geometry.py`
- Create: `tests/test_panel_geometry.py`
- Modify: `tests/test_panel_output_style.py:8-55`
- Modify: `tests/test_brink_panel.py:173-185`
- Modify: `tests/Makefile`

**Interfaces:**

- Produces: `centered_stroke_outer_radius(radius_mm: float, stroke_width_mm: float) -> float`
- Produces: `visible_output_material(radius_mm: float, stroke_width_mm: float, obscuring_radius_mm: float) -> float`
- Consumed by: output-ring and Brink light-clearance tests in this and later tasks.

- [ ] **Step 1: Add a failing unit test for centered SVG strokes**

Create `tests/test_panel_geometry.py`:

```python
#!/usr/bin/env python3

import unittest

from panel_geometry import (
    centered_stroke_outer_radius,
    visible_output_material,
)


class PanelGeometryTest(unittest.TestCase):
    def test_only_half_of_a_centered_stroke_extends_outward(self):
        self.assertAlmostEqual(
            4.30,
            centered_stroke_outer_radius(4.0, 0.60),
        )

    def test_visible_material_excludes_the_obscuring_widget(self):
        self.assertAlmostEqual(
            0.725,
            visible_output_material(4.45, 0.55, 4.0),
        )


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and verify the missing helper is the failure**

Run:

```bash
cd /Volumes/CODE/wintoid-vcv/tests
python3 test_panel_geometry.py
```

Expected: FAIL with `ModuleNotFoundError: No module named 'panel_geometry'`.

- [ ] **Step 3: Implement the minimal geometry helper**

Create `tests/panel_geometry.py`:

```python
"""Shared physical geometry for generated Rack panel tests."""


def centered_stroke_outer_radius(radius_mm, stroke_width_mm):
    return float(radius_mm) + float(stroke_width_mm) / 2.0


def visible_output_material(radius_mm, stroke_width_mm,
                            obscuring_radius_mm):
    return (
        centered_stroke_outer_radius(radius_mm, stroke_width_mm)
        - float(obscuring_radius_mm)
    )
```

- [ ] **Step 4: Prove the helper passes**

Run: `python3 tests/test_panel_geometry.py`

Expected: 2 tests pass.

- [ ] **Step 5: Correct the output-ring regression contract**

In `tests/test_panel_output_style.py`, import `visible_output_material` and replace the calculation at lines 50-53 with:

```python
visible_material_mm = visible_output_material(
    float(circle.attrib["r"]),
    float(circle.attrib["stroke-width"]),
    RACK_PORT_RADIUS_MM,
)
```

Keep `MINIMUM_VISIBLE_OUTPUT_RING_MM = 0.60` and the existing fill, stroke, input-style, position, and artifact checks. Add this assertion so the test records the actual current envelope instead of the old commit-message claim:

```python
self.assertAlmostEqual(0.725, visible_material_mm, places=3)
```

- [ ] **Step 6: Reuse the same outer-radius rule for Brink lights**

In `tests/test_brink_panel.py`, import `centered_stroke_outer_radius` and calculate:

```python
output_outer_radius = centered_stroke_outer_radius(
    self.panel.OUTPUT_BACKPLATE_RADIUS,
    self.panel.OUTPUT_STROKE_WIDTH,
)
light_to_backplate = (
    self.panel.OUTPUT_LIGHT_OFFSET
    - output_outer_radius
    - self.panel.STATUS_LIGHT_RADIUS
)
```

Retain the existing minimum `0.25 mm`, paired-light, and panel-edge assertions.

- [ ] **Step 7: Wire the helper test into the runner**

Add `test_panel_geometry` to `all`, `run`, and `.PHONY` in `tests/Makefile`:

```make
test_panel_geometry:
	python3 test_panel_geometry.py
```

- [ ] **Step 8: Run the focused geometry suite**

Run:

```bash
make -C tests test_panel_geometry test_panel_output_style test_brink_panel
```

Expected: 2 panel-geometry, 5 output-style, and 15 Brink-panel tests pass. The generated artwork remains unchanged.

- [ ] **Step 9: Commit the geometry contract**

```bash
git add tests/panel_geometry.py tests/test_panel_geometry.py
git add tests/test_panel_output_style.py tests/test_brink_panel.py tests/Makefile
git diff --cached --check
git commit -m "test: correct centered output stroke geometry"
```

---

### Task 2: Validate labels against the complete output envelope

**Files:**

- Modify: `tests/panel_geometry.py`
- Modify: `tests/test_panel_geometry.py`
- Modify: `tests/test_panel_labels.py`
- Modify: `tests/test_brink_panel.py`
- Modify: `scripts/generate_panel_brink.py`
- Modify: `scripts/generate_panel_vortex.py`
- Modify: `src/Brink/Brink.cpp`
- Modify: `src/Vortex/Vortex.cpp`
- Regenerate: `res/Brink.svg`
- Regenerate: `res/Vortex.svg`
- Regenerate: `src/Brink/layout.h`
- Regenerate: `src/Vortex/layout.h`

**Interfaces:**

- Consumes: `centered_stroke_outer_radius()` from Task 1.
- Produces: `centered_text_clearance(offset_mm, component_outer_radius_mm, font_size_px, pixels_per_mm) -> float`
- Produces: Brink layout constants `PORT_LABEL_OFFSET = 6.35`, `EVENT_LABEL_OFFSET = 6.0`, and `EVENT_LABEL_FONT_SIZE = 5.9`.
- Produces: Vortex layout constant `AUDIO_LABEL_OFFSET = 5.0`.

- [ ] **Step 1: Add a failing conservative text-clearance helper test**

Add `centered_text_clearance` to the existing import tuple in `tests/test_panel_geometry.py`, then add this method inside the existing `PanelGeometryTest` class:

```python
def test_centered_text_clearance_reserves_half_the_font_height(self):
    self.assertAlmostEqual(
        0.50,
        centered_text_clearance(5.5, 4.5, 6.0, 6.0),
    )
```

Run: `python3 tests/test_panel_geometry.py`

Expected: FAIL because `centered_text_clearance` is not defined.

- [ ] **Step 2: Implement the conservative centered-text helper**

Append to `tests/panel_geometry.py`:

```python
def centered_text_clearance(offset_mm, component_outer_radius_mm,
                            font_size_px, pixels_per_mm):
    half_text_height_mm = float(font_size_px) / (2.0 * float(pixels_per_mm))
    return (
        float(offset_mm)
        - float(component_outer_radius_mm)
        - half_text_height_mm
    )
```

Run: `python3 tests/test_panel_geometry.py`

Expected: 3 tests pass.

- [ ] **Step 3: Write failing Brink output-label contracts**

In `tests/test_brink_panel.py`, import both helpers:

```python
from panel_geometry import (
    centered_stroke_outer_radius,
    centered_text_clearance,
)
```

Rename the existing test to `test_input_and_knob_labels_clear_actual_rack_widgets`, retain only its `small knob` and `port` cases, and remove the `event port` case. Then add:

```python
def test_output_labels_clear_the_complete_output_envelope(self):
    output_outer_radius = centered_stroke_outer_radius(
        self.panel.OUTPUT_BACKPLATE_RADIUS,
        self.panel.OUTPUT_STROKE_WIDTH,
    )
    normal_clearance = centered_text_clearance(
        self.panel.PORT_LABEL_OFFSET,
        output_outer_radius,
        6.5,
        self.panel.PIXELS_PER_MM,
    )
    event_clearance = centered_text_clearance(
        self.panel.EVENT_LABEL_OFFSET,
        output_outer_radius,
        self.panel.EVENT_LABEL_FONT_SIZE,
        self.panel.PIXELS_PER_MM,
    )
    self.assertGreaterEqual(normal_clearance, 0.25)
    self.assertGreaterEqual(event_clearance, 0.25)
```

Also verify the downward-event label clears the preceding event ring:

```python
event_half_height = (
    self.panel.EVENT_LABEL_FONT_SIZE
    / (2.0 * self.panel.PIXELS_PER_MM)
)
previous_ring_bottom = self.panel.Y_EVENTS_UP + output_outer_radius
down_label_top = (
    self.panel.Y_EVENTS_DOWN
    - self.panel.EVENT_LABEL_OFFSET
    - event_half_height
)
self.assertGreaterEqual(down_label_top - previous_ring_bottom, 0.25)
```

Run: `python3 tests/test_brink_panel.py`

Expected: FAIL because current 5.5 mm offsets do not clear the complete output envelope under the documented conservative model and `EVENT_LABEL_FONT_SIZE` does not exist.

- [ ] **Step 4: Make the safe Brink label geometry generator-owned**

In `scripts/generate_panel_brink.py`, set:

```python
PORT_LABEL_OFFSET = 6.35
EVENT_LABEL_OFFSET = 6.0
EVENT_LABEL_FONT_SIZE = 5.9
```

Emit all three values from `generate_header()`:

```python
f"constexpr float PORT_LABEL_OFFSET = {PORT_LABEL_OFFSET:.2f}f;",
f"constexpr float EVENT_LABEL_OFFSET = {EVENT_LABEL_OFFSET:.1f}f;",
f"constexpr float EVENT_LABEL_FONT_SIZE = {EVENT_LABEL_FONT_SIZE:.1f}f;",
```

In `src/Brink/Brink.cpp`, replace the event-label literal:

```cpp
nvgFontSize(args.vg, brink_layout::EVENT_LABEL_FONT_SIZE);
```

Do not move ports, controls, lights, fields, rails, or logic labels.

- [ ] **Step 5: Write failing Four/Vortex output-label contracts**

In `tests/test_panel_labels.py`, import `centered_stroke_outer_radius`. Add:

```python
def test_four_output_label_clears_the_complete_ring(self):
    outer_radius = centered_stroke_outer_radius(
        self.four.OUTPUT_BACKPLATE_RADIUS,
        self.four.OUTPUT_STROKE_WIDTH,
    )
    self.assertGreaterEqual(
        self.four.GLOBAL_PORT_LABEL_OFFSET - outer_radius,
        0.25,
    )

def test_vortex_audio_labels_clear_the_complete_ring(self):
    outer_radius = centered_stroke_outer_radius(
        self.vortex.OUTPUT_BACKPLATE_RADIUS,
        self.vortex.OUTPUT_STROKE_WIDTH,
    )
    self.assertGreaterEqual(
        self.vortex.AUDIO_LABEL_OFFSET - outer_radius,
        0.25,
    )
```

Add these exact source/header assertions:

```python
vortex_header = self.vortex.generate_coords_header()
self.assertIn(
    "constexpr float AUDIO_LABEL_OFFSET = 5.0f;",
    vortex_header,
)
self.assertIn("AUDIO_IN_Y - AUDIO_LABEL_OFFSET", self.vortex_source)
self.assertIn("AUDIO_OUT_Y - AUDIO_LABEL_OFFSET", self.vortex_source)
```

Run: `python3 tests/test_panel_labels.py`

Expected: FAIL because `AUDIO_LABEL_OFFSET` does not exist. Four's current 5.1 mm horizontal offset already passes.

- [ ] **Step 6: Make Vortex's audio-label offset generator-owned**

In `scripts/generate_panel_vortex.py`, add:

```python
AUDIO_LABEL_OFFSET = 5.0
```

Emit:

```python
lines.append(f'constexpr float AUDIO_LABEL_OFFSET = {AUDIO_LABEL_OFFSET:.1f}f;')
```

In `src/Vortex/Vortex.cpp`, replace both `AUDIO_IN_Y - 4.5f` and `AUDIO_OUT_Y - 4.5f` with:

```cpp
AUDIO_IN_Y - AUDIO_LABEL_OFFSET
AUDIO_OUT_Y - AUDIO_LABEL_OFFSET
```

- [ ] **Step 7: Regenerate the affected artifacts**

Run:

```bash
python3 scripts/generate_panel_brink.py
python3 scripts/generate_panel_vortex.py
```

Expected: the generated Brink/Vortex headers change, while both generated SVGs remain byte-for-byte identical because labels are drawn by C++. Port and control coordinates remain byte-for-byte unchanged from `6cd7972`.

- [ ] **Step 8: Run the focused label and artifact suite**

Run:

```bash
python3 tests/test_panel_geometry.py
python3 tests/test_brink_panel.py
python3 tests/test_panel_labels.py
python3 tests/test_panel_output_style.py
```

Expected: all tests pass; conservative label gaps are at least 0.25 mm; output artwork remains approximately 0.725 mm beyond the jack.

- [ ] **Step 9: Commit label-envelope fixes**

```bash
git add tests/panel_geometry.py tests/test_panel_geometry.py
git add tests/test_panel_labels.py tests/test_brink_panel.py
git add scripts/generate_panel_brink.py scripts/generate_panel_vortex.py
git add src/Brink/Brink.cpp src/Vortex/Vortex.cpp
git add res/Brink.svg res/Vortex.svg src/Brink/layout.h src/Vortex/layout.h
git diff --cached --check
git commit -m "fix: clear labels from complete output rings"
```

---

### Task 3: Document the real MetaModule source and asset boundary

**Files:**

- Modify: `docs/metamodule-compatibility.md:8-19`
- Modify: `tests/test_metamodule_graphics.py`

**Interfaces:**

- Produces: a repository-owned refresh contract describing shared C++/headers and external 240 px PNG conversion separately.
- Does not modify: `/Volumes/CODE/wintoid-metamodule` or any generated `.mmplugin` checked into either repository.

- [ ] **Step 1: Add a failing documentation contract**

In `tests/test_metamodule_graphics.py`, load the compatibility document in `setUpClass()`:

```python
cls.compatibility = (
    ROOT / "docs" / "metamodule-compatibility.md"
).read_text()
```

Add:

```python
def test_compatibility_doc_records_png_faceplate_handoff(self):
    self.assertIn("assets/*.png", self.compatibility)
    self.assertIn("240 px", self.compatibility)
    self.assertIn("does not render `res/*.svg`", self.compatibility)
    self.assertIn("SvgToPng.py", self.compatibility)
```

Run: `python3 tests/test_metamodule_graphics.py`

Expected: FAIL because the current document incorrectly implies direct SVG consumption.

- [ ] **Step 2: Correct the compatibility document without assigning external work here**

Replace the first bullet under `How the wrapper consumes this repository` with text carrying these exact facts:

```markdown
- The sibling wrapper compiles versioned checkouts of the shared C++ sources
  and their included `src/*/layout.h` headers. MetaModule does not render `res/*.svg`:
  its adapter rewrites those asset requests to PNG names and the
  wrapper packages separately converted `assets/*.png` faceplates. When the
  wrapper is intentionally refreshed, regenerate each faceplate at 240 px
  height with the SDK's `SvgToPng.py` (or an equivalent approved conversion)
  so panel artwork and shared coordinates remain from the same wintoid-vcv
  revision.
```

Add this item to the wrapper-refresh/hardware gate section:

```markdown
- Confirm the wrapper's Four, Vortex, and Brink PNG faceplates were regenerated
  from the same wintoid-vcv revision as the compiled C++ and generated headers.
```

Keep the statements that the wrapper is read-only here and that missing Brink in the stale wrapper is not a wintoid-vcv defect.

- [ ] **Step 3: Run the documentation and Meta graphics tests**

Run:

```bash
python3 tests/test_metamodule_graphics.py
git diff --check -- docs/metamodule-compatibility.md tests/test_metamodule_graphics.py
```

Expected: all tests pass; no external repository has changed.

- [ ] **Step 4: Commit the corrected boundary**

```bash
git add docs/metamodule-compatibility.md tests/test_metamodule_graphics.py
git diff --cached --check
git commit -m "docs: clarify MetaModule PNG asset handoff"
```

---

### Task 4: Record the approved live-GUI decisions without rewriting history

**Files:**

- Modify: `docs/superpowers/plans/2026-08-27-audit-remediation.md:15-26`

**Interfaces:**

- Produces: an explicit addendum that supersedes only the original visual constants and acceptance wording.
- Preserves: the original task history and the external MetaModule scope boundary.

- [ ] **Step 1: Add a dated outcome addendum after `Scope and fixed decisions`**

Insert:

```markdown
### 2026-08-27 live-GUI outcome addendum

The later Rack composite review superseded only the visual decisions listed
below. The original steps remain as implementation history and must not be
used to restore older constants:

- All three modules intentionally omit decorative screw widgets.
- Vortex keeps the shared 10 px portfolio logo because the screws are absent.
- Brink's channel fields and their contained controls/ports moved down 1 mm;
  IDs, DSP bindings, and patch compatibility did not change.
- Output circles retain `OUTPUT_RING_WIDTH = 0.45` and use a centered 0.55 mm
  `#dfe7f3` stroke. Their true outer margin is 0.725 mm because only half the
  stroke extends outward.
- The earlier 0.65 mm radius extension plus a centered 0.30 mm stroke would
  leave only about 0.187 mm between a Brink ring and its 6 mm-offset light,
  contradicting the 0.25 mm light-clearance requirement. The implemented
  geometry resolves that conflict and is authoritative.
```

- [ ] **Step 2: Update stale acceptance wording**

In Task 6's live-GUI checklist, replace the screw-specific Vortex item with:

```markdown
- Vortex's 10 px lower logo is centered, fully visible, and keeps its two-colour underline on the screw-free panel.
```

Add:

```markdown
- Output and light clearances use `circle radius + stroke width / 2`; no acceptance calculation counts the full centered stroke as outward material.
```

Do not mark historical checkboxes complete and do not rewrite the original TDD sequence.

- [ ] **Step 3: Check documentation integrity and commit**

Run:

```bash
git diff --check -- docs/superpowers/plans/2026-08-27-audit-remediation.md
rg -n "live-GUI outcome addendum|0\.725 mm|0\.187 mm|screw-free" docs/superpowers/plans/2026-08-27-audit-remediation.md
```

Expected: whitespace check passes and all four addendum concepts are present.

```bash
git add docs/superpowers/plans/2026-08-27-audit-remediation.md
git commit -m "docs: record final panel design decisions"
```

---

### Task 5: Remove the Brink stress-test scheduling race

**Files:**

- Modify: `tests/test_brink_dsp.cpp:132-178`

**Interfaces:**

- Preserves: `AtomicDisplayFrame` production implementation and its memory orders.
- Produces: a reader-ready handshake guaranteeing at least one read before the writer starts.

- [ ] **Step 1: Strengthen the test synchronization**

Add `std::atomic<bool> readerReady(false);`. Start the reader before the writer and make its first accepted snapshot precede the ready signal:

```cpp
std::thread reader([&snapshot, &done, &readerReady,
                    &violations, &accepted]() {
    const brink::WindowDisplayFrame initialFrame = snapshot.load();
    ++accepted;
    if (initialFrame.center - initialFrame.signal != 1000000.f
        || initialFrame.lower - initialFrame.signal != 2000000.f
        || initialFrame.upper - initialFrame.signal != 3000000.f) {
        ++violations;
    }
    readerReady.store(true, std::memory_order_release);

    while (!done.load(std::memory_order_acquire)) {
        const brink::WindowDisplayFrame frame = snapshot.load();
        ++accepted;
        if (frame.center - frame.signal != 1000000.f
            || frame.lower - frame.signal != 2000000.f
            || frame.upper - frame.signal != 3000000.f) {
            ++violations;
        }
    }
});

while (!readerReady.load(std::memory_order_acquire)) {}

std::thread writer([&snapshot]() {
    for (int i = 0; i < publications; ++i) {
        const float value = static_cast<float>(i % 1000000);
        brink::WindowFrame frame;
        frame.signal = value;
        frame.center = value + 1000000.f;
        frame.lower = value + 2000000.f;
        frame.upper = value + 3000000.f;
        snapshot.store(frame);
    }
});
```

Keep the writer join, release-store to `done`, reader join, and both final assertions. Do not change `src/Brink/dsp.h`.

- [ ] **Step 2: Run the focused test once and repeatedly**

Run:

```bash
make -C tests test_brink_dsp
./tests/test_brink_dsp
for review_run in 1 2 3 4 5 6 7 8 9 10; do ./tests/test_brink_dsp >/dev/null || exit 1; done
```

Expected: Brink reports 22/22 once and all ten silent repetitions exit zero.

- [ ] **Step 3: Commit the deterministic stress test**

```bash
git add tests/test_brink_dsp.cpp
git diff --cached --check
git commit -m "test: synchronize Brink snapshot stress reader"
```

---

### Task 6: Scope MetaModule source contracts to the widgets they protect

**Files:**

- Modify: `tests/test_metamodule_graphics.py`
- Modify: `src/Four/Four.cpp:574`
- Modify: `src/Vortex/Vortex.cpp:371`

**Interfaces:**

- Produces: `extract_struct(source: str, marker: str) -> str`, used only by source-contract tests.
- Protects: Four `AlgoDisplay`/`FoldTypeDisplay`, Vortex `ModeDisplay`, and Brink `WindowRail` independently.

- [ ] **Step 1: Add a brace-aware source extractor**

Add to `tests/test_metamodule_graphics.py`:

```python
def extract_struct(source, marker):
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated struct: {marker}")
```

Add this test proving extraction stops at the matching outer brace despite nested scopes:

```python
def test_extract_struct_handles_nested_braces(self):
    source = (
        "struct Demo { void run() { if (true) { int value = 1; } } }; "
        "struct Next { int value; };"
    )
    body = extract_struct(source, "struct Demo")
    self.assertIn("if (true) { int value = 1; }", body)
    self.assertNotIn("struct Next", body)
```

- [ ] **Step 2: Replace whole-file substring assertions with widget-scoped checks**

Build these exact cases:

```python
widgets = (
    ("Four", "struct AlgoDisplay"),
    ("Four", "struct FoldTypeDisplay"),
    ("Vortex", "struct ModeDisplay"),
    ("Brink", "struct WindowRail"),
)
```

Use these exact scoped assertions:

```python
for name, marker in widgets:
    with self.subTest(module=name, widget=marker):
        body = extract_struct(self.sources[name], marker)
        self.assertIn("drawLayer", body)
        self.assertIn("layer != 1", body)
        self.assertIn("wintoid::ui::", body)

for name, marker in widgets[:3]:
    with self.subTest(module=name, widget=marker):
        body = extract_struct(self.sources[name], marker)
        self.assertIn("res/fonts/DejaVuSans.ttf", body)
        self.assertIn("nvgFontFaceId", body)
        self.assertIn("stroke_inset", body)
        self.assertIn("inset_extent", body)

rail = extract_struct(self.sources["Brink"], "struct WindowRail")
for helper in ("clamp_stroke_center", "stroke_inset", "inset_extent"):
    with self.subTest(helper=helper):
        self.assertIn(helper, rail)
```

Delete the broad assertions that merely prove those strings occur somewhere in a module file.

- [ ] **Step 3: Remove stale screw comments**

Change the Four and Vortex logo comments from “bottom center, between screws” to:

```cpp
// wintoid logo (bottom center on the screw-free panel)
```

- [ ] **Step 4: Run and commit the scoped contracts**

Run:

```bash
python3 tests/test_metamodule_graphics.py
make -C tests test_metamodule_graphics
```

Expected: all MetaModule graphics tests pass.

```bash
git add tests/test_metamodule_graphics.py src/Four/Four.cpp src/Vortex/Vortex.cpp
git diff --cached --check
git commit -m "test: scope MetaModule graphics contracts"
```

---

### Task 7: Perform final host, GUI, history, and MetaModule verification

**Files:**

- Verify only: all files changed by Tasks 1-6.
- Temporary output only: `/tmp/wintoid-post-review-*`.
- Do not modify: `/Volumes/CODE/wintoid-metamodule`.

**Interfaces:**

- Consumes: every prior task.
- Produces: final evidence for the push decision; no repository artifact.

- [ ] **Step 1: Prove generator idempotence and artifact consistency**

Run each generator twice:

```bash
python3 scripts/generate_panel_four.py
python3 scripts/generate_panel_vortex.py
python3 scripts/generate_panel_brink.py
python3 scripts/generate_panel_four.py
python3 scripts/generate_panel_vortex.py
python3 scripts/generate_panel_brink.py
```

Then run:

```bash
python3 tests/test_panel_output_style.py
python3 tests/test_panel_labels.py
python3 tests/test_brink_panel.py
git diff --check
```

Expected: tests pass; a second generator run introduces no diff; no hand-edited artifact drift appears.

- [ ] **Step 2: Run the full host suite from clean test binaries**

```bash
make -C tests clean run
```

Expected minimum totals: Four DSP 54/54, Four engine 18/18, Vortex 33/33, Brink 22/22, UI geometry 6/6, panel geometry 3/3, plus all Python/helper tests with no ASan or UBSan report.

- [ ] **Step 3: Force a fresh native Rack build**

```bash
make -B -j4
```

Expected: `plugin.dylib` links successfully with no new warnings.

- [ ] **Step 4: Render current module composites outside the repository**

Prepare an isolated Rack user directory:

```bash
review_dir=$(mktemp -d /tmp/wintoid-post-review-rack.XXXXXX)
mkdir -p "$review_dir/plugins-mac-arm64/wintoid"
cp plugin.dylib plugin.json LICENSE "$review_dir/plugins-mac-arm64/wintoid/"
cp -R res "$review_dir/plugins-mac-arm64/wintoid/"
'/Applications/VCV Rack 2 Pro.app/Contents/MacOS/Rack' -u "$review_dir" -t 1
```

Inspect `screenshots/wintoid/FourMM.png`, `VortexMM.png`, and `Brink.png`. Confirm:

- every output ring is continuous and every input lacks it;
- Four's `Out` label and Vortex's In/Out labels have visible separation;
- Brink's normal/event labels, lights, rings, neighboring rows, field edges, and panel edges do not touch;
- Four/Vortex display borders and Brink rail strokes retain all edges;
- the screw-free 10 px logos are centered and unobstructed.

If a visual collision appears, stop and add a failing generator-owned geometry test before changing coordinates.

- [ ] **Step 5: Cross-build all shared modules in a throwaway MetaModule wrapper**

Create a temporary wrapper at a fixed, validated path. Stop rather than deleting anything if the path already exists:

```bash
meta_review_dir=/tmp/wintoid-post-review-meta-wrapper
test ! -e "$meta_review_dir"
mkdir "$meta_review_dir"
cp /Volumes/CODE/wintoid-metamodule/CMakeLists.txt "$meta_review_dir/CMakeLists.txt"
cp /Volumes/CODE/wintoid-metamodule/plugin-mm.json "$meta_review_dir/plugin-mm.json"
ln -s /Volumes/CODE/wintoid-metamodule/metamodule-plugin-sdk "$meta_review_dir/metamodule-plugin-sdk"
ln -s /Volumes/CODE/wintoid-vcv "$meta_review_dir/wintoid-vcv"
ln -s /Volumes/CODE/wintoid-metamodule/assets "$meta_review_dir/assets"
```

Use `apply_patch` only on `/tmp/wintoid-post-review-meta-wrapper/CMakeLists.txt`:

```diff
@@ target_sources(wintoid
 	wintoid-vcv/src/Vortex/Vortex.cpp
+	wintoid-vcv/src/Brink/Brink.cpp
@@ target_include_directories(wintoid PRIVATE
 	wintoid-vcv/src/Vortex
+	wintoid-vcv/src/Brink
```

Configure and build with exact paths:

```bash
cmake -S /tmp/wintoid-post-review-meta-wrapper \
  -B /tmp/wintoid-post-review-meta-wrapper/build \
  -DCMAKE_TOOLCHAIN_FILE=/tmp/wintoid-post-review-meta-wrapper/metamodule-plugin-sdk/cmake/arm-none-eabi-gcc.cmake \
  -DTOOLCHAIN_BASE_DIR=/Users/simon/arm-gnu-toolchain/arm-gnu-toolchain-12.3.rel1-darwin-arm64-arm-none-eabi/bin \
  -DINSTALL_DIR=/tmp/wintoid-post-review-meta-wrapper/out \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/wintoid-post-review-meta-wrapper/build --parallel 4
```

Expected: Four, Vortex, and Brink compile for Cortex-A7, the plugin links, and the SDK ends with `All symbols found!`. Existing implicit-`this` C++20 deprecation warnings may remain; no new warning is acceptable.

- [ ] **Step 6: Verify credential, history, external scope, and final delta**

Run:

```bash
git diff --check origin/main...HEAD
git status --short --branch
git ls-files -- .superpowers
git check-ignore --quiet --no-index .superpowers/brainstorm/.last-token
git -C /Volumes/CODE/wintoid-metamodule status --porcelain=v1 -b
git log --oneline origin/main..HEAD
```

Expected:

- the wintoid-vcv worktree contains only intentional plan implementation changes;
- `.superpowers` has no tracked paths and remains ignored;
- the MetaModule repository is clean and unchanged;
- the complete ahead range consists only of reviewed commits;
- `git diff --check` is silent.

- [ ] **Step 7: Request a final independent review before push**

Give the reviewer `origin/main` as the base and current `HEAD` as the target. Require findings-first review of:

- centered-stroke/output-label geometry;
- generator/source/artifact consistency;
- the corrected MetaModule PNG handoff documentation;
- the synchronized Brink stress test;
- complete unpushed history and credential hygiene.

Do not push until all Important findings are resolved or explicitly accepted by the owner.
