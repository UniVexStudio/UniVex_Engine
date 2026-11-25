// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "data_table_asset_manager.h"

#include "data_table_asset.h"
#include "data_table.h"
#include "i_asset_manager.h"

namespace UVE::Asset {

void RegisterDataTableAssetLoader(IAssetManager& assetManager) {
    assetManager.RegisterLoader<DataTable>(LoadDataTableAsset,
        [](const DataTable& table) { return table.GetResidentBytes(); });
}

} // namespace UVE::Asset
