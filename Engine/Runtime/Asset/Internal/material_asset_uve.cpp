// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/material_asset_uve.h"

#include <cmath>

#include <nlohmann/json.hpp>

#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/math/color_uve.h"

namespace UVE::Asset {

namespace {

// Colors persist under the same "x"/"y"/"z" keys vectors always used: old .uvmat files load
// byte-identical, and the linear interpretation comes from the ColorUVE type, not the file.
[[nodiscard]] nlohmann::json ColorToJsonUVE(const Math::ColorUVE& value) {
    return nlohmann::json{{"x", value.r}, {"y", value.g}, {"z", value.b}};
}

[[nodiscard]] Math::ColorUVE JsonToColorUVE(const nlohmann::json& value) {
    return Math::ColorUVE{value.at("x").get<float>(), value.at("y").get<float>(), value.at("z").get<float>()};
}

[[nodiscard]] nlohmann::json Vector2ToJsonUVE(const Math::Vector2UVE& value) {
    return nlohmann::json{{"x", value.x}, {"y", value.y}};
}

[[nodiscard]] Math::Vector2UVE JsonToVector2UVE(const nlohmann::json& value) {
    return Math::Vector2UVE{value.at("x").get<float>(), value.at("y").get<float>()};
}

[[nodiscard]] bool IsFiniteUnitIntervalUVE(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

[[nodiscard]] bool IsFiniteNonNegativeColorUVE(const Math::ColorUVE& value) noexcept {
    return std::isfinite(value.r) && value.r >= 0.0F && std::isfinite(value.g) && value.g >= 0.0F &&
           std::isfinite(value.b) && value.b >= 0.0F;
}

} // namespace

bool IsMaterialAssetValidUVE(const MaterialAssetUVE& material) noexcept {
    return IsFiniteUnitIntervalUVE(material.albedoColor.r) &&
           IsFiniteUnitIntervalUVE(material.albedoColor.g) &&
           IsFiniteUnitIntervalUVE(material.albedoColor.b) &&
           IsFiniteUnitIntervalUVE(material.albedoAlpha) &&
           IsFiniteUnitIntervalUVE(material.metallic) && IsFiniteUnitIntervalUVE(material.roughness) &&
           IsFiniteNonNegativeColorUVE(material.emissiveColor) &&
           material.billboardMode <= MaterialBillboardModeUVE::Y &&
           material.alphaSource <= MaterialAlphaSourceUVE::None &&
           material.cullMode <= MaterialCullModeUVE::Disabled &&
           material.blendMode <= MaterialBlendModeUVE::Multiply &&
           material.depthDrawMode <= MaterialDepthDrawModeUVE::DepthPrePass &&
           std::isfinite(material.emissiveEnergy) && material.emissiveEnergy >= 0.0F &&
           std::isfinite(material.normalScale) && material.normalScale >= 0.0F &&
           IsFiniteUnitIntervalUVE(material.occlusionStrength) &&
           std::isfinite(material.uvScale.x) && std::isfinite(material.uvScale.y) &&
           std::isfinite(material.uvOffset.x) && std::isfinite(material.uvOffset.y) &&
           std::isfinite(material.uv2Scale.x) && std::isfinite(material.uv2Scale.y) &&
           std::isfinite(material.uv2Offset.x) && std::isfinite(material.uv2Offset.y) &&
           IsFiniteUnitIntervalUVE(material.alphaCutoff) &&
           IsFiniteUnitIntervalUVE(material.dielectricSpecular) &&
           std::isfinite(material.heightScale) && material.heightScale >= 0.0F &&
           IsFiniteUnitIntervalUVE(material.rimAmount) &&
           IsFiniteUnitIntervalUVE(material.clearcoatAmount) &&
           IsFiniteUnitIntervalUVE(material.clearcoatRoughness) &&
           IsFiniteUnitIntervalUVE(material.anisotropyAmount) &&
           IsFiniteUnitIntervalUVE(material.subsurfaceStrength) &&
           IsFiniteUnitIntervalUVE(material.refractionAmount) &&
           std::isfinite(material.textureTileX) && std::isfinite(material.textureTileY);
}

bool TryMakeMaterialBillboardRotationUVE(const MaterialBillboardModeUVE mode,
                                         const Math::Vector3UVE& objectPosition,
                                         const Math::Vector3UVE& cameraPosition,
                                         Math::QuaternionUVE& outRotation) noexcept {
    if (mode == MaterialBillboardModeUVE::Disabled || mode > MaterialBillboardModeUVE::Y) {
        return false;
    }
    if (!Math::IsFiniteUVE(objectPosition) || !Math::IsFiniteUVE(cameraPosition)) {
        return false;
    }
    Math::Vector3UVE toCamera = cameraPosition - objectPosition;
    if (mode == MaterialBillboardModeUVE::Y) {
        toCamera.y = 0.0F;
    }
    if (!Math::IsFiniteUVE(toCamera) || Math::LengthSquaredUVE(toCamera) <= 1.0e-12F) {
        return false;
    }
    return Math::TryMakeLookAtUVE(toCamera, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, outRotation);
}

bool LoadMaterialAssetUVE(const std::filesystem::path& path, MaterialAssetUVE& outMaterial) {
    const std::optional<std::pair<UveFileHeaderUVE, std::vector<std::byte>>> file = ReadUveFileUVE(path);
    if (!file.has_value()) {
        return false; // ReadUveFileUVE already logged the specific reason.
    }
    if (file->first.assetType != AssetKindUVE::Material) {
        UVE_ERROR("MaterialAssetUVE: \"{}\" is not a material file (asset type {})", path.string(),
                   static_cast<std::uint32_t>(file->first.assetType));
        return false;
    }

    const std::vector<std::byte>& payloadBuffer = file->second;
    const std::string payloadText(reinterpret_cast<const char*>(payloadBuffer.data()), payloadBuffer.size());

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(payloadText);
    } catch (const nlohmann::json::parse_error& parseError) {
        UVE_ERROR("MaterialAssetUVE: failed to parse \"{}\": {}", path.string(), parseError.what());
        return false;
    }

