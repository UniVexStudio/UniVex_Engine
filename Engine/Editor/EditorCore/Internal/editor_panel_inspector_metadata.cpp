// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The metadata-driven half of the Inspector: one drawer per declared component type, built from
// what the type says about itself rather than from a hand-written form per type.
//
// What this replaced. The Inspector used to dispatch on EditorSceneComponentKindUVE - a closed
// enum with a positionally-parallel std::variant - across eight sites, plus roughly ninety
// per-type branches in the panel file. Adding one component meant editing a dozen places in five
// files, and because that was expensive, nine of the twenty-two drawers had never been finished:
// they showed a title and a Remove button, so a camera's field of view, a light's colour, a
// collider's extents and a rigid body's mass were not editable at all.
//
// Here a component declares its properties once (Engine/Runtime/Scene/Internal/
// scene_component_metadata_uve.cpp) and the Inspector reads the declaration. The number of cases
// this file handles is bounded by the engine's value types - about a dozen - not by its component
// types. A new component becomes fully inspectable by being declared, and nothing here changes.
//
// What is deliberately NOT generic. Three drawers stay hand-written because a generic editor
// would be wrong rather than merely plain, and each says so at its exclusion below.

#include "editor.h"
#include "model_animations.h"
#include "logging_macros.h"
#include "scalar.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <typeindex>
#include <utility>
#include <vector>

#include "asset_guid.h"
#include "i_asset_database.h"
#include "i_asset_importer.h"
#include "i_project_file_index.h"
#include "texture_import_settings.h"
#include "camera_component.h"
#include "character_controller_component.h"
#include "collider_component.h"
#include "editor_description_component.h"
#include "entity.h"
#include "transform_component.h"
#include "camera_3d.h"
#include "visibility_component.h"
#include "material_asset.h"
#include "shader_asset.h"
#include "engine_project_settings.h"
#include "i_entity_manager.h"
#include "color.h"
#include "rect.h"
#include "vector2.h"
#include "vector3.h"
#include "bone_attachment_3d.h"
#include "lod_group_3d.h"
#include "sky_3d.h"
#include "skeleton_3d.h"
#include "two_bone_ik_3d.h"
#include "hierarchy_component.h"
#include "morph_target_component.h"
#include "mesh_component.h"
#include "name_component.h"
#include "primitive_mesh_component.h"
#include "skin_component.h"
#include "surface_instance_component.h"
#include "gltf_skeleton.h"
#include "mesh_asset.h"

namespace UVE::Editor {
namespace {

using Core::TypeMetadataEntryUVE;
using Core::TypeMetadataPropertyFlagsUVE;
using Core::TypeMetadataPropertyUVE;

[[nodiscard]] std::vector<std::string> FileDrawerExtensionsUVE(std::string_view drawerId);
[[nodiscard]] std::filesystem::path ClampPickerStartDirectoryUVE(const Asset::ProjectFileSnapshotUVE& project,
                                                                 const std::filesystem::path& wanted);

}

void EditorUVE::ApplyPendingComponentChangeUVE() {
    // A structural edit a row asked for last frame runs here, once, before any section takes a
    // pointer into an entity's component storage: adding or removing a component moves the entity
    // into another archetype, which frees the chunk the row that asked was drawn from. The row
    // therefore records the intent and this turns it into the edit - see the field's own comment.
    const PendingComponentChangeUVE change = m_pendingComponentChange;
    const Scene::EntityUVE target = m_pendingComponentChangeEntity;
    m_pendingComponentChange = PendingComponentChangeUVE::None;
    m_pendingComponentChangeEntity = Scene::kInvalidEntityUVE;
    if (change == PendingComponentChangeUVE::None || target == Scene::kInvalidEntityUVE) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!IsDocumentEntityUVE(target) || !entityManager.IsAliveUVE(target)) {
        return;
    }
    const bool hasSky = entityManager.HasComponentUVE<Scene::Sky3DComponentUVE>(target);
    if (change == PendingComponentChangeUVE::AddSky && !hasSky) {
        entityManager.AddComponentUVE<Scene::Sky3DComponentUVE>(target, Scene::Sky3DComponentUVE{});
    } else if (change == PendingComponentChangeUVE::RemoveSky && hasSky) {
        entityManager.RemoveComponentUVE<Scene::Sky3DComponentUVE>(target);
    }
}

