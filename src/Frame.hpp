#pragma once

#include "Weave.hpp"

#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>

namespace Weave {
    // Draws one border around a whole hyprweave group, where a lone window's border would be.
    // Every member carries one so the frame survives any member leaving; only the first member
    // in the tree draws. It reserves no space: hkUpdatePos insets the members to make room.
    class CFrameDecoration : public IHyprWindowDecoration {
      public:
        CFrameDecoration(PHLWINDOW window);
        virtual ~CFrameDecoration() = default;

        virtual SDecorationPositioningInfo getPositioningInfo() override;
        virtual void                       onPositioningReply(const SDecorationPositioningReply& reply) override;
        virtual void                       draw(Render::CRenderContext& ctx, PHLMONITOR, float const& a, const Render::SWindowRenderPresentation& presentation) override;
        virtual eDecorationType            getDecorationType() override;
        virtual void                       updateWindow(PHLWINDOW) override;
        virtual void                       damageEntire() override;
        virtual eDecorationLayer           getDecorationLayer() override;
        virtual uint64_t                   getDecorationFlags() override;
        virtual std::string                getDisplayName() override;

      private:
        PHLWINDOWREF m_window;
    };

    // Stands in for the tab bar, which a hyprweave group does not show. Hyprland expects every
    // member of a group to have one: CGroup::remove calls it without a check when
    // group:groupbar:disable_when_only is set. Draws nothing and takes no room.
    class CBarStub : public IHyprWindowDecoration {
      public:
        CBarStub(PHLWINDOW window);
        virtual ~CBarStub() = default;

        virtual SDecorationPositioningInfo getPositioningInfo() override;
        virtual void                       onPositioningReply(const SDecorationPositioningReply& reply) override;
        virtual void                       draw(Render::CRenderContext& ctx, PHLMONITOR, float const& a, const Render::SWindowRenderPresentation& presentation) override;
        virtual eDecorationType            getDecorationType() override;
        virtual void                       updateWindow(PHLWINDOW) override;
        virtual void                       damageEntire() override;
        virtual uint64_t                   getDecorationFlags() override;
        virtual std::string                getDisplayName() override;
    };

    void attachFrame(PHLWINDOW w);
    void detachFrame(PHLWINDOW w);

    // Swaps the window's tab bar for a CBarStub, and back.
    void hideBar(PHLWINDOW w);
    void restoreBar(PHLWINDOW w, Desktop::View::CGroup& group);

    // Whether the group draws a frame right now (plugin:hyprweave:frame_size > 0, more than one
    // member visible, not fullscreen).
    bool frameShown(const SGroup& state);

    // Damages where the frame was last drawn, e.g. after a focus change or when it goes away.
    void damageFrame(SGroup& state);
}
