// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "obj_material_import_uve.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "import_helpers_uve.h"
#include "model_texture_import_uve.h"

#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mtl_material_converter_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset::Detail {
namespace {

constexpr const char* kObjImporterNameUVE = "ObjImporterUVE";
constexpr std::string_view kObjTemporarySuffixUVE = ".uve_obj_tmp";

/// Most `usemtl` switches the importer will record per file. A mesh that names more materials than
/// this is not a shape anyone can look at in one viewport, and the bound keeps the scan linear.
constexpr std::size_t kMaximumObjUsedMaterialsUVE = 256U;

[[nodiscard]] std::string_view TrimmedLineUVE(std::string_view line) noexcept {
    const std::size_t commentStart = line.find('#');
    if (commentStart != std::string_view::npos) {
        line = line.substr(0U, commentStart);
    }
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
        line.remove_prefix(1U);
    }
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
        line.remove_suffix(1U);
    }
    return line;
}

/// Splits `line` into its directive and the trimmed remainder. The remainder is where OBJ puts the
/// one argument every directive this importer reads takes (a library path or a material name), and
/// material names may contain spaces, so it must stay whole.
[[nodiscard]] std::pair<std::string_view, std::string_view> SplitDirectiveUVE(std::string_view line) noexcept {
    const std::size_t separator = line.find_first_of(" \t");
    if (separator == std::string_view::npos) {
        return {line, std::string_view{}};
    }
    const std::string_view directive = line.substr(0U, separator);
    return {directive, TrimmedLineUVE(line.substr(separator))};
}

/// A texture reference that stays inside the source directory. Model files are untrusted, so an
/// absolute path or one that climbs out is refused before any read.
[[nodiscard]] bool IsTraversalSafeUVE(const std::filesystem::path& relative) {
    if (relative.empty() || relative.has_root_path() || relative.is_absolute()) {
        return false;
    }
    for (const std::filesystem::path& component : relative) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string NormalisedRelativeUVE(const std::string_view authored) {
    std::string normalised(authored);
    std::replace(normalised.begin(), normalised.end(), '\\', '/');
    return normalised;
}

/// Applies one authored texture file to a material slot through the shared model-texture helper.
void ImportTextureSlotUVE(const std::filesystem::path& sourceDirectory, const std::filesystem::path& destinationPath,
                          const std::string_view authoredFile, const TextureAssetColorSpaceUVE colorSpace,
                          const TextureUsageUVE usage, const ModelTextureImportOptionsUVE& options,
                          IAssetDatabaseUVE& assetDatabase, AssetGuidUVE& slot) {
    if (authoredFile.empty()) {
        return;
    }
    slot = ImportModelTextureFileUVE(sourceDirectory, destinationPath, authoredFile, colorSpace, usage, options,
                                     assetDatabase);
}

} // namespace

