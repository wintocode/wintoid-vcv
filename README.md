# wintoid

VCV Rack plugin — synthesizer, filter, and CV/logic utilities.

## Modules

### Four
4-operator FM/PM synthesizer (26HP)

> **Legacy compatibility module.** Four is hidden from the Module Browser for
> new patches but remains bundled and registered so existing patches continue
> to load. Use FourV2 for new patches.

- **11 algorithms** — classic 4-op routing from serial chain to full parallel
- **Per-operator controls**: Coarse (ratio or fixed Hz), Fine, Level, Warp, Fold, Feedback — each with CV input and attenuverter
- **Warp** — continuous waveshape morph: sine → triangle → saw → pulse (PolyBLEP anti-aliased)
- **Fold** — 3 types per operator (right-click): Symmetric, Asymmetric, Soft clip
- **Frequency modes** — Ratio (0.25:1 to 31.5:1) or Fixed Hz (1–9999 Hz) per operator, toggled via button
- **Global controls**: Algorithm selector, cross-modulation depth (XM), fine tune, VCA
- **External PM input** with attenuator — for audio-rate phase modulation from other sources
- **16-channel polyphony** — voice count follows the **V/OCT** input; mono and shorter polyphonic modulation inputs broadcast lane 0
- **2× internal oversampling** with DC blocking

### FourV2
Independent 4-operator phase-modulation oscillator (32HP)

FourV2 is a separate module from Four. It uses 16 fixed topology slots, but its
controls and signal flow are documented independently.

#### Routing and frequency

- **16 algorithms** — the fixed routing choices are:

The Algorithm control is numbered 1–16, matching the topology number below.

| Algorithm | Phase-modulation routing | Carriers in the output mix |
| ---: | --- | --- |
| 1 | `4 → 3 → 2 → 1` | 1 |
| 2 | `(3 + 4) → 2 → 1` | 1 |
| 3 | `4 → 2 → 1` and `3 → 1` | 1 |
| 4 | `4 → 3` and `2 → 1` | 1 and 3 |
| 5 | `4 → (1, 2, 3)` | 1, 2, and 3 |
| 6 | `4 → 3`, plus independent 2 and 1 | 1, 2, and 3 |
| 7 | no internal modulation | 1, 2, 3, and 4 |
| 8 | `4 → 3 → (1, 2)` | 1 and 2 |
| 9 | `(3 + 4) → (1, 2)` | 1 and 2 |
| 10 | `(2 + 3 + 4) → 1` | 1 |
| 11 | `4 → 3 → 1`, plus independent 2 | 1 and 2 |
| 12 | `(3 + 4) → 1`, plus independent 2 | 1 and 2 |
| 13 | `4 → (1, 2)`, plus independent 3 | 1, 2, and 3 |
| 14 | `4 → 2 → 1` and `4 → 3` | 1 and 3 |
| 15 | `4 → (1, 2)` and `3 → 1` | 1 and 2 |
| 16 | `4 → (2, 3) → 1` | 1 |

- **15 curated harmonic ratios** — Ratio mode selects equally sized zones in
  this exact order: `4:1`, `3:1`, `2:1`, `3:2`, `4:3`, `1:1`, `3:4`, `2:3`, `1:2`, `1:3`, `1:4`, `1:5`, `1:6`, `1:7`, `1:8`. The values are reduced canonical ratios, so `1:4` is used instead of `0.25:1`.
- **Ratio mode** quantises the continuous Coarse control to the nearest one of
  those 15 zones. `1:1` is the sixth selection; it is not moved to the knob's
  geometric centre.
- **Fixed mode** uses the same Coarse control continuously and maps it
  exponentially from approximately 1 Hz to 10 kHz. It is not quantised to the
  ratio zones. Fine tuning is in cents and applies in both modes.
- Each operator has a read-only frequency display showing its selected ratio or
  fixed frequency; the routing display is also informational.

#### Output, Warp, Fold, and Feedback CV controls

- **OUTPUT** controls every destination of its operator. For a carrier it sets
  audible mix level; for a modulator it sets modulation depth; an operator with
  both roles uses the same level for both. Operators continue running when
  their Output is zero, so restoring a level does not restart their phase.
- **WARP** continuously morphs Sine → Triangle → Saw → Pulse. **FOLD** adds
  wavefolding, with Symmetric, Asymmetric, and Soft Clip types. **FEEDBACK**
  adds self-phase modulation for that operator.
- Each operator keeps the Output, Warp, Fold, and Feedback knob beside its CV
  input and bipolar attenuverter in the same four-row control block. The
  effective control is the knob plus scaled CV, clamped to its documented
  range.

#### PM, output level, and polyphony

- **PM DEPTH** scales the internal operator-to-operator phase modulation after
  each source operator's Output level. Its CV input uses a bipolar attenuverter.
