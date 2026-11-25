// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "material_asset.h"

#include <array>

#include <algorithm>

#include <cmath>

#include <nlohmann/json.hpp>

#include "uve_file_envelope.h"
#include "logging_macros.h"
#include "vector2.h"
#include "vector3.h"
#include "color.h"

namespace UVE::Asset {

namespace {

// Colors persist under the same "x"/"y"/"z" keys vectors always used: old .uvmat files load
// byte-identical, and the linear interpretation comes from the Color type, not the file.
[[nodiscard]] nlohmann::json ColorToJson(const Math::Color& value) {
    return nlohmann::json{{"x", value.r}, {"y", value.g}, {"z", value.b}};
}

[[nodiscard]] Math::Color JsonToColor(const nlohmann::json& value) {
    return Math::Color{value.at("x").get<float>(), value.at("y").get<float>(), value.at("z").get<float>()};
}

[[nodiscard]] nlohmann::json Vector2ToJson(const Math::Vector2& value) {
    return nlohmann::json{{"x", value.x}, {"y", value.y}};
}

[[nodiscard]] Math::Vector2 JsonToVector2(const nlohmann::json& value) {
    return Math::Vector2{value.at("x").get<float>(), value.at("y").get<float>()};
}

[[nodiscard]] bool IsFiniteUnitInterval(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

/// The JSON key of each MaterialMap's settings, in order.
constexpr std::array<const char*, static_cast<std::size_t>(MaterialMap::Count)> kMaterialMapNames{
    "albedo", "normal", "metallicRoughness", "occlusion", "emissive", "metallic", "roughness"};

[[nodiscard]] bool IsFiniteNonNegativeColor(const Math::Color& value) noexcept {
    return std::isfinite(value.r) && value.r >= 0.0F && std::isfinite(value.g) && value.g >= 0.0F &&
           std::isfinite(value.b) && value.b >= 0.0F;
}

} // namespace

bool IsMaterialAssetValid(const MaterialAsset& material) noexcept {
    return IsFiniteUnitInterval(material.albedoColor.r) &&
           IsFiniteUnitInterval(material.albedoColor.g) &&
           IsFiniteUnitInterval(material.albedoColor.b) &&
           IsFiniteUnitInterval(material.albedoAlpha) &&
           IsFiniteUnitInterval(material.metallic) && IsFiniteUnitInterval(material.roughness) &&
           IsFiniteNonNegativeColor(material.emissiveColor) &&
           material.billboardMode <= MaterialBillboardMode::Y &&
           material.alphaSource <= MaterialAlphaSource::None &&
           material.cullMode <= MaterialCullMode::Disabled &&
           material.blendMode <= MaterialBlendMode::Multiply &&
           material.depthDrawMode <= MaterialDepthDrawMode::DepthPrePass &&
           material.kind <= MaterialKind::Shader &&
           std::isfinite(material.emissiveEnergy) && material.emissiveEnergy >= 0.0F &&
           std::isfinite(material.normalScale) && material.normalScale >= 0.0F &&
           IsFiniteUnitInterval(material.occlusionStrength) &&
           std::isfinite(material.uvScale.x) && std::isfinite(material.uvScale.y) &&
           std::isfinite(material.uvOffset.x) && std::isfinite(material.uvOffset.y) &&
           std::isfinite(material.uv2Scale.x) && std::isfinite(material.uv2Scale.y) &&
           std::isfinite(material.uv2Offset.x) && std::isfinite(material.uv2Offset.y) &&
           std::isfinite(material.triplanarBlendSharpness) && material.triplanarBlendSharpness >= 0.0F &&
           std::isfinite(material.uv2TriplanarSharpness) && material.uv2TriplanarSharpness >= 0.0F &&
           IsFiniteUnitInterval(material.alphaCutoff) &&
           IsFiniteUnitInterval(material.dielectricSpecular) &&
           std::isfinite(material.heightScale) && material.heightScale >= 0.0F &&
           IsFiniteUnitInterval(material.rimAmount) &&
           IsFiniteUnitInterval(material.rimTint.r) && IsFiniteUnitInterval(material.rimTint.g) &&
           IsFiniteUnitInterval(material.rimTint.b) &&
           IsFiniteUnitInterval(material.clearcoatAmount) &&
           IsFiniteUnitInterval(material.clearcoatRoughness) &&
           IsFiniteUnitInterval(material.anisotropyAmount) &&
           IsFiniteUnitInterval(material.subsurfaceStrength) &&
           IsFiniteUnitInterval(material.refractionAmount) &&
           material.parallaxMinLayers > 0U && material.parallaxMinLayers <= material.parallaxMaxLayers &&
           std::isfinite(material.proximityFadeDistance) && material.proximityFadeDistance >= 0.0F &&
           std::isfinite(material.distanceFadeNear) && material.distanceFadeNear >= 0.0F &&
           std::isfinite(material.distanceFadeFar) && material.distanceFadeFar >= 0.0F &&
           IsFiniteUnitInterval(material.textureTint.r) &&
           IsFiniteUnitInterval(material.textureTint.g) &&
           IsFiniteUnitInterval(material.textureTint.b) &&
           std::isfinite(material.textureTileX) && std::isfinite(material.textureTileY) &&
           std::ranges::all_of(material.maps, [](const MaterialMapSettings& map) {
               return std::isfinite(map.tiling.x) && std::isfinite(map.tiling.y) && std::isfinite(map.offset.x) &&
                      std::isfinite(map.offset.y) && map.channel <= MaterialTextureChannel::Rgb &&
                      IsFiniteUnitInterval(map.strength);
           });
}

bool TryMakeMaterialBillboardRotation(const MaterialBillboardMode mode,
                                         const Math::Vector3& objectPosition,
                                         const Math::Vector3& cameraPosition,
                                         Math::Quaternion& outRotation) noexcept {
    if (mode == MaterialBillboardMode::Disabled || mode > MaterialBillboardMode::Y) {
        return false;
    }
    if (!Math::IsFinite(objectPosition) || !Math::IsFinite(cameraPosition)) {
        return false;
    }
    Math::Vector3 toCamera = cameraPosition - objectPosition;
    if (mode == MaterialBillboardMode::Y) {
        toCamera.y = 0.0F;
    }
    if (!Math::IsFinite(toCamera) || Math::LengthSquared(toCamera) <= 1.0e-12F) {
        return false;
    }
    return Math::TryMakeLookAt(toCamera, Math::Vector3{0.0F, 1.0F, 0.0F}, outRotation);
}

bool LoadMaterialAsset(const std::filesystem::path& path, MaterialAsset& outMaterial) {
    const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(path);
    if (!file.has_value()) {
        return false; // ReadUveFile already logged the specific reason.
    }
    if (file->first.assetType != AssetKind::Material) {
        UVE_ERROR("MaterialAsset: \"{}\" is not a material file (asset type {})", path.string(),
                   static_cast<std::uint32_t>(file->first.assetType));
        return false;
    }

    const std::vector<std::byte>& payloadBuffer = file->second;
    const std::string payloadText(reinterpret_cast<const char*>(payloadBuffer.data()), payloadBuffer.size());

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(payloadText);
    } catch (const nlohmann::json::parse_error& parseError) {
        UVE_ERROR("MaterialAsset: failed to parse \"{}\": {}", path.string(), parseError.what());
        return false;
    }

