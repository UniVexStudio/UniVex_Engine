// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

#include "asset_guid.h"
#include "vector3.h"

namespace UVE::Scene {

/// Renderer-owned primitive geometry choices. The component stores only this stable semantic kind;
/// the immutable vertex/index data remains a render implementation detail and is never duplicated
/// into a scene document.
enum class PrimitiveMeshKind : std::uint8_t {
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
/// `kInvalidAssetGuid` means "no material", which draws the engine's built-in lit material
/// rather than an unlit colour.
///
/// `baseColor` is that built-in material's albedo, and it is legacy data: the Inspector no longer
/// shows a colour control for it, because a primitive gets its colour from its material and a
/// second control beside an empty material slot gave the same object two answers. It is not a tint
/// over an assigned material either - once `materialGuid` is set the material owns the surface
/// completely, matching how a mesh instance behaves. The field survives because scenes saved while
/// it was authored still carry it, and the renderer still honours it; nothing new writes one.
struct PrimitiveMeshComponent final {
    PrimitiveMeshKind kind = PrimitiveMeshKind::Cube;
    /// Neutral white by default, so a primitive with no material reads as the default material's
    /// own albedo rather than an unexplained tint. Not editable from the Inspector.
    Math::Vector3 baseColor{1.0F, 1.0F, 1.0F};
    /// Surface material. Invalid (the default) draws the built-in lit material instead.
    Asset::AssetGuid materialGuid{};
};

/// True when `component.materialGuid` is assigned (`!= kInvalidAssetGuid`).
[[nodiscard]] bool HasAssignedPrimitiveMaterial(const PrimitiveMeshComponent& component) noexcept;

[[nodiscard]] bool IsPrimitiveMeshKindValid(const PrimitiveMeshKind kind) noexcept;

[[nodiscard]] bool IsPrimitiveBaseColorValid(const Math::Vector3& color) noexcept;

[[nodiscard]] bool IsPrimitiveMeshComponentValid(const PrimitiveMeshComponent& component) noexcept;

/// Clamps `color` channels into `[0, 1]` (or neutral white if any channel is non-finite).
[[nodiscard]] Math::Vector3 ClampPrimitiveBaseColor(const Math::Vector3& color) noexcept;

/// Returns a sanitized `PrimitiveMeshComponent` with a valid kind and clamped base color.
[[nodiscard]] PrimitiveMeshComponent SanitizePrimitiveMeshComponent(
    const PrimitiveMeshComponent& component) noexcept;

/// Human-readable display label for `kind` (`"Cube"`, `"UVSphere"`, `"Plane"`).
[[nodiscard]] std::string_view GetPrimitiveMeshKindDisplayName(
    PrimitiveMeshKind kind) noexcept;

/// Local-space half-extents of the built-in unit primitive geometry for `kind`.
[[nodiscard]] Math::Vector3 ResolvePrimitiveLocalHalfExtents(
    PrimitiveMeshKind kind) noexcept;

} // namespace UVE::Scene
