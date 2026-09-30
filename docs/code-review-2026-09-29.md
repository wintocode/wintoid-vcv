# wintoid code review

Date: 2026-09-29  
Reviewed revision: `f3a5c3f547b70052138102c3b6192ead9445ca8b`  
Plugin version: `2.3.2`

## Assessment

The plugin builds successfully and the complete existing test suite passes. The review nevertheless identified seven actionable findings and two inconsistencies between documented and implemented behavior. The most useful next work is to correct frequency text entry, Fold's discontinuity, sample-rate-dependent bass response, combined cutoff modulation, and the numerical test assertions.

No P0 or P1 issue was established. This is a review of the current codebase, not just the latest commit. No implementation, panel, asset, or settings changes were made.

Priorities: **P2** means a correctness or validation issue worth addressing in normal development; **P3** means a lower-impact portability, performance, or documentation issue. The two contract discrepancies need a behavior decision before a sound-changing fix.

| ID | Priority | Area | Finding | Evidence |
| --- | --- | --- | --- | --- |
| F1 | P2 | Four/FourV2, Vortex/VortexV2 | Frequency text entry does not invert its display | Reproduced through the rebuilt plugin and Rack APIs |
| F2 | P2 | FourV2 DSP | Fold jumps when its amount leaves zero | Reproduced with production-style compiler flags |
| F3 | P2 | FourV2 DSP | DC blocking changes bass response with sample rate | Measured at four sample rates |
| F4 | P2 | VortexV2 controls | Intermediate clamping prevents opposing octave CVs from cancelling | Reproduced at both frequency limits; intended composition inferred |
| F5 | P2 | C++ tests | Approximate-equality assertions accept NaN | Reproduced using an existing test file's actual macro |
| F6 | P3 | Brink/BrinkV2 display | Snapshot publication lacks a complete memory-ordering guarantee | C++ memory-model analysis; visual failure not reproduced |
| F7 | P3 | VortexV2 performance | Idle CV connections disable coefficient reuse | Measured filter-core overhead |
| D1 | P2 contract discrepancy | FourV2 feedback | Operator Output does not scale self-feedback as documented | Reproduced; historical plan conflicts with current documentation |
| D2 | P3 contract discrepancy | FourV2 ratios | Endpoint selection zones are half the documented width | Reproduced; tests deliberately preserve current rounding |

## Findings

### F1 — Frequency text entry does not invert the displayed value

Locations: [FourV2.cpp:36](/Users/simon/Code/wintoid-vcv/src/FourV2/FourV2.cpp:36), [FourV2.cpp:235](/Users/simon/Code/wintoid-vcv/src/FourV2/FourV2.cpp:235), [VortexV2.cpp:18](/Users/simon/Code/wintoid-vcv/src/VortexV2/VortexV2.cpp:18). Legacy equivalents: [Four.cpp:6](/Users/simon/Code/wintoid-vcv/src/Four/Four.cpp:6), [Vortex.cpp:6](/Users/simon/Code/wintoid-vcv/src/Vortex/Vortex.cpp:6).

The oscillator Coarse quantities override display-string formatting but inherit Rack's raw numeric interpretation. The filter quantities format strings containing `Hz` or `kHz`, which their inherited text parser does not accept. These quantities therefore present values that cannot be edited naturally through Rack's parameter text field.

A headless probe loaded the freshly rebuilt `plugin.dylib`, created its actual modules, and called their real quantities:

| Operation | Observed result |
| --- | --- |
| FourV2, Fixed mode, enter `440` | Raw Coarse becomes `14`; display becomes `10.0 kHz` |
| Four, Fixed mode, enter `440` | Raw Coarse becomes `64`; display becomes `10.0 kHz` |
| Either oscillator, default Ratio mode, enter `2:1` | Value remains `1:1` |
| Either filter, start at 1234 Hz, enter `2.00 kHz` or `500.0 Hz` | Value remains approximately 1234 Hz |
| Either filter, enter `2000` | Correctly becomes 2 kHz |
| Either filter, enter `2` | Becomes the 20 Hz minimum |

