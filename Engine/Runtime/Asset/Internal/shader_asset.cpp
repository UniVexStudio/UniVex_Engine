// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "shader_asset.h"

#include <nlohmann/json.hpp>

#include "uve_file_envelope.h"
#include "logging_macros.h"

namespace UVE::Asset {
namespace {

[[nodiscard]] bool IsValidShaderStageValue(const std::uint8_t value) noexcept {
    return value == static_cast<std::uint8_t>(ShaderStageKind::Vertex) ||
           value == static_cast<std::uint8_t>(ShaderStageKind::Fragment) ||
           value == static_cast<std::uint8_t>(ShaderStageKind::Compute);
}

[[nodiscard]] bool IsValidShaderStage(const ShaderStageKind stage) noexcept {
    return IsValidShaderStageValue(static_cast<std::uint8_t>(stage));
}

} // namespace

bool LoadShaderAsset(const std::filesystem::path& path, ShaderAsset& outShader) {
    const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(path);
    if (!file.has_value()) {
        return false; // ReadUveFile already logged the specific reason.
    }
    if (file->first.assetType != AssetKind::Shader) {
        UVE_ERROR("ShaderAsset: \"{}\" is not a shader file (asset type {})", path.string(),
                   static_cast<std::uint32_t>(file->first.assetType));
        return false;
    }

    const std::vector<std::byte>& payloadBuffer = file->second;
    const std::string payloadText(reinterpret_cast<const char*>(payloadBuffer.data()), payloadBuffer.size());

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(payloadText);
    } catch (const nlohmann::json::parse_error& parseError) {
        UVE_ERROR("ShaderAsset: failed to parse \"{}\": {}", path.string(), parseError.what());
        return false;
    }

    ShaderAsset shader;
    std::uint8_t stageValue = 0U;
    try {
        stageValue = payload.at("stage").get<std::uint8_t>();
        shader.sourceCode = payload.at("sourceCode").get<std::string>();
        shader.entryPointName = payload.value("entryPointName", std::string("main"));
    } catch (const nlohmann::json::exception& fieldError) {
        UVE_ERROR("ShaderAsset: \"{}\" is missing an expected field: {}", path.string(), fieldError.what());
        return false;
    }

    if (!IsValidShaderStageValue(stageValue)) {
        UVE_ERROR("ShaderAsset: \"{}\" has an unknown shader stage", path.string());
        return false;
    }
    shader.stage = static_cast<ShaderStageKind>(stageValue);
    if (shader.sourceCode.empty()) {
        UVE_ERROR("ShaderAsset: \"{}\" has empty source code", path.string());
        return false;
    }

    outShader = std::move(shader);
    return true;
}

bool SaveShaderAsset(const ShaderAsset& shader, const std::filesystem::path& path) {
    if (!IsValidShaderStage(shader.stage)) {
        UVE_ERROR("ShaderAsset: refusing to save an unknown shader stage to {}", path.string());
        return false;
    }
    nlohmann::json payload;
    payload["stage"] = static_cast<std::uint8_t>(shader.stage);
    payload["sourceCode"] = shader.sourceCode;
    payload["entryPointName"] = shader.entryPointName;

    const std::string payloadText = payload.dump();
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const std::vector<std::byte> payloadBuffer(payloadBytes, payloadBytes + payloadText.size());

    return WriteUveFile(path, AssetKind::Shader, payloadBuffer);
}

} // namespace UVE::Asset
