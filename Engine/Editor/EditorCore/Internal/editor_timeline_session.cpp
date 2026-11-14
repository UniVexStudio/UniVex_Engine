// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Animation Sequencer's session, apart from how it is drawn: which AnimationSequencer the
// Entity Editor's dock shows, that player's clip (reloaded when it changes, with its own undo
// history), the transport, and the preview on the entity's skeleton and blend shapes. The preview
// writes runtime state only (the skeleton's pose, the morph components' weights) and puts the
// weights back when it stops, so scrubbing never dirties the entity or reaches its file.

#include "editor.h"
#include "animation_clip_editing.h"
#include "editor_timeline_tracks.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "animation_clip_asset.h"
#include "animation_driver_component.h"
#include "animation_sequencer.h"
#include "animation_sequencer_component.h"
#include "hierarchy_component.h"
#include "mesh_asset.h"
#include "mesh_component.h"
#include "morph_target_component.h"
#include "name_component.h"
#include "skeleton_3d.h"

namespace UVE::Editor {
namespace {

/// How many previewed objects' blend-shape name lists are kept: an editor session previews the
/// faces around one character, and a bound stops a long session from holding every mesh it saw.
constexpr std::size_t kMorphNameCacheSizeUVE = 64U;

} // namespace

void EditorUVE::StopAnimationTimelinePreviewUVE() {
    m_timeline.playing = false;
    m_timelineOwnsKeys = false;
    // The blend shapes first, and unconditionally: a skin-less facial clip has no preview skeleton
    // at all, so tying the restore to one would leave a face frozen mid-word.
    StopAnimationTimelineMorphPreviewUVE();
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (m_timeline.player != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(m_timeline.player) &&
        entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(m_timeline.player)) {
        auto& player = entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(m_timeline.player);
        player.isPlaying = false;
        player.currentTimeSeconds = 0.0F;
        player.hasStartPose = false;
    }
    m_timeline.player = Scene::kInvalidEntityUVE;
    if (m_timeline.previewSkeleton == Scene::kInvalidEntityUVE) {
        return;
    }
    // Hold the evaluated pose instead of popping to bind pose (pose.clear()).
    m_timeline.previewSkeleton = Scene::kInvalidEntityUVE;
}

void EditorUVE::StopAnimationTimelineMorphPreviewUVE() {
    m_timeline.morphTracksNotPlaying = 0U;
    if (m_timeline.morphPreview.empty()) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    for (const AnimationTimelineStateUVE::MorphPreviewTargetUVE& target : m_timeline.morphPreview) {
        if (target.entity == Scene::kInvalidEntityUVE || !entityManager.IsAliveUVE(target.entity) ||
            !entityManager.HasComponentUVE<Scene::MorphTargetComponentUVE>(target.entity)) {
            continue;
        }
        // Only the list this preview wrote: `enabled` belongs to the author and stays as it was.
        entityManager.GetComponentUVE<Scene::MorphTargetComponentUVE>(target.entity).weights = target.previousWeights;
    }
    m_timeline.morphPreview.clear();
}

std::span<const std::string> EditorUVE::TimelineMorphTargetNamesUVE(const Scene::EntityUVE entity) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (entity == Scene::kInvalidEntityUVE || !entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<Scene::MeshComponentUVE>(entity)) {
        return {};
    }
    const Asset::AssetGuidUVE meshGuid = entityManager.GetComponentUVE<Scene::MeshComponentUVE>(entity).meshGuid;
    if (meshGuid == Asset::AssetGuidUVE{} || meshGuid == Asset::kInvalidAssetGuidUVE) {
        return {};
    }
    for (const AnimationTimelineStateUVE::MorphTargetNamesUVE& cached : m_timeline.morphTargetNames) {
        if (cached.entity == entity && cached.meshGuid == meshGuid) {
            return cached.names;
        }
    }

    // A .uvmodel is read once per object per mesh, never per frame: the target NAMES are all this
    // needs, and a face mesh carries as many deltas as it does vertices.
    AnimationTimelineStateUVE::MorphTargetNamesUVE entry;
    entry.entity = entity;
    entry.meshGuid = meshGuid;
    Asset::MeshAssetUVE mesh;
    const std::filesystem::path path = m_services->GetAssetDatabaseUVE().ResolveUVE(meshGuid);
    if (!path.empty() && Asset::LoadMeshAssetUVE(path, mesh)) {
        entry.names.reserve(mesh.morphTargets.size());
        for (const Asset::MeshMorphTargetUVE& target : mesh.morphTargets) {
            entry.names.push_back(target.name);
        }
    }
    if (m_timeline.morphTargetNames.size() >= kMorphNameCacheSizeUVE) {
        m_timeline.morphTargetNames.erase(m_timeline.morphTargetNames.begin());
    }
    m_timeline.morphTargetNames.push_back(std::move(entry));
    return m_timeline.morphTargetNames.back().names;
}

