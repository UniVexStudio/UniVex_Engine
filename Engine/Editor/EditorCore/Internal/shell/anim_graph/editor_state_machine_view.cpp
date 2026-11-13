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

using Kind = Scene::AnimationGraphNodeKindUVE;
using NodeUVE = Scene::AnimationGraphNodeUVE;

constexpr float kStateWUVE = 168.0F;
constexpr float kStateHUVE = 52.0F;
constexpr float kPortWUVE = 96.0F;
constexpr float kPortHUVE = 30.0F;

const prism::Color kEntryColorUVE{72, 168, 96};
const prism::Color kAnyColorUVE{214, 146, 64};
const prism::Color kStateColorUVE{58, 66, 104};
const prism::Color kActiveColorUVE{86, 156, 236};
const prism::Color kTakenColorUVE{255, 214, 96};

/// Where the ray from `centre` towards `towards` leaves `box`.
[[nodiscard]] prism::Vec2 EdgePointUVE(const prism::Rect box, const prism::Vec2 centre, const prism::Vec2 towards) {
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

[[nodiscard]] float SegmentDistanceUVE(const prism::Vec2 p, const prism::Vec2 a, const prism::Vec2 b) {
    const float vx = b.x - a.x;
    const float vy = b.y - a.y;
    const float len = vx * vx + vy * vy;
    const float t = len > 1.0e-6F ? std::clamp(((p.x - a.x) * vx + (p.y - a.y) * vy) / len, 0.0F, 1.0F) : 0.0F;
    const float cx = a.x + vx * t - p.x;
    const float cy = a.y + vy * t - p.y;
    return std::sqrt(cx * cx + cy * cy);
}

void ArrowUVE(prism::Canvas& c, const prism::Vec2 from, const prism::Vec2 to, const prism::Color color, const float thickness,
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

const NodeUVE* StateMachineViewUVE::MachineUVE() const {
    const Scene::AnimationGraphComponentUVE* graph = m_panel.GraphPtrUVE();
    if (graph == nullptr) {
        return nullptr;
    }
    const auto found = std::ranges::find(graph->nodes, m_panel.m_editor.m_animGraph.focus, &NodeUVE::id);
    return found != graph->nodes.end() && found->kind == Kind::StateMachine ? &*found : nullptr;
}

std::size_t StateMachineViewUVE::MachineIndexUVE() const {
    const Scene::AnimationGraphComponentUVE* graph = m_panel.GraphPtrUVE();
    if (graph == nullptr) {
        return static_cast<std::size_t>(-1);
    }
    const auto found = std::ranges::find(graph->nodes, m_panel.m_editor.m_animGraph.focus, &NodeUVE::id);
    return found != graph->nodes.end() ? static_cast<std::size_t>(found - graph->nodes.begin()) : static_cast<std::size_t>(-1);
}

prism::Vec2 StateMachineViewUVE::ToScreenUVE(const Math::Vector2UVE world) const {
    const EditorUVE::AnimationGraphViewStateUVE& v = m_panel.m_editor.m_animGraph;
    return prism::Vec2{m_rect.x + (world.x - v.statePanX) * v.stateZoom, m_rect.y + (world.y - v.statePanY) * v.stateZoom};
}

Math::Vector2UVE StateMachineViewUVE::ToWorldUVE(const prism::Vec2 screen) const {
    const EditorUVE::AnimationGraphViewStateUVE& v = m_panel.m_editor.m_animGraph;
    return Math::Vector2UVE{v.statePanX + (screen.x - m_rect.x) / v.stateZoom, v.statePanY + (screen.y - m_rect.y) / v.stateZoom};
}

prism::Rect StateMachineViewUVE::BoxUVE(const NodeUVE& machine, const int box) const {
    Math::Vector2UVE at{};
    float w = kStateWUVE;
    float h = kStateHUVE;
    if (box == kEntryUVE) {
        at = machine.entryPosition;
        w = kPortWUVE;
        h = kPortHUVE;
    } else if (box == kAnyUVE) {
        at = machine.anyPosition;
        w = kPortWUVE;
        h = kPortHUVE;
    } else {
        at = AnimationStatePositionUVE(machine, static_cast<std::size_t>(box));
    }
    if (m_drag == DragUVE::Move && m_dragBox == box) {
        const float zoom = m_panel.m_editor.m_animGraph.stateZoom;
        at.x += (m_mouse.x - m_dragFrom.x) / zoom;
        at.y += (m_mouse.y - m_dragFrom.y) / zoom;
    }
    return prism::Rect{at.x, at.y, w, h};
}

prism::Rect StateMachineViewUVE::ScreenBoxUVE(const NodeUVE& machine, const int box) const {
    const prism::Rect r = BoxUVE(machine, box);
    const prism::Vec2 p = ToScreenUVE(Math::Vector2UVE{r.x, r.y});
    const float zoom = m_panel.m_editor.m_animGraph.stateZoom;
    return prism::Rect{p.x, p.y, r.w * zoom, r.h * zoom};
}

int StateMachineViewUVE::BoxAtUVE(const NodeUVE& machine, const prism::Vec2 screen) const {
    for (std::size_t slot = machine.inputs.size(); slot-- > 0U;) {
        if (ScreenBoxUVE(machine, static_cast<int>(slot)).contains(screen)) {
            return static_cast<int>(slot);
        }
    }
    if (ScreenBoxUVE(machine, kEntryUVE).contains(screen)) {
        return kEntryUVE;
    }
    if (ScreenBoxUVE(machine, kAnyUVE).contains(screen)) {
        return kAnyUVE;
    }
    return kNoneUVE;
}

bool StateMachineViewUVE::TransitionSegmentUVE(const NodeUVE& machine, const std::size_t index, prism::Vec2& from, prism::Vec2& to) const {
    const Scene::AnimationGraphTransitionUVE& t = machine.transitions[index];
    if (t.toState >= machine.inputs.size() || (t.fromState != Scene::kAnyAnimationStateUVE && t.fromState >= machine.inputs.size())) {
        return false;
    }
    const int fromBox = t.fromState == Scene::kAnyAnimationStateUVE ? kAnyUVE : static_cast<int>(t.fromState);
    const prism::Rect a = ScreenBoxUVE(machine, fromBox);
    const prism::Rect b = ScreenBoxUVE(machine, static_cast<int>(t.toState));
    prism::Vec2 ca{a.x + a.w * 0.5F, a.y + a.h * 0.5F};
    prism::Vec2 cb{b.x + b.w * 0.5F, b.y + b.h * 0.5F};
    // Two states leading to each other: each direction on its own lane.
    const bool paired = fromBox >= 0 && std::ranges::any_of(machine.transitions, [&t](const Scene::AnimationGraphTransitionUVE& o) {
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
    from = EdgePointUVE(a, ca, cb);
    to = EdgePointUVE(b, cb, ca);
    return true;
}

int StateMachineViewUVE::TransitionAtUVE(const NodeUVE& machine, const prism::Vec2 screen) const {
    int best = -1;
    float bestDistance = 7.0F;
    for (std::size_t i = 0U; i < machine.transitions.size(); ++i) {
        prism::Vec2 a;
        prism::Vec2 b;
        if (TransitionSegmentUVE(machine, i, a, b)) {
            const float d = SegmentDistanceUVE(screen, a, b);
            if (d < bestDistance) {
                bestDistance = d;
                best = static_cast<int>(i);
            }
        }
    }
    return best;
}

std::string StateMachineViewUVE::StateNameUVE(const NodeUVE& machine, const std::size_t slot) const {
    const Scene::AnimationGraphComponentUVE* graph = m_panel.GraphPtrUVE();
    if (graph != nullptr && slot < machine.inputs.size() && machine.inputs[slot] != 0U) {
        const auto child = std::ranges::find(graph->nodes, machine.inputs[slot], &NodeUVE::id);
        if (child != graph->nodes.end() && !child->name.empty()) {
            return child->name;
        }
    }
    return AnimationGraphSlotLabelUVE(Kind::StateMachine, slot);
}

std::string StateMachineViewUVE::StateDetailUVE(const NodeUVE& machine, const std::size_t slot) const {
    const Scene::AnimationGraphComponentUVE* graph = m_panel.GraphPtrUVE();
    if (graph == nullptr || slot >= machine.inputs.size() || machine.inputs[slot] == 0U) {
        return {};
    }
    const auto child = std::ranges::find(graph->nodes, machine.inputs[slot], &NodeUVE::id);
    if (child == graph->nodes.end()) {
        return {};
    }
    if (child->kind == Kind::Clip) {
        const std::string& clip = m_panel.m_editor.AnimationClipNameUVE(child->clip);
        return clip;
    }
    return AnimationGraphKindLabelUVE(child->kind);
}

void StateMachineViewUVE::FrameUVE() {
    const NodeUVE* machine = MachineUVE();
    if (machine == nullptr || m_rect.w < 10.0F || m_rect.h < 10.0F) {
        return;
    }
    prism::Rect bounds = BoxUVE(*machine, kEntryUVE);
    const auto grow = [&bounds](const prism::Rect r) {
        const float x0 = std::min(bounds.x, r.x);
        const float y0 = std::min(bounds.y, r.y);
        bounds = prism::Rect{x0, y0, std::max(bounds.right(), r.right()) - x0, std::max(bounds.bottom(), r.bottom()) - y0};
    };
    grow(BoxUVE(*machine, kAnyUVE));
    for (std::size_t slot = 0U; slot < machine->inputs.size(); ++slot) {
        grow(BoxUVE(*machine, static_cast<int>(slot)));
    }
    EditorUVE::AnimationGraphViewStateUVE& v = m_panel.m_editor.m_animGraph;
    v.stateZoom = std::clamp(std::min((m_rect.w - 80.0F) / std::max(1.0F, bounds.w), (m_rect.h - 80.0F) / std::max(1.0F, bounds.h)), 0.4F, 1.4F);
    v.statePanX = bounds.x + bounds.w * 0.5F - m_rect.w * 0.5F / v.stateZoom;
    v.statePanY = bounds.y + bounds.h * 0.5F - m_rect.h * 0.5F / v.stateZoom;
    v.stateFramed = true;
    v.stateMachine = machine->id;
    invalidate();
}

// ---------------------------------------------------------------------------------------- paint

void StateMachineViewUVE::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    c.fillRect(m_rect, prism::Color(26, 28, 33));
    const NodeUVE* machine = MachineUVE();
    if (machine == nullptr) {
        return;
    }
    EditorUVE::AnimationGraphViewStateUVE& v = m_panel.m_editor.m_animGraph;
    if (!v.stateFramed || v.stateMachine != machine->id) {
        FrameUVE();
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
    const Scene::AnimationGraphComponentUVE* graph = m_panel.GraphPtrUVE();
    const std::size_t index = MachineIndexUVE();
    const Scene::AnimationGraphNodeStateUVE* run =
        graph != nullptr && index < graph->nodeStates.size() && graph->nodeStates.size() == graph->nodes.size() ? &graph->nodeStates[index] : nullptr;
    const bool fading = run != nullptr && run->fadeSeconds > 0.0F && run->fadeElapsedSeconds < run->fadeSeconds;

    // Entry's arrow to where the machine starts.
    if (machine->entryState < machine->inputs.size()) {
        const prism::Rect e = ScreenBoxUVE(*machine, kEntryUVE);
        const prism::Rect s = ScreenBoxUVE(*machine, static_cast<int>(machine->entryState));
        const prism::Vec2 ce{e.x + e.w * 0.5F, e.y + e.h * 0.5F};
        const prism::Vec2 cs{s.x + s.w * 0.5F, s.y + s.h * 0.5F};
        ArrowUVE(c, EdgePointUVE(e, ce, cs), EdgePointUVE(s, cs, ce), kEntryColorUVE, 2.0F, false);
    }
    // Transitions.
    for (std::size_t i = 0U; i < machine->transitions.size(); ++i) {
        prism::Vec2 a;
        prism::Vec2 b;
        if (!TransitionSegmentUVE(*machine, i, a, b)) {
            continue;
        }
        const Scene::AnimationGraphTransitionUVE& tr = machine->transitions[i];
        const bool picked = static_cast<int>(i) == v.pickedTransition;
        const bool taken = fading && run != nullptr && run->lastTransition == i;
        prism::Color color = tr.fromState == Scene::kAnyAnimationStateUVE ? kAnyColorUVE : prism::Color(170, 178, 196);
        if (!tr.enabled) {
            color = prism::Color(96, 100, 110);
        }
        if (taken) {
            color = kTakenColorUVE;
        }
        if (picked) {
            color = t.accent;
        }
        ArrowUVE(c, a, b, color, picked || taken ? 2.6F : 1.7F, !tr.enabled);
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
    if (m_drag == DragUVE::Link) {
        const prism::Rect from = ScreenBoxUVE(*machine, m_dragBox);
        ArrowUVE(c, prism::Vec2{from.x + from.w * 0.5F, from.y + from.h * 0.5F}, m_mouse, t.accent, 2.0F, true);
    }
    // Entry, Any and the states.
    const auto port = [&](const int box, const char* label, const prism::Color color) {
        const prism::Rect r = ScreenBoxUVE(*machine, box);
        c.fillRoundRect(r, 14.0F * v.stateZoom, color);
        c.textIn(*t.semibold, r, label, prism::Color(255, 255, 255), prism::HAlign::Center);
    };
    port(kEntryUVE, "Entry", kEntryColorUVE);
    port(kAnyUVE, "Any State", kAnyColorUVE);
    for (std::size_t slot = 0U; slot < machine->inputs.size(); ++slot) {
        const prism::Rect r = ScreenBoxUVE(*machine, static_cast<int>(slot));
        if (!r.intersects(m_rect)) {
            continue;
        }
        const bool active = run != nullptr && run->activeState == slot;
        const bool leaving = fading && run != nullptr && run->previousState == slot;
        const bool picked = v.pickedState == static_cast<int>(slot);
        const float radius = 6.0F * v.stateZoom;
        c.shadow(r, radius, 6, prism::Color(0, 0, 0, 90));
        c.fillRoundRect(r, radius, active ? prism::Color(48, 86, 138) : kStateColorUVE);
        c.fillRoundRect(prism::Rect{r.x, r.y, 5.0F * v.stateZoom, r.h}, radius, active ? kActiveColorUVE : prism::Color(110, 120, 180));
        if (active || leaving) {
            c.strokeRoundRect(r, radius, active ? kActiveColorUVE : prism::Color(120, 150, 200, 140), 2);
        }
        if (picked) {
            c.strokeRoundRect(r.inflated(2.0F), radius + 2.0F, prism::Color(255, 255, 255), 2);
        }
        if (machine->entryState == slot) {
            prism::drawIcon(c, prism::Icon::NodeEntry, prism::Rect{r.right() - 20.0F * v.stateZoom, r.y + 4.0F * v.stateZoom, 14.0F * v.stateZoom, 14.0F * v.stateZoom},
                            kEntryColorUVE);
        }
        if (v.stateZoom > 0.5F) {
            c.textIn(*t.semibold, prism::Rect{r.x + 12.0F * v.stateZoom, r.y + 4.0F * v.stateZoom, r.w - 34.0F * v.stateZoom, r.h * 0.5F},
                     StateNameUVE(*machine, slot), prism::Color(240, 242, 248));
            c.textIn(*t.small, prism::Rect{r.x + 12.0F * v.stateZoom, r.y + r.h * 0.5F, r.w - 18.0F * v.stateZoom, r.h * 0.45F},
                     StateDetailUVE(*machine, slot), prism::Color(170, 182, 210));
        }
        // How far into the state the preview is, when it runs.
        if (active && run != nullptr) {
            const float phase = std::fmod(run->stateSeconds, 1.0F);
            c.fillRect(prism::Rect{r.x + 5.0F * v.stateZoom, r.bottom() - 3.0F, (r.w - 5.0F * v.stateZoom) * phase, 2.0F}, kActiveColorUVE);
        }
    }
    c.popClip();
}

// ---------------------------------------------------------------------------------------- input

bool StateMachineViewUVE::onMouseDown(const prism::MouseEvent& e) {
    if (prism::Application::exists()) {
        prism::Application::get().setFocus(this);
    }
    const NodeUVE* machine = MachineUVE();
    if (machine == nullptr) {
        return true;
    }
    EditorUVE::AnimationGraphViewStateUVE& v = m_panel.m_editor.m_animGraph;
    m_mouse = e.pos;
    m_dragFrom = e.pos;
    const int box = BoxAtUVE(*machine, e.pos);
    const int transition = box == kNoneUVE ? TransitionAtUVE(*machine, e.pos) : -1;
    if (e.button == prism::MouseButton::Right) {
        ShowMenuUVE(e.pos, box, transition);
        return true;
    }
    if (e.button == prism::MouseButton::Middle) {
        m_drag = DragUVE::Pan;
        return true;
    }
    if (e.button != prism::MouseButton::Left) {
        return false;
    }
    if (e.clicks >= 2) {
        if (box >= 0) {
            OpenStateUVE(static_cast<std::size_t>(box));
        } else if (box == kNoneUVE && transition < 0) {
            const Math::Vector2UVE at = ToWorldUVE(e.pos);
            const std::uint32_t id = machine->id;
            std::optional<std::size_t> added;
            m_panel.EditUVE([&](Scene::AnimationGraphComponentUVE& g) {
                added = AddAnimationStateUVE(g.nodes, id, Math::Vector2UVE{at.x - kStateWUVE * 0.5F, at.y - kStateHUVE * 0.5F});
            });
            v.pickedState = added.has_value() ? static_cast<int>(*added) : -1;
            v.pickedTransition = -1;
        }
        invalidate();
        return true;
    }
    v.pickedState = box >= 0 || box == kEntryUVE || box == kAnyUVE ? box : -1;
    v.pickedTransition = transition;
    if (box == kEntryUVE || box == kAnyUVE) {
        v.pickedState = -1;
    }
    m_panel.m_sideDirty = true;
    if (box != kNoneUVE) {
        m_dragBox = box;
        m_drag = (e.mods & prism::ModShift) != 0U ? DragUVE::Link : DragUVE::Move;
    } else if (transition < 0) {
        m_drag = DragUVE::Pan;
    }
    invalidate();
    return true;
}

bool StateMachineViewUVE::onMouseMove(const prism::MouseEvent& e) {
    if (m_drag == DragUVE::Pan) {
        EditorUVE::AnimationGraphViewStateUVE& v = m_panel.m_editor.m_animGraph;
        v.statePanX -= (e.pos.x - m_mouse.x) / v.stateZoom;
        v.statePanY -= (e.pos.y - m_mouse.y) / v.stateZoom;
    }
    m_mouse = e.pos;
    if (m_drag != DragUVE::None) {
        invalidate();
    }
    return true;
}

bool StateMachineViewUVE::onMouseUp(const prism::MouseEvent& e) {
    m_mouse = e.pos;
    const NodeUVE* machine = MachineUVE();
    if (machine != nullptr && m_drag == DragUVE::Move && (std::abs(e.pos.x - m_dragFrom.x) > 2.0F || std::abs(e.pos.y - m_dragFrom.y) > 2.0F)) {
        const prism::Rect moved = BoxUVE(*machine, m_dragBox);
        const std::uint32_t id = machine->id;
        const int box = m_dragBox;
        m_panel.EditUVE([&](Scene::AnimationGraphComponentUVE& g) {
            const Math::Vector2UVE at{std::round(moved.x / 8.0F) * 8.0F, std::round(moved.y / 8.0F) * 8.0F};
            if (box >= 0) {
                static_cast<void>(SetAnimationStatePositionUVE(g.nodes, id, static_cast<std::size_t>(box), at));
            } else if (auto node = std::ranges::find(g.nodes, id, &NodeUVE::id); node != g.nodes.end()) {
                (box == kEntryUVE ? node->entryPosition : node->anyPosition) = at;
            }
        });
    } else if (machine != nullptr && m_drag == DragUVE::Link) {
        const int target = BoxAtUVE(*machine, e.pos);
        const std::uint32_t id = machine->id;
        const int from = m_dragBox;
        EditorUVE::AnimationGraphViewStateUVE& v = m_panel.m_editor.m_animGraph;
        if (target >= 0 && from == kEntryUVE) {
            m_panel.EditNodeUVE(id, [target](NodeUVE& n) { n.entryState = static_cast<std::uint32_t>(target); });
        } else if (target >= 0 && target != from && from != kEntryUVE) {
            std::optional<std::size_t> added;
            const std::uint32_t source = from == kAnyUVE ? Scene::kAnyAnimationStateUVE : static_cast<std::uint32_t>(from);
            m_panel.EditUVE([&](Scene::AnimationGraphComponentUVE& g) {
                added = AddAnimationTransitionUVE(g.nodes, id, source, static_cast<std::uint32_t>(target));
            });
            if (added.has_value()) {
                v.pickedTransition = static_cast<int>(*added);
                v.pickedState = -1;
            }
        }
    }
    m_drag = DragUVE::None;
    m_dragBox = kNoneUVE;
    invalidate();
    return true;
}

bool StateMachineViewUVE::onMouseWheel(const prism::MouseEvent& e) {
    EditorUVE::AnimationGraphViewStateUVE& v = m_panel.m_editor.m_animGraph;
    const Math::Vector2UVE anchor = ToWorldUVE(e.pos);
    v.stateZoom = std::clamp(v.stateZoom * (e.wheel.y > 0.0F ? 1.15F : 1.0F / 1.15F), 0.3F, 2.0F);
    v.statePanX = anchor.x - (e.pos.x - m_rect.x) / v.stateZoom;
    v.statePanY = anchor.y - (e.pos.y - m_rect.y) / v.stateZoom;
    invalidate();
    return true;
}

bool StateMachineViewUVE::onKeyDown(const prism::KeyEvent& e) {
    switch (e.key) {
        case prism::Key::Escape: m_panel.CloseFocusUVE(); return true;
        case prism::Key::Delete:
        case prism::Key::Backspace: DeletePickedUVE(); return true;
        case prism::Key::F: FrameUVE(); return true;
        default: return false;
    }
}

// -------------------------------------------------------------------------------------- editing

void StateMachineViewUVE::DeletePickedUVE() {
    const NodeUVE* machine = MachineUVE();
    if (machine == nullptr) {
        return;
    }
    EditorUVE::AnimationGraphViewStateUVE& v = m_panel.m_editor.m_animGraph;
    const std::uint32_t id = machine->id;
    if (v.pickedTransition >= 0) {
        const auto index = static_cast<std::size_t>(v.pickedTransition);
        m_panel.EditUVE([&](Scene::AnimationGraphComponentUVE& g) { static_cast<void>(RemoveAnimationTransitionUVE(g.nodes, id, index)); });
        v.pickedTransition = -1;
    } else if (v.pickedState >= 0) {
        const auto slot = static_cast<std::size_t>(v.pickedState);
        m_panel.EditUVE([&](Scene::AnimationGraphComponentUVE& g) { static_cast<void>(RemoveAnimationGraphInputSlotUVE(g.nodes, id, slot)); });
        v.pickedState = -1;
    }
    invalidate();
}

void StateMachineViewUVE::OpenStateUVE(const std::size_t slot) {
    const NodeUVE* machine = MachineUVE();
    const Scene::AnimationGraphComponentUVE* graph = m_panel.GraphPtrUVE();
    if (machine == nullptr || graph == nullptr || slot >= machine->inputs.size() || machine->inputs[slot] == 0U) {
        return;
    }
    // What the state plays: its own editor when it has one, else picked in the tree.
    const std::uint32_t child = machine->inputs[slot];
    const auto node = std::ranges::find(graph->nodes, child, &NodeUVE::id);
    if (node != graph->nodes.end() && (node->kind == Kind::StateMachine || node->kind == Kind::BlendSpace1D || node->kind == Kind::BlendSpace2D)) {
        m_panel.OpenFocusUVE(child);
    } else {
        m_panel.CloseFocusUVE();
        m_panel.m_selected = {child};
        m_panel.m_sideDirty = true;
        m_panel.m_shown.reset();
    }
}

void StateMachineViewUVE::ShowMenuUVE(const prism::Vec2 at, const int box, const int transition) {
    const NodeUVE* machine = MachineUVE();
    if (machine == nullptr) {
        return;
    }
    EditorUVE::AnimationGraphViewStateUVE& v = m_panel.m_editor.m_animGraph;
    const std::uint32_t id = machine->id;
    std::vector<prism::MenuItem> items;
    if (box >= 0) {
        const auto slot = static_cast<std::uint32_t>(box);
        v.pickedState = box;
        v.pickedTransition = -1;
        m_panel.m_sideDirty = true;
        items.push_back(prism::MenuItem::Item("Start Here", [this, id, slot] { m_panel.EditNodeUVE(id, [slot](NodeUVE& n) { n.entryState = slot; }); }, "",
                                              prism::Icon::NodeEntry));
        std::vector<prism::MenuItem> targets;
        for (std::size_t other = 0U; other < machine->inputs.size(); ++other) {
            if (other == slot) {
                continue;
            }
            targets.push_back(prism::MenuItem::Item(StateNameUVE(*machine, other), [this, id, slot, other] {
                std::optional<std::size_t> added;
                m_panel.EditUVE([&](Scene::AnimationGraphComponentUVE& g) { added = AddAnimationTransitionUVE(g.nodes, id, slot, static_cast<std::uint32_t>(other)); });
                if (added.has_value()) {
                    m_panel.m_editor.m_animGraph.pickedTransition = static_cast<int>(*added);
                    m_panel.m_editor.m_animGraph.pickedState = -1;
                }
            }));
        }
        if (targets.empty()) {
            targets.push_back(prism::MenuItem::Item("None", {}).disabled());
        }
        items.push_back(prism::MenuItem::Sub("Transition To", std::move(targets), prism::Icon::NodeTransition));
        items.push_back(prism::MenuItem::Item("Transition From Any State", [this, id, slot] {
            m_panel.EditUVE([&](Scene::AnimationGraphComponentUVE& g) { static_cast<void>(AddAnimationTransitionUVE(g.nodes, id, Scene::kAnyAnimationStateUVE, slot)); });
        }));
        items.push_back(prism::MenuItem::Item("Open What It Plays", [this, slot] { OpenStateUVE(slot); }));
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item("Remove State", [this] { DeletePickedUVE(); }, "Del", prism::Icon::Trash));
    } else if (transition >= 0) {
        v.pickedTransition = transition;
        v.pickedState = -1;
        m_panel.m_sideDirty = true;
        const auto index = static_cast<std::size_t>(transition);
        const bool enabled = machine->transitions[index].enabled;
        items.push_back(prism::MenuItem::Check("Enabled", enabled, [this, id, index, enabled] {
            m_panel.EditNodeUVE(id, [index, enabled](NodeUVE& n) {
                if (index < n.transitions.size()) {
                    n.transitions[index].enabled = !enabled;
                }
            });
        }));
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item("Remove Transition", [this] { DeletePickedUVE(); }, "Del", prism::Icon::Trash));
    } else {
        const Math::Vector2UVE world = ToWorldUVE(at);
        items.push_back(prism::MenuItem::Item("Add State Here", [this, id, world] {
            m_panel.EditUVE([&](Scene::AnimationGraphComponentUVE& g) {
                static_cast<void>(AddAnimationStateUVE(g.nodes, id, Math::Vector2UVE{world.x - kStateWUVE * 0.5F, world.y - kStateHUVE * 0.5F}));
            });
        }, "", prism::Icon::Plus));
        items.push_back(prism::MenuItem::Item("Frame All", [this] { FrameUVE(); }, "F", prism::Icon::Frame));
        items.push_back(prism::MenuItem::Item("Back to the Tree", [this] { m_panel.CloseFocusUVE(); }, "Esc", prism::Icon::ChevronLeft));
    }
    prism::showContextMenu(std::move(items), at);
    invalidate();
}

} // namespace UVE::Editor
