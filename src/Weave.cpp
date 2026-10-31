#include "Internals.hpp"
#include "Weave.hpp"
#include "Frame.hpp"
#include "globals.hpp"

#include <algorithm>
#include <format>

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/config/shared/workspace/WorkspaceRuleManager.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>

using namespace Weave;
using Desktop::View::CGroup;

static std::vector<std::unique_ptr<SGroup>> g_groups;

// ---------------------------------------------------------------------------------------------
// Registry

SGroup* Weave::find(const CGroup* group) {
    if (!group)
        return nullptr;

    std::erase_if(g_groups, [](const auto& g) { return !g->group; });

    for (const auto& g : g_groups) {
        if (g->group.get() == group)
            return g.get();
    }
    return nullptr;
}

SGroup* Weave::findForWindow(const PHLWINDOW& w) {
    if (!w)
        return nullptr;
    return find(w->grouping().group().get());
}

std::vector<SGroup*> Weave::all() {
    std::erase_if(g_groups, [](const auto& g) { return !g->group; });

    std::vector<SGroup*> out;
    for (const auto& g : g_groups)
        out.push_back(g.get());
    return out;
}

static std::vector<PHLWINDOW> members(const CGroup* group) {
    std::vector<PHLWINDOW> out;
    for (const auto& w : group->windows()) {
        if (const auto W = w.lock())
            out.push_back(W);
    }
    return out;
}

static void unregister(const CGroup* group) {
    for (const auto& g : g_groups) {
        if (g->group.get() != group)
            continue;
        damageFrame(*g);
        for (const auto& w : members(group))
            detachFrame(w);
    }
    std::erase_if(g_groups, [group](const auto& g) { return !g->group || g->group.get() == group; });
}

// ---------------------------------------------------------------------------------------------
// Hooks

static CFunctionHook* g_visibilityHook = nullptr;
static CFunctionHook* g_updatePosHook  = nullptr;
static CFunctionHook* g_canGroupHook   = nullptr;

typedef void (*origVisibility)(CGroup*);
typedef void (*origUpdatePos)(Layout::CWindowGroupTarget*, uint8_t);
typedef bool (*origCanGroup)(const Desktop::View::CWindowGroupMembership*, const SP<CGroup>&);

// A hyprweave group with one member left stops being a group.
static void dissolveLater(const CGroup* group) {
    g_pEventLoopManager->doLater([ref = group->m_self] {
        const auto GROUP = ref.lock();
        if (!GROUP || GROUP->size() > 1 || !find(GROUP.get()))
            return;
        unregister(GROUP.get());
        GROUP->destroy();
    });
}

// A fullscreen member covers the group's box alone, unless the whole group went fullscreen.
// Fullscreen state belongs to the group's current member.
static PHLWINDOW soloWindow(const SGroup& state) {
    const auto GROUP = state.group.lock();
    if (!GROUP || state.fullscreenGroup)
        return nullptr;

    const auto CURRENT = GROUP->current();
    return CURRENT && Fullscreen::controller()->isFullscreen(CURRENT) ? CURRENT : nullptr;
}

// Hides members the way the core hides inactive tabs; with no solo window every member shows.
static void applyVisibility(const std::vector<PHLWINDOW>& members, const PHLWINDOW& solo) {
    for (const auto& w : members) {
        const bool VISIBLE = !solo || w == solo;
        w->setInputBlocked(Desktop::View::FOCUS_BLOCK_GROUP_INACTIVE, !VISIBLE);
        *w->presentation().alpha(Desktop::View::WINDOW_ALPHA_LAYOUT) = VISIBLE ? 1.F : 0.F;
    }
}

