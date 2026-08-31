# Task 4 fix report — BrinkV2 review findings

## Status

The four Task 4 review findings are addressed. BrinkV2 now consumes a
generator-owned label/line schema for its static layer-1 overlay, uses the
shared `wintoid::ui` clipping helpers there, places status lights at dedicated
generated 6 mm-offset coordinates, and has deterministic structural and
widget-mapping regression coverage.

The Task 5 registration/manifest/README/handoff assertion remains intentionally
pending and was not weakened. VCV Rack was not relaunched because macOS
reported `User cancelled` when the quit request was issued; the rebuilt package
was installed successfully before that request.

## Files changed

- `scripts/generate_panel_brink_v2.py`
  - owns the exact SVG/layer-1 label, event-arrow, normalisation-line, alignment,
    weight, and palette schema;
  - emits the schema and dedicated status-light coordinates into `layout.h`;
  - keeps the generated `res/BrinkV2.svg` byte-for-byte unchanged.
- `src/BrinkV2/layout.h`
  - regenerated with label/line structs, schema arrays, and all 16 light
    coordinates.
- `src/BrinkV2/BrinkV2.cpp`
  - renders the generated schema on layer 1 through `stroke_inset`,
    `inset_extent`, and `clamp_stroke_center`;
  - consumes generated status-light coordinates instead of `RAIL_WIDTH`;
  - leaves the BrinkV2 module/DSP wrapper structurally equivalent to Brink V1.
- `tests/test_brink_v2_module.py`
  - compares normalized, comment-free C++ token projections of the complete V1
    and V2 module structs, allowing only the `BrinkV2`/`Brink` class-name
    substitution;
  - verifies exact control/input/output/light ID arrays, generated coordinate
    tables, point-array order, widget-call bindings, and every loop bound.
- `tests/test_brink_v2_panel.py`
  - verifies generated light coordinates do not overlap PJ301M socket faces;
  - verifies every SVG label, event arrow, normalisation segment, color,
    alignment, weight, and coordinate equals the shared generated schema.

No Brink V1 file, `res/BrinkV2.svg`, registration/manifest/documentation file,
plan, SDD ledger, or sibling MetaModule file was changed.

## TDD evidence

Before production edits:

```text
$ python3 tests/test_brink_v2_module.py
FAILED
```

The new assertions failed for the missing generated label/line schema, missing
`wintoid::ui` helper use in `BrinkV2PanelLabels`, and missing generated light
coordinate bindings. The normalized full-module V1/V2 comparison passed at the
baseline, confirming the current DSP wrapper was equivalent before adjustment.

```text
$ python3 tests/test_brink_v2_panel.py
Ran 12 tests
FAILED (errors=2)
```

The failures were the intended missing `PANEL_LABELS` and `LIGHT_COMPONENTS`
contracts.

## Focused verification

```text
$ python3 tests/test_brink_v2_panel.py
Ran 12 tests in 0.018s
OK

$ python3 -c 'import sys, unittest; sys.path.insert(0, "tests"); import test_brink_v2_module as m; suite = unittest.defaultTestLoader.loadTestsFromTestCase(m.BrinkV2ModuleContractTest); suite = unittest.TestSuite(test for test in suite if test._testMethodName != "test_registration_and_documentation_are_frozen"); result = unittest.TextTestRunner(verbosity=1).run(suite); raise SystemExit(not result.wasSuccessful())'
Ran 11 tests in 0.018s
OK

$ python3 tests/test_metamodule_graphics.py
Ran 8 tests in 0.005s
OK

$ python3 tests/test_panel_output_style.py
Ran 7 tests in 0.016s
OK

$ python3 tests/test_panel_labels.py
Ran 16 tests in 0.011s
OK

$ make -C tests test_brink_dsp
make: `test_brink_dsp' is up to date.

$ ./tests/test_brink_dsp
22/22 tests passed

$ make -j4
BrinkV2.cpp compiled and plugin.dylib linked successfully.

$ git diff --check
OK
```

The complete `python3 tests/test_brink_v2_module.py` still has exactly the one
expected Task 5 failure: `extern Model* modelBrinkV2;` has not yet been added to
`src/plugin.hpp`.

## Install and residual concerns

`make install` completed and copied
`wintoid-2.2.1-mac-arm64.vcvplugin` to the Rack2 plugin directory. The follow-up
quit/relaunch command failed with `User cancelled (-128)`, so Rack has not yet
been confirmed to load this package in the current session. Fully quit and
relaunch VCV Rack before visual/runtime inspection.

No other residual Task 4 concern is known.
