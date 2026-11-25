// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Animation Graph's session, apart from how it is drawn: which AnimationGraph the Entity
// Editor's dock shows, the skeleton its preview runs on, the preview itself (the tree stepped on
// the skeleton's runtime pose, never saved) and its log, and the one-undo-step edit every change
// goes through.

#include "editor.h"
#include "model_animations.h"
#include "animation_graph_editing.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "animation_clip_asset.h"
#include "animation_driver_component.h"
#include "animation_graph.h"
#include "animation_graph_component.h"
#include "animation_library_asset.h"
#include "animation_sequencer_component.h"
#include "hierarchy_component.h"
#include "name_component.h"
#include "scene_component_metadata.h"
#include "skeleton_3d.h"
#include "type_metadata.h"

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKind;
using Scene::AnimationGraphNode;
using Scene::AnimationParameterType;
using Scene::AnimationParameter;

} // namespace

const std::string& Editor::AnimationClipName(const Asset::AssetGuid clip) {
    auto [it, added] = m_animGraph.clipNames.try_emplace(clip.value);
    if (added && clip != Asset::AssetGuid{}) {
        it->second = Asset::AssetDisplayName(m_services->GetAssetDatabase().Resolve(clip));
    }
    return it->second;
}

void Editor::RecordAnimationGraphPreview(const Scene::AnimationGraphComponent& tree) {
    AnimationGraphViewState& view = m_animGraph;
    constexpr std::size_t kLogLines = 80U;
    constexpr std::size_t kHistorySamples = 180U;
    const auto log = [&view](std::string text, const bool stateChange) {
        view.previewLog.push_back(AnimationGraphViewState::PreviewLogLine{view.previewClock, std::move(text), stateChange});
        if (view.previewLog.size() > kLogLines) {
            view.previewLog.erase(view.previewLog.begin());
        }
    };
    for (const std::string& event : tree.firedEvents) {
        log("event  " + event, false);
    }
    if (tree.nodeStates.size() == tree.nodes.size()) {
        for (std::size_t index = 0U; index < tree.nodes.size(); ++index) {
            const AnimationGraphNode& machine = tree.nodes[index];
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
                    const auto child = std::ranges::find(tree.nodes, machine.inputs[slot], &AnimationGraphNode::id);
                    if (child != tree.nodes.end() && !child->name.empty()) {
                        return child->name;
                    }
                }
                return AnimationGraphSlotLabel(Kind::StateMachine, slot);
            };
            // Which machine only matters when there are several.
            const bool several = std::ranges::count(tree.nodes, Kind::StateMachine, &AnimationGraphNode::kind) > 1;
            const std::string owner = several ? (machine.name.empty() ? std::string{"State Machine"} : machine.name) + ": " : "";
            log(owner + nameOf(it->second) + " -> " + nameOf(active), true);
            it->second = active;
        }
    }
    for (const AnimationParameter& parameter : tree.parameters) {
        if (parameter.type != AnimationParameterType::Float) {
            continue;
        }
        std::vector<float>& samples = view.parameterHistory[parameter.name];
        samples.push_back(parameter.value);
        if (samples.size() > kHistorySamples) {
            samples.erase(samples.begin());
        }
    }
}

void Editor::StopAnimationGraphPreview() {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (m_animGraph.previewSkeleton != Scene::kInvalidEntity && entityManager.IsAlive(m_animGraph.previewSkeleton) &&
        entityManager.HasComponent<Scene::Skeleton3DComponent>(m_animGraph.previewSkeleton)) {
        entityManager.GetComponent<Scene::Skeleton3DComponent>(m_animGraph.previewSkeleton).pose.clear();
    }
    m_animGraph.previewSkeleton = Scene::kInvalidEntity;
    if (m_animGraph.tree != Scene::kInvalidEntity && entityManager.IsAlive(m_animGraph.tree) &&
        entityManager.HasComponent<Scene::AnimationGraphComponent>(m_animGraph.tree)) {
        // Back to the start, so the next preview (and Play) begins from the entry state.
        entityManager.GetComponent<Scene::AnimationGraphComponent>(m_animGraph.tree).nodeStates.clear();
    }
}

