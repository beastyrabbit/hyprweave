# How hyprweave works

Read [basics/groups.md](basics/groups.md) first; it describes the core group code this builds on.

## The idea

A hyprweave group is a native Hyprland `CGroup` plus a hyprweave layout tree. The core keeps doing everything it already does for groups: the layout sees one target, and moving, floating, workspace changes, dragging and unmapping treat the group as a unit. hyprweave changes two things: which members are visible, and which box each member gets.

## Files

| File | Contents |
| --- | --- |
| `src/Internals.hpp` | The Hyprland headers with `private` and `protected` opened. Every source file includes it first. |
| `src/Tree.{hpp,cpp}` | `SNode` (row, column, window) and the box maths: `layout`, `prune`, `append`. No core state. |
| `src/Weave.{hpp,cpp}` | The registry of hyprweave groups (`SGroup`: weak group pointer, tree, lock), the hooks, and `create`, `setLock`, `dissolve`, `releaseAll`. |
| `src/Frame.{hpp,cpp}` | `CFrameDecoration`, the border around a whole group. |
| `src/Lua.cpp` | `hl.plugin.hyprweave.create`, `lock`, `dissolve`, `fullscreen`, `get`. |
| `src/main.cpp` | `PLUGIN_INIT` and `PLUGIN_EXIT`, the config values (`auto_join`, `frame_*`, `gap`, `background_color`), the `window.close`, `window.active`, `window.fullscreen` and `render.pre` listeners, `hyprctl hyprweave`. |

## Hooks

All three are matched on their full demangled signature; if any is missing the plugin refuses to load.

| Hook | For hyprweave groups | Otherwise |
| --- | --- | --- |
| `CGroup::updateWindowVisibility()` | Syncs the tree with the members (`prune`, then `append` for new members), keeps every member visible and accepting input, removes the tab bar decoration, recalculates the group target. Dissolves the group (deferred) when one member is left. | Original. |
| `CWindowGroupTarget::updatePos(unsigned char)` | Splits the group target's `m_box` (inside the frame, see below) along the tree and calls `setPositionGlobal` on each member's window target. Also sets member visibility (alpha and input block). When the current member is fullscreen on its own, it gets the whole box and the others are hidden like inactive tabs. | Original (every member gets the whole box). |
| `CWindowGroupMembership::canBeGroupedInto(...)` | Returns false, except for a window mapping for the first time while `auto_join` is on and the lock is `none` or `layout`; then it lifts the group's core lock and asks the original, so global locks and window rules still apply. This covers auto-group on open and dragging. | Original. |

`updateWindowVisibility` runs after every membership or current-member change (`init`, `add`, `remove`, `replaceMember`, `setCurrent`, `swapWith*`), so it is the one place where the tree catches up with changes made by the core.

## Gaps

Layouts hand the group a `STargetBox`. Dwindle and master fill only `logicalBox`; `CWindowTarget::updatePos` then adds `gaps_in` on every edge that does not touch the monitor's work area. Splitting `logicalBox` therefore gives normal gaps between members for free. Scrolling also fills `visualBox` with its gaps already applied. In that case hyprweave splits `visualBox` too and puts `gaps_in` (left + right, or top + bottom, from the workspace rule or `general:gaps_in`) between siblings.

## Frame

Outside group fullscreen, `hkUpdatePos` places the members itself (`layoutMembers`), whether or not there is a frame. That way the group's gap (`create({ gap })`, else `plugin:hyprweave:gap`, else `gaps_in` left + right) applies in every layout.
1. It works out where a lone window's border would go in the group's slot. That is the visual box if the layout filled it. Otherwise it is the logical box minus the `gaps_in` that `CWindowTarget::updatePos` would add (none on edges at the work area's edge). Floating windows draw their border outside their box, so their slot is grown by the border.
2. With a frame, the frame takes the outer `frame_size` pixels of that box, and the members tile the rest with half a gap of padding. Without one, the members tile the whole box.
3. Each member gets its slice as both logical and visual box, so the core uses it as is and only takes the member's own border off.

Every member carries a `CFrameDecoration` (added through `HyprlandAPI::addWindowDecoration`, so an unload removes it). Only the first member in the tree draws it, as a `CBorderPassElement` around the union of the members' current boxes plus padding. Following the current boxes keeps the frame on the members while they animate. The decoration has no edges and no extents, so it reserves no room and does not move the member's own border. It damages its old and new box itself whenever they differ; focus changes damage every frame, because the colour follows focus.

Corner radii use Hyprland's border rule: a curve `d` pixels further out has radius `r + d`, less `d * (√2 - 1) * max(2 - rounding_power, 0)`. The frame's inner radius is the members' rounding taken out by their border plus the padding, and its outer radius is that taken out by `frame_size`. The background uses the inner radius.

`background_color` (or the group's `background`) is a `CRectPassElement` over the same inner box. It must sit under every member, so the first member whose decoration draws in a frame paints it. Decorations on the bottom layer are drawn just before their window. `render.pre` counts frames (`g_renderSeq`), and `SGroup::backgroundSeq` records the frame that already has a background.

## Locks and closing

Every hyprweave group has the native group lock (`CGroup::setLocked`), whatever its level. The core checks it in every move into a group (`into_group`, directional moves, `moveintoorcreategroup`), so windows only join through `create` and `auto_join`, and a directional move of a member moves the whole group. The hyprweave level decides the rest: `members` and `full` also keep `auto_join` out, and `layout` is hyprweave state only; nothing changes sizes yet besides `create`. On `Event::bus()` `window.close` (emitted at the start of unmap, while the window is still a member), a locked group sends `sendClose()` to the other members on the next loop iteration. `SGroup::closing` stops the cascade from repeating.

## Creating a group

`create` forms the group the way the core's own group actions do: with the first window focused. Every core caller has the window being grouped focused, and with focus on another window of the workspace dwindle loses the group's tile while it forms (the group gets an empty box until the next full relayout). So `create` focuses the first window, forms the group, and gives focus back.

## Focus and fullscreen

Hyprland stores a group's fullscreen state on its current member (`IFullscreenHandler` resolves a group target to `current()`). So the focused member must be current: on `window.active`, a member that is not current becomes current (deferred with `doLater`, skipped while the group is fullscreen). `create` makes the focused member current too.

A normal fullscreen therefore marks the focused member, and `hkUpdatePos` shows it alone. `fullscreen()` sets `SGroup::fullscreenGroup` and then fullscreens the current member; with the flag set, `hkUpdatePos` keeps the split. The `window.fullscreen` listener clears the flag once the current member leaves fullscreen.

## Unload

`PLUGIN_EXIT` calls `releaseAll`: it forgets every hyprweave group, gives each member its tab bar back (`applyWindowDecosAndUpdates`), and runs `updateWindowVisibility`, which now takes the original path. Hooks are still installed during `PLUGIN_EXIT` and removed after it.

## Tests

`tests/nest-test.sh` runs against a nest started with `dev/nest start`: it creates, moves, floats, fullscreens, closes and nests groups in dwindle, master and scrolling, checks geometry with `hyprctl clients -j` and frame colours with `grim`. CI runs the same suite in a VM. See [testing.md](testing.md).

## Known gaps

- Directional focus and window moves inside a group use the core behaviour, which does not know about the tree. Keyboard editing is phase 3.
- `auto_join` appends to the root container instead of next to the focused member.
- `minSize`, `maxSize` and `desiredGeometry` of the group target still come from the current member only.
- Changing `frame_color_*` at runtime shows on the next redraw of the group, not immediately.
- No `tabs` node type yet.
