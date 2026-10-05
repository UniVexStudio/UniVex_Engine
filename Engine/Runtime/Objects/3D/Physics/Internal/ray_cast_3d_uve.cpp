// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/ray_cast_3d_uve.h"

namespace UVE::Scene {

std::size_t CountRayCast3DExclusionsUVE(const RayCast3DComponentUVE& value) noexcept {
    std::size_t count = 0U;
    while (count < value.exclusions.size() && value.exclusions[count] != kInvalidEntityUVE) {
        ++count;
    }
    return count;
}

bool IsRayCast3DObjectComponentValidUVE(const RayCast3DComponentUVE& value) noexcept {
    const float directionLengthSquared = Math::LengthSquaredUVE(value.direction);
    if (!IsFinite3DObjectVectorUVE(value.direction) || !std::isfinite(directionLengthSquared) ||
        directionLengthSquared <= 1.0e-8F || !std::isfinite(value.length) || value.length <= 0.0F) {
        return false;
    }

    const std::size_t exclusionCount = CountRayCast3DExclusionsUVE(value);
    for (std::size_t index = 0U; index < exclusionCount; ++index) {
        // Duplicates are refused rather than collapsed: two identical slots are an authoring
        // mistake, and silently dropping one would hide which of the two was meant.
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (value.exclusions[previous] == value.exclusions[index]) {
                return false;
            }
        }
    }
    // Everything past the first empty slot must be empty too. The list is a prefix, and a live
    // reference hiding behind an empty slot would be authored data no query would ever honour -
    // refused loudly here rather than silently ignored everywhere else.
    for (std::size_t index = exclusionCount; index < value.exclusions.size(); ++index) {
        if (value.exclusions[index] != kInvalidEntityUVE) {
            return false;
        }
    }

    return !value.hit || (value.hitEntity != kInvalidEntityUVE && IsFinite3DObjectVectorUVE(value.hitPosition) &&
                           IsFinite3DObjectVectorUVE(value.hitNormal));
}

} // namespace UVE::Scene
