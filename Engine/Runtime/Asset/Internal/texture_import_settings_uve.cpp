// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/texture_import_settings_uve.h"

#include <cstdint>
#include <string>

namespace UVE::Asset {

std::string TextureImportSettingsUVE::GetCacheVersionUVE() const {
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

TextureImportSettingsUVE ResolveTextureImportSettingsUVE(const AssetImportSettingsUVE& settings) noexcept {
    const auto* const textureSettings = dynamic_cast<const TextureImportSettingsUVE*>(&settings);
    return textureSettings == nullptr ? TextureImportSettingsUVE{} : *textureSettings;
}

} // namespace UVE::Asset
