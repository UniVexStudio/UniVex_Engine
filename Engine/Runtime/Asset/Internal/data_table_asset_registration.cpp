// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "data_table_asset_registration.h"

#include "data_table_asset.h"
#include "i_asset_database.h"
#include "logging_macros.h"

namespace UVE::Asset {

std::optional<AssetGuid> RegisterDataTableAsset(IAssetDatabase& database,
                                                      const std::filesystem::path& path) {
    if (path.empty() || path.extension() != ".uvtable") {
        UVE_ERROR("DataTableAssetRegistration: path must use the .uvtable extension");
        return std::nullopt;
    }

    DataTable validatedTable;
    if (!LoadDataTableAsset(path, validatedTable)) {
        UVE_ERROR("DataTableAssetRegistration: rejected invalid data-table asset at {}", path.string());
        return std::nullopt;
    }

    const AssetGuid guid = database.Register(path);
    if (guid == kInvalidAssetGuid) {
        UVE_ERROR("DataTableAssetRegistration: database returned an invalid GUID for {}", path.string());
        return std::nullopt;
    }
    return guid;
}

} // namespace UVE::Asset
