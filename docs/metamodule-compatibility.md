# MetaModule compatibility boundary

This document records the rules that keep the shared module sources safe
for the external 4ms MetaModule wrapper (`../wintoid-metamodule`) without
this repository depending on it. It is a source and release handoff document:
the current consumer package contains the original modules only, and this
document does not claim that the V2 modules are already available on
MetaModule.

## How the wrapper consumes this repository

- The sibling wrapper compiles versioned checkouts of the shared C++ sources
  and their included `src/*/layout.h` headers. MetaModule does not render `res/*.svg`:
  its adapter rewrites those asset requests to PNG names and the
  wrapper packages separately converted `assets/*.png` faceplates. When the
  wrapper is intentionally refreshed, regenerate each faceplate at 240 px
  height with the SDK's `SvgToPng.py` (or an equivalent approved conversion)
  so panel artwork and shared coordinates remain from the same wintoid-vcv
  revision.
- The current sibling checkout includes Four, Vortex, and Brink V1 only. It
  does not yet register, build, or package FourV2, VortexV2, or BrinkV2; that
  is the separate consumer-refresh task described below.
- The wrapper repository is **read-only from here**.  This repository may add
  compatibility checks and shared-source fixes, but must not modify the
  wrapper, its assets, metadata, or build files.  A temporary throwaway copy
  under `/tmp` may be patched solely to prove compilation and linking.

## Shared-source rules

- Host-independent shared DSP and layout code stays **C++11-compatible** and
  uses only fonts present in the MetaModule SDK component library — currently
  `res/fonts/DejaVuSans.ttf`. The VCV Rack build uses C++11; the current
  MetaModule SDK Rack interface requires a C++20 consumer build. That SDK
  requirement applies to the consumer build environment, not to the portable
  shared helpers. Custom displays must load and select the font explicitly and
  skip text (keeping the background) if loading fails; never inherit another
  widget's font.
- Custom graphics draw on **layer 1** and **within their `box`**:
  MetaModule clips other layers and out-of-box pixels.  Stroked box-edge
  primitives therefore inset or clamp via `src/ui_geometry.h`
  (`stroke_inset`, `inset_extent`, `clamp_stroke_center`), which is
  Rack-independent and unit-tested on the host.
- GUI animation must not assume a stable or high frame rate.  Brink's
  display snapshot is rate-limited to 60 Hz on the audio thread and read
  opportunistically by the widget.
- **VCV remains 16-channel.**  The current MetaModule adapter exposes at
  most four lanes; that is a consumer constraint, not a reason to reduce
  shared VCV behaviour.

## Current V2 status

The V2 Rack-facing translation units and plugin registration currently pass an
ARM syntax check against the MetaModule SDK's Rack interface. The host DSP,
polyphony, panel, and source-level graphics checks also pass. These checks
establish a favourable source/API boundary; they do not close the consumer's
full link, package, or hardware gates.

The current sibling package therefore remains V1-only. The V2 modules cannot
be loaded as MetaModule modules until the consumer is refreshed.

## FourV2 handoff

The future MetaModule consumer must treat FourV2's generated panel and shared
layout as a matched revision boundary:

- Package a `FourV2` PNG faceplate generated from the same revision of
  wintoid-vcv as `src/FourV2/layout.h`. MetaModule consumes the PNG rather than the Rack
  SVG; regenerate it at 240 px height with the SDK's `SvgToPng.py` (or an
  approved equivalent) whenever the panel or layout changes. Do not mix a PNG
  from one revision with `layout.h` from another.
- Convert and package the five generated `FourV2` state-switch frames as PNG
  component assets from that same revision:
  `FourV2FrequencyMode_Ratio.svg`, `FourV2FrequencyMode_Fixed.svg`,
  `FourV2FoldType_Symmetric.svg`, `FourV2FoldType_Asymmetric.svg`, and
  `FourV2FoldType_SoftClip.svg`.
- Map Algorithm, each Frequency Mode, and each Fold Type as ordinary static parameters;
  these are host-mappable switches. Ratio selection remains the module's deterministic
  quantisation of a continuous parameter; the consumer must not depend on
  runtime changes to Rack parameter snapping.
