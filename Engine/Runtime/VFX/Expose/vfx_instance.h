// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aabb.h"
#include "color.h"
#include "particle_system_asset.h"
#include "quaternion.h"
#include "vector3.h"
#include "vfx_program.h"
#include "vfx_values.h"

namespace UVE::VFX {

/// Frame owner state supplied by the level entity or preview when stepping a VFX instance.
struct VfxOwnerUVE final {
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE velocity{};
    std::uint8_t qualityLevel = 2U; ///< 0 = Low, 1 = Medium, 2 = High.
};

/// Primitive shape mirrored from scene Collider3D components for real-time particle collision.
enum class VfxColliderShapeKindUVE : std::uint8_t {
    Box = 0U,
    Sphere = 1U,
    Capsule = 2U,
};

/// World-space collider snapshot supplied to VFX simulation for real-time particle collision.
struct VfxColliderVolumeUVE final {
    VfxColliderShapeKindUVE kind = VfxColliderShapeKindUVE::Box;
    Math::Vector3UVE center{};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE halfExtents{0.5F, 0.5F, 0.5F};
    float radius = 0.5F;
    float height = 1.0F;
};

/// Structure-of-arrays particle state owned by a single emitter inside a running instance.
/// Only attribute arrays included in the compiled program's `attributeMask` are populated.
struct VfxEmitterStateUVE final {
    std::size_t liveCount = 0U;
    std::uint32_t nextId = 0U;
    float emitterAge = 0.0F;
    bool completed = false;
    bool hasLastOwnerPos = false;
    Math::Vector3UVE lastOwnerPos{};
    std::vector<double> spawnCarry;
    std::uint32_t stepCounter = 0U;

    std::vector<float> posX;
    std::vector<float> posY;
    std::vector<float> posZ;
    std::vector<float> velX;
    std::vector<float> velY;
    std::vector<float> velZ;
    std::vector<float> age;
    std::vector<float> lifetime;
    std::vector<float> colR;
    std::vector<float> colG;
    std::vector<float> colB;
    std::vector<float> colA;
    std::vector<float> spawnColR;
    std::vector<float> spawnColG;
    std::vector<float> spawnColB;
    std::vector<float> spawnColA;
    std::vector<float> sizeX;
    std::vector<float> sizeY;
    std::vector<float> spawnSizeX;
    std::vector<float> spawnSizeY;
    std::vector<float> rotation;
    std::vector<float> spin;
    std::vector<float> frame;
    std::vector<float> mass;
    std::vector<float> prevX;
    std::vector<float> prevY;
    std::vector<float> prevZ;
    std::vector<float> spawnX;
    std::vector<float> spawnY;
    std::vector<float> spawnZ;
    std::vector<std::uint32_t> id;
    std::vector<std::uint32_t> seed;

    // Per-frame scratch arrays.
    std::vector<float> accX;
    std::vector<float> accY;
    std::vector<float> accZ;
    std::vector<float> normalizedAge;

    Math::AabbUVE liveBounds{};
    bool hasLiveBounds = false;
};

/// CPU simulator instance for a single playing particle system.
class VfxInstanceUVE final {
public:
    VfxInstanceUVE() = default;

    /// Compiles `system` into runtime programs and initializes simulation state (including warm-up).
    [[nodiscard]] bool InitializeUVE(const Asset::ParticleSystemAssetUVE& system,
                                     const VfxOwnerUVE& initialOwner = {},
                                     std::uint32_t instanceSeed = 1U,
                                     const VfxCompileContextUVE& compileContext = {},
                                     std::vector<ParticleCompileMessageUVE>* outMessages = nullptr);

    /// Resets emitter ages, counters and live particles to initial state, then runs warm-up if configured.
    void ResetUVE(const VfxOwnerUVE& initialOwner = {});

    /// Stops all emitters from spawning new particles while allowing live particles to finish aging.
    void StopUVE() noexcept;

    /// Immediately removes all live particles across all emitters and marks the instance stopped.
    void KillUVE() noexcept;