Impact: ordinary numerical editing either silently fails or selects a very different frequency. This is independent of patch serialization, whose sampled parameter round trips passed.

Recommendation: implement matching display and parsing conversions. Coarse needs mode-aware ratio/frequency parsing and an inverse exponential mapping; FourV2 must account for Fine because its frequency display includes Fine. Filter text should accept its displayed units, or use consistent numeric display values with Rack's separate unit mechanism. Add executing quantity tests for formatted round trips and representative user input.

### F2 — Fold has a discontinuity at zero amount

Location: [FourV2/dsp.h:190](/Users/simon/Code/wintoid-vcv/src/FourV2/dsp.h:190). The inherited behavior also exists at [Four/dsp.h:175](/Users/simon/Code/wintoid-vcv/src/Four/dsp.h:175).

`wave_fold()` returns the input unchanged at `amount <= 0`. At any positive amount, Soft Clip applies `soft_clip(signal * (1 + 4 * amount))`; Asymmetric does the same on the negative half-cycle. That transfer is already nonlinear at unity gain.

Reproduction with input `-1`:

```text
Fold type       amount = 0     amount = 1e-8
Symmetric       -1.000000      -1.000000
Asymmetric      -1.000000      -0.777778
Soft Clip       -1.000000      -0.777778
```

Impact: moving Fold off zero, or modulating it to and from zero, can cause a finite signal jump even for an arbitrarily small amount change. At full operator Output and Master, the demonstrated difference is approximately 1.11 V for one carrier before subsequent filter history is considered. This can introduce clicks and unintended modulation artifacts.

Recommendation: make the transfer approach the identity continuously as Fold approaches zero, for example with an appropriate blend or normalized transfer. Test the one-sided limit for positive and negative signals in all three modes. Treat any V1 change separately because its sound is explicitly preserved for patch compatibility.

### F3 — The DC blocker changes bass response with the host sample rate

Locations: [FourV2/dsp.h:49](/Users/simon/Code/wintoid-vcv/src/FourV2/dsp.h:49), [FourV2/engine.h:249](/Users/simon/Code/wintoid-vcv/src/FourV2/engine.h:249), [FourV2/engine.h:285](/Users/simon/Code/wintoid-vcv/src/FourV2/engine.h:285).

The high-pass pole `R` is fixed at `0.999`. Processing frequency changes with Rack's sample rate, but the pole does not, so the filter's cutoff in hertz changes with it. The comment describing a roughly 20 Hz filter is not a sample-rate-independent contract implemented by this coefficient.

A single 20 Hz sine carrier, with identical parameters and full Master, produced these normalized amplitudes. The probe processed three seconds and measured the final second:

| Sample rate | Measured amplitude |
| ---: | ---: |
| 44.1 kHz | 0.944016 |
| 48 kHz | 0.934584 |
| 96 kHz | 0.794923 |
| 192 kHz | 0.547801 |

Impact: switching the same patch from 48 kHz to 192 kHz reduces this bass component by approximately 4.6 dB. The existing AC-passing test checks only 440 Hz at 48 kHz, so it misses the effect.

Recommendation: choose an explicit DC-blocking cutoff and derive the pole from the host sample period, preserving an established reference response where practical. Add low-frequency amplitude tests across supported sample rates. Four V1 has the same issue, but changing it requires a separate compatibility decision.

### F4 — Cutoff is clamped before all octave modulation is combined

Locations: [VortexV2.cpp:224](/Users/simon/Code/wintoid-vcv/src/VortexV2/VortexV2.cpp:224), [VortexV2/dsp.h:30](/Users/simon/Code/wintoid-vcv/src/VortexV2/dsp.h:30).

The module calls `cutoff_with_voct()`, which clamps to 20–20,000 Hz, then applies the independent Cutoff CV multiplier and clamps again. Clamping the intermediate value discards part of the V/Oct contribution even when the final combined frequency would be within range.

With both attenuverters at +1:

