# Four and Vortex Polyphony Design

## Summary

Four and Vortex will gain native 1-to-16-channel polyphony without adding controls or changing their panels. Four derives its voice count only from `V/OCT`; Vortex derives its voice count only from `AUDIO IN`. Every active lane owns independent oscillator or filter history, while all secondary CV inputs follow one shared, explicit broadcast rule.

The change keeps the existing scalar DSP algorithms intact. Rack-facing code becomes a thin per-lane adapter around fixed-size state arrays, supported by a small Rack-independent polyphony helper shared with Brink.

## Goals

- Give Four and Vortex complete support for 1 to 16 polyphonic lanes.
- Make the primary signal input the sole authority for each module's voice count.
- Keep each voice's oscillator, feedback, DC-blocker, and filter history independent.
- Preserve the current sound and processing behaviour for monophonic patches.
- Make mismatched channel counts deterministic and consistent across Four, Vortex, and Brink.
- Keep one-voice CPU use effectively equivalent to the current monophonic implementation.
- Document the new capability in Rack tooltips, the README, and library metadata.

## Non-Goals

- A panel voice-count parameter, channel display, activity light, or force-mono option.
- Voice allocation, note stealing, envelopes, gates, or per-note lifecycle management.
- Allowing secondary modulation inputs to increase the voice count.
- SIMD, block processing, or a broad rewrite of the existing DSP engines.
- Changing either module's panel, parameter IDs, port IDs, model slug, or patch format.
- Changing Four's algorithms or Vortex's filter equations and mode responses.
- Adding new sanitisation or sound-design behaviour unrelated to polyphony.

## Shared Polyphony Rules

### Primary-input voice count

Each module has one primary input:

| Module | Primary input | Output channel count |
| --- | --- | --- |
| Four | `V/OCT` | Effective channel count of `V/OCT` |
| Vortex | `AUDIO IN` | Effective channel count of `AUDIO IN` |

The effective channel count is clamped to 1 through 16. An unconnected primary input reports zero Rack channels and is therefore treated as one lane at 0 V.

This means an unpatched Four continues to generate its 0 V pitch, while an unpatched Vortex processes a single 0 V audio lane. A polyphonic secondary CV input never creates voices by itself.

Four continues to use the `V/OCT` channel count when one or more operators use fixed-frequency mode. Fixed-frequency operators ignore that lane's pitch voltage as they do now, but the primary cable still defines how many independent synth voices run.

### Secondary-input broadcasting

Every input voltage is read using the same explicit lane rule:

- An unconnected input contributes 0 V.
- A monophonic input broadcasts lane 0 to every active voice.
- A polyphonic input supplies its corresponding lane while that lane exists.
- An active voice beyond a shorter polyphonic input's channel count uses that input's lane 0.

For example, with eight active voices and a three-channel CV, source lanes are:

```text
0, 1, 2, 0, 0, 0, 0, 0
```

Four applies this rule to `V/OCT`, external PM, modulation CV, and every operator Level, Warp, Fold, and Feedback CV. Vortex applies it to audio, Cutoff CV, Resonance CV, and Drive CV.

## Shared Helper

A new Rack-independent header, `src/polyphony.h`, owns the common channel semantics under `wintoid::polyphony`. It provides:

- The maximum channel count of 16.
- Effective channel-count clamping.
- Source-lane selection for explicit broadcasting.
- A small channel-transition helper that identifies the contiguous lanes requiring reset when an active count grows or shrinks.

The transition helper accepts the previous count, current effective count, and a reset callback. It invokes the callback for the half-open lane range `[min(previous, current), max(previous, current))`; the module continues to own its actual DSP state. The previous count may be zero before initial activation, while a current processing count is always 1 through 16. The helper performs no allocation and is testable without the Rack SDK.

Brink's existing `brink::MAX_CHANNELS`, `brink::effective_channels()`, and `brink::broadcast_lane()` interface remains available as constants or forwarding wrappers around the shared implementation. Brink's output and patch behaviour must not change as part of this refactor.

## Four Processing Design

### State

Four replaces its single `four::EngineState` with a fixed array of 16 engine states. Each lane therefore has independent:

