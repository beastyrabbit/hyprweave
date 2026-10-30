#pragma once

#include "Tree.hpp"

namespace Weave {
    enum class eLock : uint8_t {
        NONE,
        LAYOUT,  // arrangement and sizes are fixed
        MEMBERS, // no windows can join (Hyprland's own group lock)
        FULL,    // both: the group behaves like one window
    };

    // A hyprweave group is a native Hyprland group plus a layout tree. Hyprland keeps moving,
    // floating and unmapping the group as one unit; hyprweave decides how its members share
    // the group's box and keeps all of them visible.
    struct SGroup {
        WP<Desktop::View::CGroup> group;
        std::unique_ptr<SNode>    root;
        eLock                     lock    = eLock::NONE;
        bool                      closing = false;
        // fullscreen covers the whole group with its split kept, instead of only the member
        bool                      fullscreenGroup = false;
        // pixels between members and background colour (ARGB) for this group; unset follows
        // plugin:hyprweave:gap and plugin:hyprweave:background_color
        std::optional<int>        gap;
        std::optional<int64_t>    background;
        // space between the frame and the members, and where the frame was last drawn
        Vector2D                  framePadding;
        CBox                      lastFrame;
        // the render.pre count of the frame the background was last drawn in
        uint64_t                  backgroundSeq = 0;
    };

    // Resolves and installs the hooks. On failure returns the reason and installs nothing.
    std::expected<void, std::string>                       initHooks();

    struct SStyle {
        std::optional<int>     gap;
        std::optional<int64_t> background;
    };

    std::expected<SP<Desktop::View::CGroup>, std::string>  create(std::unique_ptr<SNode> root, eLock lock, SStyle style = {});
    std::expected<void, std::string>                       setLock(PHLWINDOW member, eLock lock);
    std::expected<void, std::string>                       dissolve(PHLWINDOW member);
    // Toggles fullscreen for the whole group of `member`, keeping the split.
    std::expected<void, std::string>                       toggleGroupFullscreen(PHLWINDOW member);

    SGroup*                                                find(const Desktop::View::CGroup* group);
    SGroup*                                                findForWindow(const PHLWINDOW& w);
    std::vector<SGroup*>                                   all();

    // Called on Event::bus() window.close: closing one member of a locked group closes all.
    void                                                   onWindowClose(PHLWINDOW w);
    // Called on window.active: the focused member becomes the group's current member, which
    // owns the group's fullscreen state.
    void                                                   onWindowFocus(PHLWINDOW w);
    // Called on window.fullscreen: leaving fullscreen ends group fullscreen.
    void                                                   onWindowFullscreen(PHLWINDOW w);

    // Turns every hyprweave group back into a native tab group. Used on unload.
    void                                                   releaseAll();

    const char*                                            lockName(eLock lock);
    std::optional<eLock>                                   lockFromName(const std::string& name);
}
