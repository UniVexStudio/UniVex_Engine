// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// The Montage Editor's document: an open `.uvmontage`, its undo, and its preview - the character
// the montage is previewed on, in a scene of its own beside the level (and an open entity), posed
// at the playhead every frame. The PRISM window (shell/montage/editor_montage_editor) draws it.
#include "editor.h"

#include "animation_clip_asset.h"
#include "animation_montage.h"
#include "animation_montage_editing.h"
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
[[nodiscard]] Scene::EntityUVE FindSkeletonUnderUVE(Core::EngineServicesUVE& services, const Scene::EntityUVE root) {
    Scene::IEntityManagerUVE& em = services.GetEntityManagerUVE();
    if (root == Scene::kInvalidEntityUVE || !em.IsAliveUVE(root)) {
        return Scene::kInvalidEntityUVE;
    }
    std::vector<Scene::EntityUVE> queue{root};
    for (std::size_t next = 0U; next < queue.size() && next < 4096U; ++next) {
        if (em.HasComponentUVE<Scene::Skeleton3DComponentUVE>(queue[next])) {
            return queue[next];
        }
        const std::vector<Scene::EntityUVE> children = services.GetSceneGraphUVE().GetChildrenUVE(em, queue[next]);
        queue.insert(queue.end(), children.begin(), children.end());
    }
    return Scene::kInvalidEntityUVE;
}

} // namespace

const Asset::AnimationClipAssetUVE* EditorUVE::MontageClipUVE(const Asset::AssetGuidUVE guid) {
    if (guid == Asset::kInvalidAssetGuidUVE) {
        return nullptr;
    }
    auto it = m_montageClips.find(guid.value);
    if (it == m_montageClips.end()) {
        std::optional<Asset::AnimationClipAssetUVE> loaded;
        const std::filesystem::path path = m_services->GetAssetDatabaseUVE().ResolveUVE(guid);
        Asset::AnimationClipAssetUVE clip;
        if (!path.empty() && Asset::LoadAnimationClipAssetUVE(path, clip)) {
            loaded = std::move(clip);
        }
        it = m_montageClips.emplace(guid.value, std::move(loaded)).first;
    }
    return it->second.has_value() ? &*it->second : nullptr;
}

bool EditorUVE::OpenMontageEditorUVE(const std::filesystem::path& path) {
    if (m_montageEdit.has_value() && m_montageEdit->path == path) {
        m_montageEdit->focusPending = true;
        return true;
    }
    if (IsMontageEditorDirtyUVE()) {
        m_contentStatusMessage = "Save or close " + m_montageEdit->path.filename().string() + " first.";
        return false;
    }
    Asset::AnimationMontageAssetUVE montage;
    if (!Asset::LoadAnimationMontageAssetUVE(path, montage)) {
        m_contentStatusMessage = "Could not read " + path.filename().string() + " as a montage.";
        return false;
    }
    CloseMontageEditorUVE();
    MontageEditSessionUVE session;
    session.path = path;
    session.guid = m_services->GetAssetDatabaseUVE().RegisterUVE(path);
    session.montage = montage;
    session.saved = std::move(montage);
    session.overlay.studioView = true;
    session.overlay.entityAuthoringView = true;
    m_montageEdit = std::move(session);
    if (m_documentSceneFactory) {
        m_montageScene = m_documentSceneFactory();
    }
    RebuildMontagePreviewUVE();
    return true;
}

bool EditorUVE::SaveMontageEditorUVE() {
    if (!m_montageEdit.has_value()) {
        return false;
    }
    if (!Asset::SaveAnimationMontageAssetUVE(m_montageEdit->montage, m_montageEdit->path)) {
        m_montageEdit->status = "Could not save " + m_montageEdit->path.filename().string() + ".";
        return false;
    }
    m_montageEdit->saved = m_montageEdit->montage;
    m_montageEdit->status = "Saved " + m_montageEdit->path.filename().string() + ".";
    m_projectFileSnapshotInitialized = false;
    RefreshProjectFileIndexUVE();
    return true;
}

