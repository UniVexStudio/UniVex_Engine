// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <vector>

#include "mesh_asset.h"
#include "primitive_mesh_component.h"

namespace UVE::Render {

/// Immutable CPU-side geometry for one renderer-owned primitive. It deliberately uses the same
/// MeshVertex binary layout as asset-backed meshes so the existing GPU vertex layout and tangent
/// generation path stay canonical.
struct PrimitiveGeometry final {
    std::vector<Asset::MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    Math::Aabb localBounds;
};

/// Returns deterministic unit-scale primitive geometry. Cube and Plane use a half extent of 0.5;
/// UV Sphere uses radius 0.5 with a duplicated UV seam and per-sector pole vertices. The returned
/// cache is immutable, process-local, and never serialized into a scene document.
[[nodiscard]] const PrimitiveGeometry& GetPrimitiveGeometry(Scene::PrimitiveMeshKind kind) noexcept;

/// The same geometry as GetPrimitiveGeometry(), with a real tangent basis derived from the
/// triangles and UVs (MeshAsset's own GenerateMeshTangents(), the path every imported mesh
/// takes). Normal mapping needs it: the raw geometry tables carry a placeholder tangent, which
/// would make every normal map on a primitive look like it was mapped through the wrong axis.
///
/// The returned cache is immutable, process-local and never serialized, exactly like
/// GetPrimitiveGeometry(); the tangent derivation happens once per kind, on first request.
[[nodiscard]] const PrimitiveGeometry& GetPrimitiveLitGeometry(Scene::PrimitiveMeshKind kind) noexcept;

} // namespace UVE::Render
