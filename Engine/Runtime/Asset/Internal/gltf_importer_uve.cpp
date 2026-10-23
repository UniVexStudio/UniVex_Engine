// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/gltf_importer_uve.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "import_helpers_uve.h"
#include "gltf_document_uve.h"
#include "gltf_material_import_uve.h"
#include "uve/asset/asset_import_sidecar_uve.h"

#include "uve/asset/gltf_mesh_converter_uve.h"
#include "uve/asset/gltf_metadata_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset {
namespace {

constexpr const char* kGltfImporterNameUVE = "GltfImporterUVE";
constexpr std::size_t kGlbHeaderBytesUVE = 12U;
constexpr std::size_t kGlbChunkHeaderBytesUVE = 8U;
constexpr std::size_t kMaximumGltfSourceBytesUVE = kMaximumGltfDataUriDecodedBytesUVE;
constexpr std::string_view kGltfTemporarySuffixUVE = ".uve_gltf_tmp";

[[nodiscard]] std::uint32_t ReadU32LittleEndianUVE(const std::vector<std::byte>& bytes,
                                                   const std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2U])) << 16U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3U])) << 24U);
}

[[nodiscard]] bool ParseJsonDocumentUVE(const std::string_view jsonSource, nlohmann::json& outDocument) {
    try {
        const nlohmann::json document = nlohmann::json::parse(jsonSource);
        if (!document.is_object() || !document.contains("asset") || !document.at("asset").is_object() ||
            !document.at("asset").contains("version") || !document.at("asset").at("version").is_string() ||
            document.at("asset").at("version").get<std::string>() != "2.0") {
            return false;
        }
        outDocument = document;
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

[[nodiscard]] bool ExtractGlbDocumentAndBufferUVE(const std::vector<std::byte>& bytes,
                                                  nlohmann::json& outDocument,
                                                  std::vector<std::byte>& outBuffer) {
    if (bytes.size() < kGlbHeaderBytesUVE + kGlbChunkHeaderBytesUVE ||
        ReadU32LittleEndianUVE(bytes, 0U) != 0x46546C67U || ReadU32LittleEndianUVE(bytes, 4U) != 2U ||
        ReadU32LittleEndianUVE(bytes, 8U) != bytes.size()) {
        return false;
    }
    const std::size_t jsonLength = ReadU32LittleEndianUVE(bytes, 12U);
    if (ReadU32LittleEndianUVE(bytes, 16U) != 0x4E4F534AU || jsonLength > bytes.size() - 20U) {
        return false;
    }
    const auto* const jsonBegin = reinterpret_cast<const char*>(bytes.data() + 20U);
    if (!ParseJsonDocumentUVE(std::string_view{jsonBegin, jsonLength}, outDocument)) {
        return false;
    }

    bool foundBinaryChunk = false;
    std::size_t offset = 20U + jsonLength;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < kGlbChunkHeaderBytesUVE) {
            return false;
        }
        const std::size_t chunkLength = ReadU32LittleEndianUVE(bytes, offset);
        const std::uint32_t chunkType = ReadU32LittleEndianUVE(bytes, offset + 4U);
        offset += kGlbChunkHeaderBytesUVE;
        if (chunkLength > bytes.size() - offset) {
            return false;
        }
        if (chunkType == 0x004E4942U) {
            if (foundBinaryChunk) {
                return false;
            }
            outBuffer.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                             bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunkLength));
            foundBinaryChunk = true;
        }
        offset += chunkLength;
    }
    return foundBinaryChunk;
}

