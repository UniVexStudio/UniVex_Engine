// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_state_machine_view.h"

#include "editor.h"
#include "editor_anim_graph_panel.h"

#include "animation_graph_editing.h"

#include "prism/Application.h"
#include "prism/Canvas.h"
#include "prism/Font.h"
#include "prism/Icons.h"
#include "prism/Menu.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKind;
using Node = Scene::AnimationGraphNode;

// Android lifts the first finger's button far off-screen (-100000) when a second finger or a long press
// takes over the gesture: that is a cancel, not a drag to there.
[[nodiscard]] bool IsGestureCancel(const prism::Vec2 p) { return std::fabs(p.x) > 1.0e4F || std::fabs(p.y) > 1.0e4F; }

constexpr float kStateW = 168.0F;
constexpr float kStateH = 52.0F;
constexpr float kPortW = 96.0F;
constexpr float kPortH = 30.0F;

const prism::Color kEntryColor{72, 168, 96};
const prism::Color kAnyColor{214, 146, 64};
const prism::Color kStateColor{58, 66, 104};
const prism::Color kActiveColor{86, 156, 236};
const prism::Color kTakenColor{255, 214, 96};

/// Where the ray from `centre` towards `towards` leaves `box`.
[[nodiscard]] prism::Vec2 EdgePoint(const prism::Rect box, const prism::Vec2 centre, const prism::Vec2 towards) {
    const float dx = towards.x - centre.x;
    const float dy = towards.y - centre.y;
    if (std::abs(dx) < 1.0e-4F && std::abs(dy) < 1.0e-4F) {
        return centre;
    }
    const float sx = std::abs(dx) > 1.0e-4F ? (box.w * 0.5F) / std::abs(dx) : 1.0e9F;
    const float sy = std::abs(dy) > 1.0e-4F ? (box.h * 0.5F) / std::abs(dy) : 1.0e9F;
    const float s = std::min(sx, sy);
    return prism::Vec2{centre.x + dx * s, centre.y + dy * s};
}

[[nodiscard]] float SegmentDistance(const prism::Vec2 p, const prism::Vec2 a, const prism::Vec2 b) {
    const float vx = b.x - a.x;
    const float vy = b.y - a.y;
    const float len = vx * vx + vy * vy;
    const float t = len > 1.0e-6F ? std::clamp(((p.x - a.x) * vx + (p.y - a.y) * vy) / len, 0.0F, 1.0F) : 0.0F;
    const float cx = a.x + vx * t - p.x;
    const float cy = a.y + vy * t - p.y;
    return std::sqrt(cx * cx + cy * cy);
}

void Arrow(prism::Canvas& c, const prism::Vec2 from, const prism::Vec2 to, const prism::Color color, const float thickness,
              const bool dashed) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0F) {
        return;
    }
    const float ux = dx / len;
    const float uy = dy / len;
    const prism::Vec2 tip = to;
    const prism::Vec2 base{to.x - ux * 10.0F, to.y - uy * 10.0F};
    if (dashed) {
        for (float d = 0.0F; d < len - 10.0F; d += 9.0F) {
            const float e = std::min(d + 5.0F, len - 10.0F);
            c.line(prism::Vec2{from.x + ux * d, from.y + uy * d}, prism::Vec2{from.x + ux * e, from.y + uy * e}, color, thickness);
        }
    } else {
        c.line(from, base, color, thickness);
    }
    const prism::Vec2 head[] = {tip, prism::Vec2{base.x - uy * 5.0F, base.y + ux * 5.0F}, prism::Vec2{base.x + uy * 5.0F, base.y - ux * 5.0F}};
    c.fillPolygon(head, color);
}

} // namespace

// ------------------------------------------------------------------------------------- geometry

const Node* StateMachineView::Machine() const {
    const Scene::AnimationGraphComponent* graph = m_panel.GraphPtr();
    if (graph == nullptr) {
        return nullptr;
    }
    const auto found = std::ranges::find(graph->nodes, m_panel.m_editor.m_animGraph.focus, &Node::id);
    return found != graph->nodes.end() && found->kind == Kind::StateMachine ? &*found : nullptr;
}

std::size_t StateMachineView::MachineIndex() const {
    const Scene::AnimationGraphComponent* graph = m_panel.GraphPtr();
    if (graph == nullptr) {
        return static_cast<std::size_t>(-1);
    }
    const auto found = std::ranges::find(graph->nodes, m_panel.m_editor.m_animGraph.focus, &Node::id);
    return found != graph->nodes.end() ? static_cast<std::size_t>(found - graph->nodes.begin()) : static_cast<std::size_t>(-1);
}

