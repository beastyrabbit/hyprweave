#include "Internals.hpp"
#include "Frame.hpp"
#include "globals.hpp"

#include <algorithm>

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>

using namespace Weave;

CFrameDecoration::CFrameDecoration(PHLWINDOW window) : IHyprWindowDecoration(window), m_window(window) {
    ;
}

SDecorationPositioningInfo CFrameDecoration::getPositioningInfo() {
    // no edges and no extents: the frame takes no room from the window and pushes no other
    // decoration around; it damages itself instead (see draw)
    SDecorationPositioningInfo info;
    info.policy   = DECORATION_POSITION_ABSOLUTE;
    info.edges    = 0;
    info.reserved = false;
    return info;
}

void CFrameDecoration::onPositioningReply(const SDecorationPositioningReply& reply) {
    ;
}

// The first member in the tree draws the frame.
static PHLWINDOW frameHost(const SGroup& state) {
    if (!state.root)
        return nullptr;

    std::vector<PHLWINDOW> windows;
    collectWindows(*state.root, windows);
    return windows.empty() ? nullptr : windows.front();
}

// The group shows as a group: more than one member and nothing fullscreen.
static bool groupShown(const SGroup& state) {
    const auto GROUP = state.group.lock();
    if (!GROUP || !state.root || GROUP->size() < 2)
        return false;

    const auto CURRENT = GROUP->current();
    return !CURRENT || !Fullscreen::controller()->isFullscreen(CURRENT);
}

bool Weave::frameShown(const SGroup& state) {
    return g_frameSize->value() > 0 && groupShown(state);
}

// The frame's inner edge, global and unscaled: the members' current boxes with their borders,
// grown by the padding. Following the current (animated) geometry keeps the frame on the
// members while they move.
static CBox innerBox(const SGroup& state) {
    double x1 = INFINITY, y1 = INFINITY, x2 = -INFINITY, y2 = -INFINITY;

    for (const auto& ref : state.group->windows()) {
        const auto W = ref.lock();
        if (!W || !W->mapped())
            continue;

        const auto BOX = W->getWindowMainSurfaceBox().expand(W->presentation().borderSize());
        x1             = std::min(x1, BOX.x);
        y1             = std::min(y1, BOX.y);
        x2             = std::max(x2, BOX.x + BOX.w);
        y2             = std::max(y2, BOX.y + BOX.h);
    }

    if (x1 >= x2 || y1 >= y2)
        return {};

    const auto PAD = state.framePadding;
    return {x1 - PAD.x, y1 - PAD.y, x2 - x1 + 2 * PAD.x, y2 - y1 + 2 * PAD.y};
}

void Weave::damageFrame(SGroup& state) {
    if (state.lastFrame.empty())
        return;

    g_pHyprRenderer->damageBox(state.lastFrame.copy().expand(2));
}

void CFrameDecoration::draw(Render::CRenderContext& ctx, PHLMONITOR pMonitor, float const& a, const Render::SWindowRenderPresentation& presentation) {
    const auto  W     = m_window.lock();
    auto* const STATE = findForWindow(W);
    if (!STATE || !groupShown(*STATE))
        return;

    const auto INNER = innerBox(*STATE);
    if (INNER.empty())
        return;

    const auto TOMONITOR     = [&](CBox box) { return box.translate(presentation.workspaceOffset + presentation.floatingOffset - pMonitor->m_position).scale(pMonitor->m_scale).round(); };
    const auto ROUNDINGPOWER = W->presentation().roundingPower();
    // like Hyprland's border: a curve `d` further out has radius r + d, less a correction for
    // rounding_power below 2, so nested curves stay parallel
    const auto OUTWARD = [&](double radius, double d) { return radius + d - d * (M_SQRT2 - 1) * std::max(2.0 - ROUNDINGPOWER, 0.0); };
    // the frame's inner edge follows the members' border curves, padding further out
    const auto PAD      = std::min(STATE->framePadding.x, STATE->framePadding.y);
    const auto INNERRAD = OUTWARD(W->presentation().rounding(), W->presentation().borderSize() + PAD);

    // the background goes under every member, so the first member drawn in this frame paints it
    if (CHyprColor bg{sc<uint64_t>(STATE->background.value_or(g_background->value()))}; bg.a > 0 && STATE->backgroundSeq != g_renderSeq) {
        STATE->backgroundSeq = g_renderSeq;
        bg.a *= a;

        CRectPassElement::SRectData rect;
        rect.box           = TOMONITOR(INNER);
        rect.color         = bg;
        rect.round         = INNERRAD * pMonitor->m_scale;
        rect.roundingPower = ROUNDINGPOWER;
        g_pHyprRenderer->addPassElement(ctx, makeUnique<CRectPassElement>(rect));
    }

    if (frameHost(*STATE) != W)
        return;

    // frame and background are not part of any window's extents, so they damage their own old
    // and new place
    const int  SIZE  = frameShown(*STATE) ? g_frameSize->value() : 0;
    const auto OUTER = INNER.copy().expand(SIZE);
    if (OUTER != STATE->lastFrame) {
        damageFrame(*STATE);
        STATE->lastFrame = OUTER;
        damageFrame(*STATE);
    }

    if (SIZE <= 0)
        return;

    const auto FOCUSED = Desktop::focusState()->window();
    const bool ACTIVE  = FOCUSED && FOCUSED->grouping().group() == STATE->group.lock();

    CBorderPassElement::SBorderData data;
    data.box           = TOMONITOR(INNER);
    data.grad1         = ACTIVE ? g_frameActive->value() : g_frameInactive->value();
    data.round         = INNERRAD * pMonitor->m_scale;
    data.outerRound    = OUTWARD(INNERRAD, SIZE) * pMonitor->m_scale;
    data.roundingPower = ROUNDINGPOWER;
    data.a             = a;
    data.borderSize    = SIZE;
    data.window        = W;

    g_pHyprRenderer->addPassElement(ctx, makeUnique<CBorderPassElement>(data));
}

