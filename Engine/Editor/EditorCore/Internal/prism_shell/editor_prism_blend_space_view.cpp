// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_prism_blend_space_view.h"

#include "editor.h"
#include "editor_prism_anim_graph_panel.h"

#include "animation_graph_editing.h"

#include "prism/Application.h"
#include "prism/Canvas.h"
#include "prism/Font.h"
#include "prism/Menu.h"
#include "prism/Theme.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKindUVE;
using NodeUVE = Scene::AnimationGraphNodeUVE;

const prism::Color kPointUVE{230, 180, 70};
const prism::Color kCrossUVE{236, 86, 86};

[[nodiscard]] std::string NumberTextUVE(float v) {
    if (std::abs(v) < 1.0e-5F) {
        v = 0.0F;   // never "-0.00"
    }
    char text[24];
    std::snprintf(text, sizeof(text), std::abs(v) >= 10.0F ? "%.0f" : "%.2f", static_cast<double>(v));
    return text;
}

} // namespace

const NodeUVE* BlendSpaceViewUVE::SpaceUVE() const {
    const Scene::AnimationGraphComponentUVE* graph = m_panel.GraphPtrUVE();
    if (graph == nullptr) {
        return nullptr;
    }
    const auto found = std::ranges::find(graph->nodes, m_panel.m_editor.m_animGraph.focus, &NodeUVE::id);
    return found != graph->nodes.end() && (found->kind == Kind::BlendSpace1D || found->kind == Kind::BlendSpace2D) ? &*found : nullptr;
}

std::size_t BlendSpaceViewUVE::SpaceIndexUVE() const {
    const Scene::AnimationGraphComponentUVE* graph = m_panel.GraphPtrUVE();
    if (graph == nullptr) {
        return static_cast<std::size_t>(-1);
    }
    const auto found = std::ranges::find(graph->nodes, m_panel.m_editor.m_animGraph.focus, &NodeUVE::id);
    return found != graph->nodes.end() ? static_cast<std::size_t>(found - graph->nodes.begin()) : static_cast<std::size_t>(-1);
}

prism::Rect BlendSpaceViewUVE::PlotUVE() const noexcept {
    return prism::Rect{m_rect.x + 56.0F, m_rect.y + 24.0F, std::max(40.0F, m_rect.w - 84.0F), std::max(40.0F, m_rect.h - 80.0F)};
}

prism::Vec2 BlendSpaceViewUVE::ToScreenUVE(const NodeUVE& space, const Math::Vector2UVE at) const {
    const prism::Rect plot = PlotUVE();
    const float sx = space.areaMax.x - space.areaMin.x;
    const float sy = space.areaMax.y - space.areaMin.y;
    const float x = plot.x + (at.x - space.areaMin.x) / (std::abs(sx) > 1.0e-6F ? sx : 1.0F) * plot.w;
    const float y = space.kind == Kind::BlendSpace1D ? plot.y + plot.h * 0.5F
                                                     : plot.bottom() - (at.y - space.areaMin.y) / (std::abs(sy) > 1.0e-6F ? sy : 1.0F) * plot.h;
    return prism::Vec2{x, y};
}

Math::Vector2UVE BlendSpaceViewUVE::ToSpaceUVE(const NodeUVE& space, const prism::Vec2 screen, const bool snap) const {
    const prism::Rect plot = PlotUVE();
    Math::Vector2UVE at{space.areaMin.x + (screen.x - plot.x) / plot.w * (space.areaMax.x - space.areaMin.x),
                        space.kind == Kind::BlendSpace1D ? 0.0F : space.areaMin.y + (plot.bottom() - screen.y) / plot.h * (space.areaMax.y - space.areaMin.y)};
    at.x = std::clamp(at.x, std::min(space.areaMin.x, space.areaMax.x), std::max(space.areaMin.x, space.areaMax.x));
    at.y = std::clamp(at.y, std::min(space.areaMin.y, space.areaMax.y), std::max(space.areaMin.y, space.areaMax.y));
    const EditorUVE::AnimationGraphViewStateUVE& v = m_panel.m_editor.m_animGraph;
    if (snap && v.snap && v.snapStep > 1.0e-4F) {
        at.x = std::round(at.x / v.snapStep) * v.snapStep;
        at.y = std::round(at.y / v.snapStep) * v.snapStep;
    }
    return at;
}

