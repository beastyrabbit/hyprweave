#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/GradientValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>

inline HANDLE PHANDLE = nullptr;

#define HYPRWEAVE_VERSION "0.1.0"

// plugin:hyprweave:auto_join: newly opened windows may join a focused, unlocked hyprweave group
inline SP<Config::Values::CBoolValue> g_autoJoin;

// plugin:hyprweave:frame_size, frame_color_active, frame_color_inactive: the border around a
// whole group; 0 turns it off
inline SP<Config::Values::CIntValue>      g_frameSize;
inline SP<Config::Values::CGradientValue> g_frameActive;
inline SP<Config::Values::CGradientValue> g_frameInactive;

// plugin:hyprweave:gap: pixels between members, -1 for general:gaps_in (left + right)
inline SP<Config::Values::CIntValue> g_gap;

// plugin:hyprweave:background_color: fills the group behind its members; transparent is off
inline SP<Config::Values::CColorValue> g_background;

// counts monitor frames (render.pre), so the background is drawn once per frame
inline uint64_t g_renderSeq = 0;
