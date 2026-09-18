// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/transform_component_uve.h"

#include <cmath>

namespace UVE::Scene {

[[nodiscard]] bool IsTransformComponentValidUVE(const TransformComponentUVE& transform) noexcept {
    const auto isFiniteVector = [](const Math::Vector3UVE& value) noexcept {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    if (!isFiniteVector(transform.localPosition) || !isFiniteVector(transform.localScale) ||
        !Math::IsFiniteUVE(transform.localRotation)) {
        return false;
    }
    const float rotationLengthSquared = Math::LengthSquaredUVE(transform.localRotation);
    return std::isfinite(rotationLengthSquared) &&
           std::abs(rotationLengthSquared - 1.0F) <= 1.0e-3F;
}

} // namespace UVE::Scene
