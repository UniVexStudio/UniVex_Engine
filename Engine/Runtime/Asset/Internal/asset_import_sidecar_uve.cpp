// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/asset_import_sidecar_uve.h"

#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

namespace UVE::Asset {

std::filesystem::path GetImportSidecarPathUVE(const std::filesystem::path& sourcePath) {
    std::filesystem::path sidecar = sourcePath;
    sidecar += ".uvimport";
    return sidecar;
}

bool SaveImportSidecarUVE(const AssetImportSidecarUVE& sidecar, const std::filesystem::path& sidecarPath) {
    if (sidecarPath.empty() || sidecar.importerVersion.empty()) {
        return false;
    }
    nlohmann::json doc;
    doc["importerVersion"] = sidecar.importerVersion;
    doc["presetName"] = sidecar.presetName;
    doc["reimportNeeded"] = sidecar.reimportNeeded;
    doc["warnings"] = sidecar.warnings;
    doc["options"] = sidecar.options;

    std::ofstream stream(sidecarPath, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        return false;
    }
    stream << doc.dump(2);
    return stream.good();
}

bool LoadImportSidecarUVE(const std::filesystem::path& sidecarPath, AssetImportSidecarUVE& outSidecar) {
    std::ifstream stream(sidecarPath, std::ios::binary);
    if (!stream.is_open()) {
        return false;
    }
    nlohmann::json doc;
    try {
        stream >> doc;
        AssetImportSidecarUVE decoded;
        decoded.importerVersion = doc.value("importerVersion", std::string("v1"));
        decoded.presetName = doc.value("presetName", std::string());
        decoded.reimportNeeded = doc.value("reimportNeeded", false);
        if (doc.contains("warnings") && doc["warnings"].is_array()) {
            decoded.warnings = doc["warnings"].get<std::vector<std::string>>();
        }
        if (doc.contains("options") && doc["options"].is_object()) {
            decoded.options = doc["options"].get<std::map<std::string, std::string>>();
        }
        outSidecar = std::move(decoded);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

std::string MeshImportSettingsUVE::GetCacheVersionUVE() const {
    return "mesh-import-v1;scale=" + std::to_string(scaleFactor) +
           ";lods=" + std::to_string(generateLods ? lodCount : 0U) +
           ";collision=" + std::to_string(static_cast<std::uint32_t>(collisionMode)) +
           ";skeleton=" + std::to_string(importSkeleton ? 1U : 0U);
}

MeshImportSettingsUVE ResolveMeshImportSettingsUVE(const AssetImportSettingsUVE& settings) noexcept {
    const auto* const meshSettings = dynamic_cast<const MeshImportSettingsUVE*>(&settings);
    return meshSettings == nullptr ? MeshImportSettingsUVE{} : *meshSettings;
}

bool IsPrimitiveGeometryParametersValidUVE(const PrimitiveGeometryParametersUVE& params) noexcept {
    return std::isfinite(params.boxSize.x) && params.boxSize.x > 0.0F &&
           std::isfinite(params.boxSize.y) && params.boxSize.y > 0.0F &&
           std::isfinite(params.boxSize.z) && params.boxSize.z > 0.0F &&
           std::isfinite(params.sphereRadius) && params.sphereRadius > 0.0F &&
           std::isfinite(params.sphereHeight) && params.sphereHeight > 0.0F &&
           params.sphereRadialSegments >= 3U && params.sphereRings >= 2U &&
           std::isfinite(params.capsuleRadius) && params.capsuleRadius > 0.0F &&
           std::isfinite(params.capsuleHeight) && params.capsuleHeight >= params.capsuleRadius * 2.0F &&
           std::isfinite(params.cylinderHeight) && params.cylinderHeight > 0.0F &&
           std::isfinite(params.planeWidth) && params.planeWidth > 0.0F &&
           std::isfinite(params.planeDepth) && params.planeDepth > 0.0F &&
           std::isfinite(params.torusInnerRadius) && params.torusInnerRadius > 0.0F &&
           std::isfinite(params.torusOuterRadius) && params.torusOuterRadius > params.torusInnerRadius;
}

std::string AudioImportSettingsUVE::GetCacheVersionUVE() const {
    return "audio-import-v1;mode=" + std::to_string(static_cast<std::uint32_t>(loadMode)) +
           ";format=" + compressionFormat + ";mono=" + std::to_string(forceMono ? 1U : 0U) +
           ";rate=" + std::to_string(targetSampleRateHz);
}

std::string FontImportSettingsUVE::GetCacheVersionUVE() const {
    return "font-import-v1;mode=" + std::to_string(static_cast<std::uint32_t>(renderingMode)) +
           ";size=" + std::to_string(baseSizePixels) + ";atlas=" + std::to_string(atlasWidth) + "x" +
           std::to_string(atlasHeight);
}

} // namespace UVE::Asset
