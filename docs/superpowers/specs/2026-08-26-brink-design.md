# Brink Design

## Summary

Brink is a 12 HP, dual window-comparison utility for VCV Rack. Each channel turns a signal's relationship to a voltage window into gates, a continuous position voltage, and explicit directional boundary events. A shared logic section combines the two channel states.

Brink follows the existing wintoid implementation pattern: a Rack-independent DSP core, a thin Rack wrapper, generated panel geometry, NanoVG labels, and standalone C++ tests. It extends that pattern with 16-channel polyphony and sample-accurate event detection.

## Goals

- Provide two independent, voltage-controlled comparison windows.
- Expose continuous bipolar position within each window.
- Emit a distinct trigger for every boundary and crossing direction.
- Retain immediately useful shared Boolean and toggle-state outputs.
- Process both CV and audio-rate signals sample-accurately.
- Support up to 16 polyphonic lanes.
- Give the module an original name, panel organization, visual hierarchy, and product identity consistent with the wintoid portfolio.

## Non-Goals

- Configurable event routing or menu-driven output modes.
- Adjustable trigger duration or panel hysteresis controls.
- Persisting transient comparator or toggle state in patch JSON.
- Refactoring the existing Four or Vortex modules except where a build or registration change is required for Brink.

## Panel and Visual Design

### Dimensions and hierarchy

Brink occupies 12 HP. Its panel contains two mirrored vertical columns, Channel A on the left and Channel B on the right. Each column reads from top to bottom:

1. `CENTER` and `WIDTH` knobs.
2. `SIGNAL` input and `POSITION` output.
3. `CENTER CV` and `WIDTH CV` inputs with bipolar attenuverters.
4. `INSIDE` and `OUTSIDE` gate outputs.
5. A 2-by-2 boundary-event matrix: `LOW UP`, `HIGH UP`, `LOW DOWN`, and `HIGH DOWN`.

The shared `AND`, `OR`, `XOR`, and `STATE` outputs span the bottom of the panel. Labels may use arrow glyphs where they remain legible, but Rack parameter and port names use the full words for accessibility and tooltips.

### Position indication

Each channel has a slim vertical position rail. It shows the first polyphonic lane's clamped position relative to the lower boundary, centre, and upper boundary. It must not imitate a large multicolour status lamp. Output lights use Rack's standard low-cost light widgets and illuminate when any polyphonic lane is active.

### Portfolio styling

The background uses the existing wintoid dark navy foundation. Channel A uses a restrained teal accent and Channel B a restrained warm-orange accent. Fine routing marks indicate normalled A-to-B connections without dominating the panel.

The module title `Brink` is centred at the same top position and uses the same typography as Four and Vortex.

The wintoid logo exactly matches the existing modules:

- DejaVu Sans, font size 10.
- Horizontally centred at 124.5 mm.
- `wint` in white and `oid` in RGB `(255, 77, 0)`.
- A matching two-colour underline 2.5 mm below the text.

## Channel Controls

Each channel provides the following controls and inputs:

| Item | Range/default | Behaviour |
| --- | --- | --- |
| `CENTER` | -5 V to +5 V; default 0 V | Sets the centre of the window. |
| `WIDTH` | 0 V to 10 V; default 5 V | Sets the total distance between the boundaries. |
| `CENTER CV` | Signal input | Added to `CENTER` after attenuation. |
| Center attenuverter | -100% to +100%; default 0% | Scales `CENTER CV`. |
| `WIDTH CV` | Signal input | Added to `WIDTH` after attenuation. |
| Width attenuverter | -100% to +100%; default 0% | Scales `WIDTH CV`. |
| `SIGNAL` | Signal input | Signal evaluated against the effective window. |

The final control values are:

```text
center = centerKnob + centerCv * centerAttenuverter
width  = clamp(widthKnob + widthCv * widthAttenuverter, 0.001 V, 20 V)
lower  = center - width / 2
upper  = center + width / 2
```

