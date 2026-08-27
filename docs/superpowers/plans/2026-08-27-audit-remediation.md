# GUI, Security, and Unpushed-Change Audit Remediation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task, and superpowers:verification-before-completion before reporting success.

**Goal:** Remove every confirmed defect from the 2026-08-27 Four/Vortex/Brink GUI and unpushed-change audits while preserving VCV Rack behavior and keeping the shared source safe for the external MetaModule wrapper.

**Architecture:** Keep panel geometry authoritative in the three Python generators, emit every runtime layout value consumed by C++, and test the generated SVG/header artifacts against those authorities. Put Rack-independent drawing and concurrency contracts behind small C++11 helpers that can be exercised without either SDK. Treat the sibling `wintoid-metamodule` repository as a read-only consumer: this repository may add compatibility checks and shared-source fixes, but it must not modify the wrapper, its assets, metadata, or build files.

**Tech Stack:** C++11, VCV Rack SDK, Python 3 standard library, SVG/XML, Make, AddressSanitizer/UndefinedBehaviorSanitizer, 4ms MetaModule Plugin SDK/ARM GCC 12.3 as a read-only compatibility target.

**Spec:** `docs/superpowers/specs/2026-08-26-brink-design.md` and `docs/superpowers/specs/2026-08-26-four-vortex-polyphony-design.md`; the audit deltas and acceptance criteria below are binding additions.

---

## Scope and fixed decisions

- The repository begins on `main`, one local commit ahead of `origin/main`, with user-owned tracked and untracked changes. Preserve that state and build on it; do not reset, stash, clean, or replace files from `HEAD`.
- `.superpowers/brainstorm/.last-token` is a local visual-companion bearer key, not a GitHub, OpenAI API, VCV, or MetaModule credential. The live server is stopped. The key was absent from every reachable and unreachable local Git object and from every currently published GitHub branch, tag, and pull-request ref checked on 2026-08-27. Rotation is still included as defense in depth.
- Do not print, hash into logs, copy into tests, or commit the credential. Tests may inspect only path tracking/ignore behavior.
- Do not modify `/Volumes/CODE/wintoid-metamodule`. It is known to be stale and therefore does not contain Brink. A temporary copy may be patched solely to prove that the current `wintoid-vcv` source compiles and links against its SDK.
- Preserve 16-channel VCV polyphony. The current MetaModule SDK exposes at most four lanes; that is a consumer constraint, not a reason to reduce shared VCV behavior.
- Preserve all module slugs, parameter/input/output/light IDs, patch compatibility, DSP behavior, port coordinates, and existing output colors. The GUI work changes visual affordances, label clearances, light coordinates, and clipping only.
- A real `PJ301MPort` has a 23.7 px diameter at Rack's `15 / 5.08` pixels/mm. Output backplates must extend at least 0.60 mm beyond that real widget radius. Structural input guides may retain their smaller 3.2 mm generator radius.
- Brink's 2 mm status lights have a 1 mm radius. Their centre offset from the associated jack must be 6.0 mm, leaving at least 0.25 mm from the enlarged output backplate and from adjacent status lights.
- Suggested commits below are checkpoints, not authorization to absorb unrelated user changes. Because several fixes overlap existing uncommitted Brink work, inspect `git diff --cached` before every commit and omit the commit if the owner has not authorized committing the current batch.
- `git add -p` ignores untracked paths entirely: it prints "No changes." and stages nothing. Checkpoints below therefore stage every not-yet-tracked path with plain `git add` and reserve `git add -p` for paths Git already tracks. Staging the pre-existing untracked `tests/test_panel_output_style.py` commits its owner-authored content together with this plan's edits, so apply the owner-authorization rule above to that file specifically.

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

## Dependency order

1. Protect the current worktree and contain the credential.
2. Fix Brink's portable snapshot protocol before relying on the ARM compatibility build.
3. Fix generator geometry and regenerate artifacts before changing runtime label/light placement.
4. Fix labels and MetaModule clipping/fonts.
5. Run host GUI review, native build, temporary MetaModule build, and a final `origin/main` review.

### Task 1: Record the dirty baseline and add repository credential hygiene

**Files:**

- Modify: `.gitignore`
- Create: `tests/test_repository_hygiene.py`
- Modify: `tests/Makefile`
- Remove after validation: `.superpowers/brainstorm/.last-token`
- Remove after validation: `.superpowers/brainstorm/.last-port`

- [ ] **Step 1: Capture the baseline without changing it**

Run:

```bash
git status --short --branch
git log --oneline --decorate origin/main..HEAD
git diff --check
git diff --stat origin/main
git ls-files --others --exclude-standard
```

