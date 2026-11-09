// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>

namespace UVE::Scene {

inline constexpr std::uint32_t kMaximumParticleEmitterParticlesUVE = 1'000'000U;
inline constexpr float kMaximumParticleEmitterEmissionRateUVE = 1'000'000.0F;
inline constexpr float kMaximumParticleEmitterLifetimeSecondsUVE = 3600.0F;

/// Authored ParticleEmitter3D state. `maxParticles` is the live budget the runtime allocates
/// against. `emitting`, `emissionRate`, and `lifetimeSeconds` are what make dropping the object
/// spawn particles without a script calling Emit — EngineCoreUVE consumes them each frame.
struct ParticleEmitterComponentUVE final {
    std::uint32_t maxParticles = 100;
    bool emitting = true;
    float emissionRate = 10.0F;
    float lifetimeSeconds = 1.0F;
};

[[nodiscard]] bool IsParticleEmitterComponentValidUVE(
    const ParticleEmitterComponentUVE& component) noexcept;

/// How many particles this frame's rate and leftover fraction produce, never more than the
/// remaining budget. `remainder` is the unused fraction of a particle, carried across frames so
/// 10/s at 60 Hz is one spawn every six frames rather than zero forever. A full budget keeps the
/// fraction under 1 so a death next frame can emit immediately.
[[nodiscard]] std::uint32_t ConsumeParticleEmitterAutoEmitCountUVE(
    float& remainder, const ParticleEmitterComponentUVE& component, float deltaSeconds,
    std::uint32_t liveParticles) noexcept;

} // namespace UVE::Scene