namespace {

/// "file:uvaudio,wav" -> {".uvaudio", ".wav"}.
[[nodiscard]] std::vector<std::string> FileDrawerExtensionsUVE(std::string_view drawerId) {
    constexpr std::string_view kPrefix = "file:";
    if (drawerId.size() <= kPrefix.size()) {
        return {};
    }
    std::vector<std::string> extensions;
    std::string_view rest = drawerId.substr(kPrefix.size());
    while (!rest.empty()) {
        const std::size_t comma = rest.find(',');
        extensions.push_back("." + std::string{rest.substr(0U, comma)});
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1U);
    }
    return extensions;
}

/// `wanted` when it is Content itself or a folder the snapshot knows, else Content itself.
[[nodiscard]] std::filesystem::path ClampPickerStartDirectoryUVE(const Asset::ProjectFileSnapshotUVE& project,
                                                                 const std::filesystem::path& wanted) {
    if (wanted.empty()) {
        return std::filesystem::path{};
    }
    for (const Asset::ProjectFileEntryUVE& entry : project.entries) {
        if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory && entry.relativePath == wanted) {
            return wanted;
        }
    }
    return std::filesystem::path{};
}

} // namespace

Asset::AssetGuidUVE EditorUVE::PublishTextureSourceUVE(const std::filesystem::path& absolutePath) {
    Asset::IAssetDatabaseUVE& database = m_services->GetAssetDatabaseUVE();
    std::error_code error;
    if (!std::filesystem::is_regular_file(absolutePath, error)) {
        return Asset::kInvalidAssetGuidUVE;
    }
    std::string extension = absolutePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (extension == ".uvtex") {
        return database.RegisterUVE(absolutePath);
    }
    // The published form of this source, beside it - where the model importers publish a model's
    // textures too, and where the picker already looks for it when deciding which row to offer.
    const std::filesystem::path publishedPath =
        absolutePath.parent_path() / (absolutePath.stem().string() + ".uvtex");
    if (std::filesystem::is_regular_file(publishedPath, error)) {
        return database.RegisterUVE(publishedPath);
    }
    const Asset::AssetGuidUVE imported = m_services->GetAssetImporterUVE().ImportUVE(
        absolutePath, publishedPath, database, Asset::TextureImportSettingsUVE{});
    if (imported == Asset::kInvalidAssetGuidUVE) {
        // No importer for this kind, or the decode refused it. Registering the source keeps the
        // slot showing the picture it was given - the runtime texture loader decodes a source
        // image itself - instead of emptying a slot the author had just filled.
        return database.RegisterUVE(absolutePath);
    }
    // The published texture is a new file in the project; Content must show it now, not on the
    // next rescan, or the author sees a slot pointing at something that does not appear to exist.
    RefreshProjectFileIndexUVE();
    return imported;
}

Asset::AssetGuidUVE EditorUVE::PublishModelSourceUVE(const std::filesystem::path& absolutePath) {
    Asset::IAssetDatabaseUVE& database = m_services->GetAssetDatabaseUVE();
    std::error_code error;
    if (!std::filesystem::is_regular_file(absolutePath, error)) {
        return Asset::kInvalidAssetGuidUVE;
    }
    std::string extension = absolutePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (extension == ".uvmodel") {
        static_cast<void>(RegisterImportedModelArtifactsUVE(absolutePath));
        return database.RegisterUVE(absolutePath);
    }
    // Not a model at all - a picker that was opened for some other asset kind. Registering it is
    // still the right answer: this is the published form of what the author picked.
    if (extension != ".fbx" && extension != ".glb" && extension != ".gltf" && extension != ".obj") {
        return database.RegisterUVE(absolutePath);
    }

    // A source outside the project must come inside it first. The converted mesh is keyed on the
    // content-relative path, so a file imported straight from the Desktop would convert to a
    // .uvmodel outside the project - a mesh that works today and is gone the moment the project
    // folder is moved or shared.
    const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    std::filesystem::path relative = absolutePath.lexically_relative(project.contentRoot);
    if (relative.empty() || *relative.begin() == "..") {
        const std::filesystem::path modelDirectory = project.contentRoot / "Models";
        std::filesystem::create_directories(modelDirectory, error);
        const std::filesystem::path destination =
            MakeUniqueContentPathUVE(modelDirectory, absolutePath.stem().string(), extension);
        std::filesystem::copy_file(absolutePath, destination, error);
        if (error) {
            UVE_ERROR("EditorUVE: could not copy model \"{}\" into Content: {}", absolutePath.string(),
                      error.message());
            return Asset::kInvalidAssetGuidUVE;
        }
        relative = destination.lexically_relative(project.contentRoot);
    }

    const std::filesystem::path importedPath = GetImportedModelPathUVE(relative);
    // If the imported .uvmodel is already present and up to date, reuse it directly without
    // triggering a full project file index rescan and thumbnail flush.
    if (std::filesystem::is_regular_file(importedPath, error)) {
        std::error_code timeError;
        const auto sourceTime = std::filesystem::last_write_time(project.contentRoot / relative, timeError);
        const auto importedTime = timeError ? std::filesystem::file_time_type::min()
                                            : std::filesystem::last_write_time(importedPath, timeError);
        if (!timeError && importedTime >= sourceTime) {
            return RegisterImportedModelArtifactsUVE(importedPath);
        }
    }

    std::filesystem::create_directories(importedPath.parent_path(), error);
    const Asset::AssetGuidUVE imported = m_services->GetAssetImporterUVE().ImportUVE(
        project.contentRoot / relative, importedPath, database, Asset::AssetImportSettingsUVE{});
    if (imported == Asset::kInvalidAssetGuidUVE) {
        // No GUID rather than the source's. A slot naming a file the mesh loader cannot read looks
        // filled and draws nothing forever; an unchanged slot at least tells the truth.
        return Asset::kInvalidAssetGuidUVE;
    }
    static_cast<void>(RegisterImportedModelArtifactsUVE(importedPath));
    // The source is new to Content, and the .uvmodel is new to the project; both must appear now,
    // not on the next rescan, or the author sees a slot pointing at something that is not listed.
    RefreshProjectFileIndexUVE();
    return imported;
}

