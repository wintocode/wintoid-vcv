# wintoid

VCV Rack plugin — FM synthesizer and multi-mode filter.

## Modules

### FourMM
4-operator FM/PM synthesizer (26HP)

- **11 algorithms** — classic 4-op routing from serial chain to full parallel
- **Per-operator controls**: Coarse (ratio or fixed Hz), Fine, Level, Warp, Fold, Feedback — each with CV input and attenuverter
- **Warp** — continuous waveshape morph: sine → triangle → saw → pulse (PolyBLEP anti-aliased)
- **Fold** — 3 types per operator (right-click): Symmetric, Asymmetric, Soft clip
- **Frequency modes** — Ratio (0.25:1 to 31.5:1) or Fixed Hz (1–9999 Hz) per operator, toggled via button
- **Global controls**: Algorithm selector, cross-modulation depth (XM), fine tune, VCA
- **External PM input** with attenuverter — for audio-rate phase modulation from other sources
- **V/OCT** input
- **2× internal oversampling** with DC blocking

### VortexMM
12-mode multi-mode filter (6HP)

- **Filter modes**: LP 6/12/24dB, HP 6/12/24dB, BP, BP+, Notch, Notch+, AP, AP+
- **Controls**: Cutoff (20 Hz – 20 kHz), Resonance, Drive — each with CV input and attenuverter
- **Drive stage** — soft-clip saturation before the filter
- **Mode selector** — click display to cycle, right-click for menu
- **Filter DSP** by Yuriy Ivantsov ([ivantsov-filters](https://github.com/yIvantsov/ivantsov-filters)) — state-space design with Sigma frequency warping

## Building

Follow the [VCV Rack Plugin Development Tutorial](https://vcvrack.com/manual/PluginDevelopmentTutorial).

```sh
export RACK_DIR=/path/to/Rack-SDK
make install
```

## License

[MIT](LICENSE)
