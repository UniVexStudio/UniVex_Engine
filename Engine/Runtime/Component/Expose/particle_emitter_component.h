// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace UVE::Scene {

inline constexpr std::uint32_t kMaximumParticleEmitterParticles = 1'000'000U;
inline constexpr float kMaximumParticleEmitterEmissionRate = 1'000'000.0F;
inline constexpr float kMaximumParticleEmitterLifetimeSeconds = 3600.0F;
inline constexpr std::size_t kMaximumParticleSystemAssetPathBytes = 512U;

/// Authored ParticleEmitter3D state. `maxParticles` is the live budget the runtime allocates
/// against. `emitting`, `emissionRate`, and `lifetimeSeconds` are what make dropping the object
/// spawn particles without a script calling Emit — EngineCore consumes them each frame.
/// When `systemAssetPath` is non-empty, the entity plays the `.uvfx` Particle System through
/// VfxWorld instead of the legacy fixed-quad runtime.
struct ParticleEmitterComponent final {
    std::uint32_t maxParticles = 100;
    bool emitting = true;
    float emissionRate = 10.0F;
    float lifetimeSeconds = 1.0F;
    std::string systemAssetPath{};
    bool autoPlay = true;
    std::uint32_t seed = 0U;
};

[[nodiscard]] bool IsParticleEmitterComponentValid(
    const ParticleEmitterComponent& component) noexcept;

/// How many particles this frame's rate and leftover fraction produce, never more than the
/// remaining budget. `remainder` is the unused fraction of a particle, carried across frames so
/// 10/s at 60 Hz is one spawn every six frames rather than zero forever. A full budget keeps the
/// fraction under 1 so a death next frame can emit immediately.
[[nodiscard]] std::uint32_t ConsumeParticleEmitterAutoEmitCount(
    float& remainder, const ParticleEmitterComponent& component, float deltaSeconds,
    std::uint32_t liveParticles) noexcept;

} // namespace UVE::Scene
