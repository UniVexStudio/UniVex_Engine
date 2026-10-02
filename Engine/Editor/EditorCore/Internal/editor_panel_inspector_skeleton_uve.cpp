// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Skeleton3D in the Inspector: where its bones come from, and what they are.
//
// Bones are authored where they belong - an armature in Blender or any DCC tool - and arrive with
// the model exported from it. This node never invents a bone; a new Skeleton3D is empty until it
// is pointed at a rigged model, and pointing it again (Reload) picks up an edited rig.

#include "uve/editor/editor_uve.h"
#include "uve/retarget/retarget_conform_uve.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <typeindex>
#include <vector>

#include <imgui.h>

#include "uve/asset/fbx_mesh_converter_uve.h"
#include "uve/asset/gltf_skeleton_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/nodes/3d/animation_sequencer_uve.h"
#include "uve/nodes/3d/mesh_instance_3d_uve.h"
#include "uve/nodes/3d/object_3d_uve.h"
#include "uve/nodes/3d/skeleton_3d_uve.h"
#include "uve/scene/nodes/scene_node_type_uve.h"
#include "uve/scene/scene_component_metadata_uve.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] bool BeginSkeletonRowsUVE(const char* const id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        return false;
    }
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 0.42F);
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.58F);
    return true;
}

void ReadOnlyRowUVE(const char* const label, const char* const value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextDisabled("%s", value);
}

// The skeleton in a model source, whichever format it is: a glTF skin, or an FBX's bones.
[[nodiscard]] std::optional<Asset::GltfSkeletonUVE> ReadSkeletonSourceUVE(const std::filesystem::path& path,
                                                                          const std::size_t maximumBones) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (extension != ".fbx") {
        return Asset::ReadGltfSkeletonUVE(path, maximumBones);
    }
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    std::ifstream file(path, std::ios::binary);
    if (error || size > Asset::kMaximumFbxMeshSourceBytesUVE || !file) {
        return std::nullopt;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return Asset::ReadFbxSkeletonUVE(std::as_bytes(std::span<const char>(bytes)), maximumBones);
}

} // namespace

std::optional<Asset::GltfSkeletonUVE> EditorUVE::ReadSkeletonForSourceUVE(const std::filesystem::path& relativeSource,
                                                                          const std::size_t maximumBones) const {
    // A character that has been through Retarget is the humanoid's, and so are its clips: its
    // skeleton comes from the imported model that was conformed, not from the original file.
    if (IsModelSourcePathUVE(relativeSource)) {
        Asset::MeshAssetUVE mesh;
        if (Asset::LoadMeshAssetUVE(GetImportedModelPathUVE(relativeSource), mesh)) {
            if (const std::optional<Retarget::RetargetSkeletonUVE> rig = Retarget::RigFromMeshUVE(mesh);
                rig.has_value() && rig->bones.size() <= maximumBones) {
                std::vector<std::string> names;
                for (const Retarget::RetargetBoneUVE& bone : rig->bones) {
                    names.push_back(bone.name);
                }
                if (Retarget::AreHumanoidNamesUVE(names, Retarget::GetHumanoidReferenceUVE())) {
                    Asset::GltfSkeletonUVE skeleton;
                    skeleton.skinCount = 1U;
                    for (const Retarget::RetargetBoneUVE& bone : rig->bones) {
                        Asset::GltfJointUVE joint;
                        joint.name = bone.name;
                        joint.parentIndex = bone.parent;
                        joint.translation = bone.position;
                        joint.rotation = bone.rotation;
                        joint.scale = Math::Vector3UVE{bone.scale, bone.scale, bone.scale};
                        skeleton.joints.push_back(std::move(joint));
                    }
                    return skeleton;
                }
            }
        }
    }
    return ReadSkeletonSourceUVE(m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot / relativeSource,
                                 maximumBones);
}

