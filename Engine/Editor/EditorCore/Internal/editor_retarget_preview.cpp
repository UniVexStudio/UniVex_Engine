// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Retarget window's own world. The editor has one viewport that draws whatever the world
// holds, so the preview works the way the Entity Editor does: the scene is captured and put
// aside, a small world stands in its place (a sun, the humanoid and the character side by side),
// and closing the window brings the scene back untouched. The sky, the ground and the floor with
// its fog are the viewport's studio view (ViewportOverlayStateUVE::studioView), not objects.

#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "animation_clip_asset.h"
#include "i_asset_database.h"
#include "mesh_asset.h"
#include "name_component.h"
#include "transform_component.h"
#include "engine_services.h"
#include "editor.h"
#include "i_entity_manager.h"
#include "quaternion.h"
#include "mesh_instance_3d.h"
#include "object_3d.h"
#include "skeleton_3d.h"
#include "retarget_conform.h"
#include "retarget_humanoid.h"
#include "i_scene_graph.h"
#include "i_scene_serializer.h"
#include "scene_object_type.h"
#include "scene_object.h"

namespace UVE::Editor {
namespace {

/// How far each figure stands from the middle, so the two read side by side.
constexpr float kFigureOffsetUVE = 1.0F;
/// About how tall a figure stands, for framing both in the view.
constexpr float kFigureHeightUVE = 1.8F;

[[nodiscard]] Math::QuaternionUVE EulerUVE(const float x, const float y, const float z) {
    Math::QuaternionUVE rotation;
    static_cast<void>(Math::TryMakeEulerUVE(Math::Vector3UVE{x, y, z}, rotation));
    return rotation;
}

[[nodiscard]] std::vector<Scene::SkeletonBoneUVE> BonesOfUVE(const Retarget::RetargetSkeletonUVE& rig) {
    std::vector<Scene::SkeletonBoneUVE> bones;
    bones.reserve(rig.bones.size());
    for (const Retarget::RetargetBoneUVE& bone : rig.bones) {
        bones.push_back(Scene::SkeletonBoneUVE{bone.name, bone.parent, bone.position, bone.rotation,
                                               Math::Vector3UVE{bone.scale, bone.scale, bone.scale}});
    }
    return bones;
}

} // namespace

bool EditorUVE::BeginRetargetPreviewUVE() {
    if (m_retargetPreview.has_value()) {
        return true;
    }
    if (!m_documentSceneFactory) {
        return false;
    }
    // A world of its own beside the level (as the Montage Editor's): the level is not touched.
    RetargetPreviewUVE preview;
    preview.scene = std::shared_ptr<Core::DocumentSceneUVE>(m_documentSceneFactory());
    if (preview.scene == nullptr) {
        return false;
    }
    m_retargetPreview = std::move(preview);
    return true;
}

bool EditorUVE::EndRetargetPreviewUVE() {
    if (!m_retargetPreview.has_value()) {
        return true;
    }
    if (m_retargetPreview->scene != nullptr && m_documentSceneClosing) {
        m_documentSceneClosing(m_retargetPreview->scene->GetServicesUVE());
    }
    m_retargetPreview.reset();
    m_retargetViewportOverlayState.bones.clear();
    return true;
}

void EditorUVE::WithRetargetSceneUVE(const std::function<void()>& work) {
    if (!m_retargetPreview.has_value() || m_retargetPreview->scene == nullptr || m_retargetSceneActive) {
        return;
    }
    // Only the services are borrowed; the level's selection and dirty flag come back as they were.
    Core::EngineServicesUVE* const services = m_services;
    const std::vector<Scene::EntityUVE> selected = m_selectedEntities;
    const Scene::EntityUVE selectedEntity = m_selectedEntity;
    const bool dirty = m_sceneDirty;
    m_services = &m_retargetPreview->scene->GetServicesUVE();
    m_selectedEntities.clear();
    m_selectedEntity = Scene::kInvalidEntityUVE;
    m_retargetSceneActive = true;
    const auto restore = [&] {
        m_retargetSceneActive = false;
        m_services = services;
        m_selectedEntities = selected;
        m_selectedEntity = selectedEntity;
        m_sceneDirty = dirty;
    };
    try {
        work();
    } catch (...) {
        restore();
        throw;
    }
    restore();
}

std::uint64_t EditorUVE::RenderRetargetPreviewUVE(const Math::Vector2UVE& size, Math::Vector2UVE& outUsed,
                                                 const EditorViewportInputUVE& input) {
    outUsed = Math::Vector2UVE{0.0F, 0.0F};
    std::uint64_t texture = 0U;
    if (!m_retargetPreview.has_value() || !m_viewportPanelRenderer) {
        return texture;
    }
    RetargetPreviewUVE& preview = *m_retargetPreview;
    ViewportOverlayStateUVE& state = m_retargetViewportOverlayState;
    // The camera faces both figures from the front, a little above the floor.
    if (preview.frameRequested) {
        state.studioTarget = {0.0F, kFigureHeightUVE * 0.5F, 0.0F};
        state.studioRadius = kFigureOffsetUVE + kFigureHeightUVE * 0.75F;
        ++state.studioFramingSerial;
        preview.frameRequested = false;
    }
    WithRetargetSceneUVE([&] {
        state.studioView = true;
        state.entityAuthoringView = false;
        state.gameWorkspaceActive = false;
        state.hostDrawsGizmos = false;
        state.pointerOverOverlay = false;
        state.bones.clear();
        state.colliderLines.clear();
        BuildSkeletonOverlayUVE(state.bones);
        state.input = input;
        texture = m_viewportPanelRenderer(ViewportContextUVE::Retarget, size, outUsed, state);
    });
    return texture;
}

void EditorUVE::RebuildRetargetPreviewUVE(const RetargetPlanUVE& plan, const std::filesystem::path& modelFile) {
    WithRetargetSceneUVE([&] { BuildRetargetPreviewWorldUVE(plan, modelFile); });
}

void EditorUVE::BuildRetargetPreviewWorldUVE(const RetargetPlanUVE& plan, const std::filesystem::path& modelFile) {
    if (!m_retargetPreview.has_value()) {
        return;
    }
    RetargetPreviewUVE& preview = *m_retargetPreview;
    ClearDocumentSceneUVE();
    preview.frameRoot = preview.figures = preview.sourceSkeleton = preview.targetSkeleton = Scene::kInvalidEntityUVE;
    preview.sourceColours.clear();
    preview.targetColours.clear();

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();
    const Scene::EntityUVE sceneRoot = EnsureDocumentSceneRootUVE();
    if (sceneRoot == Scene::kInvalidEntityUVE) {
        return;
    }
    using Kind = Scene::Objects::SceneObjectKindUVE;
    const auto place = [&](const Scene::EntityUVE entity, const std::string& name, const Kind kind, const Scene::EntityUVE parent,
                           const Math::Vector3UVE position = {}, const Math::QuaternionUVE rotation = {},
                           const Math::Vector3UVE scale = {1.0F, 1.0F, 1.0F}) {
        if (entity == Scene::kInvalidEntityUVE) {
            return entity;
        }
        entityManager.GetComponentUVE<Scene::NameComponentUVE>(entity).name = name;
        Scene::SetSceneObjectKindUVE(entityManager, entity, kind);
        sceneGraph.SetParentUVE(entityManager, entity, parent);
        Scene::TransformComponentUVE transform = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
        transform.localPosition = position;
        transform.localRotation = rotation;
        transform.localScale = scale;
        sceneGraph.SetLocalTransformUVE(entityManager, entity, transform);
        return entity;
    };
    const auto shell = [&](const std::string& name) { return CreateDocumentEntityShellInternalUVE(name); };

    preview.frameRoot = place(CreateSceneObjectEntityInternalUVE(Kind::Object3D), "Retarget Preview", Kind::Object3D, sceneRoot);
    if (preview.frameRoot == Scene::kInvalidEntityUVE) {
        return;
    }
    // The sun leans over one shoulder, so the figures are shaded rather than flat.
    place(CreateSceneObjectEntityInternalUVE(Kind::Light3D), "Sun", Kind::Light3D, preview.frameRoot, Math::Vector3UVE{0.0F, 4.0F, 0.0F},
          EulerUVE(-0.95F, 0.55F, 0.0F));

    // The viewport frames the figures, not the floor under them.
    const Scene::EntityUVE figures = place(CreateSceneObjectEntityInternalUVE(Kind::Object3D), "Figures", Kind::Object3D, preview.frameRoot);
    preview.figures = figures;

    // ---- The humanoid, on the left ---------------------------------------------------------------
    const Retarget::HumanoidReferenceUVE& reference = Retarget::GetHumanoidReferenceUVE();
    {
        const Scene::EntityUVE figure = shell("Humanoid");
        Scene::ApplyObject3DObjectDefinitionUVE(entityManager, figure, Scene::Object3DObjectDefinitionUVE{});
        place(figure, "Humanoid", Kind::Object3D, figures, Math::Vector3UVE{-kFigureOffsetUVE, 0.0F, 0.0F});
        Scene::Skeleton3DObjectDefinitionUVE definition;
        definition.skeleton.bones = BonesOfUVE(reference.skeleton);
        const Scene::EntityUVE skeleton = shell("Humanoid Skeleton");
        Scene::ApplySkeleton3DObjectDefinitionUVE(entityManager, skeleton, definition);
        preview.sourceSkeleton = place(skeleton, "Humanoid Skeleton", Kind::Skeleton3D, figure);
    }
    preview.sourceColours.assign(reference.skeleton.bones.size(), GetRetargetStatusColourUVE(Retarget::JointStatusUVE::Missing));
    for (std::size_t bone = 0U; bone < plan.joints.size() && bone < preview.sourceColours.size(); ++bone) {
        preview.sourceColours[bone] = GetRetargetStatusColourUVE(plan.joints[bone].status);
        if (!plan.joints[bone].characterName.empty()) {
            preview.targetColours[plan.joints[bone].characterName] = preview.sourceColours[bone];
        }
    }

    // ---- The character, on the right --------------------------------------------------------------
    preview.builtFor = modelFile;
    Asset::MeshAssetUVE mesh;
    std::error_code error;
    const auto loadPreviewMesh = [&]() -> bool {
        if (modelFile.empty() || !std::filesystem::is_regular_file(modelFile, error)) {
            return false;
        }
        const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(modelFile, error);
        if (!error && m_inspectorMeshAssetCache.has_value() &&
            m_inspectorMeshAssetCache->resolvedPath == modelFile &&
            m_inspectorMeshAssetCache->lastWriteTime == writeTime) {
            mesh = m_inspectorMeshAssetCache->meshAsset;
            return true;
        }
        if (!Asset::LoadMeshAssetUVE(modelFile, mesh)) {
            return false;
        }
        if (!error) {
            m_inspectorMeshAssetCache = EntityEditorMeshCacheEntryUVE{modelFile, writeTime, mesh};
        }
        return true;
    };
    if (loadPreviewMesh()) {
        const Scene::EntityUVE figure = shell("Character");
        Scene::ApplyObject3DObjectDefinitionUVE(entityManager, figure, Scene::Object3DObjectDefinitionUVE{});
        place(figure, "Character", Kind::Object3D, figures, Math::Vector3UVE{kFigureOffsetUVE, 0.0F, 0.0F});
        Scene::EntityUVE meshParent = figure;
        if (const std::optional<Retarget::RetargetSkeletonUVE> rig = Retarget::RigFromMeshUVE(mesh);
            rig.has_value() && rig->bones.size() <= Scene::kMaximumSkeletonBonesUVE) {
            Scene::Skeleton3DObjectDefinitionUVE definition;
            definition.skeleton.bones = BonesOfUVE(*rig);
            const Scene::EntityUVE skeleton = shell("Character Skeleton");
            Scene::ApplySkeleton3DObjectDefinitionUVE(entityManager, skeleton, definition);
            preview.targetSkeleton = place(skeleton, "Character Skeleton", Kind::Skeleton3D, figure);
            meshParent = preview.targetSkeleton;
        }
        Scene::MeshInstance3DObjectDefinitionUVE meshDefinition;
        meshDefinition.mesh.meshGuid = RegisterImportedModelArtifactsUVE(modelFile);
        const Scene::EntityUVE meshEntity = shell("Character Mesh");
        Scene::ApplyMeshInstance3DObjectDefinitionUVE(entityManager, meshEntity, meshDefinition);
        place(meshEntity, "Character Mesh", Kind::MeshInstance3D, meshParent);
    } else if (m_retargetWindow.has_value()) {
        for (const std::filesystem::path& animPath : m_retargetWindow->animations) {
            Asset::AnimationClipAssetUVE clip;
            if (Asset::LoadAnimationClipAssetUVE(animPath, clip) && !clip.rest.empty() &&
                clip.rest.size() <= Scene::kMaximumSkeletonBonesUVE) {
                Retarget::RetargetSkeletonUVE rig;
                for (const Asset::AnimationAssetRestBoneUVE& rest : clip.rest) {
                    Retarget::RetargetBoneUVE bone;
                    bone.name = rest.bone;
                    bone.parent = rest.parent;
                    bone.position = rest.position;
                    bone.rotation = rest.rotation;
                    bone.scale = rest.scale.x;
                    rig.bones.push_back(std::move(bone));
                }
                const Scene::EntityUVE figure = shell("Character");
                Scene::ApplyObject3DObjectDefinitionUVE(entityManager, figure, Scene::Object3DObjectDefinitionUVE{});
                place(figure, "Character", Kind::Object3D, figures, Math::Vector3UVE{kFigureOffsetUVE, 0.0F, 0.0F});
                Scene::Skeleton3DObjectDefinitionUVE definition;
                definition.skeleton.bones = BonesOfUVE(rig);
                const Scene::EntityUVE skeleton = shell("Character Skeleton");
                Scene::ApplySkeleton3DObjectDefinitionUVE(entityManager, skeleton, definition);
                preview.targetSkeleton = place(skeleton, "Character Skeleton", Kind::Skeleton3D, figure);
                break;
            }
        }
    }
    preview.frameRequested = true;
}

} // namespace UVE::Editor
