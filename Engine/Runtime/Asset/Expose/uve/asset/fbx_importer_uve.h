// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/asset/i_asset_importer_uve.h"

namespace UVE::Asset {

/// Registers the FBX source importer. It reads the caller-owned path, converts it through
/// ConvertFbxMeshUVE and publishes one validated MeshAssetUVE as a .uvemodel destination, leaving
/// AssetImporterUVE responsible for database registration. It owns no materials, textures,
/// skeletons, animation, VFS mounts, GPU resources or background work.
void RegisterFbxImporterUVE(IAssetImporterUVE& importer);

} // namespace UVE::Asset