Expected: `main` is ahead by one commit; the tracked/untracked files match the audit handoff; `git diff --check` is silent. If any new path appears, stop and classify it before continuing.

- [ ] **Step 2: Write the failing repository-hygiene regression test**

Create `tests/test_repository_hygiene.py` with standard-library-only tests:

```python
#!/usr/bin/env python3

import pathlib
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SESSION_PATHS = (
    ".superpowers/brainstorm/.last-token",
    ".superpowers/brainstorm/.last-port",
)


class RepositoryHygieneTest(unittest.TestCase):
    def git(self, *args):
        return subprocess.run(
            ("git",) + args,
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def test_superpowers_session_tree_is_ignored(self):
        for path in SESSION_PATHS:
            with self.subTest(path=path):
                self.assertEqual(
                    0,
                    self.git("check-ignore", "--quiet", "--no-index", path).returncode,
                )

    def test_no_superpowers_session_file_is_tracked(self):
        tracked = self.git("ls-files", "--", ".superpowers").stdout.strip()
        self.assertEqual("", tracked)


if __name__ == "__main__":
    unittest.main()
```

Run:

```bash
python3 tests/test_repository_hygiene.py
```

Expected: FAIL because `.superpowers/` is not ignored yet.

- [ ] **Step 3: Ignore the visual-companion session tree**

Add this exact repository-local rule to `.gitignore`:

```gitignore
.superpowers/
```

This is intentionally broader than only `.last-token`: the companion's server logs and `server-info` can also embed the key, and its own usage guide classifies the entire tree as persistent session state.

Run:

```bash
python3 tests/test_repository_hygiene.py
git status --short --ignored .superpowers
```

Expected: both tests pass and Git reports `.superpowers/` as ignored.

- [ ] **Step 4: Wire the hygiene test into the test runner**

Add `test_repository_hygiene` to `all`, `run`, and `.PHONY` in `tests/Makefile`, with:

```make
test_repository_hygiene:
	python3 test_repository_hygiene.py
```

Run:

```bash
make -C tests test_repository_hygiene
```

Expected: PASS.

- [ ] **Step 5: Rotate the stopped companion session key without touching mockups**

First confirm that no live `server.cjs` process has `/Volumes/CODE/wintoid-vcv` in its arguments and that no session `server-info` exists without a corresponding `server-stopped`. Do not kill an unrelated process.

Then remove only these two exact runtime files:

```bash
rm -- .superpowers/brainstorm/.last-token
rm -- .superpowers/brainstorm/.last-port
```

Expected: no content/session directories are removed. The next visual-companion start for this project will generate a new random key and choose/write a port. Do not restart it merely to test rotation; if later GUI work needs it, verify the old browser URL no longer authenticates and never print either key.

- [ ] **Step 6: Optional checkpoint commit**

```bash
git add .gitignore tests/test_repository_hygiene.py tests/Makefile
git diff --cached --check
git diff --cached
git commit -m "chore: ignore local visual companion credentials"
```

Expected: only the ignore rule, hygiene test, and Makefile wiring are staged. Skip the commit if it would capture unrelated existing work.

### Task 2: Make Brink's GUI snapshot protocol portable to ARM

**Files:**

- Modify: `src/Brink/dsp.h`
- Modify: `tests/test_brink_dsp.cpp`
- Modify: `tests/Makefile`

- [ ] **Step 1: Add a compile-time protocol contract that fails against the current code**

In `tests/test_brink_dsp.cpp`, after including `dsp.h`, add:

```cpp
static_assert(
    brink::AtomicDisplayFrame::WRITER_BEGIN_ORDER == std::memory_order_acq_rel,
    "the odd sequence marker must precede payload stores");
static_assert(
    brink::AtomicDisplayFrame::WRITER_END_ORDER == std::memory_order_release,
    "the even sequence marker must publish the payload");
static_assert(
    brink::AtomicDisplayFrame::READER_BEGIN_ORDER == std::memory_order_acquire,
    "payload reads must follow the first sequence read");
static_assert(
    brink::AtomicDisplayFrame::READER_VALIDATE_FENCE_ORDER ==
        std::memory_order_acquire,
    "payload reads must complete before validation");
static_assert(
    brink::AtomicDisplayFrame::READER_END_ORDER == std::memory_order_relaxed,
    "the acquire fence supplies validation ordering");
```

Run:

```bash
make -C tests clean test_brink_dsp
```

Expected: compilation FAILS because the five named protocol constants do not exist.

- [ ] **Step 2: Add a concurrent consistency regression**

Add `<thread>` and a test that initializes a valid frame, then runs one writer and one reader for at least 200,000 publications. Each publication must use exactly representable float values related by fixed offsets, for example:

```cpp
frame.signal = value;
frame.center = value + 1000000.f;
frame.lower = value + 2000000.f;
frame.upper = value + 3000000.f;
```

