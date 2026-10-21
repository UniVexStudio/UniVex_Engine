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

[[nodiscard]] bool ConvertDocumentUVE(const nlohmann::json& document, const std::vector<std::byte>& buffer,
                                      const bool importMorphTargets, MeshAssetUVE& outMesh) {
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
        document.at("meshes").at(0).at("primitives").size() != 1U) {
        return false;
    }
    const auto& primitive = document.at("meshes").at(0).at("primitives").at(0);
    if (!primitive.is_object() || !primitive.contains("attributes") || !primitive.at("attributes").is_object()) {
        return false;
    }
    const auto& attributes = primitive.at("attributes");
    const auto positionIndex = Detail::ReadJsonUintUVE(attributes, "POSITION", true);
    if (!positionIndex) {
        return false;
    }
    GltfPrimitiveSourceUVE source;
    source.mode = primitive.contains("mode") ? Detail::ReadJsonU32UVE(primitive, "mode").value_or(0U) : 4U;
    const auto positions = Detail::BuildAccessorViewUVE(document, buffer, *positionIndex, "VEC3", false);
    if (!positions) {
        return false;
    }
    source.positions = *positions;
    if (attributes.contains("NORMAL")) {
        const auto normalIndex = Detail::ReadJsonUintUVE(attributes, "NORMAL", true);
        if (!normalIndex) return false;
        const auto normals = Detail::BuildAccessorViewUVE(document, buffer, *normalIndex, "VEC3", false);
        if (!normals) return false;
        source.normals = *normals;
    }
    if (attributes.contains("TEXCOORD_0")) {
        const auto texcoordIndex = Detail::ReadJsonUintUVE(attributes, "TEXCOORD_0", true);
        if (!texcoordIndex) return false;
        const auto texcoords = Detail::BuildAccessorViewUVE(document, buffer, *texcoordIndex, "VEC2", false);
        if (!texcoords) return false;
        source.texcoords0 = *texcoords;
    }
    if (primitive.contains("indices")) {
        const auto indexAccessor = Detail::ReadJsonUintUVE(primitive, "indices", true);
        if (!indexAccessor) return false;
        const auto indices = Detail::BuildAccessorViewUVE(document, buffer, *indexAccessor, "SCALAR", true);
        if (!indices) return false;
        source.indices = *indices;
    }

    // Blend shapes. The setting is honoured here rather than after the fact: a user who turned
    // morph-target import off gets the base mesh and pays nothing for the shapes they declined.
    GltfMorphTargetSetUVE morphSet;
    if (importMorphTargets && primitive.contains("targets")) {
        if (!primitive.at("targets").is_array() ||
            primitive.at("targets").size() > kMaximumMeshMorphTargetsUVE) {
            return false;
        }
        const auto& mesh = document.at("meshes").at(0);
        morphSet.names = ReadMorphTargetNamesUVE(mesh);
        morphSet.targets.reserve(primitive.at("targets").size());
        for (const auto& targetJson : primitive.at("targets")) {
            if (!targetJson.is_object()) {
                return false;
            }
            // POSITION is mandatory in a glTF morph target: a shape with no displacement is not a
            // shape, and accepting one would create a weight slot that moves nothing.
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
                const auto normals = Detail::BuildAccessorViewUVE(document, buffer, *normalIndex, "VEC3", false);
                if (!normals) return false;
                targetSource.normals = *normals;
            }
            if (targetJson.contains("TANGENT")) {
                const auto tangentIndex = Detail::ReadJsonUintUVE(targetJson, "TANGENT", true);
                if (!tangentIndex) return false;
                const auto tangents = Detail::BuildAccessorViewUVE(document, buffer, *tangentIndex, "VEC3", false);
                if (!tangents) return false;
                targetSource.tangents = *tangents;
            }
            morphSet.targets.push_back(std::move(targetSource));
        }
        if (!ReadDefaultMorphWeightsUVE(mesh, morphSet.targets.size(), morphSet.defaultWeights)) {
            return false;
        }
    }
    if (morphSet.IsEmptyUVE()) {
        return ConvertGltfPrimitiveUVE(source, outMesh);
    }
    return ConvertGltfPrimitiveUVE(source, morphSet, outMesh);
}

[[nodiscard]] bool ImportGltfSourceUVE(const std::filesystem::path& sourcePath,
                                       const std::filesystem::path& destinationPath,
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
        if (!ConvertDocumentUVE(document, buffer, meshSettings.importMorphTargets, mesh)) {
            UVE_ERROR("GltfImporterUVE: source \"{}\" failed bounded one-primitive mesh conversion",
                      sourcePath.string());
            return false;
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
    importer.RegisterImporterUVE("gltf", &ImportGltfSourceUVE);
    importer.RegisterImporterUVE("glb", &ImportGltfSourceUVE);
}

} // namespace UVE::Asset
