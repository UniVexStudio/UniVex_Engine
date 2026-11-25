// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Skeleton3D in the Inspector: where its bones come from, and what they are.
//
// Bones are authored where they belong - an armature in Blender or any DCC tool - and arrive with
// the model exported from it. This object never invents a bone; a new Skeleton3D is empty until it
// is pointed at a rigged model, and pointing it again (Reload) picks up an edited rig.

#include "editor.h"
#include "model_animations.h"
#include "skeleton_asset.h"
#include "retarget_conform.h"
#include "retarget_humanoid.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "fbx_mesh_converter.h"
#include "gltf_skeleton.h"
#include "i_entity_manager.h"
#include "visibility_component.h"
#include "world_transform_component.h"
#include "matrix4x4.h"
#include "quaternion.h"
#include "animation_sequencer.h"
#include "mesh_instance_3d.h"
#include "object_3d.h"
#include "skeleton_3d.h"
#include "scene_object_type.h"
#include "scene_component_metadata.h"

namespace UVE::Editor {
namespace {

// The skeleton in a model source, whichever format it is: a glTF skin, or an FBX's bones.
[[nodiscard]] std::optional<Asset::GltfSkeleton> ReadSkeletonSource(const std::filesystem::path& path,
                                                                          const std::size_t maximumBones) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (extension != ".fbx") {
        return Asset::ReadGltfSkeleton(path, maximumBones);
    }
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    std::ifstream file(path, std::ios::binary);
    if (error || size > Asset::kMaximumFbxMeshSourceBytes || !file) {
        return std::nullopt;
    }
    std::vector<char> bytes(static_cast<std::size_t>(size));
    if (size > 0U && !file.read(bytes.data(), static_cast<std::streamsize>(size))) {
        return std::nullopt;
    }
    return Asset::ReadFbxSkeleton(std::as_bytes(std::span<const char>(bytes)), maximumBones, Asset::FbxBoneOverflow::Truncate);
}

/// A helper bone rather than a part of the body: the root on the ground, IK targets, sockets.
/// Drawn as a joint alone - a blade to it would cut across the figure (from the floor to a hand).
[[nodiscard]] bool IsHelperBone(const std::string& name) {
    static const std::unordered_set<std::string> helpers = [] {
        std::unordered_set<std::string> out;
        const Retarget::HumanoidReference& reference = Retarget::GetHumanoidReference();
        for (std::size_t bone = 0U; bone < reference.info.size() && bone < reference.skeleton.bones.size(); ++bone) {
            const Retarget::HumanoidBoneKind kind = reference.info[bone].kind;
            if (kind == Retarget::HumanoidBoneKind::Root || kind == Retarget::HumanoidBoneKind::IK ||
                kind == Retarget::HumanoidBoneKind::Socket) {
                out.insert(reference.skeleton.bones[bone].name);
            }
        }
        return out;
    }();
    if (helpers.contains(name)) {
        return true;
    }
    // The root under another name or case ("root", "ROOT"): the same marker before and after Retarget.
    std::string lower = name;
    std::ranges::transform(lower, lower.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "root") {
        return true;
    }
    // Another tool's IK bones ("ik_foot_l", "IK_Hand_R").
    return name.size() > 2U && (name[0] == 'i' || name[0] == 'I') && (name[1] == 'k' || name[1] == 'K') &&
           (name[2] == '_' || std::isupper(static_cast<unsigned char>(name[2])) != 0);
}

} // namespace

