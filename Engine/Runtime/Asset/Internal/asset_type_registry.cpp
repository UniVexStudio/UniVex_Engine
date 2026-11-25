// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "asset_type_registry.h"

#include <array>
#include <cctype>

namespace UVE::Asset {
namespace {

struct EnvelopeExtensionEntry final {
    std::string_view normalizedExtension;
    AssetKind assetKind;
    AssetImportSourceKind importSourceKind;
    bool cookableContent;
};

constexpr std::array<EnvelopeExtensionEntry, 21> kEnvelopeExtensionTable{{
    {"uvscene", AssetKind::Scene, AssetImportSourceKind::SceneEnvelope, true},
    {"uvprefab", AssetKind::Prefab, AssetImportSourceKind::PrefabEnvelope, true},
    {"uventity", AssetKind::Prefab, AssetImportSourceKind::PrefabEnvelope, true},
    {"uvwbp", AssetKind::Prefab, AssetImportSourceKind::PrefabEnvelope, true},
    {"uvbundle", AssetKind::Bundle, AssetImportSourceKind::Unknown, false},
    {"uvmodel", AssetKind::Mesh, AssetImportSourceKind::MeshEnvelope, true},
    {"uvtex", AssetKind::Texture, AssetImportSourceKind::TextureEnvelope, true},
    {"uvshader", AssetKind::Shader, AssetImportSourceKind::ShaderEnvelope, true},
    {"uvmat", AssetKind::Material, AssetImportSourceKind::MaterialEnvelope, true},
    {"uvmaterial", AssetKind::Material, AssetImportSourceKind::MaterialEnvelope, true},
    {"uvsave", AssetKind::Save, AssetImportSourceKind::Unknown, false},
    {"uvtable", AssetKind::DataTable, AssetImportSourceKind::DataTableEnvelope, true},
    {"uvaudio", AssetKind::Audio, AssetImportSourceKind::AudioEnvelope, true},
    {"uvanim", AssetKind::Animation, AssetImportSourceKind::AnimationEnvelope, true},
    {"uvskel", AssetKind::Skeleton, AssetImportSourceKind::SkeletonEnvelope, true},
    {"uvanimlib", AssetKind::AnimationLibrary, AssetImportSourceKind::AnimationLibraryEnvelope, true},
    {"uvfont", AssetKind::Font, AssetImportSourceKind::FontEnvelope, true},
    {"uvmontage", AssetKind::MotionMontage, AssetImportSourceKind::MotionMontageEnvelope, true},
    {"uvmontagelib", AssetKind::MontageLibrary, AssetImportSourceKind::MontageLibraryEnvelope, true},
    {"uvbp", AssetKind::Blueprint, AssetImportSourceKind::BlueprintEnvelope, true},
    {"uvfx", AssetKind::ParticleSystem, AssetImportSourceKind::ParticleSystemEnvelope, true},
}};

constexpr std::array<std::string_view, 19> kBuiltInGenericEnvelopeExtensions{{
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

constexpr std::array<std::string_view, 6> kCookableRuntimeTextExtensions{{
    "uvs",
    "glsl",
    "vert",
    "frag",
    "comp",
    "glslinc",
}};

[[nodiscard]] const EnvelopeExtensionEntry* FindEnvelopeExtensionEntry(
    const std::string_view normalizedExtension) noexcept {
    for (const EnvelopeExtensionEntry& entry : kEnvelopeExtensionTable) {
        if (entry.normalizedExtension == normalizedExtension) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

std::string NormalizeAssetExtension(const std::string_view extension) {
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

std::string NormalizeAssetPathExtension(const std::filesystem::path& path) {
    return NormalizeAssetExtension(path.extension().string());
}

std::optional<AssetKind> ExpectedAssetKindForExtension(const std::string_view extension) {
    const std::string normalized = NormalizeAssetExtension(extension);
    if (const EnvelopeExtensionEntry* const entry = FindEnvelopeExtensionEntry(normalized); entry != nullptr) {
        return entry->assetKind;
    }
    return std::nullopt;
}

std::optional<AssetKind> ExpectedAssetKindForPath(const std::filesystem::path& path) {
    return ExpectedAssetKindForExtension(path.extension().string());
}

bool IsCookableNativeEnvelopeExtension(const std::string_view extension) {
    const std::string normalized = NormalizeAssetExtension(extension);
    if (const EnvelopeExtensionEntry* const entry = FindEnvelopeExtensionEntry(normalized); entry != nullptr) {
        return entry->cookableContent;
    }
    return false;
}

bool IsCookableRuntimeTextExtension(const std::string_view extension) {
    const std::string normalized = NormalizeAssetExtension(extension);
    for (const std::string_view candidate : kCookableRuntimeTextExtensions) {
        if (candidate == normalized) {
            return true;
        }
    }
    return false;
}

bool IsCookableRuntimeExtension(const std::string_view extension) {
    return IsCookableNativeEnvelopeExtension(extension) || IsCookableRuntimeTextExtension(extension);
}

std::span<const std::string_view> GetBuiltInEnvelopeExtensions() noexcept {
    return kBuiltInGenericEnvelopeExtensions;
}

AssetImportSourceKind ClassifyImportSourceKindForExtension(const std::string_view extension) noexcept {
    const std::string normalized = NormalizeAssetExtension(extension);
    if (normalized == "txt") {
        return AssetImportSourceKind::PlainText;
    }
    if (const EnvelopeExtensionEntry* const entry = FindEnvelopeExtensionEntry(normalized);
        entry != nullptr && entry->importSourceKind != AssetImportSourceKind::Unknown) {
        return entry->importSourceKind;
    }
    if (normalized == "vert" || normalized == "frag" || normalized == "comp") {
        return AssetImportSourceKind::RawShader;
    }
    if (normalized == "fbx" || normalized == "obj" || normalized == "gltf" || normalized == "glb" ||
        normalized == "dae") {
        return AssetImportSourceKind::RawModel;
    }
    if (normalized == "png" || normalized == "jpg" || normalized == "jpeg" || normalized == "tga" ||
        normalized == "bmp" || normalized == "webp" || normalized == "hdr" || normalized == "exr") {
        return AssetImportSourceKind::RawTexture;
    }
    if (normalized == "material" || normalized == "mat" || normalized == "mtl") {
        return AssetImportSourceKind::RawMaterial;
    }
    if (normalized == "wav" || normalized == "ogg" || normalized == "mp3" || normalized == "flac" ||
        normalized == "aac" || normalized == "m4a" || normalized == "wma") {
        return AssetImportSourceKind::RawAudio;
    }
    if (normalized == "ttf" || normalized == "otf") {
        return AssetImportSourceKind::RawFont;
    }
    return AssetImportSourceKind::Unknown;
}

std::string_view AssetImportSourceKindLabel(const AssetImportSourceKind kind) noexcept {
    switch (kind) {
        case AssetImportSourceKind::PlainText:
            return "plainText";
        case AssetImportSourceKind::SceneEnvelope:
            return "sceneEnvelope";
        case AssetImportSourceKind::PrefabEnvelope:
            return "prefabEnvelope";
        case AssetImportSourceKind::MeshEnvelope:
            return "meshEnvelope";
        case AssetImportSourceKind::TextureEnvelope:
            return "textureEnvelope";
        case AssetImportSourceKind::ShaderEnvelope:
            return "shaderEnvelope";
        case AssetImportSourceKind::MaterialEnvelope:
            return "materialEnvelope";
        case AssetImportSourceKind::AnimationEnvelope:
            return "animationEnvelope";
        case AssetImportSourceKind::SkeletonEnvelope:
            return "skeletonEnvelope";
        case AssetImportSourceKind::AnimationLibraryEnvelope:
            return "animationLibraryEnvelope";
        case AssetImportSourceKind::FontEnvelope:
            return "fontEnvelope";
        case AssetImportSourceKind::MotionMontageEnvelope:
            return "motionMontageEnvelope";
        case AssetImportSourceKind::MontageLibraryEnvelope:
            return "montageLibraryEnvelope";
        case AssetImportSourceKind::BlueprintEnvelope:
            return "blueprintEnvelope";
        case AssetImportSourceKind::ParticleSystemEnvelope:
            return "particleSystemEnvelope";
        case AssetImportSourceKind::DataTableEnvelope:
            return "dataTableEnvelope";
        case AssetImportSourceKind::AudioEnvelope:
            return "audioEnvelope";
        case AssetImportSourceKind::RawModel:
            return "rawModel";
        case AssetImportSourceKind::RawTexture:
            return "rawTexture";
        case AssetImportSourceKind::RawMaterial:
            return "rawMaterial";
        case AssetImportSourceKind::RawShader:
            return "rawShader";
        case AssetImportSourceKind::RawAudio:
            return "rawAudio";
        case AssetImportSourceKind::RawFont:
            return "rawFont";
        case AssetImportSourceKind::Unknown:
            return "unknown";
    }
    return "unknown";
}

} // namespace UVE::Asset
