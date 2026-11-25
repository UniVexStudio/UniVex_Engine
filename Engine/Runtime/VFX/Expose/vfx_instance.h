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
struct VfxOwner final {
    Math::Vector3 position{};
    Math::Quaternion rotation{};
    Math::Vector3 velocity{};
    std::uint8_t qualityLevel = 2U; ///< 0 = Low, 1 = Medium, 2 = High.
};

/// Primitive shape mirrored from scene Collider3D components for real-time particle collision.
enum class VfxColliderShapeKind : std::uint8_t {
    Box = 0U,
    Sphere = 1U,
    Capsule = 2U,
};

/// World-space collider snapshot supplied to VFX simulation for real-time particle collision.
struct VfxColliderVolume final {
    VfxColliderShapeKind kind = VfxColliderShapeKind::Box;
    Math::Vector3 center{};
    Math::Quaternion rotation{};
    Math::Vector3 halfExtents{0.5F, 0.5F, 0.5F};
    float radius = 0.5F;
    float height = 1.0F;
};

/// Structure-of-arrays particle state owned by a single emitter inside a running instance.
/// Only attribute arrays included in the compiled program's `attributeMask` are populated.
struct VfxEmitterState final {
    std::size_t liveCount = 0U;
    std::uint32_t nextId = 0U;
    float emitterAge = 0.0F;
    bool completed = false;
    bool hasLastOwnerPos = false;
    Math::Vector3 lastOwnerPos{};
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

    Math::Aabb liveBounds{};
    bool hasLiveBounds = false;
};

/// CPU simulator instance for a single playing particle system.
class VfxInstance final {
public:
    VfxInstance() = default;

    /// Compiles `system` into runtime programs and initializes simulation state (including warm-up).
    [[nodiscard]] bool Initialize(const Asset::ParticleSystemAsset& system,
                                     const VfxOwner& initialOwner = {},
                                     std::uint32_t instanceSeed = 1U,
                                     const VfxCompileContext& compileContext = {},
                                     std::vector<ParticleCompileMessage>* outMessages = nullptr);

    /// Resets emitter ages, counters and live particles to initial state, then runs warm-up if configured.
    void Reset(const VfxOwner& initialOwner = {});

    /// Stops all emitters from spawning new particles while allowing live particles to finish aging.
    void Stop() noexcept;

    /// Immediately removes all live particles across all emitters and marks the instance stopped.
    void Kill() noexcept;

    /// Advances all enabled emitters by `dt` seconds (clamped to [0, 0.1] and split into <= 1/30s steps).
    void Tick(float dt,
                 const VfxOwner& owner,
                 float externalSpawnScale = 1.0F,
                 std::span<const VfxColliderVolume> sceneColliders = {});

    /// Runtime user-parameter overrides (take precedence over asset defaults).
    bool SetParameterFloat(std::string_view name, float value) noexcept;
    bool SetParameterVector(std::string_view name, const Math::Vector3& value) noexcept;
    bool SetParameterColor(std::string_view name, const Math::Color& color, float alpha = 1.0F) noexcept;
    bool SetParameterBool(std::string_view name, bool value) noexcept;
    bool SetParameterInt(std::string_view name, std::int32_t value) noexcept;
    void ClearParameterOverrides() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] bool IsStopped() const noexcept { return m_stopped; }
    [[nodiscard]] bool IsAlive() const noexcept;
    [[nodiscard]] std::size_t GetLiveCount() const noexcept;
    [[nodiscard]] std::size_t GetEmitterCount() const noexcept { return m_emitters.size(); }
    [[nodiscard]] const ParticleProgram& GetProgram(std::size_t emitterIndex) const;
    [[nodiscard]] const VfxEmitterState& GetEmitterState(std::size_t emitterIndex) const;
    [[nodiscard]] std::uint32_t GetInstanceSeed() const noexcept { return m_instanceSeed; }
    [[nodiscard]] const std::vector<VfxParameterValue>& GetParameters() const noexcept { return m_parameters; }
    [[nodiscard]] float GetDuration() const noexcept { return m_duration; }
    [[nodiscard]] bool IsLooping() const noexcept { return m_looping; }
    [[nodiscard]] float GetWarmUpSeconds() const noexcept { return m_warmUpSeconds; }
    [[nodiscard]] const Math::Aabb& GetFixedBounds() const noexcept { return m_fixedBounds; }
    [[nodiscard]] Math::Aabb GetLiveBounds() const noexcept;
    [[nodiscard]] const Asset::ParticleScalability& GetScalability(std::uint8_t qualityLevel) const noexcept {
        return m_scalability[std::min<std::size_t>(qualityLevel, 2U)];
    }

private:
    struct EmitterRuntime final {
        ParticleProgram program;
        VfxEmitterState state;
    };

    void StepSingleSubstep(float dt,
                              const Math::Vector3& prevOwnerPos,
                              const VfxOwner& owner,
                              float externalSpawnScale,
                              std::span<const VfxColliderVolume> sceneColliders);
    void StepEmitter(std::size_t emitterIndex,
                        EmitterRuntime& runtime,
                        float dt,
                        const Math::Vector3& prevOwnerPos,
                        const VfxOwner& owner,
                        float externalSpawnScale,
                        std::span<const VfxColliderVolume> sceneColliders);

    bool m_initialized = false;
    bool m_stopped = false;
    bool m_killed = false;
    std::uint32_t m_instanceSeed = 1U;
    float m_duration = 2.0F;
    bool m_looping = true;
    float m_warmUpSeconds = 0.0F;
    Math::Aabb m_fixedBounds{{-2.0F, -2.0F, -2.0F}, {2.0F, 2.0F, 2.0F}};
    Math::Vector3 m_lastOwnerPos{};
    std::array<Asset::ParticleScalability, 3> m_scalability{};
    std::vector<VfxParameterValue> m_defaultParameters;
    std::vector<VfxParameterValue> m_parameters;
    std::unordered_map<std::string, std::uint32_t> m_parameterIndices;
    std::vector<EmitterRuntime> m_emitters;
};

} // namespace UVE::VFX