// Replaces CGroup::updateWindowVisibility for hyprweave groups. The core hides every member but
// the current one; here every member stays visible and accepts input. This runs after every
// membership change, so it also brings the tree in line with the group's members.
static void hkUpdateWindowVisibility(CGroup* thisptr) {
    auto* const STATE = find(thisptr);
    if (!STATE) {
        (*(origVisibility)g_visibilityHook->m_original)(thisptr);
        return;
    }

    const auto MEMBERS = members(thisptr);

    prune(STATE->root, MEMBERS);
    for (const auto& w : MEMBERS) {
        if (!STATE->root || !contains(*STATE->root, w))
            append(STATE->root, w);
    }

    for (const auto& w : MEMBERS) {
        w->m_ruleApplicator->propertiesChanged(Desktop::Rule::RULE_PROP_GROUP | Desktop::Rule::RULE_PROP_ON_WORKSPACE);
        // nothing is hidden, so the tab bar has no job; the frame marks the group
        if (const auto BAR = w->presentation().decoration(DECORATION_GROUPBAR))
            w->presentation().removeDecoration(BAR.get());
        attachFrame(w);
        w->presentation().updateDecorations();
        w->presentation().refreshValues();
    }

    // recalc runs hkUpdatePos, which applies the visibility
    thisptr->m_target->recalc();

    for (const auto& w : MEMBERS)
        g_pHyprRenderer->damageWindow(w);

    if (MEMBERS.size() <= 1)
        dissolveLater(thisptr);
}

// general:gaps_in, or the workspace rule's.
static Config::CCssGapData gapsIn(Layout::CWindowGroupTarget* target) {
    static const auto PGAPSIN = CConfigValue<Config::IComplexConfigValue>("general:gaps_in");

    auto gaps = *sc<Config::CCssGapData*>(PGAPSIN.ptr());
    if (const auto WS = target->workspace()) {
        if (const auto RULE = Config::workspaceRuleMgr()->getWorkspaceRuleFor(WS); RULE && RULE->m_gapsIn)
            gaps = *RULE->m_gapsIn;
    }
    return gaps;
}

// Space between siblings when hyprweave places the members' visual boxes itself: the group's
// own gap, else plugin:hyprweave:gap, else what Hyprland puts between two tiles.
static Vector2D innerGap(const SGroup& state, Layout::CWindowGroupTarget* target) {
    const int GAP = state.gap.value_or(g_gap->value());
    if (GAP >= 0)
        return {sc<double>(GAP), sc<double>(GAP)};

    const auto GAPS = gapsIn(target);
    return {sc<double>(GAPS.m_left + GAPS.m_right), sc<double>(GAPS.m_top + GAPS.m_bottom)};
}

// Where a lone window's border would go in this group's slot: the layout box minus the gaps
// CWindowTarget::updatePos would add (none on edges at the work area's edge), or the visual box
// when the layout filled it. Floating windows get their box as is and draw the border outside.
static CBox borderBox(Layout::CWindowGroupTarget* target, double borderSize) {
    const auto& BOX = target->m_box;
    if (target->floating())
        return BOX.logicalBox.copy().expand(borderSize);
    if (!BOX.visualBox.empty() || !target->space())
        return BOX.visualBox.empty() ? BOX.logicalBox : BOX.visualBox;

    const auto  GAPS = gapsIn(target);
    const auto& L    = BOX.logicalBox;
    const auto& WA   = target->space()->workArea();

    const double LEFT   = STICKS(L.x, WA.x) ? 0 : GAPS.m_left;
    const double TOP    = STICKS(L.y, WA.y) ? 0 : GAPS.m_top;
    const double RIGHT  = STICKS(L.x + L.w, WA.x + WA.w) ? 0 : GAPS.m_right;
    const double BOTTOM = STICKS(L.y + L.h, WA.y + WA.h) ? 0 : GAPS.m_bottom;

    return {L.x + LEFT, L.y + TOP, L.w - LEFT - RIGHT, L.h - TOP - BOTTOM};
}