The positive minimum width ensures that position is always defined and that window orientation never inverts.

## Channel Outputs

### State gates

- `INSIDE` is +10 V while the signal is inside the effective window and 0 V otherwise.
- `OUTSIDE` is the logical inverse of `INSIDE`.

The comparison uses 1 mV Schmitt hysteresis around both boundaries. Hysteresis affects state transitions and event generation, but not the continuous position output.

Initial classification is `BELOW` when `signal < lower`, `ABOVE` when `signal > upper`, and `INSIDE` otherwise. Subsequent transitions use the hysteresis amount `h = 0.001 V`:

- From `BELOW`: transition to `ABOVE` when `signal >= upper + h`; otherwise transition to `INSIDE` when `signal >= lower + h`.
- From `INSIDE`: transition to `BELOW` when `signal < lower - h`; otherwise transition to `ABOVE` when `signal > upper + h`.
- From `ABOVE`: transition to `BELOW` when `signal <= lower - h`; otherwise transition to `INSIDE` when `signal <= upper - h`.

Direct `BELOW`-to-`ABOVE` and `ABOVE`-to-`BELOW` transitions are legal and represent crossing both boundaries in one sample.

### Position

`POSITION` maps the input's location within the window to a bipolar voltage:

```text
position = clamp(10 * (signal - center) / width, -5 V, +5 V)
```

Therefore:

- At or below the lower boundary, the output is -5 V.
- At the centre, the output is 0 V.
- At or above the upper boundary, the output is +5 V.

### Boundary events

Each event output emits a +10 V pulse lasting 1 ms:

- `LOW UP`: relative position crosses the lower boundary upward.
- `LOW DOWN`: relative position crosses the lower boundary downward.
- `HIGH UP`: relative position crosses the upper boundary upward.
- `HIGH DOWN`: relative position crosses the upper boundary downward.

The event mapping follows state transitions. `BELOW` to `INSIDE` fires `LOW UP`; `INSIDE` to `ABOVE` fires `HIGH UP`; `ABOVE` to `INSIDE` fires `HIGH DOWN`; and `INSIDE` to `BELOW` fires `LOW DOWN`. A direct transition fires the two boundary events encountered in travel order.

Direction is measured in coordinates relative to the current window. Moving a boundary across a stationary signal therefore creates the same event as moving the signal across that boundary in the opposite physical direction.

If one sample moves from below the lower boundary to above the upper boundary, both `LOW UP` and `HIGH UP` fire. The corresponding downward jump fires both downward outputs. Re-triggering an already-active 1 ms pulse restarts its duration.

On the first processed sample after construction, reset, sample-rate change, or lane activation, the channel records its initial relationship to the window without emitting boundary events.

## Input Normalisation

Channel A normalises to Channel B independently for all three signal inputs:

- A `SIGNAL` normalises to B `SIGNAL`.
- A `CENTER CV` normalises to B `CENTER CV`.
- A `WIDTH CV` normalises to B `WIDTH CV`.

Patching any B input breaks only that input's normalisation. Knob positions and attenuverter values are never normalled.

Normalisation copies the complete polyphonic cable, including its channel count and all lane voltages.

## Shared Logic

The logic section operates on the two `INSIDE` states for each polyphonic lane:

- `AND` is high when A and B are both inside.
- `OR` is high when A or B is inside.
- `XOR` is high when exactly one of A or B is inside.
- `STATE` toggles on each rising edge of `XOR`.

All high states are +10 V and all low states are 0 V. `STATE` starts low after construction, patch loading, or module reset. It is runtime state and is not serialised.

## Polyphony

Brink supports 1 to 16 lanes. Each channel's outputs use the channel count of its effective `SIGNAL` input. If an effective input reports no channels, Brink treats it as one monophonic lane at 0 V and evaluates that voltage normally against the window. This preserves the stated `INSIDE`/`OUTSIDE` inverse relationship and gives deterministic unpatched behaviour.

