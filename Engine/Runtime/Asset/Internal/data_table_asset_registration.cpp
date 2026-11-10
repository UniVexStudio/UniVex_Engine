// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "data_table_asset_registration.h"

#include "data_table_asset.h"
#include "i_asset_database.h"
#include "logging_macros.h"

namespace UVE::Asset {

std::optional<AssetGuidUVE> RegisterDataTableAssetUVE(IAssetDatabaseUVE& database,
                                                      const std::filesystem::path& path) {
    if (path.empty() || path.extension() != ".uvtable") {
        UVE_ERROR("DataTableAssetRegistrationUVE: path must use the .uvtable extension");
        return std::nullopt;
    }

    DataTableUVE validatedTable;
    if (!LoadDataTableAssetUVE(path, validatedTable)) {
        UVE_ERROR("DataTableAssetRegistrationUVE: rejected invalid data-table asset at {}", path.string());
        return std::nullopt;
    }

    const AssetGuidUVE guid = database.RegisterUVE(path);
    if (guid == kInvalidAssetGuidUVE) {
        UVE_ERROR("DataTableAssetRegistrationUVE: database returned an invalid GUID for {}", path.string());
        return std::nullopt;
    }
    return guid;
}

} // namespace UVE::Asset
