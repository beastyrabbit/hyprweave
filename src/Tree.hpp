#pragma once

#include "Internals.hpp"

namespace Weave {
    enum class eNodeType : uint8_t {
        WINDOW,
        ROW,    // children side by side
        COLUMN, // children stacked
    };

    // A flex-style layout tree. Containers split their box between their children by `size`,
    // which is a weight relative to the siblings (75 and 25, or 3 and 1, give the same split).
    struct SNode {
        eNodeType                           type = eNodeType::WINDOW;
        double                              size = 1.0;
        PHLWINDOWREF                        window;   // WINDOW only
        std::vector<std::unique_ptr<SNode>> children; // ROW and COLUMN only
    };

    struct SPlacement {
        PHLWINDOW          window;
        Layout::STargetBox box;
    };

    // Splits `box` across the tree. The logical boxes tile `box.logicalBox` exactly; Hyprland adds
    // gaps itself when the visual box is empty. If the layout supplied a visual box, it is split
    // too, with `innerGap` (x for rows, y for columns) between siblings.
    void        layout(const SNode& node, const Layout::STargetBox& box, const Vector2D& innerGap, std::vector<SPlacement>& out);

    // Drops leaves whose window is gone or not in `members`, then replaces containers that have a
    // single child by that child. Leaves `root` empty if nothing is left.
    void        prune(std::unique_ptr<SNode>& root, const std::vector<PHLWINDOW>& members);

    // Adds `w` as a new leaf of the root container with the average sibling size.
    void        append(std::unique_ptr<SNode>& root, PHLWINDOW w);

    bool        contains(const SNode& node, const PHLWINDOW& w);
    void        collectWindows(const SNode& node, std::vector<PHLWINDOW>& out);
    const char* typeName(eNodeType type);
}
