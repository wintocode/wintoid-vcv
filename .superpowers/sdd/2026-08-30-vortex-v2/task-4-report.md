# Task 4 Report: Freeze the VortexV2 Rack-module contract

## Files changed

- `tests/test_vortex_v2_module.py` — added failing source-contract tests.
- `.superpowers/sdd/2026-08-30-vortex-v2/task-4-report.md` — this report.

No production source, panel, plugin, metadata, README, MetaModule, packaging,
SDK, firmware, hardware, install, or Rack lifecycle files were changed.

## RED evidence

The required test was run while `src/VortexV2/VortexV2.cpp` and the VortexV2
integration/metadata were intentionally absent. It failed cleanly with six
failures:

- Five source-contract tests reported:
  `VortexV2 module source is missing: src/VortexV2/VortexV2.cpp`.
- The metadata test reported:
  `VortexV2 metadata is missing from plugin.json`.

This is the expected RED state for Task 4.

## Test command and output

Command:

```sh
python3 tests/test_vortex_v2_module.py
```

Result:

```text
FFFFFF
Ran 6 tests in 0.000s

FAILED (failures=6)
```

The six failures are the expected missing-source and missing-metadata failures
described above. The test file also passed syntax compilation with:

```sh
PYTHONPYCACHEPREFIX=/tmp/wintoid-vortex-v2-pycache \
  python3 -m py_compile tests/test_vortex_v2_module.py
```

## Self-review

- Exact parameter, input, and output IDs are frozen in the required order with
  `PARAMS_LEN`, `INPUTS_LEN`, and `OUTPUTS_LEN` sentinels.
- Full output labels, model declarations, connection-gating markers, and
  forbidden legacy mode UI markers are asserted.
- Generated layout inclusion/constants and standard Rack control types are
  asserted; output widget placement is required to use the generated output
  grid rather than literal coordinates.
- Future model registration, manifest identity, tags, manual URL, and README
  documentation are asserted.
- `git diff --check` passed for the added test content.
- No unrelated worktree changes were present.

## Concerns

The test intentionally remains RED until Task 5 implements
`src/VortexV2/VortexV2.cpp` and Task 6 adds registration and documentation.
No build, install, or Rack lifecycle action was appropriate for this test-only
task.