- Phase for all four operators.
- Per-operator feedback history.
- DC-blocker history.

The module also tracks its previous active channel count and previous sample rate.

### Per-sample flow

For every Rack sample, Four:

1. Detects a sample-rate change and clears all runtime states when required.
2. Gets the effective voice count from `V/OCT` and prepares lanes changed since the previous sample.
3. Sets `MAIN OUT` to that channel count.
4. Reads module-wide knobs and switches and computes values that do not depend on CV once.
5. For each active lane, reads all inputs through the shared broadcast rule.
6. Builds that lane's existing `four::EngineParams`, preserving the current formulas, clamps, and evaluation order.
7. Calls the unchanged scalar `four::engine_process()` with that lane's `EngineState`.
8. Writes the scaled result to the corresponding output lane.

Only active lanes are processed. Algorithm changes, frequency-mode changes, fold-type changes, and ordinary parameter modulation do not reset oscillator state; this retains current Four behaviour.

## Vortex Processing Design

### State

Vortex uses a Rack-independent voice-state bundle containing one `vortex::Filter1` and two `vortex::Filter2` instances, plus a reset operation that clears all three filters. The Rack module owns a fixed array of 16 such bundles.

Mode remains module-wide rather than per voice. The module tracks its last mode, previous active channel count, and previous sample rate.

### Per-sample flow

For every Rack sample, Vortex:

1. Detects a sample-rate change and clears all runtime states when required.
2. Reads the module-wide mode. If it changed, all 16 voice states are reset before processing.
3. Gets the effective voice count from `AUDIO IN` and prepares lanes changed since the previous sample.
4. Sets `AUDIO OUT` to that channel count.
5. Reads knob values and computes values shared by every voice once.
6. For each active lane, reads audio and all CV inputs through the shared broadcast rule.
7. Derives that lane's effective cutoff, damping, and drive using the current formulas and clamps.
8. Executes the current drive stage and current mode-specific filter path against that lane's state.
9. Flushes denormals in that lane and writes its scaled output voltage.

The mode-change reset deliberately affects inactive as well as active lanes, ensuring no state from a previous mode can reappear after a later channel-count increase.

## State Lifecycle

Both modules use the same lifecycle conventions:

- Construction begins with clean state for all 16 lanes.
- Module reset clears every lane and resets channel/sample-rate tracking.
- A sample-rate change clears every lane before processing at the new rate.
- When the active count grows, every newly active lane is reset immediately before its first sample.
- When the active count shrinks, every disappearing lane is reset immediately.
- A later re-expansion therefore cannot restore stale oscillator or filter history.
- Lanes that remain active across a count change retain continuity.

A cable replacement that preserves the same channel count is ordinary signal discontinuity and does not itself reset state. No transient runtime state is serialised to patch JSON.

## Monophonic Operation and Compatibility

An unpatched or monophonic primary input runs exactly one lane. Users who want a monophonic patch should supply a mono primary signal. If an upstream source is polyphonic, they can select one lane with a split/channel-selection utility or mix the source to mono before the primary input.

Existing patches remain compatible because:

- Parameter, input, output, and model identifiers do not move or change.
- No panel geometry or widget changes are made.
- No new JSON state is required.
- Existing monophonic input reads correspond to lane 0 under the new adapter.
- Scalar DSP functions, constants, and mode/algorithm selection remain unchanged.

The plugin version advances from 2.1.0 to 2.2.0 as a backward-compatible feature release. The Four and Vortex manifest entries gain the `Polyphonic` tag and descriptions mentioning 16-channel support. Their README sections state which input determines the voice count. The primary-input tooltip descriptions identify `V/OCT` as Four's polyphonic voice-count source and `Audio` as Vortex's polyphonic voice-count source, both with a 1-to-16-channel range.

## Performance Model

State is stored in fixed-size arrays; there are no heap allocations, container growth, locks, or other unbounded work in the audio thread. Per-sample processing is linear in the active voice count.

