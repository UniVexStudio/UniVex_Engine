// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "path_3d.h"

#include <algorithm>
#include <cmath>

#include "i_entity_manager.h"
#include "vector3.h"
#include "object_3d.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] Math::Quaternion BuildRotationFromForwardAndUp(
    const Math::Vector3& forwardDir, const Math::Vector3& referenceUp,
    Math::Vector3& outRight, Math::Vector3& outUp) noexcept {
    // Object3D forward is -Z, so local +Z points along -forwardDir.
    const Math::Vector3 back =
        (Math::LengthSquared(forwardDir) > 1.0e-8F)
            ? (Math::Normalize(forwardDir) * -1.0F)
            : Math::Vector3{0.0F, 0.0F, 1.0F};
    Math::Vector3 upHint =
        (Math::LengthSquared(referenceUp) > 1.0e-8F)
            ? Math::Normalize(referenceUp)
            : Math::Vector3{0.0F, 1.0F, 0.0F};
    if (std::fabs(Math::Dot(back, upHint)) > 0.999F) {
        upHint = (std::fabs(back.x) < 0.9F) ? Math::Vector3{1.0F, 0.0F, 0.0F}
                                            : Math::Vector3{0.0F, 0.0F, 1.0F};
    }
    outRight = Math::Normalize(Math::Cross(upHint, back));
    outUp = Math::Normalize(Math::Cross(back, outRight));

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
    Math::Quaternion q{};
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
    Math::Quaternion normalized{};
    if (Math::TryNormalize(q, normalized)) {
        return normalized;
    }
    return Math::Quaternion{};
}

} // namespace

bool IsPath3DComponentValid(const Path3DComponent& value) noexcept {
    if (value.points.size() > kMaximumPath3DPoints ||
        !IsFinite3DObjectVector(value.upVector) ||
        Math::LengthSquared(value.upVector) <= 1.0e-6F) {
        return false;
    }
    for (const Math::Vector3& pt : value.points) {
        if (!IsFinite3DObjectVector(pt)) {
            return false;
        }
    }
    return true;
}

Path3DComponent SanitizePath3DComponent(const Path3DComponent& value) {
    Path3DComponent out = value;
    if (out.points.size() > kMaximumPath3DPoints) {
        out.points.resize(kMaximumPath3DPoints);
    }
    for (Math::Vector3& pt : out.points) {
        if (!IsFinite3DObjectVector(pt)) {
            pt = Math::Vector3{0.0F, 0.0F, 0.0F};
        }
    }
    if (!IsFinite3DObjectVector(out.upVector) ||
        Math::LengthSquared(out.upVector) <= 1.0e-6F) {
        out.upVector = Math::Vector3{0.0F, 1.0F, 0.0F};
    }
    return out;
}

bool IsPath3DObjectDefinitionValid(const Path3DObjectDefinition& value) noexcept {
    return IsPath3DComponentValid(value.path);
}

void ApplyPath3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                    const Path3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, Path3DObjectDefinition::defaultName);
    entityManager.AddComponent<Path3DComponent>(entity,
                                                      SanitizePath3DComponent(value.path));
}

float Path3D::ComputeTotalLength(const Path3DComponent& path) noexcept {
    if (!IsPath3DComponentValid(path) || path.points.size() < 2U) {
        return 0.0F;
    }
    float total = 0.0F;
    const std::size_t n = path.points.size();
    for (std::size_t i = 0U; i + 1U < n; ++i) {
        const Math::Vector3 delta = path.points[i + 1U] - path.points[i];
        total += std::sqrt(Math::LengthSquared(delta));
    }
    if (path.closed && n >= 3U) {
        const Math::Vector3 closing = path.points.front() - path.points.back();
        total += std::sqrt(Math::LengthSquared(closing));
    }
    return total;
}

Path3DSampledPose Path3D::SampleLocalPoseAtDistance(const Path3DComponent& path,
                                                             const float distanceMetres,
                                                             const bool loop) noexcept {
    Path3DSampledPose sample{};
    if (!IsPath3DComponentValid(path) || path.points.empty()) {
        return sample;
    }
    if (path.points.size() == 1U) {
        sample.position = path.points.front();
        return sample;
    }
    const float totalLength = ComputeTotalLength(path);
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
        const Math::Vector3& a = path.points[seg];
        const Math::Vector3& b = path.points[(seg + 1U) % n];
        const Math::Vector3 delta = b - a;
        const float segLen = std::sqrt(Math::LengthSquared(delta));
        if (segLen <= 1.0e-6F) {
            continue;
        }
        if (accumulated + segLen >= targetDist || seg + 1U == segmentCount) {
            const float localT = std::clamp((targetDist - accumulated) / segLen, 0.0F, 1.0F);
            sample.position = a + delta * localT;
            sample.tangent = delta * (1.0F / segLen);
            sample.rotation = BuildRotationFromForwardAndUp(sample.tangent, path.upVector,
                                                               sample.right, sample.up);
            return sample;
        }
        accumulated += segLen;
    }
    sample.position = path.points.back();
    return sample;
}

Path3DSampledPose Path3D::SampleWorldPoseAtDistance(
    const Path3DComponent& path, const WorldTransformComponent& pathWorldTransform,
    const float distanceMetres, const bool loop) noexcept {
    Path3DSampledPose localSample = SampleLocalPoseAtDistance(path, distanceMetres, loop);
    if (!IsWorldTransformComponentValid(pathWorldTransform)) {
        return localSample;
    }
    Path3DSampledPose worldSample{};
    worldSample.position = TransformWorldPoint(pathWorldTransform, localSample.position);
    const Math::Vector3 rotatedTangent =
        Math::RotateVector(pathWorldTransform.worldRotation, localSample.tangent);
    worldSample.tangent = (Math::LengthSquared(rotatedTangent) > 1.0e-8F)
                              ? Math::Normalize(rotatedTangent)
                              : Math::Vector3{0.0F, 0.0F, -1.0F};
    const Math::Vector3 rotatedUp =
        Math::RotateVector(pathWorldTransform.worldRotation, path.upVector);
    worldSample.rotation = BuildRotationFromForwardAndUp(
        worldSample.tangent, rotatedUp, worldSample.right, worldSample.up);
    return worldSample;
}

} // namespace UVE::Scene
