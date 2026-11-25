// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "audio_listener_orientation_validation.h"

#include <cmath>

namespace UVE::Audio {
namespace {

[[nodiscard]] bool IsFiniteNonZeroVector(const Math::Vector3 value) noexcept {
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
        return false;
    }
    const float lengthSquared = Math::LengthSquared(value);
    return std::isfinite(lengthSquared) && lengthSquared > 0.0F;
}

} // namespace

bool IsAudioListenerOrientationValid(const Math::Vector3 forward,
                                       const Math::Vector3 up) noexcept {
    return IsFiniteNonZeroVector(forward) && IsFiniteNonZeroVector(up);
}

} // namespace UVE::Audio
