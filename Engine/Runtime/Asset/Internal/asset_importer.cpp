// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "asset_importer.h"
#include "asset_type_registry.h"
#include "bmp_importer.h"
#include "gltf_importer.h"
#if !defined(__ANDROID__)
#include "jpeg_importer.h"
#endif
#include "mtl_importer.h"
#include "fbx_importer.h"
#include "obj_importer.h"
#include "shader_source_importer.h"

#include "png_importer.h"
#include "tga_importer.h"

#include <cctype>
#include <exception>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>
#include <unordered_map>
#include <utility>

#include "logging_macros.h"

namespace UVE::Asset {

namespace {

using ImportFuncUVE = std::function<bool(const std::filesystem::path&, const std::filesystem::path&,
                                          const AssetImportSettingsUVE&)>;

[[nodiscard]] std::string NormalizeExtensionUVE(std::string extension) {
    return NormalizeAssetExtensionUVE(extension);
}

/// The generic envelope importer: copies `source` to `destination` verbatim, applying no
/// format-specific transformation. Format-specific source parsers are registered separately.
[[nodiscard]] bool GenericFileImportUVE(const std::filesystem::path& source,
                                         const std::filesystem::path& destination,
                                         const AssetImportSettingsUVE& /*settings*/) {
    std::error_code errorCode;
    if (const std::filesystem::path parent = destination.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, errorCode);
        if (errorCode) {
            UVE_ERROR("AssetImporterUVE: failed to create destination directory \"{}\": {}", parent.string(),
                      errorCode.message());
            return false;
        }
    }
    const std::filesystem::path temporaryPath = destination.string() + ".uve_generic_tmp";
    std::filesystem::remove(temporaryPath, errorCode);
    if (!std::filesystem::copy_file(source, temporaryPath, std::filesystem::copy_options::overwrite_existing,
                                    errorCode)) {
        UVE_ERROR("AssetImporterUVE: failed to copy \"{}\" to temporary destination \"{}\": {}", source.string(),
                   temporaryPath.string(), errorCode.message());
        std::filesystem::remove(temporaryPath, errorCode);
        return false;
    }
    std::filesystem::rename(temporaryPath, destination, errorCode);
    if (errorCode) {
        UVE_ERROR("AssetImporterUVE: failed to publish \"{}\" to \"{}\": {}", source.string(),
                   destination.string(), errorCode.message());
        std::filesystem::remove(temporaryPath, errorCode);
        return false;
    }
    return true;
}

[[nodiscard]] bool TextFileImportUVE(const std::filesystem::path& source,
                                     const std::filesystem::path& destination,
                                     const AssetImportSettingsUVE& baseSettings) {
    std::error_code sourceSizeError;
    const std::uintmax_t sourceSize = std::filesystem::file_size(source, sourceSizeError);
    if (sourceSizeError || sourceSize > kMaximumTextImportBytesUVE) {
        UVE_ERROR("AssetImporterUVE: text source exceeds the {}-byte cap or cannot be sized \"{}\"",
                  kMaximumTextImportBytesUVE, source.string());
        return false;
    }
    std::ifstream input(source, std::ios::binary);
    if (!input.is_open()) {
        UVE_ERROR("AssetImporterUVE: failed to open text source \"{}\"", source.string());
        return false;
    }
    std::string sourceText{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (sourceText.size() > kMaximumTextImportBytesUVE) {
        UVE_ERROR("AssetImporterUVE: text source grew beyond the {}-byte cap \"{}\"",
                  kMaximumTextImportBytesUVE, source.string());
        return false;
    }
    if (sourceText.contains('\0')) {
        UVE_ERROR("AssetImporterUVE: text source contains a NUL byte \"{}\"", source.string());
        return false;
    }

    const auto* const textSettings = dynamic_cast<const TextImportSettingsUVE*>(&baseSettings);
    const TextImportLineEndingUVE lineEnding =
        textSettings == nullptr ? TextImportLineEndingUVE::Preserve : textSettings->lineEnding;
    const bool ensureTrailingLineEnding = textSettings != nullptr && textSettings->ensureTrailingLineEnding;
    const std::string lineEndingBytes =
        lineEnding == TextImportLineEndingUVE::CarriageReturnLineFeed ? "\r\n" : "\n";
    std::string normalized;
    normalized.reserve(sourceText.size() + 1U);
    if (lineEnding == TextImportLineEndingUVE::Preserve) {
        normalized = sourceText;
    } else {
        for (std::size_t index = 0U; index < sourceText.size(); ++index) {
            if (sourceText[index] == '\r') {
                if (index + 1U < sourceText.size() && sourceText[index + 1U] == '\n') {
                    ++index;
                }
                normalized += lineEndingBytes;
            } else if (sourceText[index] == '\n') {
                normalized += lineEndingBytes;
            } else {
                normalized.push_back(sourceText[index]);
            }
        }
    }
    if (ensureTrailingLineEnding && (normalized.empty() || normalized.back() != '\n')) {
        normalized += lineEnding == TextImportLineEndingUVE::CarriageReturnLineFeed ? "\r\n" : "\n";
    }

    std::error_code errorCode;
    if (const std::filesystem::path parent = destination.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, errorCode);
        if (errorCode) {
            UVE_ERROR("AssetImporterUVE: failed to create text destination directory \"{}\": {}",
                      parent.string(), errorCode.message());
            return false;
        }
    }
    const std::filesystem::path temporaryPath = destination.string() + ".uve_text_tmp";
    std::filesystem::remove(temporaryPath, errorCode);
    std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        UVE_ERROR("AssetImporterUVE: failed to open text temporary destination \"{}\"",
                  temporaryPath.string());
        return false;
    }
    output.write(normalized.data(), static_cast<std::streamsize>(normalized.size()));
    output.close();
    if (!output) {
        UVE_ERROR("AssetImporterUVE: failed to write text destination \"{}\"", destination.string());
        std::filesystem::remove(temporaryPath, errorCode);
        return false;
    }
    std::filesystem::rename(temporaryPath, destination, errorCode);
    if (errorCode) {
        UVE_ERROR("AssetImporterUVE: failed to publish text destination \"{}\": {}",
                  destination.string(), errorCode.message());
        std::filesystem::remove(temporaryPath, errorCode);
        return false;
    }
    return true;
}