    MaterialAssetUVE material;
    try {
        material.albedoColor = JsonToColorUVE(payload.at("albedoColor"));
        material.albedoTexture = AssetGuidUVE{payload.at("albedoTexture").get<std::uint64_t>()};
        material.normalTexture = AssetGuidUVE{payload.at("normalTexture").get<std::uint64_t>()};
        material.metallic = payload.at("metallic").get<float>();
        material.roughness = payload.at("roughness").get<float>();
        material.aoTexture = AssetGuidUVE{payload.at("aoTexture").get<std::uint64_t>()};
        material.emissiveColor = JsonToColorUVE(payload.at("emissiveColor"));
        material.vertexShader = AssetGuidUVE{payload.at("vertexShader").get<std::uint64_t>()};
        material.fragmentShader = AssetGuidUVE{payload.at("fragmentShader").get<std::uint64_t>()};
        material.isTransparent = payload.at("isTransparent").get<bool>();
        if (payload.contains("billboardMode")) {
            material.billboardMode =
                static_cast<MaterialBillboardModeUVE>(payload.at("billboardMode").get<std::uint8_t>());
        }
        if (payload.contains("metallicRoughnessTexture")) {
            material.metallicRoughnessTexture =
                AssetGuidUVE{payload.at("metallicRoughnessTexture").get<std::uint64_t>()};
        }
        if (payload.contains("emissiveTexture")) {
            material.emissiveTexture = AssetGuidUVE{payload.at("emissiveTexture").get<std::uint64_t>()};
        }
        if (payload.contains("emissiveEnergy")) {
            material.emissiveEnergy = payload.at("emissiveEnergy").get<float>();
        }
        if (payload.contains("normalScale")) {
            material.normalScale = payload.at("normalScale").get<float>();
        }
        if (payload.contains("occlusionStrength")) {
            material.occlusionStrength = payload.at("occlusionStrength").get<float>();
        }
        if (payload.contains("uvScale")) {
            material.uvScale = JsonToVector2UVE(payload.at("uvScale"));
        }
        if (payload.contains("uvOffset")) {
            material.uvOffset = JsonToVector2UVE(payload.at("uvOffset"));
        }
        if (payload.contains("unshaded")) {
            material.unshaded = payload.at("unshaded").get<bool>();
        }
        if (payload.contains("alphaCutoff")) {
            material.alphaCutoff = payload.at("alphaCutoff").get<float>();
        }
        if (payload.contains("albedoAlpha")) {
            material.albedoAlpha = payload.at("albedoAlpha").get<float>();
        }
        if (payload.contains("metallicTexture")) {
            material.metallicTexture = AssetGuidUVE{payload.at("metallicTexture").get<std::uint64_t>()};
        }
        if (payload.contains("roughnessTexture")) {
            material.roughnessTexture = AssetGuidUVE{payload.at("roughnessTexture").get<std::uint64_t>()};
        }
        if (payload.contains("cullMode")) {
            material.cullMode = static_cast<MaterialCullModeUVE>(payload.at("cullMode").get<std::uint8_t>());
        }
        if (payload.contains("blendMode")) {
            material.blendMode = static_cast<MaterialBlendModeUVE>(payload.at("blendMode").get<std::uint8_t>());
        }
        if (payload.contains("clearcoatAmount")) {
            material.clearcoatAmount = payload.at("clearcoatAmount").get<float>();
        }
        if (payload.contains("parentMaterial")) {
            material.parentMaterial = AssetGuidUVE{payload.at("parentMaterial").get<std::uint64_t>()};
        }
        if (payload.contains("nextPassMaterial")) {
            material.nextPassMaterial = AssetGuidUVE{payload.at("nextPassMaterial").get<std::uint64_t>()};
        }
        if (payload.contains("useCustomShaderCode")) {
            material.useCustomShaderCode = payload.at("useCustomShaderCode").get<bool>();
        }
        if (payload.contains("customShader")) {
            material.customShader = AssetGuidUVE{payload.at("customShader").get<std::uint64_t>()};
        }
        if (payload.contains("textureTint")) {
            material.textureTint = JsonToColorUVE(payload.at("textureTint"));
        }
        if (payload.contains("textureTileX")) {
            material.textureTileX = payload.at("textureTileX").get<float>();
        }
        if (payload.contains("textureTileY")) {
            material.textureTileY = payload.at("textureTileY").get<float>();
        }
    } catch (const nlohmann::json::exception& fieldError) {
        UVE_ERROR("MaterialAssetUVE: \"{}\" is missing an expected field: {}", path.string(), fieldError.what());
        return false;
    }

