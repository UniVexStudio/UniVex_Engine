// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstdint>

#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

/// Renderer-owned primitive geometry choices. The component stores only this stable semantic kind;
/// the immutable vertex/index data remains a render implementation detail and is never duplicated
/// into a scene document.
enum class PrimitiveMeshKindUVE : std::uint8_t {
    Cube = 0U,
    UVSphere = 1U,
    Plane = 2U,
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

} // namespace UVE::Scene