- **External PM affects every carrier** directly in phase, before waveform
  generation, Warp, and Fold. The input is bipolar, and the external phase
  contribution is `input volts × attenuator × 0.1` cycles; the attenuator scales
  it without inverting the input polarity. PM DEPTH and External PM are
  independent controls.
- The **raw carrier sum** is sent through **MASTER**, the only automatic
  post-mix gain control. FourV2 does not divide by carrier count, normalise, or
  soft-clip when carriers are added. One full-scale carrier is approximately
  ±5 V at full Master; four phase-aligned carriers can reach approximately
  ±20 V.
- **OVER** monitors the post-Master voltage on every lane. It lights at or above
  ±10 V and holds for approximately 250 ms; it never clips, limits, compresses,
  or changes the audio.
- **16-channel polyphony** — voice count follows the **V/OCT** input, with one
  lane when it is unpatched and up to 16 lanes when it is polyphonic. Mono and
  shorter polyphonic CV inputs broadcast lane 0 to the additional lanes; this
  applies to PM Depth, External PM, and every operator CV row.

#### Patch compatibility

Four remains the original V1 module with its existing model, parameters, signal
behaviour, and patch compatibility. Existing Four patches continue to load as
Four; FourV2 is an independent model and does not replace or reinterpret them.

### Vortex
12-mode multi-mode filter (6HP)

> **Legacy compatibility module.** Vortex is hidden from the Module Browser for
> new patches but remains bundled and registered so existing patches continue
> to load. Use VortexV2 for new patches.

