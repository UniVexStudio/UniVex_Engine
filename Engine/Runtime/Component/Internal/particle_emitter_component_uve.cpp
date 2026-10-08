// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/particle_emitter_component_uve.h"

#include <cmath>
#include <cstdint>

namespace UVE::Scene {

[[nodiscard]] bool IsParticleEmitterComponentValidUVE(
    const ParticleEmitterComponentUVE& component) noexcept {
    return component.maxParticles > 0U && component.maxParticles <= kMaximumParticleEmitterParticlesUVE &&
           std::isfinite(component.emissionRate) && component.emissionRate >= 0.0F &&
           component.emissionRate <= kMaximumParticleEmitterEmissionRateUVE &&
           std::isfinite(component.lifetimeSeconds) && component.lifetimeSeconds > 0.0F &&
           component.lifetimeSeconds <= kMaximumParticleEmitterLifetimeSecondsUVE;
}

std::uint32_t ConsumeParticleEmitterAutoEmitCountUVE(float& remainder,
                                                     const ParticleEmitterComponentUVE& component,
                                                     const float deltaSeconds,
                                                     const std::uint32_t liveParticles) noexcept {
    if (!component.emitting || !std::isfinite(component.emissionRate) || component.emissionRate <= 0.0F ||
        !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
        return 0U;
    }
    if (liveParticles >= component.maxParticles) {
        if (std::isfinite(remainder) && remainder >= 1.0F) {
            remainder -= std::floor(remainder);
        } else if (!std::isfinite(remainder) || remainder < 0.0F) {
            remainder = 0.0F;
        }
        return 0U;
    }

    remainder += component.emissionRate * deltaSeconds;
    if (!std::isfinite(remainder) || remainder < 0.0F) {
        remainder = 0.0F;
        return 0U;
    }
    const std::uint32_t remainingBudget = component.maxParticles - liveParticles;
    const float remainingBudgetF = static_cast<float>(remainingBudget);
    if (remainder > remainingBudgetF) {
        remainder = remainingBudgetF;
    }
    const auto count = static_cast<std::uint32_t>(remainder);
    remainder -= static_cast<float>(count);
    return count;
}

} // namespace UVE::Scene
