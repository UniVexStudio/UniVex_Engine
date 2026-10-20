// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/primitive_mesh_component_uve.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace UVE::Scene {

[[nodiscard]] bool IsPrimitiveMeshKindValidUVE(const PrimitiveMeshKindUVE kind) noexcept {
    switch (kind) {
        case PrimitiveMeshKindUVE::Cube:
        case PrimitiveMeshKindUVE::UVSphere:
        case PrimitiveMeshKindUVE::Plane:
        case PrimitiveMeshKindUVE::Capsule:
        case PrimitiveMeshKindUVE::Cylinder:
            return true;
    }
    return false;
}

[[nodiscard]] bool IsPrimitiveBaseColorValidUVE(const Math::Vector3UVE& color) noexcept {
    return std::isfinite(color.x) && std::isfinite(color.y) && std::isfinite(color.z) &&
           color.x >= 0.0F && color.x <= 1.0F && color.y >= 0.0F && color.y <= 1.0F &&
           color.z >= 0.0F && color.z <= 1.0F;
}

[[nodiscard]] bool IsPrimitiveMeshComponentValidUVE(
    const PrimitiveMeshComponentUVE& component) noexcept {
    return IsPrimitiveMeshKindValidUVE(component.kind) &&
           IsPrimitiveBaseColorValidUVE(component.baseColor);
}

[[nodiscard]] Math::Vector3UVE ClampPrimitiveBaseColorUVE(const Math::Vector3UVE& color) noexcept {
    if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z)) {
        return Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    }
    return Math::Vector3UVE{
        std::clamp(color.x, 0.0F, 1.0F),
        std::clamp(color.y, 0.0F, 1.0F),
        std::clamp(color.z, 0.0F, 1.0F),
    };
}

[[nodiscard]] PrimitiveMeshComponentUVE SanitizePrimitiveMeshComponentUVE(
    const PrimitiveMeshComponentUVE& component) noexcept {
    return PrimitiveMeshComponentUVE{
        .kind = IsPrimitiveMeshKindValidUVE(component.kind) ? component.kind
                                                            : PrimitiveMeshKindUVE::Cube,
        .baseColor = ClampPrimitiveBaseColorUVE(component.baseColor),
    };
}

[[nodiscard]] std::string_view GetPrimitiveMeshKindDisplayNameUVE(
    const PrimitiveMeshKindUVE kind) noexcept {
    switch (kind) {
        case PrimitiveMeshKindUVE::Cube:
            return "Cube";
        case PrimitiveMeshKindUVE::UVSphere:
            return "UVSphere";
        case PrimitiveMeshKindUVE::Plane:
            return "Plane";
        case PrimitiveMeshKindUVE::Capsule:
            return "Capsule";
        case PrimitiveMeshKindUVE::Cylinder:
            return "Cylinder";
    }
    return "Cube";
}

[[nodiscard]] Math::Vector3UVE ResolvePrimitiveLocalHalfExtentsUVE(
    const PrimitiveMeshKindUVE kind) noexcept {
    switch (kind) {
        case PrimitiveMeshKindUVE::Cube:
        case PrimitiveMeshKindUVE::UVSphere:
            return Math::Vector3UVE{0.5F, 0.5F, 0.5F};
        case PrimitiveMeshKindUVE::Plane:
            return Math::Vector3UVE{0.5F, 0.01F, 0.5F};
        case PrimitiveMeshKindUVE::Capsule:
        case PrimitiveMeshKindUVE::Cylinder:
            return Math::Vector3UVE{0.5F, 1.0F, 0.5F};
    }
    return Math::Vector3UVE{0.5F, 0.5F, 0.5F};
}

} // namespace UVE::Scene
