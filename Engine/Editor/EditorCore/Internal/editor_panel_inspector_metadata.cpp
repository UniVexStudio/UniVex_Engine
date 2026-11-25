// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The metadata-driven half of the Inspector: one drawer per declared component type, built from
// what the type says about itself rather than from a hand-written form per type.
//
// What this replaced. The Inspector used to dispatch on EditorSceneComponentKind - a closed
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
#include "asset_import_sidecar.h"
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

using Core::TypeMetadataEntry;
using Core::TypeMetadataPropertyFlags;
using Core::TypeMetadataProperty;

[[nodiscard]] std::vector<std::string> FileDrawerExtensions(std::string_view drawerId);
[[nodiscard]] std::filesystem::path ClampPickerStartDirectory(const Asset::ProjectFileSnapshot& project,
                                                                 const std::filesystem::path& wanted);

}

void Editor::ApplyPendingComponentChange() {
    // A structural edit a row asked for last frame runs here, once, before any section takes a
    // pointer into an entity's component storage: adding or removing a component moves the entity
    // into another archetype, which frees the chunk the row that asked was drawn from. The row
    // therefore records the intent and this turns it into the edit - see the field's own comment.
    const PendingComponentChange change = m_pendingComponentChange;
    const Scene::Entity target = m_pendingComponentChangeEntity;
    m_pendingComponentChange = PendingComponentChange::None;
    m_pendingComponentChangeEntity = Scene::kInvalidEntity;
    if (change == PendingComponentChange::None || target == Scene::kInvalidEntity) {
        return;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!IsDocumentEntity(target) || !entityManager.IsAlive(target)) {
        return;
    }
    const bool hasSky = entityManager.HasComponent<Scene::Sky3DComponent>(target);
    if (change == PendingComponentChange::AddSky && !hasSky) {
        entityManager.AddComponent<Scene::Sky3DComponent>(target, Scene::Sky3DComponent{});
    } else if (change == PendingComponentChange::RemoveSky && hasSky) {
        entityManager.RemoveComponent<Scene::Sky3DComponent>(target);
    }
}