// The members fill the box a lone window would take, with its border ring included. With a
// frame, the frame takes that ring and the members (with their own borders) tile the inside,
// with half a gap of padding to the frame. Between members goes the group's gap.
static void layoutMembers(SGroup& state, Layout::CWindowGroupTarget* target, std::vector<SPlacement>& out) {
    const auto   CURRENT = state.group->current();
    const double BORDER  = CURRENT ? CURRENT->presentation().borderSize() : 0;
    const auto   GAP     = innerGap(state, target);
    const int    SIZE    = frameShown(state) ? g_frameSize->value() : 0;

    state.framePadding = SIZE > 0 ? GAP / 2.0 : Vector2D{};

    // the frame's inner edge, then the members' area inside the padding
    const auto FRAME = borderBox(target, BORDER).expand(-SIZE);
    const auto PAD   = state.framePadding;
    const CBox AREA  = {FRAME.x + PAD.x, FRAME.y + PAD.y, FRAME.w - 2 * PAD.x, FRAME.h - 2 * PAD.y};

    layout(*state.root, Layout::STargetBox{AREA, AREA}, GAP, out);

    for (auto& p : out) {
        // tiled members take their border out of the visual box; floating ones draw it outside
        // the logical box, so make room for it here
        p.box.logicalBox = p.box.visualBox;
        if (target->floating()) {
            const double OWN = p.window->presentation().borderSize();
            p.box.logicalBox.expand(-OWN);
            p.box.visualBox = p.box.logicalBox;
        }
    }
}

// Replaces CWindowGroupTarget::updatePos for hyprweave groups. The core gives every member the
// whole group box; here each member gets its slice of the tree.
static void hkUpdatePos(Layout::CWindowGroupTarget* thisptr, uint8_t flags) {
    const auto   GROUP = thisptr->m_group.lock();
    auto* const  STATE = find(GROUP.get());
    if (!STATE || !STATE->root) {
        (*(origUpdatePos)g_updatePosHook->m_original)(thisptr, flags);
        return;
    }

    const auto MEMBERS = members(GROUP.get());
    const auto SOLO    = soloWindow(*STATE);
    applyVisibility(MEMBERS, SOLO);

    if (SOLO) {
        // like a tab group: every member gets the whole box, only the solo one shows
        for (const auto& w : MEMBERS)
            w->windowTarget()->setPositionGlobal(thisptr->m_box, flags);
        return;
    }

    std::vector<SPlacement> placements;
    if (STATE->fullscreenGroup)
        // the core's fullscreen path takes the logical boxes as is
        layout(*STATE->root, thisptr->m_box, innerGap(*STATE, thisptr), placements);
    else
        layoutMembers(*STATE, thisptr, placements);

    for (const auto& p : placements)
        p.window->windowTarget()->setPositionGlobal(p.box, flags);

    // members that are not in the tree yet (between a core add and the next visibility update)
    for (const auto& w : MEMBERS) {
        if (!contains(*STATE->root, w))
            w->windowTarget()->setPositionGlobal(thisptr->m_box, flags);
    }
}

// Wraps CWindowGroupMembership::canBeGroupedInto. Hyprland adds a newly mapped window to the
// focused group; for hyprweave groups that only happens with plugin:hyprweave:auto_join.
// hyprweave groups take windows only through the API, and with auto_join a newly opened window
// when the group's lock lets members in. Every hyprweave group carries Hyprland's own group lock
// (see applyLock), which already keeps out moves and drags; here it is lifted for auto_join only.
static bool hkCanBeGroupedInto(const Desktop::View::CWindowGroupMembership* thisptr, const SP<CGroup>& group) {
    const auto ORIGINAL = [&] { return (*(origCanGroup)g_canGroupHook->m_original)(thisptr, group); };

    auto* const STATE = group ? find(group.get()) : nullptr;
    if (!STATE)
        return ORIGINAL();

    const bool NEW_WINDOW = !!(thisptr->m_window.m_state & Desktop::View::WINDOW_STATE_FIRST_MAP);
    if (!g_autoJoin->value() || !NEW_WINDOW || STATE->lock == eLock::MEMBERS || STATE->lock == eLock::FULL)
        return false;

    // the core's remaining checks (global lock, window rules) still apply
    group->setLocked(false);
    const bool ALLOWED = ORIGINAL();
    group->setLocked(true);
    return ALLOWED;
}

static CFunctionHook* hookByName(const char* name, const char* signature, void* destination) {
    for (const auto& fn : HyprlandAPI::findFunctionsByName(PHANDLE, name)) {
        if (fn.demangled == signature)
            return HyprlandAPI::createFunctionHook(PHANDLE, fn.address, destination);
    }
    return nullptr;
}