Every accepted read must satisfy all three offsets. Use an atomic done flag, join both threads, and register the test in `main()`. This stress test supplements the compile-time ordering contract; it does not replace it.

Add `-pthread` to `CFLAGS` in `tests/Makefile` so the C++11 test links on all supported hosts.

- [ ] **Step 3: Implement the portable single-writer seqlock ordering**

Expose the five `static constexpr std::memory_order` constants inside `AtomicDisplayFrame` and use them in the implementation. The store protocol must be:

```cpp
sequence.fetch_add(1, WRITER_BEGIN_ORDER);
signal.store(frame.signal, std::memory_order_relaxed);
center.store(frame.center, std::memory_order_relaxed);
lower.store(frame.lower, std::memory_order_relaxed);
upper.store(frame.upper, std::memory_order_relaxed);
sequence.fetch_add(1, WRITER_END_ORDER);
```

The load protocol must be:

```cpp
const std::uint32_t before = sequence.load(READER_BEGIN_ORDER);
if (before & 1u) continue;

// Four relaxed atomic payload loads.

std::atomic_thread_fence(READER_VALIDATE_FENCE_ORDER);
const std::uint32_t after = sequence.load(READER_END_ORDER);
if (before == after) return frame;
```

Why these exact orders: the begin `acq_rel` RMW prevents payload stores from moving ahead of the odd marker; the final release publishes them; the reader's initial acquire and validation acquire fence bracket the relaxed payload loads. Keep payload members atomic so the C++ model never contains a data race.

- [ ] **Step 4: Run focused and repeated tests**

```bash
make -C tests clean test_brink_dsp
tests/test_brink_dsp
tests/test_brink_dsp
tests/test_brink_dsp
```

Expected: all compile-time, round-trip, rate-limiter, DSP, and concurrent snapshot checks pass under ASan/UBSan three times.

- [ ] **Step 5: Optional checkpoint commit**

```bash
git add -p src/Brink/dsp.h tests/test_brink_dsp.cpp tests/Makefile
git diff --cached --check
git diff --cached
git commit -m "fix: make Brink display snapshots portable"
```

Expected: the staged diff includes the existing snapshot feature only if the owner intends it to be committed with this correction.

### Task 3: Make every output affordance visible and move Brink lights clear of jacks

**Files:**

- Modify: `scripts/generate_panel_four.py`
- Modify: `scripts/generate_panel_vortex.py`
- Modify: `scripts/generate_panel_brink.py`
- Modify: `tests/test_panel_output_style.py`
- Modify: `tests/test_brink_panel.py`
- Modify: `src/Brink/Brink.cpp`
- Regenerate: `res/Four.svg`
- Regenerate: `res/Vortex.svg`
- Regenerate: `res/Brink.svg`
- Regenerate: `src/Four/layout.h`
- Regenerate: `src/Vortex/layout.h`
- Regenerate: `src/Brink/layout.h`

- [ ] **Step 1: Tighten the output-style test around the real Rack widget**

In `tests/test_panel_output_style.py`, define:

```python
RACK_PIXELS_PER_MM = 15.0 / 5.08
RACK_PORT_RADIUS_MM = 23.7 / (2.0 * RACK_PIXELS_PER_MM)
MINIMUM_VISIBLE_OUTPUT_RING_MM = 0.60
```

Change `assert_output_style()` to require:

```python
self.assertGreaterEqual(
    float(circle.attrib["r"]) - RACK_PORT_RADIUS_MM,
    MINIMUM_VISIBLE_OUTPUT_RING_MM,
)
```

Keep the existing fill/stroke identity checks and the input non-recoloring checks.

Run:

```bash
python3 tests/test_panel_output_style.py
```

Expected: FAIL for Four, Vortex, and Brink because their 3.85 mm backplates are smaller than the real 4.01 mm port radius.

- [ ] **Step 2: Add failing Brink light-clearance tests**

In `tests/test_brink_panel.py`, require generator constants `OUTPUT_LIGHT_OFFSET = 6.0` and `STATUS_LIGHT_RADIUS = 1.0`, then check:

```python
light_to_backplate = (
    self.panel.OUTPUT_LIGHT_OFFSET
    - self.panel.OUTPUT_BACKPLATE_RADIUS
    - self.panel.STATUS_LIGHT_RADIUS
)
self.assertGreaterEqual(light_to_backplate, 0.25)

paired_light_gap = (
    2.0 * self.panel.PAIR_OFFSET
    - 2.0 * self.panel.OUTPUT_LIGHT_OFFSET
    - 2.0 * self.panel.STATUS_LIGHT_RADIUS
)
self.assertGreaterEqual(paired_light_gap, 0.25)
```

