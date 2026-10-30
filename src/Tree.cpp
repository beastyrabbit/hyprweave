#include "Tree.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

using namespace Weave;

struct SSpan {
    double pos = 0;
    double len = 0;
};

// Splits [start, start + length) into parts weighted by `weights`, with `gap` between parts.
// Edges are rounded to whole pixels so neighbours share an edge without seams or overlap.
static std::vector<SSpan> split(double start, double length, const std::vector<double>& weights, double gap) {
    const double AVAILABLE = std::max(0.0, length - gap * (weights.size() - 1));
    const double TOTAL     = std::accumulate(weights.begin(), weights.end(), 0.0);

    std::vector<SSpan> spans;
    double             acc = 0;
    for (size_t i = 0; i < weights.size(); ++i) {
        const double FROM = std::round(AVAILABLE * acc / TOTAL);
        acc += weights[i];
        const double TO = std::round(AVAILABLE * acc / TOTAL);
        spans.push_back({start + FROM + i * gap, TO - FROM});
    }
    return spans;
}

void Weave::layout(const SNode& node, const Layout::STargetBox& box, const Vector2D& innerGap, std::vector<SPlacement>& out) {
    if (node.type == eNodeType::WINDOW) {
        if (const auto W = node.window.lock())
            out.push_back({W, box});
        return;
    }

    if (node.children.empty())
        return;

    std::vector<double> weights;
    for (const auto& c : node.children)
        weights.push_back(c->size);

    const bool ROW       = node.type == eNodeType::ROW;
    const auto LOGICAL   = box.logicalBox;
    const auto VISUAL    = box.visualBox;
    const bool HASVISUAL = !VISUAL.empty();

    const auto LSPANS = ROW ? split(LOGICAL.x, LOGICAL.w, weights, 0) : split(LOGICAL.y, LOGICAL.h, weights, 0);
    const auto VSPANS = !HASVISUAL ? std::vector<SSpan>{} : ROW ? split(VISUAL.x, VISUAL.w, weights, innerGap.x) : split(VISUAL.y, VISUAL.h, weights, innerGap.y);

    for (size_t i = 0; i < node.children.size(); ++i) {
        Layout::STargetBox child;
        child.logicalBox = ROW ? CBox{LSPANS[i].pos, LOGICAL.y, LSPANS[i].len, LOGICAL.h} : CBox{LOGICAL.x, LSPANS[i].pos, LOGICAL.w, LSPANS[i].len};
        if (HASVISUAL)
            child.visualBox = ROW ? CBox{VSPANS[i].pos, VISUAL.y, VSPANS[i].len, VISUAL.h} : CBox{VISUAL.x, VSPANS[i].pos, VISUAL.w, VSPANS[i].len};

        layout(*node.children[i], child, innerGap, out);
    }
}

static bool keep(SNode& node, const std::vector<PHLWINDOW>& members) {
    if (node.type == eNodeType::WINDOW) {
        const auto W = node.window.lock();
        return W && std::ranges::contains(members, W);
    }

    std::erase_if(node.children, [&](auto& c) { return !keep(*c, members); });
    return !node.children.empty();
}

static void collapse(std::unique_ptr<SNode>& node) {
    for (auto& c : node->children)
        collapse(c);

    if (node->type != eNodeType::WINDOW && node->children.size() == 1) {
        auto child  = std::move(node->children.front());
        child->size = node->size;
        node        = std::move(child);
    }
}

void Weave::prune(std::unique_ptr<SNode>& root, const std::vector<PHLWINDOW>& members) {
    if (!root)
        return;

    if (!keep(*root, members)) {
        root.reset();
        return;
    }

    collapse(root);
}

void Weave::append(std::unique_ptr<SNode>& root, PHLWINDOW w) {
    auto leaf    = std::make_unique<SNode>();
    leaf->type   = eNodeType::WINDOW;
    leaf->window = w;

    if (!root) {
        root = std::move(leaf);
        return;
    }

    if (root->type == eNodeType::WINDOW) {
        auto row  = std::make_unique<SNode>();
        row->type = eNodeType::ROW;
        row->size = root->size;
        root->size = 1.0;
        row->children.push_back(std::move(root));
        root = std::move(row);
    }

    double total = 0;
    for (const auto& c : root->children)
        total += c->size;
    leaf->size = root->children.empty() ? 1.0 : total / root->children.size();

    root->children.push_back(std::move(leaf));
}

bool Weave::contains(const SNode& node, const PHLWINDOW& w) {
    if (node.type == eNodeType::WINDOW)
        return node.window.lock() == w;

    return std::ranges::any_of(node.children, [&](const auto& c) { return contains(*c, w); });
}

void Weave::collectWindows(const SNode& node, std::vector<PHLWINDOW>& out) {
    if (node.type == eNodeType::WINDOW) {
        if (const auto W = node.window.lock())
            out.push_back(W);
        return;
    }

    for (const auto& c : node.children)
        collectWindows(*c, out);
}

const char* Weave::typeName(eNodeType type) {
    switch (type) {
        case eNodeType::WINDOW: return "window";
        case eNodeType::ROW: return "row";
        case eNodeType::COLUMN: return "column";
    }
    return "unknown";
}
