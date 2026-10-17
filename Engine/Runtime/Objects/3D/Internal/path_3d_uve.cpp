// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/path_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] Math::QuaternionUVE BuildRotationFromForwardAndUpUVE(
    const Math::Vector3UVE& forwardDir, const Math::Vector3UVE& referenceUp,
    Math::Vector3UVE& outRight, Math::Vector3UVE& outUp) noexcept {
    // Object3D forward is -Z, so local +Z points along -forwardDir.
    const Math::Vector3UVE back =
        (Math::LengthSquaredUVE(forwardDir) > 1.0e-8F)
            ? (Math::NormalizeUVE(forwardDir) * -1.0F)
            : Math::Vector3UVE{0.0F, 0.0F, 1.0F};
    Math::Vector3UVE upHint =
        (Math::LengthSquaredUVE(referenceUp) > 1.0e-8F)
            ? Math::NormalizeUVE(referenceUp)
            : Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    if (std::fabs(Math::DotUVE(back, upHint)) > 0.999F) {
        upHint = (std::fabs(back.x) < 0.9F) ? Math::Vector3UVE{1.0F, 0.0F, 0.0F}
                                            : Math::Vector3UVE{0.0F, 0.0F, 1.0F};
    }
    outRight = Math::NormalizeUVE(Math::CrossUVE(upHint, back));
    outUp = Math::NormalizeUVE(Math::CrossUVE(back, outRight));

    // Convert 3x3 basis [outRight, outUp, back] into a unit quaternion.
    const float m00 = outRight.x;
    const float m01 = outUp.x;
    const float m02 = back.x;
    const float m10 = outRight.y;
    const float m11 = outUp.y;
    const float m12 = back.y;
    const float m20 = outRight.z;
    const float m21 = outUp.z;
    const float m22 = back.z;
    const float trace = m00 + m11 + m22;
    Math::QuaternionUVE q{};
    if (trace > 0.0F) {
        const float s = std::sqrt(trace + 1.0F) * 2.0F;
        q.w = 0.25F * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0F + m00 - m11 - m22) * 2.0F;
        q.w = (m21 - m12) / s;
        q.x = 0.25F * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0F + m11 - m00 - m22) * 2.0F;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25F * s;
        q.z = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.0F + m22 - m00 - m11) * 2.0F;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25F * s;
    }
    Math::QuaternionUVE normalized{};
    if (Math::TryNormalizeUVE(q, normalized)) {
        return normalized;
    }
    return Math::QuaternionUVE{};
}

} // namespace

bool IsPath3DComponentValidUVE(const Path3DComponentUVE& value) noexcept {
    if (value.points.size() > kMaximumPath3DPointsUVE ||
        !IsFinite3DObjectVectorUVE(value.upVector) ||
        Math::LengthSquaredUVE(value.upVector) <= 1.0e-6F) {
        return false;
    }
    for (const Math::Vector3UVE& pt : value.points) {
        if (!IsFinite3DObjectVectorUVE(pt)) {
            return false;
        }
    }
    return true;
}

Path3DComponentUVE SanitizePath3DComponentUVE(const Path3DComponentUVE& value) {
    Path3DComponentUVE out = value;
    if (out.points.size() > kMaximumPath3DPointsUVE) {
        out.points.resize(kMaximumPath3DPointsUVE);
    }
    for (Math::Vector3UVE& pt : out.points) {
        if (!IsFinite3DObjectVectorUVE(pt)) {
            pt = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
        }
    }
    if (!IsFinite3DObjectVectorUVE(out.upVector) ||
        Math::LengthSquaredUVE(out.upVector) <= 1.0e-6F) {
        out.upVector = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    }
    return out;
}

bool IsPath3DObjectDefinitionValidUVE(const Path3DObjectDefinitionUVE& value) noexcept {
    return IsPath3DComponentValidUVE(value.path);
}

void ApplyPath3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                    const Path3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, Path3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<Path3DComponentUVE>(entity,
                                                      SanitizePath3DComponentUVE(value.path));
}

