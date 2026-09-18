// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/primitive_mesh_component_uve.h"

#include <cmath>
#include <cstdint>

namespace UVE::Scene {

[[nodiscard]] bool IsPrimitiveMeshKindValidUVE(const PrimitiveMeshKindUVE kind) noexcept {
    switch (kind) {
        case PrimitiveMeshKindUVE::Cube:
        case PrimitiveMeshKindUVE::UVSphere:
        case PrimitiveMeshKindUVE::Plane:
            return true;
    }
    return false;
}

[[nodiscard]] bool IsPrimitiveBaseColorValidUVE(const Math::Vector3UVE& color) noexcept {
    return std::isfinite(color.x) && std::isfinite(color.y) && std::isfinite(color.z) && color.x >= 0.0F &&
           color.x <= 1.0F && color.y >= 0.0F && color.y <= 1.0F && color.z >= 0.0F && color.z <= 1.0F;
}

[[nodiscard]] bool IsPrimitiveMeshComponentValidUVE(const PrimitiveMeshComponentUVE& component) noexcept {
    return IsPrimitiveMeshKindValidUVE(component.kind) && IsPrimitiveBaseColorValidUVE(component.baseColor);
}

} // namespace UVE::Scene