eDecorationType CFrameDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CFrameDecoration::updateWindow(PHLWINDOW) {
    ;
}

void CFrameDecoration::damageEntire() {
    if (auto* const STATE = findForWindow(m_window.lock()))
        damageFrame(*STATE);
}

eDecorationLayer CFrameDecoration::getDecorationLayer() {
    return DECORATION_LAYER_BOTTOM;
}

uint64_t CFrameDecoration::getDecorationFlags() {
    return DECORATION_NON_SOLID;
}

std::string CFrameDecoration::getDisplayName() {
    return "hyprweave frame";
}

static CFrameDecoration* frameOf(const PHLWINDOW& w) {
    for (const auto& d : w->presentation().decorations()) {
        if (auto* const FRAME = dynamic_cast<CFrameDecoration*>(d.get()))
            return FRAME;
    }
    return nullptr;
}

void Weave::attachFrame(PHLWINDOW w) {
    if (!frameOf(w))
        HyprlandAPI::addWindowDecoration(PHANDLE, w, makeShared<CFrameDecoration>(w));
}

void Weave::detachFrame(PHLWINDOW w) {
    if (auto* const FRAME = frameOf(w))
        HyprlandAPI::removeWindowDecoration(PHANDLE, FRAME);
}

CBarStub::CBarStub(PHLWINDOW window) : IHyprWindowDecoration(window) {
    ;
}

SDecorationPositioningInfo CBarStub::getPositioningInfo() {
    SDecorationPositioningInfo info;
    info.policy   = DECORATION_POSITION_ABSOLUTE;
    info.edges    = 0;
    info.reserved = false;
    return info;
}

void CBarStub::onPositioningReply(const SDecorationPositioningReply& reply) {
    ;
}

void CBarStub::draw(Render::CRenderContext& ctx, PHLMONITOR, float const& a, const Render::SWindowRenderPresentation& presentation) {
    ;
}

eDecorationType CBarStub::getDecorationType() {
    return DECORATION_GROUPBAR;
}

void CBarStub::updateWindow(PHLWINDOW) {
    ;
}

void CBarStub::damageEntire() {
    ;
}

uint64_t CBarStub::getDecorationFlags() {
    return DECORATION_NON_SOLID;
}

std::string CBarStub::getDisplayName() {
    return "hyprweave tab bar stand-in";
}

void Weave::hideBar(PHLWINDOW w) {
    bool                                stubbed = false;
    std::vector<IHyprWindowDecoration*> bars;
    for (const auto& d : w->presentation().decorations()) {
        if (dynamic_cast<CBarStub*>(d.get()))
            stubbed = true;
        else if (d->getDecorationType() == DECORATION_GROUPBAR)
            bars.push_back(d.get());
    }
    for (auto* const b : bars)
        w->presentation().removeDecoration(b);
    if (!stubbed)
        HyprlandAPI::addWindowDecoration(PHANDLE, w, makeShared<CBarStub>(w));
}

void Weave::restoreBar(PHLWINDOW w, Desktop::View::CGroup& group) {
    std::vector<IHyprWindowDecoration*> stubs;
    for (const auto& d : w->presentation().decorations()) {
        if (dynamic_cast<CBarStub*>(d.get()))
            stubs.push_back(d.get());
    }
    for (auto* const s : stubs)
        HyprlandAPI::removeWindowDecoration(PHANDLE, s);
    w->presentation().updateDecorations();
    group.applyWindowDecosAndUpdates(w);
}