    MaterialAsset material;
    try {
        material.albedoColor = JsonToColor(payload.at("albedoColor"));
        material.albedoTexture = AssetGuid{payload.at("albedoTexture").get<std::uint64_t>()};
        material.normalTexture = AssetGuid{payload.at("normalTexture").get<std::uint64_t>()};
        material.metallic = payload.at("metallic").get<float>();
        material.roughness = payload.at("roughness").get<float>();
        material.aoTexture = AssetGuid{payload.at("aoTexture").get<std::uint64_t>()};
        material.emissiveColor = JsonToColor(payload.at("emissiveColor"));
        material.vertexShader = AssetGuid{payload.at("vertexShader").get<std::uint64_t>()};
        material.fragmentShader = AssetGuid{payload.at("fragmentShader").get<std::uint64_t>()};
        material.isTransparent = payload.at("isTransparent").get<bool>();
        if (payload.contains("billboardMode")) {
            material.billboardMode =
                static_cast<MaterialBillboardMode>(payload.at("billboardMode").get<std::uint8_t>());
        }
        if (payload.contains("metallicRoughnessTexture")) {
            material.metallicRoughnessTexture =
                AssetGuid{payload.at("metallicRoughnessTexture").get<std::uint64_t>()};
        }
        if (payload.contains("emissiveTexture")) {
            material.emissiveTexture = AssetGuid{payload.at("emissiveTexture").get<std::uint64_t>()};
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
            material.uvScale = JsonToVector2(payload.at("uvScale"));
        }
        if (payload.contains("uvOffset")) {
            material.uvOffset = JsonToVector2(payload.at("uvOffset"));
        }
        if (payload.contains("uv2Scale")) {
            material.uv2Scale = JsonToVector2(payload.at("uv2Scale"));
        }
        if (payload.contains("uv2Offset")) {
            material.uv2Offset = JsonToVector2(payload.at("uv2Offset"));
        }
        const auto readBool = [&payload](const char* key, bool& out) {
            if (payload.contains(key)) {
                out = payload.at(key).get<bool>();
            }
        };
        const auto readFloat = [&payload](const char* key, float& out) {
            if (payload.contains(key)) {
                out = payload.at(key).get<float>();
            }
        };
        readBool("uv1Triplanar", material.triplanarEnabled);
        readBool("uv1WorldTriplanar", material.worldSpaceTriplanar);
        readFloat("uv1TriplanarSharpness", material.triplanarBlendSharpness);
        readBool("uv2Triplanar", material.uv2Triplanar);
        readBool("uv2WorldTriplanar", material.uv2WorldTriplanar);
        readFloat("uv2TriplanarSharpness", material.uv2TriplanarSharpness);
        readBool("aoOnUv2", material.aoOnUv2);
        readBool("emissionOnUv2", material.emissionOnUv2);
        // The rest of what the material Details edits (each written only when it differs from its
        // default, so older files read exactly as before).
        readFloat("dielectricSpecular", material.dielectricSpecular);
        readBool("normalFlipGreen", material.normalFlipGreen);
        readBool("useVertexColorAsAlbedo", material.useVertexColorAsAlbedo);
        readBool("vertexColorIsSrgb", material.vertexColorIsSrgb);
        readBool("disableAmbientLight", material.disableAmbientLight);
        readBool("disableFog", material.disableFog);
        readBool("aoAffectsDirectLight", material.aoAffectsDirectLight);
        readBool("receiveShadows", material.receiveShadows);
        readFloat("rimAmount", material.rimAmount);
        if (payload.contains("rimTint")) {
            material.rimTint = JsonToColor(payload.at("rimTint"));
        }
        readFloat("clearcoatRoughness", material.clearcoatRoughness);
        readFloat("anisotropyAmount", material.anisotropyAmount);
        readFloat("subsurfaceStrength", material.subsurfaceStrength);
        readFloat("refractionAmount", material.refractionAmount);
        readFloat("heightScale", material.heightScale);
        if (payload.contains("heightTexture")) {
            material.heightTexture = AssetGuid{payload.at("heightTexture").get<std::uint64_t>()};
        }
        if (payload.contains("parallaxMinLayers")) {
            material.parallaxMinLayers = payload.at("parallaxMinLayers").get<std::uint8_t>();
        }
        if (payload.contains("parallaxMaxLayers")) {
            material.parallaxMaxLayers = payload.at("parallaxMaxLayers").get<std::uint8_t>();
        }
        readFloat("proximityFadeDistance", material.proximityFadeDistance);
        readFloat("distanceFadeNear", material.distanceFadeNear);
        readFloat("distanceFadeFar", material.distanceFadeFar);
        if (payload.contains("renderLayerMask")) {
            material.renderLayerMask = payload.at("renderLayerMask").get<std::uint32_t>();
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
            material.metallicTexture = AssetGuid{payload.at("metallicTexture").get<std::uint64_t>()};
        }
        if (payload.contains("roughnessTexture")) {
            material.roughnessTexture = AssetGuid{payload.at("roughnessTexture").get<std::uint64_t>()};
        }
        if (payload.contains("cullMode")) {
            material.cullMode = static_cast<MaterialCullMode>(payload.at("cullMode").get<std::uint8_t>());
        }
        if (payload.contains("blendMode")) {
            material.blendMode = static_cast<MaterialBlendMode>(payload.at("blendMode").get<std::uint8_t>());
        }
        if (payload.contains("alphaSource")) {
            material.alphaSource =
                static_cast<MaterialAlphaSource>(payload.at("alphaSource").get<std::uint8_t>());
        }
        if (payload.contains("alphaHashingEnabled")) {
            material.alphaHashingEnabled = payload.at("alphaHashingEnabled").get<bool>();
        }
        if (payload.contains("depthDrawMode")) {
            material.depthDrawMode =
                static_cast<MaterialDepthDrawMode>(payload.at("depthDrawMode").get<std::uint8_t>());
        }
        if (payload.contains("depthTestEnabled")) {
            material.depthTestEnabled = payload.at("depthTestEnabled").get<bool>();
        }
        if (payload.contains("depthWriteEnabled")) {
            material.depthWriteEnabled = payload.at("depthWriteEnabled").get<bool>();
        }
        if (payload.contains("renderPriority")) {
            material.renderPriority = payload.at("renderPriority").get<std::int32_t>();
        }
        if (payload.contains("clearcoatAmount")) {
            material.clearcoatAmount = payload.at("clearcoatAmount").get<float>();
        }
        if (payload.contains("parentMaterial")) {
            material.parentMaterial = AssetGuid{payload.at("parentMaterial").get<std::uint64_t>()};
        }
        if (payload.contains("nextPassMaterial")) {
            material.nextPassMaterial = AssetGuid{payload.at("nextPassMaterial").get<std::uint64_t>()};
        }
        if (payload.contains("useCustomShaderCode")) {
            material.useCustomShaderCode = payload.at("useCustomShaderCode").get<bool>();
        }
        if (payload.contains("kind")) {
            material.kind = static_cast<MaterialKind>(payload.at("kind").get<std::uint8_t>());
        }
        if (payload.contains("customShader")) {
            material.customShader = AssetGuid{payload.at("customShader").get<std::uint64_t>()};
        }
        if (payload.contains("textureTint")) {
            material.textureTint = JsonToColor(payload.at("textureTint"));
        }
        if (payload.contains("textureTileX")) {
            material.textureTileX = payload.at("textureTileX").get<float>();
        }
        if (payload.contains("textureTileY")) {
            material.textureTileY = payload.at("textureTileY").get<float>();
        }
        // Each map's own reading, by name; a map the file leaves out reads as authored.
        if (payload.contains("maps") && payload.at("maps").is_object()) {
            const nlohmann::json& maps = payload.at("maps");
            for (std::size_t index = 0U; index < material.maps.size(); ++index) {
                const char* const name = kMaterialMapNames[index];
                if (!maps.contains(name) || !maps.at(name).is_object()) {
                    continue;
                }
                const nlohmann::json& entry = maps.at(name);
                MaterialMapSettings& map = material.maps[index];
                if (entry.contains("tiling")) {
                    map.tiling = JsonToVector2(entry.at("tiling"));
                }
                if (entry.contains("offset")) {
                    map.offset = JsonToVector2(entry.at("offset"));
                }
                if (entry.contains("channel")) {
                    map.channel = static_cast<MaterialTextureChannel>(entry.at("channel").get<std::uint8_t>());
                }
                map.invert = entry.value("invert", false);
                map.strength = entry.value("strength", 1.0F);
                map.onUv2 = entry.value("onUv2", false);
            }
        }
    } catch (const nlohmann::json::exception& fieldError) {
        UVE_ERROR("MaterialAsset: \"{}\" is missing an expected field: {}", path.string(), fieldError.what());
        return false;
    }

