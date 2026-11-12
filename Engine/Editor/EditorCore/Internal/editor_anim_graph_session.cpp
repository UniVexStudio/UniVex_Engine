// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Animation Graph's session, apart from how it is drawn: which AnimationGraph the Entity
// Editor's dock shows, the skeleton its preview runs on, the preview itself (the tree stepped on
// the skeleton's runtime pose, never saved) and its log, and the one-undo-step edit every change
// goes through.

#include "editor.h"
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

using Kind = Scene::AnimationGraphNodeKindUVE;
using Scene::AnimationGraphNodeUVE;
using Scene::AnimationParameterTypeUVE;
using Scene::AnimationParameterUVE;

} // namespace

const std::string& EditorUVE::AnimationClipNameUVE(const Asset::AssetGuidUVE clip) {
    auto [it, added] = m_animGraph.clipNames.try_emplace(clip.value);
    if (added && clip != Asset::AssetGuidUVE{}) {
        it->second = m_services->GetAssetDatabaseUVE().ResolveUVE(clip).stem().string();
    }
    return it->second;
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

std::vector<Asset::AssetGuidUVE> EditorUVE::GetSequencerClipsUVE(const Scene::EntityUVE sequencerEntity) const {
    std::vector<Asset::AssetGuidUVE> clips;
    if (sequencerEntity == Scene::kInvalidEntityUVE) {
        return clips;
    }
    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.IsAliveUVE(sequencerEntity) ||
        !entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(sequencerEntity)) {
        return clips;
    }
    const auto& comp = entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(sequencerEntity);
    if (comp.clip != Asset::AssetGuidUVE{}) {
        clips.push_back(comp.clip);
    }
    for (const Asset::AssetGuidUVE guid : comp.library) {
        if (guid != Asset::AssetGuidUVE{} && std::ranges::find(clips, guid) == clips.end()) {
            clips.push_back(guid);
        }
    }
    if (comp.libraryRef != Asset::AssetGuidUVE{}) {
        const std::filesystem::path libPath = m_services->GetAssetDatabaseUVE().ResolveUVE(comp.libraryRef);
        if (!libPath.empty()) {
            Asset::AnimationLibraryAssetUVE libAsset;
            if (Asset::LoadAnimationLibraryAssetUVE(libPath, libAsset)) {
                for (const auto& item : libAsset.entries) {
                    if (item.clip != Asset::AssetGuidUVE{} && std::ranges::find(clips, item.clip) == clips.end()) {
                        clips.push_back(item.clip);
                    }
                }
            }
        }
    }
    return clips;
}

Scene::EntityUVE EditorUVE::ResolveActiveSequencerForGraphUVE(const Scene::EntityUVE treeEntity) const {
    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    // 1. Explicitly assigned on the AnimationGraphComponentUVE
    if (treeEntity != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(treeEntity) &&
        entityManager.HasComponentUVE<Scene::AnimationGraphComponentUVE>(treeEntity)) {
        const auto& graphComp = entityManager.GetComponentUVE<Scene::AnimationGraphComponentUVE>(treeEntity);
        if (graphComp.sequencer != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(graphComp.sequencer) &&
            entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(graphComp.sequencer)) {
            return graphComp.sequencer;
        }
    }
    // 2. View state
    if (m_animGraph.sequencer != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(m_animGraph.sequencer) &&
        entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(m_animGraph.sequencer)) {
        return m_animGraph.sequencer;
    }
    // 3. Tree entity itself
    if (treeEntity != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(treeEntity) &&
        entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(treeEntity)) {
        return treeEntity;
    }
    // 4. Target or parent entity
    if (treeEntity != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(treeEntity)) {
        if (entityManager.HasComponentUVE<Scene::AnimationDriverComponentUVE>(treeEntity)) {
            const auto target = entityManager.GetComponentUVE<Scene::AnimationDriverComponentUVE>(treeEntity).target;
            if (target != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(target) &&
                entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(target)) {
                return target;
            }
        }
        if (entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(treeEntity)) {
            const auto parent = entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(treeEntity).parent;
            if (parent != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(parent) &&
                entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(parent)) {
                return parent;
            }
        }
    }
    // 5. Timeline player
    if (m_timeline.player != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(m_timeline.player) &&
        entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(m_timeline.player)) {
        return m_timeline.player;
    }
    return Scene::kInvalidEntityUVE;
}


