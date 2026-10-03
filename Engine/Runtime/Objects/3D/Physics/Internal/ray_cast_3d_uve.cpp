// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/ray_cast_3d_uve.h"

namespace UVE::Scene {

bool IsRayCast3DObjectComponentValidUVE(const RayCast3DComponentUVE& value) noexcept {
    const float directionLengthSquared = Math::LengthSquaredUVE(value.direction);
    if (!IsFinite3DObjectVectorUVE(value.direction) || !std::isfinite(directionLengthSquared) ||
        directionLengthSquared <= 1.0e-8F || !std::isfinite(value.length) || value.length <= 0.0F ||
        value.exclusionCount > kMaximumRayCastExclusionsUVE) {
        return false;
    }
    for (std::size_t index = 0U; index < value.exclusionCount; ++index) {
        if (value.exclusions[index] == std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (value.exclusions[previous] == value.exclusions[index]) {
                return false;
            }
        }
    }
    return !value.hit || (value.hitEntity != kInvalidEntityUVE && IsFinite3DObjectVectorUVE(value.hitPosition) &&
                           IsFinite3DObjectVectorUVE(value.hitNormal));
}

} // namespace UVE::Scene