std::optional<Asset::GltfSkeleton> Editor::ReadSkeletonForModelFile(
    const std::filesystem::path& importedPath, const std::size_t maximumBones) const {
    std::error_code ec;
    if (importedPath.empty() || !std::filesystem::is_regular_file(importedPath, ec)) {
        return std::nullopt;
    }
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(importedPath, ec);
    const Asset::MeshAsset* meshPtr = nullptr;
    Asset::MeshAsset loadedMesh;
    if (!ec && m_inspectorMeshAssetCache.has_value() &&
        m_inspectorMeshAssetCache->resolvedPath == importedPath &&
        m_inspectorMeshAssetCache->lastWriteTime == writeTime) {
        meshPtr = &m_inspectorMeshAssetCache->meshAsset;
    } else if (Asset::LoadMeshAsset(importedPath, loadedMesh)) {
        if (!ec) {
            m_inspectorMeshAssetCache = EntityEditorMeshCacheEntry{importedPath, writeTime, loadedMesh};
        }
        meshPtr = &loadedMesh;
    }
    // First, prefer the sibling .uvskel rig authored directly for this model (including Retarget's
    // A-pose humanoid skeleton), with matching joint hierarchy, binds, and scale.
    std::filesystem::path skelPath = importedPath;
    skelPath.replace_extension(".uvskel");
    Asset::SkeletonAsset skelAsset;
    const auto sameJoints = [&] {
        if (meshPtr == nullptr || meshPtr->joints.size() != skelAsset.joints.size()) {
            return meshPtr == nullptr;
        }
        for (std::size_t joint = 0U; joint < skelAsset.joints.size(); ++joint) {
            if (skelAsset.joints[joint].name != meshPtr->joints[joint].name) {
                return false;
            }
        }
        return true;
    };
    if (std::filesystem::is_regular_file(skelPath, ec) && Asset::LoadSkeletonAsset(skelPath, skelAsset) &&
        !skelAsset.joints.empty() && skelAsset.joints.size() <= maximumBones && sameJoints()) {
        Asset::GltfSkeleton skeleton;
        skeleton.skinCount = 1U;
        skeleton.joints.reserve(skelAsset.joints.size());
        for (const Asset::SkeletonJoint& joint : skelAsset.joints) {
            Asset::GltfJoint gltfJoint;
            gltfJoint.name = joint.name;
            gltfJoint.parentIndex = joint.parent;
            gltfJoint.translation = joint.position;
            gltfJoint.rotation = joint.rotation;
            gltfJoint.scale = joint.scale;
            skeleton.joints.push_back(std::move(gltfJoint));
        }
        return skeleton;
    }

    if (meshPtr != nullptr && !meshPtr->joints.empty()) {
        if (const std::optional<Retarget::RetargetSkeleton> rig = Retarget::RigFromMesh(*meshPtr);
            rig.has_value() && !rig->bones.empty() && rig->bones.size() <= maximumBones) {
            Asset::GltfSkeleton skeleton;
            skeleton.skinCount = 1U;
            skeleton.joints.reserve(rig->bones.size());
            for (const Retarget::RetargetBone& bone : rig->bones) {
                Asset::GltfJoint joint;
                joint.name = bone.name;
                joint.parentIndex = bone.parent;
                joint.translation = bone.position;
                joint.rotation = bone.rotation;
                joint.scale = Math::Vector3{bone.scale, bone.scale, bone.scale};
                skeleton.joints.push_back(std::move(joint));
            }
            return skeleton;
        }
    }
    return std::nullopt;
}

std::optional<Asset::GltfSkeleton> Editor::ReadSkeletonForSource(const std::filesystem::path& relativeSource,
                                                                          const std::size_t maximumBones) const {
    // Use the imported .uvmodel (+ sibling .uvskel) directly whenever available so we never re-parse
    // the raw multi-megabyte .fbx on the UI thread, and so characters conformed by Retarget retain
    // their humanoid A-pose rig whether referenced by source (.fbx/.glb) or directly by .uvmodel.
    std::string ext = relativeSource.extension().string();
    std::ranges::transform(ext, ext.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".uvmodel") {
        const std::filesystem::path modelPath =
            relativeSource.is_absolute()
                ? relativeSource
                : (m_services->GetProjectFileIndex().GetSnapshot().contentRoot / relativeSource);
        return ReadSkeletonForModelFile(modelPath, maximumBones);
    }
    if (IsModelSourcePath(relativeSource)) {
        if (const std::optional<Asset::GltfSkeleton> imported =
                ReadSkeletonForModelFile(GetImportedModelPath(relativeSource), maximumBones);
            imported.has_value()) {
            return imported;
        }
    }
    return ReadSkeletonSource(m_services->GetProjectFileIndex().GetSnapshot().contentRoot / relativeSource,
                                 maximumBones);
}