Scene::EntityUVE EditorUVE::PlaceModelSourceUVE(const std::filesystem::path& relativeSource,
                                                Scene::EntityUVE parent) {
    if (!IsAuthoringCommandAllowedUVE() || !IsModelSourcePathUVE(relativeSource)) {
        return Scene::kInvalidEntityUVE;
    }
    const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path source = project.contentRoot / relativeSource;
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error)) {
        return Scene::kInvalidEntityUVE;
    }
    const std::optional<Asset::GltfSkeletonUVE> skeleton = ReadSkeletonForSourceUVE(relativeSource, Scene::kMaximumSkeletonBonesUVE);
    const std::filesystem::path importedModel = GetImportedModelPathUVE(relativeSource);
    const bool hasMesh = std::filesystem::is_regular_file(importedModel, error);
    if (!hasMesh && !skeleton.has_value()) {
        m_contentStatusMessage = relativeSource.filename().string() + " is still importing.";
        return Scene::kInvalidEntityUVE;
    }
    // The file's takes, imported beside it as <file>_<take>.uvanim (importing them now if needed).
    std::vector<std::filesystem::path> clips;
    if (skeleton.has_value()) {
        static_cast<void>(ImportModelAnimationsUVE(source));
        const std::string prefix = source.stem().string() + "_";
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(source.parent_path(), error)) {
            const std::string name = entry.path().filename().string();
            // "<FBX>.uvanim" (one take) or "<FBX>_<take>.uvanim" (several).
            if (entry.path().extension() == ".uvanim" &&
                (name.starts_with(prefix) || entry.path().stem() == source.stem())) {
                clips.push_back(entry.path());
            }
        }
        std::sort(clips.begin(), clips.end());
    }

    if (parent == Scene::kInvalidEntityUVE || !IsDocumentEntityUVE(parent)) {
        parent = ResolveNewNodeParentUVE();
    }
    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sceneGraph = m_services->GetSceneGraphUVE();
    const std::string stem = relativeSource.stem().string();
    using Kind = Scene::Nodes::SceneNodeKindUVE;
    const auto make = [&](const std::string& name, const Kind kind, const auto& definition, const auto& apply,
                          const Scene::EntityUVE under) {
        const Scene::EntityUVE entity = CreateDocumentEntityShellInternalUVE(MakeUniqueDocumentEntityNameUVE(name));
        if (entity != Scene::kInvalidEntityUVE) {
            apply(entityManager, entity, definition);
            Scene::SetSceneNodeKindUVE(entityManager, entity, kind);
            if (under != Scene::kInvalidEntityUVE) {
                sceneGraph.SetParentUVE(entityManager, entity, under);
            }
        }
        return entity;
    };
    const Asset::AssetGuidUVE meshGuid =
        hasMesh ? m_services->GetAssetDatabaseUVE().RegisterUVE(importedModel) : Asset::kInvalidAssetGuidUVE;
    Scene::EntityUVE root = Scene::kInvalidEntityUVE;
    if (!skeleton.has_value()) {
        Scene::MeshInstance3DNodeDefinitionUVE definition;
        definition.mesh.meshGuid = meshGuid;
        root = make(stem, Kind::MeshInstance3D, definition, Scene::ApplyMeshInstance3DNodeDefinitionUVE, parent);
    } else {
        root = make(stem, Kind::Object3D, Scene::Object3DNodeDefinitionUVE{}, Scene::ApplyObject3DNodeDefinitionUVE, parent);
        const Scene::EntityUVE armature =
            make("Armature", Kind::Object3D, Scene::Object3DNodeDefinitionUVE{}, Scene::ApplyObject3DNodeDefinitionUVE, root);
        Scene::Skeleton3DNodeDefinitionUVE skeletonDefinition;
        skeletonDefinition.skeleton.skeletonAssetPath = relativeSource.generic_string();
        for (const Asset::GltfJointUVE& joint : skeleton->joints) {
            skeletonDefinition.skeleton.bones.push_back(
                Scene::SkeletonBoneUVE{joint.name, joint.parentIndex, joint.translation, joint.rotation, joint.scale});
        }
        const Scene::EntityUVE skeletonEntity =
            make("Skeleton3D", Kind::Skeleton3D, skeletonDefinition, Scene::ApplySkeleton3DNodeDefinitionUVE, armature);
        if (hasMesh) {
            Scene::MeshInstance3DNodeDefinitionUVE mesh;
            mesh.mesh.meshGuid = meshGuid;
            static_cast<void>(make(stem + " Mesh", Kind::MeshInstance3D, mesh, Scene::ApplyMeshInstance3DNodeDefinitionUVE, skeletonEntity));
        }
        if (!clips.empty()) {
            Scene::AnimationSequencerNodeDefinitionUVE player;
            // Every take of the file is the player's; the first one plays.
            for (const std::filesystem::path& clipPath : clips) {
                player.player.library.push_back(m_services->GetAssetDatabaseUVE().RegisterUVE(clipPath));
            }
            player.player.clip = player.player.library.front();
            player.player.loopMode = Scene::AnimationLoopModeUVE::Loop;
            static_cast<void>(make("AnimationSequencer", Kind::AnimationSequencer, player, Scene::ApplyAnimationSequencerNodeDefinitionUVE, root));
        }
    }
    if (root == Scene::kInvalidEntityUVE) {
        return Scene::kInvalidEntityUVE;
    }
    InvalidateHierarchyFilterCacheUVE();
    PlaceNewDocumentNodeUVE(root);
    const std::optional<Scene::SceneSnapshotUVE> snapshot = CaptureSubtreeUVE(root);
    if (!snapshot.has_value()) {
        DestroyDocumentSubtreeUVE(root);
        RestoreSelectionUVE(selectionBefore);
        m_sceneDirty = dirtyBefore;
        return Scene::kInvalidEntityUVE;
    }
    SelectEntityUVE(root);
    m_sceneDirty = true;
    RecordHistoryUVE(SceneNodeCreationHistoryEntryUVE{*snapshot, Scene::ResolveSceneNodeKindUVE(entityManager, root),
                                                      root, selectionBefore, CaptureSelectionSnapshotUVE(),
                                                      dirtyBefore, true, parent});
    return root;
}