prism::Vec2 StateMachineView::ToScreen(const Math::Vector2 world) const {
    const Editor::AnimationGraphViewState& v = m_panel.m_editor.m_animGraph;
    return prism::Vec2{m_rect.x + (world.x - v.statePanX) * v.stateZoom, m_rect.y + (world.y - v.statePanY) * v.stateZoom};
}

Math::Vector2 StateMachineView::ToWorld(const prism::Vec2 screen) const {
    const Editor::AnimationGraphViewState& v = m_panel.m_editor.m_animGraph;
    return Math::Vector2{v.statePanX + (screen.x - m_rect.x) / v.stateZoom, v.statePanY + (screen.y - m_rect.y) / v.stateZoom};
}

prism::Rect StateMachineView::Box(const Node& machine, const int box) const {
    Math::Vector2 at{};
    float w = kStateW;
    float h = kStateH;
    if (box == kEntry) {
        at = machine.entryPosition;
        w = kPortW;
        h = kPortH;
    } else if (box == kAny) {
        at = machine.anyPosition;
        w = kPortW;
        h = kPortH;
    } else {
        at = AnimationStatePosition(machine, static_cast<std::size_t>(box));
    }
    if (m_drag == Drag::Move && m_dragBox == box) {
        const float zoom = m_panel.m_editor.m_animGraph.stateZoom;
        at.x += (m_mouse.x - m_dragFrom.x) / zoom;
        at.y += (m_mouse.y - m_dragFrom.y) / zoom;
    }
    return prism::Rect{at.x, at.y, w, h};
}

prism::Rect StateMachineView::ScreenBox(const Node& machine, const int box) const {
    const prism::Rect r = Box(machine, box);
    const prism::Vec2 p = ToScreen(Math::Vector2{r.x, r.y});
    const float zoom = m_panel.m_editor.m_animGraph.stateZoom;
    return prism::Rect{p.x, p.y, r.w * zoom, r.h * zoom};
}

int StateMachineView::BoxAt(const Node& machine, const prism::Vec2 screen) const {
    for (std::size_t slot = machine.inputs.size(); slot-- > 0U;) {
        if (ScreenBox(machine, static_cast<int>(slot)).contains(screen)) {
            return static_cast<int>(slot);
        }
    }
    if (ScreenBox(machine, kEntry).contains(screen)) {
        return kEntry;
    }
    if (ScreenBox(machine, kAny).contains(screen)) {
        return kAny;
    }
    return kNone;
}

bool StateMachineView::TransitionSegment(const Node& machine, const std::size_t index, prism::Vec2& from, prism::Vec2& to) const {
    const Scene::AnimationGraphTransition& t = machine.transitions[index];
    if (t.toState >= machine.inputs.size() || (t.fromState != Scene::kAnyAnimationState && t.fromState >= machine.inputs.size())) {
        return false;
    }
    const int fromBox = t.fromState == Scene::kAnyAnimationState ? kAny : static_cast<int>(t.fromState);
    const prism::Rect a = ScreenBox(machine, fromBox);
    const prism::Rect b = ScreenBox(machine, static_cast<int>(t.toState));
    prism::Vec2 ca{a.x + a.w * 0.5F, a.y + a.h * 0.5F};
    prism::Vec2 cb{b.x + b.w * 0.5F, b.y + b.h * 0.5F};
    // Two states leading to each other: each direction on its own lane.
    const bool paired = fromBox >= 0 && std::ranges::any_of(machine.transitions, [&t](const Scene::AnimationGraphTransition& o) {
                            return o.fromState == t.toState && o.toState == t.fromState;
                        });
    if (paired) {
        const float dx = cb.x - ca.x;
        const float dy = cb.y - ca.y;
        const float len = std::max(1.0F, std::sqrt(dx * dx + dy * dy));
        const float off = 7.0F * m_panel.m_editor.m_animGraph.stateZoom;
        ca = prism::Vec2{ca.x - dy / len * off, ca.y + dx / len * off};
        cb = prism::Vec2{cb.x - dy / len * off, cb.y + dx / len * off};
    }
    from = EdgePoint(a, ca, cb);
    to = EdgePoint(b, cb, ca);
    return true;
}

int StateMachineView::TransitionAt(const Node& machine, const prism::Vec2 screen) const {
    int best = -1;
    float bestDistance = 7.0F;
    for (std::size_t i = 0U; i < machine.transitions.size(); ++i) {
        prism::Vec2 a;
        prism::Vec2 b;
        if (TransitionSegment(machine, i, a, b)) {
            const float d = SegmentDistance(screen, a, b);
            if (d < bestDistance) {
                bestDistance = d;
                best = static_cast<int>(i);
            }
        }
    }
    return best;
}

