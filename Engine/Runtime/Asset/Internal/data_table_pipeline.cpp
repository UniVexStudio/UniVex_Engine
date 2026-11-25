// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "data_table_pipeline.h"

#include "data_table_asset_manager.h"
#include "data_table_importer.h"

namespace UVE::Asset {

void RegisterDataTablePipeline(IAssetImporter& importer, IAssetManager& assetManager) {
    RegisterDataTableImporters(importer);
    RegisterDataTableAssetLoader(assetManager);
}

} // namespace UVE::Asset
