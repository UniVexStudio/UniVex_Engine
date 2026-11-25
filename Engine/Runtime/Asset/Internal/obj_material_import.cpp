// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "obj_material_import.h"

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

#include "import_helpers.h"
#include "model_texture_import.h"

#include "material_asset.h"
#include "mtl_material_converter.h"
#include "logging_macros.h"

namespace UVE::Asset::Detail {
namespace {

constexpr const char* kObjImporterName = "ObjImporter";
constexpr std::string_view kObjTemporarySuffix = ".uve_obj_tmp";

/// Most `usemtl` switches the importer will record per file. A mesh that names more materials than
/// this is not a shape anyone can look at in one viewport, and the bound keeps the scan linear.
constexpr std::size_t kMaximumObjUsedMaterials = 256U;

[[nodiscard]] std::string_view TrimmedLine(std::string_view line) noexcept {
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
[[nodiscard]] std::pair<std::string_view, std::string_view> SplitDirective(std::string_view line) noexcept {
    const std::size_t separator = line.find_first_of(" \t");
    if (separator == std::string_view::npos) {
        return {line, std::string_view{}};
    }
    const std::string_view directive = line.substr(0U, separator);
    return {directive, TrimmedLine(line.substr(separator))};
}

/// A texture reference that stays inside the source directory. Model files are untrusted, so an
/// absolute path or one that climbs out is refused before any read.
[[nodiscard]] bool IsTraversalSafe(const std::filesystem::path& relative) {
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

[[nodiscard]] std::string NormalisedRelative(const std::string_view authored) {
    std::string normalised(authored);
    std::replace(normalised.begin(), normalised.end(), '\\', '/');
    return normalised;
}

/// Applies one authored texture file to a material slot through the shared model-texture helper.
void ImportTextureSlot(const std::filesystem::path& sourceDirectory, const std::filesystem::path& destinationPath,
                          const std::string_view authoredFile, const TextureAssetColorSpace colorSpace,
                          const TextureUsage usage, const ModelTextureImportOptions& options,
                          IAssetDatabase& assetDatabase, AssetGuid& slot) {
    if (authoredFile.empty()) {
        return;
    }
    slot = ImportModelTextureFile(sourceDirectory, destinationPath, authoredFile, colorSpace, usage, options,
                                     assetDatabase);
}

} // namespace

namespace {

/// Turns the converter's `usemtl` runs into surface ranges, each pointing at the slot its material
/// was published in. Names are matched exactly as both readers read them, so a name that survived
/// geometry conversion but has no published material - it was absent from the library, or failed
/// validation - leaves that surface's slot invalid on purpose.
void ApplyObjSurfaceRanges(const std::vector<std::string>& publishedNames,
                              const std::vector<ObjSurfaceRange>& surfaceRanges,
                              MeshAsset& mesh) {
    if (surfaceRanges.size() < 2U || mesh.materialGuids.empty()) {
        return;
    }
    std::unordered_map<std::string, std::uint32_t> slotByName;
    slotByName.reserve(publishedNames.size());
    for (std::size_t slot = 0U; slot < publishedNames.size(); ++slot) {
        slotByName.emplace(publishedNames[slot], static_cast<std::uint32_t>(slot));
    }
    std::vector<MeshSurface> surfaces;
    surfaces.reserve(surfaceRanges.size());
    for (const ObjSurfaceRange& range : surfaceRanges) {
        MeshSurface surface{range.firstIndex, range.indexCount, kInvalidMeshSurfaceMaterialSlot};
        if (const auto found = slotByName.find(range.materialName); found != slotByName.end()) {
            surface.materialSlot = found->second;
        }
        surfaces.push_back(surface);
    }
    mesh.surfaces = std::move(surfaces);
    if (!IsMeshSurfaceDataValid(mesh)) {
        // The ranges come from the same index buffer the mesh holds, so this cannot happen unless
        // one of the two readers changed; refusing them keeps a broken table out of the .uvmodel.
        UVE_WARNING("{}: the {} converted surface ranges do not describe the index buffer; the mesh "
                    "publishes without them",
                    kObjImporterName, mesh.surfaces.size());
        mesh.surfaces.clear();
    }
}

} // namespace

bool ImportObjMaterials(const std::string_view objSource, const std::filesystem::path& sourcePath,
                           const std::filesystem::path& destinationPath, IAssetDatabase& assetDatabase,
                           MeshAsset& inOutMesh,
                           const std::vector<ObjSurfaceRange>& surfaceRanges) {
    inOutMesh.materialGuids.clear();
    inOutMesh.surfaces.clear();
    try {
        // One pass over the OBJ for the two directives that matter: the library it names and the
        // materials it switches to, in the order the author wrote them.
        std::string libraryFile;
        std::size_t libraryCount = 0U;
        std::vector<std::string> usedNames;
        std::size_t lineStart = 0U;
        while (lineStart <= objSource.size()) {
            const std::size_t lineEnd = objSource.find('\n', lineStart);
            const std::string_view line =
                TrimmedLine(objSource.substr(lineStart, lineEnd == std::string_view::npos
                                                               ? std::string_view::npos
                                                               : lineEnd - lineStart));
            if (!line.empty()) {
                const auto [directive, argument] = SplitDirective(line);
                if (directive == "mtllib" && !argument.empty()) {
                    ++libraryCount;
                    if (libraryFile.empty()) {
                        libraryFile = std::string(argument);
                    }
                } else if (directive == "usemtl" && !argument.empty() &&
                           usedNames.size() < kMaximumObjUsedMaterials) {
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
        if (libraryCount > 1U) {
            // One mesh wears the materials of the first library it names. Reading the others too
            // would need the face-by-face material ranges this increment does not have, and
            // silently taking the last one would make the choice depend on line order in a file
            // the author never sees.
            UVE_WARNING("{}: source \"{}\" names {} material libraries; only \"{}\" is read",
                        kObjImporterName, sourcePath.string(), libraryCount, libraryFile);
        }

        const std::filesystem::path relativeLibrary = std::filesystem::path(NormalisedRelative(libraryFile));
        if (!IsTraversalSafe(relativeLibrary)) {
            UVE_WARNING("{}: source \"{}\" names material library \"{}\" outside its own directory; the mesh "
                        "publishes without materials",
                        kObjImporterName, sourcePath.string(), libraryFile);
            return true;
        }
        const std::filesystem::path libraryPath = sourcePath.parent_path() / relativeLibrary;
        std::vector<std::byte> libraryBytes;
        if (!ReadBoundedSourceBytes(libraryPath, kObjImporterName, kMaximumMtlLibrarySourceBytes,
                                       libraryBytes)) {
            UVE_WARNING("{}: source \"{}\" names material library \"{}\" but it could not be read; the mesh "
                        "publishes without materials",
                        kObjImporterName, sourcePath.string(), libraryPath.string());
            return true;
        }

        MtlLibrary library;
        const std::string_view librarySource(reinterpret_cast<const char*>(libraryBytes.data()),
                                             libraryBytes.size());
        if (!ConvertMtlLibrary(librarySource, library)) {
            UVE_WARNING("{}: material library \"{}\" failed bounded MTL conversion; the mesh publishes without "
                        "materials",
                        kObjImporterName, libraryPath.string());
            return true;
        }
        if (library.skippedMaterials > 0U) {
            UVE_WARNING("{}: material library \"{}\" declared {} material(s) whose directives did not convert; "
                        "they are left out",
                        kObjImporterName, libraryPath.string(), library.skippedMaterials);
        }
        if (library.truncated) {
            UVE_WARNING("{}: material library \"{}\" declares more than {} materials; the ones past the "
                        "bound are left out",
                        kObjImporterName, libraryPath.string(), kMaximumMtlLibraryMaterials);
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
                            kObjImporterName, sourcePath.string(), name, libraryPath.string());
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
        ModelTextureImportOptions options;
        options.importerName = kObjImporterName;
        options.temporarySuffix = kObjTemporarySuffix;

        std::vector<AssetGuid> materialGuids;
        materialGuids.reserve(publishOrder.size());
        // Parallel to materialGuids: what each published slot is called, so a surface range can
        // find the slot its `usemtl` name landed in.
        std::vector<std::string> publishedNames;
        publishedNames.reserve(publishOrder.size());
        for (const std::size_t index : publishOrder) {
            MtlLibraryMaterial& source = library.materials[index];
            MaterialAsset material = source.material;
            ImportTextureSlot(sourceDirectory, destinationPath, source.albedoTextureFile,
                                 TextureAssetColorSpace::Srgb, TextureUsage::Color, options, assetDatabase,
                                 material.albedoTexture);
            ImportTextureSlot(sourceDirectory, destinationPath, source.normalTextureFile,
                                 TextureAssetColorSpace::Linear, TextureUsage::Normal, options, assetDatabase,
                                 material.normalTexture);
            ImportTextureSlot(sourceDirectory, destinationPath, source.metallicTextureFile,
                                 TextureAssetColorSpace::Linear, TextureUsage::Data, options, assetDatabase,
                                 material.metallicTexture);
            ImportTextureSlot(sourceDirectory, destinationPath, source.emissiveTextureFile,
                                 TextureAssetColorSpace::Srgb, TextureUsage::Color, options, assetDatabase,
                                 material.emissiveTexture);
            ImportTextureSlot(sourceDirectory, destinationPath, source.occlusionTextureFile,
                                 TextureAssetColorSpace::Linear, TextureUsage::Data, options, assetDatabase,
                                 material.aoTexture);

            if (!IsMaterialAssetValid(material)) {
                UVE_WARNING("{}: material \"{}\" describes values the material validator rejects; it is left out",
                            kObjImporterName, source.name);
                continue;
            }
            const std::string stem =
                MakeMaterialFileStem(modelStem, source.name, index, "material", usedStems);
            const std::filesystem::path materialPath = destinationPath.parent_path() / (stem + ".uvmat");
            if (!PublishAssetAtomically(
                    materialPath, kObjImporterName, kObjTemporarySuffix,
                    [&material](const std::filesystem::path& temporaryPath) {
                        return SaveMaterialAsset(material, temporaryPath);
                    })) {
                UVE_ERROR("{}: failed to publish imported material \"{}\"", kObjImporterName,
                          materialPath.string());
                return false;
            }
            const AssetGuid guid = assetDatabase.Register(materialPath);
            if (guid == kInvalidAssetGuid) {
                UVE_ERROR("{}: asset database rejected imported material \"{}\"", kObjImporterName,
                          materialPath.string());
                return false;
            }
            materialGuids.push_back(guid);
            publishedNames.push_back(source.name);
        }
        inOutMesh.materialGuids = std::move(materialGuids);
        ApplyObjSurfaceRanges(publishedNames, surfaceRanges, inOutMesh);
        return true;
    } catch (const std::bad_alloc&) {
        UVE_ERROR("{}: out of memory while importing OBJ materials", kObjImporterName);
        return false;
    }
}

} // namespace UVE::Asset::Detail