std::string StateMachineView::StateName(const Node& machine, const std::size_t slot) const {
    const Scene::AnimationGraphComponent* graph = m_panel.GraphPtr();
    return graph != nullptr ? AnimationStateName(graph->nodes, machine, slot) : AnimationGraphSlotLabel(Kind::StateMachine, slot);
}

std::string StateMachineView::StateDetail(const Node& machine, const std::size_t slot) const {
    const Scene::AnimationGraphComponent* graph = m_panel.GraphPtr();
    if (graph == nullptr || slot >= machine.inputs.size() || machine.inputs[slot] == 0U) {
        return {};
    }
    const auto child = std::ranges::find(graph->nodes, machine.inputs[slot], &Node::id);
    if (child == graph->nodes.end()) {
        return {};
    }
    if (child->kind == Kind::Clip) {
        const std::string& clip = m_panel.m_editor.AnimationClipName(child->clip);
        return clip;
    }
    return AnimationGraphKindLabel(child->kind);
}

void StateMachineView::Frame() {
    const Node* machine = Machine();
    if (machine == nullptr || m_rect.w < 10.0F || m_rect.h < 10.0F) {
        return;
    }
    prism::Rect bounds = Box(*machine, kEntry);
    const auto grow = [&bounds](const prism::Rect r) {
        const float x0 = std::min(bounds.x, r.x);
        const float y0 = std::min(bounds.y, r.y);
        bounds = prism::Rect{x0, y0, std::max(bounds.right(), r.right()) - x0, std::max(bounds.bottom(), r.bottom()) - y0};
    };
    grow(Box(*machine, kAny));
    for (std::size_t slot = 0U; slot < machine->inputs.size(); ++slot) {
        grow(Box(*machine, static_cast<int>(slot)));
    }
    Editor::AnimationGraphViewState& v = m_panel.m_editor.m_animGraph;
    v.stateZoom = std::clamp(std::min((m_rect.w - 80.0F) / std::max(1.0F, bounds.w), (m_rect.h - 80.0F) / std::max(1.0F, bounds.h)), 0.4F, 1.4F);
    v.statePanX = bounds.x + bounds.w * 0.5F - m_rect.w * 0.5F / v.stateZoom;
    v.statePanY = bounds.y + bounds.h * 0.5F - m_rect.h * 0.5F / v.stateZoom;
    v.stateFramed = true;
    v.stateMachine = machine->id;
    invalidate();
}

// ---------------------------------------------------------------------------------------- paint