Also assert that each logic light's right edge remains within `WIDTH_MM`.

Run:

```bash
python3 tests/test_brink_panel.py
```

Expected: FAIL because the constants are absent and C++ still uses a 4 mm offset that places the lights under the jacks.

- [ ] **Step 3: Base output artwork on the real PJ301M radius**

In all three generators, retain the smaller structural `JACK_RADIUS`/`PORT_RADIUS` for inputs, but add or reuse:

```python
PIXELS_PER_MM = 15.0 / 5.08
RACK_PORT_RADIUS = 23.7 / (2.0 * PIXELS_PER_MM)
OUTPUT_RING_WIDTH = 0.65
OUTPUT_BACKPLATE_RADIUS = RACK_PORT_RADIUS + OUTPUT_RING_WIDTH
```

Do not change the established output fill `#39445f` or stroke `#c4cede`. Do not recolor inputs. This yields roughly 0.65 mm of visible fill outside the real jack, plus the existing 0.30 mm SVG stroke.

- [ ] **Step 4: Make Brink's light geometry generator-owned**

Add to `scripts/generate_panel_brink.py`:

```python
OUTPUT_LIGHT_OFFSET = 6.0
STATUS_LIGHT_RADIUS = 1.0
```

Emit `OUTPUT_LIGHT_OFFSET` in `src/Brink/layout.h`. Replace the hard-coded `+ 4.f`/`- 4.f` status-light offsets in `Brink.cpp` with `brink_layout::OUTPUT_LIGHT_OFFSET`: the gate-light expressions (`gatePoints[gate].x ± 4.f`), the event-light expressions (`eventPoints[event].x ± 4.f`), and the four logic lights (`brinkLogicLayout[logic].x + 4.f`). Two other `± 4.f` literals are not status lights and keep their 4 mm geometry: the direction arrow beside the event labels (`point.x + 4.f`) and the `WindowRail` widget offset (`positionRail.x - 4.f`). If the enlarged output backplates visually crowd either of those two, correct them through a new generator constant under Task 6 Step 4 rather than moving them here.

Do not change light IDs, colors, sizes, or activity logic. With a 6 mm centre offset, the 2 mm lights remain inside the 12 HP panel and clear both the real jack and the enlarged backplate.

- [ ] **Step 5: Regenerate all six artifacts**

```bash
python3 scripts/generate_panel_four.py
python3 scripts/generate_panel_vortex.py
python3 scripts/generate_panel_brink.py
```

Run:

```bash
python3 tests/test_panel_output_style.py
python3 tests/test_brink_panel.py
git diff --check -- res src/Four/layout.h src/Vortex/layout.h src/Brink/layout.h
```

Expected: all geometry/artifact tests pass; only output circles enlarge; inputs keep their prior styling; Brink light offsets are represented in the generated header and consumed by C++.

- [ ] **Step 6: Optional checkpoint commit**

```bash
git add tests/test_panel_output_style.py
git add -p scripts/generate_panel_four.py scripts/generate_panel_vortex.py scripts/generate_panel_brink.py tests/test_brink_panel.py src/Brink/Brink.cpp res/Four.svg res/Vortex.svg res/Brink.svg src/Four/layout.h src/Vortex/layout.h src/Brink/layout.h
git diff --cached --check
git diff --cached
git commit -m "fix: expose outputs and clear Brink status lights"
```

### Task 4: Correct Four/Vortex label clearances and Four generator provenance

**Files:**

- Create: `tests/test_panel_labels.py`
- Modify: `tests/Makefile`
- Modify: `scripts/generate_panel_four.py`
- Modify: `scripts/generate_panel_vortex.py`
- Modify: `src/Four/Four.cpp`
- Modify: `src/Vortex/Vortex.cpp`
- Regenerate: `src/Four/layout.h`
- Regenerate: `src/Vortex/layout.h`
- Regenerate: `res/Four.svg`
- Regenerate: `res/Vortex.svg`

- [ ] **Step 1: Add failing label/provenance contracts**

Create `tests/test_panel_labels.py` using the same `importlib` loader pattern as `test_panel_output_style.py`. Require:

