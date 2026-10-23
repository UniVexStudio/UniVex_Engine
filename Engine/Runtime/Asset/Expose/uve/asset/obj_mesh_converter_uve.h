// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "uve/asset/mesh_asset_uve.h"

namespace UVE::Asset {

inline constexpr std::size_t kMaximumObjMeshSourceBytesUVE = 64U * 1024U * 1024U;
inline constexpr std::uint32_t kMaximumObjMeshVerticesUVE = 1'000'000U;

/// One contiguous run of triangles that share a `usemtl` name, in index-buffer order. Runs tile the
/// index buffer exactly, so a caller can turn them into MeshSurfaceUVE ranges without re-reading the
/// source. The run before the first `usemtl` carries an empty name.
struct ObjSurfaceRangeUVE final {
    std::uint32_t firstIndex = 0U;
    std::uint32_t indexCount = 0U;
    std::string materialName;
};

/// Converts a bounded OBJ source into a failure-atomic CPU MeshAssetUVE. The v1 path supports
/// position/UV/normal declarations, exactly three position coordinates or an optional finite nonzero
/// homogeneous position weight, `v`, `v/vt`, `v//vn`, and `v/vt/vn` face tokens, fan-triangulates
/// polygons, computes finite fallback face normals when `vn` is absent, and publishes copied local
/// bounds. It rejects trailing position coordinates instead of silently ignoring malformed source. It
/// does not load MTL files, resolve external textures, or own filesystem, importer-registration, or
/// GPU resources.
///
/// `outSurfaces`, when non-null, receives one range per `usemtl` run. A file that uses one material
/// throughout - or none at all - gets an EMPTY list: a single run says exactly what the absence of
/// ranges already says, and leaving it out keeps such a file byte-identical to what earlier versions
/// wrote. More runs than kMaximumMeshSurfacesUVE also gives an empty list (the model then draws with
/// one material), because a surface table that cannot be saved is worse than a coarse one.
[[nodiscard]] bool ConvertObjMeshUVE(std::string_view source, MeshAssetUVE& outMesh,
                                     std::vector<ObjSurfaceRangeUVE>* outSurfaces = nullptr);

} // namespace UVE::Asset
