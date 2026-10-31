# hyprweave

A C++ Hyprland plugin that turns native groups into flex-style containers: rows and columns with percentage sizes, nested, all members visible, the group still one unit in every layout. Project purpose, status and design decisions live in memex (project `hyprweave`, page `pages/projects/hyprweave.md`); search it before starting work.

## Read first

- [docs/dev/basics/](docs/dev/basics/README.md): how Hyprland 0.57 plugins work, how to build and load one, the API, function hooks, and how groups work internally. Re-check it against the source after every Hyprland update.
- [docs/dev/architecture.md](docs/dev/architecture.md): how hyprweave is built (files, hooks, tree, locks) and its known gaps.
- [docs/user/lua-api.md](docs/user/lua-api.md): the `hl.plugin.hyprweave` API. Keep it in sync with `src/Lua.cpp`.
- [docs/dev/testing.md](docs/dev/testing.md) and [docs/dev/releasing.md](docs/dev/releasing.md): the test suite, CI, and how a release and a Hyprland update are handled.

## Target

- Hyprland 0.57.0 (Arch package), commit `0aa6745f705f2c4b0a5004eb019ef76e5bbdb426`, Lua config. Headers in `/usr/include/hyprland/src/`.
- Development machine: Arch, x86_64, GCC 16.

## Develop and test

```sh
make                    # builds hyprweave.so (~10 s)
dev/nest start          # nested Hyprland in its own window; run from a terminal inside Hyprland
dev/nest reload         # rebuild and (re)load the plugin into the nest
dev/nest ctl hyprweave  # lists hyprweave groups (-j for JSON)
dev/nest ctl ...        # any hyprctl command, sent only to the nest
tests/nest-test.sh      # end-to-end checks against the running nest (~25 s)
dev/nest stop           # close all windows, then exit the nest
```

Run `tests/nest-test.sh` before handing work back, and extend it with every new behaviour.

Details and gotchas are in [build-and-load.md](docs/dev/basics/build-and-load.md#development-loop-devnest). Hyprland 0.57.0 segfaults on exit while a group exists, with or without this plugin; `dev/nest stop` avoids it.

## Rules

- Never load a build into the user's running Hyprland session, and never edit `~/.config/hypr/`, without the user's explicit OK. Test in the nest through `dev/nest`; if you call `hyprctl` by hand, pass `-i <nest>`.
- Build with a plain Makefile and `pkg-config`; `hyprpm` builds with the same `make all`.
- CI runs on the self-hosted runner `arc-hyprweave`. Never let pull requests from forks run on it.
- Keep function hooks few. Match each hook on its full demangled signature and refuse to load if any required hook is missing.
- Rebuild after every Hyprland update; the hash check rejects stale builds.
- Do not commit or push unless the user asks.