[[nodiscard]] bool LoadGltfDocumentAndBufferUVE(const std::filesystem::path& sourcePath,
                                                const std::vector<std::byte>& sourceBytes,
                                                nlohmann::json& outDocument,
                                                std::vector<std::byte>& outBuffer) {
    std::string extension = sourcePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    if (extension == ".glb") {
        return ExtractGlbDocumentAndBufferUVE(sourceBytes, outDocument, outBuffer);
    }
    const auto* const jsonBegin = reinterpret_cast<const char*>(sourceBytes.data());
    if (!ParseJsonDocumentUVE(std::string_view{jsonBegin, sourceBytes.size()}, outDocument)) {
        return false;
    }

    if (!outDocument.contains("buffers") || !outDocument.at("buffers").is_array() ||
        outDocument.at("buffers").size() != 1U || !outDocument.at("buffers").at(0).is_object()) {
        return false;
    }
    const auto& buffer = outDocument.at("buffers").at(0);
    const auto byteLength = Detail::ReadJsonUintUVE(buffer, "byteLength", true);
    if (!byteLength.has_value() || *byteLength > kMaximumGltfDataUriDecodedBytesUVE) {
        return false;
    }
    if (!buffer.contains("uri") || !buffer.at("uri").is_string()) {
        return false;
    }
    const std::string uri = buffer.at("uri").get<std::string>();
    const auto uriKind = ClassifyGltfResourceUriUVE(uri);
    if (uriKind == GltfResourceUriKindUVE::DataUri) {
        if (!DecodeGltfDataUriUVE(uri, outBuffer, kMaximumGltfDataUriDecodedBytesUVE)) {
            return false;
        }
    } else if (uriKind == GltfResourceUriKindUVE::RelativePath) {
        std::string normalizedUri = uri;
        std::replace(normalizedUri.begin(), normalizedUri.end(), '\\', '/');
        if (!Detail::ReadBoundedSourceBytesUVE(sourcePath.parent_path() / normalizedUri, kGltfImporterNameUVE,
                                               kMaximumGltfSourceBytesUVE, outBuffer)) {
            return false;
        }
    } else {
        return false;
    }
    return outBuffer.size() >= *byteLength;
}

/// Reads `mesh.extras.targetNames`, the only place glTF carries blend-shape names. A missing or
/// malformed array yields no names, which the converter turns into deterministic `targetN` ones.
[[nodiscard]] std::vector<std::string> ReadMorphTargetNamesUVE(const nlohmann::json& mesh) {
    std::vector<std::string> names;
    if (!mesh.contains("extras") || !mesh.at("extras").is_object() ||
        !mesh.at("extras").contains("targetNames") || !mesh.at("extras").at("targetNames").is_array()) {
        return names;
    }
    const auto& array = mesh.at("extras").at("targetNames");
    if (array.size() > kMaximumMeshMorphTargetsUVE) {
        return names;
    }
    names.reserve(array.size());
    for (const auto& entry : array) {
        names.push_back(entry.is_string() ? entry.get<std::string>() : std::string{});
    }
    return names;
}

/// Reads `mesh.weights`: the rest-pose weight of every morph target, or an empty vector when the
/// source declared none (a declared all-zero array stays a declared array - the two mean different
/// things to an author).
[[nodiscard]] bool ReadDefaultMorphWeightsUVE(const nlohmann::json& mesh, const std::size_t targetCount,
                                              std::vector<float>& outWeights) {
    outWeights.clear();
    if (!mesh.contains("weights")) {
        return true;
    }
    if (!mesh.at("weights").is_array() || mesh.at("weights").size() != targetCount ||
        targetCount > kMaximumMeshMorphTargetsUVE) {
        return false;
    }
    outWeights.reserve(targetCount);
    for (const auto& entry : mesh.at("weights")) {
        if (!entry.is_number()) {
            return false;
        }
        const double value = entry.get<double>();
        if (!std::isfinite(value) || value < -static_cast<double>(std::numeric_limits<float>::max()) ||
            value > static_cast<double>(std::numeric_limits<float>::max())) {
            return false;
        }
        outWeights.push_back(static_cast<float>(value));
    }
    return true;
}

/// Sentinel in the per-surface material-index table: this primitive names no glTF material.
constexpr std::uint32_t kInvalidGltfMaterialIndexUVE = 0xFFFFFFFFU;