float Path3DUVE::ComputeTotalLengthUVE(const Path3DComponentUVE& path) noexcept {
    if (!IsPath3DComponentValidUVE(path) || path.points.size() < 2U) {
        return 0.0F;
    }
    float total = 0.0F;
    const std::size_t n = path.points.size();
    for (std::size_t i = 0U; i + 1U < n; ++i) {
        const Math::Vector3UVE delta = path.points[i + 1U] - path.points[i];
        total += std::sqrt(Math::LengthSquaredUVE(delta));
    }
    if (path.closed && n >= 3U) {
        const Math::Vector3UVE closing = path.points.front() - path.points.back();
        total += std::sqrt(Math::LengthSquaredUVE(closing));
    }
    return total;
}

Path3DSampledPoseUVE Path3DUVE::SampleLocalPoseAtDistanceUVE(const Path3DComponentUVE& path,
                                                             const float distanceMetres,
                                                             const bool loop) noexcept {
    Path3DSampledPoseUVE sample{};
    if (!IsPath3DComponentValidUVE(path) || path.points.empty()) {
        return sample;
    }
    if (path.points.size() == 1U) {
        sample.position = path.points.front();
        return sample;
    }
    const float totalLength = ComputeTotalLengthUVE(path);
    if (totalLength <= 1.0e-6F || !std::isfinite(distanceMetres)) {
        sample.position = path.points.front();
        return sample;
    }

    float targetDist = distanceMetres;
    if (loop || path.closed) {
        targetDist = std::fmod(distanceMetres, totalLength);
        if (targetDist < 0.0F) {
            targetDist += totalLength;
        }
    } else {
        targetDist = std::clamp(distanceMetres, 0.0F, totalLength);
    }

    const std::size_t n = path.points.size();
    const std::size_t segmentCount = (path.closed && n >= 3U) ? n : (n - 1U);
    float accumulated = 0.0F;
    for (std::size_t seg = 0U; seg < segmentCount; ++seg) {
        const Math::Vector3UVE& a = path.points[seg];
        const Math::Vector3UVE& b = path.points[(seg + 1U) % n];
        const Math::Vector3UVE delta = b - a;
        const float segLen = std::sqrt(Math::LengthSquaredUVE(delta));
        if (segLen <= 1.0e-6F) {
            continue;
        }
        if (accumulated + segLen >= targetDist || seg + 1U == segmentCount) {
            const float localT = std::clamp((targetDist - accumulated) / segLen, 0.0F, 1.0F);
            sample.position = a + delta * localT;
            sample.tangent = delta * (1.0F / segLen);
            sample.rotation = BuildRotationFromForwardAndUpUVE(sample.tangent, path.upVector,
                                                               sample.right, sample.up);
            return sample;
        }
        accumulated += segLen;
    }
    sample.position = path.points.back();
    return sample;
}

Path3DSampledPoseUVE Path3DUVE::SampleWorldPoseAtDistanceUVE(
    const Path3DComponentUVE& path, const WorldTransformComponentUVE& pathWorldTransform,
    const float distanceMetres, const bool loop) noexcept {
    Path3DSampledPoseUVE localSample = SampleLocalPoseAtDistanceUVE(path, distanceMetres, loop);
    if (!IsWorldTransformComponentValidUVE(pathWorldTransform)) {
        return localSample;
    }
    Path3DSampledPoseUVE worldSample{};
    worldSample.position = TransformWorldPointUVE(pathWorldTransform, localSample.position);
    const Math::Vector3UVE rotatedTangent =
        Math::RotateVectorUVE(pathWorldTransform.worldRotation, localSample.tangent);
    worldSample.tangent = (Math::LengthSquaredUVE(rotatedTangent) > 1.0e-8F)
                              ? Math::NormalizeUVE(rotatedTangent)
                              : Math::Vector3UVE{0.0F, 0.0F, -1.0F};
    const Math::Vector3UVE rotatedUp =
        Math::RotateVectorUVE(pathWorldTransform.worldRotation, path.upVector);
    worldSample.rotation = BuildRotationFromForwardAndUpUVE(
        worldSample.tangent, rotatedUp, worldSample.right, worldSample.up);
    return worldSample;
}

} // namespace UVE::Scene
