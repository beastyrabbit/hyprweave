# hyprweave

A C++ Hyprland plugin that adds display modes to native groups, so group members can share the group's tile side by side or stacked instead of only as tabs. Project purpose, status and decisions live in memex (project `hyprweave`); search it before starting work.

## Read first

- [docs/dev/basics/](docs/dev/basics/README.md): how Hyprland 0.57 plugins work, how to build and load one, the API, function hooks, and how groups work internally. Re-check it against the source after every Hyprland update.

## Target

- Hyprland 0.57.0 (Arch package), commit `0aa6745f705f2c4b0a5004eb019ef76e5bbdb426`, Lua config. Headers in `/usr/include/hyprland/src/`.
- Development machine: `bunux` (Arch, x86_64, GCC 16).

## Develop and test

```sh
make                    # builds hyprweave.so (~2 s)
dev/nest start          # nested Hyprland in its own window; run from a terminal inside Hyprland
dev/nest reload         # rebuild and (re)load the plugin into the nest
dev/nest ctl hyprweave  # plugin status: version and hook targets
dev/nest ctl ...        # any hyprctl command, sent only to the nest
dev/nest stop           # close all windows, then exit the nest
```

Details and gotchas are in [build-and-load.md](docs/dev/basics/build-and-load.md#development-loop-devnest). Hyprland 0.57.0 segfaults on exit while a group exists, with or without this plugin; `dev/nest stop` avoids it.

## Rules

- Never load a build into the user's running Hyprland session, and never edit `~/.config/hypr/`, without the user's explicit OK. Test in the nest through `dev/nest`; if you call `hyprctl` by hand, pass `-i <nest>`.
- Build with a plain Makefile and `pkg-config`. `cmake`, `meson` and `hyprpm` are not installed; ask before installing them.
- Keep function hooks few. Match each hook on its full demangled signature and refuse to load if any required hook is missing.
- Rebuild after every Hyprland update; the hash check rejects stale builds.
- Do not commit or push unless the user asks.
