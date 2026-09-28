// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Entity Editor's Anim Graph tab: an AnimationTree as boxes and wires on a canvas. Nodes are
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
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/animation_mixer_component_uve.h"
#include "uve/component/animation_tree_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/nodes/3d/animation_tree_uve.h"
#include "uve/nodes/3d/skeleton_3d_uve.h"
#include "uve/object/type_metadata_uve.h"
#include "uve/scene/scene_component_metadata_uve.h"

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKindUVE;
using Scene::AnimationGraphNodeUVE;
using Scene::AnimationParameterTypeUVE;
using Scene::AnimationParameterUVE;

constexpr float kNodeWidthUVE = 176.0F;
constexpr float kHeaderHeightUVE = 24.0F;
constexpr float kSlotHeightUVE = 20.0F;
constexpr float kBodyPaddingUVE = 6.0F;
constexpr float kPinRadiusUVE = 5.0F;
constexpr float kSideStripWidthUVE = 250.0F;
constexpr float kMinimumZoomUVE = 0.3F;
constexpr float kMaximumZoomUVE = 2.0F;

constexpr ImU32 kCanvasUVE = IM_COL32(26, 29, 35, 255);
constexpr ImU32 kGridMinorUVE = IM_COL32(255, 255, 255, 10);
constexpr ImU32 kGridMajorUVE = IM_COL32(255, 255, 255, 22);
constexpr ImU32 kBodyUVE = IM_COL32(40, 44, 52, 245);
constexpr ImU32 kBorderUVE = IM_COL32(18, 20, 24, 255);
constexpr ImU32 kSelectedUVE = IM_COL32(236, 170, 72, 255);
constexpr ImU32 kWireUVE = IM_COL32(170, 178, 190, 220);
constexpr ImU32 kWireGoodUVE = IM_COL32(110, 210, 140, 255);
constexpr ImU32 kWireBadUVE = IM_COL32(230, 96, 86, 255);
constexpr ImU32 kTextUVE = IM_COL32(230, 232, 236, 255);
constexpr ImU32 kTextDimUVE = IM_COL32(150, 156, 166, 255);
constexpr ImU32 kActiveUVE = IM_COL32(110, 210, 140, 255);

/// A header colour per kind, so the graph reads at a glance: sources, mixers, control.
[[nodiscard]] ImU32 KindColourUVE(const Kind kind) noexcept {
    switch (kind) {
        case Kind::Output: return IM_COL32(170, 72, 72, 255);
        case Kind::Clip: return IM_COL32(58, 110, 170, 255);
        case Kind::Blend2:
        case Kind::BlendSpace1D: return IM_COL32(64, 138, 102, 255);
        case Kind::Additive: return IM_COL32(120, 100, 170, 255);
        case Kind::OneShot: return IM_COL32(176, 118, 52, 255);
        case Kind::TimeScale: return IM_COL32(96, 104, 118, 255);
        case Kind::StateMachine: return IM_COL32(150, 84, 136, 255);
    }
    return IM_COL32(96, 104, 118, 255);
}

constexpr std::array<Kind, 7> kAddableKindsUVE{Kind::Clip,     Kind::Blend2,    Kind::BlendSpace1D, Kind::Additive,
                                               Kind::OneShot, Kind::TimeScale, Kind::StateMachine};

[[nodiscard]] float NodeHeightUVE(const AnimationGraphNodeUVE& node) {
    const std::size_t rows = std::max<std::size_t>(node.inputs.size(), 1U);
    return kHeaderHeightUVE + static_cast<float>(rows) * kSlotHeightUVE + kBodyPaddingUVE;
}