Scene::Entity Editor::PlaceModelSource(const std::filesystem::path& relativeSource,
                                                Scene::Entity parent,
                                                const std::filesystem::path& relativeClip) {
    std::string sourceExt = relativeSource.extension().string();
    std::ranges::transform(sourceExt, sourceExt.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool isDirectUvModel = sourceExt == ".uvmodel";
    if (!IsAuthoringCommandAllowed() || (!IsModelSourcePath(relativeSource) && !isDirectUvModel)) {
        return Scene::kInvalidEntity;
    }
    const Asset::ProjectFileSnapshot project = m_services->GetProjectFileIndex().GetSnapshot();
    const std::filesystem::path source =
        relativeSource.is_absolute() ? relativeSource : (project.contentRoot / relativeSource);
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error)) {
        return Scene::kInvalidEntity;
    }
    const std::optional<Asset::GltfSkeleton> skeleton = ReadSkeletonForSource(relativeSource, Scene::kMaximumSkeletonBones);
    const std::filesystem::path importedModel = isDirectUvModel ? source : GetImportedModelPath(relativeSource);
    const bool hasMesh = std::filesystem::is_regular_file(importedModel, error);
    // An object is built only from a finished import. What used to happen here - a file that could
    // be read for bones but whose converted mesh was not written yet - produced an Armature with no
    // mesh, indistinguishable from an animation-only import, and nothing ever gave it one.
    EditorModelSourceInfo sourceInfo;
    if (!isDirectUvModel) {
        const std::string* const importFailure = FindModelImportFailure(relativeSource);
        // A retry in flight outranks the previous attempt's reason: the file may already be fixed, and
        // "still importing" is the truth about this moment.
        const bool importInFlight = m_modelImportJobs.contains(relativeSource.generic_string());
        // Read on demand rather than only from the refresh cache: a placement that ran first used to see
        // "no information" and wait forever for a mesh that was never going to be converted, because a
        // bones-only file is an animation and is never queued as a mesh at all.
        sourceInfo = ReadModelSourceInfoForSource(relativeSource);
        switch (DecideModelSourcePlacement(hasMesh, &sourceInfo, importFailure != nullptr && !importInFlight)) {
        case ModelSourcePlacement::PlaceModel:
        case ModelSourcePlacement::PlaceAnimationOnly:
            break;
        case ModelSourcePlacement::ImportFailed:
            m_contentStatusMessage = relativeSource.filename().string() + " could not be imported: " +
                                     (importFailure != nullptr ? *importFailure : std::string{"unknown reason"});
            return Scene::kInvalidEntity;
        case ModelSourcePlacement::WaitForImport:
            m_contentStatusMessage = relativeSource.filename().string() +
                                     " is still importing; nothing was placed. Try again once its mesh is converted.";
            return Scene::kInvalidEntity;
        }
    }
    if (!hasMesh && !skeleton.has_value()) {
        // The source claims motion but no bone survived the read, so there is nothing to build.
        m_contentStatusMessage = relativeSource.filename().string() + " has no baked motion to place.";
        return Scene::kInvalidEntity;
    }
    // The file's takes, imported beside it as <file>_<take>.uvanim (importing them now if needed).
    std::vector<std::filesystem::path> clips;
    if (skeleton.has_value()) {
        if (!isDirectUvModel && sourceInfo.animationCount > 0U) {
            static_cast<void>(ImportModelAnimations(source));
        }
        // A model with a mesh keeps its takes inside it: those are its player's animations.
        for (const Asset::ModelAnimationEntry& take : Asset::ListModelAnimations(importedModel)) {
            clips.push_back(Asset::MakeEmbeddedClipPath(importedModel, take.name));
        }
        const std::string prefix = source.stem().string() + "_";
        std::filesystem::path fragmentModel;
        std::string fragmentTake;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(source.parent_path(), error)) {
            const std::string name = entry.path().filename().string();
            // "<FBX>.uvanim" (one take) or "<FBX>_<take>.uvanim" (several).
            if (!clips.empty() && Asset::SplitEmbeddedClipPath(clips.front(), fragmentModel, fragmentTake)) {
                break;   // the takes are in the model: no loose files to gather
            }
            if (entry.path().extension() == ".uvanim" &&
                (name.starts_with(prefix) || entry.path().stem() == source.stem())) {
                clips.push_back(entry.path());
            }
        }
        std::sort(clips.begin(), clips.end());
    }

    if (parent == Scene::kInvalidEntity || !IsDocumentEntity(parent)) {
        parent = ResolveNewObjectParent();
    }
    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    Scene::ISceneGraph& sceneGraph = m_services->GetSceneGraph();
    const std::string stem = relativeSource.stem().string();
    using Kind = Scene::Objects::SceneObjectKind;
    const auto make = [&](const std::string& name, const Kind kind, const auto& definition, const auto& apply,
                          const Scene::Entity under) {
        const Scene::Entity entity = CreateDocumentEntityShellInternal(MakeUniqueDocumentEntityName(name));
        if (entity != Scene::kInvalidEntity) {
            apply(entityManager, entity, definition);
            Scene::SetSceneObjectKind(entityManager, entity, kind);
            if (under != Scene::kInvalidEntity) {
                sceneGraph.SetParent(entityManager, entity, under);
            }
        }
        return entity;
    };
    const Asset::AssetGuid meshGuid =
        hasMesh ? RegisterImportedModelArtifacts(importedModel) : Asset::kInvalidAssetGuid;
    const bool isParentCharacterOrEntityRoot =
        parent != Scene::kInvalidEntity &&
        (Scene::ResolveSceneObjectKind(entityManager, parent) == Scene::Objects::SceneObjectKind::Character3D ||
         Scene::ResolveSceneObjectKind(entityManager, parent) == Scene::Objects::SceneObjectKind::Player3D ||
         (m_entityDocumentActive && parent == GetEntityEditorRoot()));

    Scene::Entity root = Scene::kInvalidEntity;
    if (!skeleton.has_value()) {
        if (isParentCharacterOrEntityRoot) {
            Scene::Entity existingMesh = Scene::kInvalidEntity;
            if (entityManager.HasComponent<Scene::MeshComponent>(parent)) {
                existingMesh = parent;
            } else {
                for (const Scene::Entity child : sceneGraph.GetChildren(entityManager, parent)) {
                    if (entityManager.HasComponent<Scene::MeshComponent>(child)) {
                        existingMesh = child;
                        break;
                    }
                }
            }
            if (existingMesh != Scene::kInvalidEntity) {
                Scene::MeshComponent meshComp = entityManager.GetComponent<Scene::MeshComponent>(existingMesh);
                meshComp.meshGuid = meshGuid;
                entityManager.AddComponent<Scene::MeshComponent>(existingMesh, meshComp);
                InvalidateHierarchyFilterCache();
                SelectEntity(existingMesh);
                m_sceneDirty = true;
                return existingMesh;
            }
        }
        Scene::MeshInstance3DObjectDefinition definition;
        definition.mesh.meshGuid = meshGuid;
        root = make(stem, Kind::MeshInstance3D, definition, Scene::ApplyMeshInstance3DObjectDefinition, parent);
    } else {
        const Scene::Entity rootContainer =
            make(stem, Kind::Object3D, Scene::Object3DObjectDefinition{}, Scene::ApplyObject3DObjectDefinition, parent);
        root = rootContainer;
        Scene::Skeleton3DObjectDefinition skeletonDefinition;
        skeletonDefinition.skeleton.skeletonAssetPath = relativeSource.generic_string();
        for (const Asset::GltfJoint& joint : skeleton->joints) {
            skeletonDefinition.skeleton.bones.push_back(
                Scene::SkeletonBone{joint.name, joint.parentIndex, joint.translation, joint.rotation, joint.scale});
        }
        // A file with several mesh nodes was imported as a model per node (`<model>.part-<node>.uvmodel`):
        // placed the way another engine places it, one MeshInstance3D per node under the model, each with
        // its own mesh and materials, and ONE Skeleton3D beside them that they all bind to by name. The
        // renderer finds a skeleton that shares the mesh's parent.
        // The importer wrote the file's own object hierarchy beside the model (`<model>.uvhierarchy`: Geo > Soldier_head,
        // Soldier_body). When it is there the scene mirrors it - the model decides the hierarchy, the engine does
        // not invent one. A hierarchy that names a part file that is not there is not used.
        struct ModelObject final {
            std::string name;
            std::int32_t parent = -1;
            bool mesh = false;
            std::filesystem::path partPath;
        };
        std::vector<ModelObject> modelObjects;
        if (hasMesh) {
            std::ifstream hierarchyFile(importedModel.parent_path() / (importedModel.stem().string() + ".uvhierarchy"));
            const nlohmann::json document = nlohmann::json::parse(hierarchyFile, nullptr, false);
            if (document.is_object() && document.contains("objects") && document["objects"].is_array()) {
                bool usable = true;
                for (const nlohmann::json& entry : document["objects"]) {
                    ModelObject obj;
                    obj.name = entry.value("name", std::string{});
                    obj.parent = entry.value("parent", -1);
                    obj.mesh = entry.value("mesh", false);
                    const std::string part = entry.value("part", std::string{});
                    if (!part.empty()) {
                        obj.partPath = importedModel.parent_path() / (importedModel.stem().string() + ".part-" + part + ".uvmodel");
                        usable = usable && std::filesystem::is_regular_file(obj.partPath, error);
                    }
                    usable = usable && !obj.name.empty() && obj.parent < static_cast<std::int32_t>(modelObjects.size());
                    modelObjects.push_back(std::move(obj));
                }
                if (!usable) {
                    modelObjects.clear();
                }
            }
        }
        std::vector<std::pair<std::string, std::filesystem::path>> parts;
        if (hasMesh && modelObjects.empty()) {
            const std::string partPrefix = importedModel.stem().string() + ".part-";
            std::error_code listError;
            for (const std::filesystem::directory_entry& entry :
                 std::filesystem::directory_iterator(importedModel.parent_path(), listError)) {
                const std::string name = entry.path().filename().string();
                if (entry.is_regular_file(listError) && entry.path().extension() == ".uvmodel" &&
                    name.rfind(partPrefix, 0U) == 0U) {
                    const std::string partName = entry.path().stem().string().substr(partPrefix.size());
                    if (!partName.empty()) {
                        parts.emplace_back(partName, entry.path());
                    }
                }
            }
            std::ranges::sort(parts);
        }
        if (!modelObjects.empty()) {
            // An object's transform is not applied: a mesh's vertices already carry it, so the objects are the
            // outliner's names and grouping, and every one sits at its parent's origin.
            std::vector<Scene::Entity> built(modelObjects.size(), Scene::kInvalidEntity);
            for (std::size_t index = 0U; index < modelObjects.size(); ++index) {
                const ModelObject& obj = modelObjects[index];
                const Scene::Entity under =
                    obj.parent >= 0 ? built[static_cast<std::size_t>(obj.parent)] : rootContainer;
                if (obj.mesh) {
                    Scene::MeshInstance3DObjectDefinition mesh;
                    mesh.mesh.meshGuid = obj.partPath.empty() ? meshGuid : RegisterImportedModelArtifacts(obj.partPath);
                    built[index] = make(obj.name, Kind::MeshInstance3D, mesh, Scene::ApplyMeshInstance3DObjectDefinition, under);
                } else {
                    built[index] = make(obj.name, Kind::Object3D, Scene::Object3DObjectDefinition{},
                                        Scene::ApplyObject3DObjectDefinition, under);
                }
            }
            // Beside the top-level objects, as in the DCC file (the skeleton and `Geo` are siblings there); the
            // renderer binds a skinned mesh to the nearest skeleton under any of its ancestors.
            static_cast<void>(
                make("Skeleton3D", Kind::Skeleton3D, skeletonDefinition, Scene::ApplySkeleton3DObjectDefinition, rootContainer));
        } else if (parts.size() >= 2U) {
            for (const auto& [partName, partPath] : parts) {
                Scene::MeshInstance3DObjectDefinition mesh;
                mesh.mesh.meshGuid = RegisterImportedModelArtifacts(partPath);
                static_cast<void>(
                    make(partName, Kind::MeshInstance3D, mesh, Scene::ApplyMeshInstance3DObjectDefinition, rootContainer));
            }
            static_cast<void>(
                make("Skeleton3D", Kind::Skeleton3D, skeletonDefinition, Scene::ApplySkeleton3DObjectDefinition, rootContainer));
        } else if (hasMesh) {
            Scene::MeshInstance3DObjectDefinition mesh;
            mesh.mesh.meshGuid = meshGuid;
            const Scene::Entity characterEntity =
                make("character", Kind::MeshInstance3D, mesh, Scene::ApplyMeshInstance3DObjectDefinition, rootContainer);
            static_cast<void>(
                make("Skeleton3D", Kind::Skeleton3D, skeletonDefinition, Scene::ApplySkeleton3DObjectDefinition, characterEntity));
        } else {
            // An animation / rigged file with no mesh: Hero > Armature > Skeleton3D. The "Armature"
            // container is what a DCC tool exports for the rig and what the outliner shows for it; a
            // mesh-carrying import binds its parts to a Skeleton3D beside them instead (above).
            const Scene::Entity armature =
                make("Armature", Kind::Object3D, Scene::Object3DObjectDefinition{},
                     Scene::ApplyObject3DObjectDefinition, rootContainer);
            static_cast<void>(make("Skeleton3D", Kind::Skeleton3D, skeletonDefinition,
                                   Scene::ApplySkeleton3DObjectDefinition,
                                   armature != Scene::kInvalidEntity ? armature : rootContainer));
        }
        if (!clips.empty()) {
            Scene::AnimationSequencerObjectDefinition player;
            // Every take of the file is the player's; the one asked for plays, else the first.
            const std::filesystem::path chosenClip = relativeClip.empty() ? std::filesystem::path{} : project.contentRoot / relativeClip;
            for (const std::filesystem::path& clipPath : clips) {
                player.player.library.push_back(m_services->GetAssetDatabase().Register(clipPath));
                if (!chosenClip.empty() && clipPath == chosenClip) {
                    player.player.clip = player.player.library.back();
                }
            }
            if (player.player.clip == Asset::kInvalidAssetGuid) {
                player.player.clip = player.player.library.front();
            }
            player.player.loopMode = Scene::AnimationLoopMode::Loop;
            static_cast<void>(make("AnimationSequencer", Kind::AnimationSequencer, player, Scene::ApplyAnimationSequencerObjectDefinition, rootContainer));
        }
    }
    if (root == Scene::kInvalidEntity) {
        return Scene::kInvalidEntity;
    }
    InvalidateHierarchyFilterCache();
    if (!m_entityDocumentActive) {
        PlaceNewDocumentObject(root);
    }
    const std::optional<Scene::SceneSnapshot> snapshot = CaptureSubtree(root);
    if (!snapshot.has_value()) {
        DestroyDocumentSubtree(root);
        RestoreSelection(selectionBefore);
        m_sceneDirty = dirtyBefore;
        return Scene::kInvalidEntity;
    }
    SelectEntity(root);
    m_sceneDirty = true;
    RecordHistory(SceneObjectCreationHistoryEntry{*snapshot, Scene::ResolveSceneObjectKind(entityManager, root),
                                                      root, selectionBefore, CaptureSelectionSnapshot(),
                                                      dirtyBefore, true, parent});
    return root;
}

