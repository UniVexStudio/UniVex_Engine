// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/fbx_importer_uve.h"

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "import_helpers_uve.h"

#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/fbx_mesh_converter_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset {
namespace {

constexpr const char* kFbxImporterNameUVE = "FbxImporterUVE";
constexpr std::string_view kFbxTemporarySuffixUVE = ".uve_fbx_tmp";

/// `<model-stem>.<sanitised-material-name>.uvmat`: the material name reduced to
/// [A-Za-z0-9_-], `material_<index>` when nothing survives, and `_<n>` appended on a clash so two
/// FBX materials can never publish over each other. Deterministic for a given file, which is what
/// makes a re-import land on the same paths (and therefore, through RegisterUVE, the same GUIDs).
[[nodiscard]] std::string MakeMaterialFileStemUVE(const std::string_view modelStem,
                                                  const std::string_view materialName,
                                                  const std::size_t materialIndex,
                                                  std::vector<std::string>& usedStems) {
    std::string sanitised;
    sanitised.reserve(materialName.size());
    for (const char character : materialName) {
        const auto value = static_cast<unsigned char>(character);
        if (std::isalnum(value) != 0 || character == '_' || character == '-') {
            sanitised.push_back(character);
        } else if (character == ' ' || character == '.' || character == ':' || character == '/' || character == '\\') {
            sanitised.push_back('_');
        }
    }
    if (sanitised.empty()) {
        sanitised = "material_" + std::to_string(materialIndex);
    }
    std::string candidate = std::string(modelStem) + "." + sanitised;
    for (std::size_t suffix = 2U;; ++suffix) {
        bool clash = false;
        for (const std::string& used : usedStems) {
            if (used == candidate) {
                clash = true;
                break;
            }
        }
        if (!clash) {
            break;
        }
        candidate = std::string(modelStem) + "." + sanitised + "_" + std::to_string(suffix);
    }
    usedStems.push_back(candidate);
    return candidate;
}

/// Resolves one authored texture file into a texture slot, logging a diagnostic (never inventing
/// a GUID) when no imported sibling texture matches.
void ResolveTextureSlotUVE(const std::filesystem::path& destinationPath, const std::string_view authoredFile,
                           const IAssetDatabaseUVE& assetDatabase, const std::string_view materialName,
                           const char* slotName, AssetGuidUVE& slot) {
    if (authoredFile.empty()) {
        return;
    }
    slot = Detail::ResolveImportedSiblingTextureGuidUVE(destinationPath, authoredFile, assetDatabase);
    if (slot == kInvalidAssetGuidUVE) {
        UVE_WARNING("FbxImporterUVE: material \"{}\" references {} texture \"{}\" but no imported sibling .uvtex "
                    "with that stem is registered next to \"{}\"; the slot stays empty",
                    materialName, slotName, authoredFile, destinationPath.string());
    }
}

[[nodiscard]] bool ImportFbxSourceUVE(const std::filesystem::path& sourcePath,
                                      const std::filesystem::path& destinationPath,
                                      IAssetDatabaseUVE& assetDatabase,
                                      const AssetImportSettingsUVE& /*settings*/) {
    if (destinationPath.extension() != ".uvmodel") {
        UVE_ERROR("FbxImporterUVE: destination \"{}\" must use the .uvmodel extension",
                  destinationPath.string());
        return false;
    }

    std::vector<std::byte> sourceBytes;
    if (!Detail::ReadBoundedSourceBytesUVE(sourcePath, kFbxImporterNameUVE, kMaximumFbxMeshSourceBytesUVE,
                                           sourceBytes)) {
        return false;
    }

    MeshAssetUVE mesh;
    if (!ConvertFbxMeshUVE(sourceBytes, mesh)) {
        UVE_ERROR("FbxImporterUVE: source \"{}\" failed FBX mesh conversion", sourcePath.string());
        return false;
    }

    // Foundation WP7: publish every material as its own .uvmat beside the model and reference
    // them from the mesh. Materials are published BEFORE the mesh so a failure leaves no
    // .uvmodel pointing at materials that do not exist.
    const std::optional<std::vector<FbxMaterialUVE>> materials = ReadFbxMaterialsUVE(sourceBytes);
    if (!materials.has_value()) {
        UVE_ERROR("FbxImporterUVE: source \"{}\" failed FBX material conversion", sourcePath.string());
        return false;
    }
    const std::string modelStem = destinationPath.stem().string();
    const std::filesystem::path directory = destinationPath.parent_path();
    std::vector<std::string> usedStems;
    std::vector<AssetGuidUVE> materialGuids;
    materialGuids.reserve(materials->size());
    for (std::size_t materialIndex = 0U; materialIndex < materials->size(); ++materialIndex) {
        const FbxMaterialUVE& source = (*materials)[materialIndex];
        MaterialAssetUVE material = source.material;
        ResolveTextureSlotUVE(destinationPath, source.albedoTextureFile, assetDatabase, source.name, "albedo",
                              material.albedoTexture);
        ResolveTextureSlotUVE(destinationPath, source.normalTextureFile, assetDatabase, source.name, "normal",
                              material.normalTexture);
        ResolveTextureSlotUVE(destinationPath, source.emissiveTextureFile, assetDatabase, source.name, "emissive",
                              material.emissiveTexture);
        ResolveTextureSlotUVE(destinationPath, source.ambientOcclusionTextureFile, assetDatabase, source.name,
                              "ambient occlusion", material.aoTexture);
        if (source.hasNonFileTexture) {
            UVE_WARNING("FbxImporterUVE: material \"{}\" in \"{}\" uses a layered/procedural/unnamed texture that "
                        "cannot be referenced by file; that slot stays empty",
                        source.name, sourcePath.string());
        }
        const std::string stem = MakeMaterialFileStemUVE(modelStem, source.name, materialIndex, usedStems);
        const std::filesystem::path materialPath = directory / (stem + ".uvmat");
        if (!Detail::PublishAssetAtomicallyUVE(materialPath, kFbxImporterNameUVE, kFbxTemporarySuffixUVE,
                                               [&material](const std::filesystem::path& temporaryPath) {
                                                   return SaveMaterialAssetUVE(material, temporaryPath);
                                               })) {
            return false;
        }
        const AssetGuidUVE guid = assetDatabase.RegisterUVE(materialPath);
        if (guid == kInvalidAssetGuidUVE) {
            UVE_ERROR("FbxImporterUVE: asset database rejected material \"{}\"", materialPath.string());
            return false;
        }
        materialGuids.push_back(guid);
    }
    mesh.materialGuids = std::move(materialGuids);

    return Detail::PublishAssetAtomicallyUVE(
        destinationPath, kFbxImporterNameUVE, kFbxTemporarySuffixUVE,
        [&mesh](const std::filesystem::path& temporaryPath) { return SaveMeshAssetUVE(mesh, temporaryPath); });
}

} // namespace

void RegisterFbxImporterUVE(IAssetImporterUVE& importer) {
    importer.RegisterDatabaseAwareImporterUVE("fbx", &ImportFbxSourceUVE);
}

} // namespace UVE::Asset