bool EditorUVE::BindSelectedSkeletonSourceUVE(const std::filesystem::path& relativeSource) {
    const Core::TypeMetadataEntryUVE* const entry =
        Scene::FindSceneComponentMetadataUVE(std::type_index(typeid(Scene::Skeleton3DNodeComponentUVE)));
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (entry == nullptr || !IsDocumentEntityUVE(m_selectedEntity) ||
        !entityManager.HasComponentUVE<Scene::Skeleton3DNodeComponentUVE>(m_selectedEntity)) {
        return false;
    }
    Scene::Skeleton3DNodeComponentUVE next =
        entityManager.GetComponentUVE<Scene::Skeleton3DNodeComponentUVE>(m_selectedEntity);
    next.skeletonAssetPath = relativeSource.generic_string();
    next.bones.clear();
    if (!relativeSource.empty()) {
        const std::optional<Asset::GltfSkeletonUVE> skeleton =
            ReadSkeletonForSourceUVE(relativeSource, Scene::kMaximumSkeletonBonesUVE);
        if (!skeleton.has_value()) {
            m_skeletonSourceStatus = relativeSource.filename().string() +
                                     " has no skeleton this engine can read (a glTF skin or FBX bones, at most " +
                                     std::to_string(Scene::kMaximumSkeletonBonesUVE) + " bones).";
            return false;
        }
        for (const Asset::GltfJointUVE& joint : skeleton->joints) {
            next.bones.push_back(Scene::SkeletonBoneUVE{joint.name, joint.parentIndex, joint.translation, joint.rotation,
                                                        joint.scale});
        }
    }
    m_skeletonSourceStatus.clear();
    m_selectedSkeletonBone.clear();
    return SetSelectedComponentValueUVE(*entry, &next);
}

void EditorUVE::DrawSkeletonSourcePropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                              const Core::TypeMetadataPropertyUVE& property, const void* const instance) {
    const auto& skeleton = *static_cast<const Scene::Skeleton3DNodeComponentUVE*>(instance);
    const bool writable = property.IsAuthoringWritableUVE() && IsAuthoringCommandAllowedUVE();
    if (!BeginSkeletonRowsUVE("##skeleton-source")) {
        return;
    }
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    // No revert icon: resetting the path alone would leave the bones behind. Clear does both.
    static_cast<void>(DrawMetadataPropertyLabelUVE(entry, property, instance, false));
    ImGui::TableSetColumnIndex(1);
    ImGui::BeginDisabled(!writable);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const std::string preview = skeleton.skeletonAssetPath.empty()
                                    ? std::string{"(none)"}
                                    : std::filesystem::path{skeleton.skeletonAssetPath}.filename().string();
    // Offered: every model source whose file declares a skeleton - the same test the Content
    // Browser uses to label a file Model rather than Mesh.
    std::filesystem::path chosen;
    bool picked = false;
    if (ImGui::BeginCombo("##skeleton-source", preview.c_str())) {
        if (ImGui::Selectable("(none)", skeleton.skeletonAssetPath.empty()) && !skeleton.skeletonAssetPath.empty()) {
            picked = true;
        }
        bool any = false;
        const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        for (const Asset::ProjectFileEntryUVE& file : project.entries) {
            const EditorModelSourceInfoUVE* const source = FindModelSourceInfoUVE(file.relativePath);
            if (file.kind == Asset::ProjectFileEntryKindUVE::Directory || source == nullptr || !source->hasSkeleton) {
                continue;
            }
            any = true;
            const std::string path = file.relativePath.generic_string();
            if (ImGui::Selectable((file.relativePath.filename().string() + "##" + path).c_str(),
                                  path == skeleton.skeletonAssetPath)) {
                chosen = file.relativePath;
                picked = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", path.c_str());
            }
        }
        if (!any) {
            ImGui::TextDisabled("No rigged models yet. Export an armature from Blender as glTF (.glb) into the project.");
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::EndTable();

    // Reload re-reads the same file, for a rig edited and re-exported; Clear empties the node.
    if (!skeleton.skeletonAssetPath.empty()) {
        ImGui::BeginDisabled(!writable);
        if (ImGui::SmallButton("Reload Bones")) {
            chosen = skeleton.skeletonAssetPath;
            picked = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Read the bones again from %s, after the rig was edited and re-exported.",
                              skeleton.skeletonAssetPath.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            chosen.clear();
            picked = true;
        }
        ImGui::EndDisabled();
    }
    if (!m_skeletonSourceStatus.empty()) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(ImVec4{0.95F, 0.62F, 0.35F, 1.0F}, "%s", m_skeletonSourceStatus.c_str());
        ImGui::PopTextWrapPos();
    }
    if (picked) {
        static_cast<void>(BindSelectedSkeletonSourceUVE(chosen));
    }
}

void EditorUVE::DrawSkeletonBonesPropertyUVE(const Core::TypeMetadataEntryUVE&,
                                             const Core::TypeMetadataPropertyUVE&, const void* const instance) {
    const auto& skeleton = *static_cast<const Scene::Skeleton3DNodeComponentUVE*>(instance);
    const std::string header = "Bones (" + std::to_string(skeleton.bones.size()) + ")##skeleton-bones";
    constexpr ImGuiTreeNodeFlags kGroupFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
    if (!DrawInspectorFoldUVE(header.c_str(), "group:skeleton-bones", true, false, kGroupFlags)) {
        return;
    }
    if (skeleton.bones.empty()) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextDisabled("No bones. Pick a Source model with an armature; bones come from the DCC tool.");
        ImGui::PopTextWrapPos();
        ImGui::TreePop();
        return;
    }

    // Children by parent, once per draw: bones are ordered parent-first, so one pass builds it.
    std::vector<std::vector<std::size_t>> children(skeleton.bones.size());
    std::vector<std::size_t> roots;
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        const std::int32_t parent = skeleton.bones[index].parentIndex;
        if (parent < 0) {
            roots.push_back(index);
        } else {
            children[static_cast<std::size_t>(parent)].push_back(index);
        }
    }
    const float listHeight = std::min(220.0F, (static_cast<float>(skeleton.bones.size()) + 1.0F) *
                                                  ImGui::GetTextLineHeightWithSpacing());
    if (ImGui::BeginChild("##bone-tree", ImVec2{0.0F, listHeight}, ImGuiChildFlags_Borders)) {
        const std::function<void(std::size_t)> drawBone = [&](const std::size_t index) {
            const Scene::SkeletonBoneUVE& bone = skeleton.bones[index];
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen |
                                       ImGuiTreeNodeFlags_OpenOnArrow;
            if (children[index].empty()) {
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }
            if (bone.name == m_selectedSkeletonBone) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            ImGui::PushID(static_cast<int>(index));
            const bool open = ImGui::TreeNodeEx("##bone", flags, "%s", bone.name.c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                m_selectedSkeletonBone = bone.name;
            }
            ImGui::PopID();
            if (open && !children[index].empty()) {
                for (const std::size_t child : children[index]) {
                    drawBone(child);
                }
                ImGui::TreePop();
            }
        };
        for (const std::size_t root : roots) {
            drawBone(root);
        }
    }
    ImGui::EndChild();

    // The selected bone's rest pose, relative to its parent, as authored in the DCC tool.
    const auto selected = std::find_if(skeleton.bones.begin(), skeleton.bones.end(),
                                       [this](const Scene::SkeletonBoneUVE& bone) { return bone.name == m_selectedSkeletonBone; });
    if (selected != skeleton.bones.end() && BeginSkeletonRowsUVE("##bone-rest")) {
        constexpr float kDegrees = 180.0F / std::numbers::pi_v<float>;
        Math::Vector3UVE euler{};
        const bool haveEuler = Math::TryToEulerUVE(selected->localRotation, euler);
        const std::string parent = selected->parentIndex < 0
                                       ? std::string{"(root)"}
                                       : skeleton.bones[static_cast<std::size_t>(selected->parentIndex)].name;
        std::array<char, 96> text{};
        ReadOnlyRowUVE("Parent", parent.c_str());
        std::snprintf(text.data(), text.size(), "%.3f  %.3f  %.3f", static_cast<double>(selected->localPosition.x),
                      static_cast<double>(selected->localPosition.y), static_cast<double>(selected->localPosition.z));
        ReadOnlyRowUVE("Rest Position", text.data());
        if (haveEuler) {
            std::snprintf(text.data(), text.size(), "%.1f  %.1f  %.1f", static_cast<double>(euler.x * kDegrees),
                          static_cast<double>(euler.y * kDegrees), static_cast<double>(euler.z * kDegrees));
        } else {
            std::snprintf(text.data(), text.size(), "-");
        }
        ReadOnlyRowUVE("Rest Rotation", text.data());
        std::snprintf(text.data(), text.size(), "%.3f  %.3f  %.3f", static_cast<double>(selected->localScale.x),
                      static_cast<double>(selected->localScale.y), static_cast<double>(selected->localScale.z));
        ReadOnlyRowUVE("Rest Scale", text.data());
        ImGui::EndTable();
    }
    ImGui::TreePop();
}

