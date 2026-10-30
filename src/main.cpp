#include <format>
#include <stdexcept>
#include <string>
#include <vector>

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/ipc/s1/S1.hpp>

#include "globals.hpp"

struct SHookTarget {
    const char* name;      // passed to findFunctionsByName (substring of the mangled name)
    const char* signature; // exact demangled signature we expect
    void*       address = nullptr;
};

// The functions hyprweave will hook (see docs/dev/basics/groups.md). Resolved at load so a
// Hyprland update that renames or changes them makes the load fail instead of crashing later.
static std::vector<SHookTarget> g_hookTargets = {
    {"updateWindowVisibility", "Desktop::View::CGroup::updateWindowVisibility()"},
    {"updatePos", "Layout::CWindowGroupTarget::updatePos(unsigned char)"},
};

static SP<IPC::Socket1::SCommand> g_statusCommand;

static void resolveHookTargets() {
    for (auto& t : g_hookTargets) {
        for (const auto& fn : HyprlandAPI::findFunctionsByName(PHANDLE, t.name)) {
            if (fn.demangled == t.signature) {
                t.address = fn.address;
                break;
            }
        }
    }
}

static std::string status() {
    std::string out = std::format("hyprweave {}\n", HYPRWEAVE_VERSION);
    for (const auto& t : g_hookTargets)
        out += std::format("hook target {}: {}\n", t.signature, t.address ? "found" : "missing");
    return out;
}

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[hyprweave] Version mismatch: rebuild against the running Hyprland's headers", CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hyprweave] Version mismatch");
    }

    resolveHookTargets();

    for (const auto& t : g_hookTargets) {
        if (t.address)
            continue;
        HyprlandAPI::addNotification(PHANDLE, std::format("[hyprweave] Missing hook target {}", t.signature), CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error(std::format("[hyprweave] Missing hook target {}", t.signature));
    }

    g_statusCommand = HyprlandAPI::registerHyprCtlCommand(PHANDLE, {.name = "hyprweave", .handler = [](const IPC::Socket1::SRequest&) { return IPC::Socket1::SResponse{status()}; }});

    return {"hyprweave", "Display modes for Hyprland groups", "beasty", HYPRWEAVE_VERSION};
}

APICALL EXPORT void PLUGIN_EXIT() {
    ;
}
