// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "contact_shadows.h"

namespace UVE::Render {

std::uint32_t FillContactShadowRaySteps(const ContactShadowSettings& settings, float* steps,
                                           const std::uint32_t capacity) noexcept {
    if (steps == nullptr || capacity == 0U) {
        return 0U;
    }
    const ContactShadowSettings clamped = SanitizeContactShadowSettings(settings);
    const std::uint32_t stepCount = clamped.stepCount < capacity ? clamped.stepCount : capacity;
    const float inverseStepCount = 1.0F / static_cast<float>(stepCount);
    for (std::uint32_t stepIndex = 0U; stepIndex < stepCount; ++stepIndex) {
        const float fraction = static_cast<float>(stepIndex + 1U) * inverseStepCount;
        steps[stepIndex] = clamped.lengthMeters * fraction * fraction;
    }
    return stepCount;
}

} // namespace UVE::Render