void EditorUVE::BuildSkeletonOverlayUVE(std::vector<ViewportBoneUVE>& outBones) const {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const auto toArray = [](const Math::Vector3UVE& value) { return std::array<float, 3>{value.x, value.y, value.z}; };
    const auto subtract = [](const Math::Vector3UVE& a, const Math::Vector3UVE& b) {
        return Math::Vector3UVE{a.x - b.x, a.y - b.y, a.z - b.z};
    };
    const auto length = [](const Math::Vector3UVE& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); };
    entityManager.ForEachUVE<Scene::Skeleton3DNodeComponentUVE, Scene::WorldTransformComponentUVE>(
        [&](const Scene::EntityUVE entity, const Scene::Skeleton3DNodeComponentUVE& skeleton,
            const Scene::WorldTransformComponentUVE& world) {
            if (!skeleton.enabled || skeleton.bones.empty() || !IsDocumentEntityUVE(entity)) {
                return;
            }
            if (entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity)) {
                const auto& visibility = entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(entity);
                if (!visibility.visible || !visibility.visibleInHierarchy) {
                    return;
                }
            }
            // Current pose (the animated one while a player drives it, else rest) in world space:
            // each bone is its parent's frame times its own local TRS, and bones are stored
            // parent-first, so one forward pass is enough.
            const std::size_t count = skeleton.bones.size();
            const std::vector<Scene::SkeletonBonePoseUVE> pose = Scene::GetSkeletonCurrentPoseUVE(skeleton);
            std::vector<Math::Matrix4x4UVE> frames(count);
            std::vector<Math::Vector3UVE> heads(count);
            std::vector<Math::Vector3UVE> upAxes(count);
            const Math::Matrix4x4UVE root =
                Math::Matrix4x4UVE::ComposeTrsUVE(world.worldPosition, world.worldRotation, world.worldScale);
            for (std::size_t index = 0U; index < count; ++index) {
                const Scene::SkeletonBoneUVE& bone = skeleton.bones[index];
                const Scene::SkeletonBonePoseUVE& bonePose = pose[index];
                const Math::Matrix4x4UVE local =
                    Math::Matrix4x4UVE::ComposeTrsUVE(bonePose.position, bonePose.rotation, bonePose.scale);
                frames[index] = (bone.parentIndex < 0 ? root : frames[static_cast<std::size_t>(bone.parentIndex)]) * local;
                heads[index] = Math::TransformPointUVE(frames[index], Math::Vector3UVE{});
                upAxes[index] = subtract(Math::TransformPointUVE(frames[index], Math::Vector3UVE{0.0F, 1.0F, 0.0F}),
                                         heads[index]);
            }
            // A bone points along its own +Y (the convention DCC exporters use) and ends at the
            // child that continues that line; a leaf gets a length from its parent.
            std::vector<Math::Vector3UVE> tails(count);
            std::vector<float> lengths(count, 0.0F);
            for (std::size_t index = 0U; index < count; ++index) {
                const float upLength = length(upAxes[index]);
                const Math::Vector3UVE up = upLength > 1.0e-6F
                                                ? Math::Vector3UVE{upAxes[index].x / upLength, upAxes[index].y / upLength,
                                                                   upAxes[index].z / upLength}
                                                : Math::Vector3UVE{0.0F, 1.0F, 0.0F};
                float bestScore = 0.7F;
                for (std::size_t child = index + 1U; child < count; ++child) {
                    if (skeleton.bones[child].parentIndex != static_cast<std::int32_t>(index)) {
                        continue;
                    }
                    const Math::Vector3UVE offset = subtract(heads[child], heads[index]);
                    const float offsetLength = length(offset);
                    if (offsetLength < 1.0e-4F) {
                        continue;
                    }
                    const float score = (offset.x * up.x + offset.y * up.y + offset.z * up.z) / offsetLength;
                    if (score > bestScore) {
                        bestScore = score;
                        tails[index] = heads[child];
                        lengths[index] = offsetLength;
                    }
                }
                if (lengths[index] <= 0.0F) {
                    const std::int32_t parent = skeleton.bones[index].parentIndex;
                    const float fallback = parent >= 0 && lengths[static_cast<std::size_t>(parent)] > 0.0F
                                               ? lengths[static_cast<std::size_t>(parent)] * 0.6F
                                               : 0.1F;
                    lengths[index] = fallback;
                    tails[index] = Math::Vector3UVE{heads[index].x + up.x * fallback, heads[index].y + up.y * fallback,
                                                    heads[index].z + up.z * fallback};
                }
            }
            const bool selected = entity == m_selectedEntity;
            for (std::size_t index = 0U; index < count; ++index) {
                ViewportBoneUVE bone;
                bone.head = toArray(heads[index]);
                bone.tail = toArray(tails[index]);
                bone.side = toArray(subtract(Math::TransformPointUVE(frames[index], Math::Vector3UVE{1.0F, 0.0F, 0.0F}),
                                             heads[index]));
                const std::int32_t parent = skeleton.bones[index].parentIndex;
                if (parent >= 0 && length(subtract(tails[static_cast<std::size_t>(parent)], heads[index])) > 1.0e-4F) {
                    bone.hasLink = true;
                    bone.linkFrom = toArray(tails[static_cast<std::size_t>(parent)]);
                }
                bone.skeletonSelected = selected;
                bone.boneSelected = selected && skeleton.bones[index].name == m_selectedSkeletonBone;
                if (m_retargetPreview.has_value()) {
                    // The Retarget preview colours joints by how the character answers the humanoid.
                    const RetargetPreviewUVE& preview = *m_retargetPreview;
                    if (entity == preview.sourceSkeleton && index < preview.sourceColours.size()) {
                        bone.hasColour = true;
                        bone.colour = preview.sourceColours[index];
                    } else if (entity == preview.targetSkeleton) {
                        if (const auto found = preview.targetColours.find(skeleton.bones[index].name);
                            found != preview.targetColours.end()) {
                            bone.hasColour = true;
                            bone.colour = found->second;
                        }
                    }
                }
                outBones.push_back(bone);
            }
        });
}

} // namespace UVE::Editor
