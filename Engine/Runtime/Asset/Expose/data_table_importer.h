// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <vector>

#include "data_table.h"
#include "i_asset_importer.h"

namespace UVE::Asset {

/// Format-specific settings for explicit schema-driven Data Table source imports. Automatic schema
/// inference is deliberately not part of this contract.
struct DataTableImportSettingsUVE final : AssetImportSettingsUVE {
    std::string tableName;
    std::vector<DataTableColumnUVE> columns;

    [[nodiscard]] std::string GetCacheVersionUVE() const override;
};

/// Registers schema-driven CSV, TSV, and JSON source importers. Each importer writes one validated
/// `.uvtable` destination and lets the generic importer register that destination in the database.
void RegisterDataTableImportersUVE(IAssetImporterUVE& importer);

} // namespace UVE::Asset
