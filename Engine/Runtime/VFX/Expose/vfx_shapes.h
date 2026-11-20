// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "math_random.h"
#include "vector3.h"

namespace UVE::VFX {

/// Result of sampling a spawn shape in emitter-local space: position plus outward unit normal.
struct VfxShapeSampleUVE final {
    Math::Vector3UVE position{};
    Math::Vector3UVE normal{0.0F, 1.0F, 0.0F};
};

/// Draws a uniformly distributed unit direction on S^2.
[[nodiscard]] Math::Vector3UVE SampleUniformUnitSphereDirectionUVE(Math::RandomUVE& rng) noexcept;

/// Point shape at the emitter origin.
[[nodiscard]] VfxShapeSampleUVE SampleShapePointUVE(Math::RandomUVE& rng) noexcept;

/// Sphere (or +Y hemisphere) volume or surface sampling.
[[nodiscard]] VfxShapeSampleUVE SampleShapeSphereUVE(Math::RandomUVE& rng, float radius, bool surface,
                                                     bool hemisphere) noexcept;

/// Box centered at the origin with side extents `size` (volume or surface).
[[nodiscard]] VfxShapeSampleUVE SampleShapeBoxUVE(Math::RandomUVE& rng, const Math::Vector3UVE& size,
                                                  bool surface) noexcept;

/// Truncated cone along +Y from `y = 0` (`radius`) to `y = length` (`radius + length * tan(angle)`).
[[nodiscard]] VfxShapeSampleUVE SampleShapeConeUVE(Math::RandomUVE& rng, float angleDegrees, float length,
                                                   float radius, bool surface) noexcept;

/// Cylinder centered at the origin with radius `radius` and height `height` along Y.
[[nodiscard]] VfxShapeSampleUVE SampleShapeCylinderUVE(Math::RandomUVE& rng, float radius, float height,
                                                       bool surface) noexcept;

/// Torus centered at the origin in the XZ plane with major and minor radii.
[[nodiscard]] VfxShapeSampleUVE SampleShapeTorusUVE(Math::RandomUVE& rng, float majorRadius, float minorRadius,
                                                    bool surface) noexcept;

} // namespace UVE::VFX