void StateMachineView::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    c.fillRect(m_rect, prism::Color(26, 28, 33));
    const Node* machine = Machine();
    if (machine == nullptr) {
        return;
    }
    Editor::AnimationGraphViewState& v = m_panel.m_editor.m_animGraph;
    if (!v.stateFramed || v.stateMachine != machine->id) {
        Frame();
    }
    c.pushClip(m_rect);
    // Grid.
    const float step = 32.0F * v.stateZoom;
    if (step > 6.0F) {
        const float ox = std::fmod(-v.statePanX * v.stateZoom, step);
        const float oy = std::fmod(-v.statePanY * v.stateZoom, step);
        for (float x = m_rect.x + ox; x < m_rect.right(); x += step) {
            c.fillRect(prism::Rect{x, m_rect.y, 1.0F, m_rect.h}, prism::Color(255, 255, 255, 8));
        }
        for (float y = m_rect.y + oy; y < m_rect.bottom(); y += step) {
            c.fillRect(prism::Rect{m_rect.x, y, m_rect.w, 1.0F}, prism::Color(255, 255, 255, 8));
        }
    }
    // What the preview is doing: the active state, the one fading out, the transition taken.
    const Scene::AnimationGraphComponent* graph = m_panel.GraphPtr();
    const std::size_t index = MachineIndex();
    const Scene::AnimationGraphNodeState* run =
        graph != nullptr && index < graph->nodeStates.size() && graph->nodeStates.size() == graph->nodes.size() ? &graph->nodeStates[index] : nullptr;
    const bool fading = run != nullptr && run->fadeSeconds > 0.0F && run->fadeElapsedSeconds < run->fadeSeconds;

    // Entry's arrow to where the machine starts.
    if (machine->entryState < machine->inputs.size()) {
        const prism::Rect e = ScreenBox(*machine, kEntry);
        const prism::Rect s = ScreenBox(*machine, static_cast<int>(machine->entryState));
        const prism::Vec2 ce{e.x + e.w * 0.5F, e.y + e.h * 0.5F};
        const prism::Vec2 cs{s.x + s.w * 0.5F, s.y + s.h * 0.5F};
        Arrow(c, EdgePoint(e, ce, cs), EdgePoint(s, cs, ce), kEntryColor, 2.0F, false);
    }
    // Transitions.
    for (std::size_t i = 0U; i < machine->transitions.size(); ++i) {
        prism::Vec2 a;
        prism::Vec2 b;
        if (!TransitionSegment(*machine, i, a, b)) {
            continue;
        }
        const Scene::AnimationGraphTransition& tr = machine->transitions[i];
        const bool picked = static_cast<int>(i) == v.pickedTransition;
        const bool taken = fading && run != nullptr && run->lastTransition == i;
        prism::Color color = tr.fromState == Scene::kAnyAnimationState ? kAnyColor : prism::Color(170, 178, 196);
        if (!tr.enabled) {
            color = prism::Color(96, 100, 110);
        }
        if (taken) {
            color = kTakenColor;
        }
        if (picked) {
            color = t.highlight;
        }
        Arrow(c, a, b, color, picked || taken ? 2.6F : 1.7F, !tr.enabled);
        // Its conditions at the middle: a dot with how many.
        const prism::Vec2 mid{(a.x + b.x) * 0.5F, (a.y + b.y) * 0.5F};
        if (!tr.conditions.empty() && v.stateZoom > 0.55F) {
            c.fillCircle(mid, 7.0F, prism::Color(30, 32, 38));
            c.strokeCircle(mid, 7.0F, color, 1.2F);
            c.textIn(*t.small, prism::Rect{mid.x - 7.0F, mid.y - 7.0F, 14.0F, 14.0F}, std::to_string(tr.conditions.size()), color,
                     prism::HAlign::Center, false);
        }
    }
    // A link being drawn.
    if (m_drag == Drag::Link) {
        const prism::Rect from = ScreenBox(*machine, m_dragBox);
        const prism::Vec2 centre{from.x + from.w * 0.5F, from.y + from.h * 0.5F};
        const int target = BoxAt(*machine, m_mouse);
        prism::Vec2 end = m_mouse;
        if (target >= 0 && target != m_dragBox) {
            const prism::Rect to = ScreenBox(*machine, target);
            c.strokeRoundRect(to.inflated(3.0F), 8.0F * v.stateZoom, t.highlight, 2);
            end = EdgePoint(to, prism::Vec2{to.x + to.w * 0.5F, to.y + to.h * 0.5F}, centre);
        }
        Arrow(c, EdgePoint(from, centre, m_mouse), end, t.highlight, 2.2F, target < 0);
    }
    // Entry, Any and the states.
    const auto port = [&](const int box, const char* label, const prism::Color color) {
        const prism::Rect r = ScreenBox(*machine, box);
        c.fillRoundRect(r, 14.0F * v.stateZoom, color);
        c.textIn(*t.semibold, r, label, prism::Color(255, 255, 255), prism::HAlign::Center);
    };
    port(kEntry, "Entry", kEntryColor);
    port(kAny, "Any State", kAnyColor);
    for (std::size_t slot = 0U; slot < machine->inputs.size(); ++slot) {
        const prism::Rect r = ScreenBox(*machine, static_cast<int>(slot));
        if (!r.intersects(m_rect)) {
            continue;
        }
        const bool active = run != nullptr && run->activeState == slot;
        const bool leaving = fading && run != nullptr && run->previousState == slot;
        const bool picked = v.pickedState == static_cast<int>(slot);
        const float radius = 6.0F * v.stateZoom;
        c.shadow(r, radius, 6, prism::Color(0, 0, 0, 90));
        c.fillRoundRect(r, radius, active ? prism::Color(48, 86, 138) : kStateColor);
        c.fillRoundRect(prism::Rect{r.x, r.y, 5.0F * v.stateZoom, r.h}, radius, active ? kActiveColor : prism::Color(110, 120, 180));
        if (active || leaving) {
            c.strokeRoundRect(r, radius, active ? kActiveColor : prism::Color(120, 150, 200, 140), 2);
        }
        if (picked) {
            c.strokeRoundRect(r.inflated(2.0F), radius + 2.0F, prism::Color(255, 255, 255), 2);
        }
        // Its border lights where a drag starts a transition.
        if (m_drag == Drag::None && m_hoverBox == static_cast<int>(slot) && m_hoverEdge) {
            c.strokeRoundRect(r.inflated(4.0F), radius + 4.0F, t.highlight, 2);
        }
        if (machine->entryState == slot) {
            prism::drawIcon(c, prism::Icon::NodeEntry, prism::Rect{r.right() - 20.0F * v.stateZoom, r.y + 4.0F * v.stateZoom, 14.0F * v.stateZoom, 14.0F * v.stateZoom},
                            kEntryColor);
        }
        if (v.stateZoom > 0.5F) {
            c.textIn(*t.semibold, prism::Rect{r.x + 12.0F * v.stateZoom, r.y + 4.0F * v.stateZoom, r.w - 34.0F * v.stateZoom, r.h * 0.5F},
                     StateName(*machine, slot), prism::Color(240, 242, 248));
            c.textIn(*t.small, prism::Rect{r.x + 12.0F * v.stateZoom, r.y + r.h * 0.5F, r.w - 18.0F * v.stateZoom, r.h * 0.45F},
                     StateDetail(*machine, slot), prism::Color(170, 182, 210));
        }
        // How far into the state the preview is, when it runs.
        if (active && run != nullptr) {
            const float phase = std::fmod(run->stateSeconds, 1.0F);
            c.fillRect(prism::Rect{r.x + 5.0F * v.stateZoom, r.bottom() - 3.0F, (r.w - 5.0F * v.stateZoom) * phase, 2.0F}, kActiveColor);
        }
    }
    // How to use it, along the bottom.
    const char* hint = m_drag == Drag::Link ? "Release (or click) on a state to make the transition - Esc cancels"
                                               : "Drag from a state's edge to another state: a transition   |   Double-click a state: its graph   |   "
                                                 "Right-click: add, rename, remove";
    c.fillRect(prism::Rect{m_rect.x, m_rect.bottom() - 24.0F, m_rect.w, 24.0F}, prism::Color(18, 20, 24, 210));
    c.textIn(*t.small, prism::Rect{m_rect.x + 10.0F, m_rect.bottom() - 24.0F, m_rect.w - 20.0F, 24.0F}, hint, prism::Color(160, 168, 184));
    c.popClip();
}