    if (!IsMaterialAssetValid(material)) {
        UVE_ERROR("MaterialAsset: decoded material contains invalid values in {}", path.string());
        return false;
    }
    outMaterial = material;
    return true;
}

bool SaveMaterialAsset(const MaterialAsset& material, const std::filesystem::path& path) {
    if (!IsMaterialAssetValid(material)) {
        UVE_ERROR("MaterialAsset: rejected non-finite or out-of-range values before writing {}", path.string());
        return false;
    }
    nlohmann::json payload;
    payload["albedoColor"] = ColorToJson(material.albedoColor);
    payload["albedoTexture"] = material.albedoTexture.value;
    payload["normalTexture"] = material.normalTexture.value;
    payload["metallic"] = material.metallic;
    payload["roughness"] = material.roughness;
    payload["aoTexture"] = material.aoTexture.value;
    payload["emissiveColor"] = ColorToJson(material.emissiveColor);
    payload["vertexShader"] = material.vertexShader.value;
    payload["fragmentShader"] = material.fragmentShader.value;
    payload["isTransparent"] = material.isTransparent;
    payload["billboardMode"] = static_cast<std::uint8_t>(material.billboardMode);
    payload["metallicRoughnessTexture"] = material.metallicRoughnessTexture.value;
    payload["emissiveTexture"] = material.emissiveTexture.value;
    payload["emissiveEnergy"] = material.emissiveEnergy;
    payload["normalScale"] = material.normalScale;
    payload["occlusionStrength"] = material.occlusionStrength;
    payload["uvScale"] = Vector2ToJson(material.uvScale);
    payload["uvOffset"] = Vector2ToJson(material.uvOffset);
    // UV2 and triplanar only when set, so materials that never use them keep their files as they were.
    if (material.uv2Scale.x != 1.0F || material.uv2Scale.y != 1.0F) {
        payload["uv2Scale"] = Vector2ToJson(material.uv2Scale);
    }
    if (material.uv2Offset.x != 0.0F || material.uv2Offset.y != 0.0F) {
        payload["uv2Offset"] = Vector2ToJson(material.uv2Offset);
    }
    const auto writeBool = [&payload](const char* key, const bool value) {
        if (value) {
            payload[key] = true;
        }
    };
    const auto writeSharpness = [&payload](const char* key, const float value) {
        if (value != 1.0F) {
            payload[key] = value;
        }
    };
    writeBool("uv1Triplanar", material.triplanarEnabled);
    writeBool("uv1WorldTriplanar", material.worldSpaceTriplanar);
    writeSharpness("uv1TriplanarSharpness", material.triplanarBlendSharpness);
    writeBool("uv2Triplanar", material.uv2Triplanar);
    writeBool("uv2WorldTriplanar", material.uv2WorldTriplanar);
    writeSharpness("uv2TriplanarSharpness", material.uv2TriplanarSharpness);
    writeBool("aoOnUv2", material.aoOnUv2);
    writeBool("emissionOnUv2", material.emissionOnUv2);
    const MaterialAsset defaults{};
    const auto writeFloat = [&payload](const char* key, const float value, const float fallback) {
        if (value != fallback) {
            payload[key] = value;
        }
    };
    const auto writeFlag = [&payload](const char* key, const bool value, const bool fallback) {
        if (value != fallback) {
            payload[key] = value;
        }
    };
    writeFloat("dielectricSpecular", material.dielectricSpecular, defaults.dielectricSpecular);
    writeFlag("normalFlipGreen", material.normalFlipGreen, defaults.normalFlipGreen);
    writeFlag("useVertexColorAsAlbedo", material.useVertexColorAsAlbedo, defaults.useVertexColorAsAlbedo);
    writeFlag("vertexColorIsSrgb", material.vertexColorIsSrgb, defaults.vertexColorIsSrgb);
    writeFlag("disableAmbientLight", material.disableAmbientLight, defaults.disableAmbientLight);
    writeFlag("disableFog", material.disableFog, defaults.disableFog);
    writeFlag("aoAffectsDirectLight", material.aoAffectsDirectLight, defaults.aoAffectsDirectLight);
    writeFlag("receiveShadows", material.receiveShadows, defaults.receiveShadows);
    writeFloat("rimAmount", material.rimAmount, defaults.rimAmount);
    if (material.rimTint.r != defaults.rimTint.r || material.rimTint.g != defaults.rimTint.g ||
        material.rimTint.b != defaults.rimTint.b) {
        payload["rimTint"] = ColorToJson(material.rimTint);
    }
    writeFloat("clearcoatRoughness", material.clearcoatRoughness, defaults.clearcoatRoughness);
    writeFloat("anisotropyAmount", material.anisotropyAmount, defaults.anisotropyAmount);
    writeFloat("subsurfaceStrength", material.subsurfaceStrength, defaults.subsurfaceStrength);
    writeFloat("refractionAmount", material.refractionAmount, defaults.refractionAmount);
    writeFloat("heightScale", material.heightScale, defaults.heightScale);
    if (material.heightTexture != kInvalidAssetGuid) {
        payload["heightTexture"] = material.heightTexture.value;
    }
    if (material.parallaxMinLayers != defaults.parallaxMinLayers) {
        payload["parallaxMinLayers"] = material.parallaxMinLayers;
    }
    if (material.parallaxMaxLayers != defaults.parallaxMaxLayers) {
        payload["parallaxMaxLayers"] = material.parallaxMaxLayers;
    }
    writeFloat("proximityFadeDistance", material.proximityFadeDistance, defaults.proximityFadeDistance);
    writeFloat("distanceFadeNear", material.distanceFadeNear, defaults.distanceFadeNear);
    writeFloat("distanceFadeFar", material.distanceFadeFar, defaults.distanceFadeFar);
    if (material.renderLayerMask != defaults.renderLayerMask) {
        payload["renderLayerMask"] = material.renderLayerMask;
    }
    payload["unshaded"] = material.unshaded;
    payload["alphaCutoff"] = material.alphaCutoff;
    if (material.albedoAlpha != 1.0F) {
        payload["albedoAlpha"] = material.albedoAlpha;
    }
    if (material.metallicTexture != kInvalidAssetGuid) {
        payload["metallicTexture"] = material.metallicTexture.value;
    }
    if (material.roughnessTexture != kInvalidAssetGuid) {
        payload["roughnessTexture"] = material.roughnessTexture.value;
    }
    if (material.cullMode != MaterialCullMode::Back) {
        payload["cullMode"] = static_cast<std::uint8_t>(material.cullMode);
    }
    if (material.blendMode != MaterialBlendMode::Opaque) {
        payload["blendMode"] = static_cast<std::uint8_t>(material.blendMode);
    }
    // alphaSource, depthDrawMode, depthTestEnabled and depthWriteEnabled decide what the fragment
    // shader emits and what the pipeline state is, and all four used to be dropped on save - the
    // Inspector could set them, the renderer could honour them, and reopening the material silently
    // reverted them to their defaults.
    if (material.alphaSource != MaterialAlphaSource::AlbedoAlpha) {
        payload["alphaSource"] = static_cast<std::uint8_t>(material.alphaSource);
    }
    if (material.alphaHashingEnabled) {
        payload["alphaHashingEnabled"] = material.alphaHashingEnabled;
    }
    if (material.depthDrawMode != MaterialDepthDrawMode::OpaqueOnly) {
        payload["depthDrawMode"] = static_cast<std::uint8_t>(material.depthDrawMode);
    }
    if (!material.depthTestEnabled) {
        payload["depthTestEnabled"] = material.depthTestEnabled;
    }
    if (!material.depthWriteEnabled) {
        payload["depthWriteEnabled"] = material.depthWriteEnabled;
    }
    if (material.renderPriority != 0) {
        payload["renderPriority"] = material.renderPriority;
    }
    if (material.clearcoatAmount != 0.0F) {
        payload["clearcoatAmount"] = material.clearcoatAmount;
    }
    if (material.parentMaterial != kInvalidAssetGuid) {
        payload["parentMaterial"] = material.parentMaterial.value;
    }
    if (material.nextPassMaterial != kInvalidAssetGuid) {
        payload["nextPassMaterial"] = material.nextPassMaterial.value;
    }
    if (material.useCustomShaderCode) {
        payload["useCustomShaderCode"] = material.useCustomShaderCode;
    }
    // Omitted for Standard so a material that never touched the selector keeps the same payload
    // bytes it had before the field existed.
    if (material.kind != MaterialKind::Standard) {
        payload["kind"] = static_cast<std::uint8_t>(material.kind);
    }
    if (material.customShader != kInvalidAssetGuid) {
        payload["customShader"] = material.customShader.value;
    }
    if (material.textureTint.r != defaults.textureTint.r || material.textureTint.g != defaults.textureTint.g ||
        material.textureTint.b != defaults.textureTint.b) {
        payload["textureTint"] = ColorToJson(material.textureTint);
    }
    if (material.textureTileX != 1.0F) {
        payload["textureTileX"] = material.textureTileX;
    }
    if (material.textureTileY != 1.0F) {
        payload["textureTileY"] = material.textureTileY;
    }
    // Only the maps read some other way than as authored, so an untouched material keeps its bytes.
    nlohmann::json maps = nlohmann::json::object();
    for (std::size_t index = 0U; index < material.maps.size(); ++index) {
        const MaterialMapSettings& map = material.maps[index];
        if (map == MaterialMapSettings{}) {
            continue;
        }
        maps[kMaterialMapNames[index]] = {{"tiling", Vector2ToJson(map.tiling)},
                                             {"offset", Vector2ToJson(map.offset)},
                                             {"channel", static_cast<std::uint8_t>(map.channel)},
                                             {"invert", map.invert},
                                             {"strength", map.strength},
                                             {"onUv2", map.onUv2}};
    }
    if (!maps.empty()) {
        payload["maps"] = std::move(maps);
    }

    const std::string payloadText = payload.dump();
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const std::vector<std::byte> payloadBuffer(payloadBytes, payloadBytes + payloadText.size());

    return WriteUveFile(path, AssetKind::Material, payloadBuffer);
}

} // namespace UVE::Asset
