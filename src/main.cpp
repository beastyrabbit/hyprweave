#include "Internals.hpp"
#include "Lua.hpp"
#include "Weave.hpp"
#include "globals.hpp"

#include <format>
#include <stdexcept>

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/ipc/s1/S1.hpp>

using namespace Weave;

static SP<IPC::Socket1::SCommand> g_statusCommand;

static std::string escapeJson(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out;
}

static std::string nodeJson(const SNode& node, double percent) {
    if (node.type == eNodeType::WINDOW) {
        const auto W = node.window.lock();
        return std::format(R"({{"size":{:.2f},"window":"0x{:x}","class":"{}"}})", percent, rc<uintptr_t>(W.get()), W ? escapeJson(W->metadata().appID()) : "");
    }

    double total = 0;
    for (const auto& c : node.children)
        total += c->size;

    std::string children;
    for (const auto& c : node.children)
        children += (children.empty() ? "" : ",") + nodeJson(*c, 100.0 * c->size / total);

    return std::format(R"({{"size":{:.2f},"direction":"{}","children":[{}]}})", percent, typeName(node.type), children);
}

static std::string nodeText(const SNode& node, double percent) {
    if (node.type == eNodeType::WINDOW) {
        const auto W = node.window.lock();
        return std::format("{:.0f}% 0x{:x} {}", percent, rc<uintptr_t>(W.get()), W ? W->metadata().appID() : "");
    }

    double total = 0;
    for (const auto& c : node.children)
        total += c->size;

    std::string children;
    for (const auto& c : node.children)
        children += (children.empty() ? "" : ", ") + nodeText(*c, 100.0 * c->size / total);

    return std::format("{:.0f}% {}[{}]", percent, typeName(node.type), children);
}

// `hyprctl hyprweave` lists the hyprweave groups; `hyprctl -j hyprweave` as JSON.
static IPC::Socket1::SResponse status(const IPC::Socket1::SRequest& request) {
    const auto GROUPS = all();

    if (request.format == IPC::Socket1::FORMAT_JSON) {
        std::string groups;
        for (const auto* g : GROUPS) {
            const auto GROUP = g->group.lock();
            groups += std::format(R"({}{{"group":"0x{:x}","lock":"{}","tree":{}}})", groups.empty() ? "" : ",", rc<uintptr_t>(GROUP.get()), lockName(g->lock),
                                  g->root ? nodeJson(*g->root, 100.0) : "null");
        }
        return std::format(R"({{"version":"{}","groups":[{}]}})", HYPRWEAVE_VERSION, groups);
    }

    std::string out = std::format("hyprweave {}, {} group(s)\n", HYPRWEAVE_VERSION, GROUPS.size());
    for (const auto* g : GROUPS)
        out += std::format("group 0x{:x} lock={} {}\n", rc<uintptr_t>(g->group.get()), lockName(g->lock), g->root ? nodeText(*g->root, 100.0) : "(empty)");
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

    g_autoJoin = makeShared<Config::Values::CBoolValue>("plugin:hyprweave:auto_join", "newly opened windows may join a focused, unlocked hyprweave group", false);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_autoJoin);

    g_frameSize = makeShared<Config::Values::CIntValue>("plugin:hyprweave:frame_size", "width of the border around a whole group, 0 for none", 2,
                                                        Config::Values::SIntValueOptions{.min = 0, .max = 20, .refresh = Config::Supplementary::REFRESH_LAYOUTS});
    HyprlandAPI::addConfigValueV2(PHANDLE, g_frameSize);
    g_frameActive = makeShared<Config::Values::CGradientValue>("plugin:hyprweave:frame_color_active", "group frame colour while a member is focused", CHyprColor{0xffcba6f7});
    HyprlandAPI::addConfigValueV2(PHANDLE, g_frameActive);
    g_frameInactive = makeShared<Config::Values::CGradientValue>("plugin:hyprweave:frame_color_inactive", "group frame colour otherwise", CHyprColor{0xff7f6c99});
    HyprlandAPI::addConfigValueV2(PHANDLE, g_frameInactive);
    g_gap = makeShared<Config::Values::CIntValue>("plugin:hyprweave:gap", "pixels between members, -1 for general:gaps_in", -1,
                                                  Config::Values::SIntValueOptions{.min = -1, .max = 200, .refresh = Config::Supplementary::REFRESH_LAYOUTS});
    HyprlandAPI::addConfigValueV2(PHANDLE, g_gap);
    g_background = makeShared<Config::Values::CColorValue>("plugin:hyprweave:background_color", "fills the group behind its members", 0);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_background);

    if (const auto RESULT = initHooks(); !RESULT) {
        HyprlandAPI::addNotification(PHANDLE, std::format("[hyprweave] {}", RESULT.error()), CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error(std::format("[hyprweave] {}", RESULT.error()));
    }

    registerLua();

    static auto P1 = Event::bus()->m_events.window.close.listen([](PHLWINDOW w) { onWindowClose(w); });
    static auto P2 = Event::bus()->m_events.window.active.listen([](PHLWINDOW w, Desktop::eFocusReason) { onWindowFocus(w); });
    static auto P3 = Event::bus()->m_events.window.fullscreen.listen([](PHLWINDOW w) { onWindowFullscreen(w); });
    static auto P4 = Event::bus()->m_events.render.pre.listen([](PHLMONITOR) { ++g_renderSeq; });

    g_statusCommand = HyprlandAPI::registerHyprCtlCommand(PHANDLE, {.name = "hyprweave", .handler = status});

    return {"hyprweave", "Flex-style layouts for Hyprland groups", "beasty", HYPRWEAVE_VERSION};
}

APICALL EXPORT void PLUGIN_EXIT() {
    releaseAll();
}