[[nodiscard]] AssetImportSourceKindUVE ClassifySourceKindUVE(const std::string& extension) {
    return ClassifyImportSourceKindForExtensionUVE(extension);
}

[[nodiscard]] bool RequiresFormatSpecificParserUVE(const AssetImportSourceKindUVE kind) {
    return kind == AssetImportSourceKindUVE::RawModel || kind == AssetImportSourceKindUVE::RawTexture ||
           kind == AssetImportSourceKindUVE::RawMaterial || kind == AssetImportSourceKindUVE::RawShader ||
           kind == AssetImportSourceKindUVE::RawAudio || kind == AssetImportSourceKindUVE::RawFont;
}

} // namespace

struct AssetImporterUVE::ImplUVE {
    mutable std::mutex mutex;
    std::unordered_map<std::string, ImportFuncUVE> importers;
    /// WP7: importers that need the asset database (the FBX importer). Shares the key space with
    /// `importers`: registering either kind erases the other for that extension.
    std::unordered_map<std::string, IAssetImporterUVE::DatabaseAwareImportFuncUVE> databaseAwareImporters;
};

AssetImporterUVE::AssetImporterUVE() : m_impl(std::make_unique<ImplUVE>()) {
    RegisterImporterUVE("txt", &TextFileImportUVE);
    for (const std::string_view envelopeExtension : GetBuiltInEnvelopeExtensionsUVE()) {
        RegisterImporterUVE(std::string(envelopeExtension), &GenericFileImportUVE);
    }
    RegisterBmpImporterUVE(*this);
    RegisterPngImporterUVE(*this);
    RegisterTgaImporterUVE(*this);
    RegisterObjImporterUVE(*this);
    RegisterFbxImporterUVE(*this);
    RegisterGltfImporterUVE(*this);
#if !defined(__ANDROID__)
    RegisterJpegImporterUVE(*this);
#endif
    RegisterMtlImporterUVE(*this);
    RegisterShaderSourceImporterUVE(*this);
}

AssetImporterUVE::~AssetImporterUVE() = default;

std::string TextImportSettingsUVE::GetCacheVersionUVE() const {
    const char* const lineEndingName = lineEnding == TextImportLineEndingUVE::Preserve
                                           ? "preserve"
                                           : lineEnding == TextImportLineEndingUVE::LineFeed ? "lf" : "crlf";
    return std::string{"text-v1;line-ending="} + lineEndingName +
           ";trailing-newline=" + (ensureTrailingLineEnding ? "true" : "false");
}

void AssetImporterUVE::RegisterImporterUVE(
    std::string sourceExtension,
    std::function<bool(const std::filesystem::path&, const std::filesystem::path&,
                        const AssetImportSettingsUVE&)>
        importFunc) {
    std::string normalizedExtension = NormalizeExtensionUVE(std::move(sourceExtension));
    if (normalizedExtension.empty() || !importFunc) {
        UVE_ERROR("AssetImporterUVE: rejected importer registration with an empty extension or callback");
        return;
    }
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->databaseAwareImporters.erase(normalizedExtension);
    m_impl->importers[std::move(normalizedExtension)] = std::move(importFunc);
}