    /// Advances all enabled emitters by `dt` seconds (clamped to [0, 0.1] and split into <= 1/30s steps).
    void TickUVE(float dt,
                 const VfxOwnerUVE& owner,
                 float externalSpawnScale = 1.0F,
                 std::span<const VfxColliderVolumeUVE> sceneColliders = {});

    /// Runtime user-parameter overrides (take precedence over asset defaults).
    bool SetParameterFloatUVE(std::string_view name, float value) noexcept;
    bool SetParameterVectorUVE(std::string_view name, const Math::Vector3UVE& value) noexcept;
    bool SetParameterColorUVE(std::string_view name, const Math::ColorUVE& color, float alpha = 1.0F) noexcept;
    bool SetParameterBoolUVE(std::string_view name, bool value) noexcept;
    bool SetParameterIntUVE(std::string_view name, std::int32_t value) noexcept;
    void ClearParameterOverridesUVE() noexcept;

    [[nodiscard]] bool IsInitializedUVE() const noexcept { return m_initialized; }
    [[nodiscard]] bool IsStoppedUVE() const noexcept { return m_stopped; }
    [[nodiscard]] bool IsAliveUVE() const noexcept;
    [[nodiscard]] std::size_t GetLiveCountUVE() const noexcept;
    [[nodiscard]] std::size_t GetEmitterCountUVE() const noexcept { return m_emitters.size(); }
    [[nodiscard]] const ParticleProgramUVE& GetProgramUVE(std::size_t emitterIndex) const;
    [[nodiscard]] const VfxEmitterStateUVE& GetEmitterStateUVE(std::size_t emitterIndex) const;
    [[nodiscard]] std::uint32_t GetInstanceSeedUVE() const noexcept { return m_instanceSeed; }
    [[nodiscard]] const std::vector<VfxParameterValueUVE>& GetParametersUVE() const noexcept { return m_parameters; }
    [[nodiscard]] float GetDurationUVE() const noexcept { return m_duration; }
    [[nodiscard]] bool IsLoopingUVE() const noexcept { return m_looping; }
    [[nodiscard]] float GetWarmUpSecondsUVE() const noexcept { return m_warmUpSeconds; }
    [[nodiscard]] const Math::AabbUVE& GetFixedBoundsUVE() const noexcept { return m_fixedBounds; }
    [[nodiscard]] Math::AabbUVE GetLiveBoundsUVE() const noexcept;
    [[nodiscard]] const Asset::ParticleScalabilityUVE& GetScalabilityUVE(std::uint8_t qualityLevel) const noexcept {
        return m_scalability[std::min<std::size_t>(qualityLevel, 2U)];
    }

private:
    struct EmitterRuntimeUVE final {
        ParticleProgramUVE program;
        VfxEmitterStateUVE state;
    };

    void StepSingleSubstepUVE(float dt,
                              const Math::Vector3UVE& prevOwnerPos,
                              const VfxOwnerUVE& owner,
                              float externalSpawnScale,
                              std::span<const VfxColliderVolumeUVE> sceneColliders);
    void StepEmitterUVE(std::size_t emitterIndex,
                        EmitterRuntimeUVE& runtime,
                        float dt,
                        const Math::Vector3UVE& prevOwnerPos,
                        const VfxOwnerUVE& owner,
                        float externalSpawnScale,
                        std::span<const VfxColliderVolumeUVE> sceneColliders);

    bool m_initialized = false;
    bool m_stopped = false;
    bool m_killed = false;
    std::uint32_t m_instanceSeed = 1U;
    float m_duration = 2.0F;
    bool m_looping = true;
    float m_warmUpSeconds = 0.0F;
    Math::AabbUVE m_fixedBounds{{-2.0F, -2.0F, -2.0F}, {2.0F, 2.0F, 2.0F}};
    Math::Vector3UVE m_lastOwnerPos{};
    std::array<Asset::ParticleScalabilityUVE, 3> m_scalability{};
    std::vector<VfxParameterValueUVE> m_defaultParameters;
    std::vector<VfxParameterValueUVE> m_parameters;
    std::unordered_map<std::string, std::uint32_t> m_parameterIndices;
    std::vector<EmitterRuntimeUVE> m_emitters;
};

} // namespace UVE::VFX