bool ImportObjMaterialsUVE(const std::string_view objSource, const std::filesystem::path& sourcePath,
                           const std::filesystem::path& destinationPath, IAssetDatabaseUVE& assetDatabase,
                           MeshAssetUVE& inOutMesh) {
    inOutMesh.materialGuids.clear();
    try {
        // One pass over the OBJ for the two directives that matter: the library it names and the
        // materials it switches to, in the order the author wrote them.
        std::string libraryFile;
        std::vector<std::string> usedNames;
        std::size_t lineStart = 0U;
        while (lineStart <= objSource.size()) {
            const std::size_t lineEnd = objSource.find('\n', lineStart);
            const std::string_view line =
                TrimmedLineUVE(objSource.substr(lineStart, lineEnd == std::string_view::npos
                                                               ? std::string_view::npos
                                                               : lineEnd - lineStart));
            if (!line.empty()) {
                const auto [directive, argument] = SplitDirectiveUVE(line);
                if (directive == "mtllib" && libraryFile.empty() && !argument.empty()) {
                    libraryFile = std::string(argument);
                } else if (directive == "usemtl" && !argument.empty() &&
                           usedNames.size() < kMaximumObjUsedMaterialsUVE) {
                    const std::string name(argument);
                    if (std::find(usedNames.begin(), usedNames.end(), name) == usedNames.end()) {
                        usedNames.push_back(name);
                    }
                }
            }
            if (lineEnd == std::string_view::npos) {
                break;
            }
            lineStart = lineEnd + 1U;
        }

        if (libraryFile.empty()) {
            // No library is a valid, common OBJ: untextured geometry, nothing to publish.
            return true;
        }

        const std::filesystem::path relativeLibrary = std::filesystem::path(NormalisedRelativeUVE(libraryFile));
        if (!IsTraversalSafeUVE(relativeLibrary)) {
            UVE_WARNING("{}: source \"{}\" names material library \"{}\" outside its own directory; the mesh "
                        "publishes without materials",
                        kObjImporterNameUVE, sourcePath.string(), libraryFile);
            return true;
        }
        const std::filesystem::path libraryPath = sourcePath.parent_path() / relativeLibrary;
        std::vector<std::byte> libraryBytes;
        if (!ReadBoundedSourceBytesUVE(libraryPath, kObjImporterNameUVE, kMaximumMtlLibrarySourceBytesUVE,
                                       libraryBytes)) {
            UVE_WARNING("{}: source \"{}\" names material library \"{}\" but it could not be read; the mesh "
                        "publishes without materials",
                        kObjImporterNameUVE, sourcePath.string(), libraryPath.string());
            return true;
        }

        MtlLibraryUVE library;
        const std::string_view librarySource(reinterpret_cast<const char*>(libraryBytes.data()),
                                             libraryBytes.size());
        if (!ConvertMtlLibraryUVE(librarySource, library)) {
            UVE_WARNING("{}: material library \"{}\" failed bounded MTL conversion; the mesh publishes without "
                        "materials",
                        kObjImporterNameUVE, libraryPath.string());
            return true;
        }
        if (library.skippedMaterials > 0U) {
            UVE_WARNING("{}: material library \"{}\" declared {} material(s) whose directives did not convert; "
                        "they are left out",
                        kObjImporterNameUVE, libraryPath.string(), library.skippedMaterials);
        }
        if (library.materials.empty()) {
            return true;
        }

        // Publish the materials the mesh uses, in first-use order. When the OBJ switches no
        // material at all the library's own order is the only order there is.
        std::unordered_map<std::string, std::size_t> indexByName;
        for (std::size_t index = 0U; index < library.materials.size(); ++index) {
            indexByName.emplace(library.materials[index].name, index);
        }
        std::vector<std::size_t> publishOrder;
        publishOrder.reserve(library.materials.size());
        for (const std::string& name : usedNames) {
            const auto found = indexByName.find(name);
            if (found == indexByName.end()) {
                UVE_WARNING("{}: source \"{}\" switches to material \"{}\" which \"{}\" does not declare; that "
                            "material is left out",
                            kObjImporterNameUVE, sourcePath.string(), name, libraryPath.string());
                continue;
            }
            publishOrder.push_back(found->second);
        }
        if (publishOrder.empty()) {
            for (std::size_t index = 0U; index < library.materials.size(); ++index) {
                publishOrder.push_back(index);
            }
        }

        const std::string modelStem = destinationPath.stem().string();
        const std::filesystem::path sourceDirectory = sourcePath.parent_path();
        std::vector<std::string> usedStems;
        ModelTextureImportOptionsUVE options;
        options.importerName = kObjImporterNameUVE;
        options.temporarySuffix = kObjTemporarySuffixUVE;

        std::vector<AssetGuidUVE> materialGuids;
        materialGuids.reserve(publishOrder.size());
        for (const std::size_t index : publishOrder) {
            MtlLibraryMaterialUVE& source = library.materials[index];
            MaterialAssetUVE material = source.material;
            ImportTextureSlotUVE(sourceDirectory, destinationPath, source.albedoTextureFile,
                                 TextureAssetColorSpaceUVE::Srgb, TextureUsageUVE::Color, options, assetDatabase,
                                 material.albedoTexture);
            ImportTextureSlotUVE(sourceDirectory, destinationPath, source.normalTextureFile,
                                 TextureAssetColorSpaceUVE::Linear, TextureUsageUVE::Normal, options, assetDatabase,
                                 material.normalTexture);
            ImportTextureSlotUVE(sourceDirectory, destinationPath, source.metallicTextureFile,
                                 TextureAssetColorSpaceUVE::Linear, TextureUsageUVE::Data, options, assetDatabase,
                                 material.metallicTexture);
            ImportTextureSlotUVE(sourceDirectory, destinationPath, source.emissiveTextureFile,
                                 TextureAssetColorSpaceUVE::Srgb, TextureUsageUVE::Color, options, assetDatabase,
                                 material.emissiveTexture);
            ImportTextureSlotUVE(sourceDirectory, destinationPath, source.occlusionTextureFile,
                                 TextureAssetColorSpaceUVE::Linear, TextureUsageUVE::Data, options, assetDatabase,
                                 material.aoTexture);

            if (!IsMaterialAssetValidUVE(material)) {
                UVE_WARNING("{}: material \"{}\" describes values the material validator rejects; it is left out",
                            kObjImporterNameUVE, source.name);
                continue;
            }
            const std::string stem =
                MakeMaterialFileStemUVE(modelStem, source.name, index, "material", usedStems);
            const std::filesystem::path materialPath = destinationPath.parent_path() / (stem + ".uvmat");
            if (!PublishAssetAtomicallyUVE(
                    materialPath, kObjImporterNameUVE, kObjTemporarySuffixUVE,
                    [&material](const std::filesystem::path& temporaryPath) {
                        return SaveMaterialAssetUVE(material, temporaryPath);
                    })) {
                UVE_ERROR("{}: failed to publish imported material \"{}\"", kObjImporterNameUVE,
                          materialPath.string());
                return false;
            }
            const AssetGuidUVE guid = assetDatabase.RegisterUVE(materialPath);
            if (guid == kInvalidAssetGuidUVE) {
                UVE_ERROR("{}: asset database rejected imported material \"{}\"", kObjImporterNameUVE,
                          materialPath.string());
                return false;
            }
            materialGuids.push_back(guid);
        }
        inOutMesh.materialGuids = std::move(materialGuids);
        return true;
    } catch (const std::bad_alloc&) {
        UVE_ERROR("{}: out of memory while importing OBJ materials", kObjImporterNameUVE);
        return false;
    }
}

} // namespace UVE::Asset::Detail