std::expected<void, std::string> Weave::initHooks() {
    struct {
        CFunctionHook** hook;
        const char*     name;
        const char*     signature;
        void*           destination;
    } hooks[] = {
        {&g_visibilityHook, "updateWindowVisibility", "Desktop::View::CGroup::updateWindowVisibility()", (void*)&hkUpdateWindowVisibility},
        {&g_updatePosHook, "updatePos", "Layout::CWindowGroupTarget::updatePos(unsigned char)", (void*)&hkUpdatePos},
        {&g_canGroupHook, "canBeGroupedInto",
         "Desktop::View::CWindowGroupMembership::canBeGroupedInto(Hyprutils::Memory::CSharedPointer<Desktop::View::CGroup> const&) const", (void*)&hkCanBeGroupedInto},
    };

    for (auto& h : hooks) {
        *h.hook = hookByName(h.name, h.signature, h.destination);
        if (!*h.hook)
            return std::unexpected(std::format("hook target not found: {}", h.signature));
    }

    for (auto& h : hooks) {
        if (!(*h.hook)->hook()) {
            for (auto& u : hooks)
                (*u.hook)->unhook();
            return std::unexpected(std::format("could not hook {}", h.signature));
        }
    }

    return {};
}

// ---------------------------------------------------------------------------------------------
// Operations

// The core lock is always on, whatever the level: it keeps Hyprland's moves, drags and
// `into_group` from adding windows, and it makes moving a member move the whole group.
static void applyLock(SGroup& state, eLock lock) {
    state.lock = lock;
    state.group->setLocked(true);
}

std::expected<SP<CGroup>, std::string> Weave::create(std::unique_ptr<SNode> root, eLock lock, SStyle style) {
    if (!root || root->type == eNodeType::WINDOW)
        return std::unexpected("the root must be a row or column");

    std::vector<PHLWINDOW> windows;
    collectWindows(*root, windows);

    if (windows.size() < 2)
        return std::unexpected("a group needs at least two windows");

    for (size_t i = 0; i < windows.size(); ++i) {
        if (!windows[i]->mapped())
            return std::unexpected(std::format("window 0x{:x} is not mapped", rc<uintptr_t>(windows[i].get())));
        if (std::find(windows.begin() + i + 1, windows.end(), windows[i]) != windows.end())
            return std::unexpected(std::format("window 0x{:x} appears twice", rc<uintptr_t>(windows[i].get())));
    }

    // leave whatever group each window is in, tabbed or woven
    for (const auto& w : windows) {
        if (const auto OLD = w->grouping().group())
            OLD->remove(w);
    }

    // Hyprland's own group actions always run with the window being grouped focused. With focus on
    // another window of the workspace, dwindle loses track of the group's tile while it forms
    // (the group ends up with an empty box). So form it the core's way and give focus back after.
    const auto FOCUSED = Desktop::focusState()->window();
    if (FOCUSED != windows.front())
        Desktop::focusState()->fullWindowFocus(windows.front(), Desktop::FOCUS_REASON_DESKTOP_STATE_CHANGE);

    const auto GROUP = CGroup::create({PHLWINDOWREF{windows.front()}});
    for (size_t i = 1; i < windows.size(); ++i)
        GROUP->add(windows[i]);

    auto state   = std::make_unique<SGroup>();
    state->group = GROUP;
    state->root  = std::move(root);
    state->gap        = style.gap;
    state->background = style.background;
    applyLock(*state, lock);
    g_groups.push_back(std::move(state));

    // the focused member is current (it owns fullscreen; see onWindowFocus), else the first one
    GROUP->setCurrent(std::ranges::contains(windows, FOCUSED) ? FOCUSED : windows.front());
    GROUP->updateWindowVisibility();

    if (FOCUSED && FOCUSED->mapped() && Desktop::focusState()->window() != FOCUSED)
        Desktop::focusState()->fullWindowFocus(FOCUSED, Desktop::FOCUS_REASON_DESKTOP_STATE_CHANGE);

    return GROUP;
}

std::expected<void, std::string> Weave::setLock(PHLWINDOW member, eLock lock) {
    auto* const STATE = findForWindow(member);
    if (!STATE)
        return std::unexpected("window is not in a hyprweave group");

    applyLock(*STATE, lock);
    STATE->group->updateWindowVisibility();
    return {};
}

