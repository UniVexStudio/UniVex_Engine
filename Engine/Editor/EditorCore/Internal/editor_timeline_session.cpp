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
constexpr std::size_t kMorphNameCacheSize = 64U;

} // namespace

void Editor::StopAnimationTimelinePreview() {
    m_timeline.playing = false;
    m_timelineOwnsKeys = false;
    // The blend shapes first, and unconditionally: a skin-less facial clip has no preview skeleton
    // at all, so tying the restore to one would leave a face frozen mid-word.
    StopAnimationTimelineMorphPreview();
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (m_timeline.player != Scene::kInvalidEntity && entityManager.IsAlive(m_timeline.player) &&
        entityManager.HasComponent<Scene::AnimationSequencerComponent>(m_timeline.player)) {
        auto& player = entityManager.GetComponent<Scene::AnimationSequencerComponent>(m_timeline.player);
        player.isPlaying = false;
        player.currentTimeSeconds = 0.0F;
        player.hasStartPose = false;
    }
    RestoreAnimationTimelineObject();
    m_timeline.player = Scene::kInvalidEntity;
    if (m_timeline.previewSkeleton == Scene::kInvalidEntity) {
        return;
    }
    // Hold the evaluated pose instead of popping to bind pose (pose.clear()).
    m_timeline.previewSkeleton = Scene::kInvalidEntity;
}