// ---------------------------------------------------------------------------------------- input

bool StateMachineView::onMouseDown(const prism::MouseEvent& e) {
    if (prism::Application::exists()) {
        prism::Application::get().setFocus(this);
    }
    const Node* machine = Machine();
    if (machine == nullptr) {
        return true;
    }
    Editor::AnimationGraphViewState& v = m_panel.m_editor.m_animGraph;
    m_mouse = e.pos;
    m_dragFrom = e.pos;
    const int box = BoxAt(*machine, e.pos);
    const int transition = box == kNone ? TransitionAt(*machine, e.pos) : -1;
    if (m_linkPending) {
        // The transition from the menu ends where this click lands.
        m_linkPending = false;
        if (e.button == prism::MouseButton::Left) {
            onMouseUp(e);
        } else {
            m_drag = Drag::None;
            invalidate();
        }
        return true;
    }
    if (e.button == prism::MouseButton::Right) {
        ShowMenu(e.pos, box, transition);
        return true;
    }
    if (e.button == prism::MouseButton::Middle) {
        m_drag = Drag::Pan;
        return true;
    }
    if (e.button != prism::MouseButton::Left) {
        return false;
    }
    if (e.clicks >= 2) {
        if (box >= 0) {
            OpenState(static_cast<std::size_t>(box));
        } else if (box == kNone && transition < 0) {
            AddState(ToWorld(e.pos), Asset::AssetGuid{});
        }
        invalidate();
        return true;
    }
    v.pickedState = box >= 0 || box == kEntry || box == kAny ? box : -1;
    v.pickedTransition = transition;
    if (box == kEntry || box == kAny) {
        v.pickedState = -1;
    }
    m_panel.m_sideDirty = true;
    if (box != kNone) {
        m_dragBox = box;
        const bool link = (e.mods & prism::ModShift) != 0U || OnEdge(*machine, box, e.pos);
        m_drag = link ? Drag::Link : Drag::Move;
    } else if (transition < 0) {
        m_drag = Drag::Pan;
    }
    invalidate();
    return true;
}

bool StateMachineView::onMouseMove(const prism::MouseEvent& e) {
    if (IsGestureCancel(e.pos)) {
        CancelDrag();
        return true;
    }
    if (const Node* machine = Machine(); machine != nullptr && m_drag == Drag::None) {
        const int box = BoxAt(*machine, e.pos);
        const bool edge = box != kNone && OnEdge(*machine, box, e.pos);
        if (box != m_hoverBox || edge != m_hoverEdge) {
            m_hoverBox = box;
            m_hoverEdge = edge;
            invalidate();
        }
    }
    if (m_drag == Drag::Pan) {
        Editor::AnimationGraphViewState& v = m_panel.m_editor.m_animGraph;
        v.statePanX -= (e.pos.x - m_mouse.x) / v.stateZoom;
        v.statePanY -= (e.pos.y - m_mouse.y) / v.stateZoom;
    }
    m_mouse = e.pos;
    if (m_drag != Drag::None) {
        invalidate();
    }
    return true;
}