namespace {

/// How far the derived slope is pushed. At 2.0 an edge from black to white across two pixels tilts
/// about 45 degrees: strong enough that a photo reads as relief under a moving light, weak enough
/// that its cast shadows stay shadows instead of becoming cliffs.
constexpr float kDerivedNormalStrengthUVE = 2.0F;

} // namespace

std::optional<Asset::AssetGuidUVE> EditorUVE::DeriveNormalMapForAlbedoUVE(const Asset::AssetGuidUVE albedo) {
    if (albedo == Asset::kInvalidAssetGuidUVE) {
        return std::nullopt;
    }
    Asset::IAssetDatabaseUVE& database = m_services->GetAssetDatabaseUVE();
    const std::filesystem::path albedoPath = database.ResolveUVE(albedo);
    if (albedoPath.empty()) {
        return std::nullopt;
    }
    Asset::TextureAssetUVE albedoTexture;
    if (!Asset::LoadTextureAssetSourceUVE(albedoPath, albedoTexture)) {
        return std::nullopt;
    }
    // Beside the picture and named after it, so the map is an asset the author can see, move and
    // edit like any other, and so picking the same picture again reuses the map it produced.
    const std::filesystem::path normalPath =
        albedoPath.parent_path() / (albedoPath.stem().string() + "_normal.uvtex");
    std::error_code error;
    if (!std::filesystem::is_regular_file(normalPath, error)) {
        Asset::TextureAssetUVE normal;
        if (!Asset::DeriveNormalTextureUVE(albedoTexture, kDerivedNormalStrengthUVE, normal) ||
            !Asset::SaveTextureAssetUVE(normal, normalPath)) {
            return std::nullopt;
        }
        RefreshProjectFileIndexUVE();
    }
    const Asset::AssetGuidUVE derived = database.RegisterUVE(normalPath);
    if (derived == Asset::kInvalidAssetGuidUVE) {
        return std::nullopt;
    }
    return derived;
}