bool Editor::EditAnimationGraph(const Scene::Entity tree,
                                     const std::function<void(Scene::AnimationGraphComponent&)>& change) {
    if (!IsAuthoringCommandAllowed()) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.IsAlive(tree) || !entityManager.HasComponent<Scene::AnimationGraphComponent>(tree)) {
        return false;
    }
    const Core::TypeMetadataEntry* const entry = Scene::GetSceneComponentMetadataRegistry().FindTypeByIndex(
        std::type_index(typeid(Scene::AnimationGraphComponent)));
    if (entry == nullptr || !entry->HasFactory()) {
        return false;
    }
    auto& component = entityManager.GetComponent<Scene::AnimationGraphComponent>(tree);
    const Scene::AnimationGraphComponent original = component;
    Core::TypeInstance before = Core::TypeInstance::Clone(*entry, &component);
    change(component);
    if (component.HasSameSettings(original) || !before.IsValid()) {
        return false;
    }
    const std::string problem = Scene::DescribeAnimationGraphProblem(component);
    Core::TypeInstance after = Core::TypeInstance::Clone(*entry, &component);
    if (!problem.empty() || !after.IsValid()) {
        component = original;
        m_animGraph.status = problem.empty() ? std::string{"That change could not be recorded."} : problem;
        return false;
    }
    // The shape may have changed: the runtime rebuilds its per-node state from the new graph.
    component.nodeStates.clear();
    const EditorSelectionSnapshot selection = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    m_sceneDirty = true;
    RecordHistory(ComponentPropertyHistoryEntry{tree, entry, std::move(before), std::move(after), selection,
                                                      selection, dirtyBefore, true});
    return true;
}

std::vector<Asset::AssetGuid> Editor::GetSequencerClips(const Scene::Entity sequencerEntity) const {
    std::vector<Asset::AssetGuid> clips;
    if (sequencerEntity == Scene::kInvalidEntity) {
        return clips;
    }
    const Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.IsAlive(sequencerEntity) ||
        !entityManager.HasComponent<Scene::AnimationSequencerComponent>(sequencerEntity)) {
        return clips;
    }
    const auto& comp = entityManager.GetComponent<Scene::AnimationSequencerComponent>(sequencerEntity);
    if (comp.clip != Asset::AssetGuid{}) {
        clips.push_back(comp.clip);
    }
    for (const Asset::AssetGuid guid : comp.library) {
        if (guid != Asset::AssetGuid{} && std::ranges::find(clips, guid) == clips.end()) {
            clips.push_back(guid);
        }
    }
    if (comp.libraryRef != Asset::AssetGuid{}) {
        const std::filesystem::path libPath = m_services->GetAssetDatabase().Resolve(comp.libraryRef);
        if (!libPath.empty()) {
            Asset::AnimationLibraryAsset libAsset;
            if (Asset::LoadAnimationLibraryAsset(libPath, libAsset)) {
                for (const auto& item : libAsset.entries) {
                    if (item.clip != Asset::AssetGuid{} && std::ranges::find(clips, item.clip) == clips.end()) {
                        clips.push_back(item.clip);
                    }
                }
            }
        }
    }
    return clips;
}