std::expected<void, std::string> Weave::dissolve(PHLWINDOW member) {
    auto* const STATE = findForWindow(member);
    if (!STATE)
        return std::unexpected("window is not in a hyprweave group");

    const auto GROUP = STATE->group.lock();
    unregister(GROUP.get());
    GROUP->destroy();
    return {};
}

std::expected<void, std::string> Weave::toggleGroupFullscreen(PHLWINDOW member) {
    auto* const STATE = findForWindow(member);
    if (!STATE)
        return std::unexpected("window is not in a hyprweave group");

    const auto CURRENT = STATE->group->current();
    if (!CURRENT)
        return std::unexpected("the group has no current member");

    if (Fullscreen::controller()->isFullscreen(CURRENT)) {
        // leaving group fullscreen, or a member's own fullscreen; onWindowFullscreen clears the flag
        Fullscreen::controller()->setFullscreenMode(CURRENT, Fullscreen::FSMODE_NONE);
        return {};
    }

    STATE->fullscreenGroup = true;
    Fullscreen::controller()->setFullscreenMode(CURRENT, Fullscreen::FSMODE_FULLSCREEN);
    return {};
}

void Weave::onWindowFocus(PHLWINDOW w) {
    // the frame colour follows focus entering or leaving a group
    for (auto* const g : all())
        damageFrame(*g);

    auto* const STATE = findForWindow(w);
    if (!STATE || STATE->group->current() == w)
        return;

    // moving the current member moves fullscreen with it; leave a fullscreen group alone
    if (Fullscreen::controller()->isFullscreen(STATE->group->current()))
        return;

    // not from inside the focus change
    g_pEventLoopManager->doLater([ref = PHLWINDOWREF{w}] {
        const auto W = ref.lock();
        if (!W || Desktop::focusState()->window() != W)
            return;
        if (auto* const S = findForWindow(W); S && S->group->current() != W && !Fullscreen::controller()->isFullscreen(S->group->current()))
            S->group->setCurrent(W);
    });
}

void Weave::onWindowFullscreen(PHLWINDOW w) {
    auto* const STATE = findForWindow(w);
    if (!STATE)
        return;

    if (STATE->fullscreenGroup && !Fullscreen::controller()->isFullscreen(STATE->group->current()))
        STATE->fullscreenGroup = false;
}

void Weave::onWindowClose(PHLWINDOW w) {
    auto* const STATE = findForWindow(w);
    if (!STATE || STATE->lock == eLock::NONE || STATE->closing)
        return;

    STATE->closing = true;

    std::vector<PHLWINDOWREF> others;
    for (const auto& m : members(STATE->group.get())) {
        if (m != w)
            others.emplace_back(m);
    }

    // not from inside the closing window's unmap
    g_pEventLoopManager->doLater([others] {
        for (const auto& o : others) {
            if (const auto W = o.lock(); W && W->mapped())
                W->sendClose();
        }
    });
}

void Weave::releaseAll() {
    std::vector<SP<CGroup>> groups;
    for (const auto& g : g_groups) {
        damageFrame(*g);
        if (const auto G = g->group.lock())
            groups.push_back(G);
    }
    g_groups.clear();

    for (const auto& g : groups) {
        for (const auto& w : members(g.get())) {
            detachFrame(w);
            if (!w->presentation().decoration(DECORATION_GROUPBAR))
                g->applyWindowDecosAndUpdates(w);
        }
        g->updateWindowVisibility();
    }
}

const char* Weave::lockName(eLock lock) {
    switch (lock) {
        case eLock::NONE: return "none";
        case eLock::LAYOUT: return "layout";
        case eLock::MEMBERS: return "members";
        case eLock::FULL: return "full";
    }
    return "none";
}

std::optional<eLock> Weave::lockFromName(const std::string& name) {
    for (auto l : {eLock::NONE, eLock::LAYOUT, eLock::MEMBERS, eLock::FULL}) {
        if (name == lockName(l))
            return l;
    }
    return std::nullopt;
}
