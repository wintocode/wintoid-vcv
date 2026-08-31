# FourV2 Design

Date: 2026-08-29

Status: approved in conversation; awaiting written-spec review

## Purpose

FourV2 is a new four-operator phase-modulation oscillator for VCV Rack. It preserves the useful synthesis engine of Four while replacing controls and panel organisation that are confusing in VCV or cannot be represented faithfully by 4ms MetaModule.

Four V1 remains unchanged. FourV2 is a separate module, with its own model slug, parameter IDs, patch state, DSP implementation, panel, and tests. Existing Four patches therefore retain their current behaviour.

## Scope

This specification covers FourV2 in `wintoid-vcv`:

- fixed algorithm selection and routing display;
- operator frequency, level, shaping, feedback, and CV behaviour;
- internal and external phase modulation;
- raw carrier mixing, master level, and overload indication;
- a MetaModule-compatible interaction model;
- panel architecture and the initial light-panel visual system;
- implementation boundaries and validation.

VortexV2, the Brink restyle, final family-wide visual standardisation, and changes to `wintoid-metamodule` are separate projects. The sibling MetaModule project may be inspected for compatibility evidence but must not be modified by this work.

## Compatibility and identity

- Keep Four V1's model, IDs, DSP, panel, and patch compatibility unchanged.
- Register FourV2 as a new module with the permanent model slug `FourV2`.
- Use `FourV2` as the module name. Its panel-title typography, size, and placement are deliberately reviewed again on the working VCV panel; the name itself is not provisional.
- Build every interactive control from ordinary Rack parameters, switches, jacks, and lights supported by MetaModule.
- Use MetaModule-supported dynamic graphics only for informative displays. No required action may depend on clicking a display, opening a VCV-only menu, or dynamically changing a parameter's available choices.

## Algorithms

FourV2 uses 11 fixed algorithm slots. An 11-position algorithm knob selects only
valid, static topologies.

| Number | Modulation routing | Carriers sent to the output mix |
|---:|---|---|
| 1 | `4 → 3 → 2 → 1` | 1 |
| 2 | `(3 + 4) → 2 → 1` | 1 |
| 3 | `4 → 3 → 1` and `2 → 1` | 1 |
| 4 | `4 → 3 → 1` and `2 → 1` | 1 |
| 5 | `4 → 3` and `2 → 1` | 1 and 3 |
| 6 | `4 → (1, 2, 3)` | 1, 2, and 3 |
| 7 | `4 → 3`, plus independent 2 and 1 | 1, 2, and 3 |
| 8 | no internal modulation | 1, 2, 3, and 4 |
| 9 | `4 → 3 → (1, 2)` | 1 and 2 |
| 10 | `(3 + 4) → (1, 2)` | 1 and 2 |
| 11 | `(2 + 3 + 4) → 1` | 1 |

The engine treats the table as immutable data. It converts the user-facing `1–11` selection to an internal `0–10` index and defensively clamps that index before table access.

Every one of the 11 algorithms uses all four operators as a carrier, a modulator, or both. FourV2 therefore has no disconnected `NONE` state and does not implement the dormant-operator CPU gating considered for free routing. All operator phases continue advancing even when an Output control is zero, so raising a level or applying CV cannot restart an operator at an arbitrary phase. Any later optimisation must preserve that behaviour exactly and be justified by profiling.

## Routing display

The selected algorithm is shown in an approximately 21 mm-high, landscape routing display beside the algorithm knob.

- Numbered circles represent operators.
- Orange paths represent phase modulation and point from modulator to destination.
- Gold paths represent direct carrier output.
- Multiple carriers join a shared gold output rail, making algorithm 8 unambiguous without implying modulation between operators.
- The display accommodates both geometric extremes: the serial `4 → 3 → 2 → 1` chain and the fan-in `(2 + 3 + 4) → 1` graph.
- The display is informational. The algorithm knob is the only algorithm control.

## Operator controls

Each of the four operator sections contains the same controls in the same positions:

| Control | Behaviour |
|---|---|
| Frequency mode | Standard two-position `RATIO / FIXED` switch |
| Coarse frequency | Mode-dependent continuous parameter described below |
| Frequency readout | Local MetaModule-safe display showing canonical ratio or Hz |
| Fine | Per-operator tuning over `-100` to `+100` cents |
| Output | One operator-output level controlling every destination of that operator |
| Warp | Continuous `Sine → Triangle → Saw → Pulse` morph, unchanged from Four V1 |
| Fold | Continuous fold amount |
| Fold Type | Standard three-position rotary selector: Symmetric, Asymmetric, Soft Clip |
| Feedback | Self-phase-modulation amount |

`OUTPUT` replaces the ambiguous `LEVEL` panel label. For a carrier it controls audible mix level; for a modulator it controls modulation depth; for an operator with both roles it controls both. FourV2 does not split carrier volume and modulation level into separate controls.

### Ratio mode

Ratio mode has 15 evenly sized selection zones. Ratios decrease clockwise, with `4:1` at the counter-clockwise/left end and `1:8` at the clockwise/right end:

`4:1, 3:1, 2:1, 3:2, 4:3, 1:1, 3:4, 2:3, 1:2, 1:3, 1:4, 1:5, 1:6, 1:7, 1:8`

`1:1` is therefore the sixth position rather than the geometric 12 o'clock position. Equal selection-zone width is more important than centring `1:1`.

All values use reduced canonical notation. Examples include `1:4`, not `0.25:1`; `3:2`, not `1.5:1`; and `2:3`, not `1:1.5`.

### Fixed mode

Fixed mode interprets the same coarse parameter continuously and exponentially from approximately `1 Hz` to `10 kHz`. It is not quantised to the ratio zones.

The underlying coarse parameter remains continuous in both modes. Ratio mode quantises it inside the module, while Fixed mode uses the continuous value. This avoids relying on runtime changes to parameter snapping that a MetaModule host may not reproduce.

### Frequency readouts

Each operator has its own small dynamic readout beside the coarse control:

- Ratio mode shows the selected canonical ratio.
- Fixed mode shows Hz, using kHz formatting where appropriate.
- The readout is not interactive and is not required to change the parameter.
- Custom `ParamQuantity` text may supplement the readout in VCV but may not be the only way to discover the selected ratio or frequency.

## CV patchbay

The operator sound controls remain visually quiet inside four framed operator sections. Their per-operator modulation connections live in a separate framed `CV PATCHBAY` below them.

The patchbay is a matrix with operator columns 1–4 and parameter rows:

1. Output
2. Warp
3. Fold
4. Feedback

Every matrix cell contains a bipolar attenuverter and its CV input. This preserves Four V1's per-operator modulation facilities while confining cable density to one region. The parameter rows align with the operator sections so both operator-by-operator and parameter-by-parameter scanning remain possible.

## Global controls and signal flow

The global section contains:

- Algorithm knob and routing display;
- global Tune;
- `PM DEPTH`, its CV input, and bipolar CV attenuverter;
- `MASTER` output level;
- V/Oct input;
- External PM input and bipolar attenuverter;
- main output;
- red `OVER` light.

`PM DEPTH` replaces the cryptic `XMod` label. It scales all internal operator-to-operator phase-modulation paths after each source operator's Output level.

### External PM

External PM is applied directly to the phase of every carrier in the selected algorithm. It is added with internal PM and self-feedback before waveform generation, Warp, and Fold. It is not applied by reconstructing phase from the final mixed waveform.

The initial scaling is:

`external phase cycles = input volts × attenuverter × 0.1`

Thus a ±5 V input at full positive attenuation produces ±0.5 phase cycles. Negative attenuverter settings invert the modulation. The input is not rectified and is not used twice as both signal and depth. This scaling receives a listening check in VCV before release; changing the constant before release is permitted if the agreed range proves impractical.

### Carrier mix and Master

Carrier signals are summed raw. FourV2 does not divide by carrier count, apply automatic gain compensation, soft-clip the sum, or otherwise change a carrier merely because another carrier is present.

`MASTER` scales the raw sum. At full Master, one full-scale carrier produces approximately ±5 V, while four phase-aligned full-scale carriers can theoretically produce approximately ±20 V.

### Over indication

The red `OVER` light monitors the post-Master output voltage without modifying it.

- It activates when the absolute voltage of any polyphonic output lane reaches or exceeds 10 V.
- A peak hold of approximately 250 ms makes short excursions visible.
- It does not clip, limit, compress, or normalise the signal.
- `OVER` is the panel label rather than `CLIP`, because a VCV patch may attenuate the signal before it reaches hardware even though ±10 V is the MetaModule physical-output range.

## Defaults

The default patch is a plain sine tone:

- Algorithm 1;
- Ratio mode and `1:1` on all operators;
- OP1 Output at 100%;
- OP2, OP3, and OP4 Output at 0%;
- operator Fine, Warp, Fold, Feedback, and all CV attenuverters at zero;
- Symmetric Fold Type;
- global Tune at zero;
- PM Depth and Master at their existing full-scale defaults;
- External PM attenuverter at zero.

## Panel architecture

FourV2 targets 32 HP (162.56 mm) by 3U. This is the selected balance between the complete control set and rack space, but it remains subject to one actual-size fit review before release. The width may increase only if real component geometry, legibility, or safe interaction spacing cannot satisfy this specification at 32 HP.

The vertical hierarchy is:

1. module identity and global routing/control section;
2. four aligned framed operator sections;
3. framed CV patchbay;
4. shared I/O and overload indication.

Framed sections use the same principle as Brink's Channel A and Channel B panels: a softly differentiated fill, fine border, rounded corners, and an explicit section heading. Grouping must come from spatial hierarchy and restrained framing rather than dense decoration.

## Visual system

The initial direction is a warm, light `SEM instrument` panel:

- warm ivory field;
- dark charcoal legends and black controls;
- blue-grey structural rules and section accents;
- restrained orange functional accents;
- dark charcoal display fields with warm illuminated text;
- low visual stress and high label contrast.