- Register both `AlgorithmRoutingDisplay` and
  `OperatorFrequencyDisplay` through supported SDK display facilities. These
  displays are read-only informational graphics; no parameter write, click,
  drag, menu, or VCV-only callback is part of the FourV2 interaction contract.
- Build the shared FourV2 code against the supported SDK surface, keeping its
  host-independent logic C++11-compatible and compiling the consumer with the
  SDK-required C++20 standard. Use the available `DejaVuSans.ttf` font. Verify
  the dynamic-display memory and refresh cost on actual MetaModule hardware,
  including the routing display and four frequency displays, before release.
- This remains a read-only sibling boundary: `../wintoid-metamodule` and its
  PNG assets, metadata, and build files must not be edited from this project.
  If a compatibility probe needs changes, copy the consumer to `/tmp` first.

## VortexV2 handoff

VortexV2 has no filter-mode parameter and no interactive mode display. A future
MetaModule consumer must compile `src/VortexV2/VortexV2.cpp` with the
`src/VortexV2/layout.h` generated in the same wintoid-vcv revision, then
convert that revision's `res/VortexV2.svg` to a matched 240 px `VortexV2.png`
faceplate. The SVG and generated header must not be mixed across revisions.

The Cutoff, Resonance, Drive, and CV controls are ordinary parameters and the
twelve outputs are ordinary ports in their fixed panel order: LP 6/12/24dB,
HP 6/12/24dB, BP, BP+, NOTCH, NOTCH+, AP, AP+. There are no V2 switch-frame
assets. `VortexV2PanelLabels` is read-only layer-1 drawing using the supported
DejaVu Sans font and bounded geometry helpers; the consumer must preserve or
replace it with an equivalent supported display treatment.

The VCV module keeps its 16-channel input/output behaviour. A consumer may
expose at most four lanes, but must preserve mono and shorter-CV lane-0
broadcasting, connected-output channel counts, and reset/skip behaviour for
disconnected filter branches.

## BrinkV2 handoff

BrinkV2's generated faceplate and header form a matched revision boundary. A
future MetaModule consumer must compile `src/BrinkV2/BrinkV2.cpp` with the
`src/BrinkV2/layout.h` generated in the same wintoid-vcv revision, then convert
that revision's `res/BrinkV2.svg` to a matched 240 px `BrinkV2.png` faceplate.
The SVG and generated header must not be mixed across revisions.

BrinkV2 uses standard Rack controls and ports. Its two custom widgets are
read-only, draw static or snapshot-driven graphics on layer 1, keep their
graphics bounded to their widget boxes with the shared geometry helpers, and
explicitly select `res/fonts/DejaVuSans.ttf`; they do not add click, drag,
menu, or parameter-write behaviour. VCV's module retains all 16 channels even
if a future consumer exposes only four lanes.

The current sibling wrapper lists Brink V1 only. Separate BrinkV2 registration,
PNG assets, package/build metadata, parameter mapping, four-lane exposure, and
hardware rendering/resource validation are a later MetaModule project, not
work performed by this VCV integration.

## Remaining MetaModule acceptance gates

These remain open for whoever refreshes the consumer, whether the refresh
includes only the V2 modules or the existing V1 modules as well:

- Confirm every packaged faceplate and FourV2 switch-frame PNG was generated
  from the same wintoid-vcv revision as its compiled C++ and generated header.
- Run the full ARM compile, link, symbol check, and package build for every
  registered V2 module; a syntax-only source check is not sufficient.
- Map all ordinary controls and ports, including FourV2's Algorithm, Frequency
  Mode, and Fold Type selectors, without depending on Rack-only menu or click
  behaviour.
- Verify the FourV2 routing/frequency displays and the BrinkV2/VortexV2 label
  or rail drawing have acceptable memory and refresh cost on actual hardware.
  Full-panel label widgets allocate dynamic buffers sized to their boxes, so
  measure before attempting any optimisation.
- Verify four-lane exposure, mono and shorter-CV broadcasting, output channel
  counts, reset behaviour, and disconnected-branch handling while the shared
  VCV implementations retain 16 lanes.
- Render every module individually and together in a representative patch on
  actual MetaModule hardware.
