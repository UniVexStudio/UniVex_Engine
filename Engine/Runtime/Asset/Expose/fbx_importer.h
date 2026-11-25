// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "i_asset_importer.h"

namespace UVE::Asset {

/// Registers the FBX source importer. It reads the caller-owned path, converts it through
/// ConvertFbxMesh and publishes one validated MeshAsset as a .uvmodel destination, leaving
/// AssetImporter responsible for database registration. It owns no materials, textures,
/// separate skeleton or animation assets, VFS mounts, GPU resources or background work (a skinned
/// mesh keeps its skin inside the .uvmodel).
void RegisterFbxImporter(IAssetImporter& importer);

} // namespace UVE::Asset
