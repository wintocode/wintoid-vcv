# wintoid development notes

## Brand spelling

Always write the project and product name as `wintoid` in lowercase, including
user-facing text and documentation. Internal identifiers and file names may
retain their existing casing when required for compatibility.

## Installing changes into VCV Rack

After any plugin source, panel, or `res/` asset change, building the local
`plugin.dylib` is not enough for VCV Rack to see it. Run the install target
from the repository root after the build:

```sh
make install
```

Then fully quit and relaunch VCV Rack. Rack does not hot-reload an already
loaded plugin dylib. If the UI still looks stale after relaunch, verify that
the current `wintoid-<version>-mac-arm64.vcvplugin` package was placed in
`/Users/simon/Library/Application Support/Rack2/plugins-mac-arm64` and that
Rack has unpacked it over the installed `wintoid` plugin.

Before declaring a module-browser check successful, verify Rack's local module
whitelist. A whitelist entry overrides the plugin manifest for browser
visibility: the visible V2 models must be listed, while the deprecated V1
models remain hidden by their manifest metadata. Run this after the fresh Rack
launch has completed its Library sync and settings save:

```sh
wintoid_rack_settings="/Users/simon/Library/Application Support/Rack2/settings.json"
jq -e '
  .moduleWhitelist.wintoid as $w
  | if $w == true then true
    elif ($w | type) == "array" then
      ["BrinkV2", "FourV2", "VortexV2"]
      | all(.[]; . as $slug | ($w | index($slug)) != null)
    else false end
' "$wintoid_rack_settings"
```

The command must exit successfully. If it fails after Library sync, do not
keep editing the normal user's settings while automatic Library sync is
enabled: Rack can overwrite that file from the server on the next launch. If
the new models are not yet registered in the VCV Library, the normal-mode
local recovery is to back up the settings, pause automatic Library sync, add
the visible V2 slugs, and relaunch Rack normally:

```sh
wintoid_rack_settings="/Users/simon/Library/Application Support/Rack2/settings.json"
wintoid_rack_backup="/private/tmp/wintoid-rack-settings-before-local-fix.json"
wintoid_rack_temp="/private/tmp/wintoid-rack-settings-local-fix.json"
cp -p "$wintoid_rack_settings" "$wintoid_rack_backup"
jq '
  .autoCheckUpdates = false
  | .moduleWhitelist.wintoid =
      (((.moduleWhitelist.wintoid // [])
        + ["BrinkV2", "FourV2", "VortexV2"]) | unique)
' "$wintoid_rack_settings" > "$wintoid_rack_temp"
mv "$wintoid_rack_temp" "$wintoid_rack_settings"
```

This is a local workaround for stale server metadata, not a replacement for
updating the VCV Library registration. Re-enable `autoCheckUpdates` only after
the Library metadata includes all three V2 models, then rerun the whitelist
check after a fresh Rack launch. If maintaining the normal profile is not
appropriate, use an isolated Rack development profile for local browser
verification instead:

```sh
wintoid_rack_dev_dir="$(mktemp -d /private/tmp/wintoid-rack-dev.XXXXXX)"
mkdir -p "$wintoid_rack_dev_dir/plugins"
cp dist/wintoid-*.vcvplugin "$wintoid_rack_dev_dir/plugins/"
(
  cd "$wintoid_rack_dev_dir" || exit 1
  "/Applications/VCV Rack 2 Pro.app/Contents/MacOS/Rack" \
    --dev \
    --system "/Applications/VCV Rack 2 Pro.app/Contents/Resources" \
    --user "$wintoid_rack_dev_dir"
)
```

Development mode disables Library sync and loads packages from the profile's
`plugins` directory. Verify that the browser shows `FourV2`, `VortexV2`, and
`BrinkV2`; an absent `.moduleWhitelist.wintoid` entry in this isolated profile
is expected. Treat a post-sync failure in the normal profile as a
browser-discovery/library-metadata failure rather than a successful build.
