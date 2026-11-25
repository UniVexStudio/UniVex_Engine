// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "primitive_mesh_component.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace UVE::Scene {

[[nodiscard]] bool IsPrimitiveMeshKindValid(const PrimitiveMeshKind kind) noexcept {
    switch (kind) {
        case PrimitiveMeshKind::Cube:
        case PrimitiveMeshKind::UVSphere:
        case PrimitiveMeshKind::Plane:
        case PrimitiveMeshKind::Capsule:
        case PrimitiveMeshKind::Cylinder:
            return true;
    }
    return false;
}

[[nodiscard]] bool IsPrimitiveBaseColorValid(const Math::Vector3& color) noexcept {
    return std::isfinite(color.x) && std::isfinite(color.y) && std::isfinite(color.z) &&
           color.x >= 0.0F && color.x <= 1.0F && color.y >= 0.0F && color.y <= 1.0F &&
           color.z >= 0.0F && color.z <= 1.0F;
}

[[nodiscard]] bool IsPrimitiveMeshComponentValid(
    const PrimitiveMeshComponent& component) noexcept {
    return IsPrimitiveMeshKindValid(component.kind) &&
           IsPrimitiveBaseColorValid(component.baseColor);
}

[[nodiscard]] Math::Vector3 ClampPrimitiveBaseColor(const Math::Vector3& color) noexcept {
    if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z)) {
        return Math::Vector3{1.0F, 1.0F, 1.0F};
    }
    return Math::Vector3{
        std::clamp(color.x, 0.0F, 1.0F),
        std::clamp(color.y, 0.0F, 1.0F),
        std::clamp(color.z, 0.0F, 1.0F),
    };
}

[[nodiscard]] bool HasAssignedPrimitiveMaterial(
    const PrimitiveMeshComponent& component) noexcept {
    // Exact equality, matching HasAssignedMaterialAsset(): a material reference is either the
    // sentinel or a real asset identifier. Nothing here resolves the GUID.
    return component.materialGuid != Asset::kInvalidAssetGuid;
}

[[nodiscard]] PrimitiveMeshComponent SanitizePrimitiveMeshComponent(
    const PrimitiveMeshComponent& component) noexcept {
    return PrimitiveMeshComponent{
        .kind = IsPrimitiveMeshKindValid(component.kind) ? component.kind
                                                            : PrimitiveMeshKind::Cube,
        .baseColor = ClampPrimitiveBaseColor(component.baseColor),
        // Carried through untouched: a reference is opaque to this layer, and dropping it here
        // would silently detach a material from every primitive a sanitising boundary touches.
        .materialGuid = component.materialGuid,
    };
}

[[nodiscard]] std::string_view GetPrimitiveMeshKindDisplayName(
    const PrimitiveMeshKind kind) noexcept {
    switch (kind) {
        case PrimitiveMeshKind::Cube:
            return "Cube";
        case PrimitiveMeshKind::UVSphere:
            return "UVSphere";
        case PrimitiveMeshKind::Plane:
            return "Plane";
        case PrimitiveMeshKind::Capsule:
            return "Capsule";
        case PrimitiveMeshKind::Cylinder:
            return "Cylinder";
    }
    return "Cube";
}

[[nodiscard]] Math::Vector3 ResolvePrimitiveLocalHalfExtents(
    const PrimitiveMeshKind kind) noexcept {
    switch (kind) {
        case PrimitiveMeshKind::Cube:
        case PrimitiveMeshKind::UVSphere:
            return Math::Vector3{0.5F, 0.5F, 0.5F};
        case PrimitiveMeshKind::Plane:
            return Math::Vector3{0.5F, 0.01F, 0.5F};
        case PrimitiveMeshKind::Capsule:
        case PrimitiveMeshKind::Cylinder:
            return Math::Vector3{0.5F, 1.0F, 0.5F};
    }
    return Math::Vector3{0.5F, 0.5F, 0.5F};
}

} // namespace UVE::Scene