| Knob frequency | V/Oct | Cutoff CV | Current result | Combining octaves before clamping |
| ---: | ---: | ---: | ---: | ---: |
| 1000 Hz | +5 V | −5 V | 625 Hz | 1000 Hz |
| 40 Hz | −2 V | +2 V | 80 Hz | 40 Hz |

Impact: opposing pitch offsets fail to cancel, and tracking can plateau prematurely when the other CV should bring the result back into range.

The numerical behavior is reproduced. The expected result is an inference from both inputs being exponential octave modulation; the repository does not explicitly specify their composition at the limits.

Recommendation: combine the attenuated octave contributions before applying one final frequency limit, with overflow protection. Add tests for both opposing-input cases and ordinary single-input tracking. If the staged saturation is deliberate, document it as an explicit exception.

### F5 — Numerical test assertions accept NaN as equal

Representative locations: [test_vortex_v2_dsp.cpp:25](/Users/simon/Code/wintoid-vcv/tests/test_vortex_v2_dsp.cpp:25), [test_four_v2_engine.cpp:25](/Users/simon/Code/wintoid-vcv/tests/test_four_v2_engine.cpp:25), [test_brink_dsp.cpp:26](/Users/simon/Code/wintoid-vcv/tests/test_brink_dsp.cpp:26).

Ten C++ test files define the same approximate-equality pattern:

```cpp
if (fabsf(actual - expected) > epsilon) {
    // Fail.
}
```

If either operand is NaN, the comparison is false and the assertion passes. Subtracting two infinities has the same problem. A temporary executable included `test_vortex_v2_dsp.cpp` and invoked its actual macro; both `ASSERT_NEAR(NAN, 0.f, 1e-6f)` and `ASSERT_NEAR(INFINITY, INFINITY, 1e-6f)` passed under AddressSanitizer and UndefinedBehaviorSanitizer.

Affected suites: Four DSP/engine; FourV2 model/routing/DSP/engine; Vortex DSP; VortexV2 DSP; Brink DSP; UI geometry.

Impact: frequency mapping, numerical equivalence, state reset, and some non-finite recovery regressions can pass despite invalid results. The explicit finite-output checks elsewhere do not cover every approximate comparison.

Recommendation: positively require `fabsf(actual - expected) <= epsilon`, or explicitly require finite operands before comparing. Consolidate the assertion in one shared helper and verify that deliberately invalid comparisons fail.

### F6 — Brink's display snapshot lacks a complete publication guarantee

Locations: [Brink/dsp.h:57](/Users/simon/Code/wintoid-vcv/src/Brink/dsp.h:57), [Brink/dsp.h:79](/Users/simon/Code/wintoid-vcv/src/Brink/dsp.h:79), [test_brink_dsp.cpp:35](/Users/simon/Code/wintoid-vcv/tests/test_brink_dsp.cpp:35). Shared by Brink and BrinkV2.

The writer marks the sequence odd with an `acq_rel` operation, then stores four payload atoms with relaxed ordering. The reader loads those fields relaxed and uses an acquire fence before checking the sequence again. There is no release fence after the odd marker and before the payload stores.

Consequently, reading a newly written payload field does not establish the synchronization needed to ensure validation observes the corresponding sequence change. The C++ model permits accepting mixed fields while observing an older even sequence twice. The reference seqlock pattern in [WG21 P0603R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0603r0.htm) explains the required synchronization through atomic payload accesses and includes the missing writer-side fence.

Impact is limited to potentially inconsistent window-rail geometry; audio calculations do not consume this snapshot. This is a portability finding from memory-model analysis, not a reproduced flicker or data race. The existing concurrent stress test passed.

Recommendation: add a release fence between the odd marker and relaxed payload stores, retaining the reader's acquire fence and final release publication, or adopt another proven protocol. Update the ordering contract tests; their current `acq_rel` assertion alone does not prove snapshot consistency.

### F7 — Constant or zero-depth CV unnecessarily disables filter caching

Locations: [VortexV2.cpp:218](/Users/simon/Code/wintoid-vcv/src/VortexV2/VortexV2.cpp:218), [VortexV2/dsp.h:161](/Users/simon/Code/wintoid-vcv/src/VortexV2/dsp.h:161).