- Four exposes and emits `GLOBAL_KNOB_LABEL_OFFSET = 5.0` and `GLOBAL_PORT_LABEL_OFFSET = 5.1`.
- The knob offset clears the actual 22.67581 px `RoundSmallBlackKnob` radius by at least 1.0 mm.
- The port offset clears the real PJ301M radius by at least 1.0 mm.
- `Four.cpp` consumes both generated constants and draws the literal `"Out"` to the right of `MAIN_OUTPUT_X` using left/middle alignment.
- The conservative right-side allowance `MAIN_OUTPUT_X + GLOBAL_PORT_LABEL_OFFSET + 7.0` remains inside `PANEL_WIDTH`.
- Vortex exposes/emits `LOGO_FONT_SIZE = 8.5`, and a conservative DejaVu width of `3.2 * LOGO_FONT_SIZE` leaves at least 0.4 mm between the centered logo and the inner edges of the 15 px lower screw boxes.
- `Vortex.cpp` uses `vortex_layout::LOGO_FONT_SIZE`, not a literal `10`.
- Four's module docstring names `scripts/generate_panel_four.py`, `res/Four.svg`, and `src/Four/layout.h`.
- Four's generated header comment names `scripts/generate_panel_four.py`; Vortex's names `scripts/generate_panel_vortex.py`.

Run:

```bash
python3 tests/test_panel_labels.py
```

Expected: FAIL for the absent constants, missing Four output label, Vortex's 10 px logo, and stale Four generator paths.

- [ ] **Step 2: Move Four's global labels away from real controls**

Define both label offsets in `generate_panel_four.py` and emit them in `layout.h`. In `PanelLabels::drawLayer()`:

```cpp
const float knobOff = mm2px(GLOBAL_KNOB_LABEL_OFFSET);
const float portOff = mm2px(GLOBAL_PORT_LABEL_OFFSET);
```

Use `knobOff` for Fine, VCA, and XMod. Use `portOff` for V/Oct and Ext PM. Remove the inaccurate comments that describe a 1.5 mm gap from the smaller SVG placeholder.

After the left-side labels, switch to `NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE` and draw `Out` at:

```cpp
mm2px(MAIN_OUTPUT_X) + portOff, mm2px(MAIN_OUTPUT_Y)
```

This adds the missing semantic affordance without moving the output or colliding with the VCA knob.

- [ ] **Step 3: Give the Vortex logo deterministic screw clearance**

Define `LOGO_FONT_SIZE = 8.5` in `generate_panel_vortex.py`, emit it to `layout.h`, and use it in the logo drawing code. Keep the baseline, two colors, centering, and underline unchanged.

- [ ] **Step 4: Fix generator documentation and header provenance**

Correct Four's run command/output paths and generated-header banner. Add an equivalent generated-by banner to Vortex's header output. Do not hand-edit either checked-in header.

- [ ] **Step 5: Regenerate and run the focused suite**

```bash
python3 scripts/generate_panel_four.py
python3 scripts/generate_panel_vortex.py
python3 tests/test_panel_labels.py
python3 tests/test_panel_output_style.py
```

Expected: all tests pass and checked-in SVG/header files exactly match generator output.

- [ ] **Step 6: Wire the label test into Make**

Add `test_panel_labels` to `all`, `run`, and `.PHONY` in `tests/Makefile`.

```make
test_panel_labels:
	python3 test_panel_labels.py
```

Run:

```bash
make -C tests test_panel_labels
```

Expected: PASS.

- [ ] **Step 7: Optional checkpoint commit**

```bash
git add tests/test_panel_labels.py
git add -p tests/Makefile scripts/generate_panel_four.py scripts/generate_panel_vortex.py src/Four/Four.cpp src/Vortex/Vortex.cpp src/Four/layout.h src/Vortex/layout.h res/Four.svg res/Vortex.svg
git diff --cached --check
git diff --cached
git commit -m "fix: clear panel labels and identify Four output"
```

### Task 5: Keep custom drawing inside MetaModule boxes and select supported fonts

**Files:**

- Create: `src/ui_geometry.h`
- Create: `tests/test_ui_geometry.cpp`
- Create: `tests/test_metamodule_graphics.py`
- Create: `docs/metamodule-compatibility.md`
- Modify: `tests/Makefile`
- Modify: `src/Four/Four.cpp`
- Modify: `src/Vortex/Vortex.cpp`
- Modify: `src/Brink/Brink.cpp`

- [ ] **Step 1: Write failing Rack-independent stroke geometry tests**

Create `tests/test_ui_geometry.cpp` for a new `wintoid::ui` helper API:

```cpp
#include "../src/ui_geometry.h"

// stroke_inset(0.5) == 0.25
// inset_extent(10, 0.5) == 9.5
// clamp_stroke_center(0, 10, 0.5) == 0.25
// clamp_stroke_center(10, 10, 0.5) == 9.75
// negative widths are treated as zero
// an over-wide stroke never produces a negative extent
```

Use the existing `TEST`/`ASSERT_NEAR` style. Add a `test_ui_geometry` compile/run target to `tests/Makefile`.

Run:

```bash
make -C tests test_ui_geometry
```

Expected: compilation FAILS because `src/ui_geometry.h` does not exist.

- [ ] **Step 2: Implement the C++11 geometry helper**

