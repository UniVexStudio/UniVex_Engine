// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// The Motion Montage Editor's document: an open `.uvmontage`, its undo, and its preview - the character
// the montage is previewed on, in a scene of its own beside the level (and an open entity), posed
// at the playhead every frame. The PRISM window (shell/montage/editor_montage_editor) draws it.
#include "editor.h"

#include "animation_clip_asset.h"
#include "motion_montage.h"
#include "motion_montage_editing.h"
#include "document_scene.h"
#include "i_asset_database.h"
#include "i_entity_manager.h"
#include "i_prefab_system.h"
#include "i_project_file_index.h"
#include "i_scene_graph.h"
#include "skeleton_3d.h"
#include "transform_component.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <system_error>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

/// The skeleton under `root`, breadth first (the one nearest the root wins).
[[nodiscard]] Scene::Entity FindSkeletonUnder(Core::EngineServices& services, const Scene::Entity root) {
    Scene::IEntityManager& em = services.GetEntityManager();
    if (root == Scene::kInvalidEntity || !em.IsAlive(root)) {
        return Scene::kInvalidEntity;
    }
    std::vector<Scene::Entity> queue{root};
    for (std::size_t next = 0U; next < queue.size() && next < 4096U; ++next) {
        if (em.HasComponent<Scene::Skeleton3DComponent>(queue[next])) {
            return queue[next];
        }
        const std::vector<Scene::Entity> children = services.GetSceneGraph().GetChildren(em, queue[next]);
        queue.insert(queue.end(), children.begin(), children.end());
    }
    return Scene::kInvalidEntity;
}

} // namespace

const Asset::AnimationClipAsset* Editor::MontageClip(const Asset::AssetGuid guid) {
    if (guid == Asset::kInvalidAssetGuid) {
        return nullptr;
    }
    auto it = m_montageClips.find(guid.value);
    if (it == m_montageClips.end()) {
        std::optional<Asset::AnimationClipAsset> loaded;
        const std::filesystem::path path = m_services->GetAssetDatabase().Resolve(guid);
        Asset::AnimationClipAsset clip;
        if (!path.empty() && Asset::LoadAnimationClipAsset(path, clip)) {
            loaded = std::move(clip);
        }
        it = m_montageClips.emplace(guid.value, std::move(loaded)).first;
    }
    return it->second.has_value() ? &*it->second : nullptr;
}

bool Editor::OpenMontageEditor(const std::filesystem::path& path) {
    if (m_montageEdit.has_value() && m_montageEdit->path == path) {
        m_montageEdit->focusPending = true;
        return true;
    }
    if (IsMontageEditorDirty()) {
        m_contentStatusMessage = "Save or close " + m_montageEdit->path.filename().string() + " first.";
        return false;
    }
    Asset::MotionMontageAsset montage;
    if (!Asset::LoadMotionMontageAsset(path, montage)) {
        m_contentStatusMessage = "Could not read " + path.filename().string() + " as an motion montage.";
        return false;
    }
    CloseMontageEditor();
    MontageEditSession session;
    session.path = path;
    session.guid = m_services->GetAssetDatabase().Register(path);
    session.montage = montage;
    session.saved = std::move(montage);
    session.overlay.studioView = true;
    session.overlay.entityAuthoringView = true;
    m_montageEdit = std::move(session);
    if (m_documentSceneFactory) {
        m_montageScene = m_documentSceneFactory();
    }
    RebuildMontagePreview();
    return true;
}

bool Editor::SaveMontageEditor() {
    if (!m_montageEdit.has_value()) {
        return false;
    }
    if (!Asset::SaveMotionMontageAsset(m_montageEdit->montage, m_montageEdit->path)) {
        m_montageEdit->status = "Could not save " + m_montageEdit->path.filename().string() + ".";
        return false;
    }
    m_montageEdit->saved = m_montageEdit->montage;
    m_montageEdit->status = "Saved " + m_montageEdit->path.filename().string() + ".";
    m_projectFileSnapshotInitialized = false;
    RefreshProjectFileIndex();
    return true;
}

void Editor::CloseMontageEditor() {
    if (m_montageScene != nullptr && m_documentSceneClosing) {
        m_documentSceneClosing(m_montageScene->GetServices());
    }
    m_montageScene.reset();
    m_montageEdit.reset();
    m_montageClips.clear();
}

void Editor::EditMontage(const std::function<void(Asset::MotionMontageAsset&)>& edit) {
    if (!m_montageEdit.has_value() || !edit) {
        return;
    }
    Asset::MotionMontageAsset changed = m_montageEdit->montage;
    edit(changed);
    if (changed == m_montageEdit->montage) {
        return;
    }
    if (!Asset::IsMotionMontageAssetValid(changed)) {
        m_montageEdit->status = "That would make the motion montage invalid.";
        return;
    }
    const bool previewChanged = !(changed.previewEntity == m_montageEdit->montage.previewEntity) ||
                                (m_montageEdit->montage.segments.empty() != changed.segments.empty());
    m_montageEdit->undo.push_back(m_montageEdit->montage);
    if (m_montageEdit->undo.size() > 200U) {
        m_montageEdit->undo.erase(m_montageEdit->undo.begin());
    }
    m_montageEdit->redo.clear();
    m_montageEdit->montage = std::move(changed);
    m_montageEdit->playhead = std::clamp(m_montageEdit->playhead, 0.0, Asset::MontageLength(m_montageEdit->montage));
    if (previewChanged) {
        RebuildMontagePreview();
    }
}