[[nodiscard]] std::string LowerUVE(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// One line under a node's title: what it reads, so the graph explains itself without a click.
[[nodiscard]] std::string NodeSummaryUVE(const AnimationGraphNodeUVE& node) {
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

/// A combo over the parameters of the wanted types, with `none` meaning the fixed value.
bool PickParameterUVE(const char* const id, const std::vector<AnimationParameterUVE>& parameters,
                      const std::initializer_list<AnimationParameterTypeUVE> wanted, const char* const none,
                      std::string& inOut) {
    bool changed = false;
    if (ImGui::BeginCombo(id, inOut.empty() ? none : inOut.c_str())) {
        if (ImGui::Selectable(none, inOut.empty()) && !inOut.empty()) {
            inOut.clear();
            changed = true;
        }
        for (const AnimationParameterUVE& parameter : parameters) {
            if (std::find(wanted.begin(), wanted.end(), parameter.type) != wanted.end() &&
                ImGui::Selectable(parameter.name.c_str(), parameter.name == inOut) && parameter.name != inOut) {
                inOut = parameter.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace

void EditorUVE::StopAnimationGraphPreviewUVE() {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (m_animGraph.previewSkeleton != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(m_animGraph.previewSkeleton) &&
        entityManager.HasComponentUVE<Scene::Skeleton3DNodeComponentUVE>(m_animGraph.previewSkeleton)) {
        entityManager.GetComponentUVE<Scene::Skeleton3DNodeComponentUVE>(m_animGraph.previewSkeleton).pose.clear();
    }
    m_animGraph.previewSkeleton = Scene::kInvalidEntityUVE;
    if (m_animGraph.tree != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(m_animGraph.tree) &&
        entityManager.HasComponentUVE<Scene::AnimationTreeComponentUVE>(m_animGraph.tree)) {
        // Back to the start, so the next preview (and Play) begins from the entry state.
        entityManager.GetComponentUVE<Scene::AnimationTreeComponentUVE>(m_animGraph.tree).nodeStates.clear();
    }
}

bool EditorUVE::EditAnimationTreeUVE(const Scene::EntityUVE tree,
                                     const std::function<void(Scene::AnimationTreeComponentUVE&)>& change) {
    if (!IsAuthoringCommandAllowedUVE()) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.IsAliveUVE(tree) || !entityManager.HasComponentUVE<Scene::AnimationTreeComponentUVE>(tree)) {
        return false;
    }
    const Core::TypeMetadataEntryUVE* const entry = Scene::GetSceneComponentMetadataRegistryUVE().FindTypeByIndexUVE(
        std::type_index(typeid(Scene::AnimationTreeComponentUVE)));
    if (entry == nullptr || !entry->HasFactoryUVE()) {
        return false;
    }
    auto& component = entityManager.GetComponentUVE<Scene::AnimationTreeComponentUVE>(tree);
    const Scene::AnimationTreeComponentUVE original = component;
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
               entityManager.HasComponentUVE<Scene::AnimationTreeComponentUVE>(entity);
    };
    const std::vector<Scene::EntityUVE> entityNodes = CollectEntityEditorNodesUVE();
    const auto inEntity = [&entityNodes](const Scene::EntityUVE entity) {
        return std::ranges::find(entityNodes, entity) != entityNodes.end();
    };
    Scene::EntityUVE tree = Scene::kInvalidEntityUVE;
    if (isTree(m_selectedEntity) && inEntity(m_selectedEntity)) {
        tree = m_selectedEntity;
    } else if (isTree(view.tree) && inEntity(view.tree)) {
        tree = view.tree;
    } else {
        for (const Scene::EntityUVE node : entityNodes) {
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
        ImGui::TextDisabled("This entity has no AnimationTree.");
        return;
    }
    // ---- Preview: the tree runs on the entity's skeleton, the mixer's target or else the tree's
    // parent, searched down. Only the skeleton's runtime pose changes; nothing is saved.
    Scene::EntityUVE skeletonEntity = Scene::kInvalidEntityUVE;
    const Scene::AnimationMixerComponentUVE mixer = entityManager.HasComponentUVE<Scene::AnimationMixerComponentUVE>(tree)
                                                        ? entityManager.GetComponentUVE<Scene::AnimationMixerComponentUVE>(tree)
                                                        : Scene::AnimationMixerComponentUVE{};
    {
        Scene::EntityUVE root = mixer.target;
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
            if (entityManager.HasComponentUVE<Scene::Skeleton3DNodeComponentUVE>(queue[next])) {
                skeletonEntity = queue[next];
                break;
            }
            const std::vector<Scene::EntityUVE> children = sceneGraph.GetChildrenUVE(entityManager, queue[next]);
            queue.insert(queue.end(), children.begin(), children.end());
        }
    }
    if (view.previewing && skeletonEntity != Scene::kInvalidEntityUVE && !view.draggingNodes) {
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
        auto& live = entityManager.GetComponentUVE<Scene::AnimationTreeComponentUVE>(tree);
        auto& skeleton = entityManager.GetComponentUVE<Scene::Skeleton3DNodeComponentUVE>(skeletonEntity);
        Scene::AnimationMixerComponentUVE previewMixer = mixer;
        previewMixer.active = true;
        const float step = std::clamp(ImGui::GetIO().DeltaTime, 0.0F, 0.1F) * mixer.speedScale;
        static_cast<void>(Scene::StepSkeletalAnimationTreeUVE(live, clipFor, step, skeleton, previewMixer));
    } else if (!view.previewing && view.previewSkeleton != Scene::kInvalidEntityUVE) {
        StopAnimationGraphPreviewUVE();
    }
    const auto clipDuration = [this](const Asset::AssetGuidUVE guid) {
        const auto found = m_animGraph.clips.find(guid.value);
        return found != m_animGraph.clips.end() && found->second != nullptr ? found->second->durationSeconds : 0.0;
    };

    const Scene::AnimationTreeComponentUVE& component = entityManager.GetComponentUVE<Scene::AnimationTreeComponentUVE>(tree);
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

    // ---- Canvas -----------------------------------------------------------------------------------
    ImGui::BeginChild("##graph-canvas", ImVec2{canvasWidth, canvasHeight}, false,
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
            maxX = std::max(maxX, node.position.x + kNodeWidthUVE);
            maxY = std::max(maxY, node.position.y + NodeHeightUVE(node));
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
    std::unordered_map<std::uint32_t, std::size_t> indexById;
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        indexById.emplace(nodes[index].id, index);
    }
    const auto nodeMin = [&](const AnimationGraphNodeUVE& node) { return toScreen(node.position.x, node.position.y); };
    const auto nodeMax = [&](const AnimationGraphNodeUVE& node) {
        return toScreen(node.position.x + kNodeWidthUVE, node.position.y + NodeHeightUVE(node));
    };
    const auto outputPin = [&](const AnimationGraphNodeUVE& node) {
        return toScreen(node.position.x + kNodeWidthUVE, node.position.y + kHeaderHeightUVE * 0.5F);
    };
    const auto inputPin = [&](const AnimationGraphNodeUVE& node, const std::size_t slot) {
        return toScreen(node.position.x,
                        node.position.y + kHeaderHeightUVE + (static_cast<float>(slot) + 0.5F) * kSlotHeightUVE);
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
    std::uint32_t hoveredNode = 0U;
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
            const ImVec2 lo = nodeMin(node);
            const ImVec2 hi = nodeMax(node);
            if (mouse.x >= lo.x && mouse.x <= hi.x && mouse.y >= lo.y && mouse.y <= hi.y) {
                hoveredNode = node.id;
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

    // Nodes.
    const float fontScale = std::clamp(view.zoom, 0.6F, 1.4F);
    const float fontSize = ImGui::GetFontSize() * fontScale;
    ImFont* const font = ImGui::GetFont();
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const AnimationGraphNodeUVE& node = nodes[index];
        const ImVec2 lo = nodeMin(node);
        const ImVec2 hi = nodeMax(node);
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
        draw->AddRect(lo, hi, selected ? kSelectedUVE : (node.id == hoveredNode ? IM_COL32(110, 118, 132, 255) : kBorderUVE),
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
            if (node.kind == Kind::BlendSpace1D && slot < node.points.size()) {
                char at[32];
                std::snprintf(at, sizeof(at), "  @ %.2f", static_cast<double>(node.points[slot]));
                label += at;
            }
            draw->AddText(font, fontSize, ImVec2{pin.x + 10.0F * view.zoom, pin.y - fontSize * 0.5F},
                          activeState ? kActiveUVE : kTextDimUVE, label.c_str());
        }
        if (node.inputs.empty()) {
            const std::string summary = node.kind == Kind::Output ? std::string{} : NodeSummaryUVE(node);
            draw->AddText(font, fontSize, ImVec2{lo.x + 8.0F * view.zoom, lo.y + header + 3.0F * view.zoom}, kTextDimUVE,
                          summary.c_str());
        } else if (view.zoom >= 0.8F) {
            const std::string summary = NodeSummaryUVE(node);
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
    std::optional<std::function<void(Scene::AnimationTreeComponentUVE&)>> edit;

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
                edit = [hit](Scene::AnimationTreeComponentUVE& t) {
                    static_cast<void>(DisconnectAnimationGraphInputUVE(t.nodes, hit.node, hit.slot));
                };
            }
        } else if (hoveredNode != 0U) {
            const bool selected = std::ranges::find(view.selected, hoveredNode) != view.selected.end();
            if (io.KeyCtrl || io.KeyShift) {
                if (selected) {
                    std::erase(view.selected, hoveredNode);
                } else {
                    view.selected.push_back(hoveredNode);
                }
            } else if (!selected) {
                view.selected = {hoveredNode};
            }
            view.draggingNodes = std::ranges::find(view.selected, hoveredNode) != view.selected.end();
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
    if (view.draggingNodes) {
        auto& live = entityManager.GetComponentUVE<Scene::AnimationTreeComponentUVE>(tree);
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
            view.draggingNodes = false;
            std::vector<AnimationGraphNodeUVE> moved = live.nodes;
            live.nodes = view.dragBefore;
            view.dragBefore.clear();
            edit = [moved = std::move(moved)](Scene::AnimationTreeComponentUVE& t) { t.nodes = moved; };
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
                    edit = [hit, source, previous](Scene::AnimationTreeComponentUVE& t) {
                        if (previous.has_value()) {
                            (*previous)(t);
                        }
                        static_cast<void>(ConnectAnimationGraphNodesUVE(t.nodes, hit.node, hit.slot, source));
                    };
                } else {
                    view.status = "That wire would loop back on itself.";
                }
            } else if (hovered && hoveredNode == 0U) {
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
                const ImVec2 lo = nodeMin(node);
                const ImVec2 hi = nodeMax(node);
                if (lo.x < b.x && hi.x > a.x && lo.y < b.y && hi.y > a.y &&
                    std::ranges::find(view.selected, node.id) == view.selected.end()) {
                    view.selected.push_back(node.id);
                }
            }
        }
    }

    // Right-click: a node's menu, or Add Node where the click was.
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).x == 0.0F) {
        if (hoveredNode != 0U) {
            if (std::ranges::find(view.selected, hoveredNode) == view.selected.end()) {
                view.selected = {hoveredNode};
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

    // The Add Node menu opens up and to the left when the click was near the bottom or right edge,
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
            edit = [kind, at, this](Scene::AnimationTreeComponentUVE& t) {
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
        edit = [ids](Scene::AnimationTreeComponentUVE& t) { static_cast<void>(DeleteAnimationGraphNodesUVE(t.nodes, ids)); };
    };
    const auto duplicateSelected = [&]() {
        const std::vector<std::uint32_t> ids = view.selected;
        edit = [ids, this](Scene::AnimationTreeComponentUVE& t) {
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
            edit = [ids](Scene::AnimationTreeComponentUVE& t) {
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
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !io.WantTextInput;
    m_timelineOwnsKeys = focused;
    if (focused) {
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

    // ---- Side strip: parameters and the selected node ---------------------------------------------
    ImGui::SameLine();
    ImGui::BeginChild("##graph-side", ImVec2{0.0F, canvasHeight}, true);
    ImGui::BeginDisabled(!writable);
    ImGui::TextUnformatted("Parameters");
    ImGui::SameLine();
    if (ImGui::SmallButton("+##param") && component.parameters.size() < Scene::kMaximumAnimationParametersUVE) {
        edit = [](Scene::AnimationTreeComponentUVE& t) {
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
            edit = [index, from, renamed](Scene::AnimationTreeComponentUVE& t) {
                t.parameters[index].name = renamed;
                for (AnimationGraphNodeUVE& node : t.nodes) {
                    if (node.parameter == from) {
                        node.parameter = renamed;
                    }
                    for (Scene::AnimationTransitionUVE& transition : node.transitions) {
                        if (transition.parameter == from) {
                            transition.parameter = renamed;
                        }
                    }
                }
            };
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(width * 0.28F);
        int type = static_cast<int>(parameter.type);
        if (ImGui::Combo("##type", &type, "Float\0Bool\0Trigger\0")) {
            edit = [index, type](Scene::AnimationTreeComponentUVE& t) {
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
                edit = [index, value](Scene::AnimationTreeComponentUVE& t) { t.parameters[index].value = value; };
            }
        } else {
            bool on = value >= 0.5F;
            if (ImGui::Checkbox("##value", &on)) {
                edit = [index, on](Scene::AnimationTreeComponentUVE& t) { t.parameters[index].value = on ? 1.0F : 0.0F; };
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            edit = [index](Scene::AnimationTreeComponentUVE& t) {
                t.parameters.erase(t.parameters.begin() + static_cast<std::ptrdiff_t>(index));
            };
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (view.selected.size() != 1U || !indexById.contains(view.selected.front())) {
        ImGui::TextDisabled(view.selected.empty() ? "Select a node to edit it." : "%zu nodes selected.",
                            view.selected.size());
    } else {
        const std::size_t nodeIndex = indexById.at(view.selected.front());
        const AnimationGraphNodeUVE& node = nodes[nodeIndex];
        const std::uint32_t id = node.id;
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(KindColourUVE(node.kind) | IM_COL32(50, 50, 50, 0)), "%s",
                           AnimationGraphKindLabelUVE(node.kind));
        ImGui::TextWrapped("%s", AnimationGraphKindHelpUVE(node.kind));
        const auto editNode = [&edit, id](std::function<void(AnimationGraphNodeUVE&)> change) {
            edit = [id, change = std::move(change)](Scene::AnimationTreeComponentUVE& t) {
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
                editNode([field, value](AnimationGraphNodeUVE& n) { n.*field = value; });
            }
        };
        const auto parameterRow = [&](const char* const label, std::initializer_list<AnimationParameterTypeUVE> types,
                                      const char* const none) {
            row(label);
            std::string name = node.parameter;
            if (PickParameterUVE("##parameter", component.parameters, types, none, name)) {
                editNode([name](AnimationGraphNodeUVE& n) { n.parameter = name; });
            }
        };
        const auto syncRow = [&]() {
            row("Sync");
            bool sync = node.sync;
            if (ImGui::Checkbox("##sync", &sync)) {
                editNode([sync](AnimationGraphNodeUVE& n) { n.sync = sync; });
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Keep the inputs in step: the heaviest leads and the others play at its phase,\n"
                                  "so a walk and a run blend with their feet together.");
            }
        };
        row("Name");
        std::string renamed;
        if (EditNameUVE("##node-name", node.name, renamed)) {
            editNode([renamed](AnimationGraphNodeUVE& n) { n.name = renamed; });
        }
        switch (node.kind) {
            case Kind::Clip: {
                row("Clip");
                if (const std::optional<Asset::AssetGuidUVE> picked = DrawAssetPickerUVE("##clip", node.clip, ".uvanim")) {
                    const Asset::AssetGuidUVE guid = *picked;
                    editNode([guid](AnimationGraphNodeUVE& n) { n.clip = guid; });
                }
                row("Loop");
                bool loop = node.loop;
                if (ImGui::Checkbox("##loop", &loop)) {
                    editNode([loop](AnimationGraphNodeUVE& n) { n.loop = loop; });
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
                syncRow();
                for (std::size_t slot = 0U; slot < node.points.size(); ++slot) {
                    ImGui::PushID(static_cast<int>(slot));
                    const std::string label = AnimationGraphSlotLabelUVE(node.kind, slot);
                    row(label.c_str());
                    float point = node.points[slot];
                    ImGui::DragFloat("##point", &point, 0.01F, -1000.0F, 1000.0F, "%.2f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        editNode([slot, point](AnimationGraphNodeUVE& n) {
                            if (slot < n.points.size()) {
                                n.points[slot] = point;
                            }
                        });
                    }
                    ImGui::PopID();
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
            case Kind::StateMachine: {
                row("Entry");
                const std::string entry = AnimationGraphSlotLabelUVE(node.kind, node.entryState);
                if (ImGui::BeginCombo("##entry", entry.c_str())) {
                    for (std::uint32_t slot = 0U; slot < node.inputs.size(); ++slot) {
                        if (ImGui::Selectable(AnimationGraphSlotLabelUVE(node.kind, slot).c_str(), slot == node.entryState)) {
                            editNode([slot](AnimationGraphNodeUVE& n) { n.entryState = slot; });
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::TextDisabled("Transitions are edited in the Inspector.");
                break;
            }
            case Kind::Output:
                ImGui::TextDisabled("Wire the pose the target shows into it.");
                break;
        }
        if (node.kind == Kind::BlendSpace1D || node.kind == Kind::StateMachine) {
            if (ImGui::Button(node.kind == Kind::StateMachine ? "+ State" : "+ Point")) {
                edit = [id](Scene::AnimationTreeComponentUVE& t) { static_cast<void>(AddAnimationGraphInputSlotUVE(t.nodes, id)); };
            }
            ImGui::SameLine();
            if (ImGui::Button("- Last") && node.inputs.size() > 1U) {
                const std::size_t last = node.inputs.size() - 1U;
                edit = [id, last](Scene::AnimationTreeComponentUVE& t) {
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
                                     : std::string{"AnimationTree"};
        ImGui::TextDisabled("%s  -  %zu nodes, %zu parameters%s%s", name.c_str(), nodes.size(), component.parameters.size(),
                            component.activeStates.empty() ? "" : "  -  ", component.activeStates.c_str());
    }
    const float right = ImGui::GetContentRegionMax().x;
    char zoomText[16];
    std::snprintf(zoomText, sizeof(zoomText), "%d%%", static_cast<int>(std::lround(view.zoom * 100.0F)));
    const float buttons = ImGui::CalcTextSize("Frame All").x + ImGui::CalcTextSize("100%").x + ImGui::CalcTextSize("200%").x +
                          style.FramePadding.x * 6.0F + style.ItemSpacing.x * 3.0F;
    const float previewWidth = ImGui::CalcTextSize("Preview Off").x + style.FramePadding.x * 2.0F + style.ItemSpacing.x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + style.ItemSpacing.x, right - buttons - previewWidth));
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
        frameAll();
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
        static_cast<void>(EditAnimationTreeUVE(tree, *edit));
    }
}

} // namespace UVE::Editor