Scene::Entity Editor::ResolveActiveSequencerForGraph(const Scene::Entity treeEntity) const {
    const Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    // 1. Explicitly assigned on the AnimationGraphComponent
    if (treeEntity != Scene::kInvalidEntity && entityManager.IsAlive(treeEntity) &&
        entityManager.HasComponent<Scene::AnimationGraphComponent>(treeEntity)) {
        const auto& graphComp = entityManager.GetComponent<Scene::AnimationGraphComponent>(treeEntity);
        if (graphComp.sequencer != Scene::kInvalidEntity && entityManager.IsAlive(graphComp.sequencer) &&
            entityManager.HasComponent<Scene::AnimationSequencerComponent>(graphComp.sequencer)) {
            return graphComp.sequencer;
        }
    }
    // 2. View state
    if (m_animGraph.sequencer != Scene::kInvalidEntity && entityManager.IsAlive(m_animGraph.sequencer) &&
        entityManager.HasComponent<Scene::AnimationSequencerComponent>(m_animGraph.sequencer)) {
        return m_animGraph.sequencer;
    }
    // 3. Tree entity itself
    if (treeEntity != Scene::kInvalidEntity && entityManager.IsAlive(treeEntity) &&
        entityManager.HasComponent<Scene::AnimationSequencerComponent>(treeEntity)) {
        return treeEntity;
    }
    // 4. Target or parent entity
    if (treeEntity != Scene::kInvalidEntity && entityManager.IsAlive(treeEntity)) {
        if (entityManager.HasComponent<Scene::AnimationDriverComponent>(treeEntity)) {
            const auto target = entityManager.GetComponent<Scene::AnimationDriverComponent>(treeEntity).target;
            if (target != Scene::kInvalidEntity && entityManager.IsAlive(target) &&
                entityManager.HasComponent<Scene::AnimationSequencerComponent>(target)) {
                return target;
            }
        }
        if (entityManager.HasComponent<Scene::HierarchyComponent>(treeEntity)) {
            const auto parent = entityManager.GetComponent<Scene::HierarchyComponent>(treeEntity).parent;
            if (parent != Scene::kInvalidEntity && entityManager.IsAlive(parent) &&
                entityManager.HasComponent<Scene::AnimationSequencerComponent>(parent)) {
                return parent;
            }
        }
    }
    // 5. Timeline player
    if (m_timeline.player != Scene::kInvalidEntity && entityManager.IsAlive(m_timeline.player) &&
        entityManager.HasComponent<Scene::AnimationSequencerComponent>(m_timeline.player)) {
        return m_timeline.player;
    }
    return Scene::kInvalidEntity;
}


Editor::AnimationGraphPanelView Editor::SyncAnimationGraphView() {
    AnimationGraphPanelView out;
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    AnimationGraphViewState& view = m_animGraph;
    const auto isTree = [&entityManager](const Scene::Entity entity) {
        return entity != Scene::kInvalidEntity && entityManager.IsAlive(entity) &&
               entityManager.HasComponent<Scene::AnimationGraphComponent>(entity);
    };
    for (const Scene::Entity object : CollectEntityEditorObjects()) {
        if (isTree(object)) {
            out.graphs.push_back(object);
        }
    }
    // The tree to show: the selected one, else the one shown last, else the entity's first.
    Scene::Entity tree = Scene::kInvalidEntity;
    if (std::ranges::find(out.graphs, m_selectedEntity) != out.graphs.end()) {
        tree = m_selectedEntity;
    } else if (std::ranges::find(out.graphs, view.tree) != out.graphs.end()) {
        tree = view.tree;
    } else if (!out.graphs.empty()) {
        tree = out.graphs.front();
    }
    if (tree != view.tree) {
        StopAnimationGraphPreview();
        const bool previewing = view.previewing;
        const bool paused = view.previewPaused;
        const float rate = view.previewRate;
        auto clips = std::move(view.clips);
        view = AnimationGraphViewState{};
        view.tree = tree;
        if (isTree(tree)) {
            view.sequencer = entityManager.GetComponent<Scene::AnimationGraphComponent>(tree).sequencer;
        }
        view.previewing = previewing;
        view.previewPaused = paused;
        view.previewRate = rate;
        view.clips = std::move(clips);
    }
    out.tree = tree;
    if (tree == Scene::kInvalidEntity) {
        return out;
    }
    // The skeleton the tree runs on: the driver's target, else the tree's parent, else the active
    // sequencer's target or the sequencer itself, searched down.
    const Scene::AnimationDriverComponent driver = entityManager.HasComponent<Scene::AnimationDriverComponent>(tree)
                                                        ? entityManager.GetComponent<Scene::AnimationDriverComponent>(tree)
                                                        : Scene::AnimationDriverComponent{};
    Scene::Entity root = driver.target;
    if (root == Scene::kInvalidEntity || !entityManager.IsAlive(root)) {
        root = entityManager.HasComponent<Scene::HierarchyComponent>(tree)
                   ? entityManager.GetComponent<Scene::HierarchyComponent>(tree).parent
                   : Scene::kInvalidEntity;
    }
    if (root == Scene::kInvalidEntity || !entityManager.IsAlive(root)) {
        const Scene::Entity active = ResolveActiveSequencerForGraph(tree);
        if (active != Scene::kInvalidEntity && entityManager.IsAlive(active)) {
            if (entityManager.HasComponent<Scene::AnimationDriverComponent>(active)) {
                root = entityManager.GetComponent<Scene::AnimationDriverComponent>(active).target;
            }
            if (root == Scene::kInvalidEntity || !entityManager.IsAlive(root)) {
                root = active;
            }
        }
    }
    std::vector<Scene::Entity> queue;
    if (root != Scene::kInvalidEntity && entityManager.IsAlive(root)) {
        queue.push_back(root);
    }
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();
    for (std::size_t next = 0U; next < queue.size() && next < 4096U; ++next) {
        if (entityManager.HasComponent<Scene::Skeleton3DComponent>(queue[next])) {
            out.skeleton = queue[next];
            break;
        }
        const std::vector<Scene::Entity> children = sceneGraph.GetChildren(entityManager, queue[next]);
        queue.insert(queue.end(), children.begin(), children.end());
    }
    return out;
}

