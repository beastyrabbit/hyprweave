#include "Internals.hpp"
#include "Lua.hpp"
#include "Weave.hpp"
#include "globals.hpp"

#include <format>

#include <hyprland/src/config/lua/bindings/LuaBindingsInternal.hpp>
#include <hyprland/src/config/shared/parserUtils/ParserUtils.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

using namespace Weave;
namespace Internal = Config::Lua::Bindings::Internal;

static constexpr int MAX_DEPTH = 32;

// Reports `msg` like core Lua functions do and returns `nil, msg` to the caller.
static int fail(lua_State* L, const char* fn, const std::string& msg) {
    Internal::configError(L, std::format("{}: {}", fn, msg));
    lua_pushnil(L);
    lua_pushstring(L, msg.c_str());
    return 2;
}

static std::unique_ptr<SNode> parseNode(lua_State* L, int idx, const char* fn, int depth, std::string& err) {
    idx = lua_absindex(L, idx);

    if (!lua_istable(L, idx)) {
        err = "every node must be a table";
        return nullptr;
    }
    if (depth > MAX_DEPTH) {
        err = "containers are nested too deep";
        return nullptr;
    }

    auto node = std::make_unique<SNode>();

    lua_getfield(L, idx, "size");
    if (!lua_isnil(L, -1)) {
        if (!lua_isnumber(L, -1) || lua_tonumber(L, -1) <= 0) {
            lua_pop(L, 1);
            err = "size must be a positive number";
            return nullptr;
        }
        node->size = lua_tonumber(L, -1);
    }
    lua_pop(L, 1);

    if (Internal::hasTableField(L, idx, "window")) {
        const auto W = Internal::tableOptWindow(L, idx, "window", fn);
        if (!W || !*W) {
            err = "window not found";
            return nullptr;
        }
        node->type   = eNodeType::WINDOW;
        node->window = *W;
        return node;
    }

    lua_getfield(L, idx, "direction");
    const std::string DIRECTION = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);

    if (DIRECTION == "row")
        node->type = eNodeType::ROW;
    else if (DIRECTION == "column")
        node->type = eNodeType::COLUMN;
    else {
        err = "a node needs either window, or direction = \"row\" or \"column\"";
        return nullptr;
    }

    lua_getfield(L, idx, "children");
    if (!lua_istable(L, -1) || lua_rawlen(L, -1) == 0) {
        lua_pop(L, 1);
        err = "a row or column needs a non-empty children list";
        return nullptr;
    }

    const int CHILDREN = lua_absindex(L, -1);
    const int COUNT    = sc<int>(lua_rawlen(L, CHILDREN));
    for (int i = 1; i <= COUNT; ++i) {
        lua_rawgeti(L, CHILDREN, i);
        auto child = parseNode(L, -1, fn, depth + 1, err);
        lua_pop(L, 1);
        if (!child) {
            lua_pop(L, 1);
            return nullptr;
        }
        node->children.push_back(std::move(child));
    }
    lua_pop(L, 1);

    return node;
}

// The `window` field of the table at index 1, or the focused window if there is none.
static PHLWINDOW targetWindow(lua_State* L, const char* fn) {
    if (lua_istable(L, 1) && Internal::hasTableField(L, 1, "window")) {
        const auto W = Internal::tableOptWindow(L, 1, "window", fn);
        return W ? *W : nullptr;
    }
    return Desktop::focusState()->window();
}

static void pushNode(lua_State* L, const SNode& node, double percent) {
    lua_newtable(L);

    lua_pushnumber(L, percent);
    lua_setfield(L, -2, "size");

    if (node.type == eNodeType::WINDOW) {
        const auto W = node.window.lock();
        lua_pushstring(L, std::format("address:0x{:x}", rc<uintptr_t>(W.get())).c_str());
        lua_setfield(L, -2, "window");
        return;
    }

    lua_pushstring(L, typeName(node.type));
    lua_setfield(L, -2, "direction");

    double total = 0;
    for (const auto& c : node.children)
        total += c->size;

    lua_newtable(L);
    for (size_t i = 0; i < node.children.size(); ++i) {
        pushNode(L, *node.children[i], 100.0 * node.children[i]->size / total);
        lua_rawseti(L, -2, sc<lua_Integer>(i + 1));
    }
    lua_setfield(L, -2, "children");
}

