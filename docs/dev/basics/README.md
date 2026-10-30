# Hyprland plugin basics

Reference for building hyprweave, a Hyprland plugin. Everything here was checked against Hyprland **0.57.0** (commit `0aa6745f705f2c4b0a5004eb019ef76e5bbdb426`) on 2026-10-30. The installed headers under `/usr/include/hyprland` match that tag. Hyprland changes its internals between releases, so re-check these pages after every Hyprland update.

## Pages

| Page | Read it when |
| --- | --- |
| [build-and-load.md](build-and-load.md) | You need to compile the `.so`, use `dev/nest`, or load and unload the plugin. |
| [plugin-api.md](plugin-api.md) | You write `PLUGIN_INIT`, config values, Lua functions, dispatchers, or event listeners. |
| [hooks.md](hooks.md) | You need to change core behaviour that the API does not expose. |
| [groups.md](groups.md) | You work on group display modes, the core of hyprweave. |

## The short version

- A plugin is a C++ shared object that runs inside the Hyprland process. A crash in the plugin is a crash of the compositor.
- It must export `pluginAPIVersion` and `pluginInit`, and may export `pluginExit`. Use the `PLUGIN_API_VERSION`, `PLUGIN_INIT` and `PLUGIN_EXIT` macros.
- `PLUGIN_INIT` must compare `__hyprland_api_get_hash()` with `__hyprland_api_get_client_hash()` and throw on mismatch.
- Register config values only inside `PLUGIN_INIT`. Use `addConfigValueV2`; the V1 config API is deprecated in 0.57.
- Hyprland removes hooks, dispatchers, Lua functions, config values and layouts on unload.
- Prefer the API and the event bus (`Event::bus()`). Use function hooks only when nothing else works. Groups have no event bus events, so hyprweave will need hooks.
- Rebuild the plugin for every Hyprland update. The hash check rejects a stale build.
- Develop in a nested Hyprland session through `dev/nest`. Do not load test builds into the user's real session unless the user asks.
- Hyprland 0.57.0 segfaults on exit while a group exists, with or without plugins. Do not mistake that for a plugin crash.

## Upstream sources

| Source | Version read |
| --- | --- |
| [Plugin wiki: getting started](https://github.com/hyprwm/hyprland-wiki/blob/main/content/hyprland-plugins/development/getting-started.md) | `main`, 2026-10-30 |
| [Plugin wiki: advanced](https://github.com/hyprwm/hyprland-wiki/blob/main/content/hyprland-plugins/development/advanced.md) | `main`, 2026-10-30. Its config example uses the deprecated V1 API, and it calls the hook field `m_pOriginal`; in 0.57 it is `m_original`. |
| [Plugin wiki: guidelines](https://github.com/hyprwm/hyprland-wiki/blob/main/content/hyprland-plugins/development/plugin-guidelines.md) | `main`, 2026-10-30 |
| [Wiki: using plugins](https://github.com/hyprwm/hyprland-wiki/blob/main/content/hyprland-plugins/using-plugins.md) | `main`, 2026-10-30 |
| [Wiki: development environment](https://github.com/hyprwm/hyprland-wiki/blob/main/content/contributing-and-debugging/_index.md) | `main`, 2026-10-30 |
| [Hyprland source](https://github.com/hyprwm/Hyprland/tree/v0.57.0) | `v0.57.0` |
| [`src/plugins/PluginAPI.hpp`](https://github.com/hyprwm/Hyprland/blob/v0.57.0/src/plugins/PluginAPI.hpp) | `v0.57.0`. The best API reference; every function has a comment. |
| [hyprland-plugins](https://github.com/hyprwm/hyprland-plugins) | `main` at `d2427d4` (2026-10-20). `hyprfocus` and `csgo-vulkan-fix` are the smallest current examples. |
| [hy3](https://github.com/outfoxxed/hy3/tree/hl0.57.0) | `hl0.57.0`. A large layout plugin, useful for patterns only. |
