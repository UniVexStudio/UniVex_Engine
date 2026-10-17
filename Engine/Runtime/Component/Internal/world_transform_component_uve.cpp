// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/world_transform_component_uve.h"

#include <cmath>

namespace UVE::Scene {

bool IsWorldTransformComponentValidUVE(const WorldTransformComponentUVE& transform) noexcept {
    return std::isfinite(transform.worldPosition.x) && std::isfinite(transform.worldPosition.y) &&
           std::isfinite(transform.worldPosition.z) && Math::IsFiniteUVE(transform.worldRotation) &&
           std::isfinite(transform.worldScale.x) && std::isfinite(transform.worldScale.y) &&
           std::isfinite(transform.worldScale.z);
}

bool HasSameWorldPoseUVE(const WorldTransformComponentUVE& lhs,
                         const WorldTransformComponentUVE& rhs) noexcept {
    return lhs.worldPosition == rhs.worldPosition && lhs.worldRotation == rhs.worldRotation &&
           lhs.worldScale == rhs.worldScale;
}

Math::TrsUVE ToWorldTrsUVE(const WorldTransformComponentUVE& transform) noexcept {
    return Math::TrsUVE{transform.worldPosition, transform.worldRotation, transform.worldScale};
}

WorldTransformComponentUVE FromWorldTrsUVE(const Math::TrsUVE& trs, const bool dirty) noexcept {
    return WorldTransformComponentUVE{
        .worldPosition = trs.translation,
        .worldRotation = trs.rotation,
        .worldScale = trs.scale,
        .dirty = dirty,
    };
}

WorldTransformComponentUVE MakeWorldTransformFromLocalUVE(const TransformComponentUVE& local,
                                                          const bool dirty) noexcept {
    return WorldTransformComponentUVE{
        .worldPosition = local.localPosition,
        .worldRotation = local.localRotation,
        .worldScale = local.localScale,
        .dirty = dirty,
    };
}

WorldTransformComponentUVE ComposeWorldTransformUVE(const WorldTransformComponentUVE& parentWorld,
                                                    const TransformComponentUVE& local,
                                                    const bool dirty) noexcept {
    const Math::TrsUVE composed = Math::ComposeUVE(
        ToWorldTrsUVE(parentWorld),
        Math::TrsUVE{local.localPosition, local.localRotation, local.localScale});
    return FromWorldTrsUVE(composed, dirty);
}

Math::Matrix4x4UVE BuildWorldTransformMatrixUVE(
    const WorldTransformComponentUVE& transform) noexcept {
    return Math::Matrix4x4UVE::ComposeTrsUVE(transform.worldPosition, transform.worldRotation,
                                             transform.worldScale);
}

Math::Vector3UVE TransformWorldPointUVE(const WorldTransformComponentUVE& transform,
                                        const Math::Vector3UVE& localPoint) noexcept {
    return Math::TransformPointUVE(ToWorldTrsUVE(transform), localPoint);
}

bool TryInverseTransformWorldPointUVE(const WorldTransformComponentUVE& transform,
                                      const Math::Vector3UVE& worldPoint,
                                      Math::Vector3UVE& outLocalPoint) noexcept {
    return Math::TryInverseTransformPointUVE(ToWorldTrsUVE(transform), worldPoint, outLocalPoint);
}

Math::Vector3UVE ResolveWorldForwardUVE(const WorldTransformComponentUVE& transform) noexcept {
    constexpr Math::Vector3UVE kForward{0.0F, 0.0F, -1.0F};
    Math::QuaternionUVE normalized{};
    if (!Math::TryNormalizeUVE(transform.worldRotation, normalized)) {
        return kForward;
    }
    return Math::RotateVectorUVE(normalized, kForward);
}

Math::Vector3UVE ResolveWorldRightUVE(const WorldTransformComponentUVE& transform) noexcept {
    constexpr Math::Vector3UVE kRight{1.0F, 0.0F, 0.0F};
    Math::QuaternionUVE normalized{};
    if (!Math::TryNormalizeUVE(transform.worldRotation, normalized)) {
        return kRight;
    }
    return Math::RotateVectorUVE(normalized, kRight);
}

Math::Vector3UVE ResolveWorldUpUVE(const WorldTransformComponentUVE& transform) noexcept {
    constexpr Math::Vector3UVE kUp{0.0F, 1.0F, 0.0F};
    Math::QuaternionUVE normalized{};
    if (!Math::TryNormalizeUVE(transform.worldRotation, normalized)) {
        return kUp;
    }
    return Math::RotateVectorUVE(normalized, kUp);
}

} // namespace UVE::Scene
