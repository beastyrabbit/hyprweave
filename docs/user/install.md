# Installing hyprweave

## Requirements

- Hyprland 0.57.0 with the Lua config (`hyprland.lua`). The API is Lua only.
- x86_64. Function hooks do not work on other architectures.
- For hyprpm: `cpio`, `cmake`, `git`, `meson` and `gcc` (hyprpm builds Hyprland's headers with them).

hyprweave is alpha software. It has been tested on Arch Linux with Hyprland 0.57.0.

## With hyprpm

```sh
hyprpm update                                         # fetches the headers for your Hyprland; run once
hyprpm add https://github.com/beastyrabbit/hyprweave  # clones and builds the plugin
hyprpm enable hyprweave
hyprpm reload                                         # loads it now
```

To load it on every login, add this to `hyprland.lua`:

```lua
hl.on("hyprland.start", function()
    hl.dispatch(hl.dsp.exec_cmd("hyprpm reload -n"))
end)
```

Hyprland reads the config again after a plugin loads, so settings for hyprweave can sit in the same file. Guard them so the config stays valid while the plugin is not loaded:

```lua
if hl.plugin.hyprweave then
    hl.config({ plugin = { hyprweave = { frame_size = 2 } } })
end
```

### After a Hyprland update

Plugins must be built against the exact Hyprland they run in. After every Hyprland update:

```sh
hyprpm update
```

This rebuilds the headers and every enabled plugin. Until then hyprweave refuses to load and shows a notification saying it needs a rebuild.

### Removing it

```sh
hyprpm disable hyprweave
hyprpm remove hyprweave
```

Unloading turns every hyprweave group back into a normal tab group; no window is closed.

## From source

```sh
git clone https://github.com/beastyrabbit/hyprweave
cd hyprweave
make
hyprctl plugin load "$PWD/hyprweave.so"
```

Building needs `g++`, `make`, `pkg-config` and Hyprland's headers (`pkg-config --modversion hyprland` must print your Hyprland's version). To load it from the config instead, use an absolute path:

```lua
hl.plugin.load("/absolute/path/to/hyprweave.so")
```

## Next

[lua-api.md](lua-api.md) shows how to create groups and lists every setting.
