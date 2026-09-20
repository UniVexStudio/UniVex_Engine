// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/particle_emitter_component_uve.h"

#include <cstdint>

namespace UVE::Scene {

[[nodiscard]] bool IsParticleEmitterComponentValidUVE(
    const ParticleEmitterComponentUVE& component) noexcept {
    return component.maxParticles > 0U && component.maxParticles <= kMaximumParticleEmitterParticlesUVE;
}

} // namespace UVE::Scene
