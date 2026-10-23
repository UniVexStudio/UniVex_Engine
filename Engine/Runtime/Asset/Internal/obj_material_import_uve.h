// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// OBJ/MTL material import.
//
// An OBJ names its material library with `mtllib` and switches materials with `usemtl`; the
// geometry converter is deliberately geometry-only, so before this module an OBJ arrived with no
// material and drew grey even though the MTL and its textures were sitting next to it. Here the
// referenced library is read once, the materials the mesh actually uses are published as `.uvmat`
// beside the model, their `map_*` textures are imported as `.uvtex` (or resolved to the earlier
// import of the same stem), and the mesh references them by GUID in first-use order.
//
// The engine still draws one material per object - per-surface ranges are their own increment - so
// first-use order is what decides a multi-material OBJ's default look. Publishing them in that
// order is the honest choice: the first material an author assigns is the one the object opens with.

#pragma once

#include <filesystem>
#include <string_view>

#include "uve/asset/asset_guid_uve.h"
#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/mesh_asset_uve.h"

namespace UVE::Asset::Detail {

/// Publishes the OBJ source's materials beside the model and fills `inOutMesh.materialGuids`.
/// Returns false only when a published file is refused by the asset database; every recoverable
/// problem (no `mtllib`, missing library file, unparsable material, undecodable texture) is logged
/// and leaves that part empty, because the geometry is still worth importing.
[[nodiscard]] bool ImportObjMaterialsUVE(std::string_view objSource, const std::filesystem::path& sourcePath,
                                         const std::filesystem::path& destinationPath,
                                         IAssetDatabaseUVE& assetDatabase, MeshAssetUVE& inOutMesh);

} // namespace UVE::Asset::Detail
