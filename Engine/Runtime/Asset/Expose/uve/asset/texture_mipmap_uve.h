// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>

#include "uve/asset/texture_asset_uve.h"
#include "uve/asset/texture_import_settings_uve.h"

namespace UVE::Asset {

/// Replaces the additional mip chain on `texture` using the requested CPU filter. The base level
/// is unchanged. `maxMipLevels` counts level 0; zero means the complete chain down to 1x1. Returns
/// false without changing the existing chain if the base image, filter, or format is unsupported.
[[nodiscard]] bool GenerateTextureMipmapsUVE(TextureAssetUVE& texture,
                                              TextureMipmapFilterUVE filter,
                                              std::uint32_t maxMipLevels = 0U);

} // namespace UVE::Asset
