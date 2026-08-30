# VortexV2 Design

Date: 2026-08-30

Status: approved in conversation; awaiting written-spec review

## Purpose

VortexV2 is a new VCV Rack module that preserves Vortex's multi-mode filter
engine while replacing its filter selector with twelve simultaneous filter
outputs. It is intended to use ordinary controls and interaction patterns that
can be represented by a 4ms MetaModule consumer, but the implementation work
in this project is limited to `wintoid-vcv`.

Vortex remains unchanged. VortexV2 is a separate module with its own model
identity, parameter and port IDs, DSP state, panel, and tests.

## Scope

This project includes:

- the VortexV2 module and its filter-bank DSP;
- twelve named audio outputs;
- the existing Cutoff, Resonance, Drive, and CV controls;
- per-output connection gating and reset behavior;
- a new FourV2-inspired panel and generated layout assets;
- VCV plugin metadata and host-side tests.

This project does not include changes to the sibling MetaModule wrapper,
MetaModule assets or packaging, SDK probes, firmware, or hardware validation.
MetaModule compatibility is a source-design constraint only: VortexV2 must
use standard Rack controls and connection APIs, with no required interaction
through a VCV-only menu or custom selector display.

## Identity and compatibility

- Preserve the existing `VortexMM` model, source, panel, IDs, and behavior.
- Register a new model with the permanent slug `VortexV2` and name `VortexV2`.
- Give VortexV2 new stable parameter, input, output, and light enums. There is
  no patch migration from the old Vortex selector parameter.
- Keep the shared source C++11-compatible and avoid new dependencies.
- Use standard Rack knobs, trimpots, inputs, and outputs for every interactive
  element.

## Signal and control contract

The twelve outputs retain Vortex's existing mode order and DSP meaning:

| Output ID order | Label | Processing |
|---:|---|---|
| 0 | `LP 6dB` | one first-order low-pass stage |
| 1 | `LP 12dB` | one second-order low-pass stage |
| 2 | `LP 24dB` | two cascaded second-order low-pass stages |
| 3 | `HP 6dB` | one first-order high-pass stage |
| 4 | `HP 12dB` | one second-order high-pass stage |
| 5 | `HP 24dB` | two cascaded second-order high-pass stages |
| 6 | `BP` | one second-order band-pass stage |
| 7 | `BP+` | two cascaded second-order band-pass stages |
| 8 | `Notch` | one second-order notch stage |
| 9 | `Notch+` | two cascaded second-order notch stages |
| 10 | `AP` | one second-order all-pass stage |
| 11 | `AP+` | two cascaded second-order all-pass stages |

Every branch receives the same input lane and the same effective Cutoff,
Resonance, and Drive values. The existing CV inputs and bipolar attenuverters
retain Vortex's current broadcast, scaling, clamping, and gain behavior.

VortexV2 has no filter-mode parameter and no interactive mode display. If the
audio input is unconnected, it follows the existing Vortex convention of one
effective lane at 0 V. For every connected output, the output channel count
matches that effective input lane count; an unconnected output remains
unpatched and is not processed.

Each output uses Vortex's existing output scaling. There is no summing,
normalization, automatic gain compensation, or cross-output interaction.

## DSP architecture

The implementation reuses the filter primitives and coefficient calculations
from `src/Vortex/dsp.h` and adds a VortexV2 filter-bank layer. The logical state
ownership is independent for every output and every polyphonic lane. A
disconnected branch therefore can be reset without disturbing any other
output.

For each audio sample, the module will:

1. Reset all runtime state if the sample rate changes, as Vortex does.
2. Determine the effective input lane count and reset newly inactive lanes.
3. Read the three main controls and connected CV values once for each lane.
4. Apply Drive to the input signal using the existing Vortex soft clip.
5. For each output, read its connection state.
6. On a connected-to-disconnected transition, reset that branch's state for
   every lane and skip its DSP work while it remains disconnected.
7. For every connected branch, configure and process the appropriate one- or
   two-stage filter and write the result at Vortex's existing gain.
8. Flush filter state denormals after processing.

The initial implementation deliberately favors independent branch state and
behavioral clarity over shared-stage optimization. A future optimization may
share mathematically identical stages only if it preserves independent reset
semantics and is justified by profiling; it is not part of VortexV2's initial
scope.

Connection detection must use the standard output connection state exposed by
Rack, rather than a custom UI event or a VCV-only callback. The previous
connection mask is module runtime state and is not saved in patches.

## Panel architecture

The initial fit target is 20 HP (101.6 mm) by 3U (128.5 mm). Width remains a
geometry decision: if real PJ301M port clearances, labels, and cable access do
not fit, the panel may widen or change matrix arrangement without changing
the output ID order or signal contract.

The panel follows FourV2's light SEM-inspired system:

- warm ivory background;
- dark charcoal legends and controls;
- blue-grey section rules and grouping;
- restrained orange functional accents;
- explicit static labels with strong contrast.

The first layout candidate is:

1. a compact identity/header area;
2. a global control band containing Cutoff, Resonance, Drive, their CV
   jack/attenuverter pairs, and the audio input;
3. a framed `FILTER OUTPUTS` section below;
4. a 3-column by 4-row output matrix in the existing twelve-output order.

Every output is an ordinary `PJ301MPort` with a clear static label. The
existing inverted output backplate treatment is retained so outputs remain
visually distinct from inputs. If the 3x4 candidate is too dense, the fallback
is a 2x6 arrangement or a wider panel, chosen by actual component geometry
and label clearance rather than by changing the DSP contract.

Panel artwork and coordinates are generated from a dedicated VortexV2 panel
script and checked-in SVG/header pair. No dynamic display is required.

## Error handling and runtime safety

- Keep the existing input lane clamp, CV broadcast rules, cutoff clamp,
  resonance-to-damping conversion, drive clamp, and sample-rate reset rules.
- Do not allocate memory in the audio process path.
- Reset all branch state on module reset and sample-rate changes.
- Reset a branch on disconnection before skipping it, so reconnection starts
  from a known state.
- Preserve Vortex's denormal flushing and output voltage conventions.
- A disconnected output is not treated as a reason to change the effective
  input lane count or the behavior of other outputs.

## Verification

Host DSP tests will cover:

- each VortexV2 output matching the equivalent Vortex mode for the same input,
  controls, and lane;
- simultaneous outputs having no state cross-talk;
- per-output disconnect, reset, skip, and reconnect behavior;
- mono, polyphonic, broadcast-CV, and lane-transition behavior;
- output scaling, filter-stage order, and finite state/output values.

Module and panel tests will cover:

- model registration and the permanent `VortexV2` identity;
- exact parameter/input/output counts and output ordering;
- absence of the old mode parameter and interactive selector display;
- generated SVG/header consistency;
- actual port, label, edge, and output-ring clearances for the selected panel
  arrangement;
- regression protection showing the existing Vortex module remains unchanged.

The normal repository build and test workflow will be run after implementation.
Because MetaModule work is outside this project, any physical-device
connection, CPU, packaging, or hardware-layout checks remain external follow-up
acceptance work rather than repository completion criteria.
