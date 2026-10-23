// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UVE::Scene {

/// Most per-surface material overrides a SurfaceInstance can carry. Matches the mesh surface bound
/// (Asset::kMaximumMeshSurfacesUVE): an instance that could not name a material for every surface
/// its mesh can hold would have a hole no author could fill.
inline constexpr std::size_t kMaximumSurfaceMaterialOverridesUVE = 4096U;

enum class SurfaceShadowModeUVE : std::uint8_t {
    Off = 0,
    On,
    /// Casts from both faces, for thin geometry that is open on one side.
    DoubleSided,
    /// Invisible, but still casts: a cheap stand-in shadow for something drawn another way.
    ShadowsOnly,
};

enum class SurfaceLightingModeUVE : std::uint8_t {
    /// Takes no part in global illumination.
    Disabled = 0,
    /// Baked: contributes to and receives baked lighting; must not move.
    Static,
    /// Contributes at runtime; may move.
    Dynamic,
};

enum class SurfaceFadeModeUVE : std::uint8_t {
    /// Pops in and out at the range limits.
    Disabled = 0,
    /// Fades itself across the margins.
    Self,
    /// Fades the objects that stand in for it at other ranges.
    Dependencies,
};

/// The shared state of SurfaceInstance3D, the abstract base (under RenderInstance3D) of every
/// object that draws geometry - meshes, primitives, particles. No object is a SurfaceInstance3D on its
/// own; its kinds carry this component and add their own.
struct SurfaceInstanceComponentUVE final {
    /// Replaces every surface's material when set. Project-relative, empty for none.
    std::string materialOverridePath;
    /// Replaces ONE surface's material, index for index with the mesh's surfaces. An empty entry
    /// keeps that surface's own material. Ignored while `materialOverridePath` is set, because that
    /// one already replaces every surface - the same precedence a single override has over a
    /// mesh's authored materials.
    ///
    /// This is book-keeping of the instance, not of the mesh: two objects sharing one imported
    /// model can shade the same surface differently without duplicating the model.
    std::vector<std::string> surfaceMaterialOverridePaths;
    /// Drawn over every surface in a second pass (outlines, damage flashes). Empty for none.
    std::string materialOverlayPath;
    /// 0 opaque, 1 invisible.
    float transparency = 0.0F;
    SurfaceShadowModeUVE castShadow = SurfaceShadowModeUVE::On;
    /// Grows the bounds used for culling, for shaders that move vertices outside them.
    float extraCullMargin = 0.0F;
    /// Above 1 keeps detail further away; below 1 drops it sooner.
    float lodBias = 1.0F;
    bool ignoreOcclusionCulling = false;
    SurfaceLightingModeUVE lightingMode = SurfaceLightingModeUVE::Static;
    /// Drawn only between these distances from the camera; an end of 0 means no limit.
    float visibilityRangeBegin = 0.0F;
    float visibilityRangeBeginMargin = 0.0F;
    float visibilityRangeEnd = 0.0F;
    float visibilityRangeEndMargin = 0.0F;
    SurfaceFadeModeUVE visibilityRangeFadeMode = SurfaceFadeModeUVE::Disabled;

    [[nodiscard]] bool operator==(const SurfaceInstanceComponentUVE&) const = default;
};

[[nodiscard]] bool IsSurfaceShadowModeValidUVE(SurfaceShadowModeUVE mode) noexcept;

[[nodiscard]] bool IsSurfaceLightingModeValidUVE(SurfaceLightingModeUVE mode) noexcept;

[[nodiscard]] bool IsSurfaceFadeModeValidUVE(SurfaceFadeModeUVE mode) noexcept;

/// Known enumerators; finite values; transparency within [0, 1]; a positive LOD bias; no negative
/// margin or distance; a range end, when set, not before its beginning; and no more per-surface
/// material overrides than a mesh can have surfaces.
[[nodiscard]] bool IsSurfaceInstanceComponentValidUVE(const SurfaceInstanceComponentUVE& component) noexcept;

/// Clamps out-of-range transparency, distances, margins, and LOD bias into valid bounds, and
/// shortens the per-surface override list to the bound. Trailing empty overrides are dropped: they
/// say the same thing an absent entry does, so a list that grew and shrank again saves its old
/// bytes back.
[[nodiscard]] SurfaceInstanceComponentUVE SanitizeSurfaceInstanceComponentUVE(
    const SurfaceInstanceComponentUVE& component);

/// Returns `1.0F - clamp(transparency, 0, 1)` in `[0, 1]`.
[[nodiscard]] float ResolveSurfaceInstanceOpacityUVE(
    const SurfaceInstanceComponentUVE& component) noexcept;

/// True when `castShadow` is `On`, `DoubleSided`, or `ShadowsOnly`.
[[nodiscard]] bool CastsSurfaceShadowUVE(const SurfaceInstanceComponentUVE& component) noexcept;

/// True when main-pass geometry is drawn (`castShadow != ShadowsOnly` and `opacity > 0`).
[[nodiscard]] bool DrawsMainPassSurfaceGeometryUVE(
    const SurfaceInstanceComponentUVE& component) noexcept;

/// True when `cameraDistance` falls inside `[visibilityRangeBegin, visibilityRangeEnd]` (`end == 0` = unbounded).
[[nodiscard]] bool IsDistanceWithinSurfaceVisibilityRangeUVE(
    const SurfaceInstanceComponentUVE& component, float cameraDistance) noexcept;

/// Computes the visibility-range fade weight in `[0, 1]` at `cameraDistance`.
[[nodiscard]] float ComputeSurfaceVisibilityRangeFadeFactorUVE(
    const SurfaceInstanceComponentUVE& component, float cameraDistance) noexcept;

} // namespace UVE::Scene