bool StateMachineView::onMouseUp(const prism::MouseEvent& e) {
    if (IsGestureCancel(e.pos)) {
        CancelDrag();
        return true;
    }
    m_mouse = e.pos;
    const Node* machine = Machine();
    if (machine != nullptr && m_drag == Drag::Move && (std::abs(e.pos.x - m_dragFrom.x) > 2.0F || std::abs(e.pos.y - m_dragFrom.y) > 2.0F)) {
        const prism::Rect moved = Box(*machine, m_dragBox);
        const std::uint32_t id = machine->id;
        const int box = m_dragBox;
        m_panel.Edit([&](Scene::AnimationGraphComponent& g) {
            const Math::Vector2 at{std::round(moved.x / 8.0F) * 8.0F, std::round(moved.y / 8.0F) * 8.0F};
            if (box >= 0) {
                static_cast<void>(SetAnimationStatePosition(g.nodes, id, static_cast<std::size_t>(box), at));
            } else if (auto node = std::ranges::find(g.nodes, id, &Node::id); node != g.nodes.end()) {
                (box == kEntry ? node->entryPosition : node->anyPosition) = at;
            }
        });
    } else if (machine != nullptr && m_drag == Drag::Link) {
        const int target = BoxAt(*machine, e.pos);
        const std::uint32_t id = machine->id;
        const int from = m_dragBox;
        Editor::AnimationGraphViewState& v = m_panel.m_editor.m_animGraph;
        if (target >= 0 && from == kEntry) {
            m_panel.EditNode(id, [target](Node& n) { n.entryState = static_cast<std::uint32_t>(target); });
        } else if (target >= 0 && target != from && from != kEntry) {
            std::optional<std::size_t> added;
            const std::uint32_t source = from == kAny ? Scene::kAnyAnimationState : static_cast<std::uint32_t>(from);
            m_panel.Edit([&](Scene::AnimationGraphComponent& g) {
                added = AddAnimationTransition(g.nodes, id, source, static_cast<std::uint32_t>(target));
            });
            if (added.has_value()) {
                v.pickedTransition = static_cast<int>(*added);
                v.pickedState = -1;
            }
        }
    }
    m_drag = Drag::None;
    m_dragBox = kNone;
    invalidate();
    return true;
}

void StateMachineView::CancelDrag() {
    // A move or link in progress is dropped (the pan already applied stays).
    m_drag = Drag::None;
    m_dragBox = kNone;
    invalidate();
}

bool StateMachineView::onMouseWheel(const prism::MouseEvent& e) {
    Editor::AnimationGraphViewState& v = m_panel.m_editor.m_animGraph;
    const Math::Vector2 anchor = ToWorld(e.pos);
    // In proportion to the wheel: a pinch sends many small deltas, a fixed step per event over-zoomed.
    v.stateZoom = std::clamp(v.stateZoom * std::pow(1.15F, e.wheel.y), 0.3F, 2.0F);
    v.statePanX = anchor.x - (e.pos.x - m_rect.x) / v.stateZoom;
    v.statePanY = anchor.y - (e.pos.y - m_rect.y) / v.stateZoom;
    invalidate();
    return true;
}

bool StateMachineView::onKeyDown(const prism::KeyEvent& e) {
    switch (e.key) {
        case prism::Key::Escape:
            if (m_drag == Drag::Link) {
                m_drag = Drag::None;
                m_linkPending = false;
                invalidate();
                return true;
            }
            m_panel.CloseFocus();
            return true;
        case prism::Key::F2:
            if (m_panel.m_editor.m_animGraph.pickedState >= 0) {
                RenameState(static_cast<std::size_t>(m_panel.m_editor.m_animGraph.pickedState));
            }
            return true;
        case prism::Key::Delete:
        case prism::Key::Backspace: DeletePicked(); return true;
        case prism::Key::F: Frame(); return true;
        default: return false;
    }
}

// -------------------------------------------------------------------------------------- editing

void StateMachineView::DeletePicked() {
    const Node* machine = Machine();
    if (machine == nullptr) {
        return;
    }
    Editor::AnimationGraphViewState& v = m_panel.m_editor.m_animGraph;
    const std::uint32_t id = machine->id;
    if (v.pickedTransition >= 0) {
        const auto index = static_cast<std::size_t>(v.pickedTransition);
        m_panel.Edit([&](Scene::AnimationGraphComponent& g) { static_cast<void>(RemoveAnimationTransition(g.nodes, id, index)); });
        v.pickedTransition = -1;
    } else if (v.pickedState >= 0) {
        const auto slot = static_cast<std::size_t>(v.pickedState);
        m_panel.Edit([&](Scene::AnimationGraphComponent& g) { static_cast<void>(RemoveAnimationGraphInputSlot(g.nodes, id, slot)); });
        v.pickedState = -1;
    }
    invalidate();
}

void StateMachineView::OpenState(const std::size_t slot) {
    const Node* machine = Machine();
    if (machine == nullptr || slot >= machine->inputs.size()) {
        return;
    }
    m_panel.OpenStateGraph(machine->id, static_cast<std::uint32_t>(slot));
}