void EditorUVE::CloseMontageEditorUVE() {
    if (m_montageScene != nullptr && m_documentSceneClosing) {
        m_documentSceneClosing(m_montageScene->GetServicesUVE());
    }
    m_montageScene.reset();
    m_montageEdit.reset();
    m_montageClips.clear();
}

void EditorUVE::EditMontageUVE(const std::function<void(Asset::AnimationMontageAssetUVE&)>& edit) {
    if (!m_montageEdit.has_value() || !edit) {
        return;
    }
    Asset::AnimationMontageAssetUVE changed = m_montageEdit->montage;
    edit(changed);
    if (changed == m_montageEdit->montage) {
        return;
    }
    if (!Asset::IsAnimationMontageAssetValidUVE(changed)) {
        m_montageEdit->status = "That would make the montage invalid.";
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
    m_montageEdit->playhead = std::clamp(m_montageEdit->playhead, 0.0, Asset::MontageLengthUVE(m_montageEdit->montage));
    if (previewChanged) {
        RebuildMontagePreviewUVE();
    }
}

bool EditorUVE::UndoMontageEditUVE() {
    if (!m_montageEdit.has_value() || m_montageEdit->undo.empty()) {
        return false;
    }
    m_montageEdit->redo.push_back(m_montageEdit->montage);
    m_montageEdit->montage = m_montageEdit->undo.back();
    m_montageEdit->undo.pop_back();
    RebuildMontagePreviewUVE();
    return true;
}

bool EditorUVE::RedoMontageEditUVE() {
    if (!m_montageEdit.has_value() || m_montageEdit->redo.empty()) {
        return false;
    }
    m_montageEdit->undo.push_back(m_montageEdit->montage);
    m_montageEdit->montage = m_montageEdit->redo.back();
    m_montageEdit->redo.pop_back();
    RebuildMontagePreviewUVE();
    return true;
}

void EditorUVE::SetMontagePreviewEntityUVE(const Asset::AssetGuidUVE guid) {
    EditMontageUVE([guid](Asset::AnimationMontageAssetUVE& m) { m.previewEntity = guid; });
}

void EditorUVE::RebuildMontagePreviewUVE() {
    if (!m_montageEdit.has_value() || m_montageScene == nullptr) {
        return;
    }
    MontageEditSessionUVE& session = *m_montageEdit;
    Core::EngineServicesUVE& services = m_montageScene->GetServicesUVE();
    Scene::IEntityManagerUVE& em = services.GetEntityManagerUVE();
    // The preview is rebuilt whole: what was there is gone.
    if (session.previewRoot != Scene::kInvalidEntityUVE && em.IsAliveUVE(session.previewRoot)) {
        std::vector<Scene::EntityUVE> doomed{session.previewRoot};
        for (std::size_t i = 0U; i < doomed.size(); ++i) {
            const std::vector<Scene::EntityUVE> children = services.GetSceneGraphUVE().GetChildrenUVE(em, doomed[i]);
            doomed.insert(doomed.end(), children.begin(), children.end());
        }
        for (auto it = doomed.rbegin(); it != doomed.rend(); ++it) {
            if (em.IsAliveUVE(*it)) {
                em.DestroyEntityUVE(*it);
            }
        }
    }
    session.previewRoot = Scene::kInvalidEntityUVE;
    session.previewSkeleton = Scene::kInvalidEntityUVE;

    if (session.montage.previewEntity != Asset::kInvalidAssetGuidUVE) {
        session.previewRoot = services.GetPrefabSystemUVE().InstantiateUVE(em, services.GetSceneGraphUVE(),
                                                                           services.GetAssetDatabaseUVE(),
                                                                           session.montage.previewEntity, Scene::kInvalidEntityUVE);
        session.previewSkeleton = FindSkeletonUnderUVE(services, session.previewRoot);
        if (session.previewRoot != Scene::kInvalidEntityUVE && session.previewSkeleton == Scene::kInvalidEntityUVE) {
            session.status = "The preview entity has no Skeleton3D: the montage has no bones to move.";
        }
    }
    if (session.previewSkeleton == Scene::kInvalidEntityUVE) {
        // No character: the bones of the first clip, so the motion still shows.
        const Asset::AnimationClipAssetUVE* clip = nullptr;
        for (const Asset::MontageSegmentUVE& segment : session.montage.segments) {
            if ((clip = MontageClipUVE(segment.clip)) != nullptr && !clip->rest.empty()) {
                break;
            }
            clip = nullptr;
        }
        if (clip != nullptr) {
            const Scene::EntityUVE skeletonEntity = em.CreateEntityUVE();
            Scene::Skeleton3DObjectDefinitionUVE definition;
            for (const Asset::AnimationAssetRestBoneUVE& rest : clip->rest) {
                Scene::SkeletonBoneUVE bone;
                bone.name = rest.bone;
                bone.parentIndex = rest.parent;
                bone.localPosition = rest.position;
                bone.localRotation = rest.rotation;
                bone.localScale = rest.scale;
                definition.skeleton.bones.push_back(std::move(bone));
            }
            Scene::ApplySkeleton3DObjectDefinitionUVE(em, skeletonEntity, definition);
            if (session.previewRoot == Scene::kInvalidEntityUVE) {
                session.previewRoot = skeletonEntity;
            } else {
                services.GetSceneGraphUVE().SetParentUVE(em, skeletonEntity, session.previewRoot);
            }
            session.previewSkeleton = skeletonEntity;
        }
    }
    session.overlay.studioFramingSerial += 1U;
    TickMontageEditorUVE(0.0);
}

void EditorUVE::PlayMontagePreviewUVE(const std::string_view section) {
    if (!m_montageEdit.has_value()) {
        return;
    }
    MontageEditSessionUVE& session = *m_montageEdit;
    std::string from(section);
    if (from.empty()) {
        if (const Asset::MontageSectionUVE* const at = Asset::MontageSectionAtUVE(session.montage, session.playhead)) {
            from = at->name;
        }
    }
    session.playback = Scene::MontagePlaybackUVE{};
    if (!Scene::StartMontageUVE(session.montage, session.playback, from)) {
        session.status = session.montage.segments.empty() ? "Add a clip to the Slot track first." : "Nothing to play.";
        return;
    }
    // Resuming mid-section keeps the playhead where the author left it.
    if (section.empty() && session.playhead > session.playback.time &&
        session.playhead < Asset::MontageSectionEndUVE(session.montage, from)) {
        session.playback.time = session.playhead;
    }
    session.playback.weight = 1.0F;
    session.playback.phase = Scene::MontagePhaseUVE::Playing;
    session.previewSection = from;
    session.playing = true;
}

void EditorUVE::PauseMontagePreviewUVE() {
    if (m_montageEdit.has_value()) {
        m_montageEdit->playing = false;
    }
}

void EditorUVE::StopMontagePreviewUVE() {
    if (m_montageEdit.has_value()) {
        m_montageEdit->playing = false;
        m_montageEdit->playhead = 0.0;
        TickMontageEditorUVE(0.0);
    }
}

void EditorUVE::SetMontagePlayheadUVE(const double time) {
    if (!m_montageEdit.has_value()) {
        return;
    }
    m_montageEdit->playhead = std::clamp(std::isfinite(time) ? time : 0.0, 0.0, Asset::MontageLengthUVE(m_montageEdit->montage));
    m_montageEdit->playback.time = m_montageEdit->playhead;
    if (const Asset::MontageSectionUVE* const at = Asset::MontageSectionAtUVE(m_montageEdit->montage, m_montageEdit->playhead)) {
        m_montageEdit->playback.section = at->name;
    }
}

void EditorUVE::TickMontageEditorUVE(const double deltaSeconds) {
    if (!m_montageEdit.has_value()) {
        return;
    }
    MontageEditSessionUVE& session = *m_montageEdit;
    if (session.playing) {
        std::vector<Scene::MontageEventUVE> events;
        // The preview plays at full weight (the blends matter against a pose the editor has none of)
        // and never blends out early: it shows the whole montage.
        Asset::AnimationMontageAssetUVE timing = session.montage;
        timing.blendInSeconds = 0.0;
        timing.blendOutSeconds = 0.0;
        timing.autoBlendOut = true;
        Scene::StepMontageUVE(timing, session.playback, std::max(0.0, deltaSeconds) * session.speed, events);
        for (const Scene::MontageEventUVE& event : events) {
            switch (event.kind) {
                case Scene::MontageEventUVE::KindUVE::Notify: session.lastEvent = "Notify: " + event.name; break;
                case Scene::MontageEventUVE::KindUVE::NotifyBegin: session.lastEvent = "Begin: " + event.name; break;
                case Scene::MontageEventUVE::KindUVE::NotifyEnd: session.lastEvent = "End: " + event.name; break;
                case Scene::MontageEventUVE::KindUVE::Ended: break;
            }
        }
        if (session.playback.phase == Scene::MontagePhaseUVE::Stopped) {
            if (session.loop) {
                PlayMontagePreviewUVE(session.previewSection);
            } else {
                session.playing = false;
            }
        } else {
            session.playhead = session.playback.time;
        }
    }
    if (m_montageScene == nullptr || session.previewSkeleton == Scene::kInvalidEntityUVE) {
        return;
    }
    Core::EngineServicesUVE& services = m_montageScene->GetServicesUVE();
    Scene::IEntityManagerUVE& em = services.GetEntityManagerUVE();
    if (!em.IsAliveUVE(session.previewSkeleton) || !em.HasComponentUVE<Scene::Skeleton3DComponentUVE>(session.previewSkeleton)) {
        return;
    }
    Scene::Skeleton3DComponentUVE& skeleton = em.GetComponentUVE<Scene::Skeleton3DComponentUVE>(session.previewSkeleton);
    skeleton.pose.clear();
    const Scene::AnimationClipResolverUVE clipFor = [this](const Asset::AssetGuidUVE guid) { return MontageClipUVE(guid); };
    static_cast<void>(Scene::BlendMontagePoseUVE(session.montage, session.playhead, 1.0F, clipFor, skeleton));
    services.GetSceneGraphUVE().UpdateUVE(em);
}

std::uint64_t EditorUVE::RenderMontagePreviewUVE(const Math::Vector2UVE& size, Math::Vector2UVE& outUsed,
                                                 const EditorViewportInputUVE& input) {
    outUsed = Math::Vector2UVE{0.0F, 0.0F};
    if (!m_montageEdit.has_value() || m_montageScene == nullptr || !m_viewportPanelRenderer) {
        return 0U;
    }
    MontageEditSessionUVE& session = *m_montageEdit;
    // The view renders whatever document is active: for this call, the preview's scene. Only the
    // services and the view state are borrowed; selection, undo and the rest stay the level's.
    Core::EngineServicesUVE* const previous = m_services;
    m_services = &m_montageScene->GetServicesUVE();
    ViewportOverlayStateUVE& state = session.overlay;
    state.studioView = true;
    state.entityAuthoringView = true;
    state.gameWorkspaceActive = false;
    // No gizmo layer over this view: the renderer draws the bones itself.
    state.hostDrawsGizmos = false;
    state.pointerOverOverlay = false;
    state.bones.clear();
    state.colliderLines.clear();
    BuildSkeletonOverlayUVE(state.bones);
    state.input = input;
    std::uint64_t texture = 0U;
    try {
        texture = m_viewportPanelRenderer(ViewportContextUVE::MontageEditor, size, outUsed, state);
    } catch (...) {
        m_services = previous;
        throw;
    }
    m_services = previous;
    return texture;
}

} // namespace UVE::Editor