bool Editor::BindSelectedSkeletonSource(const std::filesystem::path& relativeSource) {
    const Core::TypeMetadataEntry* const entry =
        Scene::FindSceneComponentMetadata(std::type_index(typeid(Scene::Skeleton3DComponent)));
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (entry == nullptr || !IsDocumentEntity(m_selectedEntity) ||
        !entityManager.HasComponent<Scene::Skeleton3DComponent>(m_selectedEntity)) {
        return false;
    }
    Scene::Skeleton3DComponent next =
        entityManager.GetComponent<Scene::Skeleton3DComponent>(m_selectedEntity);
    next.skeletonAssetPath = relativeSource.generic_string();
    next.bones.clear();
    if (!relativeSource.empty()) {
        const std::optional<Asset::GltfSkeleton> skeleton =
            ReadSkeletonForSource(relativeSource, Scene::kMaximumSkeletonBones);
        if (!skeleton.has_value()) {
            m_skeletonSourceStatus = relativeSource.filename().string() +
                                     " has no skeleton this engine can read (a glTF skin or FBX bones, at most " +
                                     std::to_string(Scene::kMaximumSkeletonBones) + " bones).";
            return false;
        }
        for (const Asset::GltfJoint& joint : skeleton->joints) {
            next.bones.push_back(Scene::SkeletonBone{joint.name, joint.parentIndex, joint.translation, joint.rotation,
                                                        joint.scale});
        }
    }
    m_skeletonSourceStatus.clear();
    m_selectedSkeletonBone.clear();
    return SetSelectedComponentValue(*entry, &next);
}

