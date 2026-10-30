# Build, load and test

## Build environment

- Developed on Arch Linux (x86_64) with Hyprland 0.57.0 from the Arch package `hyprland 0.57.0-1`. Headers live in `/usr/include/hyprland/src/...` and `pkg-config --modversion hyprland` prints `0.57.0`.
- GCC 16.2.1 (`g++`, through ccache). Build with the same compiler family as Hyprland, because the API passes C++ objects and has no ABI guarantee.
- A plain Makefile is enough; hyprweave needs no `cmake` or `meson`. `hyprpm` only matters for installing (see [hyprpm](#hyprpm)).
- Function hooks only work on x86_64.

## Compile

The official plugins build with one `g++` call. This line compiles against the 0.57 headers (checked 2026-10-30 with a throwaway plugin that includes `PluginAPI.hpp`, `Group.hpp`, `WindowGroupTarget.hpp`, `EventBus.hpp` and `<lua.h>`; it took about 2 seconds):

```sh
g++ -O2 -shared -fPIC -std=c++2b --no-gnu-unique main.cpp -o hyprweave.so \
    $(pkg-config --cflags pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon)
```

- `--no-gnu-unique` is GCC only. Without it, unique symbols keep the `.so` mapped after `hyprctl plugin unload`, so a reload would still run the old code. The official Makefiles add it only when `CXX` is `g++`.
- The `pkg-config --cflags hyprland` output already includes the Lua headers, so `extern "C" { #include <lua.h> }` works without extra flags.
- `-std=c++2b` matches the official plugins. Hyprland headers use C++23 features.

The repo's `Makefile` follows [`hyprfocus/Makefile`](https://github.com/hyprwm/hyprland-plugins/blob/main/hyprfocus/Makefile). `make` builds `hyprweave.so` from `src/*.cpp`. It compiles to `hyprweave.so.tmp` and renames the result, so the file a running Hyprland has loaded is never rewritten in place. Make's default `CXX` is `g++`, so `--no-gnu-unique` applies with a plain `make`.

## Development loop (`dev/nest`)

A broken plugin takes the whole compositor down, so test in a nested Hyprland. Running `Hyprland` from inside a Hyprland session opens a new window with a second compositor in it. `dev/nest` wraps this and only ever talks to the nest:

```sh
dev/nest start          # from a terminal inside Hyprland; blocks while the nest runs
dev/nest reload         # make, then unload + load hyprweave.so in the nest
dev/nest ctl hyprweave  # any hyprctl command, sent to the nest
dev/nest ctl plugin list
dev/nest ctl clients -j
dev/nest ctl dispatch 'hl.dsp.exec_cmd("kitty")'
dev/nest ctl rollinglog -f
dev/nest unload
dev/nest stop           # closes all windows, then exits the nest
```

Checked end to end on 2026-10-30 with the packaged Hyprland 0.57.0: start, load, form a group, rebuild and reload with the group open, unload, stop. The real session kept `no plugins loaded` and its systemd environment throughout.

How it works:

- `start` writes its PID to `$XDG_RUNTIME_DIR/hyprweave/nest.pid` and `exec`s `Hyprland --config dev/nested.lua`. Every other subcommand looks that PID up in `hyprctl instances -j` and passes the matching signature to `hyprctl -i`. Without a running nest it fails; it never falls back to another instance.
- `start` sets `HYPRLAND_NO_SD_VARS=1` and `HYPRLAND_NO_SD_TARGET=1`. Hyprland already skips the systemd and D-Bus environment import when it has no seat session, which is the case when nested. The variables guard against that changing, because an import would point the real session's portals and services at the nest.
- `dev/nested.lua` has no autostart, uses `ALT` instead of `SUPER`, and binds `ALT + Return` (kitty), `ALT + Q` (close), `ALT + W` (group toggle), `ALT + Tab` / `ALT + SHIFT + Tab` (next and previous group member), `ALT` + arrows (focus) and `ALT + SHIFT + E` (exit). `Hyprland --verify-config --config dev/nested.lua` checks it without starting a compositor.
- The nest's output is a window of the outer session (`WAYLAND-1` inside the nest). `WAYLAND_DISPLAY=<nest socket> grim out.png` screenshots it; the socket is the `wl_socket` field of `hyprctl instances -j`.
- To debug a crash, start the nest by hand under gdb: `gdb --args Hyprland --config dev/nested.lua`, then `bt` after the crash. For a crash that already happened, `coredumpctl list` and `coredumpctl info <pid>` show the backtrace.

### Hyprland 0.57.0 crashes on exit while a group exists

Exiting Hyprland 0.57.0 with a group still open segfaults during cleanup: `CCompositor::cleanup` destroys the windows, the last member's destructor destroys the `CGroup`, and `~CGroup` calls `CWindowGroupTarget::assignToSpace(nullptr)`, whose `onUpdateSpace` calls `windowTarget()` on members that are already gone. It reproduces without hyprweave ever being loaded, and it does not happen without a group. Upstream `main` had not changed that code as of 2026-10-30. `dev/nest stop` closes every window first to avoid it. If a nest exits with code 139, check `coredumpctl info` before blaming the plugin.

## Talking to an instance by hand

Every running compositor is an instance. `hyprctl instances` lists them. Without a flag, `hyprctl` talks to the instance in `$HYPRLAND_INSTANCE_SIGNATURE`, which in a normal terminal is the user's real session. Outside `dev/nest`, always pass `-i <index or signature>`. A plugin command without `-i` loads the plugin into the user's live session.

`hyprctl eval '<lua>'` runs a Lua string without printing a result. `hyprctl repl '<lua>'` runs one and prints the result; `hyprctl repl` alone is an interactive Lua prompt. Dispatchers take Lua: `hyprctl dispatch 'hl.dsp.group.toggle()'`.

Plugin paths must be absolute. A reload is `unload` followed by `load`. Logs live in `$XDG_RUNTIME_DIR/hypr/<signature>/hyprland.log`. After the config loads, Hyprland stops logging to stdout (`debug.enable_stdout_logs = 0`), so follow the log with `rollinglog -f`.

`PLUGIN_EXIT` runs only on a user unload. If Hyprland ejects the plugin because of a fault, `PLUGIN_EXIT` does not run.

## Permissions

If the config enables permission management (`hl.permission(...)`), loading a plugin can show a prompt.

## Loading from the Lua config

With the Lua config, a plugin loads with `hl.plugin.load("/abs/path/hyprweave.so")`. Plugin config values are set under `plugin.<name>`:

```lua
hl.plugin.load("/abs/path/hyprweave.so")

if hl.plugin.hyprweave ~= nil then
    hl.config({ plugin = { hyprweave = { some_option = 1 } } })
end
```

The `~= nil` check keeps the config valid when the plugin is not loaded.

## hyprpm

`hyprpm` is how users install hyprweave ([install.md](../../user/install.md)). It clones the repo, reads [`hyprpm.toml`](../../../hyprpm.toml), checks out the commit pinned for the running Hyprland, runs each `build` command from the repo root (`make all`) against the headers it keeps for that Hyprland, and loads `hyprweave.so`.

A commit pin `[Hyprland commit, hyprweave commit]` says which hyprweave commit to build for a given Hyprland. Without a matching pin hyprpm builds the latest commit. Add a pin for every Hyprland release that a hyprweave release supports; see [releasing.md](../releasing.md) and the [official plugins' `hyprpm.toml`](https://github.com/hyprwm/hyprland-plugins/blob/main/hyprpm.toml). The optional `since_hyprland` key sets a minimum Hyprland commit count.

hyprpm itself needs `cpio`, `cmake`, `git`, `meson` and `gcc` to build Hyprland's headers; hyprweave's own build needs none of the first four.

## After a Hyprland update

1. Check `Hyprland --version` and `pkg-config --modversion hyprland`. They must match.
2. Rebuild. The hash check refuses a build made against other headers.
3. Run `tests/nest-test.sh` in a nest ([testing.md](../testing.md)).
4. Re-check every hook target (see [hooks.md](hooks.md)) and the group internals (see [groups.md](groups.md)) against the new source, and update these docs.