Connecting any Cutoff CV, Resonance CV, or V/Oct cable forces coefficient reuse off for every active filter branch, even if the voltage is constant or its attenuverter is centered. The existing cache already compares effective cutoff, damping, sample rate, and mode on each call, so changing CV can invalidate it without this unconditional bypass.

A local `-O3` filter-core benchmark processed 48,000 samples across 16 lanes and all 12 outputs with identical effective controls. Warm runs took approximately 18 ms with reuse enabled and 46 ms with reuse disabled, with matching output checksums: about 2.5 times the work in this benchmark.

Impact: an idle modulation cable can consume avoidable processing time. This measurement does not establish a Rack audio deadline miss or a whole-module CPU percentage.

Recommendation: retain equality-based coefficient reuse with connected CV, and verify that audio-rate changes still produce identical results. Consider any further sharing between branches only after profiling and preserving independent reset behavior.

## Contract discrepancies requiring a decision

### D1 — Self-feedback bypasses the operator Output level

Locations: [FourV2/engine.h:150](/Users/simon/Code/wintoid-vcv/src/FourV2/engine.h:150), [FourV2/engine.h:171](/Users/simon/Code/wintoid-vcv/src/FourV2/engine.h:171), [README.md:68](/Users/simon/Code/wintoid-vcv/README.md:68), [README.md:86](/Users/simon/Code/wintoid-vcv/README.md:86).

Routed modulation uses source Output, but self-feedback uses the previous unscaled waveform. With previous waveform `0.8`, Feedback `0.5`, and PM Depth `1`, feedback remains `0.381997` cycles for Output values `0`, `0.25`, and `1`.

The current README says Output controls every destination and explicitly includes self-feedback among internal PM following source Output. However, the [historical implementation plan:440](/Users/simon/Code/wintoid-vcv/docs/superpowers/plans/2026-08-29-four-v2.md:440) prescribed an unscaled feedback term. The current self-feedback test uses Output `1` and cannot distinguish the contracts.

Impact: reducing Output changes outgoing audio/modulation without reducing the feedback shaping of that operator. Output zero still mutes its outgoing contribution; this finding does not mean muted carriers leak audio.

Resolution: decide whether self-feedback is an exception. Either document the existing behavior explicitly, or apply source Output in the feedback path and add zero/reduced/full-level tests. A DSP change would alter existing FourV2 patches and should be deliberate.

### D2 — Ratio endpoint zones are half the documented width

Locations: [FourV2/model.h:74](/Users/simon/Code/wintoid-vcv/src/FourV2/model.h:74), [README.md:55](/Users/simon/Code/wintoid-vcv/README.md:55).

Coarse ranges from `0` to `14` and is rounded with `floor(coarse + 0.5)`. Thus the first and last ratio zones each occupy 0.5 raw units, while each interior zone occupies 1 unit. A uniform sweep of 140,000 positions selected each endpoint 5,000 times and each interior zone 10,000 times.

The README and design promise equally sized zones, but the tests explicitly enforce the current rounding thresholds. Endpoint ratios therefore receive half the knob travel promised by the documentation.

Resolution: document the existing nearest-index behavior, or intentionally introduce equal-width bins with an explicit compatibility strategy. Silently changing quantization can retune saved patches and automation.

## Scope and validation

The review covered all first-party module implementation, DSP, engine, model, runtime, routing and generated layout files under `src/`; the shared finite-value, polyphony and drawing-geometry helpers; plugin registration and manifest; both Makefiles; all seven Python generators and their source asset; checked-in SVGs through generator equality and structural checks; C++ and Python test code; README and MetaModule handoff documentation; and relevant design/implementation records. Independent module reviews were consolidated and their reported failures checked again.

The initial tracked worktree was clean. Local Rack SDK headers and its library were inspected or used to validate host behavior; the SDK itself, ignored build artifacts, and the separate MetaModule consumer were not audited as first-party code.

Validation performed on macOS arm64 with Apple Clang 21.0.0 and Python 3.9.6:

| Check | Result |
| --- | --- |
| `make -B -j4` | Passed; all seven translation units rebuilt and plugin linked |
| `make -C tests -B -j4 run` | Passed: 12 C++ executables and 13 Python scripts, including ASan/UBSan for C++ |
| Generated artifact checks | Passed for six panel SVG/header pairs, five state-switch frames and logo consistency |
| Actual plugin headless smoke probe | All six models produced finite output and correct channel counts through `1 → 16 → 4 → 1` transitions at 44.1/48/96/192 kHz |
| Actual parameter JSON probe | Every configured parameter round-tripped at three representative positions, within tolerance |
| Actual parameter text probe | Reproduced F1 in all four affected models |
| Focused DSP/assertion probes | Reproduced F2–F5 and both contract discrepancies |
| Filter modulation stress | All 12 branches remained finite for the tested impulse/max-resonance/alternating-cutoff cases |
| Coefficient-cache benchmark | Reproduced F7's overhead with matching checksums |

The build emitted the Rack SDK's macOS 10.9 deployment-target/libc++ warning. No project-source compile error was observed. The headless probe creates a Rack engine context and simulates connection state; it is a diagnostic harness, not a replacement for a complete Rack session.

Temporary evidence remains in this session at:

- `/private/tmp/wintoid-full-review.CIkuEY/`: actual-plugin host probe, assertion probe, and both-limit cutoff probe, with source and executables.
- `/private/tmp/wintoid-four-review.gl8TMf/`: oscillator DSP and quantity probes, including the production-style DSP build.
- `/private/tmp/wintoid-vortex-quantity-probe.cpp`, `/private/tmp/wintoid-vortex-review-probe.cpp`, `/private/tmp/wintoid-vortex-cache-probe.cpp` and corresponding executables.
- `/private/tmp/wintoid-review-brink-probe.cpp`: pulse-duration and publication-inspection probe.

These paths are temporary supporting evidence; the reproduction inputs and results above are the durable record. To rerun the actual-plugin probe after building:

```sh
c++ -std=c++11 -I Rack-SDK/include -I Rack-SDK/dep/include \
  /private/tmp/wintoid-full-review.CIkuEY/host_probe.cpp \
  -L Rack-SDK -lRack -o /private/tmp/wintoid-full-review.CIkuEY/host_probe
DYLD_LIBRARY_PATH="$PWD/Rack-SDK" \
  /private/tmp/wintoid-full-review.CIkuEY/host_probe
```

## Strengths and remaining coverage gaps

The audio paths use fixed-size state and bounded loops, with no heap allocation or locks identified in processing. FourV2's routing is immutable and acyclic; lane state is independent; shorter modulation inputs consistently broadcast lane 0. VortexV2 resets disconnected branches independently, and the shared finite-value guards and filter-state recovery provide useful protection. Generated artwork and layout consistency have extensive structural coverage, and the legacy model registrations remain available with their manifest entries hidden.

The main testing gap is host integration: the checked-in Python module tests largely inspect source strings and structure. They do not execute parameter editing, control composition, or complete module lifecycle behavior, which explains how F1 and F4 escaped a passing suite. The temporary host probe improves evidence for this review but is not part of the repository's regression suite. A small maintained Rack-facing harness would be more valuable than additional source-token assertions for these behaviors.

No fresh Rack GUI session, audio listening session, browser/Library-sync check, full patch-file migration exercise, Linux/Windows build, or MetaModule hardware test was performed. Parameter JSON checks are not equivalent to complete patch-loading validation. There was no installation or Rack restart because this review changed no plugin source, panels, or assets. No module-browser success is claimed.

Explicit legacy behaviors—Four's polarity-selective external-PM path, Vortex V1's unbounded drive tail, and the specified Brink hysteresis—were not treated as defects. Sample-quantized Brink trigger lengths were also not promoted to findings. Speculative invalid-host-input cases without an established normal trigger were excluded.

Suggested order: repair the numerical assertion helper; add executing regressions for F1–F4 and implement their fixes; settle D1/D2 before changing saved-patch behavior; then address display publication and coefficient reuse. Preserve V1 compatibility as a separate constraint throughout.
