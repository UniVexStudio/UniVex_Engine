// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "data_table_pipeline.h"

#include "data_table_asset_manager.h"
#include "data_table_importer.h"

namespace UVE::Asset {

void RegisterDataTablePipelineUVE(IAssetImporterUVE& importer, IAssetManagerUVE& assetManager) {
    RegisterDataTableImportersUVE(importer);
    RegisterDataTableAssetLoaderUVE(assetManager);
}

} // namespace UVE::Asset
