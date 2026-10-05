// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Entity Editor's Anim Graph tab: an AnimationGraph as boxes and wires on a canvas. Objects are
// added from a searchable menu where the right-click was, wired by dragging from an output to an
// input, and moved, duplicated and deleted in place. A strip on the right edits the parameters and
// the selected node; a bar along the bottom holds the view controls and what is wrong with the
// graph. Every change is one undo step on the tree component, the same history as the Inspector.

#include "uve/editor/editor_uve.h"
#include "uve/editor/animation_graph_editing_uve.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>

#include "editor_animation_graph_widgets_uve.h"

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/animation_driver_component_uve.h"
#include "uve/component/animation_graph_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/objects/3d/animation_graph_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"
#include "uve/object/type_metadata_uve.h"
#include "uve/scene/scene_component_metadata_uve.h"

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKindUVE;
using Scene::AnimationGraphNodeUVE;
using Scene::AnimationParameterTypeUVE;
using Scene::AnimationParameterUVE;

constexpr float kObjectWidthUVE = 176.0F;
constexpr float kHeaderHeightUVE = 24.0F;
constexpr float kSlotHeightUVE = 20.0F;
constexpr float kBodyPaddingUVE = 6.0F;
constexpr float kPinRadiusUVE = 5.0F;
constexpr float kSideStripWidthUVE = 250.0F;
constexpr float kMinimumZoomUVE = 0.3F;
constexpr float kMaximumZoomUVE = 2.0F;


constexpr std::array<Kind, 11> kAddableKindsUVE{Kind::Clip,         Kind::Blend2,       Kind::BlendSpace1D, Kind::BlendSpace2D,
                                                Kind::Select,       Kind::Additive,     Kind::LayeredBlend, Kind::OneShot,
                                                Kind::TimeScale,    Kind::TimeSeek,     Kind::StateMachine};

/// Rows of values edited right on a node, under its header: what one tunes most.
[[nodiscard]] std::size_t InlineRowsUVE(const Kind kind) noexcept {
    switch (kind) {
        case Kind::BlendSpace2D: return 3U; // x, y, Open
        case Kind::BlendSpace1D: return 2U; // x, Open
        case Kind::Clip:
        case Kind::Blend2:
        case Kind::Additive:
        case Kind::LayeredBlend:
        case Kind::Select:
        case Kind::TimeScale:
        case Kind::TimeSeek: return 1U;
        case Kind::StateMachine: return 1U; // Open States
        case Kind::Output:
        case Kind::OneShot: return 0U;
    }
    return 0U;
}

[[nodiscard]] float ObjectHeightUVE(const AnimationGraphNodeUVE& node) {
    // Its inputs' rows under its inline values; a node with neither still shows one row.
    const std::size_t rows = node.inputs.size() + InlineRowsUVE(node.kind);
    return kHeaderHeightUVE + static_cast<float>(std::max<std::size_t>(rows, 1U)) * kSlotHeightUVE + kBodyPaddingUVE;
}

[[nodiscard]] std::string LowerUVE(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// One line under a node's title: what it reads, so the graph explains itself without a click.
[[nodiscard]] std::string ObjectSummaryUVE(const AnimationGraphNodeUVE& node) {
    char text[96];
    switch (node.kind) {
        case Kind::Clip:
            std::snprintf(text, sizeof(text), "%s  x%.2f", node.loop ? "loop" : "once", static_cast<double>(node.speed));
            return text;
        case Kind::Blend2:
        case Kind::Additive:
        case Kind::BlendSpace1D:
            if (!node.parameter.empty()) {
                return "by " + node.parameter;
            }
            std::snprintf(text, sizeof(text), "at %.2f", static_cast<double>(node.value));
            return text;
        case Kind::OneShot: return node.parameter.empty() ? std::string{"never fires"} : "on " + node.parameter;
        case Kind::TimeScale:
            if (!node.parameter.empty()) {
                return "rate " + node.parameter;
            }
            std::snprintf(text, sizeof(text), "x%.2f", static_cast<double>(node.speed));
            return text;
        case Kind::StateMachine: return std::to_string(node.transitions.size()) + " transitions";
        case Kind::BlendSpace2D: {
            const std::string x = node.parameter.empty() ? "x" : node.parameter;
            const std::string y = node.parameterY.empty() ? "y" : node.parameterY;
            return "by " + x + ", " + y;
        }
        case Kind::Select:
            if (!node.parameter.empty()) {
                return "by " + node.parameter;
            }
            std::snprintf(text, sizeof(text), "option %d", static_cast<int>(std::lround(node.value)));
            return text;
        case Kind::LayeredBlend:
            if (node.bones.empty()) {
                return "whole body";
            }
            return "from " + node.bones.front() + (node.bones.size() > 1U ? " +" + std::to_string(node.bones.size() - 1U) : "");
        case Kind::TimeSeek:
            std::snprintf(text, sizeof(text), "to %.2fs%s", static_cast<double>(node.value),
                          node.parameter.empty() ? "" : (" on " + node.parameter).c_str());
            return text;
        case Kind::Output: return {};
    }
    return {};
}

bool EditNameUVE(const char* const id, const std::string& value, std::string& outValue) {
    std::array<char, Scene::kMaximumAnimationNameBytesUVE + 1U> buffer{};
    std::strncpy(buffer.data(), value.c_str(), buffer.size() - 1U);
    ImGui::InputText(id, buffer.data(), buffer.size());
    if (ImGui::IsItemDeactivatedAfterEdit() && value != buffer.data()) {
        outValue = buffer.data();
        return true;
    }
    return false;
}


void DrawBlendSpaceSettingsUVE(const AnimationGraphNodeUVE& node,
                               const std::function<void(std::function<void(AnimationGraphNodeUVE&)>)>& edit) {
    static constexpr std::array<const char*, 3> kModes{"Blend", "Nearest", "Nearest, in step"};
    static constexpr std::array<const char*, 3> kModeHelp{
        "Mix the points around the position.",
        "Play the nearest point alone; a point that becomes clearly nearer takes over across Switch.",
        "Nearest, and the point taking over starts at the phase the last one reached, so the stride carries on."};
    const auto mode = static_cast<std::size_t>(node.blendMode);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Blend");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(128.0F);
    if (ImGui::BeginCombo("##blend-mode", kModes[std::min(mode, kModes.size() - 1U)])) {
        for (std::size_t option = 0U; option < kModes.size(); ++option) {
            if (ImGui::Selectable(kModes[option], option == mode)) {
                const auto picked = static_cast<Scene::AnimationBlendModeUVE>(option);
                edit([picked](AnimationGraphNodeUVE& n) { n.blendMode = picked; });
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", kModeHelp[option]);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Smoothing");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72.0F);
    float smoothing = node.smoothingSeconds;
    ImGui::DragFloat("##blend-smoothing", &smoothing, 0.005F, 0.0F, 5.0F, smoothing > 0.0F ? "%.2f s" : "off");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        const float value = std::clamp(smoothing, 0.0F, 5.0F);
        edit([value](AnimationGraphNodeUVE& n) { n.smoothingSeconds = value; });
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How long the position takes to close half the gap to its parameters. It eases there and never\n"
                          "overshoots, so a sudden change of input still moves the character smoothly.");
    }
    if (node.blendMode != Scene::AnimationBlendModeUVE::Blend) {
        ImGui::SameLine();
        ImGui::TextDisabled("Switch");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(72.0F);
        float fade = node.fadeSeconds;
        ImGui::DragFloat("##blend-switch", &fade, 0.005F, 0.0F, 5.0F, fade > 0.0F ? "%.2f s" : "cut");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const float value = std::clamp(fade, 0.0F, 5.0F);
            edit([value](AnimationGraphNodeUVE& n) { n.fadeSeconds = value; });
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How long one point hands over to the next: inertialized or crossfaded, as the driver's\n"
                              "transitions are set.");
        }
    }
}

} // namespace

const std::string& EditorUVE::AnimationClipNameUVE(const Asset::AssetGuidUVE clip) {
    auto [it, added] = m_animGraph.clipNames.try_emplace(clip.value);
    if (added && clip != Asset::AssetGuidUVE{}) {
        it->second = m_services->GetAssetDatabaseUVE().ResolveUVE(clip).stem().string();
    }
    return it->second;
}

