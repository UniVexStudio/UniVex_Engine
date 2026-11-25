// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "math_random.h"
#include "vector3.h"

namespace UVE::VFX {

/// Result of sampling a spawn shape in emitter-local space: position plus outward unit normal.
struct VfxShapeSample final {
    Math::Vector3 position{};
    Math::Vector3 normal{0.0F, 1.0F, 0.0F};
};

/// Draws a uniformly distributed unit direction on S^2.
[[nodiscard]] Math::Vector3 SampleUniformUnitSphereDirection(Math::Random& rng) noexcept;

/// Point shape at the emitter origin.
[[nodiscard]] VfxShapeSample SampleShapePoint(Math::Random& rng) noexcept;

/// Sphere (or +Y hemisphere) volume or surface sampling.
[[nodiscard]] VfxShapeSample SampleShapeSphere(Math::Random& rng, float radius, bool surface,
                                                     bool hemisphere) noexcept;

/// Box centered at the origin with side extents `size` (volume or surface).
[[nodiscard]] VfxShapeSample SampleShapeBox(Math::Random& rng, const Math::Vector3& size,
                                                  bool surface) noexcept;

/// Truncated cone along +Y from `y = 0` (`radius`) to `y = length` (`radius + length * tan(angle)`).
[[nodiscard]] VfxShapeSample SampleShapeCone(Math::Random& rng, float angleDegrees, float length,
                                                   float radius, bool surface) noexcept;

/// Cylinder centered at the origin with radius `radius` and height `height` along Y.
[[nodiscard]] VfxShapeSample SampleShapeCylinder(Math::Random& rng, float radius, float height,
                                                       bool surface) noexcept;

/// Torus centered at the origin in the XZ plane with major and minor radii.
[[nodiscard]] VfxShapeSample SampleShapeTorus(Math::Random& rng, float majorRadius, float minorRadius,
                                                    bool surface) noexcept;

} // namespace UVE::VFX
