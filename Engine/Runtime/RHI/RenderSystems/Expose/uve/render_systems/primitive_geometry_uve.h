// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <vector>

#include "uve/asset/mesh_asset_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"

namespace UVE::Render {

/// Immutable CPU-side geometry for one renderer-owned primitive. It deliberately uses the same
/// MeshVertexUVE binary layout as asset-backed meshes so the existing GPU vertex layout and tangent
/// generation path stay canonical.
struct PrimitiveGeometryUVE final {
    std::vector<Asset::MeshVertexUVE> vertices;
    std::vector<std::uint32_t> indices;
    Math::AabbUVE localBounds;
};

/// Returns deterministic unit-scale primitive geometry. Cube and Plane use a half extent of 0.5;
/// UV Sphere uses radius 0.5 with a duplicated UV seam and per-sector pole vertices. The returned
/// cache is immutable, process-local, and never serialized into a scene document.
[[nodiscard]] const PrimitiveGeometryUVE& GetPrimitiveGeometryUVE(Scene::PrimitiveMeshKindUVE kind) noexcept;

/// The same geometry as GetPrimitiveGeometryUVE(), with a real tangent basis derived from the
/// triangles and UVs (MeshAssetUVE's own GenerateMeshTangentsUVE(), the path every imported mesh
/// takes). Normal mapping needs it: the raw geometry tables carry a placeholder tangent, which
/// would make every normal map on a primitive look like it was mapped through the wrong axis.
///
/// The returned cache is immutable, process-local and never serialized, exactly like
/// GetPrimitiveGeometryUVE(); the tangent derivation happens once per kind, on first request.
[[nodiscard]] const PrimitiveGeometryUVE& GetPrimitiveLitGeometryUVE(Scene::PrimitiveMeshKindUVE kind) noexcept;

} // namespace UVE::Render
