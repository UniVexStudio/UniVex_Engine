// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

#include "uve/asset/asset_guid_uve.h"
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
///
/// The geometry itself is renderer-owned and never serialized; `kind` names it. `materialGuid`
/// names the surface material the primitive is shaded with, on exactly the same asset path an
/// imported mesh uses (albedo, normal, metallic-roughness, occlusion, emissive, custom shader) —
/// `kInvalidAssetGuidUVE` means "no material", which draws the engine's built-in lit material
/// rather than an unlit colour.
///
/// `baseColor` is that built-in material's albedo. It is deliberately NOT a tint over an assigned
/// material: once `materialGuid` is set the material owns the surface completely, matching how a
/// mesh instance behaves in the reference engines — a primitive must not keep altering a material
/// its author picked. White is the neutral default.
struct PrimitiveMeshComponentUVE final {
    PrimitiveMeshKindUVE kind = PrimitiveMeshKindUVE::Cube;
    /// New primitives start neutral white, so an unassigned material reads as the material's own
    /// albedo rather than an unexplained tint.
    Math::Vector3UVE baseColor{1.0F, 1.0F, 1.0F};
    /// Surface material. Invalid (the default) draws the built-in lit material instead.
    Asset::AssetGuidUVE materialGuid{};
};

/// True when `component.materialGuid` is assigned (`!= kInvalidAssetGuidUVE`).
[[nodiscard]] bool HasAssignedPrimitiveMaterialUVE(const PrimitiveMeshComponentUVE& component) noexcept;

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