// hl.plugin.hyprweave.create({ direction, lock?, gap?, background?, children = { { window, size? } | { direction, size?, children }, ... } })
static int luaCreate(lua_State* L) {
    constexpr const char* FN = "hl.plugin.hyprweave.create";

    if (!lua_istable(L, 1))
        return fail(L, FN, "expected a table");

    std::string err;
    auto        root = parseNode(L, 1, FN, 0, err);
    if (!root)
        return fail(L, FN, err);

    auto lock = eLock::NONE;
    lua_getfield(L, 1, "lock");
    if (!lua_isnil(L, -1)) {
        const auto LOCK = lua_isstring(L, -1) ? lockFromName(lua_tostring(L, -1)) : std::nullopt;
        lua_pop(L, 1);
        if (!LOCK)
            return fail(L, FN, "lock must be \"none\", \"layout\", \"members\" or \"full\"");
        lock = *LOCK;
    } else
        lua_pop(L, 1);

    SStyle style;
    lua_getfield(L, 1, "gap");
    if (!lua_isnil(L, -1)) {
        const bool OK = lua_isinteger(L, -1) && lua_tointeger(L, -1) >= 0;
        if (OK)
            style.gap = sc<int>(lua_tointeger(L, -1));
        lua_pop(L, 1);
        if (!OK)
            return fail(L, FN, "gap must be a whole number of pixels, 0 or more");
    } else
        lua_pop(L, 1);

    lua_getfield(L, 1, "background");
    if (!lua_isnil(L, -1)) {
        std::expected<int64_t, std::string> color = std::unexpected("not a string");
        if (lua_type(L, -1) == LUA_TSTRING)
            color = Config::ParserUtils::parseColor(lua_tostring(L, -1));
        lua_pop(L, 1);
        if (!color)
            return fail(L, FN, std::format("background must be a colour like \"rgba(8c938860)\": {}", color.error()));
        style.background = *color;
    } else
        lua_pop(L, 1);

    if (const auto RESULT = Weave::create(std::move(root), lock, style); !RESULT)
        return fail(L, FN, RESULT.error());

    lua_pushboolean(L, true);
    return 1;
}

// hl.plugin.hyprweave.lock({ window?, level })
static int luaLock(lua_State* L) {
    constexpr const char* FN = "hl.plugin.hyprweave.lock";

    if (!lua_istable(L, 1))
        return fail(L, FN, "expected a table");

    lua_getfield(L, 1, "level");
    const auto LEVEL = lua_isstring(L, -1) ? lockFromName(lua_tostring(L, -1)) : std::nullopt;
    lua_pop(L, 1);
    if (!LEVEL)
        return fail(L, FN, "level must be \"none\", \"layout\", \"members\" or \"full\"");

    if (const auto RESULT = Weave::setLock(targetWindow(L, FN), *LEVEL); !RESULT)
        return fail(L, FN, RESULT.error());

    lua_pushboolean(L, true);
    return 1;
}

// hl.plugin.hyprweave.dissolve({ window? })
static int luaDissolve(lua_State* L) {
    constexpr const char* FN = "hl.plugin.hyprweave.dissolve";

    if (const auto RESULT = Weave::dissolve(targetWindow(L, FN)); !RESULT)
        return fail(L, FN, RESULT.error());

    lua_pushboolean(L, true);
    return 1;
}

// hl.plugin.hyprweave.fullscreen({ window? })
static int luaFullscreen(lua_State* L) {
    constexpr const char* FN = "hl.plugin.hyprweave.fullscreen";

    if (const auto RESULT = Weave::toggleGroupFullscreen(targetWindow(L, FN)); !RESULT)
        return fail(L, FN, RESULT.error());

    lua_pushboolean(L, true);
    return 1;
}

// hl.plugin.hyprweave.get({ window? }) -> { lock, gap?, background?, direction, size, children } or nil
static int luaGet(lua_State* L) {
    constexpr const char* FN = "hl.plugin.hyprweave.get";

    auto* const STATE = findForWindow(targetWindow(L, FN));
    if (!STATE || !STATE->root) {
        lua_pushnil(L);
        return 1;
    }

    pushNode(L, *STATE->root, 100.0);
    lua_pushstring(L, lockName(STATE->lock));
    lua_setfield(L, -2, "lock");
    if (STATE->gap) {
        lua_pushinteger(L, *STATE->gap);
        lua_setfield(L, -2, "gap");
    }
    if (STATE->background) {
        // stored as ARGB, handed back the way it is written: rgba(RRGGBBAA)
        const auto ARGB = sc<uint32_t>(*STATE->background);
        lua_pushstring(L, std::format("rgba({:06x}{:02x})", ARGB & 0xFFFFFF, ARGB >> 24).c_str());
        lua_setfield(L, -2, "background");
    }
    return 1;
}

void Weave::registerLua() {
    HyprlandAPI::addLuaFunction(PHANDLE, "hyprweave", "create", ::luaCreate);
    HyprlandAPI::addLuaFunction(PHANDLE, "hyprweave", "lock", ::luaLock);
    HyprlandAPI::addLuaFunction(PHANDLE, "hyprweave", "dissolve", ::luaDissolve);
    HyprlandAPI::addLuaFunction(PHANDLE, "hyprweave", "get", ::luaGet);
    HyprlandAPI::addLuaFunction(PHANDLE, "hyprweave", "fullscreen", ::luaFullscreen);
}