void EditorUVE::PreviewAnimationTimelineMorphsUVE(const Asset::AnimationClipAssetUVE& clip,
                                                  const Scene::EntityUVE root) {
    if (!clip.HasMorphTracksUVE() || root == Scene::kInvalidEntityUVE) {
        StopAnimationTimelineMorphPreviewUVE();
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();

    // The objects under the root, by name. The walk stops as soon as every name a track asks for
    // has an answer, and the first object in breadth-first order keeps a name two of them share -
    // the same rule the scene's own lookups follow.
    std::unordered_set<std::string> wanted;
    wanted.reserve(clip.morphWeights.size());
    for (const Asset::AnimationAssetMorphTrackUVE& track : clip.morphWeights) {
        wanted.insert(track.node);
    }
    std::unordered_map<std::string, Scene::EntityUVE> byName;
    std::vector<Scene::EntityUVE> queue{root};
    for (std::size_t next = 0U; next < queue.size() && next < 4096U && byName.size() < wanted.size(); ++next) {
        const Scene::EntityUVE entity = queue[next];
        if (entityManager.HasComponentUVE<Scene::NameComponentUVE>(entity)) {
            const std::string& name = entityManager.GetComponentUVE<Scene::NameComponentUVE>(entity).name;
            if (wanted.contains(name)) {
                byName.emplace(name, entity);
            }
        }
        const std::vector<Scene::EntityUVE> children = sceneGraph.GetChildrenUVE(entityManager, entity);
        queue.insert(queue.end(), children.begin(), children.end());
    }

    TimelineMorphSceneUVE scene;
    scene.entityForNode = [&byName](const std::string_view node) -> std::optional<Scene::EntityUVE> {
        const auto found = byName.find(std::string{node});
        return found != byName.end() ? std::optional<Scene::EntityUVE>{found->second} : std::nullopt;
    };
    scene.targetNames = [this, &entityManager](const Scene::EntityUVE entity) -> std::span<const std::string> {
        // Without the component there is nothing to write into, and the preview never adds one: a
        // preview looks at the scene, it does not author it. Reporting "no shapes" is exactly how
        // the resolve counts that track as unplayable.
        if (!entityManager.HasComponentUVE<Scene::MorphTargetComponentUVE>(entity)) {
            return {};
        }
        return TimelineMorphTargetNamesUVE(entity);
    };

    const TimelineMorphPreviewUVE preview = ResolveTimelineMorphPreviewUVE(clip, m_timeline.timeSeconds, scene);

    // A different set of objects than the one being driven means the clip or the scene moved under
    // the preview (an undo, a key edit, a renamed object). Put back what the old set held and
    // capture again from what is there now, so the authored weights are what gets restored.
    const bool sameTargets =
        m_timeline.morphPreview.size() == preview.writes.size() &&
        std::all_of(preview.writes.begin(), preview.writes.end(), [this](const TimelineMorphWriteUVE& write) {
            return std::any_of(m_timeline.morphPreview.begin(), m_timeline.morphPreview.end(),
                               [&write](const AnimationTimelineStateUVE::MorphPreviewTargetUVE& target) {
                                   return target.entity == write.entity;
                               });
        });
    if (!sameTargets) {
        StopAnimationTimelineMorphPreviewUVE();
        for (const TimelineMorphWriteUVE& write : preview.writes) {
            AnimationTimelineStateUVE::MorphPreviewTargetUVE target;
            target.entity = write.entity;
            target.previousWeights = entityManager.GetComponentUVE<Scene::MorphTargetComponentUVE>(write.entity).weights;
            m_timeline.morphPreview.push_back(std::move(target));
        }
    }
    m_timeline.morphTracksNotPlaying = preview.unresolvedTracks;

    for (const TimelineMorphWriteUVE& write : preview.writes) {
        if (!entityManager.IsAliveUVE(write.entity) ||
            !entityManager.HasComponentUVE<Scene::MorphTargetComponentUVE>(write.entity)) {
            continue;
        }
        entityManager.GetComponentUVE<Scene::MorphTargetComponentUVE>(write.entity).weights = write.weights;
    }
}

EditorUVE::AnimationTimelineViewUVE EditorUVE::SyncAnimationTimelineUVE() {
    AnimationTimelineViewUVE view;
    // Last call's replaced clip is no longer read by anything.
    m_timeline.retired.reset();
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();

    // ---- Which player: the selected one, else the one shown last, else the first with a clip -----
    for (const Scene::EntityUVE object : CollectEntityEditorObjectsUVE()) {
        if (entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(object)) {
            view.players.push_back(object);
        }
    }
    if (view.players.empty()) {
        if (m_timeline.player != Scene::kInvalidEntityUVE || !m_timeline.morphPreview.empty()) {
            StopAnimationTimelinePreviewUVE();
        }
        return view;
    }
    if (std::ranges::find(view.players, m_selectedEntity) != view.players.end()) {
        m_timeline.player = m_selectedEntity;
    } else if (std::ranges::find(view.players, m_timeline.player) == view.players.end()) {
        // The first one with a clip: an entity's own empty player should not hide a working one.
        m_timeline.player = view.players.front();
        for (const Scene::EntityUVE candidate : view.players) {
            if (entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(candidate).clip != Asset::AssetGuidUVE{}) {
                m_timeline.player = candidate;
                break;
            }
        }
    }
    view.player = m_timeline.player;
    auto& player = entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(view.player);
    const Scene::AnimationDriverComponentUVE driver =
        entityManager.HasComponentUVE<Scene::AnimationDriverComponentUVE>(view.player)
            ? entityManager.GetComponentUVE<Scene::AnimationDriverComponentUVE>(view.player)
            : Scene::AnimationDriverComponentUVE{};

    // ---- The clip, reloaded when the player's clip changes ----------------------------------------
    if (player.clip != m_timeline.clipGuid) {
        // Switching away keeps the work: edits not yet saved are written to their clip first.
        if (m_timeline.dirty && m_timeline.clip != nullptr) {
            const std::filesystem::path previous = m_services->GetAssetDatabaseUVE().ResolveUVE(m_timeline.clipGuid);
            if (!previous.empty() && Asset::SaveAnimationClipAssetUVE(*m_timeline.clip, previous)) {
                m_timeline.status = "Saved your edits to " + previous.filename().string();
            }
        }
        const Scene::EntityUVE keep = m_timeline.player;
        StopAnimationTimelinePreviewUVE();
        m_timeline.player = keep;
        m_timeline.clipGuid = player.clip;
        m_timeline.clip.reset();
        m_timeline.selectedKeys.clear();
        m_timeline.selectedEvent = -1;
        m_timeline.undo.clear();
        m_timeline.redo.clear();
        m_timeline.dirty = false;
        m_timeline.draggingKeys = false;
        m_timeline.boxSelecting = false;
        m_timeline.loadError.clear();
        m_timeline.timeSeconds = 0.0;
        m_timeline.scrollSeconds = 0.0;
        m_timeline.loop = player.loopMode != Scene::AnimationLoopModeUVE::Once;
        player.isPlaying = false;
        player.currentTimeSeconds = 0.0F;
        player.hasStartPose = false;
        if (player.clip != Asset::AssetGuidUVE{}) {
            auto loaded = std::make_shared<Asset::AnimationClipAssetUVE>();
            const std::filesystem::path path = m_services->GetAssetDatabaseUVE().ResolveUVE(player.clip);
            if (Asset::LoadAnimationClipAssetUVE(path, *loaded)) {
                m_timeline.clip = std::move(loaded);
            } else {
                m_timeline.loadError = "Could not read " + path.filename().string() + ".";
            }
        }
    }

    // ---- Where the preview looks: the driver's target, else the player's parent -------------------
    // One root for both halves of the preview - the skeleton it poses and the meshes whose blend
    // shapes it drives - so a clip can never be posed on one rig while its face animates another.
    view.previewRoot = driver.target;
    if (view.previewRoot == Scene::kInvalidEntityUVE || !entityManager.IsAliveUVE(view.previewRoot)) {
        view.previewRoot = entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(view.player)
                               ? entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(view.player).parent
                               : Scene::kInvalidEntityUVE;
    }
    std::vector<Scene::EntityUVE> queue;
    if (view.previewRoot != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(view.previewRoot)) {
        queue.push_back(view.previewRoot);
    }
    for (std::size_t next = 0U; next < queue.size() && next < 4096U; ++next) {
        if (entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(queue[next])) {
            view.skeleton = queue[next];
            break;
        }
        const std::vector<Scene::EntityUVE> children = sceneGraph.GetChildrenUVE(entityManager, queue[next]);
        queue.insert(queue.end(), children.begin(), children.end());
    }

    view.clip = m_timeline.clip.get();
    if (view.clip != nullptr) {
        view.durationSeconds = std::max(view.clip->durationSeconds, 1.0e-3);
        view.frameRate = EstimateTimelineFrameRateUVE(*view.clip);
    }
    return view;
}

void EditorUVE::StepAnimationTimelineUVE(const double deltaSeconds) {
    const AnimationTimelineViewUVE view = SyncAnimationTimelineUVE();
    if (view.player == Scene::kInvalidEntityUVE) {
        return;
    }
    if (view.clip == nullptr) {
        if (m_timeline.previewSkeleton != Scene::kInvalidEntityUVE || !m_timeline.morphPreview.empty()) {
            const Scene::EntityUVE keep = m_timeline.player;
            StopAnimationTimelinePreviewUVE();
            m_timeline.player = keep;
        }
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    auto& player = entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(view.player);
    const Scene::AnimationDriverComponentUVE driver =
        entityManager.HasComponentUVE<Scene::AnimationDriverComponentUVE>(view.player)
            ? entityManager.GetComponentUVE<Scene::AnimationDriverComponentUVE>(view.player)
            : Scene::AnimationDriverComponentUVE{};
    const double duration = view.durationSeconds;
    if (m_timeline.playing) {
        const double speed = std::isfinite(player.speed) && player.speed != 0.0F ? player.speed : 1.0;
        m_timeline.timeSeconds += deltaSeconds * speed * static_cast<double>(driver.speedScale);
        if (m_timeline.loop) {
            m_timeline.timeSeconds = std::fmod(m_timeline.timeSeconds, duration);
            if (m_timeline.timeSeconds < 0.0) {
                m_timeline.timeSeconds += duration;
            }
        } else if (m_timeline.timeSeconds >= duration || m_timeline.timeSeconds <= 0.0) {
            m_timeline.timeSeconds = std::clamp(m_timeline.timeSeconds, 0.0, duration);
            m_timeline.playing = false;
        }
        player.isPlaying = m_timeline.playing;
        player.currentTimeSeconds = static_cast<float>(m_timeline.timeSeconds);
    }
    // ---- Preview on the skeleton, then the blend shapes (which play with or without a rig) -------
    if (view.skeleton != m_timeline.previewSkeleton) {
        m_timeline.previewSkeleton = Scene::kInvalidEntityUVE;
    }
    if (view.clip->IsSkeletalUVE() && view.skeleton != Scene::kInvalidEntityUVE) {
        auto& posed = entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(view.skeleton);
        if (Scene::PoseSkeletonAtTimeUVE(*view.clip, m_timeline.timeSeconds, posed, driver)) {
            m_timeline.previewSkeleton = view.skeleton;
        }
    }
    PreviewAnimationTimelineMorphsUVE(*view.clip, view.previewRoot);
}

namespace {

[[nodiscard]] Scene::AnimationSequencerComponentUVE* TimelinePlayerUVE(Scene::IEntityManagerUVE& entityManager,
                                                                      const Scene::EntityUVE player) {
    if (player == Scene::kInvalidEntityUVE || !entityManager.IsAliveUVE(player) ||
        !entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(player)) {
        return nullptr;
    }
    return &entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(player);
}

[[nodiscard]] double TimelineDurationUVE(const std::shared_ptr<const Asset::AnimationClipAssetUVE>& clip) {
    return clip != nullptr ? std::max(clip->durationSeconds, 1.0e-3) : 1.0;
}

[[nodiscard]] double TimelineFrameRateUVE(const std::shared_ptr<const Asset::AnimationClipAssetUVE>& clip) {
    return clip != nullptr ? EstimateTimelineFrameRateUVE(*clip) : 30.0;
}

} // namespace

void EditorUVE::SetAnimationTimelineTimeUVE(const double seconds) {
    m_timeline.playing = false;
    m_timeline.timeSeconds = std::clamp(seconds, 0.0, TimelineDurationUVE(m_timeline.clip));
    if (auto* player = TimelinePlayerUVE(m_services->GetEntityManagerUVE(), m_timeline.player)) {
        player->isPlaying = false;
        player->currentTimeSeconds = static_cast<float>(m_timeline.timeSeconds);
    }
}

void EditorUVE::ToggleAnimationTimelinePlayUVE() {
    if (m_timeline.clip == nullptr) {
        return;
    }
    m_timeline.playing = !m_timeline.playing;
    if (m_timeline.playing && !m_timeline.loop && m_timeline.timeSeconds >= TimelineDurationUVE(m_timeline.clip)) {
        m_timeline.timeSeconds = 0.0;
    }
    if (auto* player = TimelinePlayerUVE(m_services->GetEntityManagerUVE(), m_timeline.player)) {
        player->isPlaying = m_timeline.playing;
        player->currentTimeSeconds = static_cast<float>(m_timeline.timeSeconds);
    }
}

void EditorUVE::StopAnimationTimelinePlaybackUVE() {
    SetAnimationTimelineTimeUVE(0.0);
}

void EditorUVE::StepAnimationTimelineFrameUVE(const int frames) {
    const double rate = TimelineFrameRateUVE(m_timeline.clip);
    const double frame = std::round(m_timeline.timeSeconds * rate) + static_cast<double>(frames);
    SetAnimationTimelineTimeUVE(frame / rate);
}

void EditorUVE::SetAnimationTimelineLoopUVE(const bool loop) {
    m_timeline.loop = loop;
    if (auto* player = TimelinePlayerUVE(m_services->GetEntityManagerUVE(), m_timeline.player)) {
        player->loopMode = loop ? Scene::AnimationLoopModeUVE::Loop : Scene::AnimationLoopModeUVE::Once;
    }
}

bool EditorUVE::EditAnimationTimelineClipUVE(const std::function<void(Asset::AnimationClipAssetUVE&)>& change) {
    if (m_timeline.clip == nullptr || !change) {
        return false;
    }
    auto next = std::make_shared<Asset::AnimationClipAssetUVE>(*m_timeline.clip);
    change(*next);
    m_timeline.undo.push_back(m_timeline.clip);
    if (m_timeline.undo.size() > 64U) {
        m_timeline.undo.erase(m_timeline.undo.begin());
    }
    m_timeline.redo.clear();
    m_timeline.retired = m_timeline.clip;
    m_timeline.clip = std::move(next);
    m_timeline.dirty = true;
    return true;
}

void EditorUVE::UndoAnimationTimelineUVE() {
    if (m_timeline.undo.empty()) {
        return;
    }
    m_timeline.redo.push_back(m_timeline.clip);
    m_timeline.clip = m_timeline.undo.back();
    m_timeline.undo.pop_back();
    m_timeline.selectedKeys.clear();
    m_timeline.selectedEvent = -1;
    m_timeline.dirty = true;
}

void EditorUVE::RedoAnimationTimelineUVE() {
    if (m_timeline.redo.empty()) {
        return;
    }
    m_timeline.undo.push_back(m_timeline.clip);
    m_timeline.clip = m_timeline.redo.back();
    m_timeline.redo.pop_back();
    m_timeline.selectedKeys.clear();
    m_timeline.selectedEvent = -1;
    m_timeline.dirty = true;
}

void EditorUVE::SaveAnimationTimelineClipUVE() {
    if (m_timeline.clip == nullptr || !m_timeline.dirty) {
        return;
    }
    const std::filesystem::path path = m_services->GetAssetDatabaseUVE().ResolveUVE(m_timeline.clipGuid);
    if (Asset::SaveAnimationClipAssetUVE(*m_timeline.clip, path)) {
        m_timeline.dirty = false;
        m_timeline.status = "Saved " + path.filename().string();
    } else {
        m_timeline.status = "Could not save " + path.filename().string();
    }
}

void EditorUVE::InsertAnimationTimelineKeyUVE() {
    if (m_timeline.clip == nullptr) {
        return;
    }
    // The selected bone's track, else the clip's own object track.
    std::optional<std::string> chosen;
    if (FindClipTrackSamplesUVE(*m_timeline.clip, m_selectedSkeletonBone) != nullptr) {
        chosen = m_selectedSkeletonBone;
    } else if (!m_timeline.clip->samples.empty()) {
        chosen = std::string{};
    }
    if (!chosen.has_value()) {
        m_timeline.status = "Select a bone's row first, then press K.";
        return;
    }
    const std::string track = *chosen;
    const double rate = TimelineFrameRateUVE(m_timeline.clip);
    Asset::AnimationClipAssetUVE probe = *m_timeline.clip;
    if (!InsertClipKeyUVE(probe, track, m_timeline.timeSeconds, rate)) {
        m_timeline.status = "There is already a key there.";
        return;
    }
    const double at = m_timeline.timeSeconds;
    static_cast<void>(EditAnimationTimelineClipUVE(
        [&](Asset::AnimationClipAssetUVE& next) { static_cast<void>(InsertClipKeyUVE(next, track, at, rate)); }));
    m_timeline.selectedKeys = {ClipKeyUVE{track, std::round(at * rate) / rate}};
    m_timeline.status = "Key added to " + (track.empty() ? std::string{"Transform"} : track);
}

} // namespace UVE::Editor
