// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The State Machine view of the Anim Graph: a machine's states as boxes, Entry and Any beside
// them, its transitions as arrows. States are the machine's input slots; what each plays is the
// object wired into its slot, which stays in the tree graph.

#include "uve/editor/editor_uve.h"
#include "uve/editor/animation_graph_editing_uve.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "editor_animation_graph_widgets_uve.h"
#include "uve/component/animation_tree_component_uve.h"

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphObjectKindUVE;
using Scene::AnimationGraphObjectUVE;
using Scene::AnimationTransitionUVE;

/// Box identities in the view besides state slots.
constexpr int kEntryBoxUVE = -1;
constexpr int kAnyBoxUVE = -2;
constexpr int kNoBoxUVE = -3;

constexpr float kStateWidthUVE = 164.0F;
constexpr float kStateHeightUVE = 50.0F;
constexpr float kPillWidthUVE = 92.0F;
constexpr float kPillHeightUVE = 28.0F;
constexpr float kHandleRadiusUVE = 6.0F;
constexpr ImU32 kEntryColourUVE = IM_COL32(96, 186, 120, 255);
constexpr ImU32 kAnyColourUVE = IM_COL32(150, 118, 200, 255);
constexpr ImU32 kFadingUVE = IM_COL32(236, 170, 72, 255);

[[nodiscard]] ImVec2 AddUVE(const ImVec2 a, const ImVec2 b) { return ImVec2{a.x + b.x, a.y + b.y}; }
[[nodiscard]] ImVec2 SubUVE(const ImVec2 a, const ImVec2 b) { return ImVec2{a.x - b.x, a.y - b.y}; }
[[nodiscard]] ImVec2 ScaleUVE(const ImVec2 a, const float s) { return ImVec2{a.x * s, a.y * s}; }
[[nodiscard]] float LengthUVE(const ImVec2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }

/// Where the ray from a box's centre toward `toward` leaves the box.
[[nodiscard]] ImVec2 EdgePointUVE(const ImVec2 min, const ImVec2 max, const ImVec2 toward) {
    const ImVec2 centre{(min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F};
    const ImVec2 d = SubUVE(toward, centre);
    const float halfW = (max.x - min.x) * 0.5F;
    const float halfH = (max.y - min.y) * 0.5F;
    if (std::abs(d.x) < 1e-4F && std::abs(d.y) < 1e-4F) {
        return centre;
    }
    const float tx = std::abs(d.x) > 1e-4F ? halfW / std::abs(d.x) : FLT_MAX;
    const float ty = std::abs(d.y) > 1e-4F ? halfH / std::abs(d.y) : FLT_MAX;
    return AddUVE(centre, ScaleUVE(d, std::min(tx, ty)));
}

[[nodiscard]] float DistanceToSegmentUVE(const ImVec2 p, const ImVec2 a, const ImVec2 b) {
    const ImVec2 ab = SubUVE(b, a);
    const float lengthSquared = ab.x * ab.x + ab.y * ab.y;
    const float t = lengthSquared > 0.0F ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lengthSquared, 0.0F, 1.0F) : 0.0F;
    return LengthUVE(SubUVE(p, AddUVE(a, ScaleUVE(ab, t))));
}

void ArrowHeadUVE(ImDrawList* const draw, const ImVec2 at, const ImVec2 direction, const ImU32 colour, const float size) {
    const float length = LengthUVE(direction);
    if (length <= 0.0F) {
        return;
    }
    const ImVec2 d = ScaleUVE(direction, 1.0F / length);
    const ImVec2 n{-d.y, d.x};
    const ImVec2 tip = AddUVE(at, ScaleUVE(d, size * 0.6F));
    const ImVec2 back = SubUVE(at, ScaleUVE(d, size * 0.6F));
    draw->AddTriangleFilled(tip, AddUVE(back, ScaleUVE(n, size * 0.55F)), SubUVE(back, ScaleUVE(n, size * 0.55F)), colour);
}

} // namespace

