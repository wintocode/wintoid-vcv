# MetaModule compatibility boundary

This document records the rules that keep the shared module sources safe
for the external 4ms MetaModule wrapper (`../wintoid-metamodule`) without
this repository depending on it.  The rules below originated as caveats in
the 2026-08-27 GUI audit; they are release acceptance gates, not TODOs.

## How the wrapper consumes this repository

- The sibling wrapper compiles versioned checkouts of the shared C++ sources
  and their included `src/*/layout.h` headers. MetaModule does not render `res/*.svg`:
  its adapter rewrites those asset requests to PNG names and the
  wrapper packages separately converted `assets/*.png` faceplates. When the
  wrapper is intentionally refreshed, regenerate each faceplate at 240 px
  height with the SDK's `SvgToPng.py` (or an equivalent approved conversion)
  so panel artwork and shared coordinates remain from the same wintoid-vcv
  revision.
- The wrapper repository is **read-only from here**.  This repository may add
  compatibility checks and shared-source fixes, but must not modify the
  wrapper, its assets, metadata, or build files.  A temporary throwaway copy
  under `/tmp` may be patched solely to prove compilation and linking.
- **Missing Brink in the stale wrapper is not a defect in this repository**
  and must not be "fixed" here.  It resolves when the wrapper is refreshed.

## Shared-source rules

- Shared code stays **C++11** and uses only fonts present in the MetaModule
  SDK component library — currently `res/fonts/DejaVuSans.ttf`.  Custom
  displays must load and select the font explicitly and skip text (keeping
  the background) if loading fails; never inherit another widget's font.
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

## Hardware-only acceptance gates

These cannot be closed against the stale wrapper and must remain explicit
release checks for whoever updates it:

- Confirm the wrapper's Four, Vortex, and Brink PNG faceplates were regenerated
  from the same wintoid-vcv revision as the compiled C++ and generated headers.
- Four/Vortex/Brink render with acceptable dynamic-buffer memory and
  refresh cost on actual MetaModule hardware, individually and in one
  patch.  Full-panel `PanelLabels` widgets allocate dynamic buffers sized
  to their boxes, so measure on hardware before attempting any
  optimization.
- Algo/Mode/Fold parameters can be mapped and changed through MetaModule
  controls.  Custom click behaviour in Rack does not substitute for
  verifying parameter mapping on MetaModule hardware.
- The SDK's four-lane exposure behaves safely while shared VCV builds
  retain 16 lanes.