EditorUVE::AnimationGraphPanelViewUVE EditorUVE::SyncAnimationGraphViewUVE() {
    AnimationGraphPanelViewUVE out;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    AnimationGraphViewStateUVE& view = m_animGraph;
    const auto isTree = [&entityManager](const Scene::EntityUVE entity) {
        return entity != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(entity) &&
               entityManager.HasComponentUVE<Scene::AnimationGraphComponentUVE>(entity);
    };
    for (const Scene::EntityUVE object : CollectEntityEditorObjectsUVE()) {
        if (isTree(object)) {
            out.graphs.push_back(object);
        }
    }
    // The tree to show: the selected one, else the one shown last, else the entity's first.
    Scene::EntityUVE tree = Scene::kInvalidEntityUVE;
    if (std::ranges::find(out.graphs, m_selectedEntity) != out.graphs.end()) {
        tree = m_selectedEntity;
    } else if (std::ranges::find(out.graphs, view.tree) != out.graphs.end()) {
        tree = view.tree;
    } else if (!out.graphs.empty()) {
        tree = out.graphs.front();
    }
    if (tree != view.tree) {
        StopAnimationGraphPreviewUVE();
        const bool previewing = view.previewing;
        const bool paused = view.previewPaused;
        const float rate = view.previewRate;
        auto clips = std::move(view.clips);
        view = AnimationGraphViewStateUVE{};
        view.tree = tree;
        if (isTree(tree)) {
            view.sequencer = entityManager.GetComponentUVE<Scene::AnimationGraphComponentUVE>(tree).sequencer;
        }
        view.previewing = previewing;
        view.previewPaused = paused;
        view.previewRate = rate;
        view.clips = std::move(clips);
    }
    out.tree = tree;
    if (tree == Scene::kInvalidEntityUVE) {
        return out;
    }
    // The skeleton the tree runs on: the driver's target, else the tree's parent, else the active
    // sequencer's target or the sequencer itself, searched down.
    const Scene::AnimationDriverComponentUVE driver = entityManager.HasComponentUVE<Scene::AnimationDriverComponentUVE>(tree)
                                                        ? entityManager.GetComponentUVE<Scene::AnimationDriverComponentUVE>(tree)
                                                        : Scene::AnimationDriverComponentUVE{};
    Scene::EntityUVE root = driver.target;
    if (root == Scene::kInvalidEntityUVE || !entityManager.IsAliveUVE(root)) {
        root = entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(tree)
                   ? entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(tree).parent
                   : Scene::kInvalidEntityUVE;
    }
    if (root == Scene::kInvalidEntityUVE || !entityManager.IsAliveUVE(root)) {
        const Scene::EntityUVE active = ResolveActiveSequencerForGraphUVE(tree);
        if (active != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(active)) {
            if (entityManager.HasComponentUVE<Scene::AnimationDriverComponentUVE>(active)) {
                root = entityManager.GetComponentUVE<Scene::AnimationDriverComponentUVE>(active).target;
            }
            if (root == Scene::kInvalidEntityUVE || !entityManager.IsAliveUVE(root)) {
                root = active;
            }
        }
    }
    std::vector<Scene::EntityUVE> queue;
    if (root != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(root)) {
        queue.push_back(root);
    }
    Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();
    for (std::size_t next = 0U; next < queue.size() && next < 4096U; ++next) {
        if (entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(queue[next])) {
            out.skeleton = queue[next];
            break;
        }
        const std::vector<Scene::EntityUVE> children = sceneGraph.GetChildrenUVE(entityManager, queue[next]);
        queue.insert(queue.end(), children.begin(), children.end());
    }
    return out;
}

const Asset::AnimationClipAssetUVE* EditorUVE::AnimationGraphPreviewClipUVE(const Asset::AssetGuidUVE guid) {
    if (guid == Asset::AssetGuidUVE{}) {
        return nullptr;
    }
    auto found = m_animGraph.clips.find(guid.value);
    if (found == m_animGraph.clips.end()) {
        auto loaded = std::make_shared<Asset::AnimationClipAssetUVE>();
        const std::filesystem::path path = m_services->GetAssetDatabaseUVE().ResolveUVE(guid);
        found = m_animGraph.clips.emplace(guid.value, !path.empty() && Asset::LoadAnimationClipAssetUVE(path, *loaded) ? std::move(loaded) : nullptr)
                    .first;
    }
    return found->second.get();
}

void EditorUVE::StepAnimationGraphPreviewUVE(const double deltaSeconds) {
    AnimationGraphViewStateUVE& view = m_animGraph;
    const AnimationGraphPanelViewUVE resolved = SyncAnimationGraphViewUVE();
    if (resolved.tree == Scene::kInvalidEntityUVE) {
        return;
    }
    if (!view.previewing || resolved.skeleton == Scene::kInvalidEntityUVE) {
        if (view.previewSkeleton != Scene::kInvalidEntityUVE) {
            StopAnimationGraphPreviewUVE();
        }
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (view.previewSkeleton != resolved.skeleton) {
        StopAnimationGraphPreviewUVE();
        view.previewSkeleton = resolved.skeleton;
    }
    const Scene::AnimationDriverComponentUVE driver = entityManager.HasComponentUVE<Scene::AnimationDriverComponentUVE>(resolved.tree)
                                                        ? entityManager.GetComponentUVE<Scene::AnimationDriverComponentUVE>(resolved.tree)
                                                        : Scene::AnimationDriverComponentUVE{};
    const Scene::AnimationClipResolverUVE clipFor = [this](const Asset::AssetGuidUVE guid) { return AnimationGraphPreviewClipUVE(guid); };
    auto& live = entityManager.GetComponentUVE<Scene::AnimationGraphComponentUVE>(resolved.tree);
    auto& skeleton = entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(resolved.skeleton);
    Scene::AnimationDriverComponentUVE mixer = driver;
    mixer.active = true;
    // Held: only a frame asked for moves it, and then by one sixtieth of a second.
    const float frame = view.previewPaused ? (view.previewStepOnce ? 1.0F / 60.0F : 0.0F)
                                           : std::clamp(static_cast<float>(deltaSeconds), 0.0F, 0.1F) * view.previewRate;
    view.previewStepOnce = false;
    if (frame > 0.0F) {
        static_cast<void>(Scene::StepSkeletalAnimationGraphUVE(live, clipFor, frame * driver.speedScale, skeleton, mixer));
        view.previewClock += static_cast<double>(frame);
        RecordAnimationGraphPreviewUVE(live);
    }
}

} // namespace UVE::Editor
