// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Retarget window's own world. The editor has one viewport that draws whatever the world
// holds, so the preview works the way the Entity Editor does: the scene is captured and put
// aside, a small world stands in its place (a floor, a sun, a sky, the humanoid and the character
// side by side), and closing the window brings the scene back untouched.

#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/core/engine_services_uve.h"
#include "uve/editor/editor_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/nodes/3d/mesh_instance_3d_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"
#include "uve/nodes/3d/skeleton_3d_uve.h"
#include "uve/retarget/retarget_conform_uve.h"
#include "uve/retarget/retarget_humanoid_uve.h"
#include "uve/scene/i_scene_graph_uve.h"
#include "uve/scene/i_scene_serializer_uve.h"
#include "uve/scene/nodes/scene_node_type_uve.h"
#include "uve/scene/nodes/scene_node_uve.h"

namespace UVE::Editor {
namespace {

/// How far each figure stands from the middle, so the two read side by side.
constexpr float kFigureOffsetUVE = 0.85F;
/// The floor is a unit plane scaled to this many metres a side.
constexpr float kFloorSizeUVE = 6.0F;

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
    if (m_entityEditSession.has_value() || !IsAuthoringCommandAllowedUVE()) {
        return false;
    }
    if (m_openUVScript.has_value() && m_openUVScript->IsDirtyUVE()) {
        m_contentStatusMessage = "Save or close " + std::filesystem::path{m_openUVScript->path}.filename().string() +
                                 " first: it has unsaved text.";
        return false;
    }
    static_cast<void>(CommitComponentPropertyPreviewUVE());