    if (!IsMaterialAssetValidUVE(material)) {
        UVE_ERROR("MaterialAssetUVE: decoded material contains invalid values in {}", path.string());
        return false;
    }
    outMaterial = material;
    return true;
}

bool SaveMaterialAssetUVE(const MaterialAssetUVE& material, const std::filesystem::path& path) {
    if (!IsMaterialAssetValidUVE(material)) {
        UVE_ERROR("MaterialAssetUVE: rejected non-finite or out-of-range values before writing {}", path.string());
        return false;
    }
    nlohmann::json payload;
    payload["albedoColor"] = ColorToJsonUVE(material.albedoColor);
    payload["albedoTexture"] = material.albedoTexture.value;
    payload["normalTexture"] = material.normalTexture.value;
    payload["metallic"] = material.metallic;
    payload["roughness"] = material.roughness;
    payload["aoTexture"] = material.aoTexture.value;
    payload["emissiveColor"] = ColorToJsonUVE(material.emissiveColor);
    payload["vertexShader"] = material.vertexShader.value;
    payload["fragmentShader"] = material.fragmentShader.value;
    payload["isTransparent"] = material.isTransparent;
    payload["billboardMode"] = static_cast<std::uint8_t>(material.billboardMode);
    payload["metallicRoughnessTexture"] = material.metallicRoughnessTexture.value;
    payload["emissiveTexture"] = material.emissiveTexture.value;
    payload["emissiveEnergy"] = material.emissiveEnergy;
    payload["normalScale"] = material.normalScale;
    payload["occlusionStrength"] = material.occlusionStrength;
    payload["uvScale"] = Vector2ToJsonUVE(material.uvScale);
    payload["uvOffset"] = Vector2ToJsonUVE(material.uvOffset);
    payload["unshaded"] = material.unshaded;
    payload["alphaCutoff"] = material.alphaCutoff;
    if (material.albedoAlpha != 1.0F) {
        payload["albedoAlpha"] = material.albedoAlpha;
    }
    if (material.metallicTexture != kInvalidAssetGuidUVE) {
        payload["metallicTexture"] = material.metallicTexture.value;
    }
    if (material.roughnessTexture != kInvalidAssetGuidUVE) {
        payload["roughnessTexture"] = material.roughnessTexture.value;
    }
    if (material.cullMode != MaterialCullModeUVE::Back) {
        payload["cullMode"] = static_cast<std::uint8_t>(material.cullMode);
    }
    if (material.blendMode != MaterialBlendModeUVE::Opaque) {
        payload["blendMode"] = static_cast<std::uint8_t>(material.blendMode);
    }
    if (material.clearcoatAmount != 0.0F) {
        payload["clearcoatAmount"] = material.clearcoatAmount;
    }
    if (material.parentMaterial != kInvalidAssetGuidUVE) {
        payload["parentMaterial"] = material.parentMaterial.value;
    }
    if (material.nextPassMaterial != kInvalidAssetGuidUVE) {
        payload["nextPassMaterial"] = material.nextPassMaterial.value;
    }
    if (material.useCustomShaderCode) {
        payload["useCustomShaderCode"] = material.useCustomShaderCode;
    }
    if (material.customShader != kInvalidAssetGuidUVE) {
        payload["customShader"] = material.customShader.value;
    }
    if (material.textureTileX != 1.0F) {
        payload["textureTileX"] = material.textureTileX;
    }
    if (material.textureTileY != 1.0F) {
        payload["textureTileY"] = material.textureTileY;
    }

    const std::string payloadText = payload.dump();
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const std::vector<std::byte> payloadBuffer(payloadBytes, payloadBytes + payloadText.size());

    return WriteUveFileUVE(path, AssetKindUVE::Material, payloadBuffer);
}

} // namespace UVE::Asset
