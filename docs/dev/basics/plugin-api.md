# Plugin API (Hyprland 0.57)

The full API is [`src/plugins/PluginAPI.hpp`](https://github.com/hyprwm/Hyprland/blob/v0.57.0/src/plugins/PluginAPI.hpp), installed at `/usr/include/hyprland/src/plugins/PluginAPI.hpp`. Read that header before you guess at a signature. This page summarises it and shows the patterns the official plugins use in 0.57.

## Skeleton

```cpp
// globals.hpp
#pragma once
#include <hyprland/src/plugins/PluginAPI.hpp>
inline HANDLE PHANDLE = nullptr;
```

```cpp
// main.cpp
#include "globals.hpp"

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[hyprweave] Version mismatch: headers do not match the running Hyprland",
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hyprweave] Version mismatch");
    }

    // config values, Lua functions, dispatchers, event listeners, hooks

    return {"hyprweave", "Display modes for Hyprland groups", "<author>", "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // undo state that Hyprland does not clean up itself
}
```

- `PLUGIN_API_VERSION` returns `HYPRLAND_API_VERSION` (`"0.2"` in 0.57). Hyprland ejects the `.so` on mismatch.
- `PLUGIN_INIT` runs synchronously on the compositor thread. Blocking calls into Hyprland from here, such as `system("hyprctl ...")`, can hang it.
- The client hash is the Hyprland commit plus the major.minor versions of aquamarine, hyprutils, hyprgraphics, hyprcursor and hyprlang. A rebuild after any of those changes is required.
- Throwing from `PLUGIN_INIT` aborts the load. Show a notification first so the user sees why.
- `PLUGIN_EXIT` runs only on a user unload, and hooks are removed after it returns. On every unload, Hyprland itself removes the plugin's tiled and floating algorithms, function hooks, window decorations, dispatchers and hyprctl commands, then lets the config manager drop its config values and Lua functions, `dlclose`s the `.so` and queues a config reload (`CPluginSystem::unloadPlugin` in `src/plugins/PluginSystem.cpp`). The plugin must undo anything else it changed in core objects. For example, `hyprfocus` clears the animation callbacks it set on windows. hyprweave will have to restore group member visibility and geometry on unload.

## What to use in 0.57

| Need | Use | Avoid (deprecated in 0.57) |
| --- | --- | --- |
| Config values | `addConfigValueV2` with `Config::Values::C*Value` | `addConfigValue`, `getConfigValue`, `addConfigKeyword` |
| React to events | `Event::bus()->m_events...listen(...)` | `registerCallbackDynamic`, `unregisterCallback` (no-ops now) |
| Keybind actions | `addDispatcherV2` | `addDispatcher` |
| Lua config functions | `addLuaFunction` | |
| hyprctl subcommand | `registerHyprCtlCommand` | |
| Custom tiling layout | `addTiledAlgo` / `addFloatingAlgo` / `removeAlgo` | `addLayout`, `removeLayout` |
| Hook internal functions | `findFunctionsByName` + `createFunctionHook` | `getFunctionAddressFromSignature` |
| Notification | `addNotification` or `addNotificationV2` | |
| Version info | `getHyprlandVersion` | |
| Plugin event for `hl.on` | `addEvent` / `removeEvent` | |

## Config values

Names must start with `plugin:`. Use `plugin:hyprweave:<option>`. Register only inside `PLUGIN_INIT`, then keep the shared pointer and read `->value()` whenever you need the current value. Pattern from [`hyprfocus/main.cpp`](https://github.com/hyprwm/hyprland-plugins/blob/main/hyprfocus/main.cpp):

```cpp
#include <hyprland/src/config/values/types/FloatValue.hpp>

static SP<Config::Values::CFloatValue> g_ratio;

// in PLUGIN_INIT
g_ratio = makeShared<Config::Values::CFloatValue>("plugin:hyprweave:ratio", "size of the first member", 0.5F,
                                                  Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
HyprlandAPI::addConfigValueV2(PHANDLE, g_ratio);

// anywhere later
const float RATIO = g_ratio->value();
```

Value types live in `src/config/values/types/`: `BoolValue`, `IntValue`, `FloatValue`, `StringValue`, `ColorValue`, `GradientValue`, `Vec2Value`, `CssGapValue`, `FontWeightValue`. Each has an options struct (`SIntValueOptions` has `min`, `max`, and a `map` from strings to ints, which suits enums such as a display mode).

In the Lua config the user sets them with `hl.config({ plugin = { hyprweave = { ratio = 0.75 } } })`.

To read a core option, use `CConfigValue`, as core code does: `static auto PINSERT = CConfigValue<Config::INTEGER>("group:insert_after_current");`, then `*PINSERT` (include `src/config/ConfigValue.hpp`).

## Lua functions

`addLuaFunction(PHANDLE, "hyprweave", "set_mode", fn)` exposes `hl.plugin.hyprweave.set_mode(...)` to the Lua config and to `hyprctl eval`. The callback has the plain Lua C signature `int (*)(lua_State*)`. The namespace `load` is reserved because `hl.plugin.load` loads plugins. Pattern from [`csgo-vulkan-fix/main.cpp`](https://github.com/hyprwm/hyprland-plugins/blob/main/csgo-vulkan-fix/main.cpp):

```cpp
#include <hyprland/src/config/lua/bindings/LuaBindingsInternal.hpp>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

static int luaSetMode(lua_State* L) {
    if (!lua_istable(L, 1))
        return Config::Lua::Bindings::Internal::configError(L, "set_mode: expected a table");
    // lua_getfield / lua_tostring / lua_pop as usual
    return 0;
}

// in PLUGIN_INIT
HyprlandAPI::addLuaFunction(PHANDLE, "hyprweave", "set_mode", ::luaSetMode);
```

`configError` reports the error through Hyprland's config error system instead of raising a raw Lua error. It returns normally (no `longjmp`), so it is safe to call with C++ objects on the stack.

Not every helper in `LuaBindingsInternal.hpp` is exported from the binary. In 0.57, `windowFromLuaSelectorOrObject` is not, but `tableOptWindow(L, idx, field, fnName)` is; it reads a table field and resolves any core window selector or `HL.Window` object. A call to an unexported function compiles but fails to load with an undefined symbol. Check with `nm -DC /usr/bin/Hyprland | grep <name>` first.

Hyprland 0.57 uses Lua 5.5 (`/usr/include/lua.h`). The binary also pulls in Lua 5.4 through libinput; the plugin's `lua_*` calls still resolve to 5.5 because Hyprland links it directly. Do not include `lua5.4/` headers. Values passed from Lua that the plugin stores across a config reload need clearing on reload; `csgo-vulkan-fix` clears its list on `m_events.config.preReload`.

The core Lua group object (`src/config/lua/objects/LuaGroup.cpp`, metatable `HL.Group`) exposes `add`, `remove`, `locked`, `denied`, `size`, `current_index`, `current` and `members`. A Lua function from hyprweave can accept window objects from that API; see how `LuaGroup.cpp` validates a `HL.Window` argument.

## Dispatchers

```cpp
static SDispatchResult dispatchMode(std::string args) {
    // parse args, e.g. with Hyprutils::String::CVarList
    return {};                                   // success
    // return {.success = false, .error = "..."}; on failure
}

HyprlandAPI::addDispatcherV2(PHANDLE, "hyprweave:mode", dispatchMode);
```

`SDispatchResult` (in `src/SharedDefs.hpp`) has `passEvent`, `success`, `requestRelease` and `error`. hy3 registers each action twice, once as a dispatcher (`hy3:makegroup`) and once as a Lua function (`hl.plugin.hy3.make_group`). That pattern lets both `hyprctl dispatch` and Lua binds call it.

## Event bus

`Event::bus()` (`src/event/EventBus.hpp`) replaced the old callback API. Keep the listener object alive; the official plugins store it in a `static` or global:

```cpp
#include <hyprland/src/event/EventBus.hpp>

static auto P = Event::bus()->m_events.window.active.listen([](PHLWINDOW w, Desktop::eFocusReason r) { /* ... */ });
```

Events in 0.57 that matter for hyprweave:

- `window`: `create`, `open`, `openEarly`, `openLate`, `destroy`, `close`, `active` (window plus focus reason; `FOCUS_REASON_GROUP_CURRENT_WINDOW_CHANGE` marks a group tab switch), `fullscreen`, `floating`, `moveToWorkspace`, `updateRules`, `updateState`.
- `config`: `preReload`, `reloaded`.
- `render`: `pre`, `stage` (`RENDER_PRE_WINDOWS`, `RENDER_POST_WINDOW`, ...). hy3 draws its tab bars from `stage`.
- `workspace`, `monitor`, `input` (mouse, keyboard, touch, tablet; the `Cancellable` ones can stop the event).

There are **no group events**: nothing fires when a group forms, gains or loses a member, or changes its current member. To react to those, hook the `CGroup` methods (see [groups.md](groups.md) and [hooks.md](hooks.md)).

## Threads

The Wayland event loop is single threaded. Do all work on the compositor thread. Only fully detached work, such as writing a file, may run in another thread. To defer work to a later loop iteration, use `g_pEventLoopManager->doLater(...)` (`src/managers/eventLoop/EventLoopManager.hpp`), as `hyprfocus` does.

## Private members

To reach private members, wrap Hyprland includes in `#define private public` / `#undef private`. Include the STL headers that Hyprland pulls in **before** that block. With GCC 16, leaving `<sstream>` inside the block fails with `error: 'struct std::__cxx11::basic_stringbuf<...>::__xfer_bufptrs' redeclared with different access`. This order compiles:

```cpp
#include <sstream>
#include <any>
#include <vector>
#include <hyprland/src/plugins/PluginAPI.hpp>

#define private public
#include <hyprland/src/desktop/view/Group.hpp>
#include <hyprland/src/layout/target/WindowGroupTarget.hpp>
#undef private
```

Private access depends on field layout and names that can change in any release. Keep it to the few places that need it.