void Editor::StopAnimationTimelineMorphPreview() {
    m_timeline.morphTracksNotPlaying = 0U;
    if (m_timeline.morphPreview.empty()) {
        return;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    for (const AnimationTimelineState::MorphPreviewTarget& target : m_timeline.morphPreview) {
        if (target.entity == Scene::kInvalidEntity || !entityManager.IsAlive(target.entity) ||
            !entityManager.HasComponent<Scene::MorphTargetComponent>(target.entity)) {
            continue;
        }
        // Only the list this preview wrote: `enabled` belongs to the author and stays as it was.
        entityManager.GetComponent<Scene::MorphTargetComponent>(target.entity).weights = target.previousWeights;
    }
    m_timeline.morphPreview.clear();
}

std::span<const std::string> Editor::TimelineMorphTargetNames(const Scene::Entity entity) {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (entity == Scene::kInvalidEntity || !entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<Scene::MeshComponent>(entity)) {
        return {};
    }
    const Asset::AssetGuid meshGuid = entityManager.GetComponent<Scene::MeshComponent>(entity).meshGuid;
    if (meshGuid == Asset::AssetGuid{} || meshGuid == Asset::kInvalidAssetGuid) {
        return {};
    }
    for (const AnimationTimelineState::MorphTargetNames& cached : m_timeline.morphTargetNames) {
        if (cached.entity == entity && cached.meshGuid == meshGuid) {
            return cached.names;
        }
    }

    // A .uvmodel is read once per object per mesh, never per frame: the target NAMES are all this
    // needs, and a face mesh carries as many deltas as it does vertices.
    AnimationTimelineState::MorphTargetNames entry;
    entry.entity = entity;
    entry.meshGuid = meshGuid;
    Asset::MeshAsset mesh;
    const std::filesystem::path path = m_services->GetAssetDatabase().Resolve(meshGuid);
    if (!path.empty() && Asset::LoadMeshAsset(path, mesh)) {
        entry.names.reserve(mesh.morphTargets.size());
        for (const Asset::MeshMorphTarget& target : mesh.morphTargets) {
            entry.names.push_back(target.name);
        }
    }
    if (m_timeline.morphTargetNames.size() >= kMorphNameCacheSize) {
        m_timeline.morphTargetNames.erase(m_timeline.morphTargetNames.begin());
    }
    m_timeline.morphTargetNames.push_back(std::move(entry));
    return m_timeline.morphTargetNames.back().names;
}

void Editor::PreviewAnimationTimelineMorphs(const Asset::AnimationClipAsset& clip,
                                                  const Scene::Entity root) {
    if (!clip.HasMorphTracks() || root == Scene::kInvalidEntity) {
        StopAnimationTimelineMorphPreview();
        return;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();

    // The objects under the root, by name. The walk stops as soon as every name a track asks for
    // has an answer, and the first object in breadth-first order keeps a name two of them share -
    // the same rule the scene's own lookups follow.
    std::unordered_set<std::string> wanted;
    wanted.reserve(clip.morphWeights.size());
    for (const Asset::AnimationAssetMorphTrack& track : clip.morphWeights) {
        wanted.insert(track.node);
    }
    std::unordered_map<std::string, Scene::Entity> byName;
    std::vector<Scene::Entity> queue{root};
    for (std::size_t next = 0U; next < queue.size() && next < 4096U && byName.size() < wanted.size(); ++next) {
        const Scene::Entity entity = queue[next];
        if (entityManager.HasComponent<Scene::NameComponent>(entity)) {
            const std::string& name = entityManager.GetComponent<Scene::NameComponent>(entity).name;
            if (wanted.contains(name)) {
                byName.emplace(name, entity);
            }
        }
        const std::vector<Scene::Entity> children = sceneGraph.GetChildren(entityManager, entity);
        queue.insert(queue.end(), children.begin(), children.end());
    }

    TimelineMorphScene scene;
    scene.entityForNode = [&byName](const std::string_view node) -> std::optional<Scene::Entity> {
        const auto found = byName.find(std::string{node});
        return found != byName.end() ? std::optional<Scene::Entity>{found->second} : std::nullopt;
    };
    scene.targetNames = [this, &entityManager](const Scene::Entity entity) -> std::span<const std::string> {
        // Without the component there is nothing to write into, and the preview never adds one: a
        // preview looks at the scene, it does not author it. Reporting "no shapes" is exactly how
        // the resolve counts that track as unplayable.
        if (!entityManager.HasComponent<Scene::MorphTargetComponent>(entity)) {
            return {};
        }
        return TimelineMorphTargetNames(entity);
    };

    const TimelineMorphPreview preview = ResolveTimelineMorphPreview(clip, m_timeline.timeSeconds, scene);

    // A different set of objects than the one being driven means the clip or the scene moved under
    // the preview (an undo, a key edit, a renamed object). Put back what the old set held and
    // capture again from what is there now, so the authored weights are what gets restored.
    const bool sameTargets =
        m_timeline.morphPreview.size() == preview.writes.size() &&
        std::all_of(preview.writes.begin(), preview.writes.end(), [this](const TimelineMorphWrite& write) {
            return std::any_of(m_timeline.morphPreview.begin(), m_timeline.morphPreview.end(),
                               [&write](const AnimationTimelineState::MorphPreviewTarget& target) {
                                   return target.entity == write.entity;
                               });
        });
    if (!sameTargets) {
        StopAnimationTimelineMorphPreview();
        for (const TimelineMorphWrite& write : preview.writes) {
            AnimationTimelineState::MorphPreviewTarget target;
            target.entity = write.entity;
            target.previousWeights = entityManager.GetComponent<Scene::MorphTargetComponent>(write.entity).weights;
            m_timeline.morphPreview.push_back(std::move(target));
        }
    }
    m_timeline.morphTracksNotPlaying = preview.unresolvedTracks;

    for (const TimelineMorphWrite& write : preview.writes) {
        if (!entityManager.IsAlive(write.entity) ||
            !entityManager.HasComponent<Scene::MorphTargetComponent>(write.entity)) {
            continue;
        }
        entityManager.GetComponent<Scene::MorphTargetComponent>(write.entity).weights = write.weights;
    }
}

Editor::AnimationTimelineView Editor::SyncAnimationTimeline() {
    AnimationTimelineView view;
    // Last call's replaced clip is no longer read by anything.
    m_timeline.retired.reset();
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();

    // ---- Which player: the selected one, else the one shown last, else the first with a clip -----
    for (const Scene::Entity object : CollectEntityEditorObjects()) {
        if (entityManager.HasComponent<Scene::AnimationSequencerComponent>(object)) {
            view.players.push_back(object);
        }
    }
    if (view.players.empty()) {
        if (m_timeline.player != Scene::kInvalidEntity || !m_timeline.morphPreview.empty()) {
            StopAnimationTimelinePreview();
        }
        return view;
    }
    if (std::ranges::find(view.players, m_selectedEntity) != view.players.end()) {
        m_timeline.player = m_selectedEntity;
    } else if (std::ranges::find(view.players, m_timeline.player) == view.players.end()) {
        // The first one with a clip: an entity's own empty player should not hide a working one.
        m_timeline.player = view.players.front();
        for (const Scene::Entity candidate : view.players) {
            if (entityManager.GetComponent<Scene::AnimationSequencerComponent>(candidate).clip != Asset::AssetGuid{}) {
                m_timeline.player = candidate;
                break;
            }
        }
    }
    view.player = m_timeline.player;
    auto& player = entityManager.GetComponent<Scene::AnimationSequencerComponent>(view.player);
    const Scene::AnimationDriverComponent driver =
        entityManager.HasComponent<Scene::AnimationDriverComponent>(view.player)
            ? entityManager.GetComponent<Scene::AnimationDriverComponent>(view.player)
            : Scene::AnimationDriverComponent{};

    // ---- The clip, reloaded when the player's clip changes ----------------------------------------
    if (player.clip != m_timeline.clipGuid) {
        // Switching away keeps the work: edits not yet saved are written to their clip first.
        if (m_timeline.dirty && m_timeline.clip != nullptr) {
            const std::filesystem::path previous = m_services->GetAssetDatabase().Resolve(m_timeline.clipGuid);
            if (!previous.empty() && Asset::SaveAnimationClipAsset(*m_timeline.clip, previous)) {
                m_timeline.status = "Saved your edits to " + previous.filename().string();
            }
        }
        const Scene::Entity keep = m_timeline.player;
        StopAnimationTimelinePreview();
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
        m_timeline.loop = player.loopMode != Scene::AnimationLoopMode::Once;
        player.isPlaying = false;
        player.currentTimeSeconds = 0.0F;
        player.hasStartPose = false;
        if (player.clip != Asset::AssetGuid{}) {
            auto loaded = std::make_shared<Asset::AnimationClipAsset>();
            const std::filesystem::path path = m_services->GetAssetDatabase().Resolve(player.clip);
            if (Asset::LoadAnimationClipAsset(path, *loaded)) {
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
    if (view.previewRoot == Scene::kInvalidEntity || !entityManager.IsAlive(view.previewRoot)) {
        view.previewRoot = entityManager.HasComponent<Scene::HierarchyComponent>(view.player)
                               ? entityManager.GetComponent<Scene::HierarchyComponent>(view.player).parent
                               : Scene::kInvalidEntity;
    }
    std::vector<Scene::Entity> queue;
    if (view.previewRoot != Scene::kInvalidEntity && entityManager.IsAlive(view.previewRoot)) {
        queue.push_back(view.previewRoot);
    }
    for (std::size_t next = 0U; next < queue.size() && next < 4096U; ++next) {
        if (entityManager.HasComponent<Scene::Skeleton3DComponent>(queue[next])) {
            view.skeleton = queue[next];
            break;
        }
        const std::vector<Scene::Entity> children = sceneGraph.GetChildren(entityManager, queue[next]);
        queue.insert(queue.end(), children.begin(), children.end());
    }

    view.clip = m_timeline.clip.get();
    if (view.clip != nullptr) {
        view.durationSeconds = std::max(view.clip->durationSeconds, 1.0e-3);
        view.frameRate = EstimateTimelineFrameRate(*view.clip);
    }
    return view;
}

void Editor::StepAnimationTimeline(const double deltaSeconds) {
    const AnimationTimelineView view = SyncAnimationTimeline();
    if (view.player == Scene::kInvalidEntity) {
        return;
    }
    if (view.clip == nullptr) {
        if (m_timeline.previewSkeleton != Scene::kInvalidEntity || !m_timeline.morphPreview.empty()) {
            const Scene::Entity keep = m_timeline.player;
            StopAnimationTimelinePreview();
            m_timeline.player = keep;
        }
        return;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    auto& player = entityManager.GetComponent<Scene::AnimationSequencerComponent>(view.player);
    const Scene::AnimationDriverComponent driver =
        entityManager.HasComponent<Scene::AnimationDriverComponent>(view.player)
            ? entityManager.GetComponent<Scene::AnimationDriverComponent>(view.player)
            : Scene::AnimationDriverComponent{};
    const double duration = view.durationSeconds;
    if (m_timeline.playing) {
        const double speed = std::isfinite(player.speed) && player.speed != 0.0F ? player.speed : 1.0;
        m_timeline.timeSeconds += deltaSeconds * speed * static_cast<double>(driver.speedScale) * m_timeline.playbackRate;
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
        m_timeline.previewSkeleton = Scene::kInvalidEntity;
    }
    if (view.clip->IsSkeletal() && view.skeleton != Scene::kInvalidEntity) {
        auto& posed = entityManager.GetComponent<Scene::Skeleton3DComponent>(view.skeleton);
        if (Scene::PoseSkeletonAtTime(*view.clip, m_timeline.timeSeconds, posed, driver)) {
            m_timeline.previewSkeleton = view.skeleton;
        }
    }
    PreviewAnimationTimelineMorphs(*view.clip, view.previewRoot);
    PreviewAnimationTimelineObject(*view.clip);
}

namespace {

[[nodiscard]] bool SamePose(const Scene::TransformComponent& a, const Scene::TransformComponent& b) noexcept {
    const auto near = [](const float x, const float y) { return std::abs(x - y) <= 1.0e-5F; };
    return near(a.localPosition.x, b.localPosition.x) && near(a.localPosition.y, b.localPosition.y) &&
           near(a.localPosition.z, b.localPosition.z) && near(a.localScale.x, b.localScale.x) && near(a.localScale.y, b.localScale.y) &&
           near(a.localScale.z, b.localScale.z) && near(a.localRotation.x, b.localRotation.x) &&
           near(a.localRotation.y, b.localRotation.y) && near(a.localRotation.z, b.localRotation.z) &&
           near(a.localRotation.w, b.localRotation.w);
}

/// A key's pose as the object's transform (its authored Euler angles follow the rotation).
void WritePoseToTransform(const Asset::AnimationAssetPose& pose, Scene::TransformComponent& transform) {
    transform.localPosition = pose.position;
    Math::Quaternion rotation{};
    if (Math::TryNormalize(pose.rotation, rotation)) {
        transform.localRotation = rotation;
        Math::Vector3 euler{};
        if (Math::TryToEuler(rotation, euler)) {
            transform.localEulerRadians = euler;
        }
    }
    transform.localScale = pose.scale;
}

/// Places a key with `pose` on the clip's Transform track at `seconds` (snapped to the clip's
/// frames), replacing a key already there. The clip grows to reach a key past its end.
void SetObjectKey(Asset::AnimationClipAsset& clip, const double seconds, const double frameRate,
                     const Asset::AnimationAssetPose& pose, const std::string& track = std::string{}) {
    const double rate = frameRate > 0.0 ? frameRate : 30.0;
    const double time = std::max(0.0, std::round(seconds * rate) / rate);
    // The clip's own track, another object's (started when it has none), or a bone's.
    std::vector<Asset::AnimationAssetSample>* list = &clip.samples;
    if (!track.empty()) {
        list = FindClipTrackSamples(clip, track);
        if (list == nullptr) {
            const bool object = track.front() == kClipObjectTrackPrefix;
            auto& tracks = object ? clip.objects : clip.bones;
            Asset::AnimationAssetBoneTrack added;
            added.bone = object ? track.substr(1U) : track;
            tracks.push_back(std::move(added));
            list = &tracks.back().samples;
        }
    }
    auto& samples = *list;
    const auto at = std::find_if(samples.begin(), samples.end(), [time, rate](const Asset::AnimationAssetSample& s) {
        return std::abs(s.timeSeconds - time) < 0.5 / rate;
    });
    if (at != samples.end()) {
        at->pose = pose;
    } else {
        Asset::AnimationAssetSample sample;
        sample.timeSeconds = time;
        sample.pose = pose;
        samples.insert(std::upper_bound(samples.begin(), samples.end(), time,
                                        [](const double t, const Asset::AnimationAssetSample& s) { return t < s.timeSeconds; }),
                       sample);
    }
    clip.durationSeconds = std::max(clip.durationSeconds, time);
}

} // namespace

Scene::Entity Editor::AnimationTimelineObjectTarget() const {
    const Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const Scene::Entity player = m_timeline.player;
    if (player == Scene::kInvalidEntity || !entityManager.IsAlive(player)) {
        return Scene::kInvalidEntity;
    }
    // The same object the engine's player moves: the driver's target, else the player's parent.
    Scene::Entity target = entityManager.HasComponent<Scene::AnimationDriverComponent>(player)
                                  ? entityManager.GetComponent<Scene::AnimationDriverComponent>(player).target
                                  : Scene::kInvalidEntity;
    if (target == Scene::kInvalidEntity || !entityManager.IsAlive(target)) {
        target = entityManager.HasComponent<Scene::HierarchyComponent>(player)
                     ? entityManager.GetComponent<Scene::HierarchyComponent>(player).parent
                     : Scene::kInvalidEntity;
    }
    if (target == Scene::kInvalidEntity || target == player || !entityManager.IsAlive(target) ||
        !entityManager.HasComponent<Scene::TransformComponent>(target)) {
        return Scene::kInvalidEntity;
    }
    return target;
}

std::optional<std::string> Editor::AnimationTimelineTrackOf(const Scene::Entity entity) const {
    const Scene::Entity target = AnimationTimelineObjectTarget();
    if (entity == Scene::kInvalidEntity || target == Scene::kInvalidEntity) {
        return std::nullopt;
    }
    if (entity == target) {
        return std::string{};
    }
    const Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.IsAlive(entity) || entity == m_timeline.player ||
        !entityManager.HasComponent<Scene::TransformComponent>(entity) ||
        !entityManager.HasComponent<Scene::NameComponent>(entity)) {
        return std::nullopt;
    }
    const std::string& name = entityManager.GetComponent<Scene::NameComponent>(entity).name;
    if (name.empty()) {
        return std::nullopt;
    }
    // Under the animated object, at any depth.
    Scene::Entity up = entity;
    for (int depth = 0; depth < 256; ++depth) {
        up = entityManager.HasComponent<Scene::HierarchyComponent>(up)
                 ? entityManager.GetComponent<Scene::HierarchyComponent>(up).parent
                 : Scene::kInvalidEntity;
        if (up == Scene::kInvalidEntity || !entityManager.IsAlive(up)) {
            return std::nullopt;
        }
        if (up == target) {
            return std::string(1, kClipObjectTrackPrefix) + name;
        }
    }
    return std::nullopt;
}

bool Editor::CanKeyAnimationTimelineObject(const Scene::Entity entity) const {
    return m_timeline.panelShown && m_timeline.clip != nullptr && AnimationTimelineTrackOf(entity).has_value();
}

bool Editor::HasAnimationTimelineObjectKeyAtPlayhead() const {
    if (m_timeline.clip == nullptr) {
        return false;
    }
    const std::string track = AnimationTimelineTrackOf(GetSelectedEntity()).value_or(std::string{});
    const std::vector<Asset::AnimationAssetSample>* const samples =
        track.empty() ? &m_timeline.clip->samples : FindClipTrackSamples(*m_timeline.clip, track);
    if (samples == nullptr) {
        return false;
    }
    const double rate = std::max(1.0, EstimateTimelineFrameRate(*m_timeline.clip));
    const double time = m_timeline.timeSeconds;
    return std::ranges::any_of(*samples,
                               [time, rate](const Asset::AnimationAssetSample& s) { return std::abs(s.timeSeconds - time) < 0.5 / rate; });
}

bool Editor::KeyAnimationTimelineObject() {
    const Scene::Entity selected = GetSelectedEntity();
    return KeyAnimationTimelineObject(AnimationTimelineTrackOf(selected).has_value() ? selected
                                                                                            : AnimationTimelineObjectTarget());
}

bool Editor::KeyAnimationTimelineObject(const Scene::Entity entity) {
    const std::optional<std::string> track = AnimationTimelineTrackOf(entity);
    if (m_timeline.clip == nullptr || !track.has_value()) {
        m_timeline.status = "Nothing to key: select the animated object or an object under it (set the Animation Driver's Target).";
        return false;
    }
    const Scene::TransformComponent now = m_services->GetEntityManager().GetComponent<Scene::TransformComponent>(entity);
    Asset::AnimationAssetPose pose;
    pose.position = now.localPosition;
    pose.rotation = now.localRotation;
    pose.scale = now.localScale;
    const double at = m_timeline.timeSeconds;
    const double rate = EstimateTimelineFrameRate(*m_timeline.clip);
    const std::string name = *track;
    const bool changed = EditAnimationTimelineClip([&](Asset::AnimationClipAsset& next) { SetObjectKey(next, at, rate, pose, name); });
    if (changed) {
        // What is on the object now is what the clip says here: no edit left to detect.
        if (name.empty()) {
            m_timeline.objectLast = now;
            m_timeline.objectLastTime = at;
            m_timeline.objectLastClip = m_timeline.clip.get();
        } else {
            m_timeline.childrenLastClip = nullptr;   // the preview takes the new track in
            bool known = false;
            for (AnimationTimelineState::ChildPose& child : m_timeline.children) {
                if (child.entity == entity) {
                    child.last = now;
                    known = true;
                }
            }
            if (!known) {
                m_timeline.children.push_back(AnimationTimelineState::ChildPose{entity, now, now});
            }
        }
        m_timeline.selectedKeys = {ClipKey{name, std::round(at * rate) / rate}};
        m_timeline.status = (name.empty() ? std::string("Transform") : name.substr(1U)) + " keyed at " +
                            std::to_string(static_cast<long>(std::lround(at * rate))) + " (frame)";
    }
    return changed;
}

bool Editor::KeyAnimationTimelineBone(const std::string& bone) {
    if (m_timeline.clip == nullptr || bone.empty()) {
        return false;
    }
    if (FindClipTrackSamples(*m_timeline.clip, bone) != nullptr) {
        InsertAnimationTimelineKey();
        return true;
    }
    // No track yet: started from the bone as it is posed now.
    const AnimationTimelineView view = SyncAnimationTimeline();
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (view.skeleton == Scene::kInvalidEntity || !entityManager.IsAlive(view.skeleton) ||
        !entityManager.HasComponent<Scene::Skeleton3DComponent>(view.skeleton)) {
        m_timeline.status = "No skeleton to key " + bone + " on.";
        return false;
    }
    const Scene::Skeleton3DComponent& skeleton = entityManager.GetComponent<Scene::Skeleton3DComponent>(view.skeleton);
    const std::vector<Scene::SkeletonBonePose> posed = Scene::GetSkeletonCurrentPose(skeleton);
    std::optional<std::size_t> index;
    for (std::size_t i = 0U; i < skeleton.bones.size() && i < posed.size(); ++i) {
        if (skeleton.bones[i].name == bone) {
            index = i;
            break;
        }
    }
    if (!index.has_value()) {
        m_timeline.status = "The skeleton has no bone " + bone + ".";
        return false;
    }
    Asset::AnimationAssetPose pose;
    pose.position = posed[*index].position;
    pose.rotation = posed[*index].rotation;
    pose.scale = posed[*index].scale;
    const double at = m_timeline.timeSeconds;
    const double rate = EstimateTimelineFrameRate(*m_timeline.clip);
    const bool changed = EditAnimationTimelineClip([&](Asset::AnimationClipAsset& next) { SetObjectKey(next, at, rate, pose, bone); });
    if (changed) {
        m_timeline.selectedKeys = {ClipKey{bone, std::round(at * rate) / rate}};
        m_timeline.status = bone + " keyed at " + std::to_string(static_cast<long>(std::lround(at * rate))) + " (frame)";
    }
    return changed;
}

void Editor::SetAnimationTimelineRecording(const bool recording) {
    m_timeline.recording = recording;
    m_timeline.status = recording ? "Recording: move the animated object to key it at the playhead" : "Recording stopped";
}

void Editor::RestoreAnimationTimelineObject() {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (m_timeline.objectApplied && m_timeline.objectTarget != Scene::kInvalidEntity && entityManager.IsAlive(m_timeline.objectTarget) &&
        entityManager.HasComponent<Scene::TransformComponent>(m_timeline.objectTarget)) {
        entityManager.GetComponent<Scene::TransformComponent>(m_timeline.objectTarget) = m_timeline.objectRest;
        if (entityManager.HasComponent<Scene::WorldTransformComponent>(m_timeline.objectTarget)) {
            entityManager.GetComponent<Scene::WorldTransformComponent>(m_timeline.objectTarget).dirty = true;
        }
    }
    for (const AnimationTimelineState::ChildPose& child : m_timeline.children) {
        if (entityManager.IsAlive(child.entity) && entityManager.HasComponent<Scene::TransformComponent>(child.entity)) {
            entityManager.GetComponent<Scene::TransformComponent>(child.entity) = child.rest;
            if (entityManager.HasComponent<Scene::WorldTransformComponent>(child.entity)) {
                entityManager.GetComponent<Scene::WorldTransformComponent>(child.entity).dirty = true;
            }
        }
    }
    m_timeline.children.clear();
    m_timeline.childrenLastTime = -1.0;
    m_timeline.childrenLastClip = nullptr;
    m_timeline.objectApplied = false;
    m_timeline.objectTarget = Scene::kInvalidEntity;
    m_timeline.objectLastTime = -1.0;
    m_timeline.objectLastClip = nullptr;
}

void Editor::PreviewAnimationTimelineObject(const Asset::AnimationClipAsset& clip) {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const Scene::Entity target = AnimationTimelineObjectTarget();
    if (target != m_timeline.objectTarget) {
        RestoreAnimationTimelineObject();
        m_timeline.objectTarget = target;
    }
    if (target == Scene::kInvalidEntity) {
        return;
    }
    PreviewAnimationTimelineChildren(clip);
    Scene::TransformComponent& transform = entityManager.GetComponent<Scene::TransformComponent>(target);
    if (!m_timeline.objectApplied) {
        m_timeline.objectRest = transform;
    }
    // Moved by hand since the preview last wrote it (the gizmo, the Details fields): recording keys
    // it at the playhead; otherwise it is the object's own new pose, kept as it is.
    const bool moved = m_timeline.objectApplied && !SamePose(transform, m_timeline.objectLast);
    if (moved && !m_timeline.playing) {
        if (m_timeline.recording) {
            static_cast<void>(KeyAnimationTimelineObject());
        } else {
            m_timeline.objectRest = transform;
            m_timeline.objectLast = transform;
        }
        return;
    }
    if (clip.samples.empty()) {
        // No keys yet: nothing to show, but watched, so the first move under Record keys it.
        m_timeline.objectApplied = true;
        m_timeline.objectLast = transform;
        return;
    }
    // Written again only when it would change: the playhead moved, the keys changed, or playing.
    if (m_timeline.objectApplied && !m_timeline.playing && m_timeline.objectLastTime == m_timeline.timeSeconds &&
        m_timeline.objectLastClip == &clip) {
        return;
    }
    const Core::TransformPose sampled = Scene::SampleAnimationTrack(clip.samples, m_timeline.timeSeconds);
    Asset::AnimationAssetPose pose;
    pose.position = sampled.position;
    pose.rotation = sampled.rotation;
    pose.scale = sampled.scale;
    WritePoseToTransform(pose, transform);
    if (entityManager.HasComponent<Scene::WorldTransformComponent>(target)) {
        entityManager.GetComponent<Scene::WorldTransformComponent>(target).dirty = true;
    }
    m_timeline.objectApplied = true;
    m_timeline.objectLast = transform;
    m_timeline.objectLastTime = m_timeline.timeSeconds;
    m_timeline.objectLastClip = &clip;
}

void Editor::PreviewAnimationTimelineChildren(const Asset::AnimationClipAsset& clip) {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    // Under Record, the selected object under the animated one is watched even before it has a
    // track, so its first move keys it.
    if (m_timeline.recording) {
        const Scene::Entity selected = GetSelectedEntity();
        if (AnimationTimelineTrackOf(selected).value_or(std::string{}).size() > 0U &&
            std::none_of(m_timeline.children.begin(), m_timeline.children.end(),
                         [selected](const AnimationTimelineState::ChildPose& c) { return c.entity == selected; })) {
            const Scene::TransformComponent& now = entityManager.GetComponent<Scene::TransformComponent>(selected);
            m_timeline.children.push_back(AnimationTimelineState::ChildPose{selected, now, now});
        }
    }
    // A hand move of a tracked object (gizmo, Details) while paused: Record keys it at the
    // playhead; otherwise it is the object's own new pose, kept.
    bool keyedAny = false;
    for (AnimationTimelineState::ChildPose& child : m_timeline.children) {
        if (!entityManager.IsAlive(child.entity) || !entityManager.HasComponent<Scene::TransformComponent>(child.entity)) {
            continue;
        }
        const Scene::TransformComponent& now = entityManager.GetComponent<Scene::TransformComponent>(child.entity);
        if (!m_timeline.playing && !SamePose(now, child.last)) {
            if (m_timeline.recording) {
                const Scene::Entity moved = child.entity;
                static_cast<void>(KeyAnimationTimelineObject(moved));
                keyedAny = true;
                break;   // the list may have changed
            }
            child.rest = now;
            child.last = now;
        }
    }
    if (keyedAny || clip.objects.empty()) {
        return;
    }
    if (!m_timeline.playing && m_timeline.childrenLastTime == m_timeline.timeSeconds && m_timeline.childrenLastClip == &clip) {
        return;
    }
    // Each tracked object under the animated one, found by name.
    const Scene::Entity target = AnimationTimelineObjectTarget();
    std::vector<Scene::Entity> queue{target};
    for (std::size_t next = 0U; next < queue.size() && next < 4096U; ++next) {
        const Scene::Entity candidate = queue[next];
        if (next > 0U && entityManager.HasComponent<Scene::NameComponent>(candidate) &&
            entityManager.HasComponent<Scene::TransformComponent>(candidate)) {
            const std::string& name = entityManager.GetComponent<Scene::NameComponent>(candidate).name;
            for (const Asset::AnimationAssetBoneTrack& track : clip.objects) {
                if (track.bone != name || track.samples.empty()) {
                    continue;
                }
                Scene::TransformComponent& transform = entityManager.GetComponent<Scene::TransformComponent>(candidate);
                auto known = std::find_if(m_timeline.children.begin(), m_timeline.children.end(),
                                          [candidate](const AnimationTimelineState::ChildPose& c) { return c.entity == candidate; });
                if (known == m_timeline.children.end()) {
                    m_timeline.children.push_back(AnimationTimelineState::ChildPose{candidate, transform, transform});
                    known = std::prev(m_timeline.children.end());
                }
                const Core::TransformPose sampled = Scene::SampleAnimationTrack(track.samples, m_timeline.timeSeconds);
                Asset::AnimationAssetPose pose;
                pose.position = sampled.position;
                pose.rotation = sampled.rotation;
                pose.scale = sampled.scale;
                WritePoseToTransform(pose, transform);
                if (entityManager.HasComponent<Scene::WorldTransformComponent>(candidate)) {
                    entityManager.GetComponent<Scene::WorldTransformComponent>(candidate).dirty = true;
                }
                known->last = transform;
                break;
            }
        }
        const std::vector<Scene::Entity> children = m_services->GetSceneGraph().GetChildren(entityManager, candidate);
        queue.insert(queue.end(), children.begin(), children.end());
    }
    m_timeline.childrenLastTime = m_timeline.timeSeconds;
    m_timeline.childrenLastClip = &clip;
}

namespace {

[[nodiscard]] Scene::AnimationSequencerComponent* TimelinePlayer(Scene::IEntityManager& entityManager,
                                                                      const Scene::Entity player) {
    if (player == Scene::kInvalidEntity || !entityManager.IsAlive(player) ||
        !entityManager.HasComponent<Scene::AnimationSequencerComponent>(player)) {
        return nullptr;
    }
    return &entityManager.GetComponent<Scene::AnimationSequencerComponent>(player);
}

[[nodiscard]] double TimelineDuration(const std::shared_ptr<const Asset::AnimationClipAsset>& clip) {
    return clip != nullptr ? std::max(clip->durationSeconds, 1.0e-3) : 1.0;
}

[[nodiscard]] double TimelineFrameRate(const std::shared_ptr<const Asset::AnimationClipAsset>& clip) {
    return clip != nullptr ? EstimateTimelineFrameRate(*clip) : 30.0;
}

} // namespace

void Editor::SetAnimationTimelineTime(const double seconds) {
    m_timeline.playing = false;
    m_timeline.timeSeconds = std::clamp(seconds, 0.0, TimelineDuration(m_timeline.clip));
    if (auto* player = TimelinePlayer(m_services->GetEntityManager(), m_timeline.player)) {
        player->isPlaying = false;
        player->currentTimeSeconds = static_cast<float>(m_timeline.timeSeconds);
    }
}

void Editor::ToggleAnimationTimelinePlay() {
    if (m_timeline.clip == nullptr) {
        return;
    }
    m_timeline.playing = !m_timeline.playing;
    if (m_timeline.playing && !m_timeline.loop && m_timeline.timeSeconds >= TimelineDuration(m_timeline.clip)) {
        m_timeline.timeSeconds = 0.0;
    }
    if (auto* player = TimelinePlayer(m_services->GetEntityManager(), m_timeline.player)) {
        player->isPlaying = m_timeline.playing;
        player->currentTimeSeconds = static_cast<float>(m_timeline.timeSeconds);
    }
}

void Editor::StopAnimationTimelinePlayback() {
    SetAnimationTimelineTime(0.0);
}

void Editor::StepAnimationTimelineFrame(const int frames) {
    const double rate = TimelineFrameRate(m_timeline.clip);
    const double frame = std::round(m_timeline.timeSeconds * rate) + static_cast<double>(frames);
    SetAnimationTimelineTime(frame / rate);
}

void Editor::SetAnimationTimelineLoop(const bool loop) {
    m_timeline.loop = loop;
    if (auto* player = TimelinePlayer(m_services->GetEntityManager(), m_timeline.player)) {
        player->loopMode = loop ? Scene::AnimationLoopMode::Loop : Scene::AnimationLoopMode::Once;
    }
}

bool Editor::EditAnimationTimelineClip(const std::function<void(Asset::AnimationClipAsset&)>& change) {
    if (m_timeline.clip == nullptr || !change) {
        return false;
    }
    auto next = std::make_shared<Asset::AnimationClipAsset>(*m_timeline.clip);
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

void Editor::UndoAnimationTimeline() {
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

void Editor::RedoAnimationTimeline() {
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

void Editor::SaveAnimationTimelineClip() {
    if (m_timeline.clip == nullptr || !m_timeline.dirty) {
        return;
    }
    const std::filesystem::path path = m_services->GetAssetDatabase().Resolve(m_timeline.clipGuid);
    if (Asset::SaveAnimationClipAsset(*m_timeline.clip, path)) {
        m_timeline.dirty = false;
        m_timeline.status = "Saved " + path.filename().string();
    } else {
        m_timeline.status = "Could not save " + path.filename().string();
    }
}

void Editor::InsertAnimationTimelineKey() {
    if (m_timeline.clip == nullptr) {
        return;
    }
    // The selected bone's track, else the clip's own object track.
    std::optional<std::string> chosen;
    if (FindClipTrackSamples(*m_timeline.clip, m_selectedSkeletonBone) != nullptr) {
        chosen = m_selectedSkeletonBone;
    } else if (!m_selectedSkeletonBone.empty() && m_selectedSkeletonBone.front() != kClipObjectTrackPrefix) {
        static_cast<void>(KeyAnimationTimelineBone(m_selectedSkeletonBone));   // its track starts here
        return;
    } else if (AnimationTimelineTrackOf(GetSelectedEntity()).value_or(std::string{}).size() > 0U) {
        static_cast<void>(KeyAnimationTimelineObject(GetSelectedEntity()));   // an object under the animated one
        return;
    } else if (!m_timeline.clip->samples.empty()) {
        chosen = std::string{};
    }
    if (!chosen.has_value()) {
        m_timeline.status = "Select a bone's row first, then press K.";
        return;
    }
    const std::string track = *chosen;
    const double rate = TimelineFrameRate(m_timeline.clip);
    Asset::AnimationClipAsset probe = *m_timeline.clip;
    if (!InsertClipKey(probe, track, m_timeline.timeSeconds, rate)) {
        m_timeline.status = "There is already a key there.";
        return;
    }
    const double at = m_timeline.timeSeconds;
    static_cast<void>(EditAnimationTimelineClip(
        [&](Asset::AnimationClipAsset& next) { static_cast<void>(InsertClipKey(next, track, at, rate)); }));
    m_timeline.selectedKeys = {ClipKey{track, std::round(at * rate) / rate}};
    m_timeline.status = "Key added to " + (track.empty() ? std::string{"Transform"} : track);
}

} // namespace UVE::Editor
