#pragma once

// Hyprland headers that hyprweave needs with private and protected members opened up
// (CGroup internals, ITarget::m_box, CWindowGroupMembership::m_window). Include this before
// any other Hyprland header in every source file, so all files see the same definitions.
// STL headers come first: `#define private public` breaks them (docs/dev/basics/plugin-api.md).
#include <any>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <hyprland/src/plugins/PluginAPI.hpp>

#define private public
#define protected public
#include <hyprland/src/desktop/view/Group.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowGroupMembership.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/layout/target/WindowGroupTarget.hpp>
#include <hyprland/src/layout/target/WindowTarget.hpp>
#undef protected
#undef private