/// Builds the geometry-only source of ONE glTF primitive, plus the glTF material index it names
/// (kInvalidGltfMaterialIndexUVE when it names none).
///
/// Every index it reads is checked against the document's own arrays, so a hostile or truncated
/// file cannot walk past one; a primitive that breaks any of these rules fails the import rather
/// than publishing a mesh with a hole in it.
[[nodiscard]] bool BuildPrimitiveSourceUVE(const nlohmann::json& document, const std::vector<std::byte>& buffer,
                                           const nlohmann::json& primitive, GltfPrimitiveSourceUVE& outSource,
                                           std::uint32_t& outMaterialIndex) {
    outMaterialIndex = kInvalidGltfMaterialIndexUVE;
    if (!primitive.is_object() || !primitive.contains("attributes") || !primitive.at("attributes").is_object()) {
        return false;
    }
    const auto& attributes = primitive.at("attributes");
    const auto positionIndex = Detail::ReadJsonUintUVE(attributes, "POSITION", true);
    if (!positionIndex) {
        return false;
    }
    outSource.mode = primitive.contains("mode") ? Detail::ReadJsonU32UVE(primitive, "mode").value_or(0U) : 4U;
    const auto positions = Detail::BuildAccessorViewUVE(document, buffer, *positionIndex, "VEC3", false);
    if (!positions) {
        return false;
    }
    outSource.positions = *positions;
    if (attributes.contains("NORMAL")) {
        const auto normalIndex = Detail::ReadJsonUintUVE(attributes, "NORMAL", true);
        if (!normalIndex) return false;
        const auto normals = Detail::BuildAccessorViewUVE(document, buffer, *normalIndex, "VEC3", false);
        if (!normals) return false;
        outSource.normals = *normals;
    }
    if (attributes.contains("TEXCOORD_0")) {
        const auto texcoordIndex = Detail::ReadJsonUintUVE(attributes, "TEXCOORD_0", true);
        if (!texcoordIndex) return false;
        const auto texcoords = Detail::BuildAccessorViewUVE(document, buffer, *texcoordIndex, "VEC2", false);
        if (!texcoords) return false;
        outSource.texcoords0 = *texcoords;
    }
    if (primitive.contains("indices")) {
        const auto indexAccessor = Detail::ReadJsonUintUVE(primitive, "indices", true);
        if (!indexAccessor) return false;
        const auto indices = Detail::BuildAccessorViewUVE(document, buffer, *indexAccessor, "SCALAR", true);
        if (!indices) return false;
        outSource.indices = *indices;
    }
    if (primitive.contains("material")) {
        const auto materialIndex = Detail::ReadJsonUintUVE(primitive, "material", true);
        if (!materialIndex) {
            return false;
        }
        if (*materialIndex > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
            return false;
        }
        outMaterialIndex = static_cast<std::uint32_t>(*materialIndex);
    }
    return true;
}

