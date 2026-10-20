// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

/// Renderer-owned primitive geometry choices. The component stores only this stable semantic kind;
/// the immutable vertex/index data remains a render implementation detail and is never duplicated
/// into a scene document.
enum class PrimitiveMeshKindUVE : std::uint8_t {
    Cube = 0U,
    UVSphere = 1U,
    Plane = 2U,
    Capsule = 3U,
    Cylinder = 4U,
};

/// A serializable, editor-authored reference to one deterministic built-in primitive mesh.
/// `baseColor` is bounded linear RGB used by the primitive lighting path; it is not a material
/// asset and therefore does not imply texture, normal-map, or PBR authoring support.
struct PrimitiveMeshComponentUVE final {
    PrimitiveMeshKindUVE kind = PrimitiveMeshKindUVE::Cube;
    /// New primitives start neutral white; authored materials/colors can override this explicitly.
    Math::Vector3UVE baseColor{1.0F, 1.0F, 1.0F};
};

[[nodiscard]] bool IsPrimitiveMeshKindValidUVE(const PrimitiveMeshKindUVE kind) noexcept;

[[nodiscard]] bool IsPrimitiveBaseColorValidUVE(const Math::Vector3UVE& color) noexcept;

[[nodiscard]] bool IsPrimitiveMeshComponentValidUVE(const PrimitiveMeshComponentUVE& component) noexcept;

/// Clamps `color` channels into `[0, 1]` (or neutral white if any channel is non-finite).
[[nodiscard]] Math::Vector3UVE ClampPrimitiveBaseColorUVE(const Math::Vector3UVE& color) noexcept;

/// Returns a sanitized `PrimitiveMeshComponentUVE` with a valid kind and clamped base color.
[[nodiscard]] PrimitiveMeshComponentUVE SanitizePrimitiveMeshComponentUVE(
    const PrimitiveMeshComponentUVE& component) noexcept;

/// Human-readable display label for `kind` (`"Cube"`, `"UVSphere"`, `"Plane"`).
[[nodiscard]] std::string_view GetPrimitiveMeshKindDisplayNameUVE(
    PrimitiveMeshKindUVE kind) noexcept;

/// Local-space half-extents of the built-in unit primitive geometry for `kind`.
[[nodiscard]] Math::Vector3UVE ResolvePrimitiveLocalHalfExtentsUVE(
    PrimitiveMeshKindUVE kind) noexcept;

} // namespace UVE::Scene