bool EditorUVE::AutoPopulateSkinFromModelUVE(const Scene::EntityUVE entity, Scene::SkinComponentUVE& skin) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();

    // 1. Try to find Skeleton3D on entity, its children, siblings, or parent
    const Scene::Skeleton3DComponentUVE* skelComp = nullptr;
    if (entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(entity)) {
        skelComp = &entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(entity);
    } else {
        for (const Scene::EntityUVE child : m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, entity)) {
            if (entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(child)) {
                skelComp = &entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(child);
                break;
            }
        }
        if (skelComp == nullptr && entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(entity)) {
            const Scene::EntityUVE parent = entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(entity).parent;
            if (parent != Scene::kInvalidEntityUVE) {
                if (entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(parent)) {
                    skelComp = &entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(parent);
                } else {
                    for (const Scene::EntityUVE sibling :
                         m_services->GetSceneGraphUVE().GetChildrenUVE(entityManager, parent)) {
                        if (entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(sibling)) {
                            skelComp = &entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(sibling);
                            break;
                        }
                    }
                }
            }
        }
    }
    if (skelComp != nullptr && !skelComp->bones.empty()) {
        // A mesh and a skeleton in the same place are bound by NAME, through the same lookup the
        // renderer poses with. Binding them by position instead - the skeleton's bone 3 to the
        // mesh's joint 3 - is only right when the two lists came from one export in one order, and
        // silently wrong in every other case: the Inspector would show a configured Skin that
        // poses the model into a knot.
        const std::optional<Asset::MeshAssetUVE> meshAsset = LoadEntityMeshAssetUVE(entity);
        if (meshAsset.has_value() &&
            Scene::TryBuildSkinFromMeshJointsUVE(*meshAsset, *skelComp, skin)) {
            return true;
        }
        // Nothing to match against - a skeleton with no skinned mesh of its own - so the binds are
        // the bones themselves, in order, with an identity inverse bind pose.
        skin.boneBinds.clear();
        skin.boneBinds.reserve(skelComp->bones.size());
        for (std::size_t i = 0; i < skelComp->bones.size(); ++i) {
            Scene::SkinBoneBindUVE bind;
            bind.boneName = skelComp->bones[i].name;
            bind.boneIndex = static_cast<std::int32_t>(i);
            bind.inverseBindPose = Math::Matrix4x4UVE::IdentityUVE();
            skin.boneBinds.push_back(std::move(bind));
        }
        skin.bindCount = static_cast<std::uint32_t>(skin.boneBinds.size());
        return true;
    }

    // 2. Read skeleton from assigned model in MeshComponentUVE (using cached LoadEntityMeshAssetUVE)
    if (const std::optional<Asset::MeshAssetUVE> meshAsset = LoadEntityMeshAssetUVE(entity);
        meshAsset.has_value() && !meshAsset->joints.empty()) {
        skin.boneBinds.clear();
        skin.boneBinds.reserve(meshAsset->joints.size());
        for (std::size_t i = 0; i < meshAsset->joints.size(); ++i) {
            Scene::SkinBoneBindUVE bind;
            bind.boneName = meshAsset->joints[i].name.empty()
                                ? ("Bone_" + std::to_string(i))
                                : meshAsset->joints[i].name;
            bind.boneIndex = static_cast<std::int32_t>(i);
            bind.inverseBindPose = meshAsset->joints[i].inverseBindMatrix;
            skin.boneBinds.push_back(std::move(bind));
        }
        skin.bindCount = static_cast<std::uint32_t>(skin.boneBinds.size());
        return true;
    }
    return false;
}

std::optional<Asset::MeshAssetUVE> EditorUVE::LoadEntityMeshAssetUVE(const Scene::EntityUVE entity) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::MeshComponentUVE>(entity)) {
        return std::nullopt;
    }
    const Scene::MeshComponentUVE meshComponent = entityManager.GetComponentUVE<Scene::MeshComponentUVE>(entity);
    if (meshComponent.meshGuid == Asset::kInvalidAssetGuidUVE) {
        return std::nullopt;
    }
    std::filesystem::path resolved = m_services->GetAssetDatabaseUVE().ResolveUVE(meshComponent.meshGuid);
    std::error_code ec;
    if (resolved.extension() != ".uvmodel" || !std::filesystem::is_regular_file(resolved, ec)) {
        // A model source (.glb/.fbx/...) draws through the .uvmodel the importer wrote; resolve
        // that the same way the skin and material paths do.
        const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        const std::filesystem::path relative = resolved.lexically_relative(project.contentRoot);
        if (!relative.empty() && *relative.begin() != "..") {
            const std::filesystem::path imported = GetImportedModelPathUVE(relative);
            if (std::filesystem::is_regular_file(imported, ec)) {
                resolved = imported;
            }
        }
    }
    if (resolved.empty() || !std::filesystem::is_regular_file(resolved, ec)) {
        return std::nullopt;
    }
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(resolved, ec);
    if (!ec && m_inspectorMeshAssetCache.has_value() && m_inspectorMeshAssetCache->resolvedPath == resolved &&
        m_inspectorMeshAssetCache->lastWriteTime == writeTime) {
        return m_inspectorMeshAssetCache->meshAsset;
    }
    Asset::MeshAssetUVE meshAsset;
    if (!Asset::LoadMeshAssetUVE(resolved, meshAsset)) {
        return std::nullopt;
    }
    if (!ec) {
        m_inspectorMeshAssetCache = EntityEditorMeshCacheEntryUVE{resolved, writeTime, meshAsset};
    }
    return meshAsset;
}

