// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "i_asset_importer.h"

namespace UVE::Asset {

/// Registers the bounded raw glTF/GLB source importer. It supports one mesh with one TRIANGLES
/// primitive, one buffer, POSITION plus optional NORMAL/TEXCOORD_0 and indices, safe relative or
/// data-URI buffer bytes, and persists one validated MeshAsset as a .uvmodel destination. It
/// leaves AssetImporter responsible for database registration and owns no scene assembly, materials,
/// images, skins, LODs, VFS mounts, GPU resources, or background work.
void RegisterGltfImporter(IAssetImporter& importer);

} // namespace UVE::Asset