The shared logic outputs use the greater effective channel count of A and B. When a monophonic channel is combined with a polyphonic channel, its lane 0 state broadcasts to all lanes of the polyphonic side. When both sides are polyphonic with unequal counts, lanes beyond the shorter side use that side's lane 0 state. This broadcast rule is explicit and does not depend on an incidental Rack port-accessor behaviour.

Every comparator, trigger timer, XOR edge detector, and toggle state is independent per lane.

## Processing and State Model

The processing order for each sample is:

1. Resolve A inputs and B normalisations.
2. Compute effective centre, width, lower boundary, and upper boundary per channel and lane.
3. Compute unclamped relative position for event detection and clamped `POSITION` voltage for output.
4. Advance each channel's hysteretic state machine.
5. Detect all directional boundary crossings and advance pulse timers.
6. Write per-channel gates, position, event voltages, and lights.
7. Derive shared logic, detect XOR rising edges, update `STATE`, and write logic outputs.

The Rack wrapper performs voltage and port handling only. Window maths, state transitions, event classification, pulse timing, broadcasting decisions, and shared logic live in testable Rack-independent code.

## Source Organization

Implementation will follow the repository's existing module structure:

```text
src/Brink/Brink.cpp          Rack module and widget
src/Brink/dsp.h              Rack-independent processing and state
src/Brink/layout.h           Generated coordinates in millimetres
scripts/generate_panel_brink.py
res/Brink.svg
tests/test_brink_dsp.cpp
```

The implementation also updates the existing plugin registration, plugin manifest, README module list, and test Makefile. No repository file, identifier, comment, description, or asset metadata will name or advertise an external inspiration.

## Reset and Exceptional Cases

- Module reset clears all event timers, per-lane initialisation flags, XOR history, and `STATE` outputs.
- Sample-rate changes clear event timing state and reinitialise comparator history without emitting triggers.
- Newly appearing polyphonic lanes initialise silently on their first sample.
- Disappearing lanes have their state cleared before possible later reuse.
- Effective width never falls below 1 mV, preventing division by zero.
- Large single-sample changes may cross and trigger both boundaries, as specified above.
- Non-finite intermediate values are replaced with safe defaults: centre and signal become 0 V, and width becomes its 1 mV minimum. Outputs must never emit NaN or infinity.

## Testing Strategy

### Rack-independent DSP tests

Standalone C++ tests cover:

- Lower, centre, and upper position mapping plus clamping outside the window.
- Window states at and around each hysteretic boundary.
- Each of the four directional event outputs.
- Equivalent events caused by signal movement and window movement.
- Full-window jumps that cross both boundaries in one sample.
- Re-trigger and 1 ms pulse duration at several sample rates.
- Silent initialisation after construction, reset, sample-rate change, and lane activation.
- `AND`, `OR`, `XOR`, and rising-edge toggle behaviour.
- Independent polyphonic state and explicit mono-to-poly broadcasting.
- Minimum and maximum effective widths.
- Non-finite input sanitisation.

### Integration verification

- Build all standalone tests with warnings and sanitizers enabled.
- Run the complete standalone test suite.
- Build the Rack plugin against the configured Rack SDK.
- Validate the manifest registration and asset paths.
- Confirm that the panel reports 12 HP and all widgets remain within bounds.
- Smoke-test normalled inputs, polyphonic channel counts, lights, reset, and patch reload in Rack when the local environment supports launching it.

## Acceptance Criteria

Brink is ready for release when:

- Both channels produce the specified gates, bipolar position, and four directional events.
- Normalisation, shared logic, and 16-lane polyphony follow this document exactly.
- Event outputs are stable around noisy boundaries and emit no startup/reset artefacts.
- The mirrored 12 HP panel is readable at normal Rack zoom and visually belongs beside Four and Vortex.
- The title and wintoid logo match the established portfolio placement and treatment.
- All standalone tests pass under address and undefined-behaviour sanitizers.
- The Rack plugin builds and loads with Brink registered in its manifest.
- Repository-bound Brink materials contain only its own product identity.