void EditorUVE::DrawBlendSpaceEditorUVE(const Scene::EntityUVE tree, const std::size_t objectIndex) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    auto& live = entityManager.GetComponentUVE<Scene::AnimationGraphComponentUVE>(tree);
    if (objectIndex >= live.nodes.size()) {
        return;
    }
    AnimationGraphViewStateUVE& view = m_animGraph;
    const AnimationGraphNodeUVE node = live.nodes[objectIndex];
    const bool twoD = node.kind == Kind::BlendSpace2D;
    const std::uint32_t spaceId = node.id;
    const bool writable = IsAuthoringCommandAllowedUVE();
    const ImGuiStyle& style = ImGui::GetStyle();
    std::optional<std::function<void(Scene::AnimationGraphComponentUVE&)>> edit;
    const auto addedSlot = std::make_shared<std::optional<std::size_t>>();
    const auto editSpace = [&edit, spaceId](std::function<void(AnimationGraphNodeUVE&)> change) {
        edit = [spaceId, change = std::move(change)](Scene::AnimationGraphComponentUVE& t) {
            const auto it = std::ranges::find(t.nodes, spaceId, &AnimationGraphNodeUVE::id);
            if (it != t.nodes.end()) {
                change(*it);
            }
        };
    };
    const auto readParameter = [&live](const std::string& name, const float fallback) {
        const auto found = std::ranges::find(live.parameters, name, &AnimationParameterUVE::name);
        return name.empty() || found == live.parameters.end() ? fallback : found->value;
    };
    const Math::Vector2UVE cursor{readParameter(node.parameter, node.value),
                                  twoD ? readParameter(node.parameterY, node.valueY) : 0.0F};
    const std::size_t pointCount = node.blendPoints.size();
    const auto pointAt = [&node, twoD](const std::size_t slot) {
        const Math::Vector2UVE at = node.blendPoints[slot].position;
        return twoD ? at : Math::Vector2UVE{at.x, 0.0F};
    };

    // ---- Toolbar: tools, snapping, sync, what the axes read ----------------------------------------
    ImGui::BeginDisabled(!writable);
    const auto toolButton = [&view](const char* label, const int tool, const char* tip) {
        const bool on = view.spaceTool == tool;
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(label)) {
            view.spaceTool = tool;
        }
        if (on) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tip);
        }
        ImGui::SameLine();
    };
    toolButton("Select", 0, "Drag a point to move it, anywhere else to try a position (1)");
    toolButton("Add", 1, "Click to place a point and pick its animation (2)");
    toolButton("Remove", 2, "Click a point to remove it and its clip (3)");
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &view.snap);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(56.0F);
    ImGui::DragFloat("##snap-step", &view.snapStep, 0.01F, 0.01F, 100.0F, "%.2f");
    view.snapStep = std::clamp(view.snapStep, 0.01F, 100.0F);
    ImGui::SameLine();
    bool sync = node.sync;
    if (ImGui::Checkbox("Sync", &sync)) {
        editSpace([sync](AnimationGraphNodeUVE& n) { n.sync = sync; });
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Keep the animations in step: the heaviest leads, the others follow its phase.");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    const auto axisPicker = [&](const char* id, const char* axis, const std::string& current, const bool yAxis) {
        ImGui::TextUnformatted(axis);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0F);
        std::string name = current;
        if (PickParameterUVE(id, live.parameters, {AnimationParameterTypeUVE::Float}, "(fixed)", name)) {
            editSpace([name, yAxis](AnimationGraphNodeUVE& n) { (yAxis ? n.parameterY : n.parameter) = name; });
        }
        ImGui::SameLine();
    };
    axisPicker("##axis-x", "X", node.parameter, false);
    if (twoD) {
        axisPicker("##axis-y", "Y", node.parameterY, true);
    }
    if (ImGui::Button("Fit")) {
        // The area around the points, with a margin.
        Math::Vector2UVE lo{FLT_MAX, FLT_MAX};
        Math::Vector2UVE hi{-FLT_MAX, -FLT_MAX};
        for (std::size_t slot = 0U; slot < pointCount; ++slot) {
            const Math::Vector2UVE point = pointAt(slot);
            lo = Math::Vector2UVE{std::min(lo.x, point.x), std::min(lo.y, point.y)};
            hi = Math::Vector2UVE{std::max(hi.x, point.x), std::max(hi.y, point.y)};
        }
        if (pointCount > 0U) {
            const float marginX = std::max((hi.x - lo.x) * 0.15F, 0.5F);
            const float marginY = std::max((hi.y - lo.y) * 0.15F, 0.5F);
            const Math::Vector2UVE areaMin{lo.x - marginX, lo.y - marginY};
            const Math::Vector2UVE areaMax{hi.x + marginX, hi.y + marginY};
            editSpace([areaMin, areaMax](AnimationGraphNodeUVE& n) {
                n.areaMin = areaMin;
                n.areaMax = areaMax;
            });
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Fit the area to the points");
    }
    ImGui::EndDisabled();

    // ---- Layout: Y range on the left, X range under the plane ----------------------------------------
    const float rangeWidth = 58.0F;
    const float rowHeight = ImGui::GetFrameHeight();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float plotLeft = start.x + (twoD ? rangeWidth + style.ItemSpacing.x : 0.0F);
    const float plotWidth = std::max(60.0F, avail.x - (plotLeft - start.x));
    // Under the plane: the X range row, then the blend settings row.
    const float fullHeight = std::max(60.0F, avail.y - rowHeight * 3.0F - style.ItemSpacing.y * 3.0F);
    const float plotHeight = twoD ? fullHeight : std::min(fullHeight, 96.0F);
    const float plotTop = start.y + (twoD ? 0.0F : (fullHeight - plotHeight) * 0.5F);
    const Math::Vector2UVE areaMin = node.areaMin;
    const Math::Vector2UVE areaMax{node.areaMax.x, twoD ? node.areaMax.y : 1.0F};
    const float areaMinY = twoD ? areaMin.y : -1.0F;
    const auto toScreen = [&](const Math::Vector2UVE p) {
        return ImVec2{plotLeft + (p.x - areaMin.x) / (areaMax.x - areaMin.x) * plotWidth,
                      twoD ? plotTop + plotHeight - (p.y - areaMinY) / (areaMax.y - areaMinY) * plotHeight
                           : plotTop + plotHeight * 0.5F};
    };
    const auto toArea = [&](const ImVec2 p) {
        Math::Vector2UVE value{areaMin.x + (p.x - plotLeft) / plotWidth * (areaMax.x - areaMin.x),
                               twoD ? areaMinY + (plotTop + plotHeight - p.y) / plotHeight * (areaMax.y - areaMinY) : 0.0F};
        if (view.snap && view.snapStep > 0.0F) {
            value.x = std::round(value.x / view.snapStep) * view.snapStep;
            value.y = twoD ? std::round(value.y / view.snapStep) * view.snapStep : 0.0F;
        }
        return value;
    };

    // Y range fields: the top of the area at the top, its bottom at the bottom.
    const auto rangeField = [&](const char* id, const float value, const ImVec2 at, const bool isMax, const bool yAxis) {
        ImGui::SetCursorScreenPos(at);
        ImGui::SetNextItemWidth(rangeWidth);
        float edited = value;
        ImGui::BeginDisabled(!writable);
        ImGui::DragFloat(id, &edited, 0.05F, 0.0F, 0.0F, "%.2f");
        ImGui::EndDisabled();
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            editSpace([edited, isMax, yAxis](AnimationGraphNodeUVE& n) {
                float& target = yAxis ? (isMax ? n.areaMax.y : n.areaMin.y) : (isMax ? n.areaMax.x : n.areaMin.x);
                const float other = yAxis ? (isMax ? n.areaMin.y : n.areaMax.y) : (isMax ? n.areaMin.x : n.areaMax.x);
                // Keep the area the right way round: a maximum stays above its minimum.
                target = isMax ? std::max(edited, other + 0.01F) : std::min(edited, other - 0.01F);
            });
        }
    };
    if (twoD) {
        rangeField("##max-y", node.areaMax.y, ImVec2{start.x, plotTop}, true, true);
        const std::string yName = node.parameterY.empty() ? std::string{"y"} : node.parameterY;
        const ImVec2 yNameSize = ImGui::CalcTextSize(yName.c_str());
        ImGui::GetWindowDrawList()->AddText(ImVec2{start.x + rangeWidth - yNameSize.x, plotTop + plotHeight * 0.5F - yNameSize.y * 0.5F},
                                            kTextDimUVE, yName.c_str());
        rangeField("##min-y", node.areaMin.y, ImVec2{start.x, plotTop + plotHeight - rowHeight}, false, true);
    }

    // ---- The plane ---------------------------------------------------------------------------------
    ImGui::SetCursorScreenPos(ImVec2{plotLeft, plotTop});
    ImGui::InvisibleButton("##space", ImVec2{plotWidth, plotHeight}, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const ImVec2 lo{plotLeft, plotTop};
    const ImVec2 hi{plotLeft + plotWidth, plotTop + plotHeight};
    draw->PushClipRect(lo, hi, true);
    draw->AddRectFilled(lo, hi, kCanvasUVE, 3.0F);
    // Grid on the snap step (coarser when it would crowd), axes through zero.
    const float spanX = areaMax.x - areaMin.x;
    float step = view.snapStep;
    while (step > 0.0F && plotWidth * step / spanX < 12.0F) {
        step *= 2.0F;
    }
    if (step > 0.0F) {
        for (float x = std::ceil(areaMin.x / step) * step; x <= areaMax.x; x += step) {
            const float sx = toScreen(Math::Vector2UVE{x, 0.0F}).x;
            draw->AddLine(ImVec2{sx, lo.y}, ImVec2{sx, hi.y}, kGridMinorUVE);
        }
        if (twoD) {
            for (float y = std::ceil(areaMinY / step) * step; y <= areaMax.y; y += step) {
                const float sy = toScreen(Math::Vector2UVE{0.0F, y}).y;
                draw->AddLine(ImVec2{lo.x, sy}, ImVec2{hi.x, sy}, kGridMinorUVE);
            }
        }
    }
    if (areaMin.x < 0.0F && areaMax.x > 0.0F) {
        const float sx = toScreen(Math::Vector2UVE{0.0F, 0.0F}).x;
        draw->AddLine(ImVec2{sx, lo.y}, ImVec2{sx, hi.y}, IM_COL32(255, 255, 255, 60));
    }
    if (twoD && areaMinY < 0.0F && areaMax.y > 0.0F) {
        const float sy = toScreen(Math::Vector2UVE{0.0F, 0.0F}).y;
        draw->AddLine(ImVec2{lo.x, sy}, ImVec2{hi.x, sy}, IM_COL32(255, 255, 255, 60));
    }
    if (!twoD) {
        draw->AddLine(ImVec2{lo.x, (lo.y + hi.y) * 0.5F}, ImVec2{hi.x, (lo.y + hi.y) * 0.5F}, IM_COL32(255, 255, 255, 60), 2.0F);
    }

    // What plays: while the tree runs, the weights and smoothed position it used; otherwise what
    // it would use at the parameters' position.
    std::vector<Math::Vector2UVE> positions;
    std::vector<float> positionsX;
    for (std::size_t slot = 0U; slot < pointCount; ++slot) {
        positions.push_back(pointAt(slot));
        positionsX.push_back(positions.back().x);
    }
    const std::vector<std::array<std::uint32_t, 3>> triangles =
        twoD ? Scene::TriangulateBlendSpaceUVE(positions) : std::vector<std::array<std::uint32_t, 3>>{};
    const Scene::AnimationGraphNodeStateUVE* const running =
        live.nodeStates.size() == live.nodes.size() && live.nodeStates[objectIndex].blendAtSet &&
                live.nodeStates[objectIndex].pointWeights.size() == pointCount
            ? &live.nodeStates[objectIndex]
            : nullptr;
    const Math::Vector2UVE blendAt = running != nullptr ? Math::Vector2UVE{running->blendAt.x, twoD ? running->blendAt.y : 0.0F} : cursor;
    std::vector<float> weights;
    if (running != nullptr) {
        weights = running->pointWeights;
    } else {
        weights = twoD ? Scene::AnimationBlendSpace2DWeightsUVE(positions, triangles, cursor)
                       : Scene::AnimationBlendSpace1DWeightsUVE(positionsX, cursor.x);
        const bool any = std::ranges::any_of(weights, [](const float w) { return w > 0.0F; });
        if (node.blendMode != Scene::AnimationBlendModeUVE::Blend && any) {
            // Nearest: the closest point alone.
            std::size_t nearest = 0U;
            const auto distance = [&](const std::size_t slot) {
                const Math::Vector2UVE gap = cursor - positions[slot];
                return gap.x * gap.x + gap.y * gap.y;
            };
            for (std::size_t slot = 1U; slot < pointCount; ++slot) {
                nearest = distance(slot) < distance(nearest) ? slot : nearest;
            }
            std::ranges::fill(weights, 0.0F);
            weights[nearest] = 1.0F;
        }
    }
    // The triangles the plane blends inside; the one the position is in, lit.
    for (const auto& triangle : triangles) {
        const ImVec2 a = toScreen(positions[triangle[0]]);
        const ImVec2 b = toScreen(positions[triangle[1]]);
        const ImVec2 c = toScreen(positions[triangle[2]]);
        const bool lit = weights[triangle[0]] + weights[triangle[1]] + weights[triangle[2]] > 0.999F &&
                         node.blendMode == Scene::AnimationBlendModeUVE::Blend;
        if (lit) {
            draw->AddTriangleFilled(a, b, c, IM_COL32(110, 210, 140, 28));
        }
        draw->AddTriangle(a, b, c, lit ? IM_COL32(110, 210, 140, 150) : IM_COL32(255, 255, 255, 45), lit ? 1.5F : 1.0F);
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int hoveredPoint = -1;
    for (std::size_t slot = 0U; slot < pointCount; ++slot) {
        const ImVec2 at = toScreen(pointAt(slot));
        if (hovered && (mouse.x - at.x) * (mouse.x - at.x) + (mouse.y - at.y) * (mouse.y - at.y) <= 81.0F) {
            hoveredPoint = static_cast<int>(slot);
        }
    }
    std::vector<ImVec4> placedLabels;
    for (std::size_t slot = 0U; slot < pointCount; ++slot) {
        const ImVec2 at = toScreen(pointAt(slot));
        const float weight = slot < weights.size() ? weights[slot] : 0.0F;
        if (weight > 0.0F) {
            draw->AddCircleFilled(at, 6.0F + 22.0F * weight, IM_COL32(110, 210, 140, static_cast<int>(20.0F + 60.0F * weight)));
        }
        const bool hot = hoveredPoint == static_cast<int>(slot) || view.plotDrag == static_cast<int>(slot);
        const ImU32 fill = view.spaceTool == 2 && hot ? kWireBadUVE : hot ? kSelectedUVE : weight > 0.0F ? kWireGoodUVE : IM_COL32(150, 158, 170, 255);
        draw->AddCircleFilled(at, 5.5F, fill);
        draw->AddCircle(at, 5.5F, kBorderUVE, 0, 1.5F);
        // What the point plays: its animation's name, or that it has none yet.
        const std::string& clipName = AnimationClipNameUVE(node.blendPoints[slot].clip);
        std::string label = clipName.empty() ? "Point " + std::to_string(slot + 1U) + " (no animation)" : clipName;
        if (weight > 0.005F) {
            char share[16];
            std::snprintf(share, sizeof(share), "  %d%%", static_cast<int>(std::lround(weight * 100.0F)));
            label += share;
        }
        // Beside the point, on the first side that neither leaves the plane nor covers a label
        // already placed: above-right, below-right, above-left, below-left.
        const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
        const std::array<ImVec2, 4> spots{ImVec2{at.x + 9.0F, at.y - labelSize.y - 2.0F}, ImVec2{at.x + 9.0F, at.y + 3.0F},
                                          ImVec2{at.x - 9.0F - labelSize.x, at.y - labelSize.y - 2.0F},
                                          ImVec2{at.x - 9.0F - labelSize.x, at.y + 3.0F}};
        const auto fits = [&](const ImVec2 spot) {
            const ImVec4 box{spot.x, spot.y, spot.x + labelSize.x, spot.y + labelSize.y};
            if (box.x < lo.x || box.z > hi.x || box.y < lo.y || box.w > hi.y) {
                return false;
            }
            return std::ranges::none_of(placedLabels, [&box](const ImVec4& other) {
                return box.x < other.z && other.x < box.z && box.y < other.w && other.y < box.w;
            });
        };
        const auto chosen = std::ranges::find_if(spots, fits);
        const ImVec2 spot = chosen != spots.end() ? *chosen : spots[0];
        placedLabels.push_back(ImVec4{spot.x, spot.y, spot.x + labelSize.x, spot.y + labelSize.y});
        draw->AddText(spot, hot ? kTextUVE : kTextDimUVE, label.c_str());
    }
    // The position: a cross where the parameters are; while smoothing, a ring where the space has
    // got to on its way there, tied to it.
    const ImVec2 cross = toScreen(cursor);
    const ImVec2 follow = toScreen(blendAt);
    const float lagX = follow.x - cross.x;
    const float lagY = follow.y - cross.y;
    if (running != nullptr && lagX * lagX + lagY * lagY > 4.0F) {
        draw->AddLine(cross, follow, IM_COL32(245, 190, 90, 120), 1.0F);
    }
    if (running != nullptr) {
        draw->AddCircle(follow, 7.0F, IM_COL32(245, 190, 90, 230), 0, 2.0F);
    }
    draw->AddLine(ImVec2{cross.x - 8.0F, cross.y}, ImVec2{cross.x + 8.0F, cross.y}, kSelectedUVE, 2.0F);
    draw->AddLine(ImVec2{cross.x, cross.y - 8.0F}, ImVec2{cross.x, cross.y + 8.0F}, kSelectedUVE, 2.0F);
    // Adding: where the point would land.
    if (view.spaceTool == 1 && hovered && hoveredPoint < 0) {
        const ImVec2 ghost = toScreen(toArea(mouse));
        draw->AddCircle(ghost, 6.0F, kWireGoodUVE, 0, 1.5F);
        draw->AddLine(ImVec2{ghost.x - 3.0F, ghost.y}, ImVec2{ghost.x + 3.0F, ghost.y}, kWireGoodUVE);
        draw->AddLine(ImVec2{ghost.x, ghost.y - 3.0F}, ImVec2{ghost.x, ghost.y + 3.0F}, kWireGoodUVE);
    }
    const char* hint = nullptr;
    if (pointCount == 0U) {
        hint = "No points yet: choose Add, then click where an animation belongs.";
    } else if (twoD && triangles.empty()) {
        hint = "A plane blends inside triangles: place points until three of them make one.";
    }
    if (hint != nullptr) {
        // An empty plane says so in its middle; a plane with points warns along its top edge, out of
        // the way of the points and the position.
        const ImVec2 hintSize = ImGui::CalcTextSize(hint);
        const bool empty = pointCount == 0U;
        const ImVec2 hintAt{(lo.x + hi.x - hintSize.x) * 0.5F, empty ? (lo.y + hi.y - hintSize.y) * 0.5F : lo.y + 6.0F};
        if (!empty) {
            draw->AddRectFilled(ImVec2{hintAt.x - 8.0F, hintAt.y - 3.0F}, ImVec2{hintAt.x + hintSize.x + 8.0F, hintAt.y + hintSize.y + 3.0F},
                                IM_COL32(70, 52, 20, 220), 3.0F);
        }
        draw->AddText(hintAt, empty ? kTextDimUVE : IM_COL32(245, 200, 120, 255), hint);
    }
    draw->PopClipRect();
    draw->AddRect(lo, hi, kBorderUVE, 3.0F);

    // ---- Interaction ---------------------------------------------------------------------------------
    if (writable && hovered && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_1)) {
            view.spaceTool = 0;
        } else if (ImGui::IsKeyPressed(ImGuiKey_2)) {
            view.spaceTool = 1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_3)) {
            view.spaceTool = 2;
        }
    }
    if (writable && ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (view.spaceTool == 1 && hoveredPoint < 0) {
            const Math::Vector2UVE at = toArea(mouse);
            // The new point's index, known once the edit runs; then its animation is asked for.
            edit = [spaceId, at, addedSlot](Scene::AnimationGraphComponentUVE& t) {
                *addedSlot = AddBlendSpacePointUVE(t.nodes, spaceId, at, Asset::AssetGuidUVE{});
            };
        } else if (view.spaceTool == 2 && hoveredPoint >= 0) {
            const auto slot = static_cast<std::size_t>(hoveredPoint);
            edit = [spaceId, slot](Scene::AnimationGraphComponentUVE& t) {
                static_cast<void>(RemoveBlendSpacePointUVE(t.nodes, spaceId, slot));
            };
        } else if (view.spaceTool == 0) {
            view.plotDrag = hoveredPoint >= 0 ? hoveredPoint : -1;
            view.plotBefore = live;
        }
    }
    if (view.plotDrag != -2 && ImGui::IsItemActive()) {
        const Math::Vector2UVE at = toArea(mouse);
        if (view.plotDrag >= 0) {
            static_cast<void>(MoveBlendSpacePointUVE(live.nodes, spaceId, static_cast<std::size_t>(view.plotDrag), at));
        } else {
            AnimationGraphNodeUVE& edited = live.nodes[objectIndex];
            const Math::Vector2UVE raw = toArea(mouse);
            const auto write = [&live](const std::string& name, float& fixed, const float value) {
                const auto found = std::ranges::find(live.parameters, name, &AnimationParameterUVE::name);
                if (!name.empty() && found != live.parameters.end()) {
                    found->value = value;
                } else {
                    fixed = value;
                }
            };
            write(edited.parameter, edited.value, raw.x);
            if (twoD) {
                write(edited.parameterY, edited.valueY, raw.y);
            }
        }
    }
    if (view.plotDrag != -2 && ImGui::IsItemDeactivated()) {
        const Scene::AnimationGraphComponentUVE after = live;
        live = view.plotBefore;
        view.plotDrag = -2;
        edit = [after](Scene::AnimationGraphComponentUVE& t) {
            t.parameters = after.parameters;
            t.nodes = after.nodes;
        };
    }
    // Right-click a point: its animation, or remove it.
    if (writable && hovered && hoveredPoint >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        view.pickClipForSlot = hoveredPoint;
        ImGui::OpenPopup("##point-menu");
    }

    // ---- X range and the readout under the plane -----------------------------------------------------
    const float rangeY = plotTop + plotHeight + style.ItemSpacing.y;
    rangeField("##min-x", node.areaMin.x, ImVec2{plotLeft, rangeY}, false, false);
    const std::string xName = node.parameter.empty() ? std::string{"x"} : node.parameter;
    char readout[96];
    if (twoD) {
        std::snprintf(readout, sizeof(readout), "%s   -   at %.2f, %.2f", xName.c_str(), static_cast<double>(cursor.x),
                      static_cast<double>(cursor.y));
    } else {
        std::snprintf(readout, sizeof(readout), "%s   -   at %.2f", xName.c_str(), static_cast<double>(cursor.x));
    }
    const ImVec2 readoutSize = ImGui::CalcTextSize(readout);
    ImGui::GetWindowDrawList()->AddText(ImVec2{plotLeft + (plotWidth - readoutSize.x) * 0.5F, rangeY + (rowHeight - readoutSize.y) * 0.5F},
                                        kTextDimUVE, readout);
    rangeField("##max-x", node.areaMax.x, ImVec2{plotLeft + plotWidth - rangeWidth, rangeY}, true, false);

    // ---- How the position becomes what plays -------------------------------------------------------
    ImGui::SetCursorScreenPos(ImVec2{plotLeft, rangeY + rowHeight + style.ItemSpacing.y});
    ImGui::BeginDisabled(!writable);
    DrawBlendSpaceSettingsUVE(node, [&editSpace](std::function<void(AnimationGraphNodeUVE&)> change) {
        editSpace(std::move(change));
    });
    ImGui::EndDisabled();

    // ---- A point's own settings (after Add, or from its right-click menu) -----------------------------
    if (ImGui::BeginPopup("##point-menu")) {
        const auto slot = static_cast<std::size_t>(std::max(view.pickClipForSlot, 0));
        const AnimationGraphNodeUVE& current = live.nodes[objectIndex];
        if (slot < current.blendPoints.size()) {
            const Scene::AnimationBlendPointUVE point = current.blendPoints[slot];
            const auto editPoint = [&editSpace, slot](std::function<void(Scene::AnimationBlendPointUVE&)> change) {
                editSpace([slot, change = std::move(change)](AnimationGraphNodeUVE& n) {
                    if (slot < n.blendPoints.size()) {
                        change(n.blendPoints[slot]);
                    }
                });
            };
            ImGui::TextDisabled("Point %zu", slot + 1U);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Animation");
            ImGui::SameLine(84.0F);
            ImGui::SetNextItemWidth(220.0F);
            if (const std::optional<Asset::AssetGuidUVE> picked = DrawAssetPickerUVE("##point-clip", point.clip, ".uvanim")) {
                const Asset::AssetGuidUVE guid = *picked;
                editPoint([guid](Scene::AnimationBlendPointUVE& p) { p.clip = guid; });
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Speed");
            ImGui::SameLine(84.0F);
            ImGui::SetNextItemWidth(120.0F);
            float speed = point.speed;
            ImGui::DragFloat("##point-speed", &speed, 0.01F, -100.0F, 100.0F, "x%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                editPoint([speed](Scene::AnimationBlendPointUVE& p) { p.speed = speed; });
            }
            ImGui::SameLine();
            bool loop = point.loop;
            if (ImGui::Checkbox("Loop", &loop)) {
                editPoint([loop](Scene::AnimationBlendPointUVE& p) { p.loop = loop; });
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Point")) {
                edit = [spaceId, slot](Scene::AnimationGraphComponentUVE& t) {
                    static_cast<void>(RemoveBlendSpacePointUVE(t.nodes, spaceId, slot));
                };
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (edit.has_value()) {
        static_cast<void>(EditAnimationGraphUVE(tree, *edit));
        if (addedSlot->has_value()) {
            // The point just added: straight to its animation.
            view.pickClipForSlot = static_cast<int>(**addedSlot);
            ImGui::OpenPopup("##point-menu");
        }
    }
}

void EditorUVE::RecordAnimationGraphPreviewUVE(const Scene::AnimationGraphComponentUVE& tree) {
    AnimationGraphViewStateUVE& view = m_animGraph;
    constexpr std::size_t kLogLinesUVE = 80U;
    constexpr std::size_t kHistorySamplesUVE = 180U;
    const auto log = [&view](std::string text, const bool stateChange) {
        view.previewLog.push_back(AnimationGraphViewStateUVE::PreviewLogLineUVE{view.previewClock, std::move(text), stateChange});
        if (view.previewLog.size() > kLogLinesUVE) {
            view.previewLog.erase(view.previewLog.begin());
        }
    };
    for (const std::string& event : tree.firedEvents) {
        log("event  " + event, false);
    }
    if (tree.nodeStates.size() == tree.nodes.size()) {
        for (std::size_t index = 0U; index < tree.nodes.size(); ++index) {
            const AnimationGraphNodeUVE& machine = tree.nodes[index];
            if (machine.kind != Kind::StateMachine) {
                continue;
            }
            const std::uint32_t active = tree.nodeStates[index].activeState;
            const auto [it, added] = view.previewActive.try_emplace(machine.id, active);
            if (added || it->second == active) {
                continue;
            }
            const auto nameOf = [&tree, &machine](const std::uint32_t slot) {
                if (slot < machine.inputs.size()) {
                    const auto child = std::ranges::find(tree.nodes, machine.inputs[slot], &AnimationGraphNodeUVE::id);
                    if (child != tree.nodes.end() && !child->name.empty()) {
                        return child->name;
                    }
                }
                return AnimationGraphSlotLabelUVE(Kind::StateMachine, slot);
            };
            // Which machine only matters when there are several.
            const bool several = std::ranges::count(tree.nodes, Kind::StateMachine, &AnimationGraphNodeUVE::kind) > 1;
            const std::string owner = several ? (machine.name.empty() ? std::string{"State Machine"} : machine.name) + ": " : "";
            log(owner + nameOf(it->second) + " -> " + nameOf(active), true);
            it->second = active;
        }
    }
    for (const AnimationParameterUVE& parameter : tree.parameters) {
        if (parameter.type != AnimationParameterTypeUVE::Float) {
            continue;
        }
        std::vector<float>& samples = view.parameterHistory[parameter.name];
        samples.push_back(parameter.value);
        if (samples.size() > kHistorySamplesUVE) {
            samples.erase(samples.begin());
        }
    }
}

void EditorUVE::StopAnimationGraphPreviewUVE() {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (m_animGraph.previewSkeleton != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(m_animGraph.previewSkeleton) &&
        entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(m_animGraph.previewSkeleton)) {
        entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(m_animGraph.previewSkeleton).pose.clear();
    }
    m_animGraph.previewSkeleton = Scene::kInvalidEntityUVE;
    if (m_animGraph.tree != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(m_animGraph.tree) &&
        entityManager.HasComponentUVE<Scene::AnimationGraphComponentUVE>(m_animGraph.tree)) {
        // Back to the start, so the next preview (and Play) begins from the entry state.
        entityManager.GetComponentUVE<Scene::AnimationGraphComponentUVE>(m_animGraph.tree).nodeStates.clear();
    }
}

bool EditorUVE::EditAnimationGraphUVE(const Scene::EntityUVE tree,
                                     const std::function<void(Scene::AnimationGraphComponentUVE&)>& change) {
    if (!IsAuthoringCommandAllowedUVE()) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.IsAliveUVE(tree) || !entityManager.HasComponentUVE<Scene::AnimationGraphComponentUVE>(tree)) {
        return false;
    }
    const Core::TypeMetadataEntryUVE* const entry = Scene::GetSceneComponentMetadataRegistryUVE().FindTypeByIndexUVE(
        std::type_index(typeid(Scene::AnimationGraphComponentUVE)));
    if (entry == nullptr || !entry->HasFactoryUVE()) {
        return false;
    }
    auto& component = entityManager.GetComponentUVE<Scene::AnimationGraphComponentUVE>(tree);
    const Scene::AnimationGraphComponentUVE original = component;
    Core::TypeInstanceUVE before = Core::TypeInstanceUVE::CloneUVE(*entry, &component);
    change(component);
    if (component.HasSameSettingsUVE(original) || !before.IsValidUVE()) {
        return false;
    }
    const std::string problem = Scene::DescribeAnimationGraphProblemUVE(component);
    Core::TypeInstanceUVE after = Core::TypeInstanceUVE::CloneUVE(*entry, &component);
    if (!problem.empty() || !after.IsValidUVE()) {
        component = original;
        m_animGraph.status = problem.empty() ? std::string{"That change could not be recorded."} : problem;
        return false;
    }
    // The shape may have changed: the runtime rebuilds its per-node state from the new graph.
    component.nodeStates.clear();
    const EditorSelectionSnapshotUVE selection = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    m_sceneDirty = true;
    RecordHistoryUVE(ComponentPropertyHistoryEntryUVE{tree, entry, std::move(before), std::move(after), selection,
                                                      selection, dirtyBefore, true});
    return true;
}

void EditorUVE::DrawAnimationGraphCanvasUVE() {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    AnimationGraphViewStateUVE& view = m_animGraph;

    // The tree to show: the selected node when it is one, else the one already shown, else the
    // entity's first.
    const auto isTree = [&entityManager](const Scene::EntityUVE entity) {
        return entity != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(entity) &&
               entityManager.HasComponentUVE<Scene::AnimationGraphComponentUVE>(entity);
    };
    const std::vector<Scene::EntityUVE> entityObjects = CollectEntityEditorObjectsUVE();
    const auto inEntity = [&entityObjects](const Scene::EntityUVE entity) {
        return std::ranges::find(entityObjects, entity) != entityObjects.end();
    };
    Scene::EntityUVE tree = Scene::kInvalidEntityUVE;
    if (isTree(m_selectedEntity) && inEntity(m_selectedEntity)) {
        tree = m_selectedEntity;
    } else if (isTree(view.tree) && inEntity(view.tree)) {
        tree = view.tree;
    } else {
        for (const Scene::EntityUVE node : entityObjects) {
            if (isTree(node)) {
                tree = node;
                break;
            }
        }
    }
    if (tree != view.tree) {
        StopAnimationGraphPreviewUVE();
        const bool previewing = view.previewing;
        auto clips = std::move(view.clips);
        view = AnimationGraphViewStateUVE{};
        view.tree = tree;
        view.previewing = previewing;
        view.clips = std::move(clips);
    }
    if (tree == Scene::kInvalidEntityUVE) {
        ImGui::TextDisabled("This entity has no AnimationGraph.");
        return;
    }
    // ---- Preview: the tree runs on the entity's skeleton, the driver's target or else the tree's
    // parent, searched down. Only the skeleton's runtime pose changes; nothing is saved.
    Scene::EntityUVE skeletonEntity = Scene::kInvalidEntityUVE;
    const Scene::AnimationDriverComponentUVE driver = entityManager.HasComponentUVE<Scene::AnimationDriverComponentUVE>(tree)
                                                        ? entityManager.GetComponentUVE<Scene::AnimationDriverComponentUVE>(tree)
                                                        : Scene::AnimationDriverComponentUVE{};
    {
        Scene::EntityUVE root = driver.target;
        if (root == Scene::kInvalidEntityUVE || !entityManager.IsAliveUVE(root)) {
            root = entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(tree)
                       ? entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(tree).parent
                       : Scene::kInvalidEntityUVE;
        }
        std::vector<Scene::EntityUVE> queue;
        if (root != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(root)) {
            queue.push_back(root);
        }
        Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();
        for (std::size_t next = 0U; next < queue.size() && next < 4096U; ++next) {
            if (entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(queue[next])) {
                skeletonEntity = queue[next];
                break;
            }
            const std::vector<Scene::EntityUVE> children = sceneGraph.GetChildrenUVE(entityManager, queue[next]);
            queue.insert(queue.end(), children.begin(), children.end());
        }
    }
    if (view.previewing && skeletonEntity != Scene::kInvalidEntityUVE && !view.draggingObjects) {
        const Scene::AnimationClipResolverUVE clipFor = [this](const Asset::AssetGuidUVE guid)
            -> const Asset::AnimationClipAssetUVE* {
            if (guid == Asset::AssetGuidUVE{}) {
                return nullptr;
            }
            auto found = m_animGraph.clips.find(guid.value);
            if (found == m_animGraph.clips.end()) {
                auto loaded = std::make_shared<Asset::AnimationClipAssetUVE>();
                const std::filesystem::path path = m_services->GetAssetDatabaseUVE().ResolveUVE(guid);
                found = m_animGraph.clips
                            .emplace(guid.value, Asset::LoadAnimationClipAssetUVE(path, *loaded) ? std::move(loaded) : nullptr)
                            .first;
            }
            return found->second.get();
        };
        if (view.previewSkeleton != skeletonEntity) {
            StopAnimationGraphPreviewUVE();
            view.previewSkeleton = skeletonEntity;
        }
        auto& live = entityManager.GetComponentUVE<Scene::AnimationGraphComponentUVE>(tree);
        auto& skeleton = entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(skeletonEntity);
        Scene::AnimationDriverComponentUVE previewMixer = driver;
        previewMixer.active = true;
        // Held: only a frame asked for moves it, and then by one sixtieth of a second.
        const float frame = view.previewPaused ? (view.previewStepOnce ? 1.0F / 60.0F : 0.0F)
                                               : std::clamp(ImGui::GetIO().DeltaTime, 0.0F, 0.1F) * view.previewRate;
        view.previewStepOnce = false;
        if (frame > 0.0F) {
            const float step = frame * driver.speedScale;
            static_cast<void>(Scene::StepSkeletalAnimationGraphUVE(live, clipFor, step, skeleton, previewMixer));
            view.previewClock += static_cast<double>(frame);
            RecordAnimationGraphPreviewUVE(live);
        }
    } else if (!view.previewing && view.previewSkeleton != Scene::kInvalidEntityUVE) {
        StopAnimationGraphPreviewUVE();
    }
    const auto clipDuration = [this](const Asset::AssetGuidUVE guid) {
        const auto found = m_animGraph.clips.find(guid.value);
        return found != m_animGraph.clips.end() && found->second != nullptr ? found->second->durationSeconds : 0.0;
    };

    const Scene::AnimationGraphComponentUVE& component = entityManager.GetComponentUVE<Scene::AnimationGraphComponentUVE>(tree);
    // A copy: edits below replace the component, and this frame keeps drawing what it started with.
    const std::vector<AnimationGraphNodeUVE> nodes = component.nodes;
    const std::vector<Scene::AnimationGraphNodeStateUVE> states =
        component.nodeStates.size() == nodes.size() ? component.nodeStates : std::vector<Scene::AnimationGraphNodeStateUVE>{};
    std::erase_if(view.selected, [&nodes](const std::uint32_t id) {
        return std::ranges::find(nodes, id, &AnimationGraphNodeUVE::id) == nodes.end();
    });
    const bool writable = IsAuthoringCommandAllowedUVE();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float barHeight = ImGui::GetFrameHeight() + style.WindowPadding.y;
    const ImVec2 area = ImGui::GetContentRegionAvail();
    const float canvasWidth = std::max(120.0F, area.x - kSideStripWidthUVE - style.ItemSpacing.x);
    const float canvasHeight = std::max(80.0F, area.y - barHeight - style.ItemSpacing.y);

    std::optional<std::function<void(Scene::AnimationGraphComponentUVE&)>> edit;
    std::unordered_map<std::uint32_t, std::size_t> indexById;
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        indexById.emplace(nodes[index].id, index);
    }

    // ---- Path: the tree, and the node opened in its own editor --------------------------------------
    const auto focusIt = indexById.find(view.focus);
    const bool focused = view.focus != 0U && focusIt != indexById.end() &&
                         (nodes[focusIt->second].kind == Kind::BlendSpace1D || nodes[focusIt->second].kind == Kind::BlendSpace2D ||
                          nodes[focusIt->second].kind == Kind::StateMachine);
    const bool focusedMachine = focused && nodes[focusIt->second].kind == Kind::StateMachine;
    if (!focused) {
        view.focus = 0U;
    }
    const float pathTop = ImGui::GetCursorPosY();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Path");
    ImGui::SameLine();
    if (ImGui::Button("Tree") || (focused && ImGui::IsKeyPressed(ImGuiKey_Escape) && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))) {
        view.focus = 0U;
    }
    if (focused) {
        const AnimationGraphNodeUVE& open = nodes[focusIt->second];
        ImGui::SameLine();
        ImGui::TextDisabled(">");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(KindColourUVE(open.kind)));
        ImGui::Button(open.name.empty() ? AnimationGraphKindLabelUVE(open.kind) : open.name.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("(Esc goes back)");
    }
    const float pathHeight = ImGui::GetCursorPosY() - pathTop;
    const float graphHeight = std::max(60.0F, canvasHeight - pathHeight);

    if (focused) {
        ImGui::BeginChild("##space-editor", ImVec2{canvasWidth, graphHeight}, false,
                          ImGuiWindowFlags_NoScrollbar | (focusedMachine ? ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove : 0));
        if (focusedMachine) {
            DrawStateMachineViewUVE(tree, focusIt->second);
        } else {
            DrawBlendSpaceEditorUVE(tree, focusIt->second);
        }
        m_timelineOwnsKeys = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::GetIO().WantTextInput;
        ImGui::EndChild();
    } else {
    // ---- Canvas -----------------------------------------------------------------------------------
    ImGui::BeginChild("##graph-canvas", ImVec2{canvasWidth, graphHeight}, false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();

    // First look: frame the whole graph.
    const auto frameAll = [&]() {
        if (nodes.empty()) {
            return;
        }
        float minX = FLT_MAX;
        float minY = FLT_MAX;
        float maxX = -FLT_MAX;
        float maxY = -FLT_MAX;
        for (const AnimationGraphNodeUVE& node : nodes) {
            minX = std::min(minX, node.position.x);
            minY = std::min(minY, node.position.y);
            maxX = std::max(maxX, node.position.x + kObjectWidthUVE);
            maxY = std::max(maxY, node.position.y + ObjectHeightUVE(node));
        }
        constexpr float kMarginUVE = 48.0F;
        const float zoomX = size.x / std::max(1.0F, maxX - minX + kMarginUVE * 2.0F);
        const float zoomY = size.y / std::max(1.0F, maxY - minY + kMarginUVE * 2.0F);
        view.zoom = std::clamp(std::min(zoomX, zoomY), kMinimumZoomUVE, 1.0F);
        view.panX = (minX + maxX) * 0.5F - size.x * 0.5F / view.zoom;
        view.panY = (minY + maxY) * 0.5F - size.y * 0.5F / view.zoom;
    };
    if (!view.framed && size.x > 1.0F && size.y > 1.0F) {
        frameAll();
        view.framed = true;
    }
    const auto toScreen = [&](const float x, const float y) {
        return ImVec2{origin.x + (x - view.panX) * view.zoom, origin.y + (y - view.panY) * view.zoom};
    };
    const auto toCanvas = [&](const ImVec2 point) {
        return ImVec2{(point.x - origin.x) / view.zoom + view.panX, (point.y - origin.y) / view.zoom + view.panY};
    };

    ImGui::SetNextItemAllowOverlap(); // the values edited on nodes sit on top of the canvas
    ImGui::InvisibleButton("##canvas", ImVec2{std::max(1.0F, size.x), std::max(1.0F, size.y)},
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = io.MousePos;
    draw->PushClipRect(origin, ImVec2{origin.x + size.x, origin.y + size.y}, true);
    draw->AddRectFilled(origin, ImVec2{origin.x + size.x, origin.y + size.y}, kCanvasUVE);

    // Grid: minor every 24 units, major every 5.
    const float step = 24.0F * view.zoom;
    if (step >= 6.0F) {
        const float firstX = std::floor(view.panX / 24.0F);
        const float firstY = std::floor(view.panY / 24.0F);
        for (float i = firstX;; i += 1.0F) {
            const float x = toScreen(i * 24.0F, 0.0F).x;
            if (x > origin.x + size.x) {
                break;
            }
            draw->AddLine(ImVec2{x, origin.y}, ImVec2{x, origin.y + size.y},
                          std::fmod(std::abs(i), 5.0F) < 0.5F ? kGridMajorUVE : kGridMinorUVE);
        }
        for (float i = firstY;; i += 1.0F) {
            const float y = toScreen(0.0F, i * 24.0F).y;
            if (y > origin.y + size.y) {
                break;
            }
            draw->AddLine(ImVec2{origin.x, y}, ImVec2{origin.x + size.x, y},
                          std::fmod(std::abs(i), 5.0F) < 0.5F ? kGridMajorUVE : kGridMinorUVE);
        }
    }

    // Geometry of each node on screen, for drawing and hit tests.
    const auto objectMin = [&](const AnimationGraphNodeUVE& node) { return toScreen(node.position.x, node.position.y); };
    const auto objectMax = [&](const AnimationGraphNodeUVE& node) {
        return toScreen(node.position.x + kObjectWidthUVE, node.position.y + ObjectHeightUVE(node));
    };
    const auto outputPin = [&](const AnimationGraphNodeUVE& node) {
        return toScreen(node.position.x + kObjectWidthUVE, node.position.y + kHeaderHeightUVE * 0.5F);
    };
    const auto inputPin = [&](const AnimationGraphNodeUVE& node, const std::size_t slot) {
        const auto row = static_cast<float>(InlineRowsUVE(node.kind) + slot);
        return toScreen(node.position.x, node.position.y + kHeaderHeightUVE + (row + 0.5F) * kSlotHeightUVE);
    };
    const float pinHit = std::max(kPinRadiusUVE * view.zoom + 4.0F, 8.0F);
    const auto near = [pinHit](const ImVec2 a, const ImVec2 b) {
        return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) <= pinHit * pinHit;
    };

    // What is under the mouse: an input pin, an output pin, or a node (topmost = last drawn).
    struct SlotHitUVE {
        std::uint32_t node = 0U;
        std::size_t slot = 0U;
    };
    std::optional<SlotHitUVE> hoveredSlot;
    std::uint32_t hoveredOutput = 0U;
    std::uint32_t hoveredObject = 0U;
    if (hovered) {
        for (const AnimationGraphNodeUVE& node : nodes) {
            if (node.kind != Kind::Output && near(mouse, outputPin(node))) {
                hoveredOutput = node.id;
            }
            for (std::size_t slot = 0U; slot < node.inputs.size(); ++slot) {
                if (near(mouse, inputPin(node, slot))) {
                    hoveredSlot = SlotHitUVE{node.id, slot};
                }
            }
            const ImVec2 lo = objectMin(node);
            const ImVec2 hi = objectMax(node);
            if (mouse.x >= lo.x && mouse.x <= hi.x && mouse.y >= lo.y && mouse.y <= hi.y) {
                hoveredObject = node.id;
            }
        }
    }

    const auto wireCurve = [&](const ImVec2 from, const ImVec2 to, const ImU32 colour, const float thickness) {
        const float bend = std::max(40.0F * view.zoom, std::abs(to.x - from.x) * 0.5F);
        draw->AddBezierCubic(from, ImVec2{from.x + bend, from.y}, ImVec2{to.x - bend, to.y}, to, colour,
                             thickness * std::max(0.6F, view.zoom));
    };

    // Which nodes the Output actually reaches: live wires are drawn brighter than dangling ones.
    std::vector<std::uint32_t> reached;
    for (const AnimationGraphNodeUVE& node : nodes) {
        if (node.kind == Kind::Output) {
            std::vector<std::uint32_t> pending{node.id};
            while (!pending.empty()) {
                const std::uint32_t id = pending.back();
                pending.pop_back();
                if (std::ranges::find(reached, id) != reached.end()) {
                    continue;
                }
                reached.push_back(id);
                if (const auto it = indexById.find(id); it != indexById.end()) {
                    for (const std::uint32_t input : nodes[it->second].inputs) {
                        if (input != 0U) {
                            pending.push_back(input);
                        }
                    }
                }
            }
        }
    }

    // Wires under the nodes.
    for (const AnimationGraphNodeUVE& node : nodes) {
        for (std::size_t slot = 0U; slot < node.inputs.size(); ++slot) {
            const auto source = indexById.find(node.inputs[slot]);
            if (source == indexById.end()) {
                continue;
            }
            const bool live = std::ranges::find(reached, node.id) != reached.end();
            const ImVec2 from = outputPin(nodes[source->second]);
            const ImVec2 to = inputPin(node, slot);
            if (!states.empty() && view.previewing && live) {
                // Previewing: a wire is as bright and thick as the share of the pose it carries.
                const float weight = states[source->second].weight;
                const auto alpha = static_cast<int>(70.0F + 185.0F * weight);
                wireCurve(from, to, IM_COL32(110, 210, 140, alpha), 1.4F + 2.6F * weight);
                if (weight > 0.005F && weight < 0.995F && view.zoom >= 0.6F) {
                    char label[16];
                    std::snprintf(label, sizeof(label), "%d%%", static_cast<int>(std::lround(weight * 100.0F)));
                    const ImVec2 middle{(from.x + to.x) * 0.5F, (from.y + to.y) * 0.5F};
                    const ImVec2 textSize = ImGui::CalcTextSize(label);
                    draw->AddRectFilled(ImVec2{middle.x - textSize.x * 0.5F - 3.0F, middle.y - textSize.y * 0.5F - 1.0F},
                                        ImVec2{middle.x + textSize.x * 0.5F + 3.0F, middle.y + textSize.y * 0.5F + 1.0F},
                                        IM_COL32(20, 22, 26, 220), 3.0F);
                    draw->AddText(ImVec2{middle.x - textSize.x * 0.5F, middle.y - textSize.y * 0.5F}, kTextUVE, label);
                }
            } else {
                wireCurve(from, to, live ? kWireUVE : IM_COL32(120, 126, 136, 120), live ? 2.2F : 1.6F);
            }
        }
    }

    // Objects.
    const float fontScale = std::clamp(view.zoom, 0.6F, 1.4F);
    const float fontSize = ImGui::GetFontSize() * fontScale;
    ImFont* const font = ImGui::GetFont();
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const AnimationGraphNodeUVE& node = nodes[index];
        const ImVec2 lo = objectMin(node);
        const ImVec2 hi = objectMax(node);
        if (hi.x < origin.x || lo.x > origin.x + size.x || hi.y < origin.y || lo.y > origin.y + size.y) {
            continue;
        }
        const float rounding = 5.0F * view.zoom;
        const float header = kHeaderHeightUVE * view.zoom;
        const bool selected = std::ranges::find(view.selected, node.id) != view.selected.end();
        draw->AddRectFilled(ImVec2{lo.x + 2.0F, lo.y + 3.0F}, ImVec2{hi.x + 2.0F, hi.y + 3.0F}, IM_COL32(0, 0, 0, 70),
                            rounding);
        draw->AddRectFilled(lo, hi, kBodyUVE, rounding);
        draw->AddRectFilled(lo, ImVec2{hi.x, lo.y + header}, KindColourUVE(node.kind), rounding,
                            ImDrawFlags_RoundCornersTop);
        draw->AddRect(lo, hi, selected ? kSelectedUVE : (node.id == hoveredObject ? IM_COL32(110, 118, 132, 255) : kBorderUVE),
                      rounding, 0, selected ? 2.0F : 1.0F);
        const std::string title = node.name.empty() ? std::string{AnimationGraphKindLabelUVE(node.kind)} : node.name;
        const float textY = lo.y + (header - fontSize) * 0.5F;
        draw->AddText(font, fontSize, ImVec2{lo.x + 8.0F * view.zoom, textY}, kTextUVE, title.c_str());
        if (node.name != AnimationGraphKindLabelUVE(node.kind) && view.zoom >= 0.7F) {
            const char* const kindLabel = AnimationGraphKindLabelUVE(node.kind);
            const float kindWidth = font->CalcTextSizeA(fontSize * 0.85F, FLT_MAX, 0.0F, kindLabel).x;
            draw->AddText(font, fontSize * 0.85F, ImVec2{hi.x - kindWidth - 8.0F * view.zoom, textY + fontSize * 0.1F},
                          IM_COL32(255, 255, 255, 150), kindLabel);
        }

        // Previewing: where a clip is, as a bar under its header, lit while it counts.
        if (node.kind == Kind::Clip && view.previewing && index < states.size()) {
            const double duration = clipDuration(node.clip);
            if (duration > 0.0) {
                const float progress = static_cast<float>(std::clamp(states[index].timeSeconds / duration, 0.0, 1.0));
                const float barY = lo.y + header;
                draw->AddRectFilled(ImVec2{lo.x + 1.0F, barY}, ImVec2{hi.x - 1.0F, barY + 3.0F}, IM_COL32(20, 22, 26, 255));
                draw->AddRectFilled(ImVec2{lo.x + 1.0F, barY}, ImVec2{lo.x + 1.0F + (hi.x - lo.x - 2.0F) * progress, barY + 3.0F},
                                    states[index].weight > 0.0F ? kActiveUVE : IM_COL32(90, 96, 108, 255));
            }
        }
        // Input slots, each with its meaning and what feeds it.
        const auto* const state = index < states.size() ? &states[index] : nullptr;
        for (std::size_t slot = 0U; slot < node.inputs.size(); ++slot) {
            const ImVec2 pin = inputPin(node, slot);
            const bool filled = node.inputs[slot] != 0U;
            const bool activeState = node.kind == Kind::StateMachine && state != nullptr && state->activeState == slot;
            const bool slotHovered = hoveredSlot.has_value() && hoveredSlot->node == node.id && hoveredSlot->slot == slot;
            ImU32 pinColour = filled ? kWireUVE : IM_COL32(90, 96, 108, 255);
            if (view.wireFrom != 0U && slotHovered) {
                pinColour = CanConnectAnimationGraphNodesUVE(nodes, node.id, slot, view.wireFrom) ? kWireGoodUVE : kWireBadUVE;
            }
            draw->AddCircleFilled(pin, kPinRadiusUVE * view.zoom, pinColour);
            draw->AddCircle(pin, kPinRadiusUVE * view.zoom, kBorderUVE);
            std::string label = AnimationGraphSlotLabelUVE(node.kind, slot);
            if (node.kind == Kind::StateMachine && filled) {
                if (const auto it = indexById.find(node.inputs[slot]); it != indexById.end()) {
                    label = nodes[it->second].name.empty() ? label : nodes[it->second].name;
                }
                if (slot == node.entryState) {
                    label = "> " + label;
                }
            }
            draw->AddText(font, fontSize, ImVec2{pin.x + 10.0F * view.zoom, pin.y - fontSize * 0.5F},
                          activeState ? kActiveUVE : kTextDimUVE, label.c_str());
        }
        const std::size_t inlineRows = InlineRowsUVE(node.kind);
        if (inlineRows > 0U && view.zoom >= 0.7F) {
            // Values edited right on the node: drag to tune, one undo step per drag.
            auto& liveTree = entityManager.GetComponentUVE<Scene::AnimationGraphComponentUVE>(tree);
            const std::uint32_t objectId = node.id;
            const float rowHeight = kSlotHeightUVE * view.zoom;
            const float inset = 8.0F * view.zoom;
            const float rowWidth = (hi.x - lo.x) - inset * 2.0F;
            const auto rowAt = [&](const std::size_t row) {
                return ImVec2{lo.x + inset, lo.y + header + static_cast<float>(row) * rowHeight + 2.0F * view.zoom};
            };
            ImGui::PushID(static_cast<int>(objectId));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                ImVec2{4.0F, std::max(0.0F, (rowHeight - ImGui::GetFontSize()) * 0.5F - 2.0F)});
            ImGui::BeginDisabled(!writable);
            // A parameter-driven value edits the parameter (what the preview reads); else the node's own.
            const auto readValue = [&liveTree](const std::string& parameterName, const float fixed) {
                const auto found = std::ranges::find(liveTree.parameters, parameterName, &AnimationParameterUVE::name);
                return parameterName.empty() || found == liveTree.parameters.end() ? fixed : found->value;
            };
            const auto inlineDrag = [&](const char* id, const char* format, const float shown, const float speed,
                                        const float minimum, const float maximum,
                                        const std::function<void(Scene::AnimationGraphComponentUVE&, float)>& apply,
                                        const std::size_t row) {
                ImGui::SetCursorScreenPos(rowAt(row));
                ImGui::SetNextItemWidth(rowWidth);
                float value = shown;
                if (ImGui::DragFloat(id, &value, speed, minimum, maximum, format)) {
                    if (!view.inlineEditing) {
                        view.inlineEditing = true;
                        view.inlineBefore = liveTree;
                    }
                    apply(liveTree, value);
                }
                if (ImGui::IsItemDeactivated() && view.inlineEditing) {
                    view.inlineEditing = false;
                    const Scene::AnimationGraphComponentUVE after = liveTree;
                    liveTree = view.inlineBefore;
                    edit = [after](Scene::AnimationGraphComponentUVE& t) {
                        t.parameters = after.parameters;
                        t.nodes = after.nodes;
                    };
                }
            };
            const auto objectField = [objectId](float AnimationGraphNodeUVE::*field, const std::string parameterName) {
                return [objectId, field, parameterName](Scene::AnimationGraphComponentUVE& t, const float value) {
                    const auto found = std::ranges::find(t.parameters, parameterName, &AnimationParameterUVE::name);
                    if (!parameterName.empty() && found != t.parameters.end()) {
                        found->value = value;
                        return;
                    }
                    const auto it = std::ranges::find(t.nodes, objectId, &AnimationGraphNodeUVE::id);
                    if (it != t.nodes.end()) {
                        (*it).*field = value;
                    }
                };
            };
            const std::string xFormat = (node.parameter.empty() ? std::string{"x %.2f"} : node.parameter + " %.2f");
            const std::string weightFormat = (node.parameter.empty() ? std::string{"weight %.2f"} : node.parameter + " %.2f");
            switch (node.kind) {
                case Kind::Clip: {
                    ImGui::SetCursorScreenPos(rowAt(0U));
                    ImGui::SetNextItemWidth(rowWidth);
                    if (const std::optional<Asset::AssetGuidUVE> picked = DrawAssetPickerUVE("##clip", node.clip, ".uvanim")) {
                        const Asset::AssetGuidUVE guid = *picked;
                        const std::string clipName = AnimationClipNameUVE(guid);
                        edit = [objectId, guid, clipName](Scene::AnimationGraphComponentUVE& t) {
                            const auto it = std::ranges::find(t.nodes, objectId, &AnimationGraphNodeUVE::id);
                            if (it != t.nodes.end()) {
                                it->clip = guid;
                                // A clip node still named for its kind takes its animation's name.
                                if ((it->name.empty() || it->name == "Clip") && !clipName.empty()) {
                                    it->name = clipName;
                                }
                            }
                        };
                    }
                    break;
                }
                case Kind::Blend2:
                case Kind::Additive:
                case Kind::LayeredBlend:
                    inlineDrag("##weight", weightFormat.c_str(), readValue(node.parameter, node.value), 0.01F, 0.0F, 1.0F,
                               objectField(&AnimationGraphNodeUVE::value, node.parameter), 0U);
                    break;
                case Kind::Select:
                    inlineDrag("##option", node.parameter.empty() ? "option %.0f" : (node.parameter + " %.0f").c_str(),
                               readValue(node.parameter, node.value), 0.05F, 0.0F,
                               static_cast<float>(std::max<std::size_t>(node.inputs.size(), 1U) - 1U),
                               objectField(&AnimationGraphNodeUVE::value, node.parameter), 0U);
                    break;
                case Kind::TimeScale:
                    inlineDrag("##rate", node.parameter.empty() ? "rate x%.2f" : (node.parameter + " x%.2f").c_str(),
                               readValue(node.parameter, node.speed), 0.01F, -100.0F, 100.0F,
                               objectField(&AnimationGraphNodeUVE::speed, node.parameter), 0U);
                    break;
                case Kind::TimeSeek:
                    inlineDrag("##seek", "to %.2fs", node.value, 0.01F, 0.0F, 3600.0F,
                               objectField(&AnimationGraphNodeUVE::value, std::string{}), 0U);
                    break;
                case Kind::BlendSpace1D:
                case Kind::BlendSpace2D: {
                    inlineDrag("##x", xFormat.c_str(), readValue(node.parameter, node.value), 0.01F, 0.0F, 0.0F,
                               objectField(&AnimationGraphNodeUVE::value, node.parameter), 0U);
                    std::size_t next = 1U;
                    if (node.kind == Kind::BlendSpace2D) {
                        const std::string yFormat = node.parameterY.empty() ? std::string{"y %.2f"} : node.parameterY + " %.2f";
                        inlineDrag("##y", yFormat.c_str(), readValue(node.parameterY, node.valueY), 0.01F, 0.0F, 0.0F,
                                   objectField(&AnimationGraphNodeUVE::valueY, node.parameterY), 1U);
                        next = 2U;
                    }
                    ImGui::SetCursorScreenPos(rowAt(next));
                    ImGui::EndDisabled();
                    if (ImGui::Button("Open Editor", ImVec2{rowWidth, 0.0F})) {
                        view.focus = objectId;
                    }
                    ImGui::BeginDisabled(!writable);
                    break;
                }
                case Kind::StateMachine:
                    ImGui::SetCursorScreenPos(rowAt(0U));
                    ImGui::EndDisabled();
                    if (ImGui::Button("Open States", ImVec2{rowWidth, 0.0F})) {
                        view.focus = objectId;
                    }
                    ImGui::BeginDisabled(!writable);
                    break;
                case Kind::Output:
                case Kind::OneShot:
                    break;
            }
            ImGui::EndDisabled();
            ImGui::PopStyleVar();
            ImGui::PopID();
        } else if (inlineRows > 0U) {
            const std::string summary = ObjectSummaryUVE(node);
            draw->AddText(font, fontSize, ImVec2{lo.x + 8.0F * view.zoom, lo.y + header + 3.0F * view.zoom}, kTextDimUVE,
                          summary.c_str());
        } else if (node.inputs.empty()) {
            const std::string summary = node.kind == Kind::Output ? std::string{} : ObjectSummaryUVE(node);
            draw->AddText(font, fontSize, ImVec2{lo.x + 8.0F * view.zoom, lo.y + header + 3.0F * view.zoom}, kTextDimUVE,
                          summary.c_str());
        } else if (view.zoom >= 0.8F) {
            const std::string summary = ObjectSummaryUVE(node);
            const float width = font->CalcTextSizeA(fontSize * 0.85F, FLT_MAX, 0.0F, summary.c_str()).x;
            draw->AddText(font, fontSize * 0.85F, ImVec2{hi.x - width - 8.0F * view.zoom, lo.y + header + 3.0F * view.zoom},
                          IM_COL32(150, 156, 166, 170), summary.c_str());
        }
        if (node.kind != Kind::Output) {
            const ImVec2 pin = outputPin(node);
            draw->AddCircleFilled(pin, kPinRadiusUVE * view.zoom,
                                  node.id == hoveredOutput || node.id == view.wireFrom ? kSelectedUVE : kWireUVE);
            draw->AddCircle(pin, kPinRadiusUVE * view.zoom, kBorderUVE);
        }
    }

    // ---- Interaction ----------------------------------------------------------------------------

    // Zoom about the cursor; pan with the middle button or Space + drag.
    if (hovered && io.MouseWheel != 0.0F) {
        const ImVec2 anchor = toCanvas(mouse);
        view.zoom = std::clamp(view.zoom * std::pow(1.12F, io.MouseWheel), kMinimumZoomUVE, kMaximumZoomUVE);
        view.panX = anchor.x - (mouse.x - origin.x) / view.zoom;
        view.panY = anchor.y - (mouse.y - origin.y) / view.zoom;
    }
    const bool spaceHeld = ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsWindowFocused();
    if (ImGui::IsItemActive() &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0F) || (spaceHeld && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0F)))) {
        view.panX -= io.MouseDelta.x / view.zoom;
        view.panY -= io.MouseDelta.y / view.zoom;
    }

    if (writable && hovered && !spaceHeld && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hoveredOutput != 0U) {
            view.wireFrom = hoveredOutput;
        } else if (hoveredSlot.has_value()) {
            // Grabbing a wired input picks the wire up from its source, to re-route or drop it.
            const AnimationGraphNodeUVE& target = nodes[indexById.at(hoveredSlot->node)];
            const std::uint32_t source = target.inputs[hoveredSlot->slot];
            if (source != 0U) {
                view.wireFrom = source;
                const SlotHitUVE hit = *hoveredSlot;
                edit = [hit](Scene::AnimationGraphComponentUVE& t) {
                    static_cast<void>(DisconnectAnimationGraphInputUVE(t.nodes, hit.node, hit.slot));
                };
            }
        } else if (hoveredObject != 0U && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                   (nodes[indexById.at(hoveredObject)].kind == Kind::BlendSpace1D ||
                    nodes[indexById.at(hoveredObject)].kind == Kind::BlendSpace2D ||
                    nodes[indexById.at(hoveredObject)].kind == Kind::StateMachine)) {
            view.focus = hoveredObject;
        } else if (hoveredObject != 0U) {
            const bool selected = std::ranges::find(view.selected, hoveredObject) != view.selected.end();
            if (io.KeyCtrl || io.KeyShift) {
                if (selected) {
                    std::erase(view.selected, hoveredObject);
                } else {
                    view.selected.push_back(hoveredObject);
                }
            } else if (!selected) {
                view.selected = {hoveredObject};
            }
            view.draggingObjects = std::ranges::find(view.selected, hoveredObject) != view.selected.end();
            view.dragBefore = nodes;
        } else {
            if (!io.KeyCtrl && !io.KeyShift) {
                view.selected.clear();
            }
            view.boxSelecting = true;
            view.boxFromX = mouse.x;
            view.boxFromY = mouse.y;
        }
    }

    // Dragging nodes moves them live without history; release records the whole move once.
    if (view.draggingObjects) {
        auto& live = entityManager.GetComponentUVE<Scene::AnimationGraphComponentUVE>(tree);
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0F);
            if (live.nodes.size() == view.dragBefore.size()) {
                for (std::size_t index = 0U; index < live.nodes.size(); ++index) {
                    if (std::ranges::find(view.selected, live.nodes[index].id) != view.selected.end()) {
                        live.nodes[index].position.x = view.dragBefore[index].position.x + delta.x / view.zoom;
                        live.nodes[index].position.y = view.dragBefore[index].position.y + delta.y / view.zoom;
                    }
                }
            }
        } else {
            view.draggingObjects = false;
            std::vector<AnimationGraphNodeUVE> moved = live.nodes;
            live.nodes = view.dragBefore;
            view.dragBefore.clear();
            edit = [moved = std::move(moved)](Scene::AnimationGraphComponentUVE& t) { t.nodes = moved; };
        }
    }

    // A wire in hand follows the mouse; released on a slot it connects.
    if (view.wireFrom != 0U) {
        if (const auto it = indexById.find(view.wireFrom); it != indexById.end()) {
            ImU32 colour = kSelectedUVE;
            if (hoveredSlot.has_value()) {
                colour = CanConnectAnimationGraphNodesUVE(nodes, hoveredSlot->node, hoveredSlot->slot, view.wireFrom)
                             ? kWireGoodUVE
                             : kWireBadUVE;
            }
            draw->PushClipRect(origin, ImVec2{origin.x + size.x, origin.y + size.y}, true);
            wireCurve(outputPin(nodes[it->second]), mouse, colour, 2.4F);
            draw->PopClipRect();
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const std::uint32_t source = view.wireFrom;
            view.wireFrom = 0U;
            if (hoveredSlot.has_value()) {
                const SlotHitUVE hit = *hoveredSlot;
                if (CanConnectAnimationGraphNodesUVE(nodes, hit.node, hit.slot, source)) {
                    auto previous = std::move(edit);
                    edit = [hit, source, previous](Scene::AnimationGraphComponentUVE& t) {
                        if (previous.has_value()) {
                            (*previous)(t);
                        }
                        static_cast<void>(ConnectAnimationGraphNodesUVE(t.nodes, hit.node, hit.slot, source));
                    };
                } else {
                    view.status = "That wire would loop back on itself.";
                }
            } else if (hovered && hoveredObject == 0U) {
                // Dropped on empty canvas: offer a node to plug it into, right there.
                view.addAtX = toCanvas(mouse).x;
                view.addAtY = toCanvas(mouse).y;
                view.addSearch.clear();
                view.wireFrom = 0U;
                ImGui::OpenPopup("##graph-add");
            }
        }
    }

    // Selection box.
    if (view.boxSelecting) {
        const ImVec2 a{std::min(view.boxFromX, mouse.x), std::min(view.boxFromY, mouse.y)};
        const ImVec2 b{std::max(view.boxFromX, mouse.x), std::max(view.boxFromY, mouse.y)};
        draw->PushClipRect(origin, ImVec2{origin.x + size.x, origin.y + size.y}, true);
        draw->AddRectFilled(a, b, IM_COL32(236, 170, 72, 30));
        draw->AddRect(a, b, IM_COL32(236, 170, 72, 160));
        draw->PopClipRect();
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            view.boxSelecting = false;
            for (const AnimationGraphNodeUVE& node : nodes) {
                const ImVec2 lo = objectMin(node);
                const ImVec2 hi = objectMax(node);
                if (lo.x < b.x && hi.x > a.x && lo.y < b.y && hi.y > a.y &&
                    std::ranges::find(view.selected, node.id) == view.selected.end()) {
                    view.selected.push_back(node.id);
                }
            }
        }
    }

    // Right-click: a node's menu, or Add Object where the click was.
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).x == 0.0F) {
        if (hoveredObject != 0U) {
            if (std::ranges::find(view.selected, hoveredObject) == view.selected.end()) {
                view.selected = {hoveredObject};
            }
            ImGui::OpenPopup("##graph-node");
        } else {
            view.addAtX = toCanvas(mouse).x;
            view.addAtY = toCanvas(mouse).y;
            view.addSearch.clear();
            ImGui::OpenPopup("##graph-add");
        }
    }
    draw->PopClipRect();

    // The Add Object menu opens up and to the left when the click was near the bottom or right edge,
    // so it always fits inside the dock.
    const auto placePopup = [&](const ImVec2 popupSize) {
        ImVec2 at = mouse;
        if (at.y + popupSize.y > origin.y + size.y) {
            at.y -= popupSize.y;
        }
        if (at.x + popupSize.x > origin.x + size.x) {
            at.x -= popupSize.x;
        }
        ImGui::SetNextWindowPos(at, ImGuiCond_Appearing);
    };
    placePopup(ImVec2{240.0F, 280.0F});
    if (ImGui::BeginPopup("##graph-add")) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        std::array<char, 64> search{};
        std::strncpy(search.data(), view.addSearch.c_str(), search.size() - 1U);
        ImGui::SetNextItemWidth(220.0F);
        ImGui::InputTextWithHint("##search", "Add node...", search.data(), search.size());
        view.addSearch = search.data();
        const std::string needle = LowerUVE(view.addSearch);
        std::optional<Kind> picked;
        std::optional<Kind> first;
        for (const Kind kind : kAddableKindsUVE) {
            const std::string haystack =
                LowerUVE(std::string{AnimationGraphKindLabelUVE(kind)} + " " + AnimationGraphKindHelpUVE(kind));
            if (!needle.empty() && haystack.find(needle) == std::string::npos) {
                continue;
            }
            first = first.value_or(kind);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(KindColourUVE(kind) | IM_COL32(60, 60, 60, 0)));
            ImGui::Bullet();
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Selectable(AnimationGraphKindLabelUVE(kind))) {
                picked = kind;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", AnimationGraphKindHelpUVE(kind));
            }
        }
        if (!first.has_value()) {
            ImGui::TextDisabled("No node matches.");
        }
        if (first.has_value() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            picked = first;
        }
        if (picked.has_value()) {
            const Kind kind = *picked;
            const Math::Vector2UVE at{view.addAtX, view.addAtY};
            edit = [kind, at, this](Scene::AnimationGraphComponentUVE& t) {
                const std::uint32_t id = AddAnimationGraphNodeUVE(t.nodes, kind, at);
                if (id != 0U) {
                    m_animGraph.selected = {id};
                }
            };
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    const auto deleteSelected = [&]() {
        const std::vector<std::uint32_t> ids = view.selected;
        edit = [ids](Scene::AnimationGraphComponentUVE& t) { static_cast<void>(DeleteAnimationGraphNodesUVE(t.nodes, ids)); };
    };
    const auto duplicateSelected = [&]() {
        const std::vector<std::uint32_t> ids = view.selected;
        edit = [ids, this](Scene::AnimationGraphComponentUVE& t) {
            m_animGraph.selected = DuplicateAnimationGraphNodesUVE(t.nodes, ids, Math::Vector2UVE{32.0F, 32.0F});
        };
    };
    placePopup(ImVec2{200.0F, 120.0F});
    if (ImGui::BeginPopup("##graph-node")) {
        const bool onlyOutput = std::ranges::all_of(view.selected, [&](const std::uint32_t id) {
            const auto it = indexById.find(id);
            return it == indexById.end() || nodes[it->second].kind == Kind::Output;
        });
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, writable && !onlyOutput)) {
            duplicateSelected();
        }
        if (ImGui::MenuItem("Disconnect Inputs", nullptr, false, writable)) {
            const std::vector<std::uint32_t> ids = view.selected;
            edit = [ids](Scene::AnimationGraphComponentUVE& t) {
                for (AnimationGraphNodeUVE& node : t.nodes) {
                    if (std::ranges::find(ids, node.id) != ids.end()) {
                        std::ranges::fill(node.inputs, 0U);
                    }
                }
            };
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del", false, writable && !onlyOutput)) {
            deleteSelected();
        }
        if (onlyOutput && ImGui::IsWindowHovered()) {
            ImGui::SetTooltip("The Output stays: it is what the target shows.");
        }
        ImGui::EndPopup();
    }

    // Keys while the canvas has focus; they are the graph's, not the editor's.
    const bool canvasFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !io.WantTextInput;
    m_timelineOwnsKeys = canvasFocused;
    if (canvasFocused) {
        if (writable && !view.selected.empty() &&
            (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
            deleteSelected();
        }
        if (writable && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && !view.selected.empty()) {
            duplicateSelected();
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
            view.selected.clear();
            for (const AnimationGraphNodeUVE& node : nodes) {
                view.selected.push_back(node.id);
            }
        }
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) {
            frameAll();
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            static_cast<void>(io.KeyShift ? RedoUVE() : UndoUVE());
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            static_cast<void>(RedoUVE());
        }
    }
    ImGui::EndChild();
    } // graph

    // ---- Side strip: parameters and the selected node ---------------------------------------------
    ImGui::SameLine();
    ImGui::BeginChild("##graph-side", ImVec2{0.0F, graphHeight}, true);
    ImGui::BeginDisabled(!writable);
    ImGui::TextUnformatted("Parameters");
    ImGui::SameLine();
    if (ImGui::SmallButton("+##param") && component.parameters.size() < Scene::kMaximumAnimationParametersUVE) {
        edit = [](Scene::AnimationGraphComponentUVE& t) {
            std::string name = "param";
            for (int suffix = 1; std::ranges::any_of(t.parameters, [&name](const AnimationParameterUVE& p) {
                     return p.name == name;
                 });
                 ++suffix) {
                name = "param" + std::to_string(suffix);
            }
            t.parameters.push_back(AnimationParameterUVE{name, AnimationParameterTypeUVE::Float, 0.0F});
        };
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Add a parameter for nodes and transitions to read.");
    }
    if (component.parameters.empty()) {
        ImGui::TextDisabled("None yet.");
    }
    for (std::size_t index = 0U; index < component.parameters.size(); ++index) {
        const AnimationParameterUVE& parameter = component.parameters[index];
        ImGui::PushID(static_cast<int>(index));
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(width * 0.42F);
        std::string renamed;
        if (EditNameUVE("##name", parameter.name, renamed) && !renamed.empty()) {
            const std::string from = parameter.name;
            edit = [index, from, renamed](Scene::AnimationGraphComponentUVE& t) {
                t.parameters[index].name = renamed;
                for (AnimationGraphNodeUVE& node : t.nodes) {
                    for (std::string* const reads : {&node.parameter, &node.parameterY}) {
                        if (*reads == from) {
                            *reads = renamed;
                        }
                    }
                    for (Scene::AnimationGraphTransitionUVE& transition : node.transitions) {
                        for (Scene::AnimationTransitionConditionUVE& test : transition.conditions) {
                            if (test.parameter == from) {
                                test.parameter = renamed;
                            }
                        }
                    }
                }
            };
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(width * 0.28F);
        int type = static_cast<int>(parameter.type);
        if (ImGui::Combo("##type", &type, "Float\0Bool\0Trigger\0")) {
            edit = [index, type](Scene::AnimationGraphComponentUVE& t) {
                t.parameters[index].type = static_cast<AnimationParameterTypeUVE>(type);
                t.parameters[index].value = 0.0F;
            };
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - style.ItemSpacing.x);
        float value = parameter.value;
        if (parameter.type == AnimationParameterTypeUVE::Float) {
            ImGui::DragFloat("##value", &value, 0.01F, 0.0F, 0.0F, "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                edit = [index, value](Scene::AnimationGraphComponentUVE& t) { t.parameters[index].value = value; };
            }
        } else if (parameter.type == AnimationParameterTypeUVE::Trigger) {
            // A trigger is an event, not a setting: fire it into the running preview only.
            ImGui::BeginDisabled(!view.previewing);
            if (ImGui::SmallButton(value >= 0.5F ? "Armed" : "Fire")) {
                entityManager.GetComponentUVE<Scene::AnimationGraphComponentUVE>(tree).parameters[index].value = 1.0F;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Fire it in the preview; the first transition or node that reads it uses it up.");
            }
        } else {
            bool on = value >= 0.5F;
            if (ImGui::Checkbox("##value", &on)) {
                edit = [index, on](Scene::AnimationGraphComponentUVE& t) { t.parameters[index].value = on ? 1.0F : 0.0F; };
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            edit = [index](Scene::AnimationGraphComponentUVE& t) {
                t.parameters.erase(t.parameters.begin() + static_cast<std::ptrdiff_t>(index));
            };
        }
        // A Float's last few seconds while the preview runs: what drove the graph just now.
        if (parameter.type == AnimationParameterTypeUVE::Float && view.previewing) {
            const auto found = view.parameterHistory.find(parameter.name);
            if (found != view.parameterHistory.end() && found->second.size() > 1U) {
                const std::vector<float>& samples = found->second;
                const auto [lowest, highest] = std::ranges::minmax_element(samples);
                const float low = *lowest;
                const float span = std::max(*highest - low, 1e-3F);
                const ImVec2 at = ImGui::GetCursorScreenPos();
                const float sparkWidth = ImGui::GetContentRegionAvail().x;
                constexpr float kSparkHeightUVE = 14.0F;
                ImDrawList* const spark = ImGui::GetWindowDrawList();
                spark->AddRectFilled(at, ImVec2{at.x + sparkWidth, at.y + kSparkHeightUVE}, IM_COL32(255, 255, 255, 8), 2.0F);
                for (std::size_t sample = 1U; sample < samples.size(); ++sample) {
                    const auto x = [&](const std::size_t i) {
                        return at.x + sparkWidth * static_cast<float>(i) / static_cast<float>(samples.size() - 1U);
                    };
                    const auto y = [&](const float v) { return at.y + kSparkHeightUVE - 2.0F - (v - low) / span * (kSparkHeightUVE - 4.0F); };
                    spark->AddLine(ImVec2{x(sample - 1U), y(samples[sample - 1U])}, ImVec2{x(sample), y(samples[sample])},
                                   kActiveUVE, 1.2F);
                }
                ImGui::Dummy(ImVec2{sparkWidth, kSparkHeightUVE});
            }
        }
        ImGui::PopID();
    }

    // ---- What the preview did: state changes and clip events, newest first --------------------------
    if (view.previewing && ImGui::CollapsingHeader("Log", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("Clear").x - ImGui::GetStyle().FramePadding.x * 2.0F);
        if (ImGui::SmallButton("Clear")) {
            view.previewLog.clear();
        }
        ImGui::BeginChild("##preview-log", ImVec2{0.0F, 84.0F}, false);
        if (view.previewLog.empty()) {
            ImGui::TextDisabled("State changes and clip events show here.");
        }
        for (auto line = view.previewLog.rbegin(); line != view.previewLog.rend(); ++line) {
            ImGui::TextDisabled("%6.2fs", line->at);
            ImGui::SameLine();
            ImGui::TextColored(line->stateChange ? ImVec4{0.43F, 0.82F, 0.55F, 1.0F} : ImVec4{0.93F, 0.67F, 0.28F, 1.0F}, "%s",
                               line->text.c_str());
        }
        ImGui::EndChild();
    }

    ImGui::Separator();
    if (focusedMachine) {
        DrawStateMachineSelectionUVE(tree, focusIt->second, edit);
    } else if (view.selected.size() != 1U || !indexById.contains(view.selected.front())) {
        ImGui::TextDisabled(view.selected.empty() ? "Select a node to edit it." : "%zu nodes selected.",
                            view.selected.size());
    } else {
        const std::size_t objectIndex = indexById.at(view.selected.front());
        const AnimationGraphNodeUVE& node = nodes[objectIndex];
        const std::uint32_t id = node.id;
        // The kind, with what it does on hover: the strip's height goes to the settings.
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(KindColourUVE(node.kind) | IM_COL32(50, 50, 50, 0)), "%s",
                           AnimationGraphKindLabelUVE(node.kind));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", AnimationGraphKindHelpUVE(node.kind));
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", AnimationGraphKindHelpUVE(node.kind));
        }
        const auto editObject = [&edit, id](std::function<void(AnimationGraphNodeUVE&)> change) {
            edit = [id, change = std::move(change)](Scene::AnimationGraphComponentUVE& t) {
                const auto it = std::ranges::find(t.nodes, id, &AnimationGraphNodeUVE::id);
                if (it != t.nodes.end()) {
                    change(*it);
                }
            };
        };
        const auto row = [](const char* const label) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", label);
            ImGui::SameLine(84.0F);
            ImGui::SetNextItemWidth(-FLT_MIN);
        };
        const auto dragRow = [&](const char* const label, const float current, const float speed, const float minimum,
                                 const float maximum, float AnimationGraphNodeUVE::*field) {
            row(label);
            float value = current;
            const std::string widgetId = std::string{"##"} + label;
            ImGui::DragFloat(widgetId.c_str(), &value, speed, minimum, maximum, "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                editObject([field, value](AnimationGraphNodeUVE& n) { n.*field = value; });
            }
        };
        const auto parameterRow = [&](const char* const label, std::initializer_list<AnimationParameterTypeUVE> types,
                                      const char* const none) {
            row(label);
            std::string name = node.parameter;
            if (PickParameterUVE("##parameter", component.parameters, types, none, name)) {
                editObject([name](AnimationGraphNodeUVE& n) { n.parameter = name; });
            }
        };
        const auto syncRow = [&]() {
            row("Sync");
            bool sync = node.sync;
            if (ImGui::Checkbox("##sync", &sync)) {
                editObject([sync](AnimationGraphNodeUVE& n) { n.sync = sync; });
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Keep the inputs in step: the heaviest leads and the others play at its phase,\n"
                                  "so a walk and a run blend with their feet together.");
            }
        };
        const auto blendModeRows = [&]() {
            row("Blend");
            static constexpr std::array<const char*, 3> kModes{"Blend", "Nearest", "Nearest, in step"};
            const auto mode = std::min(static_cast<std::size_t>(node.blendMode), kModes.size() - 1U);
            if (ImGui::BeginCombo("##blend-mode", kModes[mode])) {
                for (std::size_t option = 0U; option < kModes.size(); ++option) {
                    if (ImGui::Selectable(kModes[option], option == mode)) {
                        const auto picked = static_cast<Scene::AnimationBlendModeUVE>(option);
                        editObject([picked](AnimationGraphNodeUVE& n) { n.blendMode = picked; });
                    }
                }
                ImGui::EndCombo();
            }
            dragRow("Smoothing", node.smoothingSeconds, 0.005F, 0.0F, 5.0F, &AnimationGraphNodeUVE::smoothingSeconds);
            if (node.blendMode != Scene::AnimationBlendModeUVE::Blend) {
                dragRow("Switch", node.fadeSeconds, 0.005F, 0.0F, 5.0F, &AnimationGraphNodeUVE::fadeSeconds);
            }
        };
        row("Name");
        std::string renamed;
        if (EditNameUVE("##node-name", node.name, renamed)) {
            editObject([renamed](AnimationGraphNodeUVE& n) { n.name = renamed; });
        }
        switch (node.kind) {
            case Kind::Clip: {
                row("Clip");
                if (const std::optional<Asset::AssetGuidUVE> picked = DrawAssetPickerUVE("##clip", node.clip, ".uvanim")) {
                    const Asset::AssetGuidUVE guid = *picked;
                    editObject([guid](AnimationGraphNodeUVE& n) { n.clip = guid; });
                }
                row("Loop");
                bool loop = node.loop;
                if (ImGui::Checkbox("##loop", &loop)) {
                    editObject([loop](AnimationGraphNodeUVE& n) { n.loop = loop; });
                }
                dragRow("Speed", node.speed, 0.01F, -100.0F, 100.0F, &AnimationGraphNodeUVE::speed);
                break;
            }
            case Kind::Blend2:
            case Kind::Additive:
                parameterRow("Weight", {AnimationParameterTypeUVE::Float}, "(fixed)");
                if (node.parameter.empty()) {
                    dragRow("Value", node.value, 0.01F, 0.0F, 1.0F, &AnimationGraphNodeUVE::value);
                }
                syncRow();
                break;
            case Kind::BlendSpace1D:
                parameterRow("Position", {AnimationParameterTypeUVE::Float}, "(fixed)");
                if (node.parameter.empty()) {
                    dragRow("Value", node.value, 0.01F, -1000.0F, 1000.0F, &AnimationGraphNodeUVE::value);
                }
                blendModeRows();
                syncRow();
                if (ImGui::Button("Open Editor##1d", ImVec2{-FLT_MIN, 0.0F})) {
                    view.focus = id;
                }
                break;
            case Kind::OneShot:
                parameterRow("Fire On", {AnimationParameterTypeUVE::Trigger, AnimationParameterTypeUVE::Bool}, "(never)");
                dragRow("Fade", node.fadeSeconds, 0.01F, 0.0F, 10.0F, &AnimationGraphNodeUVE::fadeSeconds);
                break;
            case Kind::TimeScale:
                parameterRow("Rate", {AnimationParameterTypeUVE::Float}, "(fixed)");
                if (node.parameter.empty()) {
                    dragRow("Value", node.speed, 0.01F, -100.0F, 100.0F, &AnimationGraphNodeUVE::speed);
                }
                break;
            case Kind::BlendSpace2D: {
                parameterRow("X", {AnimationParameterTypeUVE::Float}, "(fixed)");
                row("Y");
                {
                    std::string name = node.parameterY;
                    if (PickParameterUVE("##parameter-y", component.parameters, {AnimationParameterTypeUVE::Float}, "(fixed)", name)) {
                        editObject([name](AnimationGraphNodeUVE& n) { n.parameterY = name; });
                    }
                }
                blendModeRows();
                syncRow();
                if (ImGui::Button("Open Editor", ImVec2{-FLT_MIN, 0.0F})) {
                    view.focus = id;
                }
                break;
            }
            case Kind::Select:
                parameterRow("Pick", {AnimationParameterTypeUVE::Float, AnimationParameterTypeUVE::Bool}, "(fixed)");
                if (node.parameter.empty()) {
                    row("Option");
                    int option = static_cast<int>(std::lround(node.value));
                    if (ImGui::SliderInt("##option", &option, 0, static_cast<int>(node.inputs.size()) - 1)) {
                        editObject([option](AnimationGraphNodeUVE& n) { n.value = static_cast<float>(option); });
                    }
                }
                dragRow("Fade", node.fadeSeconds, 0.01F, 0.0F, 10.0F, &AnimationGraphNodeUVE::fadeSeconds);
                row("Restart");
                {
                    bool restart = node.restart;
                    if (ImGui::Checkbox("##restart", &restart)) {
                        editObject([restart](AnimationGraphNodeUVE& n) { n.restart = restart; });
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Start the picked input from its beginning each time it is picked.");
                    }
                }
                break;
            case Kind::LayeredBlend: {
                parameterRow("Weight", {AnimationParameterTypeUVE::Float}, "(fixed)");
                if (node.parameter.empty()) {
                    dragRow("Value", node.value, 0.01F, 0.0F, 1.0F, &AnimationGraphNodeUVE::value);
                }
                syncRow();
                ImGui::TextDisabled("%s", node.bones.empty() ? "Whole body - add a bone to limit it:"
                                                             : "Only these branches:");
                for (std::size_t bone = 0U; bone < node.bones.size(); ++bone) {
                    ImGui::PushID(static_cast<int>(bone) + 9000);
                    if (ImGui::SmallButton("x")) {
                        const std::string name = node.bones[bone];
                        editObject([name](AnimationGraphNodeUVE& n) { std::erase(n.bones, name); });
                    }
                    ImGui::SameLine();
                    ImGui::TextUnformatted(node.bones[bone].c_str());
                    ImGui::PopID();
                }
                // The skeleton's bones, searchable: a rig has a hundred or more.
                std::vector<std::string> boneNames;
                if (skeletonEntity != Scene::kInvalidEntityUVE) {
                    for (const Scene::SkeletonBoneUVE& bone :
                         entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(skeletonEntity).bones) {
                        boneNames.push_back(bone.name);
                    }
                }
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##add-bone", "+ Bone branch", ImGuiComboFlags_HeightLarge)) {
                    if (ImGui::IsWindowAppearing()) {
                        view.boneSearch.clear();
                        ImGui::SetKeyboardFocusHere();
                    }
                    std::array<char, 64> search{};
                    std::strncpy(search.data(), view.boneSearch.c_str(), search.size() - 1U);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputTextWithHint("##bone-search", "Search bones...", search.data(), search.size());
                    view.boneSearch = search.data();
                    const std::string needle = LowerUVE(view.boneSearch);
                    if (boneNames.empty()) {
                        ImGui::TextDisabled("No skeleton under the tree's target.");
                    }
                    for (const std::string& name : boneNames) {
                        if ((!needle.empty() && LowerUVE(name).find(needle) == std::string::npos) ||
                            std::ranges::find(node.bones, name) != node.bones.end()) {
                            continue;
                        }
                        if (ImGui::Selectable(name.c_str())) {
                            editObject([name](AnimationGraphNodeUVE& n) {
                                if (n.bones.size() < Scene::kMaximumAnimationLayerBonesUVE) {
                                    n.bones.push_back(name);
                                }
                            });
                        }
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            case Kind::TimeSeek:
                parameterRow("On", {AnimationParameterTypeUVE::Trigger, AnimationParameterTypeUVE::Bool}, "(never)");
                dragRow("To (s)", node.value, 0.01F, 0.0F, 3600.0F, &AnimationGraphNodeUVE::value);
                break;
            case Kind::StateMachine: {
                row("Entry");
                const std::string entry = AnimationGraphSlotLabelUVE(node.kind, node.entryState);
                if (ImGui::BeginCombo("##entry", entry.c_str())) {
                    for (std::uint32_t slot = 0U; slot < node.inputs.size(); ++slot) {
                        if (ImGui::Selectable(AnimationGraphSlotLabelUVE(node.kind, slot).c_str(), slot == node.entryState)) {
                            editObject([slot](AnimationGraphNodeUVE& n) { n.entryState = slot; });
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Button("Open States##machine", ImVec2{-FLT_MIN, 0.0F})) {
                    view.focus = id;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Its states and transitions, drawn and edited (or double-click the node).");
                }
                break;
            }
            case Kind::Output:
                ImGui::TextDisabled("Wire the pose the target shows into it.");
                break;
        }
        if (node.kind == Kind::Select || node.kind == Kind::StateMachine) {
            if (ImGui::Button(node.kind == Kind::StateMachine ? "+ State" : "+ Option")) {
                edit = [id](Scene::AnimationGraphComponentUVE& t) { static_cast<void>(AddAnimationGraphInputSlotUVE(t.nodes, id)); };
            }
            ImGui::SameLine();
            if (ImGui::Button("- Last") && node.inputs.size() > 1U) {
                const std::size_t last = node.inputs.size() - 1U;
                edit = [id, last](Scene::AnimationGraphComponentUVE& t) {
                    static_cast<void>(RemoveAnimationGraphInputSlotUVE(t.nodes, id, last));
                };
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::EndChild();

    // ---- Bottom bar: the graph's health on the left, the view on the right ------------------------
    const std::string problem = Scene::DescribeAnimationGraphProblemUVE(component);
    ImGui::AlignTextToFramePadding();
    if (!problem.empty()) {
        ImGui::TextColored(ImVec4{0.95F, 0.55F, 0.45F, 1.0F}, "%s", problem.c_str());
    } else if (!view.status.empty()) {
        ImGui::TextDisabled("%s", view.status.c_str());
    } else {
        const std::string name = entityManager.HasComponentUVE<Scene::NameComponentUVE>(tree)
                                     ? entityManager.GetComponentUVE<Scene::NameComponentUVE>(tree).name
                                     : std::string{"AnimationGraph"};
        ImGui::TextDisabled("%s  -  %zu nodes, %zu parameters%s%s", name.c_str(), nodes.size(), component.parameters.size(),
                            component.activeStates.empty() ? "" : "  -  ", component.activeStates.c_str());
    }
    const float right = ImGui::GetContentRegionMax().x;
    char zoomText[16];
    std::snprintf(zoomText, sizeof(zoomText), "%d%%", static_cast<int>(std::lround(view.zoom * 100.0F)));
    const float buttons = ImGui::CalcTextSize("Frame All").x + ImGui::CalcTextSize("100%").x + ImGui::CalcTextSize("200%").x +
                          style.FramePadding.x * 6.0F + style.ItemSpacing.x * 3.0F;
    const float previewWidth = ImGui::CalcTextSize("Preview Off").x + style.FramePadding.x * 2.0F + style.ItemSpacing.x;
    const float transportWidth = ImGui::CalcTextSize("Pause").x + ImGui::CalcTextSize("Step").x + 64.0F +
                                 style.FramePadding.x * 4.0F + style.ItemSpacing.x * 3.0F;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + style.ItemSpacing.x, right - buttons - previewWidth - transportWidth));
    // The preview's transport: hold it, move it a frame at a time, run it slower to see a blend.
    ImGui::BeginDisabled(!view.previewing || skeletonEntity == Scene::kInvalidEntityUVE);
    if (ImGui::Button(view.previewPaused ? "Play" : "Pause")) {
        view.previewPaused = !view.previewPaused;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!view.previewPaused);
    if (ImGui::Button("Step")) {
        view.previewStepOnce = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("One frame (1/60 s) while paused");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(64.0F);
    static constexpr std::array<float, 6> kRatesUVE{0.1F, 0.25F, 0.5F, 1.0F, 1.5F, 2.0F};
    char rateText[16];
    std::snprintf(rateText, sizeof(rateText), "%gx", static_cast<double>(view.previewRate));
    if (ImGui::BeginCombo("##rate", rateText)) {
        for (const float rate : kRatesUVE) {
            std::snprintf(rateText, sizeof(rateText), "%gx", static_cast<double>(rate));
            if (ImGui::Selectable(rateText, rate == view.previewRate)) {
                view.previewRate = rate;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Preview speed");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (skeletonEntity == Scene::kInvalidEntityUVE) {
        ImGui::BeginDisabled();
    }
    ImGui::PushStyleColor(ImGuiCol_Button, view.previewing && skeletonEntity != Scene::kInvalidEntityUVE
                                               ? ImVec4{0.20F, 0.42F, 0.28F, 1.0F}
                                               : style.Colors[ImGuiCol_Button]);
    if (ImGui::Button(view.previewing ? "Preview On" : "Preview Off")) {
        view.previewing = !view.previewing;
    }
    ImGui::PopStyleColor();
    if (skeletonEntity == Scene::kInvalidEntityUVE) {
        ImGui::EndDisabled();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(skeletonEntity == Scene::kInvalidEntityUVE
                              ? "No Skeleton3D under the tree's target to preview on."
                              : "Run the tree on the skeleton while this tab is open (pose only, never saved).");
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame All")) {
        view.focus = 0U;
        view.framed = false; // the canvas frames everything on its next draw
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Fit the whole graph (F)");
    }
    ImGui::SameLine();
    if (ImGui::Button(zoomText)) {
        view.zoom = 1.0F;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Zoom: wheel over the canvas. Click for 100%%.");
    }

    if (edit.has_value()) {
        view.status.clear();
        static_cast<void>(EditAnimationGraphUVE(tree, *edit));
    }
}

} // namespace UVE::Editor
