// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>

namespace UVE::Scene {

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
    /// Fades the nodes that stand in for it at other ranges.
    Dependencies,
};

/// The shared state of SurfaceInstance3D, the abstract base (under RenderInstance3D) of every
/// node that draws geometry - meshes, primitives, particles. No node is a SurfaceInstance3D on its
/// own; its kinds carry this component and add their own.
struct SurfaceInstanceComponentUVE final {
    /// Replaces every surface's material when set. Project-relative, empty for none.
    std::string materialOverridePath;
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

/// Known enumerators; finite values; transparency within [0, 1]; a positive LOD bias; no negative
/// margin or distance; and a range end, when set, not before its beginning.
[[nodiscard]] bool IsSurfaceInstanceComponentValidUVE(const SurfaceInstanceComponentUVE& component) noexcept;

} // namespace UVE::Scene