bool StateMachineView::OnEdge(const Node& machine, const int box, const prism::Vec2 screen) const {
    if (box < 0) {
        return false;
    }
    const prism::Rect r = ScreenBox(machine, box);
    const float band = std::clamp(9.0F * m_panel.m_editor.m_animGraph.stateZoom, 6.0F, 14.0F);
    return r.contains(screen) && !r.deflated(band).contains(screen);
}

void StateMachineView::BeginTransition(const int slot) {
    m_dragBox = slot;
    m_drag = Drag::Link;
    m_linkPending = true;
    if (prism::Application::exists()) {
        m_mouse = prism::Application::get().mousePos();
        prism::Application::get().setFocus(this);
    }
    invalidate();
}

void StateMachineView::RenameState(const std::size_t slot) {
    const Node* machine = Machine();
    if (machine == nullptr || slot >= machine->inputs.size()) {
        return;
    }
    const prism::Rect r = ScreenBox(*machine, static_cast<int>(slot));
    const std::uint32_t id = machine->id;
    prism::showTextPopup(this, prism::Vec2{r.x + 8.0F, r.y + 4.0F}, StateName(*machine, slot), [this, id, slot](const std::string& to) {
        if (to.empty()) {
            return false;
        }
        m_panel.EditNode(id, [slot, &to](Node& n) {
            if (slot < n.inputs.size()) {
                if (n.stateNames.size() < n.inputs.size()) {
                    n.stateNames.resize(n.inputs.size());
                }
                n.stateNames[slot] = to;
            }
        });
        m_panel.m_sideDirty = true;
        return true;
    });
}

void StateMachineView::AddState(const Math::Vector2 at, const Asset::AssetGuid clip) {
    const Node* machine = Machine();
    if (machine == nullptr) {
        return;
    }
    const std::uint32_t id = machine->id;
    std::optional<std::size_t> added;
    m_panel.Edit([&](Scene::AnimationGraphComponent& g) {
        added = AddAnimationState(g.nodes, id, Math::Vector2{at.x - kStateW * 0.5F, at.y - kStateH * 0.5F});
        if (!added.has_value() || clip == Asset::AssetGuid{}) {
            return;
        }
        // Playing that clip, and named after it.
        const auto node = std::ranges::find(g.nodes, id, &Node::id);
        if (node != g.nodes.end() && *added < node->inputs.size()) {
            const std::uint32_t child = node->inputs[*added];
            if (const auto played = std::ranges::find(g.nodes, child, &Node::id); played != g.nodes.end()) {
                played->clip = clip;
            }
            const std::string name = m_panel.m_editor.AnimationClipName(clip);
            const auto again = std::ranges::find(g.nodes, id, &Node::id);
            if (!name.empty() && *added < again->stateNames.size()) {
                again->stateNames[*added] = name;
            }
        }
    });
    Editor::AnimationGraphViewState& v = m_panel.m_editor.m_animGraph;
    v.pickedState = added.has_value() ? static_cast<int>(*added) : -1;
    v.pickedTransition = -1;
    m_panel.m_sideDirty = true;
    invalidate();
}

void StateMachineView::onMouseLeave() {
    if (m_hoverBox != kNone || m_hoverEdge) {
        m_hoverBox = kNone;
        m_hoverEdge = false;
        invalidate();
    }
}

prism::Cursor StateMachineView::cursor() const {
    if (m_drag == Drag::Link || m_hoverEdge) {
        return prism::Cursor::Hand;
    }
    return m_drag == Drag::Move ? prism::Cursor::SizeAll : prism::Cursor::Arrow;
}