bool EditorUVE::IsPropertyAtDefaultUVE(const TypeMetadataEntryUVE& entry, const TypeMetadataPropertyUVE& property,
                                       const void* const instance) {
    if (property.areEqual == nullptr || !entry.HasFactoryUVE()) {
        return false;
    }
    auto defaults = m_inspectorDefaultInstances.find(&entry);
    if (defaults == m_inspectorDefaultInstances.end()) {
        defaults = m_inspectorDefaultInstances.emplace(&entry, Core::TypeInstanceUVE::MakeDefaultUVE(entry)).first;
    }
    return defaults->second.IsValidUVE() && property.areEqual(defaults->second.GetUVE(), instance);
}

bool EditorUVE::SetSelectedComponentValueUVE(const TypeMetadataEntryUVE& entry, const void* const newInstance) {
    // A live edit still in flight is finished first, before this one changes anything, so the two
    // land in history in the order they happened.
    static_cast<void>(CommitComponentPropertyPreviewUVE());
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() || !entry.HasFactoryUVE() ||
        newInstance == nullptr || (entry.isInstanceValid != nullptr && !entry.isInstanceValid(newInstance))) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE(m_selectedEntity, entry.typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointerUVE(m_selectedEntity, entry.typeIndex);
    Core::TypeInstanceUVE before = Core::TypeInstanceUVE::CloneUVE(entry, instance);
    Core::TypeInstanceUVE after = Core::TypeInstanceUVE::CloneUVE(entry, newInstance);
    if (!before.IsValidUVE() || !after.IsValidUVE()) {
        return false;
    }
    entry.assignInstance(instance, newInstance);
    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    m_sceneDirty = true;
    RecordHistoryUVE(ComponentPropertyHistoryEntryUVE{m_selectedEntity, &entry, std::move(before), std::move(after),
                                                      selectionBefore, CaptureSelectionSnapshotUVE(), dirtyBefore,
                                                      true});
    return true;
}

bool EditorUVE::CopySelectedComponentUVE(const TypeMetadataEntryUVE& entry) {
    if (!HasSingleDocumentSelectionUVE() || !entry.HasFactoryUVE()) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE(m_selectedEntity, entry.typeIndex)) {
        return false;
    }
    Core::TypeInstanceUVE copy =
        Core::TypeInstanceUVE::CloneUVE(entry, entityManager.GetComponentPointerUVE(m_selectedEntity, entry.typeIndex));
    if (!copy.IsValidUVE()) {
        return false;
    }
    m_componentClipboard = ComponentClipboardUVE{&entry, std::move(copy)};
    return true;
}

bool EditorUVE::CanPasteSelectedComponentUVE(const TypeMetadataEntryUVE& entry) const noexcept {
    // Types are compared by identity, not by name: two entries can never share a type index.
    return m_componentClipboard.has_value() && m_componentClipboard->entry != nullptr &&
           m_componentClipboard->entry->typeIndex == entry.typeIndex && m_componentClipboard->value.IsValidUVE();
}

bool EditorUVE::PasteSelectedComponentUVE(const TypeMetadataEntryUVE& entry) {
    if (!CanPasteSelectedComponentUVE(entry)) {
        return false;
    }
    return SetSelectedComponentValueUVE(entry, m_componentClipboard->value.GetUVE());
}

bool EditorUVE::ResetSelectedComponentUVE(const TypeMetadataEntryUVE& entry) {
    const Core::TypeInstanceUVE defaults = Core::TypeInstanceUVE::MakeDefaultUVE(entry);
    return defaults.IsValidUVE() && SetSelectedComponentValueUVE(entry, defaults.GetUVE());
}

bool EditorUVE::CopySelectedTransformUVE() {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!HasSingleDocumentSelectionUVE() ||
        !entityManager.HasComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity)) {
        return false;
    }
    m_transformClipboard = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity);
    return true;
}

namespace {

// The local pose of `pose` written over `target`, leaving everything that is not pose alone.
Scene::TransformComponentUVE WithLocalPoseUVE(Scene::TransformComponentUVE target,
                                              const Scene::TransformComponentUVE& pose) {
    target.localPosition = pose.localPosition;
    target.localRotation = pose.localRotation;
    target.localScale = pose.localScale;
    target.localEulerRadians = pose.localEulerRadians;
    target.eulerOrder = pose.eulerOrder;
    target.rotationEditMode = pose.rotationEditMode;
    return target;
}

} // namespace

bool EditorUVE::PasteSelectedTransformUVE() {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!m_transformClipboard.has_value() || !HasSingleDocumentSelectionUVE() ||
        !entityManager.HasComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity)) {
        return false;
    }
    return SetSelectedLocalTransformUVE(WithLocalPoseUVE(
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity), *m_transformClipboard));
}

