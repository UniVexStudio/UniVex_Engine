// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "i_asset_importer.h"

namespace UVE::Asset {

/// Registers the bounded raw OBJ source importer. The importer reads a caller-owned filesystem path,
/// converts supported source forms through ConvertObjMesh, persists one validated MeshAsset as
/// a .uvmodel destination, and leaves AssetImporter responsible for database registration. It
/// owns no MTL files, VFS mounts, GPU resources, background work, or asset-manager state.
void RegisterObjImporter(IAssetImporter& importer);

} // namespace UVE::Asset
