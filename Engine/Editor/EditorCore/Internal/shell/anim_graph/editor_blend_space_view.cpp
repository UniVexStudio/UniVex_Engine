// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_blend_space_view.h"

#include "editor.h"
#include "editor_anim_graph_panel.h"

#include "animation_graph_editing.h"

#include "prism/Application.h"
#include "prism/Canvas.h"
#include "prism/Font.h"
#include "prism/Menu.h"
#include "prism/Theme.h"
#include "prism/Icons.h"

#include "editor_asset_table.h"
#include "i_asset_database.h"
#include "i_project_file_index.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <cstdio>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <chrono>
#include <filesystem>

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKind;
using Node = Scene::AnimationGraphNode;

const prism::Color kPoint{230, 180, 70};
const prism::Color kCross{236, 86, 86};
constexpr float kStripW = 236.0F;
constexpr float kStripRowH = 46.0F;
constexpr float kHintH = 24.0F;

[[nodiscard]] std::string NumberText(float v) {
    if (std::abs(v) < 1.0e-5F) {
        v = 0.0F;   // never "-0.00"
    }
    char text[24];
    std::snprintf(text, sizeof(text), std::abs(v) >= 10.0F ? "%.0f" : "%.2f", static_cast<double>(v));
    return text;
}

} // namespace

const Node* BlendSpaceView::Space() const {
    const Scene::AnimationGraphComponent* graph = m_panel.GraphPtr();
    if (graph == nullptr) {
        return nullptr;
    }
    const auto found = std::ranges::find(graph->nodes, m_panel.m_editor.m_animGraph.focus, &Node::id);
    return found != graph->nodes.end() && (found->kind == Kind::BlendSpace1D || found->kind == Kind::BlendSpace2D) ? &*found : nullptr;
}

std::size_t BlendSpaceView::SpaceIndex() const {
    const Scene::AnimationGraphComponent* graph = m_panel.GraphPtr();
    if (graph == nullptr) {
        return static_cast<std::size_t>(-1);
    }
    const auto found = std::ranges::find(graph->nodes, m_panel.m_editor.m_animGraph.focus, &Node::id);
    return found != graph->nodes.end() ? static_cast<std::size_t>(found - graph->nodes.begin()) : static_cast<std::size_t>(-1);
}

prism::Rect BlendSpaceView::Plot() const noexcept {
    return prism::Rect{m_rect.x + 56.0F, m_rect.y + 28.0F, std::max(40.0F, m_rect.w - 84.0F - kStripW),
                       std::max(40.0F, m_rect.h - 84.0F - kHintH)};
}

prism::Vec2 BlendSpaceView::ToScreen(const Node& space, const Math::Vector2 at) const {
    const prism::Rect plot = Plot();
    const float sx = space.areaMax.x - space.areaMin.x;
    const float sy = space.areaMax.y - space.areaMin.y;
    const float x = plot.x + (at.x - space.areaMin.x) / (std::abs(sx) > 1.0e-6F ? sx : 1.0F) * plot.w;
    const float y = space.kind == Kind::BlendSpace1D ? plot.y + plot.h * 0.5F
                                                     : plot.bottom() - (at.y - space.areaMin.y) / (std::abs(sy) > 1.0e-6F ? sy : 1.0F) * plot.h;
    return prism::Vec2{x, y};
}

Math::Vector2 BlendSpaceView::ToSpace(const Node& space, const prism::Vec2 screen, const bool snap) const {
    const prism::Rect plot = Plot();
    Math::Vector2 at{space.areaMin.x + (screen.x - plot.x) / plot.w * (space.areaMax.x - space.areaMin.x),
                        space.kind == Kind::BlendSpace1D ? 0.0F : space.areaMin.y + (plot.bottom() - screen.y) / plot.h * (space.areaMax.y - space.areaMin.y)};
    at.x = std::clamp(at.x, std::min(space.areaMin.x, space.areaMax.x), std::max(space.areaMin.x, space.areaMax.x));
    at.y = std::clamp(at.y, std::min(space.areaMin.y, space.areaMax.y), std::max(space.areaMin.y, space.areaMax.y));
    // Onto the grid's lines (Shift turns snapping off for one drag).
    if (snap && space.snapToGrid) {
        const float sx = GridStep(space, false);
        const float sy = GridStep(space, true);
        at.x = space.areaMin.x + std::round((at.x - space.areaMin.x) / sx) * sx;
        if (space.kind == Kind::BlendSpace2D) {
            at.y = space.areaMin.y + std::round((at.y - space.areaMin.y) / sy) * sy;
        }
    }
    return at;
}

