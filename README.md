# wintoid

VCV Rack plugin — synthesizer, filter, and CV/logic utilities.

## Modules

### Four
4-operator FM/PM synthesizer (26HP)

- **11 algorithms** — classic 4-op routing from serial chain to full parallel
- **Per-operator controls**: Coarse (ratio or fixed Hz), Fine, Level, Warp, Fold, Feedback — each with CV input and attenuverter
- **Warp** — continuous waveshape morph: sine → triangle → saw → pulse (PolyBLEP anti-aliased)
- **Fold** — 3 types per operator (right-click): Symmetric, Asymmetric, Soft clip
- **Frequency modes** — Ratio (0.25:1 to 31.5:1) or Fixed Hz (1–9999 Hz) per operator, toggled via button
- **Global controls**: Algorithm selector, cross-modulation depth (XM), fine tune, VCA
- **External PM input** with attenuverter — for audio-rate phase modulation from other sources
- **16-channel polyphony** — voice count follows the **V/OCT** input; mono and shorter polyphonic modulation inputs broadcast lane 0
- **2× internal oversampling** with DC blocking

### Vortex
12-mode multi-mode filter (6HP)

- **Controls**: Cutoff (20 Hz – 20 kHz), Resonance, Drive — each with CV input and attenuverter
- **16-channel polyphony** — voice count follows **AUDIO IN**; mono and shorter polyphonic CV inputs broadcast lane 0
- **Filter modes**: LP 6/12/24dB, HP 6/12/24dB, BP, BP+, Notch, Notch+, AP, AP+
- **Drive stage** — soft-clip saturation before the filter
- **Mode selector** — click display to cycle, right-click for menu
- **Filter DSP** by Yuriy Ivantsov ([ivantsov-filters](https://github.com/yIvantsov/ivantsov-filters)) — state-space design with Sigma frequency warping

### Brink
Dual voltage-window processor (12HP)

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

## Building

Follow the [VCV Rack Plugin Development Tutorial](https://vcvrack.com/manual/PluginDevelopmentTutorial).

```sh
export RACK_DIR=/path/to/Rack-SDK
make install
```

## License

[MIT](LICENSE)
