#!/usr/bin/env python3

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
manifest = json.loads((ROOT / "plugin.json").read_text(encoding="utf-8"))
modules = {module["name"]: module for module in manifest["modules"]}

assert manifest["version"] == "2.2.1"
for name in ("Four", "Vortex"):
    assert "Polyphonic" in modules[name]["tags"]
    assert "16-channel polyphonic" in modules[name]["description"].lower()

readme = (ROOT / "README.md").read_text(encoding="utf-8")
assert "voice count follows the **V/OCT** input" in readme
assert "voice count follows **AUDIO IN**" in readme
assert "shorter polyphonic modulation inputs broadcast lane 0" in readme
assert "shorter polyphonic CV inputs broadcast lane 0" in readme

four_source = (ROOT / "src/Four/Four.cpp").read_text(encoding="utf-8")
vortex_source = (ROOT / "src/Vortex/Vortex.cpp").read_text(encoding="utf-8")
assert "V/OCT (polyphonic voice count, 1 to 16 channels)" in four_source
assert "Audio (polyphonic voice count, 1 to 16 channels)" in vortex_source

print("polyphony metadata tests passed")