- **Controls**: Cutoff (20 Hz – 20 kHz), Resonance, Drive — each with CV input and attenuverter
- **16-channel polyphony** — voice count follows **AUDIO IN**; mono and shorter polyphonic CV inputs broadcast lane 0
- **Filter modes**: LP 6/12/24dB, HP 6/12/24dB, BP, BP+, Notch, Notch+, AP, AP+
- **Drive stage** — soft-clip saturation before the filter
- **Mode selector** — click display to cycle, right-click for menu
- **Filter DSP** by Yuriy Ivantsov ([ivantsov-filters](https://github.com/yIvantsov/ivantsov-filters)) — state-space design with Sigma frequency warping

### VortexV2
Independent twelve-output multi-mode filter (12HP)

- **Controls**: Cutoff (20 Hz – 20 kHz), Resonance, Drive — each with CV input and attenuverter
- **Outputs**: LP 6/12/24dB, HP 6/12/24dB, BP, BP+, Notch, Notch+, AP, AP+ — in that order
- **Simultaneous outputs** — all twelve outputs can be used at the same time
- **Disconnected outputs** — reset and skip their individual filter branch
- **Drive stage** — soft-clip saturation before the filters, bounded at ±3 (V1's curve is intentionally unbounded)
- **16-channel polyphony** — voice count follows **AUDIO IN**; mono and shorter polyphonic CV inputs broadcast lane 0

### Brink
Dual voltage-window processor (12HP)

> **Legacy compatibility module.** Brink is hidden from the Module Browser for
> new patches but remains bundled and registered so existing patches continue
> to load. Use BrinkV2 for new patches.

Brink compares two signals with independently movable voltage windows. Each
channel reports whether its signal is inside or outside its window, produces a
continuous position voltage, and emits separate triggers for crossings of the
low and high boundaries. A shared section combines the two channel states with
logic and a latched toggle output.

#### Setting the window

`CENTER` sets the midpoint of the window and `WIDTH` sets its **total span**:

```text
low boundary  = center - width / 2
high boundary = center + width / 2
```

For example:

| Desired window | CENTER | WIDTH |
| --- | ---: | ---: |
| -5 V to +5 V | 0 V | 10 V |
| 0 V to +10 V | +5 V | 10 V |
| -1 V to +1 V | 0 V | 2 V |

The `CENTER` knob ranges from -5 V to +5 V. The `WIDTH` knob ranges from 0 V
to 10 V and defaults to 5 V. `CTR CV` and `WID CV` are added through their
bipolar attenuverters, so CV can move the window or change its size. The final
width is kept between 1 mV and 20 V.

#### Channel inputs and outputs

Both Channel A and Channel B provide the same controls and connections:

| Connection | Behaviour |
| --- | --- |
| `SIGNAL` | The voltage compared with the effective window. |
| `CTR CV` | Moves the centre after scaling by its attenuverter. |
| `WID CV` | Changes the total width after scaling by its attenuverter. |
| `INSIDE` | +10 V while the signal is inside the window; otherwise 0 V. |
| `OUTSIDE` | The inverse of `INSIDE`. |
| `POSITION` | -5 V at the low boundary, 0 V at the centre, and +5 V at the high boundary. Values outside the window clamp to -5 V or +5 V. |

The boundary itself counts as inside. A small amount of hysteresis prevents a
noisy signal sitting on a boundary from rapidly changing state.

#### Directional crossing triggers

The four event outputs emit +10 V, 1 ms pulses:

| Panel label | Trigger condition |
| --- | --- |
| `LOW ↑` | The signal crosses the low boundary upward, entering the window from below. |
| `HIGH ↑` | The signal crosses the high boundary upward, leaving the window above. |
| `LOW ↓` | The signal crosses the low boundary downward, leaving the window below. |
| `HIGH ↓` | The signal crosses the high boundary downward, entering the window from above. |

Crossings are measured relative to the moving window. Modulating `CENTER` or
`WIDTH` across a stationary signal can therefore produce the same events as
moving the signal. A jump across the complete window fires both boundaries in
travel order. Brink establishes the initial state silently, without producing
a trigger when the module starts or resets.

#### A-to-B normalisation

When the corresponding Channel B input is unpatched:

- Channel A `SIGNAL` feeds Channel B `SIGNAL`.
- Channel A `CTR CV` feeds Channel B `CTR CV`.
- Channel A `WID CV` feeds Channel B `WID CV`.

Each normalled connection is broken independently by patching its Channel B
input. Knob and attenuverter settings remain independent, making it easy to
compare one signal with two different windows. Polyphonic cables are copied in
full.

#### Shared logic and TOGGLE

The bottom row operates on the two `INSIDE` states and is deliberately outside
the Channel A and Channel B sections. High outputs are +10 V and low outputs
are 0 V.

| Output | High when… |
| --- | --- |
| `AND` | A and B are both inside. |
| `OR` | At least one channel is inside. |
| `XOR` | Exactly one channel is inside. |
| `TOGGLE` | A stored state is high; it flips each time `XOR` changes from low to high. |

In other words, `TOGGLE` changes state whenever A and B go from matching
(both inside or both outside) to disagreeing. It then remembers that 0 V or
10 V state until the next XOR rising edge. The stored state starts low after
module creation or reset and is not saved in a patch.

#### Window visualisers

The slim rail between each channel's `SIGNAL` and `POSITION` sockets shows the
first polyphonic lane over a fixed -10 V to +10 V scale:

- The translucent channel-coloured band is the current window.
- Channel-coloured lines mark its low boundary, centre, and high boundary.
- The short pale line is the current signal voltage.
- The top, middle, and bottom ticks represent +10 V, 0 V, and -10 V.

Values beyond the display range stop at an endpoint; this only clips the
visualisation, not the signal processing.

#### Polyphony

Brink processes up to 16 lanes independently. Each channel's output count
follows its effective `SIGNAL` input. Mono or shorter `CTR CV` and `WID CV`
inputs broadcast lane 0 to the additional signal lanes. The shared logic uses
the larger channel count of A and B, broadcasting lane 0 from the shorter side
where necessary. The visualisers show lane 0, while the socket lights indicate
activity on any lane.

#### Patch ideas

- **Window gate:** Patch an LFO, envelope, or random source to `SIGNAL`; use
  `INSIDE` to gate events only while it occupies a chosen voltage range.
- **Four-way crossing detector:** Use the four arrow outputs to distinguish
  which boundary was crossed and in which direction.
- **Two-window classifier:** Leave Channel B's `SIGNAL` unpatched, set two
  different windows for the same source, and use the shared logic outputs to
  combine their classifications.
- **Alternating difference state:** Compare two signals and use `TOGGLE` as a
  latched 0/10 V state that alternates each time their inside/outside states
  begin to differ.

### BrinkV2
Behaviour-identical dual voltage-window processor with a V2 SEM panel (16HP)

BrinkV2 is a separate model with the same controls, A-to-B normalisation,
inside/outside gates, bipolar position output, directional boundary events,
shared logic, reset behaviour, and 16-channel polyphonic processing as Brink.
Each channel processes up to 16 lanes, with mono or shorter centre/width CVs
broadcast lane 0 to additional signal lanes.

The V2 SEM faceplate uses uniform socket treatment: every input and output has
the same jack styling. Brink remains the original V1 model for existing patch
compatibility; BrinkV2 does not replace or reinterpret Brink patches.

## MetaModule compatibility

The current sibling MetaModule package is an unreleased V1-only prototype. The
planned MetaModule release will contain FourV2, VortexV2, and BrinkV2 only;
those models remain separate VCV Rack modules in this repository. Their shared
code follows the MetaModule compatibility boundary, but consumer registration,
PNG assets, packaging, and hardware validation remain future work. See
[docs/metamodule-compatibility.md](docs/metamodule-compatibility.md) for the
handoff requirements.

## Building

Follow the [VCV Rack Plugin Development Tutorial](https://vcvrack.com/manual/PluginDevelopmentTutorial).

```sh
export RACK_DIR=/path/to/Rack-SDK
make install
```

## License

[MIT](LICENSE)
