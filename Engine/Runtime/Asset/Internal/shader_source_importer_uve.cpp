// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/shader_source_importer_uve.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "import_helpers_uve.h"

#include "uve/asset/asset_importer_uve.h"
#include "uve/asset/shader_asset_uve.h"
#include "uve/debug/logging_macros_uve.h"

namespace UVE::Asset {
namespace {

constexpr const char* kShaderSourceImporterNameUVE = "ShaderSourceImporterUVE";
constexpr std::string_view kShaderTemporarySuffixUVE = ".uve_shader_tmp";

[[nodiscard]] std::string NormalizeShaderExtensionUVE(std::string extension) {
    if (!extension.empty() && extension.front() == '.') {
        extension.erase(extension.begin());
    }
    for (char& character : extension) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return extension;
}

[[nodiscard]] bool ReadShaderSourceUVE(const std::filesystem::path& sourcePath, std::string& outSource) {
    std::error_code sizeError;
    const std::uintmax_t sourceSize = std::filesystem::file_size(sourcePath, sizeError);
    if (sizeError || sourceSize > kMaximumShaderSourceBytesUVE ||
        sourceSize > std::numeric_limits<std::size_t>::max()) {
        UVE_ERROR("ShaderSourceImporterUVE: shader source exceeds the {}-byte cap or cannot be sized \"{}\"",
                  kMaximumShaderSourceBytesUVE, sourcePath.string());
        return false;
    }

    std::ifstream input(sourcePath, std::ios::binary);
    if (!input.is_open()) {
        UVE_ERROR("ShaderSourceImporterUVE: failed to open shader source \"{}\"", sourcePath.string());
        return false;
    }
    std::string source{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (!input || source.size() > kMaximumShaderSourceBytesUVE) {
        UVE_ERROR("ShaderSourceImporterUVE: shader source \"{}\" could not be read completely or grew beyond the cap",
                  sourcePath.string());
        return false;
    }
    if (source.empty() || source.find('\0') != std::string::npos) {
        UVE_ERROR("ShaderSourceImporterUVE: shader source \"{}\" is empty or contains a NUL byte",
                  sourcePath.string());
        return false;
    }
    outSource = std::move(source);
    return true;
}

[[nodiscard]] bool ShaderStageForExtensionUVE(const std::string& extension,
                                               ShaderStageKindUVE& outStage) noexcept {
    if (extension == "vert") {
        outStage = ShaderStageKindUVE::Vertex;
        return true;
    }
    if (extension == "frag") {
        outStage = ShaderStageKindUVE::Fragment;
        return true;
    }
    if (extension == "comp") {
        outStage = ShaderStageKindUVE::Compute;
        return true;
    }
    return false;
}

[[nodiscard]] bool ImportShaderSourceUVE(const std::filesystem::path& sourcePath,
                                         const std::filesystem::path& destinationPath,
                                         const AssetImportSettingsUVE& /*settings*/) {
    if (NormalizeShaderExtensionUVE(destinationPath.extension().string()) != "uveshader") {
        UVE_ERROR("ShaderSourceImporterUVE: destination \"{}\" must use the .uveshader extension",
                  destinationPath.string());
        return false;
    }

    ShaderStageKindUVE stage = ShaderStageKindUVE::Vertex;
    const std::string sourceExtension = NormalizeShaderExtensionUVE(sourcePath.extension().string());
    if (!ShaderStageForExtensionUVE(sourceExtension, stage)) {
        UVE_ERROR("ShaderSourceImporterUVE: source \"{}\" has no supported shader stage extension",
                  sourcePath.string());
        return false;
    }

    std::string sourceCode;
    if (!ReadShaderSourceUVE(sourcePath, sourceCode)) {
        return false;
    }

    ShaderAssetUVE shader;
    shader.stage = stage;
    shader.sourceCode = std::move(sourceCode);
    shader.entryPointName = "main";
    return Detail::PublishAssetAtomicallyUVE(destinationPath, kShaderSourceImporterNameUVE,
                                             kShaderTemporarySuffixUVE,
                                             [&shader](const std::filesystem::path& temporaryPath) {
                                                 return SaveShaderAssetUVE(shader, temporaryPath);
                                             });
}

} // namespace

void RegisterShaderSourceImporterUVE(IAssetImporterUVE& importer) {
    importer.RegisterImporterUVE("vert", &ImportShaderSourceUVE);
    importer.RegisterImporterUVE("frag", &ImportShaderSourceUVE);
    importer.RegisterImporterUVE("comp", &ImportShaderSourceUVE);
}

} // namespace UVE::Asset
