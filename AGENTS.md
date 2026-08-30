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
the current `wintoid-2.2.1-mac-arm64.vcvplugin` package was placed in
`/Users/simon/Library/Application Support/Rack2/plugins-mac-arm64` and that
Rack has unpacked it over the installed `wintoid` plugin.