void AssetImporterUVE::RegisterDatabaseAwareImporterUVE(std::string sourceExtension,
                                                        DatabaseAwareImportFuncUVE importFunc) {
    std::string normalizedExtension = NormalizeExtensionUVE(std::move(sourceExtension));
    if (normalizedExtension.empty() || !importFunc) {
        UVE_ERROR("AssetImporterUVE: rejected database-aware importer registration with an empty extension or callback");
        return;
    }
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->importers.erase(normalizedExtension);
    m_impl->databaseAwareImporters[std::move(normalizedExtension)] = std::move(importFunc);
}

AssetImportSourceClassificationUVE AssetImporterUVE::ClassifySourceUVE(
    const std::filesystem::path& sourcePath) const {
    AssetImportSourceClassificationUVE classification;
    classification.normalizedExtension = NormalizeExtensionUVE(sourcePath.extension().string());
    classification.kind = ClassifySourceKindUVE(classification.normalizedExtension);
    classification.requiresFormatSpecificParser = RequiresFormatSpecificParserUVE(classification.kind);
    {
        const std::lock_guard<std::mutex> lock(m_impl->mutex);
        classification.importerRegistered =
            m_impl->importers.find(classification.normalizedExtension) != m_impl->importers.end() ||
            m_impl->databaseAwareImporters.find(classification.normalizedExtension) !=
                m_impl->databaseAwareImporters.end();
    }

    if (classification.requiresFormatSpecificParser) {
        classification.diagnostic = classification.importerRegistered
                                         ? "format-specific parser is registered"
                                         : "format-specific parser is not registered";
    } else if (classification.kind == AssetImportSourceKindUVE::Unknown) {
        classification.diagnostic = classification.importerRegistered
                                         ? "custom importer is registered without built-in classification"
                                         : "unsupported source extension";
    } else if (classification.kind == AssetImportSourceKindUVE::PlainText) {
        classification.diagnostic = classification.importerRegistered
                                         ? "built-in text parser is registered"
                                         : "source format is classified but no importer is registered";
    } else {
        classification.diagnostic = classification.importerRegistered
                                         ? "built-in generic copy importer is registered"
                                         : "source format is classified but no importer is registered";
    }
    return classification;
}

AssetGuidUVE AssetImporterUVE::ImportUVE(const std::filesystem::path& sourcePath,
                                          const std::filesystem::path& destinationPath,
                                          IAssetDatabaseUVE& assetDatabase,
                                          const AssetImportSettingsUVE& settings) {
    ImportFuncUVE importFunc;
    DatabaseAwareImportFuncUVE databaseAwareImportFunc;
    const std::string key = NormalizeExtensionUVE(sourcePath.extension().string());
    {
        const std::lock_guard<std::mutex> lock(m_impl->mutex);
        const auto it = m_impl->importers.find(key);
        const auto awareIt = m_impl->databaseAwareImporters.find(key);
        if (it == m_impl->importers.end() && awareIt == m_impl->databaseAwareImporters.end()) {
            UVE_ERROR("AssetImporterUVE: no importer registered for extension \"{}\" (source \"{}\")", key,
                       sourcePath.string());
            return kInvalidAssetGuidUVE;
        }
        if (awareIt != m_impl->databaseAwareImporters.end()) {
            databaseAwareImportFunc = awareIt->second;
        } else {
            importFunc = it->second;
        }
    }

    bool imported = false;
    std::string failureReason;
    try {
        imported = databaseAwareImportFunc ? databaseAwareImportFunc(sourcePath, destinationPath, assetDatabase, settings)
                                           : importFunc(sourcePath, destinationPath, settings);
    } catch (const std::exception& exception) {
        failureReason = "importer threw: " + std::string(exception.what());
    } catch (...) {
        failureReason = "importer threw an unknown exception";
    }
    if (!imported) {
        if (failureReason.empty()) {
            UVE_ERROR("AssetImporterUVE: import failed for \"{}\"", sourcePath.string());
        } else {
            UVE_ERROR("AssetImporterUVE: import failed for \"{}\": {}", sourcePath.string(), failureReason);
        }
        return kInvalidAssetGuidUVE;
    }

    AssetGuidUVE guid = kInvalidAssetGuidUVE;
    try {
        guid = assetDatabase.RegisterUVE(destinationPath);
    } catch (const std::exception& exception) {
        UVE_ERROR("AssetImporterUVE: asset database registration threw for destination \"{}\": {}",
                  destinationPath.string(), exception.what());
        return kInvalidAssetGuidUVE;
    } catch (...) {
        UVE_ERROR("AssetImporterUVE: asset database registration threw an unknown exception for destination \"{}\"",
                  destinationPath.string());
        return kInvalidAssetGuidUVE;
    }
    if (guid == kInvalidAssetGuidUVE) {
        UVE_ERROR("AssetImporterUVE: asset database rejected registration for destination \"{}\"",
                  destinationPath.string());
        return kInvalidAssetGuidUVE;
    }
    return guid;
}

} // namespace UVE::Asset
