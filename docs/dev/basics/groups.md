# Groups in Hyprland 0.57

How native groups work inside Hyprland 0.57.0. hyprweave changes this behaviour, so read this page before touching group code. File paths are relative to `src/` in the [v0.57.0 source](https://github.com/hyprwm/Hyprland/tree/v0.57.0/src), and the same headers are in `/usr/include/hyprland/src/`.

## The pieces

| Class | File | Role |
| --- | --- | --- |
| `Desktop::View::CGroup` | `desktop/view/Group.{hpp,cpp}` | Member list (`m_windows`), current index (`m_current`), lock and deny flags. Decides which member is visible. |
| `Layout::CWindowGroupTarget` | `layout/target/WindowGroupTarget.{hpp,cpp}` | The single layout target that stands for the whole group. Layouts only ever see this. |
| `Layout::CWindowTarget` | `layout/target/WindowTarget.{hpp,cpp}` | Per-window target. Each member keeps one, but while grouped it is a "ghost" in the space. |
| `Layout::ITarget` | `layout/target/Target.{hpp,cpp}` | Base class: box, space, floating state, `setPositionGlobal`, `recalc`. |
| `CHyprGroupBarDecoration` | `render/decorations/CHyprGroupBarDecoration.{hpp,cpp}` | The tab bar. Every member gets its own instance. |
| `HL.Group` (Lua) | `config/lua/objects/LuaGroup.cpp` | Lua view of a group: `add`, `remove`, `locked`, `denied`, `size`, `current_index`, `current`, `members`. |

`Desktop::View::groups()` returns every live group as `std::vector<WP<CGroup>>`. A window's group is `window->grouping().group()`.

## How a group gets one box

1. `CGroup::create` builds the group and a `CWindowGroupTarget` for it, then calls `init()`.
2. `init()` attaches every window, calls `g_layoutManager->switchTargets(firstWindow->windowTarget(), m_target)` so the group target takes the first window's place in the layout, and calls `setSpaceGhost(space)` on every member's window target. A ghost target belongs to the space but the layout algorithm never sees it (`ITarget::m_ghostSpace`).
3. The layout (dwindle, master, scrolling, a Lua layout or a plugin algorithm) positions the group target like any other target, through `ITarget::setPositionGlobal(STargetBox)`. `STargetBox` has a `logicalBox` and a `visualBox`.
4. `CWindowGroupTarget::setPositionGlobal` stores the box and calls the private `updatePos`, which passes **the same box to every member**:

   ```cpp
   void CWindowGroupTarget::updatePos(uint8_t flags) {
       for (const auto& w : m_group->windows())
           w->windowTarget()->setPositionGlobal(m_box, flags);
   }
   ```

   Every member, hidden or not, has the full group geometry.

5. `CWindowTarget::updatePos` turns the box into the window's real geometry. For a tiled, non-fullscreen window:
   - If `visualBox` is empty, it adds `general:gaps_in` (or the workspace rule's gaps) on every side of `logicalBox` that does not touch the monitor's work area. If the layout already filled `visualBox`, it uses that and adds no gaps.
   - It subtracts the window's own reserved decoration area (`getFullWindowReservedArea()`, which includes the group bar), applies pseudotiling and `misc:size_limits_tiled`, then configures the client.

   Floating members get `logicalBox` as is. Maximized members use the reserved area of the group's current member instead of their own; the source comment says "other windows are hidden anyway".

   So if hyprweave hands each member a slice of `logicalBox` with an empty `visualBox`, the core adds inner gaps between the slices without extra code. Verify that once it exists.

This is why one plugin can work in every layout: whatever algorithm runs, it ends with a box for the group target.

## How the core hides members

`CGroup::updateWindowVisibility()` (private) runs after every membership or current-member change:

```cpp
for (size_t i = 0; i < m_windows.size(); ++i) {
    if (i == m_current) {
        x->setInputBlocked(FOCUS_BLOCK_GROUP_INACTIVE, false);
        *x->presentation().alpha(WINDOW_ALPHA_LAYOUT) = 1.F;
        // rule, decoration and value refresh
    } else {
        x->setInputBlocked(FOCUS_BLOCK_GROUP_INACTIVE, true);
        *x->presentation().alpha(WINDOW_ALPHA_LAYOUT) = 0.F;
    }
}
m_target->recalc();
m_target->damageEntire();
```

Inactive members stay mapped at full size. They are fully transparent (`WINDOW_ALPHA_LAYOUT` 0) and do not accept input (`IFocusable::acceptsInput` is false while any block reason is set). The core does not use `CWindow::setHidden` for groups.

Its callers in 0.57 are `init`, `add`, `replaceMember`, `remove`, `setCurrent(size_t)`, `swapWithNext` and `swapWithLast`. All 7 calls go through the exported symbol in the packaged binary (see [hooks.md](hooks.md#check-that-a-hook-will-fire)).

## Where the core assumes one visible member

Showing several members at once breaks these assumptions. Each needs a decision during design.

- `CWindowGroupTarget::window()` returns `current()`. Code that asks the layout target for "its window" gets only the current member.
- `CWindowGroupTarget::floating()`, `desiredGeometry()`, `minSize()` and `maxSize()` all read from `current()`. A layout that respects min sizes sees only the current member's.
- `CWindowGroupTarget::damageEntire()` damages only `current()`. Other visible members would not repaint when the group repaints.
- Fullscreen: `CWindowTarget::updatePos` takes fullscreen state from the group's current member (`effectiveWindow`). `CGroup::add`, `setCurrent` and `replaceMember` move fullscreen between members. Window rules that match on fullscreen state also read `current()` (`desktop/rule/windowRule/WindowRule.cpp`).
- Focus: `CLayoutManager::bringTargetToTop` (`layout/LayoutManager.cpp`) calls `group->setCurrent(window)`. When a grouped window gets focus, `CFocusState` calls `CWindow::deactivateGroupMembers()`, which tells every other member's Wayland client that it is inactive (X11 clients excepted).
- Counting: `CHLWorkspace::getWindowCount` and `getGroups` (`workspace/HLWorkspace.cpp`) treat a member with `FOCUS_BLOCK_GROUP_INACTIVE` as not visible.
- Group bar: each member has its own `CHyprGroupBarDecoration`. It reserves space at the top edge (`DECORATION_POSITION_STICKY`, `DECORATION_EDGE_TOP`, `info.reserved = true`), with height from the `group:groupbar:*` options. In a side-by-side mode every visible member would draw a full tab bar unless hyprweave changes that.
- Group events: the event bus has none (see [plugin-api.md](plugin-api.md#event-bus)). Socket2 IPC posts `togglegroup` from `init` and `destroy`.

Other files that touch groups and need checking: `layout/supplementary/DragController.cpp` (drag into group), `layout/algorithm/tiled/scrolling/ScrollingAlgorithm.cpp` and `ScrollingFullscreenHandler.cpp`, `managers/fullscreen/handler/FullscreenHandler.cpp`, `desktop/state/GlobalWindowController.cpp`, `desktop/view/window/WindowSwallowController.cpp`, `config/shared/actions/ConfigActions.cpp` (group dispatchers), `ipc/s1/Commands.cpp` (hyprctl output), `render/decorations/CHyprBorderDecoration.cpp` (group border colours). Find them again with `grep -rln 'grouping().group()' src`.

## Hookable symbols in the installed binary

All `CGroup` and `CWindowGroupTarget` methods are exported from `/usr/bin/Hyprland` 0.57.0, private ones included. List them with:

```sh
nm -DC /usr/bin/Hyprland | grep -E 'CGroup::|CWindowGroupTarget::'
```

The two functions that decide what a group looks like:

| Function | Controls |
| --- | --- |
| `Desktop::View::CGroup::updateWindowVisibility()` | Which members are transparent and input-blocked. |
| `Layout::CWindowGroupTarget::updatePos(unsigned char)` | Which box each member gets. |

These are where to start, not a finished design. The previous section lists what else depends on `current()`.

## How hy3 does it (patterns only)

[hy3](https://github.com/outfoxxed/hy3/tree/hl0.57.0) is a full tiling algorithm (`addTiledAlgo`) with its own node tree. The user does not want to use it, but some of its 0.57 code shows working patterns:

- It hides inactive tabs with `CWindow::setHidden(bool)`, which also drops focus from the window and suspends it. It does not use the core group mechanism (`src/Hy3Node.cpp`, `recalcSizePosRecursive`).
- It reads inner gaps the way the core does, from the workspace rule with `general:gaps_in` as fallback, and subtracts `(n - 1)` gaps before splitting a box by ratio.
- It positions windows through `setPositionGlobal({.logicalBox = ..., .visualBox = ...})` followed by `warpPositionSize()` when it does not want an animation.
- It draws tab bars as render pass elements from `Event::bus()->m_events.render.stage` at `RENDER_POST_WINDOW`.
- It registers every action as both a dispatcher and a Lua function.