Create a Rack-independent header with inline functions:

```cpp
namespace wintoid {
namespace ui {

inline float nonnegative(float value);
inline float stroke_inset(float strokeWidth);
inline float inset_extent(float extent, float strokeWidth);
inline float clamp_stroke_center(float position, float extent, float strokeWidth);

} // namespace ui
} // namespace wintoid
```

`stroke_inset()` returns half a nonnegative stroke width. `inset_extent()` subtracts the full stroke width and clamps at zero. `clamp_stroke_center()` clamps a line centre between the half-stroke inset and `extent - inset`, handling degenerate boxes deterministically.

Run:

```bash
make -C tests test_ui_geometry
tests/test_ui_geometry
```

Expected: PASS under ASan/UBSan.

- [ ] **Step 3: Inset Four and Vortex display outlines**

Include `ui_geometry.h` in `Four.cpp` and `Vortex.cpp`. For `AlgoDisplay`, every `FoldTypeDisplay`, and `ModeDisplay`:

- Define the current 0.5 px stroke width once.
- Start the rounded rectangle at `stroke_inset(width)` on both axes.
- Use `inset_extent(box.size.x, width)` and `inset_extent(box.size.y, width)`.
- Keep fill, stroke color, corner radius, layer, and widget box unchanged.

The fill may occupy the inset rectangle; the visible result should differ only at the half-pixel outer edge that MetaModule previously clipped.

- [ ] **Step 4: Clamp every stroked Brink rail primitive**

Use the same helpers for:

- the 0.30 mm rounded track outline;
- the top/middle/bottom 0.25 mm tick centres;
- both 0.30 mm lower/upper boundary centres;
- the 0.35 mm center-line centre;
- the already-clamped signal marker, replacing its local min/max expression.

Filled band geometry may touch the box because it has no stroke. Preserve rail dimensions, normalization, colors, and z-order.

- [ ] **Step 5: Explicitly select MetaModule's supported DejaVu font**

`FoldTypeDisplay` and all three `PanelLabels` classes already select DejaVu Sans. Add the same guarded `loadFont(asset::system("res/fonts/DejaVuSans.ttf"))` and `nvgFontFaceId()` sequence to `AlgoDisplay` and `ModeDisplay` before text drawing. If loading fails, draw the background/border but skip text; do not silently inherit another widget's font.

- [ ] **Step 6: Add a source-level MetaModule regression test**

Create `tests/test_metamodule_graphics.py` that reads the three module sources and asserts:

- `Four.cpp`, `Vortex.cpp`, and `Brink.cpp` include and consume `ui_geometry.h` where they draw stroked box-edge primitives;
- Algo and Mode displays name `DejaVuSans.ttf` and call `nvgFontFaceId`;
- their custom displays continue drawing on layer 1;
- no display rectangle remains at the literal full-box form `nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, ...)`;
- Brink's track does not start a stroked rectangle at y `0.f` with full `box.size.y` height.

Run:

```bash
python3 tests/test_metamodule_graphics.py
```

Expected: FAIL before Steps 3-5, then PASS.

- [ ] **Step 7: Document the shared-source compatibility boundary**

Create `docs/metamodule-compatibility.md` documenting these repository rules:

- the sibling wrapper consumes these exact C++ files and generator artifacts;
- shared code stays C++11 and uses only fonts present in the SDK component library;
- custom graphics draw on layer 1 and within their `box` because MetaModule clips other layers/out-of-box pixels;
- GUI animation must not assume a stable/high frame rate;
- VCV remains 16-channel while the current MetaModule adapter caps exposed lanes at four;
- full-panel `PanelLabels` widgets allocate dynamic buffers sized to their boxes, so release validation must measure Four/Vortex/Brink on hardware before any attempted optimization;
- custom Algo/Mode/Fold click behavior in Rack does not substitute for verifying parameter mapping on MetaModule hardware;
- missing Brink in the stale wrapper is not a defect in this repository and must not be “fixed” here.

This turns the audit caveats into explicit acceptance gates without speculatively rewriting static labels or the external wrapper.

- [ ] **Step 8: Wire tests and run the focused suite**

Add `test_ui_geometry` and `test_metamodule_graphics` to `all`, `run`, `clean`, and `.PHONY` as appropriate.

Run:

```bash
make -C tests clean test_ui_geometry test_metamodule_graphics
tests/test_ui_geometry
python3 tests/test_metamodule_graphics.py
```

Expected: PASS.

- [ ] **Step 9: Optional checkpoint commit**

```bash
git add src/ui_geometry.h tests/test_ui_geometry.cpp tests/test_metamodule_graphics.py docs/metamodule-compatibility.md
git add -p tests/Makefile src/Four/Four.cpp src/Vortex/Vortex.cpp src/Brink/Brink.cpp
git diff --cached --check
git diff --cached
git commit -m "fix: keep custom graphics MetaModule-safe"
```

