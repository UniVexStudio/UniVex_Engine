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
// The order the materials are published in is also the order the mesh's surface ranges refer to, so
// the `usemtl` runs the geometry converter found become surfaces here, one slot per name. First-use
// order still decides a multi-material OBJ's default look for anything that names no surface: the
// first material an author assigns is the one the object opens with.

#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/obj_mesh_converter_uve.h"

namespace UVE::Asset::Detail {

/// Publishes the OBJ source's materials beside the model, fills `inOutMesh.materialGuids`, and -
/// when `surfaceRanges` holds more than one run - turns those runs into `inOutMesh.surfaces` with
/// the published slot each name landed in. A name the library does not declare, or a material that
/// failed validation and was left out, gives that surface an invalid slot: the renderer then draws
/// it with the mesh's first material, which is visible, rather than dropping it silently.
///
/// Returns false only when a published file is refused by the asset database; every recoverable
/// problem (no `mtllib`, missing library file, unparsable material, undecodable texture) is logged
/// and leaves that part empty, because the geometry is still worth importing.
[[nodiscard]] bool ImportObjMaterialsUVE(std::string_view objSource, const std::filesystem::path& sourcePath,
                                         const std::filesystem::path& destinationPath,
                                         IAssetDatabaseUVE& assetDatabase, MeshAssetUVE& inOutMesh,
                                         const std::vector<ObjSurfaceRangeUVE>& surfaceRanges);

} // namespace UVE::Asset::Detail
