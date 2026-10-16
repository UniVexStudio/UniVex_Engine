// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Overrides the world-space point and orientation from which 3D spatial audio is heard.
/// When an enabled entity carries a valid `AudioListener3DComponentUVE` with `current == true` and
/// a valid `WorldTransformComponentUVE`, the engine routes listener position and orientation from
/// this object instead of the active camera. When `ignoreRotation` is true, the listener keeps a
/// fixed world-aligned orientation (`forward = (0, 0, -1)`, `up = (0, 1, 0)`) so camera roll or
/// boom orbit does not pan audio unexpectedly.
struct AudioListener3DComponentUVE final {
    bool current = true;
    bool ignoreRotation = false;
    Math::Vector3UVE positionOffset{0.0F, 0.0F, 0.0F};

    [[nodiscard]] bool operator==(const AudioListener3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsAudioListener3DComponentValidUVE(
    const AudioListener3DComponentUVE& value) noexcept;

[[nodiscard]] AudioListener3DComponentUVE SanitizeAudioListener3DComponentUVE(
    const AudioListener3DComponentUVE& value) noexcept;

struct AudioListener3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "AudioListener3D";
    AudioListener3DComponentUVE listener{};
};

[[nodiscard]] bool IsAudioListener3DObjectDefinitionValidUVE(
    const AudioListener3DObjectDefinitionUVE& value) noexcept;

void ApplyAudioListener3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                             const AudioListener3DObjectDefinitionUVE& value);

struct ResolvedAudioListener3DPoseUVE final {
    EntityUVE entity = kInvalidEntityUVE;
    Math::Vector3UVE position{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE forward{0.0F, 0.0F, -1.0F};
    Math::Vector3UVE up{0.0F, 0.0F, 1.0F};
};

class AudioListener3DUVE final {
public:
    AudioListener3DUVE() = delete;

    /// Marks `listenerEntity` as `current = true` and clears `current` on all other document
    /// `AudioListener3DComponentUVE` entities.
    static void MakeCurrentUVE(IEntityManagerUVE& entityManager, EntityUVE listenerEntity);

    /// Resolves the world-space listener pose for `listener` + `worldTransform`.
    [[nodiscard]] static ResolvedAudioListener3DPoseUVE ResolvePoseFromWorldTransformUVE(
        EntityUVE entity, const AudioListener3DComponentUVE& listener,
        const WorldTransformComponentUVE& worldTransform) noexcept;

    /// Finds the active `AudioListener3DComponentUVE` in `entityManager` (or falls back to
    /// `fallbackCameraEntity`'s `WorldTransformComponentUVE`). Returns false when neither yields a
    /// valid world pose.
    [[nodiscard]] static bool TryResolveActiveListenerPoseUVE(
        IEntityManagerUVE& entityManager, EntityUVE fallbackCameraEntity,
        ResolvedAudioListener3DPoseUVE& outPose);
};

} // namespace UVE::Scene