const Asset::AnimationClipAsset* Editor::AnimationGraphPreviewClip(const Asset::AssetGuid guid) {
    if (guid == Asset::AssetGuid{}) {
        return nullptr;
    }
    auto found = m_animGraph.clips.find(guid.value);
    if (found == m_animGraph.clips.end()) {
        auto loaded = std::make_shared<Asset::AnimationClipAsset>();
        const std::filesystem::path path = m_services->GetAssetDatabase().Resolve(guid);
        found = m_animGraph.clips.emplace(guid.value, !path.empty() && Asset::LoadAnimationClipAsset(path, *loaded) ? std::move(loaded) : nullptr)
                    .first;
    }
    return found->second.get();
}

void Editor::StepAnimationGraphPreview(const double deltaSeconds) {
    AnimationGraphViewState& view = m_animGraph;
    const AnimationGraphPanelView resolved = SyncAnimationGraphView();
    if (resolved.tree == Scene::kInvalidEntity) {
        return;
    }
    if (!view.previewing || resolved.skeleton == Scene::kInvalidEntity) {
        if (view.previewSkeleton != Scene::kInvalidEntity) {
            StopAnimationGraphPreview();
        }
        return;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (view.previewSkeleton != resolved.skeleton) {
        StopAnimationGraphPreview();
        view.previewSkeleton = resolved.skeleton;
    }
    const Scene::AnimationDriverComponent driver = entityManager.HasComponent<Scene::AnimationDriverComponent>(resolved.tree)
                                                        ? entityManager.GetComponent<Scene::AnimationDriverComponent>(resolved.tree)
                                                        : Scene::AnimationDriverComponent{};
    const Scene::AnimationClipResolver clipFor = [this](const Asset::AssetGuid guid) { return AnimationGraphPreviewClip(guid); };
    auto& live = entityManager.GetComponent<Scene::AnimationGraphComponent>(resolved.tree);
    auto& skeleton = entityManager.GetComponent<Scene::Skeleton3DComponent>(resolved.skeleton);
    Scene::AnimationDriverComponent mixer = driver;
    mixer.active = true;
    // Held: only a frame asked for moves it, and then by one sixtieth of a second.
    const float frame = view.previewPaused ? (view.previewStepOnce ? 1.0F / 60.0F : 0.0F)
                                           : std::clamp(static_cast<float>(deltaSeconds), 0.0F, 0.1F) * view.previewRate;
    view.previewStepOnce = false;
    if (frame > 0.0F) {
        static_cast<void>(Scene::StepSkeletalAnimationGraph(live, clipFor, frame * driver.speedScale, skeleton, mixer));
        view.previewClock += static_cast<double>(frame);
        RecordAnimationGraphPreview(live);
    }
}

} // namespace UVE::Editor
