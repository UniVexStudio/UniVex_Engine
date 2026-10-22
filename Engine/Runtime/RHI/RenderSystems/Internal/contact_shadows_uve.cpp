// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/render_systems/contact_shadows_uve.h"

namespace UVE::Render {

std::uint32_t FillContactShadowRayStepsUVE(const ContactShadowSettingsUVE& settings, float* steps,
                                           const std::uint32_t capacity) noexcept {
    if (steps == nullptr || capacity == 0U) {
        return 0U;
    }
    const ContactShadowSettingsUVE clamped = SanitizeContactShadowSettingsUVE(settings);
    const std::uint32_t stepCount = clamped.stepCountUVE < capacity ? clamped.stepCountUVE : capacity;
    const float inverseStepCount = 1.0F / static_cast<float>(stepCount);
    for (std::uint32_t stepIndex = 0U; stepIndex < stepCount; ++stepIndex) {
        const float fraction = static_cast<float>(stepIndex + 1U) * inverseStepCount;
        steps[stepIndex] = clamped.lengthMetersUVE * fraction * fraction;
    }
    return stepCount;
}

} // namespace UVE::Render
