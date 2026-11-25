// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "gltf_material_import.h"

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

#include "gltf_document.h"
#include "import_helpers.h"

#include "gltf_metadata.h"
#include "material_asset.h"
#include "logging_macros.h"

namespace UVE::Asset::Detail {
namespace {

constexpr const char* kGltfImporterName = "GltfImporter";
constexpr std::string_view kGltfTemporarySuffix = ".uve_gltf_tmp";

/// glTF's own defaults for every factor a material may omit. They are part of the specification,
/// not a guess: an exporter that writes only `baseColorTexture` still means a white, fully rough,
/// non-metallic surface.
constexpr float kDefaultMetallic = 1.0F;
constexpr float kDefaultRoughness = 1.0F;
constexpr float kDefaultNormalScale = 1.0F;
constexpr float kDefaultOcclusionStrength = 1.0F;
constexpr float kDefaultAlphaCutoff = 0.5F;

[[nodiscard]] bool ReadJsonFloat(const nlohmann::json& object, const char* key, float& outValue) {
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
[[nodiscard]] float ReadUnitFactor(const nlohmann::json& object, const char* key, const float fallback) {
    float value = 0.0F;
    if (!ReadJsonFloat(object, key, value)) {
        return fallback;
    }
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] bool ReadFloatArray(const nlohmann::json& array, const std::size_t expectedSize,
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
[[nodiscard]] bool ReadTextureInfoIndex(const nlohmann::json& owner, const char* key, std::size_t& outIndex) {
    if (!owner.contains(key) || !owner.at(key).is_object()) {
        return false;
    }
    const auto index = ReadJsonUint(owner.at(key), "index", true);
    if (!index || *index > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    outIndex = static_cast<std::size_t>(*index);
    return true;
}

/// Resolves one glTF element (material, texture, image) by index, returning null when the document
/// does not have it. Bounds come from the document itself, so a hostile file cannot make the
/// importer walk past an array end.
[[nodiscard]] const nlohmann::json* CheckedElement(const nlohmann::json& document, const char* arrayKey,
                                                      const std::size_t index) {
    if (!document.contains(arrayKey) || !document.at(arrayKey).is_array()) {
        return nullptr;
    }
    const auto checked = CheckedJsonArrayIndex(document.at(arrayKey), index);
    if (!checked || !document.at(arrayKey).at(*checked).is_object()) {
        return nullptr;
    }
    return &document.at(arrayKey).at(*checked);
}

/// Owns the per-import caches and naming for one document's material work, so a texture referenced
/// by two slots is published and registered once.
class GltfMaterialPublisher final {
public:
    GltfMaterialPublisher(const nlohmann::json& document, const std::vector<std::byte>& buffer,
                             std::filesystem::path sourceDirectory, std::filesystem::path destinationPath,
                             IAssetDatabase& assetDatabase)
        : m_document(document), m_buffer(buffer), m_sourceDirectory(std::move(sourceDirectory)),
          m_destinationPath(std::move(destinationPath)), m_assetDatabase(assetDatabase) {
        m_options.importerName = kGltfImporterName;
        m_options.temporarySuffix = kGltfTemporarySuffix;
    }

    /// Publishes every material the document's imported mesh references.
    ///
    /// The mesh's primitives are the authority on which materials exist for it: a document may
    /// describe twenty materials and use three, and publishing the unused seventeen would put
    /// files beside a model that nothing samples. Each distinct index is published once, in the
    /// order the primitives reference it, which is exactly the order that turns into the mesh's
    /// material slots - a surface stores the slot, not the glTF index, so the two have to agree by
    /// construction rather than by a later lookup.
    [[nodiscard]] bool PublishMeshMaterials(GltfMaterialImportResult& outResult) {
        if (!m_document.contains("meshes") || !m_document.at("meshes").is_array() ||
            m_document.at("meshes").empty()) {
            return true;
        }
        const nlohmann::json* const mesh = CheckedElement(m_document, "meshes", 0U);
        if (mesh == nullptr || !mesh->contains("primitives") || !mesh->at("primitives").is_array() ||
            mesh->at("primitives").empty()) {
            return true;
        }
        const nlohmann::json& primitives = mesh->at("primitives");
        for (std::size_t primitiveIndex = 0U; primitiveIndex < primitives.size(); ++primitiveIndex) {
            const nlohmann::json* const primitive = CheckedElement(*mesh, "primitives", primitiveIndex);
            if (primitive == nullptr || !primitive->contains("material")) {
                // No material on a primitive is a normal, valid glTF: that surface has no authored
                // look of its own and the object's material shades it.
                continue;
            }
            const auto materialIndex = ReadJsonUint(*primitive, "material", true);
            if (!materialIndex || *materialIndex > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
                UVE_WARNING("{}: primitive {} names a material index that is not a bounded unsigned "
                            "integer; that surface publishes without a material",
                            kGltfImporterName, primitiveIndex);
                continue;
            }
            if (const auto existing = outResult.materialIndexToSlot.find(*materialIndex);
                existing != outResult.materialIndexToSlot.end()) {
                if (primitiveIndex == 0U) {
                    outResult.primitiveMaterialGuid = outResult.materialGuids[existing->second];
                }
                continue;
            }
            AssetGuid publishedGuid = kInvalidAssetGuid;
            if (!TryPublishMaterialAt(*materialIndex, publishedGuid)) {
                return false;
            }
            if (publishedGuid == kInvalidAssetGuid) {
                continue; // Described badly and skipped; the surface falls back to the object's material.
            }
            const auto slot = static_cast<std::uint32_t>(outResult.materialGuids.size());
            outResult.materialGuids.push_back(publishedGuid);
            outResult.materialIndexToSlot.emplace(*materialIndex, slot);
            if (primitiveIndex == 0U) {
                outResult.primitiveMaterialGuid = publishedGuid;
            }
        }
        return true;
    }

private:
    /// Publishes one glTF material at `materialIndex`. Returns false only when the asset database
    /// refuses the published file; a material the document merely describes badly leaves
    /// `outGuid` invalid and returns true so the caller keeps importing the geometry.
    [[nodiscard]] bool TryPublishMaterialAt(const std::uint64_t materialIndex, AssetGuid& outGuid) {
        outGuid = kInvalidAssetGuid;
        const nlohmann::json* const materialJson =
            CheckedElement(m_document, "materials", static_cast<std::size_t>(materialIndex));
        if (materialJson == nullptr) {
            UVE_WARNING("{}: a primitive names material {} but the document has no such material; "
                        "that surface publishes without one",
                        kGltfImporterName, materialIndex);
            return true;
        }

        MaterialAsset material;
        PopulateMaterial(*materialJson, material);
        if (!IsMaterialAssetValid(material)) {
            UVE_WARNING("{}: material {} describes values the material validator rejects; that "
                        "surface publishes without a material",
                        kGltfImporterName, materialIndex);
            return true;
        }

        std::string authoredName = "material";
        if (materialJson->contains("name") && materialJson->at("name").is_string()) {
            authoredName = materialJson->at("name").get<std::string>();
        }
        const std::string modelStem = m_destinationPath.stem().string();
        const std::string stem = MakeMaterialFileStem(
            modelStem, authoredName, static_cast<std::size_t>(materialIndex), "material", m_usedMaterialStems);
        const std::filesystem::path materialPath = m_destinationPath.parent_path() / (stem + ".uvmat");
        if (!PublishAssetAtomically(
                materialPath, kGltfImporterName, kGltfTemporarySuffix,
                [&material](const std::filesystem::path& temporaryPath) {
                    return SaveMaterialAsset(material, temporaryPath);
                })) {
            UVE_ERROR("{}: failed to publish the imported material \"{}\"", kGltfImporterName,
                      materialPath.string());
            return false;
        }
        const AssetGuid guid = m_assetDatabase.Register(materialPath);
        if (guid == kInvalidAssetGuid) {
            UVE_ERROR("{}: asset database rejected imported material \"{}\"", kGltfImporterName,
                      materialPath.string());
            return false;
        }
        outGuid = guid;
        return true;
    }

    void PopulateMaterial(const nlohmann::json& materialJson, MaterialAsset& material) {
        const nlohmann::json* const pbr =
            materialJson.contains("pbrMetallicRoughness") && materialJson.at("pbrMetallicRoughness").is_object()
                ? &materialJson.at("pbrMetallicRoughness")
                : nullptr;

        // glTF factors are authored in linear space, which is exactly what Color stores; unlike
        // MTL's display-authored Kd these must NOT go through the display conversion.
        std::vector<float> baseColor;
        if (pbr != nullptr && pbr->contains("baseColorFactor") &&
            ReadFloatArray(pbr->at("baseColorFactor"), 4U, baseColor)) {
            material.albedoColor = Math::Color{std::clamp(baseColor[0], 0.0F, 1.0F),
                                                  std::clamp(baseColor[1], 0.0F, 1.0F),
                                                  std::clamp(baseColor[2], 0.0F, 1.0F)};
            material.albedoAlpha = std::clamp(baseColor[3], 0.0F, 1.0F);
        }
        if (pbr != nullptr) {
            material.metallic = ReadUnitFactor(*pbr, "metallicFactor", kDefaultMetallic);
            material.roughness = ReadUnitFactor(*pbr, "roughnessFactor", kDefaultRoughness);
            std::size_t textureIndex = 0U;
            if (ReadTextureInfoIndex(*pbr, "baseColorTexture", textureIndex)) {
                material.albedoTexture = ResolveTexture(textureIndex, TextureAssetColorSpace::Srgb,
                                                           TextureUsage::Color, "baseColorTexture");
            }
            if (ReadTextureInfoIndex(*pbr, "metallicRoughnessTexture", textureIndex)) {
                material.metallicRoughnessTexture = ResolveTexture(
                    textureIndex, TextureAssetColorSpace::Linear, TextureUsage::Data,
                    "metallicRoughnessTexture");
            }
        }

        std::size_t textureIndex = 0U;
        if (ReadTextureInfoIndex(materialJson, "normalTexture", textureIndex)) {
            material.normalTexture = ResolveTexture(textureIndex, TextureAssetColorSpace::Linear,
                                                       TextureUsage::Normal, "normalTexture");
            float scale = kDefaultNormalScale;
            if (ReadJsonFloat(materialJson.at("normalTexture"), "scale", scale) && std::isfinite(scale) &&
                scale >= 0.0F) {
                material.normalScale = scale;
            }
        }
        if (ReadTextureInfoIndex(materialJson, "occlusionTexture", textureIndex)) {
            material.aoTexture = ResolveTexture(textureIndex, TextureAssetColorSpace::Linear,
                                                   TextureUsage::Data, "occlusionTexture");
            float strength = kDefaultOcclusionStrength;
            if (ReadJsonFloat(materialJson.at("occlusionTexture"), "strength", strength) &&
                std::isfinite(strength)) {
                material.occlusionStrength = std::clamp(strength, 0.0F, 1.0F);
            }
        }
        if (ReadTextureInfoIndex(materialJson, "emissiveTexture", textureIndex)) {
            material.emissiveTexture = ResolveTexture(textureIndex, TextureAssetColorSpace::Srgb,
                                                         TextureUsage::Color, "emissiveTexture");
        }
        std::vector<float> emissiveFactor;
        if (materialJson.contains("emissiveFactor") &&
            ReadFloatArray(materialJson.at("emissiveFactor"), 3U, emissiveFactor)) {
            material.emissiveColor = Math::Color{std::max(0.0F, emissiveFactor[0]),
                                                    std::max(0.0F, emissiveFactor[1]),
                                                    std::max(0.0F, emissiveFactor[2])};
        }

        if (materialJson.contains("alphaMode") && materialJson.at("alphaMode").is_string()) {
            const std::string alphaMode = materialJson.at("alphaMode").get<std::string>();
            if (alphaMode == "BLEND") {
                material.isTransparent = true;
                material.blendMode = MaterialBlendMode::AlphaBlend;
                material.depthWriteEnabled = false;
            } else if (alphaMode == "MASK") {
                float cutoff = kDefaultAlphaCutoff;
                if (!ReadJsonFloat(materialJson, "alphaCutoff", cutoff) || !std::isfinite(cutoff)) {
                    cutoff = kDefaultAlphaCutoff;
                }
                material.alphaCutoff = std::clamp(cutoff, 0.0F, 1.0F);
            }
        }
        bool doubleSided = false;
        if (materialJson.contains("doubleSided") && materialJson.at("doubleSided").is_boolean()) {
            doubleSided = materialJson.at("doubleSided").get<bool>();
        }
        material.cullMode = doubleSided ? MaterialCullMode::Disabled : MaterialCullMode::Back;
    }

    [[nodiscard]] AssetGuid ResolveTexture(const std::size_t textureIndex,
                                                 const TextureAssetColorSpace colorSpace,
                                                 const TextureUsage usage, const char* slotName) {
        const nlohmann::json* const texture = CheckedElement(m_document, "textures", textureIndex);
        if (texture == nullptr || !texture->contains("source")) {
            UVE_WARNING("{}: {} references texture {} which the document does not define with a source image; "
                        "the slot stays empty",
                        kGltfImporterName, slotName, textureIndex);
            return kInvalidAssetGuid;
        }
        const auto source = ReadJsonUint(*texture, "source", true);
        if (!source || *source > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            UVE_WARNING("{}: {} references texture {} whose source image index is not a bounded unsigned "
                        "integer; the slot stays empty",
                        kGltfImporterName, slotName, textureIndex);
            return kInvalidAssetGuid;
        }
        return ResolveImage(static_cast<std::size_t>(*source), colorSpace, usage, slotName);
    }

    [[nodiscard]] AssetGuid ResolveImage(const std::size_t imageIndex,
                                               const TextureAssetColorSpace colorSpace,
                                               const TextureUsage usage, const char* slotName) {
        const auto cached = m_textureByImage.find(imageIndex);
        if (cached != m_textureByImage.end()) {
            return cached->second;
        }
        AssetGuid guid = ImportImage(imageIndex, colorSpace, usage, slotName);
        m_textureByImage.emplace(imageIndex, guid);
        return guid;
    }

    [[nodiscard]] AssetGuid ImportImage(const std::size_t imageIndex,
                                              const TextureAssetColorSpace colorSpace,
                                              const TextureUsage usage, const char* slotName) {
        const nlohmann::json* const image = CheckedElement(m_document, "images", imageIndex);
        if (image == nullptr) {
            UVE_WARNING("{}: {} references image {} which the document does not define; the slot stays empty",
                        kGltfImporterName, slotName, imageIndex);
            return kInvalidAssetGuid;
        }
        std::string authoredName = "texture_" + std::to_string(imageIndex);
        if (image->contains("name") && image->at("name").is_string()) {
            const std::string named = image->at("name").get<std::string>();
            if (!named.empty()) {
                authoredName = named;
            }
        }
        const std::string textureStem = m_destinationPath.stem().string() + "." +
                                        MakeTextureFileStem(authoredName, "texture", imageIndex, m_usedTextureStems);

        if (image->contains("bufferView")) {
            const auto viewIndex = ReadJsonUint(*image, "bufferView", true);
            if (!viewIndex) {
                UVE_WARNING("{}: image {} has a bufferView index that is not a bounded unsigned integer; the "
                            "texture is skipped",
                            kGltfImporterName, imageIndex);
                return kInvalidAssetGuid;
            }
            const auto view = ReadBufferView(m_document, *viewIndex);
            if (!view || view->buffer != 0U) {
                UVE_WARNING("{}: image {} references a bufferView outside the document's single binary buffer; "
                            "the texture is skipped",
                            kGltfImporterName, imageIndex);
                return kInvalidAssetGuid;
            }
            if (view->byteLength > m_options.maximumImageBytes) {
                UVE_WARNING("{}: image {} declares {} bytes, above the bounded image limit; the texture is "
                            "skipped",
                            kGltfImporterName, imageIndex, view->byteLength);
                return kInvalidAssetGuid;
            }
            std::uint64_t viewEnd = 0U;
            if (!AddU64(view->byteOffset, view->byteLength, viewEnd) || viewEnd > m_buffer.size()) {
                UVE_WARNING("{}: image {} declares a bufferView past the end of the binary chunk; the texture "
                            "is skipped",
                            kGltfImporterName, imageIndex);
                return kInvalidAssetGuid;
            }
            std::vector<std::byte> imageBytes(
                m_buffer.begin() + static_cast<std::ptrdiff_t>(view->byteOffset),
                m_buffer.begin() + static_cast<std::ptrdiff_t>(viewEnd));
            return ImportModelTextureBytes(imageBytes, textureStem, m_destinationPath, colorSpace, usage,
                                              m_options, m_assetDatabase);
        }

        if (!image->contains("uri") || !image->at("uri").is_string()) {
            UVE_WARNING("{}: image {} has neither a bufferView nor a uri; the texture is skipped",
                        kGltfImporterName, imageIndex);
            return kInvalidAssetGuid;
        }
        const std::string uri = image->at("uri").get<std::string>();
        switch (ClassifyGltfResourceUri(uri)) {
        case GltfResourceUriKind::DataUri: {
            std::vector<std::byte> imageBytes;
            if (!DecodeGltfDataUri(uri, imageBytes, m_options.maximumImageBytes)) {
                UVE_WARNING("{}: image {} carries a data URI that failed bounded decoding; the texture is skipped",
                            kGltfImporterName, imageIndex);
                return kInvalidAssetGuid;
            }
            return ImportModelTextureBytes(imageBytes, textureStem, m_destinationPath, colorSpace, usage,
                                              m_options, m_assetDatabase);
        }
        case GltfResourceUriKind::RelativePath:
            return ImportModelTextureFile(m_sourceDirectory, m_destinationPath, uri, colorSpace, usage,
                                             m_options, m_assetDatabase);
        case GltfResourceUriKind::Invalid:
            break;
        }
        UVE_WARNING("{}: image {} names a uri that is neither a bounded data URI nor a traversal-safe relative "
                    "path; the texture is skipped",
                    kGltfImporterName, imageIndex);
        return kInvalidAssetGuid;
    }

    const nlohmann::json& m_document;
    const std::vector<std::byte>& m_buffer;
    std::filesystem::path m_sourceDirectory;
    std::filesystem::path m_destinationPath;
    IAssetDatabase& m_assetDatabase;
    ModelTextureImportOptions m_options;
    std::unordered_map<std::size_t, AssetGuid> m_textureByImage;
    std::vector<std::string> m_usedMaterialStems;
    std::vector<std::string> m_usedTextureStems;
};

} // namespace

bool ImportGltfMaterials(const nlohmann::json& document, const std::vector<std::byte>& buffer,
                            const std::filesystem::path& sourcePath,
                            const std::filesystem::path& destinationPath, IAssetDatabase& assetDatabase,
                            GltfMaterialImportResult& outResult) {
    outResult = GltfMaterialImportResult{};
    if (!document.contains("materials")) {
        // A document with no materials is not an error, and neither is a primitive that names none:
        // the mesh is simply untextured, exactly as it imported before material support existed.
        return true;
    }
    try {
        GltfMaterialPublisher publisher(document, buffer, sourcePath.parent_path(), destinationPath,
                                           assetDatabase);
        return publisher.PublishMeshMaterials(outResult);
    } catch (const std::bad_alloc&) {
        UVE_ERROR("{}: out of memory while importing glTF materials", kGltfImporterName);
        return false;
    }
}

} // namespace UVE::Asset::Detail