bool Editor::UndoMontageEdit() {
    if (!m_montageEdit.has_value() || m_montageEdit->undo.empty()) {
        return false;
    }
    m_montageEdit->redo.push_back(m_montageEdit->montage);
    m_montageEdit->montage = m_montageEdit->undo.back();
    m_montageEdit->undo.pop_back();
    RebuildMontagePreview();
    return true;
}

bool Editor::RedoMontageEdit() {
    if (!m_montageEdit.has_value() || m_montageEdit->redo.empty()) {
        return false;
    }
    m_montageEdit->undo.push_back(m_montageEdit->montage);
    m_montageEdit->montage = m_montageEdit->redo.back();
    m_montageEdit->redo.pop_back();
    RebuildMontagePreview();
    return true;
}

void Editor::SetMontagePreviewEntity(const Asset::AssetGuid guid) {
    EditMontage([guid](Asset::MotionMontageAsset& m) { m.previewEntity = guid; });
}

void Editor::RebuildMontagePreview() {
    if (!m_montageEdit.has_value() || m_montageScene == nullptr) {
        return;
    }
    MontageEditSession& session = *m_montageEdit;
    Core::EngineServices& services = m_montageScene->GetServices();
    Scene::IEntityManager& em = services.GetEntityManager();
    // The preview is rebuilt whole: what was there is gone.
    if (session.previewRoot != Scene::kInvalidEntity && em.IsAlive(session.previewRoot)) {
        std::vector<Scene::Entity> doomed{session.previewRoot};
        for (std::size_t i = 0U; i < doomed.size(); ++i) {
            const std::vector<Scene::Entity> children = services.GetSceneGraph().GetChildren(em, doomed[i]);
            doomed.insert(doomed.end(), children.begin(), children.end());
        }
        for (auto it = doomed.rbegin(); it != doomed.rend(); ++it) {
            if (em.IsAlive(*it)) {
                em.DestroyEntity(*it);
            }
        }
    }
    session.previewRoot = Scene::kInvalidEntity;
    session.previewSkeleton = Scene::kInvalidEntity;

    if (session.montage.previewEntity != Asset::kInvalidAssetGuid) {
        session.previewRoot = services.GetPrefabSystem().Instantiate(em, services.GetSceneGraph(),
                                                                           services.GetAssetDatabase(),
                                                                           session.montage.previewEntity, Scene::kInvalidEntity);
        session.previewSkeleton = FindSkeletonUnder(services, session.previewRoot);
        if (session.previewRoot != Scene::kInvalidEntity && session.previewSkeleton == Scene::kInvalidEntity) {
            session.status = "The preview entity has no Skeleton3D: the motion montage has no bones to move.";
        }
    }
    if (session.previewSkeleton == Scene::kInvalidEntity) {
        // No character: the bones of the first clip, so the motion still shows.
        const Asset::AnimationClipAsset* clip = nullptr;
        for (const Asset::MontageSegment& segment : session.montage.segments) {
            if ((clip = MontageClip(segment.clip)) != nullptr && !clip->rest.empty()) {
                break;
            }
            clip = nullptr;
        }
        if (clip != nullptr) {
            const Scene::Entity skeletonEntity = em.CreateEntity();
            Scene::Skeleton3DObjectDefinition definition;
            for (const Asset::AnimationAssetRestBone& rest : clip->rest) {
                Scene::SkeletonBone bone;
                bone.name = rest.bone;
                bone.parentIndex = rest.parent;
                bone.localPosition = rest.position;
                bone.localRotation = rest.rotation;
                bone.localScale = rest.scale;
                definition.skeleton.bones.push_back(std::move(bone));
            }
            Scene::ApplySkeleton3DObjectDefinition(em, skeletonEntity, definition);
            if (session.previewRoot == Scene::kInvalidEntity) {
                session.previewRoot = skeletonEntity;
            } else {
                services.GetSceneGraph().SetParent(em, skeletonEntity, session.previewRoot);
            }
            session.previewSkeleton = skeletonEntity;
        }
    }
    session.overlay.studioFramingSerial += 1U;
    TickMontageEditor(0.0);
}