int BlendSpaceViewUVE::PointAtUVE(const NodeUVE& space, const prism::Vec2 screen) const {
    int best = -1;
    float bestDistance = 10.0F;
    for (std::size_t i = 0U; i < space.blendPoints.size(); ++i) {
        const prism::Vec2 p = ToScreenUVE(space, space.blendPoints[i].position);
        const float d = std::hypot(p.x - screen.x, p.y - screen.y);
        if (d < bestDistance) {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

Math::Vector2UVE BlendSpaceViewUVE::CrossUVE(const NodeUVE& space) const {
    Math::Vector2UVE at{space.value, space.valueY};
    if (const Scene::AnimationGraphComponentUVE* graph = m_panel.GraphPtrUVE()) {
        for (const Scene::AnimationParameterUVE& p : graph->parameters) {
            if (!space.parameter.empty() && p.name == space.parameter) {
                at.x = p.value;
            }
            if (!space.parameterY.empty() && p.name == space.parameterY) {
                at.y = p.value;
            }
        }
    }
    return at;
}

void BlendSpaceViewUVE::SetCrossUVE(const NodeUVE& space, const Math::Vector2UVE at) {
    // The parameters when they drive it (what a script would set), else the node's own values.
    const Scene::AnimationGraphComponentUVE* graph = m_panel.GraphPtrUVE();
    bool xDone = false;
    bool yDone = space.kind == Kind::BlendSpace1D;
    if (graph != nullptr) {
        for (std::size_t i = 0U; i < graph->parameters.size(); ++i) {
            if (!space.parameter.empty() && graph->parameters[i].name == space.parameter) {
                m_panel.LiveParameterUVE(i, at.x);
                xDone = true;
            } else if (!yDone && !space.parameterY.empty() && graph->parameters[i].name == space.parameterY) {
                m_panel.LiveParameterUVE(i, at.y);
                yDone = true;
            }
        }
    }
    if (!xDone || !yDone) {
        m_panel.LiveNodeUVE(space.id, [at, xDone, yDone](NodeUVE& n) {
            if (!xDone) {
                n.value = at.x;
            }
            if (!yDone) {
                n.valueY = at.y;
            }
        });
    }
}

// ---------------------------------------------------------------------------------------- paint

void BlendSpaceViewUVE::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    c.fillRect(m_rect, prism::Color(26, 28, 33));
    const NodeUVE* space = SpaceUVE();
    if (space == nullptr) {
        return;
    }
    const EditorUVE::AnimationGraphViewStateUVE& v = m_panel.m_editor.m_animGraph;
    const bool two = space->kind == Kind::BlendSpace2D;
    const prism::Rect plot = PlotUVE();
    c.pushClip(m_rect);
    c.fillRoundRect(plot.inflated(1.0F), 4.0F, prism::Color(34, 37, 44));
    c.strokeRoundRect(plot.inflated(1.0F), 4.0F, t.border, 1);
    // Grid: the snap step when it is fine enough to read, else tenths of the area; labelled axes.
    const auto gridStep = [&v](const float span) {
        float step = v.snap && v.snapStep > 1.0e-4F ? v.snapStep : span / 10.0F;
        while (span / step > 24.0F) {
            step *= 2.0F;
        }
        return std::max(step, 1.0e-4F);
    };
    {
        const float lo = std::min(space->areaMin.x, space->areaMax.x);
        const float hi = std::max(space->areaMin.x, space->areaMax.x);
        const float step = gridStep(hi - lo);
        for (float x = std::ceil(lo / step) * step; x <= hi + 1.0e-4F; x += step) {
            const float sx = ToScreenUVE(*space, Math::Vector2UVE{x, space->areaMin.y}).x;
            c.fillRect(prism::Rect{sx, plot.y, 1.0F, plot.h}, std::abs(x) < step * 0.01F ? prism::Color(255, 255, 255, 45) : prism::Color(255, 255, 255, 12));
            c.textIn(*t.small, prism::Rect{sx - 24.0F, plot.bottom() + 4.0F, 48.0F, 14.0F}, NumberTextUVE(x), t.textDim, prism::HAlign::Center, false);
        }
        c.textIn(*t.small, prism::Rect{plot.x, plot.bottom() + 18.0F, plot.w, 14.0F},
                 space->parameter.empty() ? std::string{"X"} : space->parameter, t.text, prism::HAlign::Center);
    }
    if (two) {
        const float lo = std::min(space->areaMin.y, space->areaMax.y);
        const float hi = std::max(space->areaMin.y, space->areaMax.y);
        const float step = gridStep(hi - lo);
        for (float y = std::ceil(lo / step) * step; y <= hi + 1.0e-4F; y += step) {
            const float sy = ToScreenUVE(*space, Math::Vector2UVE{space->areaMin.x, y}).y;
            c.fillRect(prism::Rect{plot.x, sy, plot.w, 1.0F}, std::abs(y) < step * 0.01F ? prism::Color(255, 255, 255, 45) : prism::Color(255, 255, 255, 12));
            c.textIn(*t.small, prism::Rect{plot.x - 52.0F, sy - 7.0F, 46.0F, 14.0F}, NumberTextUVE(y), t.textDim, prism::HAlign::Right, false);
        }
        c.textIn(*t.small, prism::Rect{m_rect.x + 4.0F, plot.y - 20.0F, 120.0F, 14.0F},
                 space->parameterY.empty() ? std::string{"Y"} : space->parameterY, t.text);
    } else {
        c.fillRect(prism::Rect{plot.x, plot.y + plot.h * 0.5F, plot.w, 1.0F}, prism::Color(255, 255, 255, 40));
    }

    // The runtime's triangles and weights, when the preview has run it.
    const Scene::AnimationGraphComponentUVE* graph = m_panel.GraphPtrUVE();
    const std::size_t index = SpaceIndexUVE();
    const Scene::AnimationGraphNodeStateUVE* run =
        graph != nullptr && graph->nodeStates.size() == graph->nodes.size() && index < graph->nodeStates.size() ? &graph->nodeStates[index] : nullptr;
    if (two && run != nullptr && run->triangulatedPoints.size() == space->blendPoints.size()) {
        const Math::Vector2UVE cross = CrossUVE(*space);
        for (const std::array<std::uint32_t, 3>& tri : run->triangles) {
            if (tri[0] >= space->blendPoints.size() || tri[1] >= space->blendPoints.size() || tri[2] >= space->blendPoints.size()) {
                continue;
            }
            const prism::Vec2 a = ToScreenUVE(*space, space->blendPoints[tri[0]].position);
            const prism::Vec2 b = ToScreenUVE(*space, space->blendPoints[tri[1]].position);
            const prism::Vec2 d = ToScreenUVE(*space, space->blendPoints[tri[2]].position);
            // Lit when the cross is inside it.
            const prism::Vec2 p = ToScreenUVE(*space, cross);
            const auto side = [](const prism::Vec2 o, const prism::Vec2 q, const prism::Vec2 r) { return (q.x - o.x) * (r.y - o.y) - (q.y - o.y) * (r.x - o.x); };
            const float s1 = side(a, b, p);
            const float s2 = side(b, d, p);
            const float s3 = side(d, a, p);
            const bool inside = (s1 >= 0 && s2 >= 0 && s3 >= 0) || (s1 <= 0 && s2 <= 0 && s3 <= 0);
            if (inside) {
                const prism::Vec2 fill[] = {a, b, d};
                c.fillPolygon(fill, prism::Color(86, 156, 236, 46));
            }
            const prism::Vec2 outline[] = {a, b, d};
            c.polyline(outline, inside ? prism::Color(120, 180, 250, 200) : prism::Color(255, 255, 255, 40), 1.0F, true);
        }
    } else if (!two && space->blendPoints.size() > 1U) {
        for (std::size_t i = 1U; i < space->blendPoints.size(); ++i) {
            c.line(ToScreenUVE(*space, space->blendPoints[i - 1U].position), ToScreenUVE(*space, space->blendPoints[i].position),
                   prism::Color(255, 255, 255, 50), 2.0F);
        }
    }

    // The points: each its animation, sized by its weight now.
    for (std::size_t i = 0U; i < space->blendPoints.size(); ++i) {
        const Scene::AnimationBlendPointUVE& point = space->blendPoints[i];
        Math::Vector2UVE at = point.position;
        if (m_drag == DragUVE::Point && m_dragPoint == static_cast<int>(i)) {
            at = ToSpaceUVE(*space, m_mouse, m_snap);
        }
        const prism::Vec2 p = ToScreenUVE(*space, at);
        const float weight = run != nullptr && i < run->pointWeights.size() ? run->pointWeights[i] : 0.0F;
        if (weight > 0.001F) {
            c.fillCircle(p, 7.0F + 14.0F * weight, prism::Color(230, 180, 70, 50));
        }
        const bool picked = v.pickedState == static_cast<int>(i);
        c.fillCircle(p, picked ? 7.0F : 5.5F, picked ? prism::Color(255, 226, 120) : kPointUVE);
        c.strokeCircle(p, picked ? 7.0F : 5.5F, prism::Color(20, 20, 24), 1.5F);
        std::string label = m_panel.m_editor.AnimationClipNameUVE(point.clip);
        if (label.empty()) {
            label = "(no animation)";
        }
        if (weight > 0.001F) {
            label += "  " + std::to_string(static_cast<int>(std::lround(weight * 100.0F))) + "%";
        }
        c.textIn(*t.small, prism::Rect{p.x + 9.0F, p.y - 16.0F, 180.0F, 14.0F}, label, picked ? t.textBright : t.text);
    }
    // The position cross.
    const prism::Vec2 cross = ToScreenUVE(*space, CrossUVE(*space));
    c.line(prism::Vec2{cross.x - 9.0F, cross.y}, prism::Vec2{cross.x + 9.0F, cross.y}, kCrossUVE, 2.0F);
    c.line(prism::Vec2{cross.x, cross.y - 9.0F}, prism::Vec2{cross.x, cross.y + 9.0F}, kCrossUVE, 2.0F);
    c.strokeCircle(cross, 5.0F, kCrossUVE, 1.5F);
    static constexpr const char* kTools[] = {"Select: drag a point, or the cross on empty space", "Add: click to put a point",
                                             "Remove: click a point"};
    c.textIn(*t.small, prism::Rect{m_rect.x + 10.0F, m_rect.bottom() - 20.0F, m_rect.w - 20.0F, 16.0F},
             std::string{kTools[std::clamp(v.spaceTool, 0, 2)]} + "   Shift: no snap   Esc: back to the tree", t.textDisabled);
    c.popClip();
}

// ---------------------------------------------------------------------------------------- input

bool BlendSpaceViewUVE::onMouseDown(const prism::MouseEvent& e) {
    if (prism::Application::exists()) {
        prism::Application::get().setFocus(this);
    }
    const NodeUVE* space = SpaceUVE();
    if (space == nullptr) {
        return true;
    }
    EditorUVE::AnimationGraphViewStateUVE& v = m_panel.m_editor.m_animGraph;
    m_mouse = e.pos;
    m_snap = (e.mods & prism::ModShift) == 0U;
    const int point = PointAtUVE(*space, e.pos);
    if (e.button == prism::MouseButton::Right) {
        if (point >= 0) {
            ShowPointMenuUVE(e.pos, point);
        }
        return true;
    }
    if (e.button != prism::MouseButton::Left) {
        return false;
    }
    const std::uint32_t id = space->id;
    switch (v.spaceTool) {
        case 1:   // add
            if (point < 0 && PlotUVE().inflated(6.0F).contains(e.pos)) {
                AskClipUVE(e.pos, ToSpaceUVE(*space, e.pos, m_snap));
            }
            break;
        case 2:   // remove
            if (point >= 0) {
                m_panel.EditUVE([id, point](Scene::AnimationGraphComponentUVE& g) {
                    static_cast<void>(RemoveBlendSpacePointUVE(g.nodes, id, static_cast<std::size_t>(point)));
                });
                v.pickedState = -1;
            }
            break;
        default:
            v.pickedState = point;
            m_panel.m_sideDirty = true;
            if (point >= 0) {
                m_drag = DragUVE::Point;
                m_dragPoint = point;
            } else if (PlotUVE().inflated(6.0F).contains(e.pos)) {
                m_drag = DragUVE::Cross;
                SetCrossUVE(*space, ToSpaceUVE(*space, e.pos, false));
            }
            break;
    }
    invalidate();
    return true;
}

bool BlendSpaceViewUVE::onMouseMove(const prism::MouseEvent& e) {
    m_mouse = e.pos;
    if (m_drag == DragUVE::Cross) {
        if (const NodeUVE* space = SpaceUVE()) {
            SetCrossUVE(*space, ToSpaceUVE(*space, e.pos, false));
        }
    }
    if (m_drag != DragUVE::None) {
        invalidate();
    }
    return true;
}

bool BlendSpaceViewUVE::onMouseUp(const prism::MouseEvent& e) {
    m_mouse = e.pos;
    const NodeUVE* space = SpaceUVE();
    if (space != nullptr && m_drag == DragUVE::Point && m_dragPoint >= 0) {
        const Math::Vector2UVE to = ToSpaceUVE(*space, e.pos, m_snap);
        const std::uint32_t id = space->id;
        const auto slot = static_cast<std::size_t>(m_dragPoint);
        m_panel.EditUVE([&](Scene::AnimationGraphComponentUVE& g) { static_cast<void>(MoveBlendSpacePointUVE(g.nodes, id, slot, to)); });
    } else if (m_drag == DragUVE::Cross) {
        m_panel.CommitLiveUVE();
    }
    m_drag = DragUVE::None;
    m_dragPoint = -1;
    invalidate();
    return true;
}

bool BlendSpaceViewUVE::onKeyDown(const prism::KeyEvent& e) {
    EditorUVE::AnimationGraphViewStateUVE& v = m_panel.m_editor.m_animGraph;
    const NodeUVE* space = SpaceUVE();
    if (e.key == prism::Key::Escape) {
        m_panel.CloseFocusUVE();
        return true;
    }
    if ((e.key == prism::Key::Delete || e.key == prism::Key::Backspace) && space != nullptr && v.pickedState >= 0) {
        const std::uint32_t id = space->id;
        const auto slot = static_cast<std::size_t>(v.pickedState);
        m_panel.EditUVE([id, slot](Scene::AnimationGraphComponentUVE& g) { static_cast<void>(RemoveBlendSpacePointUVE(g.nodes, id, slot)); });
        v.pickedState = -1;
        invalidate();
        return true;
    }
    return false;
}

void BlendSpaceViewUVE::AskClipUVE(const prism::Vec2 at, const Math::Vector2UVE position) {
    const NodeUVE* space = SpaceUVE();
    if (space == nullptr) {
        return;
    }
    const std::uint32_t id = space->id;
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Item("Animation for the new point", {}).disabled());
    for (const Asset::AssetGuidUVE clip : m_panel.ClipChoicesUVE()) {
        items.push_back(prism::MenuItem::Item(m_panel.m_editor.AnimationClipNameUVE(clip), [this, id, position, clip] {
            std::optional<std::size_t> added;
            m_panel.EditUVE([&](Scene::AnimationGraphComponentUVE& g) { added = AddBlendSpacePointUVE(g.nodes, id, position, clip); });
            if (added.has_value()) {
                m_panel.m_editor.m_animGraph.pickedState = static_cast<int>(*added);
            }
        }, "", prism::Icon::NodeClip));
    }
    if (items.size() == 1U) {
        items.push_back(prism::MenuItem::Item("No animations: add some to the AnimationSequencer", {}).disabled());
    }
    prism::showContextMenu(std::move(items), at);
}

void BlendSpaceViewUVE::ShowPointMenuUVE(const prism::Vec2 at, const int point) {
    const NodeUVE* space = SpaceUVE();
    if (space == nullptr) {
        return;
    }
    const std::uint32_t id = space->id;
    m_panel.m_editor.m_animGraph.pickedState = point;
    m_panel.m_sideDirty = true;
    std::vector<prism::MenuItem> clips;
    const Asset::AssetGuidUVE current = space->blendPoints[static_cast<std::size_t>(point)].clip;
    for (const Asset::AssetGuidUVE clip : m_panel.ClipChoicesUVE()) {
        clips.push_back(prism::MenuItem::Check(m_panel.m_editor.AnimationClipNameUVE(clip), clip == current, [this, id, point, clip] {
            m_panel.EditNodeUVE(id, [point, clip](NodeUVE& n) {
                if (static_cast<std::size_t>(point) < n.blendPoints.size()) {
                    n.blendPoints[static_cast<std::size_t>(point)].clip = clip;
                }
            });
        }));
    }
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Sub("Animation", std::move(clips), prism::Icon::NodeClip));
    items.push_back(prism::MenuItem::Sep());
    items.push_back(prism::MenuItem::Item("Remove Point", [this, id, point] {
        m_panel.EditUVE([id, point](Scene::AnimationGraphComponentUVE& g) { static_cast<void>(RemoveBlendSpacePointUVE(g.nodes, id, static_cast<std::size_t>(point))); });
        m_panel.m_editor.m_animGraph.pickedState = -1;
    }, "Del", prism::Icon::Trash));
    prism::showContextMenu(std::move(items), at);
}

} // namespace UVE::Editor