    RetargetPreviewUVE preview;
    const std::vector<Scene::EntityUVE> roots = GetDocumentRootsUVE();
    preview.sceneWasEmpty = roots.empty();
    if (!preview.sceneWasEmpty) {
        const std::optional<Scene::SceneSnapshotUVE> snapshot = m_services->GetSceneSerializerUVE().CaptureUVE(
            m_services->GetEntityManagerUVE(), roots, Asset::AssetKindUVE::Scene);
        if (!snapshot.has_value()) {
            return false;
        }
        preview.sceneSnapshot = *snapshot;
    }
    preview.sceneDirtyBefore = m_sceneDirty;
    preview.selectionBefore = CaptureSelectionPathsUVE(roots);
    CloseOpenUVScriptUVE();
    ClearDocumentSceneUVE();
    ClearHistoryUVE();
    m_sceneDirty = false;
    // Nothing runs in the preview: a character would otherwise fall through the floor.
    if (m_simulationControl != nullptr) {
        preview.simulationBefore = m_simulationControl->GetSimulationExecutionModeUVE();
        static_cast<void>(m_simulationControl->SetSimulationExecutionModeUVE(Core::SimulationExecutionModeUVE::Paused));
    }
    m_retargetPreview = std::move(preview);
    return true;
}

bool EditorUVE::EndRetargetPreviewUVE() {
    if (!m_retargetPreview.has_value()) {
        return true;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    // Kept until the scene is back, so a failed restore can return to the preview.
    const std::vector<Scene::EntityUVE> previewRoots = GetDocumentRootsUVE();
    const std::optional<Scene::SceneSnapshotUVE> previewSnapshot =
        previewRoots.empty() ? std::nullopt
                             : m_services->GetSceneSerializerUVE().CaptureUVE(entityManager, previewRoots,
                                                                               Asset::AssetKindUVE::Scene);
    const RetargetPreviewUVE preview = std::move(*m_retargetPreview);
    m_retargetPreview.reset();

    ClearDocumentSceneUVE();
    std::vector<Scene::EntityUVE> restored;
    if (!preview.sceneWasEmpty) {
        restored = m_services->GetSceneSerializerUVE().RestoreUVE(entityManager, preview.sceneSnapshot);
        if (restored.empty()) {
            if (previewSnapshot.has_value()) {
                static_cast<void>(m_services->GetSceneSerializerUVE().RestoreUVE(entityManager, *previewSnapshot));
            }
            m_retargetPreview = preview;
            m_contentStatusMessage = "Could not bring the scene back; the Retarget preview stays open.";
            InvalidateHierarchyFilterCacheUVE();
            return false;
        }
    } else {
        static_cast<void>(EnsureDocumentSceneRootUVE());
    }
    RestoreSelectionUVE(ResolveSelectionPathsUVE(preview.selectionBefore, restored));
    m_sceneDirty = preview.sceneDirtyBefore;
    ClearHistoryUVE();
    if (m_simulationControl != nullptr && preview.simulationBefore.has_value()) {
        static_cast<void>(m_simulationControl->SetSimulationExecutionModeUVE(*preview.simulationBefore));
    }
    m_viewportOverlayState.bones.clear();
    InvalidateHierarchyFilterCacheUVE();
    return true;
}

void EditorUVE::RebuildRetargetPreviewUVE(const RetargetPlanUVE& plan, const std::filesystem::path& modelFile) {
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
    using Kind = Scene::Nodes::SceneNodeKindUVE;
    const auto place = [&](const Scene::EntityUVE entity, const std::string& name, const Kind kind, const Scene::EntityUVE parent,
                           const Math::Vector3UVE position = {}, const Math::QuaternionUVE rotation = {},
                           const Math::Vector3UVE scale = {1.0F, 1.0F, 1.0F}) {
        if (entity == Scene::kInvalidEntityUVE) {
            return entity;
        }
        entityManager.GetComponentUVE<Scene::NameComponentUVE>(entity).name = name;
        Scene::SetSceneNodeKindUVE(entityManager, entity, kind);
        sceneGraph.SetParentUVE(entityManager, entity, parent);
        Scene::TransformComponentUVE transform = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
        transform.localPosition = position;
        transform.localRotation = rotation;
        transform.localScale = scale;
        sceneGraph.SetLocalTransformUVE(entityManager, entity, transform);
        return entity;
    };
    const auto shell = [&](const std::string& name) { return CreateDocumentEntityShellInternalUVE(name); };

    preview.frameRoot = place(CreateSceneNodeEntityInternalUVE(Kind::Node3D), "Retarget Preview", Kind::Node3D, sceneRoot);
    if (preview.frameRoot == Scene::kInvalidEntityUVE) {
        return;
    }
    place(CreateSceneNodeEntityInternalUVE(Kind::PlaneMesh3D), "Floor", Kind::PlaneMesh3D, preview.frameRoot, {}, {},
          Math::Vector3UVE{kFloorSizeUVE, 1.0F, kFloorSizeUVE});
    // The sun leans over one shoulder, so the figures are shaded rather than flat.
    place(CreateSceneNodeEntityInternalUVE(Kind::Light3D), "Sun", Kind::Light3D, preview.frameRoot, Math::Vector3UVE{0.0F, 4.0F, 0.0F},
          EulerUVE(-0.95F, 0.55F, 0.0F));
    place(CreateSceneNodeEntityInternalUVE(Kind::WorldEnvironment3D), "Sky", Kind::WorldEnvironment3D, preview.frameRoot);

    // The viewport frames the figures, not the floor under them.
    const Scene::EntityUVE figures = place(CreateSceneNodeEntityInternalUVE(Kind::Node3D), "Figures", Kind::Node3D, preview.frameRoot);
    preview.figures = figures;

    // ---- The humanoid, on the left ---------------------------------------------------------------
    const Retarget::HumanoidReferenceUVE& reference = Retarget::GetHumanoidReferenceUVE();
    {
        const Scene::EntityUVE figure = shell("Humanoid");
        Scene::ApplyNode3DNodeDefinitionUVE(entityManager, figure, Scene::Node3DNodeDefinitionUVE{});
        place(figure, "Humanoid", Kind::Node3D, figures, Math::Vector3UVE{-kFigureOffsetUVE, 0.0F, 0.0F});
        Scene::Skeleton3DNodeDefinitionUVE definition;
        definition.skeleton.bones = BonesOfUVE(reference.skeleton);
        const Scene::EntityUVE skeleton = shell("Humanoid Skeleton");
        Scene::ApplySkeleton3DNodeDefinitionUVE(entityManager, skeleton, definition);
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
    if (!modelFile.empty() && std::filesystem::is_regular_file(modelFile, error) && Asset::LoadMeshAssetUVE(modelFile, mesh)) {
        const Scene::EntityUVE figure = shell("Character");
        Scene::ApplyNode3DNodeDefinitionUVE(entityManager, figure, Scene::Node3DNodeDefinitionUVE{});
        place(figure, "Character", Kind::Node3D, figures, Math::Vector3UVE{kFigureOffsetUVE, 0.0F, 0.0F});
        Scene::EntityUVE meshParent = figure;
        if (const std::optional<Retarget::RetargetSkeletonUVE> rig = Retarget::RigFromMeshUVE(mesh);
            rig.has_value() && rig->bones.size() <= Scene::kMaximumSkeletonBonesUVE) {
            Scene::Skeleton3DNodeDefinitionUVE definition;
            definition.skeleton.bones = BonesOfUVE(*rig);
            const Scene::EntityUVE skeleton = shell("Character Skeleton");
            Scene::ApplySkeleton3DNodeDefinitionUVE(entityManager, skeleton, definition);
            preview.targetSkeleton = place(skeleton, "Character Skeleton", Kind::Skeleton3D, figure);
            meshParent = preview.targetSkeleton;
        }
        Scene::MeshInstance3DNodeDefinitionUVE meshDefinition;
        meshDefinition.mesh.meshGuid = m_services->GetAssetDatabaseUVE().RegisterUVE(modelFile);
        const Scene::EntityUVE meshEntity = shell("Character Mesh");
        Scene::ApplyMeshInstance3DNodeDefinitionUVE(entityManager, meshEntity, meshDefinition);
        place(meshEntity, "Character Mesh", Kind::MeshInstance3D, meshParent);
    }
    ClearHistoryUVE();
    m_sceneDirty = false;
    InvalidateHierarchyFilterCacheUVE();
    preview.frameRequested = true;
}

void EditorUVE::DrawRetargetPreviewUVE() {
    if (!m_retargetPreview.has_value()) {
        ImGui::TextDisabled("The preview needs the editor idle: stop playing and close the Entity Editor, then reopen this window.");
        return;
    }
    RetargetPreviewUVE& preview = *m_retargetPreview;
    // Framing needs the figures' world transforms, which settle a frame after they are made.
    if (preview.frameRequested && preview.figures != Scene::kInvalidEntityUVE && RequestViewportFocusUVE(preview.figures)) {
        preview.frameRequested = false;
    }
    DrawViewportImageUVE();
}

void EditorUVE::DrawRetargetPlaceholderUVE() {
    // The main window stays out of the way: the scene is put aside while the Retarget window shows its own world.
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2{mainViewport->WorkPos.x, mainViewport->WorkPos.y + ImGui::GetFrameHeight() * 2.0F});
    ImGui::SetNextWindowSize(ImVec2{mainViewport->WorkSize.x, mainViewport->WorkSize.y - ImGui::GetFrameHeight() * 2.0F});
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##retarget-placeholder", nullptr, flags);
    const char* const line1 = "Retargeting in the Retarget window.";
    const char* const line2 = "The scene waits here, untouched. Close the Retarget window to come back to it.";
    const ImVec2 area = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPos(ImVec2{(area.x - ImGui::CalcTextSize(line1).x) * 0.5F, area.y * 0.42F});
    ImGui::TextUnformatted(line1);
    ImGui::SetCursorPosX((area.x - ImGui::CalcTextSize(line2).x) * 0.5F);
    ImGui::TextDisabled("%s", line2);
    ImGui::SetCursorPosX((area.x - 180.0F) * 0.5F);
    if (ImGui::Button("Show the Retarget window", ImVec2{180.0F, 0.0F})) {
        ImGui::SetWindowFocus("###retarget-window");
    }
    ImGui::End();
}

} // namespace UVE::Editor