float BlendSpaceView::GridStep(const Node& space, const bool y) const {
    const float span = y ? space.areaMax.y - space.areaMin.y : space.areaMax.x - space.areaMin.x;
    const auto divisions = static_cast<float>(std::max<std::uint32_t>(1U, y ? space.gridY : space.gridX));
    return std::max(std::abs(span) / divisions, 1.0e-4F);
}

int BlendSpaceView::PointAt(const Node& space, const prism::Vec2 screen) const {
    int best = -1;
    float bestDistance = 10.0F;
    for (std::size_t i = 0U; i < space.blendPoints.size(); ++i) {
        const prism::Vec2 p = ToScreen(space, space.blendPoints[i].position);
        const float d = std::hypot(p.x - screen.x, p.y - screen.y);
        if (d < bestDistance) {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

Math::Vector2 BlendSpaceView::Cross(const Node& space) const {
    Math::Vector2 at{space.value, space.valueY};
    if (const Scene::AnimationGraphComponent* graph = m_panel.GraphPtr()) {
        for (const Scene::AnimationParameter& p : graph->parameters) {
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

void BlendSpaceView::SetCross(const Node& space, const Math::Vector2 at) {
    // The parameters when they drive it (what a script would set), else the node's own values.
    const Scene::AnimationGraphComponent* graph = m_panel.GraphPtr();
    bool xDone = false;
    bool yDone = space.kind == Kind::BlendSpace1D;
    if (graph != nullptr) {
        for (std::size_t i = 0U; i < graph->parameters.size(); ++i) {
            if (!space.parameter.empty() && graph->parameters[i].name == space.parameter) {
                m_panel.LiveParameter(i, at.x);
                xDone = true;
            } else if (!yDone && !space.parameterY.empty() && graph->parameters[i].name == space.parameterY) {
                m_panel.LiveParameter(i, at.y);
                yDone = true;
            }
        }
    }
    if (!xDone || !yDone) {
        m_panel.LiveNode(space.id, [at, xDone, yDone](Node& n) {
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

void BlendSpaceView::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    c.fillRect(m_rect, prism::Color(26, 28, 33));
    const Node* space = Space();
    if (space == nullptr) {
        return;
    }
    const Editor::AnimationGraphViewState& v = m_panel.m_editor.m_animGraph;
    const bool two = space->kind == Kind::BlendSpace2D;
    const prism::Rect plot = Plot();
    c.pushClip(m_rect);
    c.fillRoundRect(plot.inflated(1.0F), 4.0F, prism::Color(34, 37, 44));
    c.strokeRoundRect(plot.inflated(1.0F), 4.0F, t.border, 1);
    // Grid: one line per division of each axis, labelled; the axes named.
    {
        const float step = GridStep(*space, false);
        for (std::uint32_t k = 0U; k <= std::max<std::uint32_t>(1U, space->gridX); ++k) {
            const float x = space->areaMin.x + step * static_cast<float>(k);
            const float sx = ToScreen(*space, Math::Vector2{x, space->areaMin.y}).x;
            c.fillRect(prism::Rect{sx, plot.y, 1.0F, plot.h}, std::abs(x) < step * 0.01F ? prism::Color(255, 255, 255, 45) : prism::Color(255, 255, 255, 16));
            c.textIn(*t.small, prism::Rect{sx - 24.0F, plot.bottom() + 4.0F, 48.0F, 14.0F}, NumberText(x), t.textDim, prism::HAlign::Center, false);
        }
        const std::string name = !space->axisNameX.empty() ? space->axisNameX : space->parameter.empty() ? std::string{"X"} : space->parameter;
        c.textIn(*t.semibold, prism::Rect{plot.x, plot.bottom() + 20.0F, plot.w, 16.0F}, name, t.text, prism::HAlign::Center);
    }
    if (two) {
        const float step = GridStep(*space, true);
        for (std::uint32_t k = 0U; k <= std::max<std::uint32_t>(1U, space->gridY); ++k) {
            const float y = space->areaMin.y + step * static_cast<float>(k);
            const float sy = ToScreen(*space, Math::Vector2{space->areaMin.x, y}).y;
            c.fillRect(prism::Rect{plot.x, sy, plot.w, 1.0F}, std::abs(y) < step * 0.01F ? prism::Color(255, 255, 255, 45) : prism::Color(255, 255, 255, 16));
            c.textIn(*t.small, prism::Rect{plot.x - 52.0F, sy - 7.0F, 46.0F, 14.0F}, NumberText(y), t.textDim, prism::HAlign::Right, false);
        }
        const std::string name = !space->axisNameY.empty() ? space->axisNameY : space->parameterY.empty() ? std::string{"Y"} : space->parameterY;
        c.textIn(*t.semibold, prism::Rect{m_rect.x + 6.0F, plot.y - 22.0F, 200.0F, 16.0F}, name, t.text);
    } else {
        c.fillRect(prism::Rect{plot.x, plot.y + plot.h * 0.5F, plot.w, 1.0F}, prism::Color(255, 255, 255, 40));
    }
    if (m_dropHot || m_drag == Drag::Clip) {
        c.strokeRoundRect(plot.inflated(3.0F), 5.0F, t.highlight, 2);
    }

    // The runtime's triangles and weights, when the preview has run it.
    const Scene::AnimationGraphComponent* graph = m_panel.GraphPtr();
    const std::size_t index = SpaceIndex();
    const Scene::AnimationGraphNodeState* run =
        graph != nullptr && graph->nodeStates.size() == graph->nodes.size() && index < graph->nodeStates.size() ? &graph->nodeStates[index] : nullptr;
    if (two && run != nullptr && run->triangulatedPoints.size() == space->blendPoints.size()) {
        const Math::Vector2 cross = Cross(*space);
        for (const std::array<std::uint32_t, 3>& tri : run->triangles) {
            if (tri[0] >= space->blendPoints.size() || tri[1] >= space->blendPoints.size() || tri[2] >= space->blendPoints.size()) {
                continue;
            }
            const prism::Vec2 a = ToScreen(*space, space->blendPoints[tri[0]].position);
            const prism::Vec2 b = ToScreen(*space, space->blendPoints[tri[1]].position);
            const prism::Vec2 d = ToScreen(*space, space->blendPoints[tri[2]].position);
            // Lit when the cross is inside it.
            const prism::Vec2 p = ToScreen(*space, cross);
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
            c.line(ToScreen(*space, space->blendPoints[i - 1U].position), ToScreen(*space, space->blendPoints[i].position),
                   prism::Color(255, 255, 255, 50), 2.0F);
        }
    }

    // The points: each its animation, sized by its weight now.
    for (std::size_t i = 0U; i < space->blendPoints.size(); ++i) {
        const Scene::AnimationBlendPoint& point = space->blendPoints[i];
        Math::Vector2 at = point.position;
        if (m_drag == Drag::Point && m_dragPoint == static_cast<int>(i)) {
            at = ToSpace(*space, m_mouse, m_snap);
        }
        const prism::Vec2 p = ToScreen(*space, at);
        const float weight = run != nullptr && i < run->pointWeights.size() ? run->pointWeights[i] : 0.0F;
        if (weight > 0.001F) {
            c.fillCircle(p, 7.0F + 14.0F * weight, prism::Color(230, 180, 70, 50));
        }
        const bool picked = v.pickedState == static_cast<int>(i);
        c.fillCircle(p, picked ? 7.0F : 5.5F, picked ? prism::Color(255, 226, 120) : kPoint);
        c.strokeCircle(p, picked ? 7.0F : 5.5F, prism::Color(20, 20, 24), 1.5F);
        std::string label = m_panel.m_editor.AnimationClipName(point.clip);
        if (label.empty()) {
            label = "(no animation)";
        }
        if (weight > 0.001F) {
            label += "  " + std::to_string(static_cast<int>(std::lround(weight * 100.0F))) + "%";
        }
        c.textIn(*t.small, prism::Rect{p.x + 9.0F, p.y - 16.0F, 180.0F, 14.0F}, label, picked ? t.textBright : t.text);
    }
    // The position cross.
    const prism::Vec2 cross = ToScreen(*space, Cross(*space));
    c.line(prism::Vec2{cross.x - 9.0F, cross.y}, prism::Vec2{cross.x + 9.0F, cross.y}, kCross, 2.0F);
    c.line(prism::Vec2{cross.x, cross.y - 9.0F}, prism::Vec2{cross.x, cross.y + 9.0F}, kCross, 2.0F);
    c.strokeCircle(cross, 5.0F, kCross, 1.5F);
    // A clip being dragged in from the list: where it would land.
    if (m_drag == Drag::Clip && plot.inflated(6.0F).contains(m_mouse)) {
        const prism::Vec2 at = ToScreen(*space, ToSpace(*space, m_mouse, m_snap));
        c.fillCircle(at, 7.0F, t.highlight);
        c.textIn(*t.small, prism::Rect{at.x + 10.0F, at.y - 16.0F, 200.0F, 14.0F}, m_panel.m_editor.AnimationClipName(m_dragClip), t.textBright);
    }
    PaintStrip(c);
    const char* hint = m_drag == Drag::Clip ? "Drop on the grid to put the animation there   |   Shift: no snapping"
                                               : "Drag an animation from the list (or the Asset Table) onto the grid   |   Double-click: add here   |   "
                                                 "Drag the red cross to preview   |   Right-click: more";
    c.fillRect(prism::Rect{m_rect.x, m_rect.bottom() - kHintH, m_rect.w, kHintH}, prism::Color(18, 20, 24, 220));
    c.textIn(*t.small, prism::Rect{m_rect.x + 10.0F, m_rect.bottom() - kHintH, m_rect.w - 20.0F, kHintH}, hint, prism::Color(160, 168, 184));
    if (m_drag == Drag::Clip && !plot.inflated(6.0F).contains(m_mouse)) {
        c.textIn(*t.small, prism::Rect{m_mouse.x + 12.0F, m_mouse.y + 4.0F, 220.0F, 14.0F}, m_panel.m_editor.AnimationClipName(m_dragClip), t.textBright);
    }
    c.popClip();
}

prism::Rect BlendSpaceView::Strip() const noexcept {
    return prism::Rect{m_rect.right() - kStripW, m_rect.y, kStripW, std::max(0.0F, m_rect.h - kHintH)};
}

prism::Rect BlendSpaceView::StripRow(const std::size_t row) const noexcept {
    const prism::Rect strip = Strip();
    return prism::Rect{strip.x + 6.0F, strip.y + 32.0F + static_cast<float>(row) * kStripRowH - m_stripScroll, strip.w - 12.0F, kStripRowH - 4.0F};
}

int BlendSpaceView::StripRowAt(const prism::Vec2 screen) const {
    const prism::Rect strip = Strip();
    if (!strip.contains(screen) || screen.y < strip.y + 32.0F) {
        return -1;
    }
    for (std::size_t row = 0U; row < m_clips.size(); ++row) {
        if (StripRow(row).contains(screen)) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

void BlendSpaceView::PaintStrip(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    // The project's animations, read again now and then (files come and go).
    static std::chrono::steady_clock::time_point lastRead{};
    const auto now = std::chrono::steady_clock::now();
    if (m_clips.empty() || now - lastRead > std::chrono::milliseconds(1500)) {
        m_clips = m_panel.ClipChoices();
        lastRead = now;
    }
    const prism::Rect strip = Strip();
    c.fillRect(strip, prism::Color(22, 24, 29));
    c.fillRect(prism::Rect{strip.x, strip.y, 1.0F, strip.h}, t.border);
    prism::drawIcon(c, prism::Icon::AnimLibrary, prism::Rect{strip.x + 10.0F, strip.y + 8.0F, 16.0F, 16.0F}, t.text);
    c.textIn(*t.semibold, prism::Rect{strip.x + 32.0F, strip.y + 4.0F, strip.w - 40.0F, 24.0F},
             "Animations (" + std::to_string(m_clips.size()) + ")", t.textBright);
    c.pushClip(prism::Rect{strip.x, strip.y + 30.0F, strip.w, strip.h - 30.0F});
    Asset::IAssetDatabase& database = m_panel.m_editor.m_services->GetAssetDatabase();
    for (std::size_t row = 0U; row < m_clips.size(); ++row) {
        const prism::Rect r = StripRow(row);
        if (r.bottom() < strip.y + 30.0F || r.y > strip.bottom()) {
            continue;
        }
        const bool hot = m_stripHover == static_cast<int>(row) || (m_drag == Drag::Clip && m_dragClip == m_clips[row]);
        c.fillRoundRect(r, 4.0F, hot ? prism::Color(52, 58, 72) : prism::Color(32, 35, 42));
        const prism::Rect iconRect{r.x + 3.0F, r.y + 3.0F, (r.h - 6.0F) * 152.0F / 128.0F, r.h - 6.0F};
        const std::filesystem::path file = database.Resolve(m_clips[row]);
        const Editor::AnimationIcon* icon = file.empty() ? nullptr : m_panel.m_editor.GetAnimationIcon(file);
        if (icon == nullptr) {
            invalidate();   // drawn in the background: look again next frame
        }
        if (icon != nullptr && icon->width > 0) {
            c.blitScaled(icon->pixels.data(), icon->width, icon->height, icon->width, iconRect);
        } else {
            c.fillRoundRect(iconRect, 3.0F, prism::Color(40, 44, 54));
            prism::drawIcon(c, prism::Icon::AnimClip, iconRect.deflated(8.0F), t.textDim);
        }
        c.textIn(*t.ui, prism::Rect{iconRect.right() + 8.0F, r.y + 3.0F, r.right() - iconRect.right() - 12.0F, 18.0F},
                 m_panel.m_editor.AnimationClipName(m_clips[row]), t.text);
        if (icon != nullptr) {
            char detail[64];
            std::snprintf(detail, sizeof(detail), "%.2f s  -  %zu frames", icon->durationSeconds, icon->frames);
            c.textIn(*t.small, prism::Rect{iconRect.right() + 8.0F, r.y + 22.0F, r.right() - iconRect.right() - 12.0F, 16.0F}, detail, t.textDim);
        }
    }
    if (m_clips.empty()) {
        c.textIn(*t.small, prism::Rect{strip.x + 10.0F, strip.y + 36.0F, strip.w - 20.0F, 40.0F}, "No animations in the project yet", t.textDim);
    }
    c.popClip();
}

// ---------------------------------------------------------------------------------------- input

bool BlendSpaceView::onMouseDown(const prism::MouseEvent& e) {
    if (prism::Application::exists()) {
        prism::Application::get().setFocus(this);
    }
    const Node* space = Space();
    if (space == nullptr) {
        return true;
    }
    Editor::AnimationGraphViewState& v = m_panel.m_editor.m_animGraph;
    m_mouse = e.pos;
    m_snap = (e.mods & prism::ModShift) == 0U;
    // The animations list: press on one and drag it onto the grid.
    if (Strip().contains(e.pos)) {
        const int row = StripRowAt(e.pos);
        if (row >= 0 && e.button == prism::MouseButton::Left) {
            m_dragClip = m_clips[static_cast<std::size_t>(row)];
            m_drag = Drag::Clip;
            invalidate();
        }
        return true;
    }
    const int point = PointAt(*space, e.pos);
    if (e.button == prism::MouseButton::Right) {
        if (point >= 0) {
            ShowPointMenu(e.pos, point);
        } else if (Plot().inflated(6.0F).contains(e.pos)) {
            ShowEmptyMenu(e.pos, ToSpace(*space, e.pos, m_snap));
        }
        return true;
    }
    if (e.button != prism::MouseButton::Left) {
        return false;
    }
    if (e.clicks >= 2 && point < 0 && Plot().inflated(6.0F).contains(e.pos)) {
        AskClip(e.pos, ToSpace(*space, e.pos, m_snap));
        return true;
    }
    const std::uint32_t id = space->id;
    switch (v.spaceTool) {
        case 1:   // add
            if (point < 0 && Plot().inflated(6.0F).contains(e.pos)) {
                AskClip(e.pos, ToSpace(*space, e.pos, m_snap));
            }
            break;
        case 2:   // remove
            if (point >= 0) {
                m_panel.Edit([id, point](Scene::AnimationGraphComponent& g) {
                    static_cast<void>(RemoveBlendSpacePoint(g.nodes, id, static_cast<std::size_t>(point)));
                });
                v.pickedState = -1;
            }
            break;
        default:
            v.pickedState = point;
            m_panel.m_sideDirty = true;
            if (point >= 0) {
                m_drag = Drag::Point;
                m_dragPoint = point;
            } else if (Plot().inflated(6.0F).contains(e.pos)) {
                m_drag = Drag::Cross;
                SetCross(*space, ToSpace(*space, e.pos, false));
            }
            break;
    }
    invalidate();
    return true;
}

bool BlendSpaceView::onMouseMove(const prism::MouseEvent& e) {
    m_mouse = e.pos;
    if (m_drag == Drag::Clip) {
        m_snap = (e.mods & prism::ModShift) == 0U;
    }
    if (const int row = m_drag == Drag::None ? StripRowAt(e.pos) : -1; row != m_stripHover) {
        m_stripHover = row;
        invalidate();
    }
    if (m_drag == Drag::Cross) {
        if (const Node* space = Space()) {
            SetCross(*space, ToSpace(*space, e.pos, false));
        }
    }
    if (m_drag != Drag::None) {
        invalidate();
    }
    return true;
}

bool BlendSpaceView::onMouseUp(const prism::MouseEvent& e) {
    m_mouse = e.pos;
    const Node* space = Space();
    if (space != nullptr && m_drag == Drag::Point && m_dragPoint >= 0) {
        const Math::Vector2 to = ToSpace(*space, e.pos, m_snap);
        const std::uint32_t id = space->id;
        const auto slot = static_cast<std::size_t>(m_dragPoint);
        m_panel.Edit([&](Scene::AnimationGraphComponent& g) { static_cast<void>(MoveBlendSpacePoint(g.nodes, id, slot, to)); });
    } else if (m_drag == Drag::Cross) {
        m_panel.CommitLive();
    } else if (space != nullptr && m_drag == Drag::Clip && Plot().inflated(6.0F).contains(e.pos)) {
        AddPoint(ToSpace(*space, e.pos, m_snap), m_dragClip);
    }
    m_drag = Drag::None;
    m_dragPoint = -1;
    invalidate();
    return true;
}

bool BlendSpaceView::onKeyDown(const prism::KeyEvent& e) {
    Editor::AnimationGraphViewState& v = m_panel.m_editor.m_animGraph;
    const Node* space = Space();
    if (e.key == prism::Key::Escape) {
        m_panel.CloseFocus();
        return true;
    }
    if ((e.key == prism::Key::Delete || e.key == prism::Key::Backspace) && space != nullptr && v.pickedState >= 0) {
        const std::uint32_t id = space->id;
        const auto slot = static_cast<std::size_t>(v.pickedState);
        m_panel.Edit([id, slot](Scene::AnimationGraphComponent& g) { static_cast<void>(RemoveBlendSpacePoint(g.nodes, id, slot)); });
        v.pickedState = -1;
        invalidate();
        return true;
    }
    return false;
}

void BlendSpaceView::AskClip(const prism::Vec2 at, const Math::Vector2 position) {
    const Node* space = Space();
    if (space == nullptr) {
        return;
    }
    const std::uint32_t id = space->id;
    std::vector<prism::MenuItem> items;
    for (const Asset::AssetGuid clip : m_panel.ClipChoices()) {
        items.push_back(prism::MenuItem::Item(m_panel.m_editor.AnimationClipName(clip), [this, id, position, clip] {
            std::optional<std::size_t> added;
            m_panel.Edit([&](Scene::AnimationGraphComponent& g) { added = AddBlendSpacePoint(g.nodes, id, position, clip); });
            if (added.has_value()) {
                m_panel.m_editor.m_animGraph.pickedState = static_cast<int>(*added);
            }
        }, "", prism::Icon::AnimClip));
    }
    if (items.empty()) {
        items.push_back(prism::MenuItem::Item("None", {}).disabled());
    }
    prism::showContextMenu(std::move(items), at);
}

void BlendSpaceView::ShowPointMenu(const prism::Vec2 at, const int point) {
    const Node* space = Space();
    if (space == nullptr) {
        return;
    }
    const std::uint32_t id = space->id;
    m_panel.m_editor.m_animGraph.pickedState = point;
    m_panel.m_sideDirty = true;
    std::vector<prism::MenuItem> clips;
    const Asset::AssetGuid current = space->blendPoints[static_cast<std::size_t>(point)].clip;
    for (const Asset::AssetGuid clip : m_panel.ClipChoices()) {
        clips.push_back(prism::MenuItem::Check(m_panel.m_editor.AnimationClipName(clip), clip == current, [this, id, point, clip] {
            m_panel.EditNode(id, [point, clip](Node& n) {
                if (static_cast<std::size_t>(point) < n.blendPoints.size()) {
                    n.blendPoints[static_cast<std::size_t>(point)].clip = clip;
                }
            });
        }));
    }
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Header(m_panel.m_editor.AnimationClipName(current).empty() ? std::string{"Point"}
                                                                                                      : m_panel.m_editor.AnimationClipName(current)));
    items.push_back(prism::MenuItem::Sub("Replace Animation", std::move(clips), prism::Icon::AnimClip));
    items.push_back(prism::MenuItem::Sep());
    items.push_back(prism::MenuItem::Item("Delete Point", [this, id, point] {
        m_panel.Edit([id, point](Scene::AnimationGraphComponent& g) { static_cast<void>(RemoveBlendSpacePoint(g.nodes, id, static_cast<std::size_t>(point))); });
        m_panel.m_editor.m_animGraph.pickedState = -1;
    }, "Del", prism::Icon::Trash));
    prism::showContextMenu(std::move(items), at);
}

void BlendSpaceView::AddPoint(const Math::Vector2 position, const Asset::AssetGuid clip) {
    const Node* space = Space();
    if (space == nullptr) {
        return;
    }
    const std::uint32_t id = space->id;
    std::optional<std::size_t> added;
    m_panel.Edit([&](Scene::AnimationGraphComponent& g) { added = AddBlendSpacePoint(g.nodes, id, position, clip); });
    if (added.has_value()) {
        m_panel.m_editor.m_animGraph.pickedState = static_cast<int>(*added);
    } else if (prism::Application::exists()) {
        prism::Application::get().notify("A point is already there (or the space is full)", prism::ToastLevel::Warning, 1800);
    }
    m_panel.m_sideDirty = true;
    invalidate();
}

void BlendSpaceView::ShowEmptyMenu(const prism::Vec2 at, const Math::Vector2 position) {
    std::vector<prism::MenuItem> clips;
    for (const Asset::AssetGuid clip : m_panel.ClipChoices()) {
        clips.push_back(prism::MenuItem::Item(m_panel.m_editor.AnimationClipName(clip), [this, position, clip] { AddPoint(position, clip); }, "",
                                              prism::Icon::AnimClip));
    }
    if (clips.empty()) {
        clips.push_back(prism::MenuItem::Item("No animations in the project", {}).disabled());
    }
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Header("Blend Space"));
    items.push_back(prism::MenuItem::Sub("Add Animation Here", std::move(clips), prism::Icon::Plus));
    items.push_back(prism::MenuItem::Item("Empty Point Here", [this, position] { AddPoint(position, Asset::AssetGuid{}); }));
    items.push_back(prism::MenuItem::Sep());
    items.push_back(prism::MenuItem::Item("Up", [this] { m_panel.CloseFocus(); }, "Esc", prism::Icon::ChevronLeft));
    prism::showContextMenu(std::move(items), at);
}

bool BlendSpaceView::onMouseWheel(const prism::MouseEvent& e) {
    if (!Strip().contains(e.pos)) {
        return false;
    }
    const float content = static_cast<float>(m_clips.size()) * kStripRowH + 40.0F;
    m_stripScroll = std::clamp(m_stripScroll - e.wheel.y * 40.0F, 0.0F, std::max(0.0F, content - Strip().h));
    invalidate();
    return true;
}

void BlendSpaceView::onMouseLeave() {
    if (m_stripHover != -1) {
        m_stripHover = -1;
        invalidate();
    }
}

bool BlendSpaceView::onDragOver(const prism::DragDropEvent& e) {
    const std::string* path = e.operation && e.operation->tag() == EditorAssetTable::kDragTag ? e.operation->getPayload<std::string>() : nullptr;
    const bool accepts = path != nullptr && std::filesystem::path{*path}.extension() == ".uvanim" && Plot().inflated(6.0F).contains(e.pos);
    m_mouse = e.pos;
    if (accepts != m_dropHot) {
        m_dropHot = accepts;
    }
    invalidate();
    return accepts;
}

void BlendSpaceView::onDragLeave() {
    m_dropHot = false;
    invalidate();
}

bool BlendSpaceView::onDrop(const prism::DragDropEvent& e) {
    const bool accepts = onDragOver(e);
    m_dropHot = false;
    const Node* space = Space();
    if (!accepts || space == nullptr) {
        return false;
    }
    const std::filesystem::path relative{*e.operation->getPayload<std::string>()};
    const Asset::ProjectFileSnapshot snapshot = m_panel.m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    const Asset::AssetGuid clip = m_panel.m_editor.m_services->GetAssetDatabase().Register(snapshot.contentRoot / relative);
    AddPoint(ToSpace(*space, e.pos, (e.mods & prism::ModShift) == 0U), clip);
    return true;
}

const std::string& BlendSpaceView::tooltipAt(const prism::Vec2 p) const {
    m_tip.clear();
    const Node* space = Space();
    if (space == nullptr || m_drag != Drag::None) {
        return m_tip;
    }
    if (const int point = PointAt(*space, p); point >= 0) {
        const Scene::AnimationBlendPoint& at = space->blendPoints[static_cast<std::size_t>(point)];
        m_tip = m_panel.m_editor.AnimationClipName(at.clip);
        if (m_tip.empty()) {
            m_tip = "(no animation)";
        }
        m_tip += "\nAt " + NumberText(at.position.x) + (space->kind == Kind::BlendSpace2D ? ", " + NumberText(at.position.y) : std::string{}) +
                 "\nRate " + NumberText(at.speed) + "x" + (at.loop ? ", loops" : "");
    } else if (const int row = StripRowAt(p); row >= 0) {
        m_tip = m_panel.m_editor.AnimationClipName(m_clips[static_cast<std::size_t>(row)]) + "\nDrag it onto the grid";
    }
    return m_tip;
}

prism::Cursor BlendSpaceView::cursor() const {
    if (m_drag == Drag::Clip || m_stripHover >= 0) {
        return prism::Cursor::Hand;
    }
    return m_drag == Drag::Point ? prism::Cursor::SizeAll : prism::Cursor::Arrow;
}

} // namespace UVE::Editor
