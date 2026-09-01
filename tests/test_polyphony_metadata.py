#!/usr/bin/env python3

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
manifest = json.loads((ROOT / "plugin.json").read_text(encoding="utf-8"))
modules = {module["name"]: module for module in manifest["modules"]}

for name in ("Four", "Vortex", "VortexV2", "FourV2", "BrinkV2"):
    assert name in modules, f"missing {name} manifest entry"
    assert "Polyphonic" in modules[name]["tags"]
    assert "16-channel polyphonic" in modules[name]["description"].lower()

for name in ("Four", "Vortex", "Brink"):
    assert modules[name].get("hidden") is True, f"legacy {name} must be hidden"
for name in ("FourV2", "VortexV2", "BrinkV2"):
    assert modules[name].get("hidden", False) is False, f"{name} must remain visible"
assert manifest["version"] == "2.3.1"

readme = (ROOT / "README.md").read_text(encoding="utf-8")
assert "voice count follows the **V/OCT** input" in readme
assert "voice count follows **AUDIO IN**" in readme
assert "shorter polyphonic modulation inputs broadcast lane 0" in readme
assert "shorter polyphonic CV inputs broadcast lane 0" in readme
vortex_v2_readme = readme[
    readme.index("### VortexV2"):readme.index("### Brink")
]
assert "voice count follows **AUDIO IN**" in vortex_v2_readme
assert "shorter polyphonic CV inputs broadcast lane 0" in vortex_v2_readme
assert "### BrinkV2" in readme
brink_v2_readme = readme[
    readme.index("### BrinkV2"):readme.index("## Building")
]
assert "V2 SEM panel (16HP)" in brink_v2_readme
assert "16-channel" in brink_v2_readme
assert "up to 16 lanes" in brink_v2_readme
assert "broadcast lane 0" in brink_v2_readme

four_source = (ROOT / "src/Four/Four.cpp").read_text(encoding="utf-8")
vortex_source = (ROOT / "src/Vortex/Vortex.cpp").read_text(encoding="utf-8")
vortex_v2_source = (
    ROOT / "src/VortexV2/VortexV2.cpp"
).read_text(encoding="utf-8")
four_v2_source = (ROOT / "src/FourV2/FourV2.cpp").read_text(encoding="utf-8")
brink_v2_source = (ROOT / "src/BrinkV2/BrinkV2.cpp").read_text(encoding="utf-8")
assert "V/OCT (polyphonic voice count, 1 to 16 channels)" in four_source
assert "Audio (polyphonic voice count, 1 to 16 channels)" in vortex_source
assert "Audio (polyphonic voice count, 1 to 16 channels)" in vortex_v2_source
assert "V/OCT (polyphonic voice count, 1 to 16 channels)" in four_v2_source
for contract in (
    "brink::MAX_CHANNELS",
    "brink::effective_channels",
    "brink::logic_channels",
    ".setChannels(signalChannels[channel])",
    ".setChannels(logicChannels)",
):
    assert contract in brink_v2_source

print("polyphony metadata tests passed")
