// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "i_asset_importer.h"

namespace UVE::Asset {

/// Registers the bounded raw BMP source importer. It decodes supported 24/32-bit uncompressed BMP
/// forms into copied RGBA8 pixels, persists one validated TextureAsset as a `.uvtex` destination,
/// and leaves AssetImporter responsible for database registration. It owns no GPU resources,
/// background work, or asset-manager state.
void RegisterBmpImporter(IAssetImporter& importer);

} // namespace UVE::Asset