void Editor::PlayMontagePreview(const std::string_view section) {
    if (!m_montageEdit.has_value()) {
        return;
    }
    MontageEditSession& session = *m_montageEdit;
    std::string from(section);
    if (from.empty()) {
        if (const Asset::MontageSection* const at = Asset::MontageSectionAt(session.montage, session.playhead)) {
            from = at->name;
        }
    }
    session.playback = Scene::MontagePlayback{};
    if (!Scene::StartMontage(session.montage, session.playback, from)) {
        session.status = session.montage.segments.empty() ? "Add a clip to the Slot track first." : "Nothing to play.";
        return;
    }
    // Resuming mid-section keeps the playhead where the author left it.
    if (section.empty() && session.playhead > session.playback.time &&
        session.playhead < Asset::MontageSectionEnd(session.montage, from)) {
        session.playback.time = session.playhead;
    }
    session.playback.weight = 1.0F;
    session.playback.phase = Scene::MontagePhase::Playing;
    session.previewSection = from;
    session.playing = true;
}

void Editor::PauseMontagePreview() {
    if (m_montageEdit.has_value()) {
        m_montageEdit->playing = false;
    }
}

void Editor::StopMontagePreview() {
    if (m_montageEdit.has_value()) {
        m_montageEdit->playing = false;
        m_montageEdit->playhead = 0.0;
        TickMontageEditor(0.0);
    }
}

void Editor::SetMontagePlayhead(const double time) {
    if (!m_montageEdit.has_value()) {
        return;
    }
    m_montageEdit->playhead = std::clamp(std::isfinite(time) ? time : 0.0, 0.0, Asset::MontageLength(m_montageEdit->montage));
    m_montageEdit->playback.time = m_montageEdit->playhead;
    if (const Asset::MontageSection* const at = Asset::MontageSectionAt(m_montageEdit->montage, m_montageEdit->playhead)) {
        m_montageEdit->playback.section = at->name;
    }
}

void Editor::TickMontageEditor(const double deltaSeconds) {
    if (!m_montageEdit.has_value()) {
        return;
    }
    MontageEditSession& session = *m_montageEdit;
    if (session.playing) {
        std::vector<Scene::MontageEvent> events;
        // The preview plays at full weight (the blends matter against a pose the editor has none of)
        // and never blends out early: it shows the whole montage.
        Asset::MotionMontageAsset timing = session.montage;
        timing.blendInSeconds = 0.0;
        timing.blendOutSeconds = 0.0;
        timing.autoBlendOut = true;
        Scene::StepMontage(timing, session.playback, std::max(0.0, deltaSeconds) * session.speed, events);
        for (const Scene::MontageEvent& event : events) {
            switch (event.kind) {
                case Scene::MontageEvent::Kind::Notify: session.lastEvent = "Notify: " + event.name; break;
                case Scene::MontageEvent::Kind::NotifyBegin: session.lastEvent = "Begin: " + event.name; break;
                case Scene::MontageEvent::Kind::NotifyEnd: session.lastEvent = "End: " + event.name; break;
                case Scene::MontageEvent::Kind::Ended: break;
            }
        }
        if (session.playback.phase == Scene::MontagePhase::Stopped) {
            if (session.loop) {
                PlayMontagePreview(session.previewSection);
            } else {
                session.playing = false;
            }
        } else {
            session.playhead = session.playback.time;
        }
    }
    if (m_montageScene == nullptr || session.previewSkeleton == Scene::kInvalidEntity) {
        return;
    }
    Core::EngineServices& services = m_montageScene->GetServices();
    Scene::IEntityManager& em = services.GetEntityManager();
    if (!em.IsAlive(session.previewSkeleton) || !em.HasComponent<Scene::Skeleton3DComponent>(session.previewSkeleton)) {
        return;
    }
    Scene::Skeleton3DComponent& skeleton = em.GetComponent<Scene::Skeleton3DComponent>(session.previewSkeleton);
    skeleton.pose.clear();
    const Scene::AnimationClipResolver clipFor = [this](const Asset::AssetGuid guid) { return MontageClip(guid); };
    static_cast<void>(Scene::BlendMontagePose(session.montage, session.playhead, 1.0F, clipFor, skeleton));
    services.GetSceneGraph().Update(em);
}

std::uint64_t Editor::RenderMontagePreview(const Math::Vector2& size, Math::Vector2& outUsed,
                                                 const EditorViewportInput& input) {
    outUsed = Math::Vector2{0.0F, 0.0F};
    if (!m_montageEdit.has_value() || m_montageScene == nullptr || !m_viewports.montage) {
        return 0U;
    }
    MontageEditSession& session = *m_montageEdit;
    // The view renders whatever document is active: for this call, the preview's scene. Only the
    // services and the view state are borrowed; selection, undo and the rest stay the level's.
    Core::EngineServices* const previous = m_services;
    m_services = &m_montageScene->GetServices();
    ViewportOverlayState& state = session.overlay;
    state.studioView = true;
    state.entityAuthoringView = true;
    state.gameWorkspaceActive = false;
    // No gizmo layer over this view: the renderer draws the bones itself.
    state.hostDrawsGizmos = false;
    state.pointerOverOverlay = false;
    state.bones.clear();
    state.colliderLines.clear();
    BuildSkeletonOverlay(state.bones);
    state.input = input;
    std::uint64_t texture = 0U;
    try {
        texture = m_viewports.montage(size, outUsed, state);
    } catch (...) {
        m_services = previous;
        throw;
    }
    m_services = previous;
    return texture;
}

} // namespace UVE::Editor