void StateMachineView::ShowMenu(const prism::Vec2 at, const int box, const int transition) {
    const Node* machine = Machine();
    if (machine == nullptr) {
        return;
    }
    Editor::AnimationGraphViewState& v = m_panel.m_editor.m_animGraph;
    const std::uint32_t id = machine->id;
    std::vector<prism::MenuItem> items;
    if (box >= 0) {
        const auto slot = static_cast<std::uint32_t>(box);
        v.pickedState = box;
        v.pickedTransition = -1;
        m_panel.m_sideDirty = true;
        items.push_back(prism::MenuItem::Header(StateName(*machine, slot)));
        items.push_back(prism::MenuItem::Item("Open State Graph", [this, slot] { OpenState(slot); }, "Double-click", prism::Icon::StateGraph));
        items.push_back(prism::MenuItem::Item("Rename", [this, slot] { RenameState(slot); }, "F2"));
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item("Add Transition...", [this, box] { BeginTransition(box); }, "Drag from edge", prism::Icon::NodeTransition));
        std::vector<prism::MenuItem> targets;
        for (std::size_t other = 0U; other < machine->inputs.size(); ++other) {
            if (other == slot) {
                continue;
            }
            targets.push_back(prism::MenuItem::Item(StateName(*machine, other), [this, id, slot, other] {
                std::optional<std::size_t> added;
                m_panel.Edit([&](Scene::AnimationGraphComponent& g) { added = AddAnimationTransition(g.nodes, id, slot, static_cast<std::uint32_t>(other)); });
                if (added.has_value()) {
                    m_panel.m_editor.m_animGraph.pickedTransition = static_cast<int>(*added);
                    m_panel.m_editor.m_animGraph.pickedState = -1;
                }
            }));
        }
        if (targets.empty()) {
            targets.push_back(prism::MenuItem::Item("No other state", {}).disabled());
        }
        items.push_back(prism::MenuItem::Sub("Transition To", std::move(targets), prism::Icon::NodeTransition));
        items.push_back(prism::MenuItem::Item("Transition From Any State", [this, id, slot] {
            m_panel.Edit([&](Scene::AnimationGraphComponent& g) { static_cast<void>(AddAnimationTransition(g.nodes, id, Scene::kAnyAnimationState, slot)); });
        }));
        auto entry = prism::MenuItem::Item("Set as Entry State", [this, id, slot] { m_panel.EditNode(id, [slot](Node& n) { n.entryState = slot; }); }, "",
                                           prism::Icon::NodeEntry);
        if (machine->entryState == slot) {
            entry.enabled = false;
        }
        items.push_back(std::move(entry));
        items.push_back(prism::MenuItem::Sep());
        auto remove = prism::MenuItem::Item("Delete State", [this] { DeletePicked(); }, "Del", prism::Icon::Trash);
        if (machine->inputs.size() <= 1U) {
            remove.enabled = false;   // a machine keeps one state
        }
        items.push_back(std::move(remove));
    } else if (transition >= 0) {
        v.pickedTransition = transition;
        v.pickedState = -1;
        m_panel.m_sideDirty = true;
        const auto index = static_cast<std::size_t>(transition);
        const Scene::AnimationGraphTransition& tr = machine->transitions[index];
        const std::string from = tr.fromState == Scene::kAnyAnimationState ? std::string{"Any State"} : StateName(*machine, tr.fromState);
        items.push_back(prism::MenuItem::Header(from + "  ->  " + StateName(*machine, tr.toState)));
        const bool enabled = tr.enabled;
        items.push_back(prism::MenuItem::Check("Enabled", enabled, [this, id, index, enabled] {
            m_panel.EditNode(id, [index, enabled](Node& n) {
                if (index < n.transitions.size()) {
                    n.transitions[index].enabled = !enabled;
                }
            });
        }));
        if (tr.fromState != Scene::kAnyAnimationState) {
            const std::uint32_t back = tr.fromState;
            const std::uint32_t to = tr.toState;
            const bool exists = std::ranges::any_of(machine->transitions, [back, to](const Scene::AnimationGraphTransition& o) {
                return o.fromState == to && o.toState == back;
            });
            auto reverse = prism::MenuItem::Item("Add Reverse Transition", [this, id, back, to] {
                m_panel.Edit([&](Scene::AnimationGraphComponent& g) { static_cast<void>(AddAnimationTransition(g.nodes, id, to, back)); });
            });
            if (exists) {
                reverse.enabled = false;
            }
            items.push_back(std::move(reverse));
        }
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item("Delete Transition", [this] { DeletePicked(); }, "Del", prism::Icon::Trash));
    } else {
        const Math::Vector2 world = ToWorld(at);
        items.push_back(prism::MenuItem::Header("State Machine"));
        items.push_back(prism::MenuItem::Item("Add State", [this, world] { AddState(world, Asset::AssetGuid{}); }, "Double-click", prism::Icon::Plus));
        std::vector<prism::MenuItem> clips;
        for (const Asset::AssetGuid clip : m_panel.ClipChoices()) {
            clips.push_back(prism::MenuItem::Item(m_panel.m_editor.AnimationClipName(clip), [this, world, clip] { AddState(world, clip); }, "",
                                                  prism::Icon::AnimClip));
        }
        if (clips.empty()) {
            clips.push_back(prism::MenuItem::Item("No animations in the project", {}).disabled());
        }
        items.push_back(prism::MenuItem::Sub("Add State Playing", std::move(clips), prism::Icon::AnimClip));
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item("Frame All", [this] { Frame(); }, "F", prism::Icon::Frame));
        items.push_back(prism::MenuItem::Item("Up", [this] { m_panel.CloseFocus(); }, "Esc", prism::Icon::ChevronLeft));
    }
    prism::showContextMenu(std::move(items), at);
    invalidate();
}

} // namespace UVE::Editor