/// Converts the document's single mesh into engine geometry.
///
/// A glTF mesh carries ONE OR MORE primitives, and every primitive is an independent vertex/index
/// run with its own material - which is exactly a surface. They are merged into the one vertex and
/// index buffer a `.uvmodel` holds, and each primitive's range is recorded as a surface so the
/// renderer can draw each with its own material without splitting the asset into several files or
/// buffers. A single-primitive mesh records no surfaces at all: it is the implicit
/// whole-buffer surface, which keeps its serialized bytes identical to what this importer always
/// produced.
///
/// `outSurfaceMaterialIndices` receives one glTF material index per primitive (kInvalid when the
/// primitive names none), in surface order. The caller resolves them into slots once the
/// materials have been published - the two orders are independent, so the mapping is done by
/// material index rather than by position.
[[nodiscard]] bool ConvertDocumentUVE(const nlohmann::json& document, const std::vector<std::byte>& buffer,
                                      const bool importMorphTargets, MeshAssetUVE& outMesh,
                                      std::vector<std::uint32_t>& outSurfaceMaterialIndices) {
    if (!document.contains("buffers") || !document.at("buffers").is_array() ||
        document.at("buffers").size() != 1U || !document.at("buffers").at(0).is_object()) {
        return false;
    }
    const auto declaredBufferLength = Detail::ReadJsonUintUVE(document.at("buffers").at(0), "byteLength", true);
    if (!declaredBufferLength.has_value() || *declaredBufferLength > kMaximumGltfDataUriDecodedBytesUVE ||
        buffer.size() < *declaredBufferLength) {
        return false;
    }
    if (!document.contains("meshes") || !document.at("meshes").is_array() || document.at("meshes").size() != 1U ||
        !document.at("meshes").at(0).is_object() || !document.at("meshes").at(0).contains("primitives") ||
        !document.at("meshes").at(0).at("primitives").is_array() ||
        document.at("meshes").at(0).at("primitives").empty() ||
        document.at("meshes").at(0).at("primitives").size() > kMaximumMeshSurfacesUVE) {
        return false;
    }
    const nlohmann::json& meshJson = document.at("meshes").at(0);
    const nlohmann::json& primitives = meshJson.at("primitives");
    const bool multiplePrimitives = primitives.size() > 1U;

    MeshAssetUVE merged;
    merged.surfaces.reserve(multiplePrimitives ? primitives.size() : 0U);
    std::vector<std::uint32_t> surfaceMaterialIndices;
    surfaceMaterialIndices.reserve(primitives.size());

    for (std::size_t primitiveIndex = 0U; primitiveIndex < primitives.size(); ++primitiveIndex) {
        const nlohmann::json& primitive = primitives.at(primitiveIndex);
        GltfPrimitiveSourceUVE source;
        std::uint32_t materialIndex = kInvalidGltfMaterialIndexUVE;
        if (!BuildPrimitiveSourceUVE(document, buffer, primitive, source, materialIndex)) {
            return false;
        }

        MeshAssetUVE part;
        if (multiplePrimitives) {
            // Blend shapes are per-primitive data indexed by that primitive's own vertex array.
            // Merging the arrays would need every primitive's targets to agree by name AND by
            // vertex range, which glTF does not require them to do - so a multi-primitive mesh
            // imports without shapes, with a diagnostic, rather than binding a target to the wrong
            // vertices.
            if (importMorphTargets && primitive.contains("targets")) {
                UVE_WARNING("GltfImporterUVE: a multi-primitive mesh's morph targets are not "
                            "imported; the mesh publishes without blend shapes");
            }
            if (!ConvertGltfPrimitiveUVE(source, part)) {
                return false;
            }
        } else {
            // Blend shapes. The setting is honoured here rather than after the fact: a user who
            // turned morph-target import off gets the base mesh and pays nothing for the shapes
            // they declined.
            GltfMorphTargetSetUVE morphSet;
            if (importMorphTargets && primitive.contains("targets")) {
                if (!primitive.at("targets").is_array() ||
                    primitive.at("targets").size() > kMaximumMeshMorphTargetsUVE) {
                    return false;
                }
                morphSet.names = ReadMorphTargetNamesUVE(meshJson);
                morphSet.targets.reserve(primitive.at("targets").size());
                for (const auto& targetJson : primitive.at("targets")) {
                    if (!targetJson.is_object()) {
                        return false;
                    }
                    // POSITION is mandatory in a glTF morph target: a shape with no displacement is
                    // not a shape, and accepting one would create a weight slot that moves nothing.
                    const auto targetPositionIndex = Detail::ReadJsonUintUVE(targetJson, "POSITION", true);
                    if (!targetPositionIndex) {
                        return false;
                    }
                    const auto targetPositions =
                        Detail::BuildAccessorViewUVE(document, buffer, *targetPositionIndex, "VEC3", false);
                    if (!targetPositions) {
                        return false;
                    }
                    GltfMorphTargetSourceUVE targetSource;
                    targetSource.positions = *targetPositions;
                    if (targetJson.contains("NORMAL")) {
                        const auto normalIndex = Detail::ReadJsonUintUVE(targetJson, "NORMAL", true);
                        if (!normalIndex) return false;
                        const auto normals =
                            Detail::BuildAccessorViewUVE(document, buffer, *normalIndex, "VEC3", false);
                        if (!normals) return false;
                        targetSource.normals = *normals;
                    }
                    if (targetJson.contains("TANGENT")) {
                        const auto tangentIndex = Detail::ReadJsonUintUVE(targetJson, "TANGENT", true);
                        if (!tangentIndex) return false;
                        const auto tangents =
                            Detail::BuildAccessorViewUVE(document, buffer, *tangentIndex, "VEC3", false);
                        if (!tangents) return false;
                        targetSource.tangents = *tangents;
                    }
                    morphSet.targets.push_back(std::move(targetSource));
                }
                if (!ReadDefaultMorphWeightsUVE(meshJson, morphSet.targets.size(), morphSet.defaultWeights)) {
                    return false;
                }
            }
            const bool converted = morphSet.IsEmptyUVE() ? ConvertGltfPrimitiveUVE(source, part)
                                                         : ConvertGltfPrimitiveUVE(source, morphSet, part);
            if (!converted) {
                return false;
            }
        }

        // Bounded before the merge, because a merged vertex offset is stored as a uint32 index and
        // an asset that would need a wider one cannot be represented at all.
        constexpr auto kMaximumIndexableCount =
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
        if (part.vertices.size() > kMaximumIndexableCount - merged.vertices.size() ||
            part.indices.size() > kMaximumIndexableCount - merged.indices.size()) {
            UVE_ERROR("GltfImporterUVE: the mesh's primitives exceed the addressable vertex or index "
                      "count of a .uvmodel");
            return false;
        }
        const auto vertexOffset = static_cast<std::uint32_t>(merged.vertices.size());
        const auto firstIndex = static_cast<std::uint32_t>(merged.indices.size());
        merged.vertices.insert(merged.vertices.end(), part.vertices.begin(), part.vertices.end());
        merged.indices.reserve(merged.indices.size() + part.indices.size());
        for (const std::uint32_t index : part.indices) {
            merged.indices.push_back(index + vertexOffset);
        }
        if (multiplePrimitives) {
            merged.surfaces.push_back(MeshSurfaceUVE{firstIndex,
                                                     static_cast<std::uint32_t>(part.indices.size()),
                                                     kInvalidMeshSurfaceMaterialSlotUVE});
        }
        surfaceMaterialIndices.push_back(materialIndex);
        if (primitiveIndex == 0U) {
            merged.localBounds = part.localBounds;
        } else {
            merged.localBounds.min.x = std::min(merged.localBounds.min.x, part.localBounds.min.x);
            merged.localBounds.min.y = std::min(merged.localBounds.min.y, part.localBounds.min.y);
            merged.localBounds.min.z = std::min(merged.localBounds.min.z, part.localBounds.min.z);
            merged.localBounds.max.x = std::max(merged.localBounds.max.x, part.localBounds.max.x);
            merged.localBounds.max.y = std::max(merged.localBounds.max.y, part.localBounds.max.y);
            merged.localBounds.max.z = std::max(merged.localBounds.max.z, part.localBounds.max.z);
        }
        if (!multiplePrimitives) {
            merged.morphTargets = std::move(part.morphTargets);
            merged.defaultMorphWeights = std::move(part.defaultMorphWeights);
        }
    }

    if (!IsMeshSurfaceDataValidUVE(merged)) {
        UVE_ERROR("GltfImporterUVE: the merged primitive ranges do not tile the index buffer");
        return false;
    }
    outMesh = std::move(merged);
    outSurfaceMaterialIndices = std::move(surfaceMaterialIndices);
    return true;
}

