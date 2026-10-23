// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/fbx_importer_uve.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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
    // The converter reports one range per material its faces use; which published slot each range
    // belongs to is known here, because only this importer knows which materials were publishable.
    std::vector<FbxSurfaceRangeUVE> surfaceRanges;
    if (!ConvertFbxMeshUVE(sourceBytes, mesh, &surfaceRanges)) {
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
    // File material position -> published slot. A material ReadFbxMaterialsUVE could not validate
    // is left out of the published list, so the range's file position cannot be used as a slot
    // directly; this is the one place both orders are known.
    std::unordered_map<std::uint32_t, std::uint32_t> slotOfSourceIndex;
    slotOfSourceIndex.reserve(materials->size());
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
        const std::string stem = Detail::MakeMaterialFileStemUVE(modelStem, source.name, materialIndex, "material", usedStems);
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
        slotOfSourceIndex.emplace(source.sourceIndex,
                                  static_cast<std::uint32_t>(materialGuids.size()));
        materialGuids.push_back(guid);
    }
    mesh.materialGuids = std::move(materialGuids);

    if (!surfaceRanges.empty() && !mesh.materialGuids.empty()) {
        mesh.surfaces.reserve(surfaceRanges.size());
        for (const FbxSurfaceRangeUVE& range : surfaceRanges) {
            MeshSurfaceUVE surface{range.firstIndex, range.indexCount, kInvalidMeshSurfaceMaterialSlotUVE};
            if (const auto found = slotOfSourceIndex.find(range.materialSourceIndex);
                found != slotOfSourceIndex.end()) {
                surface.materialSlot = found->second;
            }
            mesh.surfaces.push_back(surface);
        }
        if (!IsMeshSurfaceDataValidUVE(mesh)) {
            // The ranges come from the same index buffer the mesh holds, so this cannot happen
            // unless one of the two changed; refusing them keeps a broken table out of the .uvmodel.
            UVE_WARNING("FbxImporterUVE: the converted surface ranges do not describe the index buffer; "
                        "\"{}\" publishes without them",
                        destinationPath.string());
            mesh.surfaces.clear();
        }
    }

    return Detail::PublishAssetAtomicallyUVE(
        destinationPath, kFbxImporterNameUVE, kFbxTemporarySuffixUVE,
        [&mesh](const std::filesystem::path& temporaryPath) { return SaveMeshAssetUVE(mesh, temporaryPath); });
}

} // namespace

void RegisterFbxImporterUVE(IAssetImporterUVE& importer) {
    importer.RegisterDatabaseAwareImporterUVE("fbx", &ImportFbxSourceUVE);
}

} // namespace UVE::Asset