At one channel, each module executes one invocation of the same scalar DSP used today, plus a small amount of channel-count and lane-selection bookkeeping. There is no separate mono DSP path because that would add maintenance risk without avoiding the dominant DSP work. Shared knob calculations stay outside the lane loop, and inactive lanes are not processed.

Resetting all 16 states occurs only for construction/reset, sample-rate changes, and Vortex mode changes. Resetting a contiguous changed-lane range occurs only when the primary channel count changes.

## Source Organization

Expected implementation changes are:

```text
src/polyphony.h                 Shared Rack-independent channel helpers
src/Brink/dsp.h                 Forward existing Brink helpers
src/Four/Four.cpp               Four Rack polyphonic adapter and state lifecycle
src/Four/engine.h               Explicit EngineState reset helper
src/Vortex/Vortex.cpp           Vortex Rack polyphonic adapter and state lifecycle
src/Vortex/dsp.h                Per-voice filter-state bundle/reset helper
tests/test_polyphony.cpp         Shared count, broadcast, and transition tests
tests/test_four_engine.cpp       Four state independence and reset coverage
tests/test_vortex_dsp.cpp        Vortex state independence and reset coverage
tests/test_brink_dsp.cpp         Brink forwarding behaviour regression coverage
tests/Makefile                   New standalone test target
README.md                        User-facing polyphony documentation
plugin.json                     Version, descriptions, and Polyphonic tags
```

No panel SVG, layout generator, widget position, or module registration change is required.

## Testing Strategy

### Shared helper tests

Standalone tests cover:

- Channel requests below 1, inside 1 through 16, and above 16.
- Unconnected, monophonic, matching polyphonic, and shorter polyphonic source-lane selection.
- The explicit eight-voice/three-channel mapping.
- Exact reset-lane ranges for growth, shrinkage, no change, initial activation, and the 16-channel boundary.
- Brink's forwarding interface producing the same results as the shared helper.

### DSP state tests

Four tests interleave two independently parameterised `EngineState` instances and compare each output stream with a separately processed reference stream. Processing or resetting one lane must not change the other, and resetting a state must reproduce its clean initial sequence.

Vortex tests perform equivalent interleaved-reference checks with separate filter-state bundles. Bundle reset must clear all three filters, and activity in one bundle must not affect another.

All existing Four, Vortex, and Brink scalar tests remain in the suite. A changed monophonic reference result is a regression unless the design is amended and approved separately.

### Build and Rack acceptance

Verification includes:

- Building and running the complete standalone suite with its warning and sanitizer flags.
- Building the plugin against the configured Rack SDK.
- Confirming existing mono patches produce one output channel and unchanged behaviour.
- Checking primary inputs at 1, 4, 8, and 16 channels produce the same output count.
- Confirming polyphonic secondary CV inputs do not create voices.
- Confirming mono CV broadcast and shorter-poly lane-0 fallback.
- Confirming voices have independent oscillator/filter histories.
- Shrinking and re-expanding channel counts to verify stale state does not return.
- Confirming Vortex mode changes reset all 16 lane states, including lanes inactive at the time of the change.
- Confirming Four algorithm changes do not reset active lanes.
- Briefly comparing Rack's CPU meter at one and 16 voices to catch unexpected overhead or non-linear scaling.
- Checking README, tooltip, manifest version, descriptions, and tags.

Rack smoke tests are performed when the local environment can launch Rack; otherwise they remain an explicit release-checklist item and are reported as unverified rather than silently assumed.

## Acceptance Criteria

The feature is ready when:

- Four and Vortex emit 1 to 16 channels determined solely by their documented primary inputs.
- All other inputs obey the documented broadcast rule and never increase voice count.
- Every active lane has independent DSP state.
- State reset behaviour matches this document for count changes, module reset, sample-rate changes, and all-lane Vortex mode changes.
- One-channel patches retain the current DSP behaviour with negligible adapter overhead.
- Existing patches, identifiers, panels, and model registration remain compatible.
- Brink retains its current channel behaviour after adopting the shared helper.
- All standalone tests and the Rack plugin build pass.
- Rack-facing behaviour is smoke-tested or clearly identified as a remaining release check.
- Documentation and library metadata accurately describe 16-channel polyphony.
