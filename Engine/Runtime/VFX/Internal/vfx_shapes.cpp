// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vfx_shapes.h"

#include <algorithm>
#include <cmath>

#include "scalar.h"

namespace UVE::VFX {

Math::Vector3UVE SampleUniformUnitSphereDirectionUVE(Math::RandomUVE& rng) noexcept {
    const float z = 1.0F - 2.0F * rng.NextFloatUVE();
    const float phi = 2.0F * Math::kPiUVE * rng.NextFloatUVE();
    const float sinTheta = std::sqrt(std::max(0.0F, 1.0F - z * z));
    return Math::Vector3UVE{sinTheta * std::cos(phi), z, sinTheta * std::sin(phi)};
}

VfxShapeSampleUVE SampleShapePointUVE(Math::RandomUVE& rng) noexcept {
    return VfxShapeSampleUVE{Math::Vector3UVE{}, SampleUniformUnitSphereDirectionUVE(rng)};
}

VfxShapeSampleUVE SampleShapeSphereUVE(Math::RandomUVE& rng, float radius, const bool surface,
                                       const bool hemisphere) noexcept {
    radius = std::max(0.0F, radius);
    Math::Vector3UVE dir = SampleUniformUnitSphereDirectionUVE(rng);
    if (hemisphere) {
        dir.y = std::abs(dir.y);
    }
    const float distance = surface ? radius : radius * std::cbrt(rng.NextFloatUVE());
    return VfxShapeSampleUVE{dir * distance, dir};
}

VfxShapeSampleUVE SampleShapeBoxUVE(Math::RandomUVE& rng, const Math::Vector3UVE& size,
                                    const bool surface) noexcept {
    const float sx = std::max(0.0F, size.x);
    const float sy = std::max(0.0F, size.y);
    const float sz = std::max(0.0F, size.z);
    const float hx = sx * 0.5F;
    const float hy = sy * 0.5F;
    const float hz = sz * 0.5F;

    if (!surface) {
        const Math::Vector3UVE pos{
            (rng.NextFloatUVE() - 0.5F) * sx,
            (rng.NextFloatUVE() - 0.5F) * sy,
            (rng.NextFloatUVE() - 0.5F) * sz,
        };
        const float nx = hx > 1e-6F ? pos.x / hx : 0.0F;
        const float ny = hy > 1e-6F ? pos.y / hy : 0.0F;
        const float nz = hz > 1e-6F ? pos.z / hz : 0.0F;
        const float ax = std::abs(nx);
        const float ay = std::abs(ny);
        const float az = std::abs(nz);
        Math::Vector3UVE normal{0.0F, 1.0F, 0.0F};
        if (ax >= ay && ax >= az && ax > 1e-6F) {
            normal = Math::Vector3UVE{nx >= 0.0F ? 1.0F : -1.0F, 0.0F, 0.0F};
        } else if (ay >= ax && ay >= az && ay > 1e-6F) {
            normal = Math::Vector3UVE{0.0F, ny >= 0.0F ? 1.0F : -1.0F, 0.0F};
        } else if (az > 1e-6F) {
            normal = Math::Vector3UVE{0.0F, 0.0F, nz >= 0.0F ? 1.0F : -1.0F};
        }
        return VfxShapeSampleUVE{pos, normal};
    }

    const float areaX = sy * sz;
    const float areaY = sx * sz;
    const float areaZ = sx * sy;
    const float totalArea = areaX + areaY + areaZ;
    if (totalArea <= 1e-12F) {
        return SampleShapePointUVE(rng);
    }

    const float pick = rng.NextFloatUVE() * totalArea;
    const float sign = rng.ChanceUVE(0.5F) ? 1.0F : -1.0F;
    const float u = rng.NextFloatUVE() - 0.5F;
    const float v = rng.NextFloatUVE() - 0.5F;

    if (pick < areaX) {
        return VfxShapeSampleUVE{Math::Vector3UVE{sign * hx, u * sy, v * sz},
                                 Math::Vector3UVE{sign, 0.0F, 0.0F}};
    }
    if (pick < areaX + areaY) {
        return VfxShapeSampleUVE{Math::Vector3UVE{u * sx, sign * hy, v * sz},
                                 Math::Vector3UVE{0.0F, sign, 0.0F}};
    }
    return VfxShapeSampleUVE{Math::Vector3UVE{u * sx, v * sy, sign * hz},
                             Math::Vector3UVE{0.0F, 0.0F, sign}};
}

VfxShapeSampleUVE SampleShapeConeUVE(Math::RandomUVE& rng, float angleDegrees, float length,
                                     float radius, const bool surface) noexcept {
    angleDegrees = std::clamp(angleDegrees, 0.0F, 89.0F);
    length = std::max(0.0F, length);
    const float r0 = std::max(0.0F, radius);
    const float r1 = r0 + length * std::tan(Math::DegToRadUVE(angleDegrees));

    const float u = rng.NextFloatUVE();
    float t = u;
    const float deltaR = r1 - r0;
    if (std::abs(deltaR) > 1e-5F) {
        if (surface) {
            const float rTarget = std::sqrt(Math::LerpUVE(r0 * r0, r1 * r1, u));
            t = std::clamp((rTarget - r0) / deltaR, 0.0F, 1.0F);
        } else {
            const float rTarget = std::cbrt(Math::LerpUVE(r0 * r0 * r0, r1 * r1 * r1, u));
            t = std::clamp((rTarget - r0) / deltaR, 0.0F, 1.0F);
        }
    }

    const float sliceRadius = Math::LerpUVE(r0, r1, t);
    const float radial = surface ? sliceRadius : sliceRadius * std::sqrt(rng.NextFloatUVE());
    const float theta = 2.0F * Math::kPiUVE * rng.NextFloatUVE();
    const float cosT = std::cos(theta);
    const float sinT = std::sin(theta);

    const Math::Vector3UVE pos{radial * cosT, t * length, radial * sinT};
    Math::Vector3UVE normal{0.0F, 1.0F, 0.0F};
    if (length > 1e-5F) {
        const Math::Vector3UVE rawNormal{cosT * length, -deltaR, sinT * length};
        if (Math::LengthSquaredUVE(rawNormal) > 1e-12F) {
            normal = Math::NormalizeUVE(rawNormal);
        }
    } else if (radial > 1e-6F) {
        normal = Math::Vector3UVE{cosT, 0.0F, sinT};
    }
    return VfxShapeSampleUVE{pos, normal};
}

VfxShapeSampleUVE SampleShapeCylinderUVE(Math::RandomUVE& rng, float radius, float height,
                                         const bool surface) noexcept {
    radius = std::max(0.0F, radius);
    height = std::max(0.0F, height);
    const float y = (rng.NextFloatUVE() - 0.5F) * height;
    const float theta = 2.0F * Math::kPiUVE * rng.NextFloatUVE();
    const float radial = surface ? radius : radius * std::sqrt(rng.NextFloatUVE());
    const float cosT = std::cos(theta);
    const float sinT = std::sin(theta);
    return VfxShapeSampleUVE{
        Math::Vector3UVE{radial * cosT, y, radial * sinT},
        Math::Vector3UVE{cosT, 0.0F, sinT},
    };
}

VfxShapeSampleUVE SampleShapeTorusUVE(Math::RandomUVE& rng, float majorRadius, float minorRadius,
                                      const bool surface) noexcept {
    majorRadius = std::max(0.0F, majorRadius);
    minorRadius = std::max(0.0F, minorRadius);
    const float outer = majorRadius + minorRadius;

    float phi = 0.0F;
    float rho = minorRadius;
    for (int attempt = 0; attempt < 16; ++attempt) {
        phi = 2.0F * Math::kPiUVE * rng.NextFloatUVE();
        rho = surface ? minorRadius : minorRadius * std::sqrt(rng.NextFloatUVE());
        if (outer <= 1e-7F) {
            break;
        }
        const float weight = std::max(0.0F, majorRadius + rho * std::cos(phi)) / outer;
        if (rng.NextFloatUVE() <= weight || attempt == 15) {
            break;
        }
    }

    const float theta = 2.0F * Math::kPiUVE * rng.NextFloatUVE();
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);
    const float cosTheta = std::cos(theta);
    const float sinTheta = std::sin(theta);
    const float ring = majorRadius + rho * cosPhi;

    return VfxShapeSampleUVE{
        Math::Vector3UVE{ring * cosTheta, rho * sinPhi, ring * sinTheta},
        Math::Vector3UVE{cosPhi * cosTheta, sinPhi, cosPhi * sinTheta},
    };
}

} // namespace UVE::VFX