void EditorUVE::DrawStateMachineViewUVE(const Scene::EntityUVE tree, const std::size_t objectIndex) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    auto& live = entityManager.GetComponentUVE<Scene::AnimationTreeComponentUVE>(tree);
    if (objectIndex >= live.objects.size()) {
        return;
    }
    AnimationGraphViewStateUVE& view = m_animGraph;
    const AnimationGraphObjectUVE machine = live.objects[objectIndex];
    const std::uint32_t machineId = machine.id;
    if (view.stateMachine != machineId) {
        view.stateMachine = machineId;
        view.stateFramed = false;
        view.pickedState = -1;
        view.pickedTransition = -1;
        view.stateDrag = kNoBoxUVE;
        view.linkFrom = kNoBoxUVE;
    }
    const bool writable = IsAuthoringCommandAllowedUVE();
    const std::size_t stateCount = machine.inputs.size();
    if (view.pickedState >= static_cast<int>(stateCount)) {
        view.pickedState = -1;
    }
    if (view.pickedTransition >= static_cast<int>(machine.transitions.size())) {
        view.pickedTransition = -1;
    }
    std::optional<std::function<void(Scene::AnimationTreeComponentUVE&)>> edit;

    // What runs: the machine's active state, the one fading out and the transition last taken.
    const bool running = live.objectStates.size() == live.objects.size() && live.objectStates[objectIndex].started;
    const Scene::AnimationGraphObjectStateUVE runtime = running ? live.objectStates[objectIndex] : Scene::AnimationGraphObjectStateUVE{};
    const auto childOf = [&live, &machine](const std::size_t slot) -> const AnimationGraphObjectUVE* {
        if (slot >= machine.inputs.size() || machine.inputs[slot] == 0U) {
            return nullptr;
        }
        const auto it = std::ranges::find(live.objects, machine.inputs[slot], &AnimationGraphObjectUVE::id);
        return it != live.objects.end() ? &*it : nullptr;
    };
    const auto childWeight = [&live, &machine](const std::size_t slot) {
        if (slot >= machine.inputs.size() || live.objectStates.size() != live.objects.size()) {
            return 0.0F;
        }
        const auto it = std::ranges::find(live.objects, machine.inputs[slot], &AnimationGraphObjectUVE::id);
        return it != live.objects.end() ? live.objectStates[static_cast<std::size_t>(it - live.objects.begin())].weight : 0.0F;
    };
    const auto stateName = [&](const std::size_t slot) {
        const AnimationGraphObjectUVE* const child = childOf(slot);
        return child != nullptr && !child->name.empty() ? child->name : AnimationGraphSlotLabelUVE(Kind::StateMachine, slot);
    };

    // ---- Canvas and its mapping ----------------------------------------------------------------------
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const auto boxMin = [&](const int box) -> Math::Vector2UVE {
        if (box == kEntryBoxUVE) {
            return machine.entryPosition;
        }
        if (box == kAnyBoxUVE) {
            return machine.anyPosition;
        }
        return AnimationStatePositionUVE(machine, static_cast<std::size_t>(box));
    };
    const auto boxSize = [](const int box) {
        return box < 0 ? ImVec2{kPillWidthUVE, kPillHeightUVE} : ImVec2{kStateWidthUVE, kStateHeightUVE};
    };
    if (!view.stateFramed && size.x > 0.0F && size.y > 0.0F) {
        float minX = FLT_MAX;
        float minY = FLT_MAX;
        float maxX = -FLT_MAX;
        float maxY = -FLT_MAX;
        for (int box = kAnyBoxUVE; box < static_cast<int>(stateCount); ++box) {
            const Math::Vector2UVE at = boxMin(box);
            minX = std::min(minX, at.x);
            minY = std::min(minY, at.y);
            maxX = std::max(maxX, at.x + boxSize(box).x);
            maxY = std::max(maxY, at.y + boxSize(box).y);
        }
        const float margin = 40.0F;
        view.stateZoom = std::clamp(std::min(size.x / (maxX - minX + margin * 2.0F), size.y / (maxY - minY + margin * 2.0F)), 0.4F, 1.4F);
        view.statePanX = (minX + maxX) * 0.5F - size.x * 0.5F / view.stateZoom;
        view.statePanY = (minY + maxY) * 0.5F - size.y * 0.5F / view.stateZoom;
        view.stateFramed = true;
    }
    const float zoom = view.stateZoom;
    const auto toScreen = [&](const Math::Vector2UVE p) {
        return ImVec2{origin.x + (p.x - view.statePanX) * zoom, origin.y + (p.y - view.statePanY) * zoom};
    };
    const auto toCanvas = [&](const ImVec2 p) {
        return Math::Vector2UVE{view.statePanX + (p.x - origin.x) / zoom, view.statePanY + (p.y - origin.y) / zoom};
    };
    const auto rectOf = [&](const int box) {
        const ImVec2 min = toScreen(boxMin(box));
        return std::pair<ImVec2, ImVec2>{min, AddUVE(min, ScaleUVE(boxSize(box), zoom))};
    };

    ImGui::InvisibleButton("##states", ImVec2{std::max(size.x, 1.0F), std::max(size.y, 1.0F)},
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = io.MousePos;
    const ImVec2 lo = origin;
    const ImVec2 hi = AddUVE(origin, size);
    draw->PushClipRect(lo, hi, true);
    draw->AddRectFilled(lo, hi, kCanvasUVE);
    const float gridStep = 32.0F * zoom;
    if (gridStep >= 8.0F) {
        for (float x = std::fmod(-view.statePanX * zoom, gridStep); x < size.x; x += gridStep) {
            draw->AddLine(ImVec2{lo.x + x, lo.y}, ImVec2{lo.x + x, hi.y}, kGridMinorUVE);
        }
        for (float y = std::fmod(-view.statePanY * zoom, gridStep); y < size.y; y += gridStep) {
            draw->AddLine(ImVec2{lo.x, lo.y + y}, ImVec2{hi.x, lo.y + y}, kGridMinorUVE);
        }
    }

    // ---- What the mouse is over ----------------------------------------------------------------------
    int hoveredBox = kNoBoxUVE;
    bool overHandle = false;
    if (hovered) {
        for (int box = static_cast<int>(stateCount) - 1; box >= kAnyBoxUVE && hoveredBox == kNoBoxUVE; --box) {
            const auto [min, max] = rectOf(box);
            const ImVec2 handle{max.x, (min.y + max.y) * 0.5F};
            if (LengthUVE(SubUVE(mouse, handle)) <= kHandleRadiusUVE + 3.0F) {
                hoveredBox = box;
                overHandle = true;
                break;
            }
            if (mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y) {
                hoveredBox = box;
            }
        }
    }

    // ---- Transitions: one arrow per pair of boxes, a count when several share it ---------------------
    std::map<std::pair<int, int>, std::vector<std::size_t>> pairs;
    for (std::size_t index = 0U; index < machine.transitions.size(); ++index) {
        const AnimationTransitionUVE& transition = machine.transitions[index];
        const int from = transition.fromState == Scene::kAnyAnimationStateUVE ? kAnyBoxUVE : static_cast<int>(transition.fromState);
        pairs[{from, static_cast<int>(transition.toState)}].push_back(index);
    }
    const float flash = running && runtime.lastTransition != Scene::kAnyAnimationStateUVE
                            ? std::clamp(1.0F - runtime.stateSeconds / 0.8F, 0.0F, 1.0F)
                            : 0.0F;
    int hoveredTransition = -1;
    struct ArrowUVE final {
        ImVec2 from;
        ImVec2 to;
        std::vector<std::size_t> indices;
    };
    std::vector<ArrowUVE> arrows;
    for (const auto& [key, indices] : pairs) {
        const auto [fromMin, fromMax] = rectOf(key.first);
        const auto [toMin, toMax] = rectOf(key.second);
        const ImVec2 fromCentre{(fromMin.x + fromMax.x) * 0.5F, (fromMin.y + fromMax.y) * 0.5F};
        const ImVec2 toCentre{(toMin.x + toMax.x) * 0.5F, (toMin.y + toMax.y) * 0.5F};
        ImVec2 a = EdgePointUVE(fromMin, fromMax, toCentre);
        ImVec2 b = EdgePointUVE(toMin, toMax, fromCentre);
        // A pair with a way back is drawn as two lanes, each to the right of its direction.
        if (pairs.contains({key.second, key.first})) {
            const ImVec2 d = SubUVE(b, a);
            const float length = std::max(LengthUVE(d), 1.0F);
            const ImVec2 side = ScaleUVE(ImVec2{-d.y / length, d.x / length}, 7.0F);
            a = AddUVE(a, side);
            b = AddUVE(b, side);
        }
        arrows.push_back(ArrowUVE{a, b, indices});
        if (hovered && hoveredBox == kNoBoxUVE && DistanceToSegmentUVE(mouse, a, b) <= 6.0F) {
            hoveredTransition = static_cast<int>(indices.front());
        }
    }
    // The entry arrow, then the transitions over it.
    if (machine.entryState < stateCount) {
        const auto [entryMin, entryMax] = rectOf(kEntryBoxUVE);
        const auto [stateMin, stateMax] = rectOf(static_cast<int>(machine.entryState));
        const ImVec2 a = EdgePointUVE(entryMin, entryMax, ImVec2{(stateMin.x + stateMax.x) * 0.5F, (stateMin.y + stateMax.y) * 0.5F});
        const ImVec2 b = EdgePointUVE(stateMin, stateMax, ImVec2{(entryMin.x + entryMax.x) * 0.5F, (entryMin.y + entryMax.y) * 0.5F});
        draw->AddLine(a, b, kEntryColourUVE, 2.0F);
        ArrowHeadUVE(draw, ImVec2{a.x + (b.x - a.x) * 0.6F, a.y + (b.y - a.y) * 0.6F}, SubUVE(b, a), kEntryColourUVE, 11.0F);
    }
    for (const ArrowUVE& arrow : arrows) {
        const bool picked = std::ranges::find(arrow.indices, static_cast<std::size_t>(view.pickedTransition)) != arrow.indices.end();
        const bool taken = flash > 0.0F &&
                           std::ranges::find(arrow.indices, static_cast<std::size_t>(runtime.lastTransition)) != arrow.indices.end();
        const bool anyOn = std::ranges::any_of(arrow.indices, [&machine](const std::size_t i) { return machine.transitions[i].enabled; });
        const bool hot = hoveredTransition >= 0 &&
                         std::ranges::find(arrow.indices, static_cast<std::size_t>(hoveredTransition)) != arrow.indices.end();
        ImU32 colour = anyOn ? kWireUVE : IM_COL32(120, 124, 132, 110);
        if (taken) {
            colour = kActiveUVE;
        }
        if (hot) {
            colour = kTextUVE;
        }
        if (picked) {
            colour = kSelectedUVE;
        }
        const float thickness = picked || taken ? 2.6F : 1.6F;
        if (anyOn) {
            draw->AddLine(arrow.from, arrow.to, colour, thickness);
        } else {
            // Switched off: dashed.
            const ImVec2 d = SubUVE(arrow.to, arrow.from);
            const float length = LengthUVE(d);
            for (float t = 0.0F; t < length; t += 10.0F) {
                const ImVec2 p0 = AddUVE(arrow.from, ScaleUVE(d, t / length));
                const ImVec2 p1 = AddUVE(arrow.from, ScaleUVE(d, std::min(t + 5.0F, length) / length));
                draw->AddLine(p0, p1, colour, thickness);
            }
        }
        const ImVec2 mid{arrow.from.x + (arrow.to.x - arrow.from.x) * 0.55F, arrow.from.y + (arrow.to.y - arrow.from.y) * 0.55F};
        ArrowHeadUVE(draw, mid, SubUVE(arrow.to, arrow.from), colour, 12.0F);
        if (arrow.indices.size() > 1U) {
            char count[8];
            std::snprintf(count, sizeof(count), "%zu", arrow.indices.size());
            const ImVec2 at{mid.x + 8.0F, mid.y - 16.0F};
            draw->AddCircleFilled(ImVec2{at.x + 4.0F, at.y + 7.0F}, 8.0F, IM_COL32(60, 64, 74, 255));
            draw->AddText(at, kTextUVE, count);
        }
    }

    // ---- Boxes --------------------------------------------------------------------------------------
    const auto drawPill = [&](const int box, const char* const label, const ImU32 colour) {
        const auto [min, max] = rectOf(box);
        const bool hot = hoveredBox == box;
        draw->AddRectFilled(min, max, (colour & 0x00FFFFFFU) | (hot ? 0x70000000U : 0x48000000U), (max.y - min.y) * 0.5F);
        draw->AddRect(min, max, colour, (max.y - min.y) * 0.5F, 0, hot ? 2.0F : 1.2F);
        const ImVec2 text = ImGui::CalcTextSize(label);
        draw->AddText(ImVec2{(min.x + max.x - text.x) * 0.5F, (min.y + max.y - text.y) * 0.5F}, kTextUVE, label);
    };
    drawPill(kEntryBoxUVE, "Entry", kEntryColourUVE);
    drawPill(kAnyBoxUVE, "Any State", kAnyColourUVE);
    for (std::size_t slot = 0U; slot < stateCount; ++slot) {
        const int box = static_cast<int>(slot);
        const auto [min, max] = rectOf(box);
        const AnimationGraphObjectUVE* const child = childOf(slot);
        const bool active = running && runtime.activeState == slot;
        const bool leaving = running && runtime.previousState == slot;
        const bool picked = view.pickedState == box;
        const float rounding = 5.0F;
        draw->AddRectFilled(min, max, kBodyUVE, rounding);
        // The accent bar: what kind of thing the state plays.
        draw->AddRectFilled(min, ImVec2{min.x + 5.0F, max.y}, child != nullptr ? KindColourUVE(child->kind) : IM_COL32(90, 92, 100, 255),
                            rounding, ImDrawFlags_RoundCornersLeft);
        if (active) {
            // How long it has run, as a thin bar filling over a two-second window.
            const float fill = std::fmod(runtime.stateSeconds, 2.0F) / 2.0F;
            draw->AddRectFilled(ImVec2{min.x + 5.0F, max.y - 3.0F}, ImVec2{min.x + 5.0F + (max.x - min.x - 5.0F) * fill, max.y},
                                kActiveUVE);
        }
        ImU32 border = kBorderUVE;
        float thickness = 1.0F;
        if (leaving) {
            border = (kFadingUVE & 0x00FFFFFFU) | (static_cast<ImU32>(std::clamp(childWeight(slot), 0.15F, 1.0F) * 255.0F) << 24U);
            thickness = 2.0F;
        }
        if (active) {
            border = kActiveUVE;
            thickness = 2.4F;
        }
        if (picked) {
            border = kSelectedUVE;
            thickness = 2.4F;
        }
        if (hoveredBox == box && !picked && !active) {
            border = IM_COL32(120, 126, 138, 255);
        }
        draw->AddRect(min, max, border, rounding, 0, thickness);
        const std::string title = stateName(slot);
        std::string subtitle = child == nullptr ? std::string{"plays nothing yet"} : AnimationGraphKindLabelUVE(child->kind);
        if (child != nullptr && child->kind == Kind::Clip) {
            const std::string& clip = AnimationClipNameUVE(child->clip);
            subtitle = clip.empty() ? std::string{"Clip - pick an animation"} : clip;
        }
        if (active) {
            char seconds[24];
            std::snprintf(seconds, sizeof(seconds), "  %.1fs", static_cast<double>(runtime.stateSeconds));
            subtitle += seconds;
        }
        draw->PushClipRect(ImVec2{min.x + 10.0F, min.y}, ImVec2{max.x - 8.0F, max.y}, true);
        draw->AddText(ImVec2{min.x + 12.0F, min.y + 7.0F * zoom}, kTextUVE, title.c_str());
        draw->AddText(ImVec2{min.x + 12.0F, min.y + 7.0F * zoom + ImGui::GetTextLineHeight() + 1.0F}, kTextDimUVE, subtitle.c_str());
        draw->PopClipRect();
        if (machine.entryState == slot) {
            draw->AddCircleFilled(ImVec2{max.x - 9.0F, min.y + 9.0F}, 3.5F, kEntryColourUVE);
        }
    }
    // The handle a transition is drawn from, on the box under the mouse.
    if (writable && hoveredBox != kNoBoxUVE && view.linkFrom == kNoBoxUVE && view.stateDrag == kNoBoxUVE) {
        const auto [min, max] = rectOf(hoveredBox);
        const ImVec2 handle{max.x, (min.y + max.y) * 0.5F};
        const ImU32 colour = hoveredBox == kEntryBoxUVE ? kEntryColourUVE : kSelectedUVE;
        draw->AddCircleFilled(handle, kHandleRadiusUVE, overHandle ? colour : IM_COL32(60, 64, 74, 255));
        draw->AddCircle(handle, kHandleRadiusUVE, colour, 0, 1.5F);
    }
    // A transition being drawn.
    if (view.linkFrom != kNoBoxUVE) {
        const auto [min, max] = rectOf(view.linkFrom);
        const ImVec2 start{max.x, (min.y + max.y) * 0.5F};
        const bool onTarget = hoveredBox >= 0 && hoveredBox != view.linkFrom;
        const ImU32 colour = view.linkFrom == kEntryBoxUVE ? kEntryColourUVE : onTarget ? kWireGoodUVE : kSelectedUVE;
        draw->AddLine(start, mouse, colour, 2.0F);
        ArrowHeadUVE(draw, mouse, SubUVE(mouse, start), colour, 12.0F);
    }
    if (stateCount == 1U && machine.inputs[0] == 0U) {
        const char* const hint = "Double-click to add a state, then drag from a state's edge to another to add a transition.";
        const ImVec2 hintSize = ImGui::CalcTextSize(hint);
        draw->AddText(ImVec2{(lo.x + hi.x - hintSize.x) * 0.5F, hi.y - hintSize.y - 10.0F}, kTextDimUVE, hint);
    }
    draw->PopClipRect();

    // ---- Tooltips -----------------------------------------------------------------------------------
    if (hoveredTransition >= 0 && view.linkFrom == kNoBoxUVE && view.stateDrag == kNoBoxUVE) {
        const AnimationTransitionUVE& transition = machine.transitions[static_cast<std::size_t>(hoveredTransition)];
        const std::string from = transition.fromState == Scene::kAnyAnimationStateUVE ? std::string{"Any State"}
                                                                                       : stateName(transition.fromState);
        ImGui::SetTooltip("%s -> %s\n%s", from.c_str(), stateName(transition.toState).c_str(),
                          DescribeAnimationTransitionUVE(transition).c_str());
    } else if (overHandle && view.linkFrom == kNoBoxUVE) {
        ImGui::SetTooltip(hoveredBox == kEntryBoxUVE ? "Drag to the state the machine starts in" : "Drag to a state to add a transition");
    }

    // ---- Interaction --------------------------------------------------------------------------------
    // Pan with the middle button (or Alt + left), zoom on the wheel about the mouse.
    if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                                  (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left)))) {
        view.statePanX -= io.MouseDelta.x / zoom;
        view.statePanY -= io.MouseDelta.y / zoom;
    }
    if (hovered && io.MouseWheel != 0.0F) {
        const Math::Vector2UVE before = toCanvas(mouse);
        view.stateZoom = std::clamp(view.stateZoom * (io.MouseWheel > 0.0F ? 1.1F : 1.0F / 1.1F), 0.3F, 2.0F);
        view.statePanX = before.x - (mouse.x - origin.x) / view.stateZoom;
        view.statePanY = before.y - (mouse.y - origin.y) / view.stateZoom;
    }
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_F) && !io.WantTextInput) {
        view.stateFramed = false;
    }
    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt) {
        if (writable && overHandle) {
            view.linkFrom = hoveredBox;
        } else if (hoveredBox != kNoBoxUVE) {
            view.pickedState = hoveredBox >= 0 ? hoveredBox : -1;
            view.pickedTransition = -1;
            if (writable) {
                view.stateDrag = hoveredBox;
                view.stateBefore = live;
            }
        } else if (hoveredTransition >= 0) {
            // Clicking a shared arrow again steps through the transitions it carries.
            const auto pair = std::ranges::find_if(arrows, [&](const ArrowUVE& a) {
                return std::ranges::find(a.indices, static_cast<std::size_t>(hoveredTransition)) != a.indices.end();
            });
            int next = hoveredTransition;
            if (pair != arrows.end()) {
                const auto current = std::ranges::find(pair->indices, static_cast<std::size_t>(view.pickedTransition));
                if (current != pair->indices.end() && current + 1 != pair->indices.end()) {
                    next = static_cast<int>(*(current + 1));
                } else if (current != pair->indices.end()) {
                    next = static_cast<int>(pair->indices.front());
                }
            }
            view.pickedTransition = next;
            view.pickedState = -1;
        } else {
            view.pickedState = -1;
            view.pickedTransition = -1;
        }
    }
    if (view.stateDrag != kNoBoxUVE && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0F)) {
        AnimationGraphObjectUVE& moving = live.objects[objectIndex];
        const Math::Vector2UVE delta{io.MouseDelta.x / zoom, io.MouseDelta.y / zoom};
        if (view.stateDrag == kEntryBoxUVE) {
            moving.entryPosition = moving.entryPosition + delta;
        } else if (view.stateDrag == kAnyBoxUVE) {
            moving.anyPosition = moving.anyPosition + delta;
        } else {
            const auto slot = static_cast<std::size_t>(view.stateDrag);
            static_cast<void>(SetAnimationStatePositionUVE(live.objects, machineId, slot, AnimationStatePositionUVE(moving, slot) + delta));
        }
    }
    if (ImGui::IsItemDeactivated()) {
        if (view.stateDrag != kNoBoxUVE) {
            const bool moved = live.objects != view.stateBefore.objects;
            if (moved) {
                const std::vector<AnimationGraphObjectUVE> after = live.objects;
                live = view.stateBefore;
                edit = [after](Scene::AnimationTreeComponentUVE& t) { t.objects = after; };
            }
            view.stateDrag = kNoBoxUVE;
        }
        if (view.linkFrom != kNoBoxUVE) {
            const int from = view.linkFrom;
            view.linkFrom = kNoBoxUVE;
            if (hoveredBox >= 0 && hoveredBox != from) {
                const auto to = static_cast<std::uint32_t>(hoveredBox);
                if (from == kEntryBoxUVE) {
                    edit = [machineId, to](Scene::AnimationTreeComponentUVE& t) {
                        const auto it = std::ranges::find(t.objects, machineId, &AnimationGraphObjectUVE::id);
                        if (it != t.objects.end()) {
                            it->entryState = to;
                        }
                    };
                } else {
                    const std::uint32_t source = from == kAnyBoxUVE ? Scene::kAnyAnimationStateUVE : static_cast<std::uint32_t>(from);
                    edit = [machineId, source, to](Scene::AnimationTreeComponentUVE& t) {
                        static_cast<void>(AddAnimationTransitionUVE(t.objects, machineId, source, to));
                    };
                    view.pickedTransition = static_cast<int>(machine.transitions.size());
                    view.pickedState = -1;
                }
            }
        }
    }
    // Double-click empty space: a new state there.
    if (writable && hovered && hoveredBox == kNoBoxUVE && hoveredTransition < 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        const Math::Vector2UVE at = toCanvas(mouse) - Math::Vector2UVE{kStateWidthUVE * 0.5F, kStateHeightUVE * 0.5F};
        const bool fillsFirst = stateCount == 1U && machine.inputs[0] == 0U;
        edit = [machineId, at](Scene::AnimationTreeComponentUVE& t) {
            static_cast<void>(AddAnimationStateUVE(t.objects, machineId, at));
        };
        view.pickedState = fillsFirst ? 0 : static_cast<int>(stateCount);
        view.pickedTransition = -1;
    }
    // Right-click: what can be done to the thing under the mouse.
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        if (hoveredBox >= 0) {
            view.pickedState = hoveredBox;
            view.pickedTransition = -1;
            ImGui::OpenPopup("##state-menu");
        } else if (hoveredTransition >= 0) {
            view.pickedTransition = hoveredTransition;
            view.pickedState = -1;
            ImGui::OpenPopup("##transition-menu");
        } else {
            view.addAtX = toCanvas(mouse).x - kStateWidthUVE * 0.5F;
            view.addAtY = toCanvas(mouse).y - kStateHeightUVE * 0.5F;
            ImGui::OpenPopup("##states-menu");
        }
    }
    // Delete: the picked transition, or the picked state (a machine keeps one).
    if (writable && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !io.WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
        if (view.pickedTransition >= 0) {
            const auto index = static_cast<std::size_t>(view.pickedTransition);
            edit = [machineId, index](Scene::AnimationTreeComponentUVE& t) {
                static_cast<void>(RemoveAnimationTransitionUVE(t.objects, machineId, index));
            };
            view.pickedTransition = -1;
        } else if (view.pickedState >= 0 && stateCount > 1U) {
            const auto slot = static_cast<std::size_t>(view.pickedState);
            edit = [machineId, slot](Scene::AnimationTreeComponentUVE& t) {
                static_cast<void>(RemoveAnimationGraphInputSlotUVE(t.objects, machineId, slot));
            };
            view.pickedState = -1;
        }
    }

    if (ImGui::BeginPopup("##states-menu")) {
        ImGui::BeginDisabled(!writable);
        if (ImGui::MenuItem("Add State")) {
            const Math::Vector2UVE at{view.addAtX, view.addAtY};
            edit = [machineId, at](Scene::AnimationTreeComponentUVE& t) {
                static_cast<void>(AddAnimationStateUVE(t.objects, machineId, at));
            };
        }
        ImGui::EndDisabled();
        if (ImGui::MenuItem("Frame All", "F")) {
            view.stateFramed = false;
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##state-menu")) {
        const int slot = view.pickedState;
        ImGui::BeginDisabled(!writable || slot < 0);
        if (ImGui::MenuItem("Start Here", nullptr, slot >= 0 && machine.entryState == static_cast<std::uint32_t>(slot))) {
            const auto to = static_cast<std::uint32_t>(slot);
            edit = [machineId, to](Scene::AnimationTreeComponentUVE& t) {
                const auto it = std::ranges::find(t.objects, machineId, &AnimationGraphObjectUVE::id);
                if (it != t.objects.end()) {
                    it->entryState = to;
                }
            };
        }
        if (ImGui::BeginMenu("Transition To")) {
            for (std::size_t target = 0U; target < stateCount; ++target) {
                if (static_cast<int>(target) != slot && ImGui::MenuItem(stateName(target).c_str())) {
                    const auto from = static_cast<std::uint32_t>(slot);
                    const auto to = static_cast<std::uint32_t>(target);
                    edit = [machineId, from, to](Scene::AnimationTreeComponentUVE& t) {
                        static_cast<void>(AddAnimationTransitionUVE(t.objects, machineId, from, to));
                    };
                    view.pickedTransition = static_cast<int>(machine.transitions.size());
                    view.pickedState = -1;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Transition From Any State")) {
            const auto to = static_cast<std::uint32_t>(slot);
            edit = [machineId, to](Scene::AnimationTreeComponentUVE& t) {
                static_cast<void>(AddAnimationTransitionUVE(t.objects, machineId, Scene::kAnyAnimationStateUVE, to));
            };
            view.pickedTransition = static_cast<int>(machine.transitions.size());
            view.pickedState = -1;
        }
        ImGui::Separator();
        ImGui::BeginDisabled(stateCount <= 1U);
        if (ImGui::MenuItem("Remove State", "Del")) {
            const auto removed = static_cast<std::size_t>(slot);
            edit = [machineId, removed](Scene::AnimationTreeComponentUVE& t) {
                static_cast<void>(RemoveAnimationGraphInputSlotUVE(t.objects, machineId, removed));
            };
            view.pickedState = -1;
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##transition-menu")) {
        const int index = view.pickedTransition;
        ImGui::BeginDisabled(!writable || index < 0);
        if (index >= 0 && static_cast<std::size_t>(index) < machine.transitions.size()) {
            const bool on = machine.transitions[static_cast<std::size_t>(index)].enabled;
            if (ImGui::MenuItem(on ? "Switch Off" : "Switch On")) {
                const auto at = static_cast<std::size_t>(index);
                edit = [machineId, at, on](Scene::AnimationTreeComponentUVE& t) {
                    const auto it = std::ranges::find(t.objects, machineId, &AnimationGraphObjectUVE::id);
                    if (it != t.objects.end() && at < it->transitions.size()) {
                        it->transitions[at].enabled = !on;
                    }
                };
            }
        }
        if (ImGui::MenuItem("Remove Transition", "Del")) {
            const auto at = static_cast<std::size_t>(index);
            edit = [machineId, at](Scene::AnimationTreeComponentUVE& t) {
                static_cast<void>(RemoveAnimationTransitionUVE(t.objects, machineId, at));
            };
            view.pickedTransition = -1;
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    if (edit.has_value()) {
        static_cast<void>(EditAnimationTreeUVE(tree, *edit));
    }
}

void EditorUVE::DrawStateMachineSelectionUVE(const Scene::EntityUVE tree, const std::size_t objectIndex,
                                             std::optional<std::function<void(Scene::AnimationTreeComponentUVE&)>>& edit) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    auto& live = entityManager.GetComponentUVE<Scene::AnimationTreeComponentUVE>(tree);
    if (objectIndex >= live.objects.size()) {
        return;
    }
    AnimationGraphViewStateUVE& view = m_animGraph;
    const AnimationGraphObjectUVE machine = live.objects[objectIndex];
    const std::uint32_t machineId = machine.id;
    const bool writable = IsAuthoringCommandAllowedUVE();
    const auto stateName = [&](const std::uint32_t slot) -> std::string {
        if (slot == Scene::kAnyAnimationStateUVE) {
            return "Any State";
        }
        if (slot < machine.inputs.size()) {
            const auto it = std::ranges::find(live.objects, machine.inputs[slot], &AnimationGraphObjectUVE::id);
            if (it != live.objects.end() && !it->name.empty()) {
                return it->name;
            }
        }
        return AnimationGraphSlotLabelUVE(Kind::StateMachine, slot);
    };
    const auto editMachine = [&edit, machineId](std::function<void(AnimationGraphObjectUVE&)> change) {
        edit = [machineId, change = std::move(change)](Scene::AnimationTreeComponentUVE& t) {
            const auto it = std::ranges::find(t.objects, machineId, &AnimationGraphObjectUVE::id);
            if (it != t.objects.end()) {
                change(*it);
            }
        };
    };
    const auto label = [](const char* const text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", text);
        ImGui::SameLine(84.0F);
        ImGui::SetNextItemWidth(-FLT_MIN);
    };
    ImGui::BeginDisabled(!writable);

    // ---- A transition ---------------------------------------------------------------------------------
    if (view.pickedTransition >= 0 && static_cast<std::size_t>(view.pickedTransition) < machine.transitions.size()) {
        const auto index = static_cast<std::size_t>(view.pickedTransition);
        AnimationTransitionUVE transition = machine.transitions[index];
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kSelectedUVE), "%s  ->  %s", stateName(transition.fromState).c_str(),
                           stateName(transition.toState).c_str());
        // Its place among the transitions leaving the same state: the first ready one wins.
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Priority %zu of %zu", index + 1U, machine.transitions.size());
        ImGui::SameLine();
        ImGui::BeginDisabled(index == 0U);
        if (ImGui::ArrowButton("##earlier", ImGuiDir_Up)) {
            edit = [machineId, index](Scene::AnimationTreeComponentUVE& t) {
                static_cast<void>(MoveAnimationTransitionUVE(t.objects, machineId, index, index - 1U));
            };
            view.pickedTransition = static_cast<int>(index) - 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(index + 1U >= machine.transitions.size());
        if (ImGui::ArrowButton("##later", ImGuiDir_Down)) {
            edit = [machineId, index](Scene::AnimationTreeComponentUVE& t) {
                static_cast<void>(MoveAnimationTransitionUVE(t.objects, machineId, index, index + 1U));
            };
            view.pickedTransition = static_cast<int>(index) + 1;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Transitions are tried top to bottom: the first one ready is taken.");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            edit = [machineId, index](Scene::AnimationTreeComponentUVE& t) {
                static_cast<void>(RemoveAnimationTransitionUVE(t.objects, machineId, index));
            };
            view.pickedTransition = -1;
        }
        ImGui::Separator();
        const TransitionEditUVE changed = DrawAnimationTransitionUVE(transition, live.parameters);
        if (changed.dragging && !view.transitionDragging) {
            view.transitionDragging = true;
            view.stateBefore = live;
        }
        if (changed.dragging || changed.changed || changed.released) {
            // Seen at once; recorded as one step below.
            live.objects[objectIndex].transitions[index] = transition;
        }
        if (changed.changed || changed.released) {
            if (view.transitionDragging) {
                live = view.stateBefore;
                view.transitionDragging = false;
            }
            edit = [machineId, index, transition](Scene::AnimationTreeComponentUVE& t) {
                const auto it = std::ranges::find(t.objects, machineId, &AnimationGraphObjectUVE::id);
                if (it != t.objects.end() && index < it->transitions.size()) {
                    it->transitions[index] = transition;
                }
            };
        }
        ImGui::EndDisabled();
        return;
    }

    // ---- A state ---------------------------------------------------------------------------------------
    if (view.pickedState >= 0 && static_cast<std::size_t>(view.pickedState) < machine.inputs.size()) {
        const auto slot = static_cast<std::uint32_t>(view.pickedState);
        const auto childIt = std::ranges::find(live.objects, machine.inputs[slot], &AnimationGraphObjectUVE::id);
        const AnimationGraphObjectUVE* const child = machine.inputs[slot] != 0U && childIt != live.objects.end() ? &*childIt : nullptr;
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kSelectedUVE), "%s", stateName(slot).c_str());
        if (child != nullptr) {
            const std::uint32_t childId = child->id;
            const auto editChild = [&edit, childId](std::function<void(AnimationGraphObjectUVE&)> change) {
                edit = [childId, change = std::move(change)](Scene::AnimationTreeComponentUVE& t) {
                    const auto it = std::ranges::find(t.objects, childId, &AnimationGraphObjectUVE::id);
                    if (it != t.objects.end()) {
                        change(*it);
                    }
                };
            };
            label("Name");
            std::array<char, Scene::kMaximumAnimationNameBytesUVE + 1U> buffer{};
            child->name.copy(buffer.data(), std::min(child->name.size(), buffer.size() - 1U));
            ImGui::InputText("##state-name", buffer.data(), buffer.size());
            if (ImGui::IsItemDeactivatedAfterEdit() && child->name != buffer.data()) {
                const std::string renamed = buffer.data();
                editChild([renamed](AnimationGraphObjectUVE& n) { n.name = renamed; });
            }
            label("Plays");
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(KindColourUVE(child->kind) | IM_COL32(60, 60, 60, 0)), "%s",
                               AnimationGraphKindLabelUVE(child->kind));
            if (child->kind == Kind::Clip) {
                label("Animation");
                if (const std::optional<Asset::AssetGuidUVE> picked = DrawAssetPickerUVE("##state-clip", child->clip, ".uvanim")) {
                    const Asset::AssetGuidUVE guid = *picked;
                    editChild([guid](AnimationGraphObjectUVE& n) { n.clip = guid; });
                }
                label("Loop");
                bool loop = child->loop;
                if (ImGui::Checkbox("##state-loop", &loop)) {
                    editChild([loop](AnimationGraphObjectUVE& n) { n.loop = loop; });
                }
            } else {
                ImGui::TextDisabled("Its settings are on its node in the tree.");
            }
            if (ImGui::Button("Show in Tree", ImVec2{-FLT_MIN, 0.0F})) {
                view.focus = 0U;
                view.selected = {childId};
            }
        } else {
            ImGui::TextDisabled("Nothing plays in this state: wire a node into it in the tree.");
        }
        label("Start here");
        bool entry = machine.entryState == slot;
        if (ImGui::Checkbox("##entry", &entry) && entry) {
            editMachine([slot](AnimationGraphObjectUVE& n) { n.entryState = slot; });
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The state the machine starts in (drag from Entry in the view, too).");
        }

        // What leaves this state, in the order they are tried.
        ImGui::Spacing();
        ImGui::TextDisabled("Leaves by");
        bool any = false;
        for (std::size_t index = 0U; index < machine.transitions.size(); ++index) {
            const AnimationTransitionUVE& transition = machine.transitions[index];
            if (transition.fromState != slot && transition.fromState != Scene::kAnyAnimationStateUVE) {
                continue;
            }
            any = true;
            ImGui::PushID(static_cast<int>(index));
            const std::string line = (transition.fromState == Scene::kAnyAnimationStateUVE ? "(any) -> " : "-> ") +
                                     stateName(transition.toState) + "   " + DescribeAnimationTransitionUVE(transition);
            if (ImGui::Selectable(line.c_str(), false)) {
                view.pickedTransition = static_cast<int>(index);
                view.pickedState = -1;
            }
            ImGui::PopID();
        }
        if (!any) {
            ImGui::TextDisabled("  nothing yet");
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##add-transition", "+ Transition to...")) {
            for (std::uint32_t target = 0U; target < machine.inputs.size(); ++target) {
                if (target != slot && ImGui::Selectable(stateName(target).c_str())) {
                    edit = [machineId, slot, target](Scene::AnimationTreeComponentUVE& t) {
                        static_cast<void>(AddAnimationTransitionUVE(t.objects, machineId, slot, target));
                    };
                    view.pickedTransition = static_cast<int>(machine.transitions.size());
                    view.pickedState = -1;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(machine.inputs.size() <= 1U);
        if (ImGui::Button("Remove State", ImVec2{-FLT_MIN, 0.0F})) {
            const std::size_t removed = slot;
            edit = [machineId, removed](Scene::AnimationTreeComponentUVE& t) {
                static_cast<void>(RemoveAnimationGraphInputSlotUVE(t.objects, machineId, removed));
            };
            view.pickedState = -1;
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        return;
    }

    // ---- Nothing picked: the machine, and how to work the view ----------------------------------------
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(KindColourUVE(Kind::StateMachine) | IM_COL32(60, 60, 60, 0)), "%s",
                       machine.name.empty() ? "State Machine" : machine.name.c_str());
    ImGui::TextDisabled("%zu states, %zu transitions", machine.inputs.size(), machine.transitions.size());
    ImGui::Spacing();
    ImGui::TextWrapped("Double-click the view to add a state. Drag from a state's edge to another to add a transition; "
                       "from Entry to choose where it starts. Click an arrow to edit it.");
    ImGui::EndDisabled();
}

} // namespace UVE::Editor
