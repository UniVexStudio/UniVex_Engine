// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "gltf_material_import_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gltf_document_uve.h"
#include "import_helpers_uve.h"

#include "uve/asset/gltf_metadata_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset::Detail {
namespace {

constexpr const char* kGltfImporterNameUVE = "GltfImporterUVE";
constexpr std::string_view kGltfTemporarySuffixUVE = ".uve_gltf_tmp";

/// glTF's own defaults for every factor a material may omit. They are part of the specification,
/// not a guess: an exporter that writes only `baseColorTexture` still means a white, fully rough,
/// non-metallic surface.
constexpr float kDefaultMetallicUVE = 1.0F;
constexpr float kDefaultRoughnessUVE = 1.0F;
constexpr float kDefaultNormalScaleUVE = 1.0F;
constexpr float kDefaultOcclusionStrengthUVE = 1.0F;
constexpr float kDefaultAlphaCutoffUVE = 0.5F;

[[nodiscard]] bool ReadJsonFloatUVE(const nlohmann::json& object, const char* key, float& outValue) {
    if (!object.contains(key)) {
        return false;
    }
    const auto& value = object.at(key);
    if (!value.is_number()) {
        return false;
    }
    const double number = value.get<double>();
    if (!std::isfinite(number) || number < -static_cast<double>(std::numeric_limits<float>::max()) ||
        number > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }
    outValue = static_cast<float>(number);
    return true;
}

/// Reads a unit-interval factor, falling back to the specification default when the document omits
/// it and clamping when an exporter writes a value outside [0,1]. A clamped factor still renders -
/// refusing the whole model over one out-of-range number would lose the mesh as well.
[[nodiscard]] float ReadUnitFactorUVE(const nlohmann::json& object, const char* key, const float fallback) {
    float value = 0.0F;
    if (!ReadJsonFloatUVE(object, key, value)) {
        return fallback;
    }
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] bool ReadFloatArrayUVE(const nlohmann::json& array, const std::size_t expectedSize,
                                     std::vector<float>& outValues) {
    if (!array.is_array() || array.size() != expectedSize) {
        return false;
    }
    outValues.clear();
    outValues.reserve(expectedSize);
    for (const auto& entry : array) {
        if (!entry.is_number()) {
            return false;
        }
        const double number = entry.get<double>();
        if (!std::isfinite(number)) {
            return false;
        }
        outValues.push_back(static_cast<float>(number));
    }
    return true;
}

/// `{"index": n}` as glTF's textureInfo. The optional `texCoord`, sampler and the per-slot
/// `scale`/`strength` that some slots add are read by the caller that needs them.
[[nodiscard]] bool ReadTextureInfoIndexUVE(const nlohmann::json& owner, const char* key, std::size_t& outIndex) {
    if (!owner.contains(key) || !owner.at(key).is_object()) {
        return false;
    }
    const auto index = ReadJsonUintUVE(owner.at(key), "index", true);
    if (!index || *index > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    outIndex = static_cast<std::size_t>(*index);
    return true;
}

/// Resolves one glTF element (material, texture, image) by index, returning null when the document
/// does not have it. Bounds come from the document itself, so a hostile file cannot make the
/// importer walk past an array end.
[[nodiscard]] const nlohmann::json* CheckedElementUVE(const nlohmann::json& document, const char* arrayKey,
                                                      const std::size_t index) {
    if (!document.contains(arrayKey) || !document.at(arrayKey).is_array()) {
        return nullptr;
    }
    const auto checked = CheckedJsonArrayIndexUVE(document.at(arrayKey), index);
    if (!checked || !document.at(arrayKey).at(*checked).is_object()) {
        return nullptr;
    }
    return &document.at(arrayKey).at(*checked);
}

/// Owns the per-import caches and naming for one document's material work, so a texture referenced
/// by two slots is published and registered once.
class GltfMaterialPublisherUVE final {
public:
    GltfMaterialPublisherUVE(const nlohmann::json& document, const std::vector<std::byte>& buffer,
                             std::filesystem::path sourceDirectory, std::filesystem::path destinationPath,
                             IAssetDatabaseUVE& assetDatabase)
        : m_document(document), m_buffer(buffer), m_sourceDirectory(std::move(sourceDirectory)),
          m_destinationPath(std::move(destinationPath)), m_assetDatabase(assetDatabase) {
        m_options.importerName = kGltfImporterNameUVE;
        m_options.temporarySuffix = kGltfTemporarySuffixUVE;
    }

    /// Publishes the material of the document's single imported primitive.
    [[nodiscard]] bool PublishPrimitiveMaterialUVE(GltfMaterialImportResultUVE& outResult) {
        if (!m_document.contains("meshes") || !m_document.at("meshes").is_array() ||
            m_document.at("meshes").empty()) {
            return true;
        }
        const nlohmann::json* const mesh = CheckedElementUVE(m_document, "meshes", 0U);
        if (mesh == nullptr || !mesh->contains("primitives") || !mesh->at("primitives").is_array() ||
            mesh->at("primitives").empty()) {
            return true;
        }
        const nlohmann::json* const primitive = CheckedElementUVE(*mesh, "primitives", 0U);
        if (primitive == nullptr || !primitive->contains("material")) {
            // No material on the primitive is a normal, valid glTF: the mesh has no authored look.
            return true;
        }
        const auto materialIndex = ReadJsonUintUVE(*primitive, "material", true);
        if (!materialIndex || *materialIndex > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            UVE_WARNING("{}: the imported primitive names a material index that is not a bounded unsigned "
                        "integer; the mesh publishes without a material",
                        kGltfImporterNameUVE);
            return true;
        }
        const nlohmann::json* const materialJson =
            CheckedElementUVE(m_document, "materials", static_cast<std::size_t>(*materialIndex));
        if (materialJson == nullptr) {
            UVE_WARNING("{}: the imported primitive names material {} but the document has no such material; "
                        "the mesh publishes without one",
                        kGltfImporterNameUVE, *materialIndex);
            return true;
        }

        MaterialAssetUVE material;
        PopulateMaterialUVE(*materialJson, material);
        if (!IsMaterialAssetValidUVE(material)) {
            UVE_WARNING("{}: material {} describes values the material validator rejects; the mesh publishes "
                        "without a material",
                        kGltfImporterNameUVE, *materialIndex);
            return true;
        }

        std::string authoredName = "material";
        if (materialJson->contains("name") && materialJson->at("name").is_string()) {
            authoredName = materialJson->at("name").get<std::string>();
        }
        const std::string modelStem = m_destinationPath.stem().string();
        const std::string stem = MakeMaterialFileStemUVE(
            modelStem, authoredName, static_cast<std::size_t>(*materialIndex), "material", m_usedMaterialStems);
        const std::filesystem::path materialPath = m_destinationPath.parent_path() / (stem + ".uvmat");
        if (!PublishAssetAtomicallyUVE(
                materialPath, kGltfImporterNameUVE, kGltfTemporarySuffixUVE,
                [&material](const std::filesystem::path& temporaryPath) {
                    return SaveMaterialAssetUVE(material, temporaryPath);
                })) {
            UVE_ERROR("{}: failed to publish the imported material \"{}\"", kGltfImporterNameUVE,
                      materialPath.string());
            return false;
        }
        const AssetGuidUVE guid = m_assetDatabase.RegisterUVE(materialPath);
        if (guid == kInvalidAssetGuidUVE) {
            UVE_ERROR("{}: asset database rejected imported material \"{}\"", kGltfImporterNameUVE,
                      materialPath.string());
            return false;
        }
        outResult.materialGuids.push_back(guid);
        outResult.primitiveMaterialGuid = guid;
        return true;
    }

private:
    void PopulateMaterialUVE(const nlohmann::json& materialJson, MaterialAssetUVE& material) {
        const nlohmann::json* const pbr =
            materialJson.contains("pbrMetallicRoughness") && materialJson.at("pbrMetallicRoughness").is_object()
                ? &materialJson.at("pbrMetallicRoughness")
                : nullptr;

        // glTF factors are authored in linear space, which is exactly what ColorUVE stores; unlike
        // MTL's display-authored Kd these must NOT go through the display conversion.
        std::vector<float> baseColor;
        if (pbr != nullptr && pbr->contains("baseColorFactor") &&
            ReadFloatArrayUVE(pbr->at("baseColorFactor"), 4U, baseColor)) {
            material.albedoColor = Math::ColorUVE{std::clamp(baseColor[0], 0.0F, 1.0F),
                                                  std::clamp(baseColor[1], 0.0F, 1.0F),
                                                  std::clamp(baseColor[2], 0.0F, 1.0F)};
            material.albedoAlpha = std::clamp(baseColor[3], 0.0F, 1.0F);
        }
        if (pbr != nullptr) {
            material.metallic = ReadUnitFactorUVE(*pbr, "metallicFactor", kDefaultMetallicUVE);
            material.roughness = ReadUnitFactorUVE(*pbr, "roughnessFactor", kDefaultRoughnessUVE);
            std::size_t textureIndex = 0U;
            if (ReadTextureInfoIndexUVE(*pbr, "baseColorTexture", textureIndex)) {
                material.albedoTexture = ResolveTextureUVE(textureIndex, TextureAssetColorSpaceUVE::Srgb,
                                                           TextureUsageUVE::Color, "baseColorTexture");
            }
            if (ReadTextureInfoIndexUVE(*pbr, "metallicRoughnessTexture", textureIndex)) {
                material.metallicRoughnessTexture = ResolveTextureUVE(
                    textureIndex, TextureAssetColorSpaceUVE::Linear, TextureUsageUVE::Data,
                    "metallicRoughnessTexture");
            }
        }

        std::size_t textureIndex = 0U;
        if (ReadTextureInfoIndexUVE(materialJson, "normalTexture", textureIndex)) {
            material.normalTexture = ResolveTextureUVE(textureIndex, TextureAssetColorSpaceUVE::Linear,
                                                       TextureUsageUVE::Normal, "normalTexture");
            float scale = kDefaultNormalScaleUVE;
            if (ReadJsonFloatUVE(materialJson.at("normalTexture"), "scale", scale) && std::isfinite(scale) &&
                scale >= 0.0F) {
                material.normalScale = scale;
            }
        }
        if (ReadTextureInfoIndexUVE(materialJson, "occlusionTexture", textureIndex)) {
            material.aoTexture = ResolveTextureUVE(textureIndex, TextureAssetColorSpaceUVE::Linear,
                                                   TextureUsageUVE::Data, "occlusionTexture");
            float strength = kDefaultOcclusionStrengthUVE;
            if (ReadJsonFloatUVE(materialJson.at("occlusionTexture"), "strength", strength) &&
                std::isfinite(strength)) {
                material.occlusionStrength = std::clamp(strength, 0.0F, 1.0F);
            }
        }
        if (ReadTextureInfoIndexUVE(materialJson, "emissiveTexture", textureIndex)) {
            material.emissiveTexture = ResolveTextureUVE(textureIndex, TextureAssetColorSpaceUVE::Srgb,
                                                         TextureUsageUVE::Color, "emissiveTexture");
        }
        std::vector<float> emissiveFactor;
        if (materialJson.contains("emissiveFactor") &&
            ReadFloatArrayUVE(materialJson.at("emissiveFactor"), 3U, emissiveFactor)) {
            material.emissiveColor = Math::ColorUVE{std::max(0.0F, emissiveFactor[0]),
                                                    std::max(0.0F, emissiveFactor[1]),
                                                    std::max(0.0F, emissiveFactor[2])};
        }

        if (materialJson.contains("alphaMode") && materialJson.at("alphaMode").is_string()) {
            const std::string alphaMode = materialJson.at("alphaMode").get<std::string>();
            if (alphaMode == "BLEND") {
                material.isTransparent = true;
                material.blendMode = MaterialBlendModeUVE::AlphaBlend;
                material.depthWriteEnabled = false;
            } else if (alphaMode == "MASK") {
                float cutoff = kDefaultAlphaCutoffUVE;
                if (!ReadJsonFloatUVE(materialJson, "alphaCutoff", cutoff) || !std::isfinite(cutoff)) {
                    cutoff = kDefaultAlphaCutoffUVE;
                }
                material.alphaCutoff = std::clamp(cutoff, 0.0F, 1.0F);
            }
        }
        bool doubleSided = false;
        if (materialJson.contains("doubleSided") && materialJson.at("doubleSided").is_boolean()) {
            doubleSided = materialJson.at("doubleSided").get<bool>();
        }
        material.cullMode = doubleSided ? MaterialCullModeUVE::Disabled : MaterialCullModeUVE::Back;
    }

    [[nodiscard]] AssetGuidUVE ResolveTextureUVE(const std::size_t textureIndex,
                                                 const TextureAssetColorSpaceUVE colorSpace,
                                                 const TextureUsageUVE usage, const char* slotName) {
        const nlohmann::json* const texture = CheckedElementUVE(m_document, "textures", textureIndex);
        if (texture == nullptr || !texture->contains("source")) {
            UVE_WARNING("{}: {} references texture {} which the document does not define with a source image; "
                        "the slot stays empty",
                        kGltfImporterNameUVE, slotName, textureIndex);
            return kInvalidAssetGuidUVE;
        }
        const auto source = ReadJsonUintUVE(*texture, "source", true);
        if (!source || *source > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            UVE_WARNING("{}: {} references texture {} whose source image index is not a bounded unsigned "
                        "integer; the slot stays empty",
                        kGltfImporterNameUVE, slotName, textureIndex);
            return kInvalidAssetGuidUVE;
        }
        return ResolveImageUVE(static_cast<std::size_t>(*source), colorSpace, usage, slotName);
    }

    [[nodiscard]] AssetGuidUVE ResolveImageUVE(const std::size_t imageIndex,
                                               const TextureAssetColorSpaceUVE colorSpace,
                                               const TextureUsageUVE usage, const char* slotName) {
        const auto cached = m_textureByImage.find(imageIndex);
        if (cached != m_textureByImage.end()) {
            return cached->second;
        }
        AssetGuidUVE guid = ImportImageUVE(imageIndex, colorSpace, usage, slotName);
        m_textureByImage.emplace(imageIndex, guid);
        return guid;
    }

    [[nodiscard]] AssetGuidUVE ImportImageUVE(const std::size_t imageIndex,
                                              const TextureAssetColorSpaceUVE colorSpace,
                                              const TextureUsageUVE usage, const char* slotName) {
        const nlohmann::json* const image = CheckedElementUVE(m_document, "images", imageIndex);
        if (image == nullptr) {
            UVE_WARNING("{}: {} references image {} which the document does not define; the slot stays empty",
                        kGltfImporterNameUVE, slotName, imageIndex);
            return kInvalidAssetGuidUVE;
        }
        std::string authoredName = "texture_" + std::to_string(imageIndex);
        if (image->contains("name") && image->at("name").is_string()) {
            const std::string named = image->at("name").get<std::string>();
            if (!named.empty()) {
                authoredName = named;
            }
        }
        const std::string textureStem = m_destinationPath.stem().string() + "." +
                                        MakeTextureFileStemUVE(authoredName, "texture", imageIndex, m_usedTextureStems);

        if (image->contains("bufferView")) {
            const auto viewIndex = ReadJsonUintUVE(*image, "bufferView", true);
            if (!viewIndex) {
                UVE_WARNING("{}: image {} has a bufferView index that is not a bounded unsigned integer; the "
                            "texture is skipped",
                            kGltfImporterNameUVE, imageIndex);
                return kInvalidAssetGuidUVE;
            }
            const auto view = ReadBufferViewUVE(m_document, *viewIndex);
            if (!view || view->buffer != 0U) {
                UVE_WARNING("{}: image {} references a bufferView outside the document's single binary buffer; "
                            "the texture is skipped",
                            kGltfImporterNameUVE, imageIndex);
                return kInvalidAssetGuidUVE;
            }
            if (view->byteLength > m_options.maximumImageBytes) {
                UVE_WARNING("{}: image {} declares {} bytes, above the bounded image limit; the texture is "
                            "skipped",
                            kGltfImporterNameUVE, imageIndex, view->byteLength);
                return kInvalidAssetGuidUVE;
            }
            std::uint64_t viewEnd = 0U;
            if (!AddU64UVE(view->byteOffset, view->byteLength, viewEnd) || viewEnd > m_buffer.size()) {
                UVE_WARNING("{}: image {} declares a bufferView past the end of the binary chunk; the texture "
                            "is skipped",
                            kGltfImporterNameUVE, imageIndex);
                return kInvalidAssetGuidUVE;
            }
            std::vector<std::byte> imageBytes(
                m_buffer.begin() + static_cast<std::ptrdiff_t>(view->byteOffset),
                m_buffer.begin() + static_cast<std::ptrdiff_t>(viewEnd));
            return ImportModelTextureBytesUVE(imageBytes, textureStem, m_destinationPath, colorSpace, usage,
                                              m_options, m_assetDatabase);
        }

        if (!image->contains("uri") || !image->at("uri").is_string()) {
            UVE_WARNING("{}: image {} has neither a bufferView nor a uri; the texture is skipped",
                        kGltfImporterNameUVE, imageIndex);
            return kInvalidAssetGuidUVE;
        }
        const std::string uri = image->at("uri").get<std::string>();
        switch (ClassifyGltfResourceUriUVE(uri)) {
        case GltfResourceUriKindUVE::DataUri: {
            std::vector<std::byte> imageBytes;
            if (!DecodeGltfDataUriUVE(uri, imageBytes, m_options.maximumImageBytes)) {
                UVE_WARNING("{}: image {} carries a data URI that failed bounded decoding; the texture is skipped",
                            kGltfImporterNameUVE, imageIndex);
                return kInvalidAssetGuidUVE;
            }
            return ImportModelTextureBytesUVE(imageBytes, textureStem, m_destinationPath, colorSpace, usage,
                                              m_options, m_assetDatabase);
        }
        case GltfResourceUriKindUVE::RelativePath:
            return ImportModelTextureFileUVE(m_sourceDirectory, m_destinationPath, uri, colorSpace, usage,
                                             m_options, m_assetDatabase);
        case GltfResourceUriKindUVE::Invalid:
            break;
        }
        UVE_WARNING("{}: image {} names a uri that is neither a bounded data URI nor a traversal-safe relative "
                    "path; the texture is skipped",
                    kGltfImporterNameUVE, imageIndex);
        return kInvalidAssetGuidUVE;
    }

    const nlohmann::json& m_document;
    const std::vector<std::byte>& m_buffer;
    std::filesystem::path m_sourceDirectory;
    std::filesystem::path m_destinationPath;
    IAssetDatabaseUVE& m_assetDatabase;
    ModelTextureImportOptionsUVE m_options;
    std::unordered_map<std::size_t, AssetGuidUVE> m_textureByImage;
    std::vector<std::string> m_usedMaterialStems;
    std::vector<std::string> m_usedTextureStems;
};

} // namespace

bool ImportGltfMaterialsUVE(const nlohmann::json& document, const std::vector<std::byte>& buffer,
                            const std::filesystem::path& sourcePath,
                            const std::filesystem::path& destinationPath, IAssetDatabaseUVE& assetDatabase,
                            GltfMaterialImportResultUVE& outResult) {
    outResult = GltfMaterialImportResultUVE{};
    if (!document.contains("materials")) {
        // A document with no materials is not an error, and neither is a primitive that names none:
        // the mesh is simply untextured, exactly as it imported before material support existed.
        return true;
    }
    try {
        GltfMaterialPublisherUVE publisher(document, buffer, sourcePath.parent_path(), destinationPath,
                                           assetDatabase);
        return publisher.PublishPrimitiveMaterialUVE(outResult);
    } catch (const std::bad_alloc&) {
        UVE_ERROR("{}: out of memory while importing glTF materials", kGltfImporterNameUVE);
        return false;
    }
}

} // namespace UVE::Asset::Detail