bool EditorUVE::ResetSelectedTransformUVE() {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!HasSingleDocumentSelectionUVE() ||
        !entityManager.HasComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity)) {
        return false;
    }
    return SetSelectedLocalTransformUVE(WithLocalPoseUVE(
        entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity), Scene::TransformComponentUVE{}));
}

bool EditorUVE::SetEntityVisibleUVE(const Scene::EntityUVE entity, const bool visible) {
    if (!IsAuthoringCommandAllowedUVE() || !IsDocumentEntityUVE(entity)) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity)) {
        return false;
    }
    const Core::TypeMetadataEntryUVE* const entry = Scene::GetSceneComponentMetadataRegistryUVE().FindTypeByIndexUVE(
        std::type_index(typeid(Scene::VisibilityComponentUVE)));
    if (entry == nullptr || !entry->HasFactoryUVE()) {
        return false;
    }
    auto& visibility = entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(entity);
    if (visibility.visible == visible) {
        return false;
    }
    // Same history entry as an Inspector edit, so undo restores the whole component on this
    // entity and leaves the selection exactly as it was.
    Core::TypeInstanceUVE before = Core::TypeInstanceUVE::CloneUVE(*entry, &visibility);
    if (!before.IsValidUVE()) {
        return false;
    }
    visibility.visible = visible;
    Core::TypeInstanceUVE after = Core::TypeInstanceUVE::CloneUVE(*entry, &visibility);
    if (!after.IsValidUVE()) {
        visibility.visible = !visible;
        return false;
    }
    const EditorSelectionSnapshotUVE selection = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    m_sceneDirty = true;
    RecordHistoryUVE(ComponentPropertyHistoryEntryUVE{entity, entry, std::move(before), std::move(after), selection,
                                                      selection, dirtyBefore, true});
    return true;
}

bool EditorUVE::SetSelectedComponentPropertyUVE(const TypeMetadataEntryUVE& entry,
                                                const TypeMetadataPropertyUVE& property,
                                                const void* const newValue) {
    // See SetSelectedComponentValueUVE: a live edit in flight is recorded before this one.
    static_cast<void>(CommitComponentPropertyPreviewUVE());
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
        !property.IsAuthoringWritableUVE() || !entry.HasFactoryUVE() || newValue == nullptr) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE(m_selectedEntity, entry.typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointerUVE(m_selectedEntity, entry.typeIndex);

    // The prior value is cloned before the write, so an undo restores the whole component rather
    // than replaying a reverse edit - the same guarantee the per-type commands give.
    Core::TypeInstanceUVE before = Core::TypeInstanceUVE::CloneUVE(entry, instance);
    if (!before.IsValidUVE()) {
        return false;
    }
    property.setValue(instance, newValue);
    // A collider that has just been switched to Capsule inherits a Box's numbers, and the fields'
    // own defaults (height 1.0, radius 0.5) make the two caps meet exactly: no barrel, so the
    // author picks Capsule and is shown a sphere. The fit runs here rather than on every height
    // drag, so it only repairs a shape that cannot express itself and never overrules a height the
    // author chose on purpose afterwards. It happens before the validity check and before the
    // "after" clone, so the repaired value is what gets recorded and what undo puts back.
    if (entry.typeIndex == std::type_index(typeid(Scene::ColliderComponentUVE))) {
        auto* const collider = static_cast<Scene::ColliderComponentUVE*>(instance);
        *collider = Scene::FitColliderShapeUVE(*collider);
    }
    if (entry.isInstanceValid != nullptr && !entry.isInstanceValid(instance)) {
        // The component's own rule refuses this combination; put the prior value back.
        entry.assignInstance(instance, before.GetUVE());
        return false;
    }
    // An edit that changed nothing records no history, matching what the per-type commands do.
    // A property with no equality operator is treated as changed, which over-records rather than
    // silently dropping a real edit.
    if (property.areEqual != nullptr && property.areEqual(before.GetUVE(), instance)) {
        return false;
    }

    Core::TypeInstanceUVE after = Core::TypeInstanceUVE::CloneUVE(entry, instance);
    if (!after.IsValidUVE()) {
        // The write already happened; putting the prior value back leaves the scene consistent
        // rather than mutated with no way to undo it.
        entry.assignInstance(instance, before.GetUVE());
        return false;
    }

    const EditorSelectionSnapshotUVE selectionBefore = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    m_sceneDirty = true;
    RecordHistoryUVE(ComponentPropertyHistoryEntryUVE{m_selectedEntity, &entry, std::move(before),
                                                      std::move(after), selectionBefore,
                                                      CaptureSelectionSnapshotUVE(), dirtyBefore, true});
    if (entry.typeIndex == std::type_index(typeid(Scene::CameraComponentUVE))) {
        const auto* const camera = static_cast<const Scene::CameraComponentUVE*>(instance);
        if (camera != nullptr && camera->current) {
            Scene::MakeCameraCurrentUVE(entityManager, m_selectedEntity);
        }
    }
    return true;
}