[[nodiscard]] bool ImportGltfSourceUVE(const std::filesystem::path& sourcePath,
                                       const std::filesystem::path& destinationPath,
                                       IAssetDatabaseUVE& assetDatabase,
                                       const AssetImportSettingsUVE& settings) {
    const MeshImportSettingsUVE meshSettings = ResolveMeshImportSettingsUVE(settings);
    try {
        if (destinationPath.extension() != ".uvmodel") {
            UVE_ERROR("GltfImporterUVE: destination \"{}\" must use the .uvmodel extension",
                      destinationPath.string());
            return false;
        }
        std::vector<std::byte> sourceBytes;
        if (!Detail::ReadBoundedSourceBytesUVE(sourcePath, kGltfImporterNameUVE, kMaximumGltfSourceBytesUVE,
                                               sourceBytes)) {
            return false;
        }
        nlohmann::json document;
        std::vector<std::byte> buffer;
        if (!LoadGltfDocumentAndBufferUVE(sourcePath, sourceBytes, document, buffer)) {
            UVE_ERROR("GltfImporterUVE: source \"{}\" failed bounded glTF/GLB structure or buffer loading",
                      sourcePath.string());
            return false;
        }
        MeshAssetUVE mesh;
        std::vector<std::uint32_t> surfaceMaterialIndices;
        if (!ConvertDocumentUVE(document, buffer, meshSettings.importMorphTargets, mesh,
                                surfaceMaterialIndices)) {
            UVE_ERROR("GltfImporterUVE: source \"{}\" failed bounded mesh conversion", sourcePath.string());
            return false;
        }
        // Materials and their textures are published BEFORE the mesh so a failure never leaves a
        // .uvmodel pointing at materials that do not exist. An unusable material is not a reason to
        // lose the geometry: it is skipped with a diagnostic and the mesh publishes untextured.
        Detail::GltfMaterialImportResultUVE materials;
        if (!Detail::ImportGltfMaterialsUVE(document, buffer, sourcePath, destinationPath, assetDatabase,
                                            materials)) {
            return false;
        }
        mesh.materialGuids = std::move(materials.materialGuids);
        // Resolve each surface's glTF material index into the slot it occupies in the mesh's
        // material table. A primitive whose material was not publishable keeps an invalid slot,
        // which the renderer treats as "no material of its own" rather than as a broken reference.
        for (std::size_t surfaceIndex = 0U; surfaceIndex < mesh.surfaces.size(); ++surfaceIndex) {
            const std::uint32_t materialIndex = surfaceIndex < surfaceMaterialIndices.size()
                                                    ? surfaceMaterialIndices[surfaceIndex]
                                                    : kInvalidGltfMaterialIndexUVE;
            if (materialIndex == kInvalidGltfMaterialIndexUVE) {
                continue;
            }
            const auto slot = materials.materialIndexToSlot.find(materialIndex);
            if (slot != materials.materialIndexToSlot.end()) {
                mesh.surfaces[surfaceIndex].materialSlot = slot->second;
            }
        }
        return Detail::PublishAssetAtomicallyUVE(destinationPath, kGltfImporterNameUVE, kGltfTemporarySuffixUVE,
                                                 [&mesh](const std::filesystem::path& temporaryPath) {
                                                     return SaveMeshAssetUVE(mesh, temporaryPath);
                                                 });
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace

void RegisterGltfImporterUVE(IAssetImporterUVE& importer) {
    // Database-aware: the importer publishes the primitive's material and its textures beside the
    // model and needs their GUIDs before it writes the .uvmodel that references them.
    importer.RegisterDatabaseAwareImporterUVE("gltf", &ImportGltfSourceUVE);
    importer.RegisterDatabaseAwareImporterUVE("glb", &ImportGltfSourceUVE);
}

} // namespace UVE::Asset
