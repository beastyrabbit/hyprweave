# hyprweave Lua API

hyprweave turns a Hyprland group into a flex-style container: every member stays visible and gets a share of the group's tile. The group still moves, floats and changes workspace as one window, in every layout (dwindle, master, scrolling, Lua layouts).

Install it first ([install.md](install.md)). All functions live under `hl.plugin.hyprweave`. Call them from your Lua config (binds, `hl.on` handlers) or from a script with `hyprctl repl '<lua>'`. They return `true` on success. On failure they return `nil, message` and report the error like core config functions do.

Windows are given as selectors (`"address:0x..."`, `"class:^mpv$"`, `"title:..."`, as in core dispatchers) or as window objects such as `hl.get_active_window()`.

## Layout tree

A group is a tree of containers. Each container is a `row` (children side by side) or a `column` (children stacked). Each child is a window or another container. `size` is the child's share relative to its siblings: `75` and `25` give the same split as `3` and `1`. Children without `size` share equally. Sizes stay proportional when the group's tile changes size.

```lua
-- stream on top (75%), chat below (25%)
{
    direction = "column",
    children = {
        { window = "class:^mpv$", size = 75 },
        { window = "class:^chatterino$", size = 25 },
    },
}

-- editor on the left half; the right half is a 75/25 column
{
    direction = "row",
    children = {
        { window = "class:^code$", size = 50 },
        { size = 50, direction = "column", children = {
            { window = "class:^firefox$", size = 75 },
            { window = "class:^kitty$", size = 25 },
        }},
    },
}
```

## Functions

### `create(spec)`

Creates a group from a layout tree. `spec` is a `row` or `column` container with an optional `lock`, an optional `gap` (pixels between this group's members, overriding `plugin:hyprweave:gap`) and an optional `background` (a colour such as `"rgb(8c9388)"`, overriding `plugin:hyprweave:background_color`). It needs at least two windows. Windows leave any group they are in first. The group takes the place of the first window in the tree, and the other windows move to its workspace.

```lua
hl.plugin.hyprweave.create({
    direction = "column",
    lock = "full",
    children = {
        { window = "address:0x55d0c1a2b3c0", size = 75 },
        { window = "address:0x55d0c1d4e5f0", size = 25 },
    },
})
```

### `lock({ window?, level })`

Sets the lock level of the group that `window` (default: the focused window) belongs to.

| Level | Effect |
| --- | --- |
| `none` | Nothing is fixed. |
| `layout` | The arrangement and sizes are fixed. |
| `members` | No windows can join (this is Hyprland's own group lock). |
| `full` | Both. The group behaves like one window. |

Closing a member of a locked group (any level except `none`) closes every member. Closing a member of an unlocked group lets its siblings take the space; when one window is left, the group dissolves.

### `dissolve({ window? })`

Turns the group back into separate windows.

### `fullscreen({ window? })`

Toggles fullscreen for the whole group, keeping the split across the monitor. A normal fullscreen (`hl.dsp.window.fullscreen()`, or the app's own fullscreen) covers the monitor with that member alone and hides the others until it leaves fullscreen. To bind group fullscreen:

```lua
hl.bind("SUPER + SHIFT + F", function() hl.plugin.hyprweave.fullscreen() end)
```

### `get({ window? })`

Returns the group's tree, or `nil` if the window is not in a hyprweave group. Sizes come back as percentages of the parent, and the root also has `lock`, plus `gap` and `background` if the group has its own.

```lua
local g = hl.plugin.hyprweave.get()
-- { lock = "full", direction = "column", size = 100, children = {
--     { window = "address:0x...", size = 75 }, { window = "address:0x...", size = 25 } } }
```

## Config

```lua
hl.config({ plugin = { hyprweave = {
    auto_join = false,                      -- newly opened windows may join a focused, unlocked hyprweave group
    frame_size = 2,                         -- border around the whole group; 0 turns it off
    frame_color_active = "rgb(cba6f7)",     -- while a member is focused
    frame_color_inactive = "rgb(7f6c99)",
    gap = -1,                               -- pixels between members; -1 uses general:gaps_in like normal tiles
    background_color = "rgba(00000000)",    -- fills the group behind its members, e.g. "rgba(cba6f733)"; transparent is off
} } })
```

The background shows in the gaps and around the members inside the frame, and through translucent windows. With `gap = 0` it fills the notches where rounded member corners meet. The frame and background corners follow `decoration:rounding` and `rounding_power` like Hyprland's own borders, so their curves run parallel to the members' corners.

Hyprland normally adds a new window to the focused group. hyprweave groups only take windows through this API unless `auto_join` is on. With `auto_join`, the new window is added to the root container with an average share.

hyprweave groups have no tab bar. Instead, one frame goes around the whole group where a single window's border would be, and the members sit inside it with half a gap of padding. Each member keeps its own border in Hyprland's group colours (`group:col.border_active`, `group:col.border_locked_active`, ...). The frame is hidden while the group or one of its members is fullscreen.

## hyprctl

`hyprctl hyprweave` lists the hyprweave groups and their trees. `hyprctl -j hyprweave` prints the same as JSON.

## Behaviour to know

- Fullscreening a member shows that member alone across the monitor; `fullscreen()` fullscreens the whole group with its split.
- The focused member is the group's current member.
- Floating one member floats the whole group.
- Unloading the plugin turns every hyprweave group back into a tab group.
