// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "texture_import_settings.h"

#include <cstdint>
#include <string>

namespace UVE::Asset {

std::string TextureImportSettings::GetCacheVersion() const {
    return "texture-import-v3;color-space=" +
           std::to_string(static_cast<std::uint32_t>(colorSpace)) + ";usage=" +
           std::to_string(static_cast<std::uint32_t>(usage)) + ";generate-mipmaps=" +
           std::to_string(generateMipmaps ? 1U : 0U) + ";mip-filter=" +
           std::to_string(static_cast<std::uint32_t>(mipFilter)) + ";max-levels=" +
           std::to_string(maxMipLevels) + ";compression=" +
           std::to_string(static_cast<std::uint32_t>(compressionMode)) + ";compression-quality=" +
           std::to_string(static_cast<std::uint32_t>(compressionQuality)) + ";compression-effort=" +
           std::to_string(static_cast<std::uint32_t>(compressionEffort));
}

TextureImportSettings ResolveTextureImportSettings(const AssetImportSettings& settings) noexcept {
    const auto* const textureSettings = dynamic_cast<const TextureImportSettings*>(&settings);
    return textureSettings == nullptr ? TextureImportSettings{} : *textureSettings;
}

} // namespace UVE::Asset