namespace {

/// "file:uvaudio,wav" -> {".uvaudio", ".wav"}.
[[nodiscard]] std::vector<std::string> FileDrawerExtensions(std::string_view drawerId) {
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
[[nodiscard]] std::filesystem::path ClampPickerStartDirectory(const Asset::ProjectFileSnapshot& project,
                                                                 const std::filesystem::path& wanted) {
    if (wanted.empty()) {
        return std::filesystem::path{};
    }
    for (const Asset::ProjectFileEntry& entry : project.entries) {
        if (entry.kind == Asset::ProjectFileEntryKind::Directory && entry.relativePath == wanted) {
            return wanted;
        }
    }
    return std::filesystem::path{};
}

} // namespace

Asset::AssetGuid Editor::PublishTextureSource(const std::filesystem::path& absolutePath) {
    Asset::IAssetDatabase& database = m_services->GetAssetDatabase();
    std::error_code error;
    if (!std::filesystem::is_regular_file(absolutePath, error)) {
        return Asset::kInvalidAssetGuid;
    }
    std::string extension = absolutePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (extension == ".uvtex") {
        return database.Register(absolutePath);
    }
    // The published form of this source, beside it - where the model importers publish a model's
    // textures too, and where the picker already looks for it when deciding which row to offer.
    const std::filesystem::path publishedPath =
        absolutePath.parent_path() / (absolutePath.stem().string() + ".uvtex");
    if (std::filesystem::is_regular_file(publishedPath, error)) {
        return database.Register(publishedPath);
    }
    const Asset::AssetGuid imported = m_services->GetAssetImporter().Import(
        absolutePath, publishedPath, database, LoadTextureImportSettings(absolutePath));
    if (imported == Asset::kInvalidAssetGuid) {
        // No importer for this kind, or the decode refused it. Registering the source keeps the
        // slot showing the picture it was given - the runtime texture loader decodes a source
        // image itself - instead of emptying a slot the author had just filled.
        return database.Register(absolutePath);
    }
    // The published texture is a new file in the project; Content must show it now, not on the
    // next rescan, or the author sees a slot pointing at something that does not appear to exist.
    RefreshProjectFileIndex();
    return imported;
}

namespace {

[[nodiscard]] bool IsTextureSourceImage(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tga";
}

} // namespace

std::filesystem::path Editor::FindTextureSource(const std::filesystem::path& texturePath) {
    if (IsTextureSourceImage(texturePath)) {
        return texturePath;
    }
    std::error_code error;
    for (const char* extension : {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".PNG", ".JPG", ".JPEG", ".TGA", ".BMP"}) {
        const std::filesystem::path candidate = texturePath.parent_path() / (texturePath.stem().string() + extension);
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return {};
}

Asset::TextureImportSettings Editor::LoadTextureImportSettings(const std::filesystem::path& source) {
    Asset::TextureImportSettings settings;
    Asset::AssetImportSidecar sidecar;
    std::error_code error;
    const std::filesystem::path sidecarPath = Asset::GetImportSidecarPath(source);
    if (!std::filesystem::is_regular_file(sidecarPath, error) || !Asset::LoadImportSidecar(sidecarPath, sidecar)) {
        return settings;
    }
    const auto option = [&sidecar](const char* key) -> const std::string* {
        const auto found = sidecar.options.find(key);
        return found == sidecar.options.end() ? nullptr : &found->second;
    };
    const auto flag = [&option](const char* key, bool& out) {
        if (const std::string* value = option(key)) {
            out = *value == "true";
        }
    };
    const auto number = [&option](const char* key, std::uint32_t& out, const std::uint32_t hi) {
        if (const std::string* value = option(key)) {
            try {
                out = static_cast<std::uint32_t>(std::min<unsigned long>(std::stoul(*value), hi));
            } catch (...) {
            }
        }
    };
    if (const std::string* value = option("colorSpace")) {
        settings.colorSpace = *value == "linear" ? Asset::TextureAssetColorSpace::Linear : Asset::TextureAssetColorSpace::Srgb;
    }
    if (const std::string* value = option("usage")) {
        settings.usage = *value == "normal" ? Asset::TextureUsage::Normal
                         : *value == "data" ? Asset::TextureUsage::Data
                                            : Asset::TextureUsage::Color;
    }
    if (const std::string* value = option("mipFilter")) {
        settings.mipFilter = *value == "nearest" ? Asset::TextureMipmapFilter::Nearest : Asset::TextureMipmapFilter::Box;
    }
    if (const std::string* value = option("compression")) {
        settings.compressionMode = *value == "etc1s" ? Asset::TextureCompressionMode::BasisETC1S
                                   : *value == "uastc" ? Asset::TextureCompressionMode::BasisUASTC
                                                       : Asset::TextureCompressionMode::None;
    }
    flag("generateMipmaps", settings.generateMipmaps);
    flag("invertGreen", settings.invertGreenChannel);
    flag("premultiplyAlpha", settings.premultiplyAlpha);
    number("maxSize", settings.maxResolutionClamp, 16384U);
    return settings;
}

bool Editor::ReimportTexture(const std::filesystem::path& source, const Asset::TextureImportSettings& settings) {
    Asset::AssetImportSidecar sidecar;
    std::error_code error;
    const std::filesystem::path sidecarPath = Asset::GetImportSidecarPath(source);
    if (std::filesystem::is_regular_file(sidecarPath, error)) {
        static_cast<void>(Asset::LoadImportSidecar(sidecarPath, sidecar));
    }
    sidecar.options["colorSpace"] = settings.colorSpace == Asset::TextureAssetColorSpace::Linear ? "linear" : "srgb";
    sidecar.options["usage"] = settings.usage == Asset::TextureUsage::Normal ? "normal"
                               : settings.usage == Asset::TextureUsage::Data ? "data"
                                                                                : "color";
    sidecar.options["mipFilter"] = settings.mipFilter == Asset::TextureMipmapFilter::Nearest ? "nearest" : "box";
    sidecar.options["compression"] = settings.compressionMode == Asset::TextureCompressionMode::BasisETC1S ? "etc1s"
                                     : settings.compressionMode == Asset::TextureCompressionMode::BasisUASTC ? "uastc"
                                                                                                                  : "none";
    sidecar.options["generateMipmaps"] = settings.generateMipmaps ? "true" : "false";
    sidecar.options["invertGreen"] = settings.invertGreenChannel ? "true" : "false";
    sidecar.options["premultiplyAlpha"] = settings.premultiplyAlpha ? "true" : "false";
    sidecar.options["maxSize"] = std::to_string(settings.maxResolutionClamp);
    sidecar.reimportNeeded = false;
    if (!Asset::SaveImportSidecar(sidecar, sidecarPath)) {
        return false;
    }
    const std::filesystem::path publishedPath = source.parent_path() / (source.stem().string() + ".uvtex");
    Asset::IAssetDatabase& database = m_services->GetAssetDatabase();
    const Asset::AssetGuid imported = m_services->GetAssetImporter().Import(source, publishedPath, database, settings);
    if (imported == Asset::kInvalidAssetGuid) {
        return false;
    }
    // Everything that drew the old import draws the new one: the previews now, the renderer on its reload.
    ClearTextureThumbnailCache();
    RefreshProjectFileIndex();
    return true;
}

Asset::AssetGuid Editor::PublishModelSource(const std::filesystem::path& absolutePath) {
    Asset::IAssetDatabase& database = m_services->GetAssetDatabase();
    std::error_code error;
    if (!std::filesystem::is_regular_file(absolutePath, error)) {
        return Asset::kInvalidAssetGuid;
    }
    std::string extension = absolutePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (extension == ".uvmodel") {
        static_cast<void>(RegisterImportedModelArtifacts(absolutePath));
        return database.Register(absolutePath);
    }
    // Not a model at all - a picker that was opened for some other asset kind. Registering it is
    // still the right answer: this is the published form of what the author picked.
    if (extension != ".fbx" && extension != ".glb" && extension != ".gltf" && extension != ".obj") {
        return database.Register(absolutePath);
    }

    // A source outside the project must come inside it first. The converted mesh is keyed on the
    // content-relative path, so a file imported straight from the Desktop would convert to a
    // .uvmodel outside the project - a mesh that works today and is gone the moment the project
    // folder is moved or shared.
    const Asset::ProjectFileSnapshot project = m_services->GetProjectFileIndex().GetSnapshot();
    const auto contentRelative = [](const std::filesystem::path& path, const std::filesystem::path& base) {
    // Both sides canonical: a symlinked or relative content root (Android /sdcard, "assets/")
    // must not make a file inside the project look outside it.
    std::error_code canonError;
    std::filesystem::path p = std::filesystem::weakly_canonical(path, canonError);
    if (canonError) { p = path; }
    std::filesystem::path b = std::filesystem::weakly_canonical(base, canonError);
    if (canonError) { b = base; }
    return p.lexically_relative(b);
};
    std::filesystem::path relative = contentRelative(absolutePath, project.contentRoot);
    if (relative.empty() || *relative.begin() == "..") {
        const std::filesystem::path modelDirectory = project.contentRoot / "Models";
        std::filesystem::create_directories(modelDirectory, error);
        const std::filesystem::path destination =
            MakeUniqueContentPath(modelDirectory, absolutePath.stem().string(), extension);
        std::filesystem::copy_file(absolutePath, destination, error);
        if (error) {
            UVE_ERROR("Editor: could not copy model \"{}\" into Content: {}", absolutePath.string(),
                      error.message());
            return Asset::kInvalidAssetGuid;
        }
        relative = destination.lexically_relative(project.contentRoot);
    }

    const std::filesystem::path importedPath = GetImportedModelPath(relative);
    // If the imported .uvmodel is already present and up to date, reuse it directly without
    // triggering a full project file index rescan and thumbnail flush.
    if (std::filesystem::is_regular_file(importedPath, error)) {
        std::error_code timeError;
        const auto sourceTime = std::filesystem::last_write_time(project.contentRoot / relative, timeError);
        const auto importedTime = timeError ? std::filesystem::file_time_type::min()
                                            : std::filesystem::last_write_time(importedPath, timeError);
        if (!timeError && importedTime >= sourceTime) {
            return RegisterImportedModelArtifacts(importedPath);
        }
    }

    std::filesystem::create_directories(importedPath.parent_path(), error);
    const Asset::AssetGuid imported = m_services->GetAssetImporter().Import(
        project.contentRoot / relative, importedPath, database, Asset::AssetImportSettings{});
    if (imported == Asset::kInvalidAssetGuid) {
        // No GUID rather than the source's. A slot naming a file the mesh loader cannot read looks
        // filled and draws nothing forever; an unchanged slot at least tells the truth.
        return Asset::kInvalidAssetGuid;
    }
    static_cast<void>(RegisterImportedModelArtifacts(importedPath));
    // The source is new to Content, and the .uvmodel is new to the project; both must appear now,
    // not on the next rescan, or the author sees a slot pointing at something that is not listed.
    RefreshProjectFileIndex();
    return imported;
}

namespace {

/// How far the derived slope is pushed. At 2.0 an edge from black to white across two pixels tilts
/// about 45 degrees: strong enough that a photo reads as relief under a moving light, weak enough
/// that its cast shadows stay shadows instead of becoming cliffs.
constexpr float kDerivedNormalStrength = 2.0F;

} // namespace

std::optional<Asset::AssetGuid> Editor::DeriveNormalMapForAlbedo(const Asset::AssetGuid albedo) {
    if (albedo == Asset::kInvalidAssetGuid) {
        return std::nullopt;
    }
    Asset::IAssetDatabase& database = m_services->GetAssetDatabase();
    const std::filesystem::path albedoPath = database.Resolve(albedo);
    if (albedoPath.empty()) {
        return std::nullopt;
    }
    Asset::TextureAsset albedoTexture;
    if (!Asset::LoadTextureAssetSource(albedoPath, albedoTexture)) {
        return std::nullopt;
    }
    // Beside the picture and named after it, so the map is an asset the author can see, move and
    // edit like any other, and so picking the same picture again reuses the map it produced.
    const std::filesystem::path normalPath =
        albedoPath.parent_path() / (albedoPath.stem().string() + "_normal.uvtex");
    std::error_code error;
    if (!std::filesystem::is_regular_file(normalPath, error)) {
        Asset::TextureAsset normal;
        if (!Asset::DeriveNormalTexture(albedoTexture, kDerivedNormalStrength, normal) ||
            !Asset::SaveTextureAsset(normal, normalPath)) {
            return std::nullopt;
        }
        RefreshProjectFileIndex();
    }
    const Asset::AssetGuid derived = database.Register(normalPath);
    if (derived == Asset::kInvalidAssetGuid) {
        return std::nullopt;
    }
    return derived;
}

bool Editor::AutoPopulateSkinFromModel(const Scene::Entity entity, Scene::SkinComponent& skin) {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();

    // 1. Try to find Skeleton3D on entity, its children, siblings, or parent
    const Scene::Skeleton3DComponent* skelComp = nullptr;
    if (entityManager.HasComponent<Scene::Skeleton3DComponent>(entity)) {
        skelComp = &entityManager.GetComponent<Scene::Skeleton3DComponent>(entity);
    } else {
        for (const Scene::Entity child : m_services->GetSceneGraph().GetChildren(entityManager, entity)) {
            if (entityManager.HasComponent<Scene::Skeleton3DComponent>(child)) {
                skelComp = &entityManager.GetComponent<Scene::Skeleton3DComponent>(child);
                break;
            }
        }
        if (skelComp == nullptr && entityManager.HasComponent<Scene::HierarchyComponent>(entity)) {
            const Scene::Entity parent = entityManager.GetComponent<Scene::HierarchyComponent>(entity).parent;
            if (parent != Scene::kInvalidEntity) {
                if (entityManager.HasComponent<Scene::Skeleton3DComponent>(parent)) {
                    skelComp = &entityManager.GetComponent<Scene::Skeleton3DComponent>(parent);
                } else {
                    for (const Scene::Entity sibling :
                         m_services->GetSceneGraph().GetChildren(entityManager, parent)) {
                        if (entityManager.HasComponent<Scene::Skeleton3DComponent>(sibling)) {
                            skelComp = &entityManager.GetComponent<Scene::Skeleton3DComponent>(sibling);
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
        const std::optional<Asset::MeshAsset> meshAsset = LoadEntityMeshAsset(entity);
        if (meshAsset.has_value() &&
            Scene::TryBuildSkinFromMeshJoints(*meshAsset, *skelComp, skin)) {
            return true;
        }
        // Nothing to match against - a skeleton with no skinned mesh of its own - so the binds are
        // the bones themselves, in order, with an identity inverse bind pose.
        skin.boneBinds.clear();
        skin.boneBinds.reserve(skelComp->bones.size());
        for (std::size_t i = 0; i < skelComp->bones.size(); ++i) {
            Scene::SkinBoneBind bind;
            bind.boneName = skelComp->bones[i].name;
            bind.boneIndex = static_cast<std::int32_t>(i);
            bind.inverseBindPose = Math::Matrix4x4::Identity();
            skin.boneBinds.push_back(std::move(bind));
        }
        skin.bindCount = static_cast<std::uint32_t>(skin.boneBinds.size());
        return true;
    }

    // 2. Read skeleton from assigned model in MeshComponent (using cached LoadEntityMeshAsset)
    if (const std::optional<Asset::MeshAsset> meshAsset = LoadEntityMeshAsset(entity);
        meshAsset.has_value() && !meshAsset->joints.empty()) {
        skin.boneBinds.clear();
        skin.boneBinds.reserve(meshAsset->joints.size());
        for (std::size_t i = 0; i < meshAsset->joints.size(); ++i) {
            Scene::SkinBoneBind bind;
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

std::optional<Asset::MeshAsset> Editor::LoadEntityMeshAsset(const Scene::Entity entity) {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::MeshComponent>(entity)) {
        return std::nullopt;
    }
    const Scene::MeshComponent meshComponent = entityManager.GetComponent<Scene::MeshComponent>(entity);
    if (meshComponent.meshGuid == Asset::kInvalidAssetGuid) {
        return std::nullopt;
    }
    std::filesystem::path resolved = m_services->GetAssetDatabase().Resolve(meshComponent.meshGuid);
    std::error_code ec;
    if (resolved.extension() != ".uvmodel" || !std::filesystem::is_regular_file(resolved, ec)) {
        // A model source (.glb/.fbx/...) draws through the .uvmodel the importer wrote; resolve
        // that the same way the skin and material paths do.
        const Asset::ProjectFileSnapshot project = m_services->GetProjectFileIndex().GetSnapshot();
        const std::filesystem::path relative = resolved.lexically_relative(project.contentRoot);
        if (!relative.empty() && *relative.begin() != "..") {
            const std::filesystem::path imported = GetImportedModelPath(relative);
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
    Asset::MeshAsset meshAsset;
    if (!Asset::LoadMeshAsset(resolved, meshAsset)) {
        return std::nullopt;
    }
    if (!ec) {
        m_inspectorMeshAssetCache = EntityEditorMeshCacheEntry{resolved, writeTime, meshAsset};
    }
    return meshAsset;
}

bool Editor::IsPropertyAtDefault(const TypeMetadataEntry& entry, const TypeMetadataProperty& property,
                                       const void* const instance) {
    if (property.areEqual == nullptr || !entry.HasFactory()) {
        return false;
    }
    auto defaults = m_inspectorDefaultInstances.find(&entry);
    if (defaults == m_inspectorDefaultInstances.end()) {
        defaults = m_inspectorDefaultInstances.emplace(&entry, Core::TypeInstance::MakeDefault(entry)).first;
    }
    return defaults->second.IsValid() && property.areEqual(defaults->second.Get(), instance);
}

bool Editor::SetSelectedComponentValue(const TypeMetadataEntry& entry, const void* const newInstance) {
    // A live edit still in flight is finished first, before this one changes anything, so the two
    // land in history in the order they happened.
    static_cast<void>(CommitComponentPropertyPreview());
    if (!IsAuthoringCommandAllowed() || !HasSingleDocumentSelection() || !entry.HasFactory() ||
        newInstance == nullptr || (entry.isInstanceValid != nullptr && !entry.isInstanceValid(newInstance))) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent(m_selectedEntity, entry.typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointer(m_selectedEntity, entry.typeIndex);
    Core::TypeInstance before = Core::TypeInstance::Clone(entry, instance);
    Core::TypeInstance after = Core::TypeInstance::Clone(entry, newInstance);
    if (!before.IsValid() || !after.IsValid()) {
        return false;
    }
    entry.assignInstance(instance, newInstance);
    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    m_sceneDirty = true;
    RecordHistory(ComponentPropertyHistoryEntry{m_selectedEntity, &entry, std::move(before), std::move(after),
                                                      selectionBefore, CaptureSelectionSnapshot(), dirtyBefore,
                                                      true});
    return true;
}

bool Editor::CopySelectedComponent(const TypeMetadataEntry& entry) {
    if (!HasSingleDocumentSelection() || !entry.HasFactory()) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent(m_selectedEntity, entry.typeIndex)) {
        return false;
    }
    Core::TypeInstance copy =
        Core::TypeInstance::Clone(entry, entityManager.GetComponentPointer(m_selectedEntity, entry.typeIndex));
    if (!copy.IsValid()) {
        return false;
    }
    m_componentClipboard = ComponentClipboard{&entry, std::move(copy)};
    return true;
}

bool Editor::CanPasteSelectedComponent(const TypeMetadataEntry& entry) const noexcept {
    // Types are compared by identity, not by name: two entries can never share a type index.
    return m_componentClipboard.has_value() && m_componentClipboard->entry != nullptr &&
           m_componentClipboard->entry->typeIndex == entry.typeIndex && m_componentClipboard->value.IsValid();
}

bool Editor::PasteSelectedComponent(const TypeMetadataEntry& entry) {
    if (!CanPasteSelectedComponent(entry)) {
        return false;
    }
    return SetSelectedComponentValue(entry, m_componentClipboard->value.Get());
}

bool Editor::ResetSelectedComponent(const TypeMetadataEntry& entry) {
    const Core::TypeInstance defaults = Core::TypeInstance::MakeDefault(entry);
    return defaults.IsValid() && SetSelectedComponentValue(entry, defaults.Get());
}

bool Editor::CopySelectedTransform() {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!HasSingleDocumentSelection() ||
        !entityManager.HasComponent<Scene::TransformComponent>(m_selectedEntity)) {
        return false;
    }
    m_transformClipboard = entityManager.GetComponent<Scene::TransformComponent>(m_selectedEntity);
    return true;
}

namespace {

// The local pose of `pose` written over `target`, leaving everything that is not pose alone.
Scene::TransformComponent WithLocalPose(Scene::TransformComponent target,
                                              const Scene::TransformComponent& pose) {
    target.localPosition = pose.localPosition;
    target.localRotation = pose.localRotation;
    target.localScale = pose.localScale;
    target.localEulerRadians = pose.localEulerRadians;
    target.eulerOrder = pose.eulerOrder;
    target.rotationEditMode = pose.rotationEditMode;
    return target;
}

} // namespace

bool Editor::PasteSelectedTransform() {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!m_transformClipboard.has_value() || !HasSingleDocumentSelection() ||
        !entityManager.HasComponent<Scene::TransformComponent>(m_selectedEntity)) {
        return false;
    }
    return SetSelectedLocalTransform(WithLocalPose(
        entityManager.GetComponent<Scene::TransformComponent>(m_selectedEntity), *m_transformClipboard));
}

bool Editor::ResetSelectedTransform() {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!HasSingleDocumentSelection() ||
        !entityManager.HasComponent<Scene::TransformComponent>(m_selectedEntity)) {
        return false;
    }
    return SetSelectedLocalTransform(WithLocalPose(
        entityManager.GetComponent<Scene::TransformComponent>(m_selectedEntity), Scene::TransformComponent{}));
}

bool Editor::SetEntityVisible(const Scene::Entity entity, const bool visible) {
    if (!IsAuthoringCommandAllowed() || !IsDocumentEntity(entity)) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent<Scene::VisibilityComponent>(entity)) {
        return false;
    }
    const Core::TypeMetadataEntry* const entry = Scene::GetSceneComponentMetadataRegistry().FindTypeByIndex(
        std::type_index(typeid(Scene::VisibilityComponent)));
    if (entry == nullptr || !entry->HasFactory()) {
        return false;
    }
    auto& visibility = entityManager.GetComponent<Scene::VisibilityComponent>(entity);
    if (visibility.visible == visible) {
        return false;
    }
    // Same history entry as an Inspector edit, so undo restores the whole component on this
    // entity and leaves the selection exactly as it was.
    Core::TypeInstance before = Core::TypeInstance::Clone(*entry, &visibility);
    if (!before.IsValid()) {
        return false;
    }
    visibility.visible = visible;
    Core::TypeInstance after = Core::TypeInstance::Clone(*entry, &visibility);
    if (!after.IsValid()) {
        visibility.visible = !visible;
        return false;
    }
    const EditorSelectionSnapshot selection = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    m_sceneDirty = true;
    RecordHistory(ComponentPropertyHistoryEntry{entity, entry, std::move(before), std::move(after), selection,
                                                      selection, dirtyBefore, true});
    return true;
}

bool Editor::SetSelectedComponentProperty(const TypeMetadataEntry& entry,
                                                const TypeMetadataProperty& property,
                                                const void* const newValue) {
    // See SetSelectedComponentValue: a live edit in flight is recorded before this one.
    static_cast<void>(CommitComponentPropertyPreview());
    if (!IsAuthoringCommandAllowed() || !HasSingleDocumentSelection() ||
        !property.IsAuthoringWritable() || !entry.HasFactory() || newValue == nullptr) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent(m_selectedEntity, entry.typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointer(m_selectedEntity, entry.typeIndex);

    // The prior value is cloned before the write, so an undo restores the whole component rather
    // than replaying a reverse edit - the same guarantee the per-type commands give.
    Core::TypeInstance before = Core::TypeInstance::Clone(entry, instance);
    if (!before.IsValid()) {
        return false;
    }
    property.setValue(instance, newValue);
    // A collider that has just been switched to Capsule inherits a Box's numbers, and the fields'
    // own defaults (height 1.0, radius 0.5) make the two caps meet exactly: no barrel, so the
    // author picks Capsule and is shown a sphere. The fit runs here rather than on every height
    // drag, so it only repairs a shape that cannot express itself and never overrules a height the
    // author chose on purpose afterwards. It happens before the validity check and before the
    // "after" clone, so the repaired value is what gets recorded and what undo puts back.
    if (entry.typeIndex == std::type_index(typeid(Scene::ColliderComponent))) {
        auto* const collider = static_cast<Scene::ColliderComponent*>(instance);
        *collider = Scene::FitColliderShape(*collider);
    }
    if (entry.isInstanceValid != nullptr && !entry.isInstanceValid(instance)) {
        // The component's own rule refuses this combination; put the prior value back.
        entry.assignInstance(instance, before.Get());
        return false;
    }
    // An edit that changed nothing records no history, matching what the per-type commands do.
    // A property with no equality operator is treated as changed, which over-records rather than
    // silently dropping a real edit.
    if (property.areEqual != nullptr && property.areEqual(before.Get(), instance)) {
        return false;
    }

    Core::TypeInstance after = Core::TypeInstance::Clone(entry, instance);
    if (!after.IsValid()) {
        // The write already happened; putting the prior value back leaves the scene consistent
        // rather than mutated with no way to undo it.
        entry.assignInstance(instance, before.Get());
        return false;
    }

    const EditorSelectionSnapshot selectionBefore = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    m_sceneDirty = true;
    RecordHistory(ComponentPropertyHistoryEntry{m_selectedEntity, &entry, std::move(before),
                                                      std::move(after), selectionBefore,
                                                      CaptureSelectionSnapshot(), dirtyBefore, true});
    if (entry.typeIndex == std::type_index(typeid(Scene::CameraComponent))) {
        const auto* const camera = static_cast<const Scene::CameraComponent*>(instance);
        if (camera != nullptr && camera->current) {
            Scene::MakeCameraCurrent(entityManager, m_selectedEntity);
        }
    }
    return true;
}

bool Editor::PreviewSelectedComponentProperty(const TypeMetadataEntry& entry,
                                                    const TypeMetadataProperty& property,
                                                    const void* const newValue) {
    if (!IsAuthoringCommandAllowed() || !HasSingleDocumentSelection() ||
        !property.IsAuthoringWritable() || !entry.HasFactory() || newValue == nullptr) {
        return false;
    }
    if (m_componentPropertyPreview.has_value() &&
        (m_componentPropertyPreview->entity != m_selectedEntity || m_componentPropertyPreview->entry != &entry ||
         m_componentPropertyPreview->property != &property)) {
        static_cast<void>(CommitComponentPropertyPreview());
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent(m_selectedEntity, entry.typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointer(m_selectedEntity, entry.typeIndex);
    Core::TypeInstance previous = Core::TypeInstance::Clone(entry, instance);
    if (!previous.IsValid()) {
        return false;
    }
    if (!m_componentPropertyPreview.has_value()) {
        Core::TypeInstance before = Core::TypeInstance::Clone(entry, instance);
        if (!before.IsValid()) {
            return false;
        }
        m_componentPropertyPreview = ComponentPropertyPreview{m_selectedEntity, &entry, &property, std::move(before),
                                                                 CaptureSelectionSnapshot(), m_sceneDirty};
    }
    property.setValue(instance, newValue);
    if (entry.isInstanceValid != nullptr && !entry.isInstanceValid(instance)) {
        // Refused by the component's own rule: keep the last accepted value on screen.
        entry.assignInstance(instance, previous.Get());
        return false;
    }
    m_sceneDirty = true;
    return true;
}

bool Editor::CommitComponentPropertyPreview() {
    if (!m_componentPropertyPreview.has_value()) {
        return false;
    }
    ComponentPropertyPreview preview = std::move(*m_componentPropertyPreview);
    m_componentPropertyPreview.reset();
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!IsDocumentEntity(preview.entity) || !entityManager.HasComponent(preview.entity, preview.entry->typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointer(preview.entity, preview.entry->typeIndex);
    if (preview.property->areEqual != nullptr && preview.property->areEqual(preview.before.Get(), instance)) {
        // Opened and closed without a net change: nothing to undo, and nothing left unsaved.
        m_sceneDirty = preview.dirtyBefore;
        return true;
    }
    Core::TypeInstance after = Core::TypeInstance::Clone(*preview.entry, instance);
    if (!after.IsValid()) {
        preview.entry->assignInstance(instance, preview.before.Get());
        m_sceneDirty = preview.dirtyBefore;
        return false;
    }
    m_sceneDirty = true;
    RecordHistory(ComponentPropertyHistoryEntry{preview.entity, preview.entry, std::move(preview.before),
                                                      std::move(after), preview.selectionBefore,
                                                      CaptureSelectionSnapshot(), preview.dirtyBefore, true});
    return true;
}

bool Editor::CancelComponentPropertyPreview() {
    if (!m_componentPropertyPreview.has_value()) {
        return false;
    }
    ComponentPropertyPreview preview = std::move(*m_componentPropertyPreview);
    m_componentPropertyPreview.reset();
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!IsDocumentEntity(preview.entity) || !entityManager.HasComponent(preview.entity, preview.entry->typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointer(preview.entity, preview.entry->typeIndex);
    preview.entry->assignInstance(instance, preview.before.Get());
    m_sceneDirty = preview.dirtyBefore;
    return true;
}

bool Editor::CommitComponentPropertyPreviewFor(const TypeMetadataEntry& entry,
                                                    const TypeMetadataProperty& property) {
    if (!m_componentPropertyPreview.has_value() || m_componentPropertyPreview->entry != &entry ||
        m_componentPropertyPreview->property != &property) {
        return false;
    }
    return CommitComponentPropertyPreview();
}

bool Editor::ApplyComponentPropertySnapshot(const Scene::Entity entity,
                                                  const TypeMetadataEntry* const metadata,
                                                  const void* const snapshot) {
    if (metadata == nullptr || snapshot == nullptr || !metadata->HasFactory() ||
        !IsDocumentEntity(entity)) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.HasComponent(entity, metadata->typeIndex)) {
        return false;
    }
    void* const instance = entityManager.GetComponentPointer(entity, metadata->typeIndex);
    metadata->assignInstance(instance, snapshot);
    return true;
}

std::vector<Scene::Entity> Editor::GetEntityReferenceCandidates() {
    // Outliner order, by the same walk the Outliner itself uses: roots reversed onto a stack, then
    // each object's children pushed in reverse so the next one popped is the next one drawn.
    std::vector<Scene::Entity> candidates;
    const Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    std::vector<Scene::Entity> pending = GetDocumentRoots();
    std::reverse(pending.begin(), pending.end());
    while (!pending.empty()) {
        const Scene::Entity candidate = pending.back();
        pending.pop_back();
        std::vector<Scene::Entity> children =
            m_services->GetSceneGraph().GetChildren(m_services->GetEntityManager(), candidate);
        pending.insert(pending.end(), children.rbegin(), children.rend());
        if (candidate == m_selectedEntity ||
            !entityManager.HasComponent<Scene::TransformComponent>(candidate)) {
            continue;
        }
        candidates.push_back(candidate);
    }
    return candidates;
}

bool Editor::ResetSelectedComponentProperty(const TypeMetadataEntry& entry,
                                                  const TypeMetadataProperty& property) {
    if (!entry.HasFactory() || property.getValue == nullptr) {
        return false;
    }
    const Core::TypeInstance defaults = Core::TypeInstance::MakeDefault(entry);
    if (!defaults.IsValid()) {
        return false;
    }
    // Read the default through the same accessor the live value uses, so the buffer is exactly the
    // right size and type for the write that follows - without this function naming either.
    std::array<std::byte, 64> buffer{};
    if (property.typeId == Scene::kPropertyTypeString) {
        std::string value;
        property.getValue(defaults.Get(), &value);
        return SetSelectedComponentProperty(entry, property, &value);
    }
    if (property.typeId == Scene::kPropertyTypeAssetGuid) {
        Asset::AssetGuid value{};
        property.getValue(defaults.Get(), &value);
        return SetSelectedComponentProperty(entry, property, &value);
    }
    if (property.typeId == Scene::kPropertyTypeEntity) {
        Scene::Entity value = Scene::kInvalidEntity;
        property.getValue(defaults.Get(), &value);
        return SetSelectedComponentProperty(entry, property, &value);
    }
    // The byte buffer is only sound for trivially copyable values, so the types allowed through it
    // are named rather than assumed. A list or other owning value (the metadata entries) is never
    // reset through here; its custom drawer owns that.
    const bool trivial = !property.enumEntries.empty() || property.typeId == Scene::kPropertyTypeBool ||
                         property.typeId == Scene::kPropertyTypeFloat ||
                         property.typeId == Scene::kPropertyTypeInt32 ||
                         property.typeId == Scene::kPropertyTypeUInt32 ||
                         property.typeId == Scene::kPropertyTypeUInt8 ||
                         property.typeId == Scene::kPropertyTypeBitMask32 ||
                         property.typeId == Scene::kPropertyTypeVector2 ||
                         property.typeId == Scene::kPropertyTypeVector3 ||
                         property.typeId == Scene::kPropertyTypeRect ||
                         property.typeId == Scene::kPropertyTypeColor ||
                         property.typeId == Scene::kPropertyTypeLinearColor ||
                         property.typeId == Scene::kPropertyTypeQuaternion;
    if (!trivial) {
        return false;
    }
    property.getValue(defaults.Get(), buffer.data());
    return SetSelectedComponentProperty(entry, property, buffer.data());
}

} // namespace UVE::Editor
