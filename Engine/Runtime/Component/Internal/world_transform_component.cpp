// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "world_transform_component.h"

#include <cmath>

namespace UVE::Scene {

bool IsWorldTransformComponentValid(const WorldTransformComponent& transform) noexcept {
    return std::isfinite(transform.worldPosition.x) && std::isfinite(transform.worldPosition.y) &&
           std::isfinite(transform.worldPosition.z) && Math::IsFinite(transform.worldRotation) &&
           std::isfinite(transform.worldScale.x) && std::isfinite(transform.worldScale.y) &&
           std::isfinite(transform.worldScale.z);
}

bool HasSameWorldPose(const WorldTransformComponent& lhs,
                         const WorldTransformComponent& rhs) noexcept {
    return lhs.worldPosition == rhs.worldPosition && lhs.worldRotation == rhs.worldRotation &&
           lhs.worldScale == rhs.worldScale;
}

Math::Trs ToWorldTrs(const WorldTransformComponent& transform) noexcept {
    return Math::Trs{transform.worldPosition, transform.worldRotation, transform.worldScale};
}

WorldTransformComponent FromWorldTrs(const Math::Trs& trs, const bool dirty) noexcept {
    return WorldTransformComponent{
        .worldPosition = trs.translation,
        .worldRotation = trs.rotation,
        .worldScale = trs.scale,
        .dirty = dirty,
    };
}

WorldTransformComponent MakeWorldTransformFromLocal(const TransformComponent& local,
                                                          const bool dirty) noexcept {
    return WorldTransformComponent{
        .worldPosition = local.localPosition,
        .worldRotation = local.localRotation,
        .worldScale = local.localScale,
        .dirty = dirty,
    };
}

WorldTransformComponent ComposeWorldTransform(const WorldTransformComponent& parentWorld,
                                                    const TransformComponent& local,
                                                    const bool dirty) noexcept {
    const Math::Trs composed = Math::Compose(
        ToWorldTrs(parentWorld),
        Math::Trs{local.localPosition, local.localRotation, local.localScale});
    return FromWorldTrs(composed, dirty);
}

Math::Matrix4x4 BuildWorldTransformMatrix(
    const WorldTransformComponent& transform) noexcept {
    return Math::Matrix4x4::ComposeTrs(transform.worldPosition, transform.worldRotation,
                                             transform.worldScale);
}

Math::Vector3 TransformWorldPoint(const WorldTransformComponent& transform,
                                        const Math::Vector3& localPoint) noexcept {
    return Math::TransformPoint(ToWorldTrs(transform), localPoint);
}

bool TryInverseTransformWorldPoint(const WorldTransformComponent& transform,
                                      const Math::Vector3& worldPoint,
                                      Math::Vector3& outLocalPoint) noexcept {
    return Math::TryInverseTransformPoint(ToWorldTrs(transform), worldPoint, outLocalPoint);
}

Math::Vector3 ResolveWorldForward(const WorldTransformComponent& transform) noexcept {
    constexpr Math::Vector3 kForward{0.0F, 0.0F, -1.0F};
    Math::Quaternion normalized{};
    if (!Math::TryNormalize(transform.worldRotation, normalized)) {
        return kForward;
    }
    return Math::RotateVector(normalized, kForward);
}

Math::Vector3 ResolveWorldRight(const WorldTransformComponent& transform) noexcept {
    constexpr Math::Vector3 kRight{1.0F, 0.0F, 0.0F};
    Math::Quaternion normalized{};
    if (!Math::TryNormalize(transform.worldRotation, normalized)) {
        return kRight;
    }
    return Math::RotateVector(normalized, kRight);
}

Math::Vector3 ResolveWorldUp(const WorldTransformComponent& transform) noexcept {
    constexpr Math::Vector3 kUp{0.0F, 1.0F, 0.0F};
    Math::Quaternion normalized{};
    if (!Math::TryNormalize(transform.worldRotation, normalized)) {
        return kUp;
    }
    return Math::RotateVector(normalized, kUp);
}

} // namespace UVE::Scene