The aim is to combine the warmth and directness of the Oberheim SEM with the disciplined hierarchy admired in XAOC Devices modules, without copying either brand's decorative identity.

### wintoid mark

The logo retains its existing concept:

- lowercase `wintoid`;
- DejaVu Sans as the initial typeface;
- `wint` in vivid blue (initial target `#155f91`);
- `oid` in vivid orange (initial target `#ed5b22`);
- a matching blue underline exactly under `wint`;
- a matching orange underline exactly under `oid`.

The colour boundary occurs strictly between the complete `t` and `o` glyphs. Neither colour nor underline may cut through a glyph or extend beyond its text group.

The final static logo is produced as an optically checked vector mark. Shape the complete DejaVu Sans word first, freeze the glyph positions, then colour glyphs and underline groups. Do not independently measure and concatenate `wint` and `oid`, as the current implementation does. Validate the mark at its real panel size on ivory because the selected vivid chroma can exaggerate optical effects.

The appearance of the `FourV2` panel title—font treatment, size, and placement—is intentionally selected during the working VCV visual review. The title text remains `FourV2`.

## MetaModule compatibility contract

FourV2 must be consumable by a future MetaModule plugin without redesigning its interaction model.

- Algorithm: ordinary static 11-position parameter.
- Ratio/Fixed and Fold Type: ordinary static switches/selectors.
- Ratio selection: deterministic quantisation of a continuous parameter into 15 static zones.
- Routing and frequency readouts: non-interactive dynamic graphics/text implemented through supported display facilities.
- Over indication: ordinary module light.
- Panel interaction: no required buttons, context menus, custom clickable displays, dynamic menus, rejected parameter states, or commit gestures.
- Custom graphics draw only on supported layers and remain within their declared bounds.
- DSP and parameter semantics do not depend on VCV widget callbacks or `ParamQuantity::setValue()` interception.

## Implementation boundaries

- Add FourV2 under a dedicated `src/FourV2/` boundary with focused module, DSP, engine, layout, and display responsibilities.
- Reuse existing generic polyphony and UI-geometry utilities where appropriate.
- Do not alter Four V1 to share code unless the extracted unit is demonstrably generic, behaviour-preserving, and covered by both V1 and V2 tests. Isolation is preferred over risky deduplication.
- Register the new model and add its panel/assets inside `wintoid-vcv` only.
- Do not change the sibling `wintoid-metamodule` repository.

## State and defensive behaviour

Rack's standard parameter persistence stores the module state. No dynamic routing graph or custom choice list needs separate JSON state.

At the DSP boundary:

- clamp algorithm and Fold Type indices before table access;
- map all finite coarse values deterministically in either frequency mode;
- clamp bounded levels and shaping controls to their documented ranges;
- treat non-finite restored/control values as their safe defaults;
- maintain independent engine state for every active polyphonic lane;
- reset changed lanes consistently with the existing polyphony utilities.

No user selection can create an invalid or cyclic graph because only the 11 compile-time algorithms exist.

## Testing and acceptance

### DSP and state tests

- Assert the exact edges and carriers of all 11 algorithms.
- Assert all 15 ratio zones, descending order, edge thresholds, and canonical strings.
- Assert Fixed mode remains continuous and covers approximately 1 Hz–10 kHz.
- Assert the per-operator Fine control affects both frequency modes correctly.
- Assert Output scales every configured destination of an operator.
- Preserve Warp landmarks and Fold Type behaviour.
- Assert External PM affects every carrier and no non-carrier directly.
- Assert positive, negative, and inverted External PM scaling.
- Assert carriers are summed raw and Master is the only automatic post-mix gain stage.
- Assert the Over threshold, any-lane polyphonic trigger, and peak hold.
- Assert defaults produce one unmodulated sine carrier.
- Assert invalid restored indices and non-finite values cannot cause out-of-bounds access or non-finite audio.
- Preserve polyphonic lane isolation and channel-broadcast behaviour.

### Host and visual tests

- Build and run the native VCV test suite.
- Build the VCV plugin and load FourV2 in VCV Rack.
- Inspect the complete panel at actual 32 HP scale.
- Exercise every parameter, input, output, light, and dynamic display.
- Verify the worst-case routing diagrams and algorithm 8's shared output rail.
- Verify ratio and Fixed-mode readouts at their smallest rendered size.
- Verify logo glyph alignment, colour boundary, and exact underline extents.
- Evaluate External PM scaling by ear with one and multiple carriers.
- Check the visible `FourV2` title treatment and increase panel width only if the 32 HP acceptance criteria cannot be met.
- Run an SDK compatibility build or equivalent available MetaModule-facing verification without modifying `wintoid-metamodule`.

## Release gates

Before FourV2's first compatibility-bearing release:

1. Confirm the 32 HP panel at actual size or document the approved increase.
2. Approve the visible `FourV2` title treatment.
3. Approve the real-size vector wintoid mark.
4. Confirm External PM scaling after listening tests.
5. Confirm all native and compatibility-oriented tests pass.
6. Freeze the `FourV2` model slug and parameter ordering for subsequent patch compatibility.
