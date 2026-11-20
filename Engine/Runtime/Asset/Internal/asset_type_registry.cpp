// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "asset_type_registry.h"

#include <array>
#include <cctype>

namespace UVE::Asset {
namespace {

struct EnvelopeExtensionEntryUVE final {
    std::string_view normalizedExtension;
    AssetKindUVE assetKind;
    AssetImportSourceKindUVE importSourceKind;
    bool cookableContent;
};

constexpr std::array<EnvelopeExtensionEntryUVE, 21> kEnvelopeExtensionTableUVE{{
    {"uvscene", AssetKindUVE::Scene, AssetImportSourceKindUVE::SceneEnvelope, true},
    {"uvprefab", AssetKindUVE::Prefab, AssetImportSourceKindUVE::PrefabEnvelope, true},
    {"uventity", AssetKindUVE::Prefab, AssetImportSourceKindUVE::PrefabEnvelope, true},
    {"uvwbp", AssetKindUVE::Prefab, AssetImportSourceKindUVE::PrefabEnvelope, true},
    {"uvbundle", AssetKindUVE::Bundle, AssetImportSourceKindUVE::Unknown, false},
    {"uvmodel", AssetKindUVE::Mesh, AssetImportSourceKindUVE::MeshEnvelope, true},
    {"uvtex", AssetKindUVE::Texture, AssetImportSourceKindUVE::TextureEnvelope, true},
    {"uvshader", AssetKindUVE::Shader, AssetImportSourceKindUVE::ShaderEnvelope, true},
    {"uvmat", AssetKindUVE::Material, AssetImportSourceKindUVE::MaterialEnvelope, true},
    {"uvmaterial", AssetKindUVE::Material, AssetImportSourceKindUVE::MaterialEnvelope, true},
    {"uvsave", AssetKindUVE::Save, AssetImportSourceKindUVE::Unknown, false},
    {"uvtable", AssetKindUVE::DataTable, AssetImportSourceKindUVE::DataTableEnvelope, true},
    {"uvaudio", AssetKindUVE::Audio, AssetImportSourceKindUVE::AudioEnvelope, true},
    {"uvanim", AssetKindUVE::Animation, AssetImportSourceKindUVE::AnimationEnvelope, true},
    {"uvskel", AssetKindUVE::Skeleton, AssetImportSourceKindUVE::SkeletonEnvelope, true},
    {"uvanimlib", AssetKindUVE::AnimationLibrary, AssetImportSourceKindUVE::AnimationLibraryEnvelope, true},
    {"uvfont", AssetKindUVE::Font, AssetImportSourceKindUVE::FontEnvelope, true},
    {"uvmontage", AssetKindUVE::MotionMontage, AssetImportSourceKindUVE::MotionMontageEnvelope, true},
    {"uvmontagelib", AssetKindUVE::MontageLibrary, AssetImportSourceKindUVE::MontageLibraryEnvelope, true},
    {"uvbp", AssetKindUVE::Blueprint, AssetImportSourceKindUVE::BlueprintEnvelope, true},
    {"uvfx", AssetKindUVE::ParticleSystem, AssetImportSourceKindUVE::ParticleSystemEnvelope, true},
}};

constexpr std::array<std::string_view, 19> kBuiltInGenericEnvelopeExtensionsUVE{{
    "uvscene",
    "uvprefab",
    "uventity",
    "uvwbp",
    "uvmodel",
    "uvtex",
    "uvshader",
    "uvmat",
    "uvmaterial",
    "uvtable",
    "uvaudio",
    "uvanim",
    "uvskel",
    "uvanimlib",
    "uvfont",
    "uvmontage",
    "uvmontagelib",
    "uvbp",
    "uvfx",
}};

constexpr std::array<std::string_view, 6> kCookableRuntimeTextExtensionsUVE{{
    "uvs",
    "glsl",
    "vert",
    "frag",
    "comp",
    "glslinc",
}};

[[nodiscard]] const EnvelopeExtensionEntryUVE* FindEnvelopeExtensionEntryUVE(
    const std::string_view normalizedExtension) noexcept {
    for (const EnvelopeExtensionEntryUVE& entry : kEnvelopeExtensionTableUVE) {
        if (entry.normalizedExtension == normalizedExtension) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

std::string NormalizeAssetExtensionUVE(const std::string_view extension) {
    std::string_view trimmed = extension;
    if (!trimmed.empty() && trimmed.front() == '.') {
        trimmed.remove_prefix(1U);
    }
    std::string normalized;
    normalized.reserve(trimmed.size());
    for (const char character : trimmed) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return normalized;
}

std::string NormalizeAssetPathExtensionUVE(const std::filesystem::path& path) {
    return NormalizeAssetExtensionUVE(path.extension().string());
}

std::optional<AssetKindUVE> ExpectedAssetKindForExtensionUVE(const std::string_view extension) {
    const std::string normalized = NormalizeAssetExtensionUVE(extension);
    if (const EnvelopeExtensionEntryUVE* const entry = FindEnvelopeExtensionEntryUVE(normalized); entry != nullptr) {
        return entry->assetKind;
    }
    return std::nullopt;
}

std::optional<AssetKindUVE> ExpectedAssetKindForPathUVE(const std::filesystem::path& path) {
    return ExpectedAssetKindForExtensionUVE(path.extension().string());
}

bool IsCookableNativeEnvelopeExtensionUVE(const std::string_view extension) {
    const std::string normalized = NormalizeAssetExtensionUVE(extension);
    if (const EnvelopeExtensionEntryUVE* const entry = FindEnvelopeExtensionEntryUVE(normalized); entry != nullptr) {
        return entry->cookableContent;
    }
    return false;
}

bool IsCookableRuntimeTextExtensionUVE(const std::string_view extension) {
    const std::string normalized = NormalizeAssetExtensionUVE(extension);
    for (const std::string_view candidate : kCookableRuntimeTextExtensionsUVE) {
        if (candidate == normalized) {
            return true;
        }
    }
    return false;
}

bool IsCookableRuntimeExtensionUVE(const std::string_view extension) {
    return IsCookableNativeEnvelopeExtensionUVE(extension) || IsCookableRuntimeTextExtensionUVE(extension);
}

std::span<const std::string_view> GetBuiltInEnvelopeExtensionsUVE() noexcept {
    return kBuiltInGenericEnvelopeExtensionsUVE;
}

AssetImportSourceKindUVE ClassifyImportSourceKindForExtensionUVE(const std::string_view extension) noexcept {
    const std::string normalized = NormalizeAssetExtensionUVE(extension);
    if (normalized == "txt") {
        return AssetImportSourceKindUVE::PlainText;
    }
    if (const EnvelopeExtensionEntryUVE* const entry = FindEnvelopeExtensionEntryUVE(normalized);
        entry != nullptr && entry->importSourceKind != AssetImportSourceKindUVE::Unknown) {
        return entry->importSourceKind;
    }
    if (normalized == "vert" || normalized == "frag" || normalized == "comp") {
        return AssetImportSourceKindUVE::RawShader;
    }
    if (normalized == "fbx" || normalized == "obj" || normalized == "gltf" || normalized == "glb" ||
        normalized == "dae") {
        return AssetImportSourceKindUVE::RawModel;
    }
    if (normalized == "png" || normalized == "jpg" || normalized == "jpeg" || normalized == "tga" ||
        normalized == "bmp" || normalized == "webp" || normalized == "hdr" || normalized == "exr") {
        return AssetImportSourceKindUVE::RawTexture;
    }
    if (normalized == "material" || normalized == "mat" || normalized == "mtl") {
        return AssetImportSourceKindUVE::RawMaterial;
    }
    if (normalized == "wav" || normalized == "ogg" || normalized == "mp3" || normalized == "flac" ||
        normalized == "aac" || normalized == "m4a" || normalized == "wma") {
        return AssetImportSourceKindUVE::RawAudio;
    }
    if (normalized == "ttf" || normalized == "otf") {
        return AssetImportSourceKindUVE::RawFont;
    }
    return AssetImportSourceKindUVE::Unknown;
}

std::string_view AssetImportSourceKindLabelUVE(const AssetImportSourceKindUVE kind) noexcept {
    switch (kind) {
        case AssetImportSourceKindUVE::PlainText:
            return "plainText";
        case AssetImportSourceKindUVE::SceneEnvelope:
            return "sceneEnvelope";
        case AssetImportSourceKindUVE::PrefabEnvelope:
            return "prefabEnvelope";
        case AssetImportSourceKindUVE::MeshEnvelope:
            return "meshEnvelope";
        case AssetImportSourceKindUVE::TextureEnvelope:
            return "textureEnvelope";
        case AssetImportSourceKindUVE::ShaderEnvelope:
            return "shaderEnvelope";
        case AssetImportSourceKindUVE::MaterialEnvelope:
            return "materialEnvelope";
        case AssetImportSourceKindUVE::AnimationEnvelope:
            return "animationEnvelope";
        case AssetImportSourceKindUVE::SkeletonEnvelope:
            return "skeletonEnvelope";
        case AssetImportSourceKindUVE::AnimationLibraryEnvelope:
            return "animationLibraryEnvelope";
        case AssetImportSourceKindUVE::FontEnvelope:
            return "fontEnvelope";
        case AssetImportSourceKindUVE::MotionMontageEnvelope:
            return "motionMontageEnvelope";
        case AssetImportSourceKindUVE::MontageLibraryEnvelope:
            return "montageLibraryEnvelope";
        case AssetImportSourceKindUVE::BlueprintEnvelope:
            return "blueprintEnvelope";
        case AssetImportSourceKindUVE::ParticleSystemEnvelope:
            return "particleSystemEnvelope";
        case AssetImportSourceKindUVE::DataTableEnvelope:
            return "dataTableEnvelope";
        case AssetImportSourceKindUVE::AudioEnvelope:
            return "audioEnvelope";
        case AssetImportSourceKindUVE::RawModel:
            return "rawModel";
        case AssetImportSourceKindUVE::RawTexture:
            return "rawTexture";
        case AssetImportSourceKindUVE::RawMaterial:
            return "rawMaterial";
        case AssetImportSourceKindUVE::RawShader:
            return "rawShader";
        case AssetImportSourceKindUVE::RawAudio:
            return "rawAudio";
        case AssetImportSourceKindUVE::RawFont:
            return "rawFont";
        case AssetImportSourceKindUVE::Unknown:
            return "unknown";
    }
    return "unknown";
}

} // namespace UVE::Asset
