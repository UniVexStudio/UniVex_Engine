// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "i_asset_importer.h"

namespace UVE::Asset {

/// Registers the bounded raw BMP source importer. It decodes supported 24/32-bit uncompressed BMP
/// forms into copied RGBA8 pixels, persists one validated TextureAssetUVE as a `.uvtex` destination,
/// and leaves AssetImporterUVE responsible for database registration. It owns no GPU resources,
/// background work, or asset-manager state.
void RegisterBmpImporterUVE(IAssetImporterUVE& importer);

} // namespace UVE::Asset
