// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "i_asset_importer.h"

namespace UVE::Asset {

/// Registers the bounded raw JPEG source importer for `.jpg` and `.jpeg`. The importer reads capped
/// source bytes, decodes supported baseline/progressive JPEG through DecodeJpegRgba8Image, and
/// persists one validated RGBA8 TextureAsset as a `.uvtex` destination. It leaves AssetImporter
/// responsible for database registration and owns no VFS mounts, GPU resources, background work, or
/// texture compression/streaming policy.
void RegisterJpegImporter(IAssetImporter& importer);

} // namespace UVE::Asset
