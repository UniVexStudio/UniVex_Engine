// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "shader_source_importer.h"

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

#include "import_helpers.h"

#include "asset_importer.h"
#include "shader_asset.h"
#include "logging_macros.h"

namespace UVE::Asset {
namespace {

constexpr const char* kShaderSourceImporterName = "ShaderSourceImporter";
constexpr std::string_view kShaderTemporarySuffix = ".uve_shader_tmp";

[[nodiscard]] std::string NormalizeShaderExtension(std::string extension) {
    if (!extension.empty() && extension.front() == '.') {
        extension.erase(extension.begin());
    }
    for (char& character : extension) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return extension;
}

[[nodiscard]] bool ReadShaderSource(const std::filesystem::path& sourcePath, std::string& outSource) {
    std::error_code sizeError;
    const std::uintmax_t sourceSize = std::filesystem::file_size(sourcePath, sizeError);
    if (sizeError || sourceSize > kMaximumShaderSourceBytes ||
        sourceSize > std::numeric_limits<std::size_t>::max()) {
        UVE_ERROR("ShaderSourceImporter: shader source exceeds the {}-byte cap or cannot be sized \"{}\"",
                  kMaximumShaderSourceBytes, sourcePath.string());
        return false;
    }

    std::ifstream input(sourcePath, std::ios::binary);
    if (!input.is_open()) {
        UVE_ERROR("ShaderSourceImporter: failed to open shader source \"{}\"", sourcePath.string());
        return false;
    }
    std::string source{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (!input || source.size() > kMaximumShaderSourceBytes) {
        UVE_ERROR("ShaderSourceImporter: shader source \"{}\" could not be read completely or grew beyond the cap",
                  sourcePath.string());
        return false;
    }
    if (source.empty() || source.contains('\0')) {
        UVE_ERROR("ShaderSourceImporter: shader source \"{}\" is empty or contains a NUL byte",
                  sourcePath.string());
        return false;
    }
    outSource = std::move(source);
    return true;
}

[[nodiscard]] bool ShaderStageForExtension(const std::string& extension,
                                               ShaderStageKind& outStage) noexcept {
    if (extension == "vert") {
        outStage = ShaderStageKind::Vertex;
        return true;
    }
    if (extension == "frag") {
        outStage = ShaderStageKind::Fragment;
        return true;
    }
    if (extension == "comp") {
        outStage = ShaderStageKind::Compute;
        return true;
    }
    return false;
}

[[nodiscard]] bool ImportShaderSource(const std::filesystem::path& sourcePath,
                                         const std::filesystem::path& destinationPath,
                                         const AssetImportSettings& /*settings*/) {
    if (NormalizeShaderExtension(destinationPath.extension().string()) != "uvshader") {
        UVE_ERROR("ShaderSourceImporter: destination \"{}\" must use the .uvshader extension",
                  destinationPath.string());
        return false;
    }

    ShaderStageKind stage = ShaderStageKind::Vertex;
    const std::string sourceExtension = NormalizeShaderExtension(sourcePath.extension().string());
    if (!ShaderStageForExtension(sourceExtension, stage)) {
        UVE_ERROR("ShaderSourceImporter: source \"{}\" has no supported shader stage extension",
                  sourcePath.string());
        return false;
    }

    std::string sourceCode;
    if (!ReadShaderSource(sourcePath, sourceCode)) {
        return false;
    }

    ShaderAsset shader;
    shader.stage = stage;
    shader.sourceCode = std::move(sourceCode);
    shader.entryPointName = "main";
    return Detail::PublishAssetAtomically(destinationPath, kShaderSourceImporterName,
                                             kShaderTemporarySuffix,
                                             [&shader](const std::filesystem::path& temporaryPath) {
                                                 return SaveShaderAsset(shader, temporaryPath);
                                             });
}

} // namespace

void RegisterShaderSourceImporter(IAssetImporter& importer) {
    importer.RegisterImporter("vert", &ImportShaderSource);
    importer.RegisterImporter("frag", &ImportShaderSource);
    importer.RegisterImporter("comp", &ImportShaderSource);
}

} // namespace UVE::Asset
