// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "data_table_asset_manager.h"

#include "data_table_asset.h"
#include "data_table.h"
#include "i_asset_manager.h"

namespace UVE::Asset {

void RegisterDataTableAssetLoaderUVE(IAssetManagerUVE& assetManager) {
    assetManager.RegisterLoaderUVE<DataTableUVE>(LoadDataTableAssetUVE,
        [](const DataTableUVE& table) { return table.GetResidentBytesUVE(); });
}

} // namespace UVE::Asset