### Task 6: Prove generator/source/artifact consistency and inspect the live Rack GUI

**Files:**

- Verify only: `scripts/generate_panel_four.py`
- Verify only: `scripts/generate_panel_vortex.py`
- Verify only: `scripts/generate_panel_brink.py`
- Verify only: `res/Four.svg`
- Verify only: `res/Vortex.svg`
- Verify only: `res/Brink.svg`
- Verify only: `src/Four/layout.h`
- Verify only: `src/Vortex/layout.h`
- Verify only: `src/Brink/layout.h`
- Verify only: `src/Four/Four.cpp`
- Verify only: `src/Vortex/Vortex.cpp`
- Verify only: `src/Brink/Brink.cpp`

- [ ] **Step 1: Run generators twice and prove idempotence**

Run all three generators, record the six generated file checksums in a temporary file outside the repository, run all three generators again, and compare checksums. Do not print file contents.

```bash
python3 scripts/generate_panel_four.py
python3 scripts/generate_panel_vortex.py
python3 scripts/generate_panel_brink.py
```

Expected: the second generation changes no checksum. Then run:

```bash
python3 tests/test_panel_output_style.py
python3 tests/test_panel_labels.py
python3 tests/test_brink_panel.py
```

Expected: checked-in SVGs and headers exactly equal generated output.

- [ ] **Step 2: Build the native Rack plugin**

```bash
make -j4
```

Expected: native plugin build succeeds with no new warnings or errors.

- [ ] **Step 3: Inspect full composites in VCV Rack, not SVG backgrounds alone**

Load the development plugin in Rack and place Four, Vortex, and Brink next to each other at 100% zoom. Also inspect each at one lower and one higher zoom. Check the actual composite of panel SVG, screws, NanoVG text/displays/rails, real knobs, real `PJ301MPort`s, and lights.

Acceptance checklist:

- Every output shows a continuous navy/light output ring outside the real jack; no input has that ring.
- Output and light clearances use `circle radius + stroke width / 2`; no acceptance calculation counts the full centered stroke as outward material.
- Four's only output reads `Out` without colliding with VCA or the panel edge.
- Fine, VCA, XMod, V/Oct, and Ext PM labels have visibly stable gaps at all tested zooms.
- Vortex's 10 px lower logo is centered, fully visible, and keeps its two-colour underline on the screw-free panel.
- All 16 Brink status lights are completely visible and do not touch a jack, output ring, another light, or the panel edge.
- Brink's gate/event/logic labels remain associated with the correct ports after light movement.
- Algo, Mode, and Fold borders have four complete edges; Brink rail tracks/ticks/boundaries/center/signal marks do not lose half a stroke at the top or bottom.
- Algo, Mode, and Fold text uses DejaVu consistently and remains centered.
- Left click/right click still changes Four Algo/Fold and Vortex Mode exactly as before.
- No coordinate, ID, tooltip, menu, patch load, DSP, or polyphony behavior changes.

Capture screenshots outside the repository unless the owner explicitly asks to version them.

- [ ] **Step 4: If the live composite exposes a new collision, fix authority first**

Any geometry correction must be made in the corresponding generator constant, covered by a failing numeric test, regenerated, and then consumed from `layout.h`. Do not hand-edit an SVG/header or introduce a C++-only coordinate literal.

Re-run Steps 1-3 after any correction.

### Task 7: Run the complete suite and a read-only MetaModule compatibility build

**Files:**

- Verify: all changed and untracked files in `/Volumes/CODE/wintoid-vcv`
- Read-only source: `/Volumes/CODE/wintoid-metamodule/metamodule-plugin-sdk`
- Temporary only: a directory created with `mktemp -d` under `/tmp`

- [ ] **Step 1: Run every host test from a clean test-binary state**

```bash
make -C tests clean run
```

Expected: all Four, Vortex, Brink, polyphony, generator/artifact, label, repository-hygiene, UI-geometry, MetaModule-graphics, ASan, and UBSan checks pass. No sanitizer report is acceptable.

- [ ] **Step 2: Build current shared sources against MetaModule without touching its repository**

Create a temporary copy of the wrapper excluding `.git`, `build-mm`, installed plugins, and its stale `wintoid-vcv` directory. Copy the current working tree into the temporary wrapper as `wintoid-vcv`, excluding `.git`, `Rack-SDK`, build products, and `.superpowers`.

Patch only the temporary `CMakeLists.txt` so `target_sources(wintoid ...)` includes:

```cmake
wintoid-vcv/src/Brink/Brink.cpp
```

