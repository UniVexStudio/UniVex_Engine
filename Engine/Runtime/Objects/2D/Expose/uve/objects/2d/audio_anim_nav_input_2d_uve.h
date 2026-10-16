// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// 2D positional audio listener (`current`).
struct AudioListener2DComponentUVE final {
    bool current = true;

    [[nodiscard]] bool operator==(const AudioListener2DComponentUVE&) const = default;
};

struct AudioListener2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "AudioListener2D";
    AudioListener2DComponentUVE listener{};
};

void ApplyAudioListener2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                             const AudioListener2DObjectDefinitionUVE& value);

/// 2D positional audio stream player (`audioAssetGuid`, `volumeLinear`, `maxDistancePixels`,
/// `playing`).
struct AudioStreamPlayer2DComponentUVE final {
    Asset::AssetGuidUVE audioAssetGuid = Asset::kInvalidAssetGuidUVE;
    float volumeLinear = 1.0F;
    float maxDistancePixels = 500.0F;
    bool playing = false;

    // Runtime state, never persisted:
    float effectiveGain = 0.0F;

    [[nodiscard]] bool operator==(const AudioStreamPlayer2DComponentUVE& other) const noexcept {
        return audioAssetGuid == other.audioAssetGuid && volumeLinear == other.volumeLinear &&
               maxDistancePixels == other.maxDistancePixels && playing == other.playing;
    }
};

struct AudioStreamPlayer2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "AudioStreamPlayer2D";
    AudioStreamPlayer2DComponentUVE player{};
};

void ApplyAudioStreamPlayer2DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, EntityUVE entity,
    const AudioStreamPlayer2DObjectDefinitionUVE& value);

/// 2D skeletal bone (`boneName`, `lengthPixels`, `restRotationRadians`) and skeleton hierarchy.
struct Bone2DComponentUVE final {
    std::string boneName{"Bone2D"};
    float lengthPixels = 32.0F;
    float restRotationRadians = 0.0F;
    float poseRotationRadians = 0.0F;

    [[nodiscard]] bool operator==(const Bone2DComponentUVE&) const = default;
};

struct Skeleton2DComponentUVE final {
    std::vector<EntityUVE> boneEntities{};
    bool enabled = true;

    [[nodiscard]] bool operator==(const Skeleton2DComponentUVE&) const = default;
};

struct Bone2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Bone2D";
    Bone2DComponentUVE bone{};
};
struct Skeleton2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Skeleton2D";
    Skeleton2DComponentUVE skeleton{};
};

void ApplyBone2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                    const Bone2DObjectDefinitionUVE& value);
void ApplySkeleton2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                        const Skeleton2DObjectDefinitionUVE& value);

/// 2D navigation region, seeker agent, obstacle, and off-mesh link.
struct NavigationRegion2DComponentUVE final {
    Math::RectUVE bounds{Math::Vector2UVE{-500.0F, -500.0F}, Math::Vector2UVE{1000.0F, 1000.0F}};
    std::uint32_t navigationLayers = 1U;
    bool enabled = true;

    [[nodiscard]] bool operator==(const NavigationRegion2DComponentUVE&) const = default;
};

struct NavigationAgent2DComponentUVE final {
    Math::Vector2UVE targetPosition{0.0F, 0.0F};
    float maxSpeedPixelsPerSecond = 120.0F;
    float radiusPixels = 16.0F;
    bool enabled = true;

    // Runtime state, never persisted:
    Math::Vector2UVE desiredVelocity{0.0F, 0.0F};
    bool targetReached = false;

    [[nodiscard]] bool operator==(const NavigationAgent2DComponentUVE& other) const noexcept {
        return targetPosition == other.targetPosition &&
               maxSpeedPixelsPerSecond == other.maxSpeedPixelsPerSecond &&
               radiusPixels == other.radiusPixels && enabled == other.enabled;
    }
};

struct NavigationObstacle2DComponentUVE final {
    float radiusPixels = 24.0F;
    bool enabled = true;

    [[nodiscard]] bool operator==(const NavigationObstacle2DComponentUVE&) const = default;
};

struct NavigationLink2DComponentUVE final {
    Math::Vector2UVE startPosition{0.0F, 0.0F};
    Math::Vector2UVE endPosition{64.0F, 0.0F};
    bool bidirectional = true;
    bool enabled = true;

    [[nodiscard]] bool operator==(const NavigationLink2DComponentUVE&) const = default;
};

struct NavigationRegion2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "NavigationRegion2D";
    NavigationRegion2DComponentUVE region{};
};
struct NavigationAgent2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "NavigationAgent2D";
    NavigationAgent2DComponentUVE agent{};
};
struct NavigationObstacle2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "NavigationObstacle2D";
    NavigationObstacle2DComponentUVE obstacle{};
};
struct NavigationLink2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "NavigationLink2D";
    NavigationLink2DComponentUVE link{};
};

void ApplyNavigationRegion2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                                const NavigationRegion2DObjectDefinitionUVE& value);
void ApplyNavigationAgent2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                               const NavigationAgent2DObjectDefinitionUVE& value);
void ApplyNavigationObstacle2DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, EntityUVE entity,
    const NavigationObstacle2DObjectDefinitionUVE& value);
void ApplyNavigationLink2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                              const NavigationLink2DObjectDefinitionUVE& value);

/// 2D GPU and CPU particle emitter component (`amount`, `lifetimeSeconds`, `emitting`,
/// `gpuAccelerated`).
struct Particles2DComponentUVE final {
    std::uint32_t amount = 32U;
    float lifetimeSeconds = 1.0F;
    Math::Vector2UVE initialVelocity{0.0F, -50.0F};
    bool gpuAccelerated = true;
    bool emitting = true;

    // Runtime state, never persisted:
    std::uint32_t activeParticleCount = 0U;

    [[nodiscard]] bool operator==(const Particles2DComponentUVE& other) const noexcept {
        return amount == other.amount && lifetimeSeconds == other.lifetimeSeconds &&
               initialVelocity == other.initialVelocity &&
               gpuAccelerated == other.gpuAccelerated && emitting == other.emitting;
    }
};

struct GPUParticles2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "GPUParticles2D";
    Particles2DComponentUVE particles{32U, 1.0F, Math::Vector2UVE{0.0F, -50.0F}, true, true};
};
struct CPUParticles2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "CPUParticles2D";
    Particles2DComponentUVE particles{32U, 1.0F, Math::Vector2UVE{0.0F, -50.0F}, false, true};
};

void ApplyGPUParticles2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const GPUParticles2DObjectDefinitionUVE& value);
void ApplyCPUParticles2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const CPUParticles2DObjectDefinitionUVE& value);

/// On-screen touch input button (`bounds`, `actionName`, `pressed`).
struct TouchScreenButtonComponentUVE final {
    Math::RectUVE bounds{Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{64.0F, 64.0F}};
    std::string actionName{"ui_accept"};
    bool pressed = false;

    [[nodiscard]] bool operator==(const TouchScreenButtonComponentUVE&) const = default;
};

struct TouchScreenButtonObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "TouchScreenButton";
    TouchScreenButtonComponentUVE button{};
};

void ApplyTouchScreenButtonObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                               const TouchScreenButtonObjectDefinitionUVE& value);

/// Steps 2D positional audio attenuation, 2D navigation steering + obstacle avoidance, and 2D
/// particle emitters.
std::size_t UpdateAudioAnimNavInput2DObjectsUVE(IEntityManagerUVE& entityManager,
                                                float deltaTimeSeconds);

} // namespace UVE::Scene