bool EditorUVE::PreviewSelectedComponentPropertyUVE(const TypeMetadataEntryUVE& entry,
                                                    const TypeMetadataPropertyUVE& property,
                                                    const void* const newValue) {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
        !property.IsAuthoringWritableUVE() || !entry.HasFactoryUVE() || newValue == nullptr) {
        return false;
    }
    if (m_componentPropertyPreview.has_value() &&
        (m_componentPropertyPreview->entity != m_selectedEntity || m_componentPropertyPreview->entry != &entry ||
         m_componentPropertyPreview->property != &property)) {
        static_cast<void>(CommitComponentPropertyPreviewUVE());
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE(m_selectedEntity, entry.typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointerUVE(m_selectedEntity, entry.typeIndex);
    Core::TypeInstanceUVE previous = Core::TypeInstanceUVE::CloneUVE(entry, instance);
    if (!previous.IsValidUVE()) {
        return false;
    }
    if (!m_componentPropertyPreview.has_value()) {
        Core::TypeInstanceUVE before = Core::TypeInstanceUVE::CloneUVE(entry, instance);
        if (!before.IsValidUVE()) {
            return false;
        }
        m_componentPropertyPreview = ComponentPropertyPreviewUVE{m_selectedEntity, &entry, &property, std::move(before),
                                                                 CaptureSelectionSnapshotUVE(), m_sceneDirty};
    }
    property.setValue(instance, newValue);
    if (entry.isInstanceValid != nullptr && !entry.isInstanceValid(instance)) {
        // Refused by the component's own rule: keep the last accepted value on screen.
        entry.assignInstance(instance, previous.GetUVE());
        return false;
    }
    m_sceneDirty = true;
    return true;
}

bool EditorUVE::CommitComponentPropertyPreviewUVE() {
    if (!m_componentPropertyPreview.has_value()) {
        return false;
    }
    ComponentPropertyPreviewUVE preview = std::move(*m_componentPropertyPreview);
    m_componentPropertyPreview.reset();
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!IsDocumentEntityUVE(preview.entity) || !entityManager.HasComponentUVE(preview.entity, preview.entry->typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointerUVE(preview.entity, preview.entry->typeIndex);
    if (preview.property->areEqual != nullptr && preview.property->areEqual(preview.before.GetUVE(), instance)) {
        // Opened and closed without a net change: nothing to undo, and nothing left unsaved.
        m_sceneDirty = preview.dirtyBefore;
        return true;
    }
    Core::TypeInstanceUVE after = Core::TypeInstanceUVE::CloneUVE(*preview.entry, instance);
    if (!after.IsValidUVE()) {
        preview.entry->assignInstance(instance, preview.before.GetUVE());
        m_sceneDirty = preview.dirtyBefore;
        return false;
    }
    m_sceneDirty = true;
    RecordHistoryUVE(ComponentPropertyHistoryEntryUVE{preview.entity, preview.entry, std::move(preview.before),
                                                      std::move(after), preview.selectionBefore,
                                                      CaptureSelectionSnapshotUVE(), preview.dirtyBefore, true});
    return true;
}

bool EditorUVE::CancelComponentPropertyPreviewUVE() {
    if (!m_componentPropertyPreview.has_value()) {
        return false;
    }
    ComponentPropertyPreviewUVE preview = std::move(*m_componentPropertyPreview);
    m_componentPropertyPreview.reset();
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!IsDocumentEntityUVE(preview.entity) || !entityManager.HasComponentUVE(preview.entity, preview.entry->typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointerUVE(preview.entity, preview.entry->typeIndex);
    preview.entry->assignInstance(instance, preview.before.GetUVE());
    m_sceneDirty = preview.dirtyBefore;
    return true;
}

bool EditorUVE::CommitComponentPropertyPreviewForUVE(const TypeMetadataEntryUVE& entry,
                                                    const TypeMetadataPropertyUVE& property) {
    if (!m_componentPropertyPreview.has_value() || m_componentPropertyPreview->entry != &entry ||
        m_componentPropertyPreview->property != &property) {
        return false;
    }
    return CommitComponentPropertyPreviewUVE();
}

bool EditorUVE::ApplyComponentPropertySnapshotUVE(const Scene::EntityUVE entity,
                                                  const TypeMetadataEntryUVE* const metadata,
                                                  const void* const snapshot) {
    if (metadata == nullptr || snapshot == nullptr || !metadata->HasFactoryUVE() ||
        !IsDocumentEntityUVE(entity)) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE(entity, metadata->typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointerUVE(entity, metadata->typeIndex);
    metadata->assignInstance(instance, snapshot);
    return true;
}

std::vector<Scene::EntityUVE> EditorUVE::GetEntityReferenceCandidatesUVE() {
    // Outliner order, by the same walk the Outliner itself uses: roots reversed onto a stack, then
    // each object's children pushed in reverse so the next one popped is the next one drawn.
    std::vector<Scene::EntityUVE> candidates;
    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    std::vector<Scene::EntityUVE> pending = GetDocumentRootsUVE();
    std::reverse(pending.begin(), pending.end());
    while (!pending.empty()) {
        const Scene::EntityUVE candidate = pending.back();
        pending.pop_back();
        std::vector<Scene::EntityUVE> children =
            m_services->GetSceneGraphUVE().GetChildrenUVE(m_services->GetEntityManagerUVE(), candidate);
        pending.insert(pending.end(), children.rbegin(), children.rend());
        if (candidate == m_selectedEntity ||
            !entityManager.HasComponentUVE<Scene::TransformComponentUVE>(candidate)) {
            continue;
        }
        candidates.push_back(candidate);
    }
    return candidates;
}

bool EditorUVE::ResetSelectedComponentPropertyUVE(const TypeMetadataEntryUVE& entry,
                                                  const TypeMetadataPropertyUVE& property) {
    if (!entry.HasFactoryUVE() || property.getValue == nullptr) {
        return false;
    }
    const Core::TypeInstanceUVE defaults = Core::TypeInstanceUVE::MakeDefaultUVE(entry);
    if (!defaults.IsValidUVE()) {
        return false;
    }
    // Read the default through the same accessor the live value uses, so the buffer is exactly the
    // right size and type for the write that follows - without this function naming either.
    std::array<std::byte, 64> buffer{};
    if (property.typeId == Scene::kPropertyTypeStringUVE) {
        std::string value;
        property.getValue(defaults.GetUVE(), &value);
        return SetSelectedComponentPropertyUVE(entry, property, &value);
    }
    if (property.typeId == Scene::kPropertyTypeAssetGuidUVE) {
        Asset::AssetGuidUVE value{};
        property.getValue(defaults.GetUVE(), &value);
        return SetSelectedComponentPropertyUVE(entry, property, &value);
    }
    if (property.typeId == Scene::kPropertyTypeEntityUVE) {
        Scene::EntityUVE value = Scene::kInvalidEntityUVE;
        property.getValue(defaults.GetUVE(), &value);
        return SetSelectedComponentPropertyUVE(entry, property, &value);
    }
    // The byte buffer is only sound for trivially copyable values, so the types allowed through it
    // are named rather than assumed. A list or other owning value (the metadata entries) is never
    // reset through here; its custom drawer owns that.
    const bool trivial = !property.enumEntries.empty() || property.typeId == Scene::kPropertyTypeBoolUVE ||
                         property.typeId == Scene::kPropertyTypeFloatUVE ||
                         property.typeId == Scene::kPropertyTypeInt32UVE ||
                         property.typeId == Scene::kPropertyTypeUInt32UVE ||
                         property.typeId == Scene::kPropertyTypeUInt8UVE ||
                         property.typeId == Scene::kPropertyTypeBitMask32UVE ||
                         property.typeId == Scene::kPropertyTypeVector2UVE ||
                         property.typeId == Scene::kPropertyTypeVector3UVE ||
                         property.typeId == Scene::kPropertyTypeRectUVE ||
                         property.typeId == Scene::kPropertyTypeColorUVE ||
                         property.typeId == Scene::kPropertyTypeLinearColorUVE ||
                         property.typeId == Scene::kPropertyTypeQuaternionUVE;
    if (!trivial) {
        return false;
    }
    property.getValue(defaults.GetUVE(), buffer.data());
    return SetSelectedComponentPropertyUVE(entry, property, buffer.data());
}

} // namespace UVE::Editor