and `target_include_directories(wintoid PRIVATE ...)` includes:

```cmake
wintoid-vcv/src/Brink
```

The temporary copy will already contain current `plugin.cpp`/`plugin.json`, which register Brink. Use the wrapper's existing `plugin-mm.json` and assets only as build scaffolding; do not copy any result back.

Configure and build with the known compatible toolchain:

```bash
cmake --fresh -S /tmp/<probe>/wrapper -B /tmp/<probe>/wrapper/build-mm -G Ninja -DTOOLCHAIN_BASE_DIR=/Users/simon/arm-gnu-toolchain/arm-gnu-toolchain-12.3.rel1-darwin-arm64-arm-none-eabi/bin
cmake --build /tmp/<probe>/wrapper/build-mm
```

Expected: Four, Vortex, and Brink compile as ARM C++11 sources; the plugin links; the SDK checker reports `All symbols found!`. Failure caused by shared source must be fixed here with a regression test. Failure caused solely by stale wrapper assets/metadata is recorded as out of scope and is not fixed in `/Volumes/CODE/wintoid-metamodule`.

- [ ] **Step 3: Inspect the ARM implementation of the snapshot protocol**

Use the temporary build's `compile_commands.json` to compile/disassemble the `AtomicDisplayFrame` code at release optimization. Confirm the writer's odd marker cannot be reordered after payload stores and the reader emits the validation barrier before the final sequence read. This is an architecture check in addition to the language-level tests, not a substitute for them.

- [ ] **Step 4: Record hardware-only acceptance gates**

The following cannot be closed by the stale wrapper and must remain explicit release checks for whoever updates it later:

- Four/Vortex/Brink render with acceptable dynamic-buffer memory and refresh cost on actual MetaModule hardware, individually and in one patch.
- Algo/Mode/Fold parameters can be mapped and changed through MetaModule controls despite Rack's custom click widgets.
- The SDK's four-lane exposure behaves safely while shared VCV builds retain 16 lanes.

Do not mark these as current `wintoid-vcv` defects unless hardware evidence shows a failure. If a failure is in shared source, return to this repository; if it is wrapper registration/assets/metadata, it remains outside this plan.

### Task 8: Re-review the complete unpushed delta and prepare handoff

**Files:**

- Review: everything different from `origin/main`, including the ahead commit and all untracked non-ignored files

- [ ] **Step 1: Re-run mechanical integrity checks**

```bash
git diff --check
git status --short --branch
git log --oneline --decorate origin/main..HEAD
git diff --stat origin/main
git ls-files --others --exclude-standard
```

Expected: no whitespace errors; `.superpowers/` is absent from untracked output; every remaining untracked file is intentional.

- [ ] **Step 2: Review the entire delta against `origin/main`**

Inspect, in order:

```bash
git diff --find-renames origin/main -- plugin.json src scripts tests res docs .gitignore Makefile README.md
git diff --name-status origin/main
```

Then review the ahead commit with:

```bash
git show --stat --oneline HEAD
git show --check HEAD
```

Acceptance:

- no credential/session content is present;
- every C++ layout coordinate introduced by this work comes from a generated header;
- every checked-in SVG/header matches its generator;
- input/output counts and IDs match `plugin.json` and widget construction;
- every output and light is instantiated exactly once;
- the Brink snapshot has the portable ordering and remains rate-limited to 60 Hz;
- no source change assumes the external wrapper has already added Brink;
- no file under `/Volumes/CODE/wintoid-metamodule` changed.

- [ ] **Step 3: Run final verification immediately before any success claim**

```bash
make -C tests clean run
make -j4
git diff --check
```

Expected: all commands exit zero with fresh output. Quote the command results and test totals in the handoff; do not rely on an earlier run.

- [ ] **Step 4: Optional final commit/review checkpoint**

If the owner authorizes committing the full unpushed batch, stage intentionally, inspect the complete index, and commit with a message that reflects both the existing Brink work and the audit remediation. Otherwise leave all changes uncommitted and report that explicitly.

```bash
git add -p
git diff --cached --check
git diff --cached --stat
git diff --cached
```

Do not run `git add -A` while unrelated or owner-owned changes remain unclassified. Do not push; pushing requires a separate explicit request.

## Completion criteria

This plan is complete only when:

- the local companion credential is ignored and rotated without exposing its value;
- all confirmed P1/P2/P3 findings are covered by passing regressions and visible host-GUI acceptance;
- all generated artifacts are deterministic and current;
- native Rack and temporary MetaModule ARM builds pass;
- the final review covers the full `origin/main` delta, not just the latest edits;
- the sibling MetaModule repository remains byte-for-byte untouched by the implementation;
- hardware-only MetaModule caveats are handed off as explicit gates rather than misreported as fixed.
