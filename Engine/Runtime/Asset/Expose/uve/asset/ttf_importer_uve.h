// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/asset/i_asset_importer_uve.h"

namespace UVE::Asset {

/// Registers the `.ttf`/`.otf` → `.uvfont` importer: bounded source read, sfnt-magic check, raw
/// bytes wrapped in a `FontAssetUVE` with the source stem as the family name. Called once from
/// engine startup next to the other format importers.
void RegisterTtfImporterUVE(IAssetImporterUVE& importer);

} // namespace UVE::Asset
