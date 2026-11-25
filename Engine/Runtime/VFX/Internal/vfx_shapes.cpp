// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vfx_shapes.h"

#include <algorithm>
#include <cmath>

#include "scalar.h"

namespace UVE::VFX {

Math::Vector3 SampleUniformUnitSphereDirection(Math::Random& rng) noexcept {
    const float z = 1.0F - 2.0F * rng.NextFloat();
    const float phi = 2.0F * Math::kPi * rng.NextFloat();
    const float sinTheta = std::sqrt(std::max(0.0F, 1.0F - z * z));
    return Math::Vector3{sinTheta * std::cos(phi), z, sinTheta * std::sin(phi)};
}

VfxShapeSample SampleShapePoint(Math::Random& rng) noexcept {
    return VfxShapeSample{Math::Vector3{}, SampleUniformUnitSphereDirection(rng)};
}

VfxShapeSample SampleShapeSphere(Math::Random& rng, float radius, const bool surface,
                                       const bool hemisphere) noexcept {
    radius = std::max(0.0F, radius);
    Math::Vector3 dir = SampleUniformUnitSphereDirection(rng);
    if (hemisphere) {
        dir.y = std::abs(dir.y);
    }
    const float distance = surface ? radius : radius * std::cbrt(rng.NextFloat());
    return VfxShapeSample{dir * distance, dir};
}

VfxShapeSample SampleShapeBox(Math::Random& rng, const Math::Vector3& size,
                                    const bool surface) noexcept {
    const float sx = std::max(0.0F, size.x);
    const float sy = std::max(0.0F, size.y);
    const float sz = std::max(0.0F, size.z);
    const float hx = sx * 0.5F;
    const float hy = sy * 0.5F;
    const float hz = sz * 0.5F;

    if (!surface) {
        const Math::Vector3 pos{
            (rng.NextFloat() - 0.5F) * sx,
            (rng.NextFloat() - 0.5F) * sy,
            (rng.NextFloat() - 0.5F) * sz,
        };
        const float nx = hx > 1e-6F ? pos.x / hx : 0.0F;
        const float ny = hy > 1e-6F ? pos.y / hy : 0.0F;
        const float nz = hz > 1e-6F ? pos.z / hz : 0.0F;
        const float ax = std::abs(nx);
        const float ay = std::abs(ny);
        const float az = std::abs(nz);
        Math::Vector3 normal{0.0F, 1.0F, 0.0F};
        if (ax >= ay && ax >= az && ax > 1e-6F) {
            normal = Math::Vector3{nx >= 0.0F ? 1.0F : -1.0F, 0.0F, 0.0F};
        } else if (ay >= ax && ay >= az && ay > 1e-6F) {
            normal = Math::Vector3{0.0F, ny >= 0.0F ? 1.0F : -1.0F, 0.0F};
        } else if (az > 1e-6F) {
            normal = Math::Vector3{0.0F, 0.0F, nz >= 0.0F ? 1.0F : -1.0F};
        }
        return VfxShapeSample{pos, normal};
    }

    const float areaX = sy * sz;
    const float areaY = sx * sz;
    const float areaZ = sx * sy;
    const float totalArea = areaX + areaY + areaZ;
    if (totalArea <= 1e-12F) {
        return SampleShapePoint(rng);
    }

    const float pick = rng.NextFloat() * totalArea;
    const float sign = rng.Chance(0.5F) ? 1.0F : -1.0F;
    const float u = rng.NextFloat() - 0.5F;
    const float v = rng.NextFloat() - 0.5F;

    if (pick < areaX) {
        return VfxShapeSample{Math::Vector3{sign * hx, u * sy, v * sz},
                                 Math::Vector3{sign, 0.0F, 0.0F}};
    }
    if (pick < areaX + areaY) {
        return VfxShapeSample{Math::Vector3{u * sx, sign * hy, v * sz},
                                 Math::Vector3{0.0F, sign, 0.0F}};
    }
    return VfxShapeSample{Math::Vector3{u * sx, v * sy, sign * hz},
                             Math::Vector3{0.0F, 0.0F, sign}};
}

VfxShapeSample SampleShapeCone(Math::Random& rng, float angleDegrees, float length,
                                     float radius, const bool surface) noexcept {
    angleDegrees = std::clamp(angleDegrees, 0.0F, 89.0F);
    length = std::max(0.0F, length);
    const float r0 = std::max(0.0F, radius);
    const float r1 = r0 + length * std::tan(Math::DegToRad(angleDegrees));

    const float u = rng.NextFloat();
    float t = u;
    const float deltaR = r1 - r0;
    if (std::abs(deltaR) > 1e-5F) {
        if (surface) {
            const float rTarget = std::sqrt(Math::Lerp(r0 * r0, r1 * r1, u));
            t = std::clamp((rTarget - r0) / deltaR, 0.0F, 1.0F);
        } else {
            const float rTarget = std::cbrt(Math::Lerp(r0 * r0 * r0, r1 * r1 * r1, u));
            t = std::clamp((rTarget - r0) / deltaR, 0.0F, 1.0F);
        }
    }

    const float sliceRadius = Math::Lerp(r0, r1, t);
    const float radial = surface ? sliceRadius : sliceRadius * std::sqrt(rng.NextFloat());
    const float theta = 2.0F * Math::kPi * rng.NextFloat();
    const float cosT = std::cos(theta);
    const float sinT = std::sin(theta);

    const Math::Vector3 pos{radial * cosT, t * length, radial * sinT};
    Math::Vector3 normal{0.0F, 1.0F, 0.0F};
    if (length > 1e-5F) {
        const Math::Vector3 rawNormal{cosT * length, -deltaR, sinT * length};
        if (Math::LengthSquared(rawNormal) > 1e-12F) {
            normal = Math::Normalize(rawNormal);
        }
    } else if (radial > 1e-6F) {
        normal = Math::Vector3{cosT, 0.0F, sinT};
    }
    return VfxShapeSample{pos, normal};
}

VfxShapeSample SampleShapeCylinder(Math::Random& rng, float radius, float height,
                                         const bool surface) noexcept {
    radius = std::max(0.0F, radius);
    height = std::max(0.0F, height);
    const float y = (rng.NextFloat() - 0.5F) * height;
    const float theta = 2.0F * Math::kPi * rng.NextFloat();
    const float radial = surface ? radius : radius * std::sqrt(rng.NextFloat());
    const float cosT = std::cos(theta);
    const float sinT = std::sin(theta);
    return VfxShapeSample{
        Math::Vector3{radial * cosT, y, radial * sinT},
        Math::Vector3{cosT, 0.0F, sinT},
    };
}

VfxShapeSample SampleShapeTorus(Math::Random& rng, float majorRadius, float minorRadius,
                                      const bool surface) noexcept {
    majorRadius = std::max(0.0F, majorRadius);
    minorRadius = std::max(0.0F, minorRadius);
    const float outer = majorRadius + minorRadius;

    float phi = 0.0F;
    float rho = minorRadius;
    for (int attempt = 0; attempt < 16; ++attempt) {
        phi = 2.0F * Math::kPi * rng.NextFloat();
        rho = surface ? minorRadius : minorRadius * std::sqrt(rng.NextFloat());
        if (outer <= 1e-7F) {
            break;
        }
        const float weight = std::max(0.0F, majorRadius + rho * std::cos(phi)) / outer;
        if (rng.NextFloat() <= weight || attempt == 15) {
            break;
        }
    }

    const float theta = 2.0F * Math::kPi * rng.NextFloat();
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);
    const float cosTheta = std::cos(theta);
    const float sinTheta = std::sin(theta);
    const float ring = majorRadius + rho * cosPhi;

    return VfxShapeSample{
        Math::Vector3{ring * cosTheta, rho * sinPhi, ring * sinTheta},
        Math::Vector3{cosPhi * cosTheta, sinPhi, cosPhi * sinTheta},
    };
}

} // namespace UVE::VFX
