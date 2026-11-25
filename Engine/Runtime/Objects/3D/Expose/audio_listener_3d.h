// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "entity.h"
#include "world_transform_component.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

/// Overrides the world-space point and orientation from which 3D spatial audio is heard.
/// When an enabled entity carries a valid `AudioListener3DComponent` with `current == true` and
/// a valid `WorldTransformComponent`, the engine routes listener position and orientation from
/// this object instead of the active camera. When `ignoreRotation` is true, the listener keeps a
/// fixed world-aligned orientation (`forward = (0, 0, -1)`, `up = (0, 1, 0)`) so camera roll or
/// boom orbit does not pan audio unexpectedly.
struct AudioListener3DComponent final {
    bool current = true;
    bool ignoreRotation = false;
    Math::Vector3 positionOffset{0.0F, 0.0F, 0.0F};

    [[nodiscard]] bool operator==(const AudioListener3DComponent&) const = default;
};

[[nodiscard]] bool IsAudioListener3DComponentValid(
    const AudioListener3DComponent& value) noexcept;

[[nodiscard]] AudioListener3DComponent SanitizeAudioListener3DComponent(
    const AudioListener3DComponent& value) noexcept;

struct AudioListener3DObjectDefinition final {
    static constexpr std::string_view defaultName = "AudioListener3D";
    AudioListener3DComponent listener{};
};

[[nodiscard]] bool IsAudioListener3DObjectDefinitionValid(
    const AudioListener3DObjectDefinition& value) noexcept;

void ApplyAudioListener3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                             const AudioListener3DObjectDefinition& value);

struct ResolvedAudioListener3DPose final {
    Entity entity = kInvalidEntity;
    Math::Vector3 position{0.0F, 0.0F, 0.0F};
    Math::Vector3 forward{0.0F, 0.0F, -1.0F};
    Math::Vector3 up{0.0F, 0.0F, 1.0F};
};

class AudioListener3D final {
public:
    AudioListener3D() = delete;

    /// Marks `listenerEntity` as `current = true` and clears `current` on all other document
    /// `AudioListener3DComponent` entities.
    static void MakeCurrent(IEntityManager& entityManager, Entity listenerEntity);

    /// Resolves the world-space listener pose for `listener` + `worldTransform`.
    [[nodiscard]] static ResolvedAudioListener3DPose ResolvePoseFromWorldTransform(
        Entity entity, const AudioListener3DComponent& listener,
        const WorldTransformComponent& worldTransform) noexcept;

    /// Finds the active `AudioListener3DComponent` in `entityManager` (or falls back to
    /// `fallbackCameraEntity`'s `WorldTransformComponent`). Returns false when neither yields a
    /// valid world pose.
    [[nodiscard]] static bool TryResolveActiveListenerPose(
        IEntityManager& entityManager, Entity fallbackCameraEntity,
        ResolvedAudioListener3DPose& outPose);
};

} // namespace UVE::Scene
