// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "i_asset_importer.h"

namespace UVE::Asset {

/// Registers the bounded raw TGA source importer for `.tga`. The importer owns only capped source
/// reading, TGA-to-RGBA8 decoding, and atomic `.uvtex` publication; AssetImporterUVE owns database
/// registration.
void RegisterTgaImporterUVE(IAssetImporterUVE& importer);

} // namespace UVE::Asset