void Editor::BuildSkeletonOverlay(std::vector<ViewportBone>& outBones) const {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const auto toArray = [](const Math::Vector3& value) { return std::array<float, 3>{value.x, value.y, value.z}; };
    const auto subtract = [](const Math::Vector3& a, const Math::Vector3& b) {
        return Math::Vector3{a.x - b.x, a.y - b.y, a.z - b.z};
    };
    const auto length = [](const Math::Vector3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); };
    entityManager.ForEach<Scene::Skeleton3DComponent, Scene::WorldTransformComponent>(
        [&](const Scene::Entity entity, const Scene::Skeleton3DComponent& skeleton,
            const Scene::WorldTransformComponent& world) {
            if (!skeleton.enabled || skeleton.bones.empty() || !IsDocumentEntity(entity)) {
                return;
            }
            if (entityManager.HasComponent<Scene::VisibilityComponent>(entity)) {
                const auto& visibility = entityManager.GetComponent<Scene::VisibilityComponent>(entity);
                if (!visibility.visible || !visibility.visibleInHierarchy) {
                    return;
                }
            }
            // Current pose (the animated one while a player drives it, else rest) in world space:
            // each bone is its parent's frame times its own local TRS, and bones are stored
            // parent-first, so one forward pass is enough.
            const std::size_t count = skeleton.bones.size();
            const std::vector<Scene::SkeletonBonePose> pose = Scene::GetSkeletonCurrentPose(skeleton);
            std::vector<Math::Matrix4x4> frames(count);
            std::vector<Math::Vector3> heads(count);
            std::vector<Math::Vector3> upAxes(count);
            const Math::Matrix4x4 root =
                Math::Matrix4x4::ComposeTrs(world.worldPosition, world.worldRotation, world.worldScale);
            for (std::size_t index = 0U; index < count; ++index) {
                const Scene::SkeletonBone& bone = skeleton.bones[index];
                const Scene::SkeletonBonePose& bonePose = pose[index];
                const Math::Matrix4x4 local =
                    Math::Matrix4x4::ComposeTrs(bonePose.position, bonePose.rotation, bonePose.scale);
                frames[index] = ((bone.parentIndex < 0 || static_cast<std::size_t>(bone.parentIndex) >= index)
                                     ? root
                                     : frames[static_cast<std::size_t>(bone.parentIndex)]) *
                                local;
                heads[index] = Math::TransformPoint(frames[index], Math::Vector3{});
                upAxes[index] = subtract(Math::TransformPoint(frames[index], Math::Vector3{0.0F, 1.0F, 0.0F}),
                                         heads[index]);
            }
            // The skeleton is drawn connected, as one figure: a blade from each joint to every joint
            // that hangs from it, so no bone floats apart from the next. A joint that sits on the way
            // to a sibling (a twist bone half-way down the arm) is a joint on that blade, not a blade
            // of its own; a bone with nothing below it goes on a short way the way its parent came.
            // Only where each joint is matters - the numbers are the skeleton's own, untouched.
            std::vector<std::vector<std::size_t>> children(count);
            std::vector<float> fromParent(count, 0.0F);
            for (std::size_t index = 0U; index < count; ++index) {
                const std::int32_t parent = skeleton.bones[index].parentIndex;
                if (parent >= 0 && static_cast<std::size_t>(parent) < count) {
                    children[static_cast<std::size_t>(parent)].push_back(index);
                    fromParent[index] = length(subtract(heads[index], heads[static_cast<std::size_t>(parent)]));
                }
            }
            const auto dot = [](const Math::Vector3& a, const Math::Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
            // Whether `child` lies on the way from `index` to one of its other children.
            const auto onTheWay = [&](const std::size_t index, const std::size_t child) {
                const Math::Vector3 to = subtract(heads[child], heads[index]);
                for (const std::size_t other : children[index]) {
                    if (other == child) {
                        continue;
                    }
                    const Math::Vector3 way = subtract(heads[other], heads[index]);
                    const float wayLength = length(way);
                    if (wayLength < 1.0e-4F) {
                        continue;
                    }
                    const float along = dot(to, way) / wayLength;
                    const Math::Vector3 across = subtract(to, Math::Vector3{way.x * along / wayLength, way.y * along / wayLength,
                                                                                  way.z * along / wayLength});
                    if (along > 0.0F && along < wayLength * 0.98F && length(across) < wayLength * 0.12F) {
                        return true;
                    }
                }
                return false;
            };
            std::vector<bool> helper(count, false);
            for (std::size_t index = 0U; index < count; ++index) {
                helper[index] = IsHelperBone(skeleton.bones[index].name);
            }
            std::vector<bool> inline_(count, false);
            for (std::size_t index = 0U; index < count; ++index) {
                for (const std::size_t child : children[index]) {
                    inline_[child] = inline_[child] || onTheWay(index, child);
                }
            }
            const bool selected = entity == m_selectedEntity;
            const RetargetPreview* const retarget =
                m_retargetSceneActive && m_retargetPreview.has_value() ? &*m_retargetPreview : nullptr;
            const auto emit = [&](const std::size_t index, const Math::Vector3& tail, const bool jointOnly) {
                ViewportBone bone;
                bone.head = toArray(heads[index]);
                bone.tail = toArray(tail);
                bone.side = toArray(subtract(Math::TransformPoint(frames[index], Math::Vector3{1.0F, 0.0F, 0.0F}), heads[index]));
                bone.jointOnly = jointOnly;
                bone.skeletonSelected = selected;
                bone.boneSelected = selected && skeleton.bones[index].name == m_selectedSkeletonBone;
                if (retarget != nullptr) {
                    // The Retarget preview colours joints by how the character answers the humanoid,
                    // and lights the one picked in the window.
                    if (entity == retarget->targetSkeleton) {
                        if (const auto found = retarget->targetColours.find(skeleton.bones[index].name);
                            found != retarget->targetColours.end()) {
                            bone.hasColour = true;
                            bone.colour = found->second;
                        }
                        if (!retarget->highlightBone.empty() && skeleton.bones[index].name == retarget->highlightBone) {
                            bone.boneSelected = true;
                            bone.hasColour = false;
                        }
                    }
                }
                outBones.push_back(bone);
            };
            for (std::size_t index = 0U; index < count; ++index) {
                if (helper[index]) {
                    // A marker where it is, tied to its parent by a dashed line (drawn by the viewport).
                    emit(index, Math::Vector3{heads[index].x, heads[index].y + 0.05F, heads[index].z}, true);
                    ViewportBone& marker = outBones.back();
                    marker.helper = true;
                    const std::int32_t parent = skeleton.bones[index].parentIndex;
                    if (parent >= 0 && fromParent[index] > 1.0e-4F) {
                        marker.hasLink = true;
                        marker.linkFrom = toArray(heads[static_cast<std::size_t>(parent)]);
                    }
                    continue;
                }
                bool drew = false;
                for (const std::size_t child : children[index]) {
                    if (!inline_[child] && !helper[child] && fromParent[child] >= 1.0e-4F) {
                        emit(index, heads[child], false);
                        drew = true;
                    }
                }
                if (drew) {
                    continue;
                }
                // Nothing below it to reach: on a short way the way it came (or along its own +Y).
                const std::int32_t parent = skeleton.bones[index].parentIndex;
                Math::Vector3 way = parent >= 0 ? subtract(heads[index], heads[static_cast<std::size_t>(parent)]) : upAxes[index];
                float wayLength = length(way);
                if (wayLength < 1.0e-6F) {
                    way = upAxes[index];
                    wayLength = length(way);
                }
                const Math::Vector3 dir = wayLength > 1.0e-6F ? Math::Vector3{way.x / wayLength, way.y / wayLength, way.z / wayLength}
                                                                 : Math::Vector3{0.0F, 1.0F, 0.0F};
                const float reach = std::max(parent >= 0 ? fromParent[index] * 0.45F : 0.1F, 0.01F);
                emit(index, Math::Vector3{heads[index].x + dir.x * reach, heads[index].y + dir.y * reach, heads[index].z + dir.z * reach},
                     inline_[index]);
            }
        });
}

} // namespace UVE::Editor
