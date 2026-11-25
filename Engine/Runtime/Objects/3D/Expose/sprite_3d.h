// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>

#include "asset_guid.h"
#include "entity.h"
#include "world_transform_component.h"
#include "color.h"
#include "rect.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

enum class BillboardMode3D : std::uint8_t {
    Disabled = 0,
    Enabled = 1,
    FixedY = 2,
};

/// 3D sprite and frame-animated billboard quad component built on `SurfaceInstance3DBase`.
/// Supports `BillboardMode3D` (`Disabled`, `Enabled`, `FixedY`), sprite-sheet grid slicing
/// (`hframes`, `vframes`, `frame`, `framesPerSecond`, `playing`, `loop`), and `pixelSizeMetres`.
struct Sprite3DComponent final {
    Asset::AssetGuid textureAssetGuid = Asset::kInvalidAssetGuid;
    Math::Color modulate{1.0F, 1.0F, 1.0F};
    float opacity = 1.0F;
    float pixelSizeMetres = 0.01F;
    Math::Vector2 quadSizePixels{64.0F, 64.0F};
    BillboardMode3D billboardMode = BillboardMode3D::Disabled;
    bool flipH = false;
    bool flipV = false;
    std::uint32_t hframes = 1U;
    std::uint32_t vframes = 1U;
    std::uint32_t frame = 0U;
    float framesPerSecond = 10.0F;
    bool playing = false;
    bool loop = true;

    // Runtime state, never persisted:
    float frameAccumulatorSeconds = 0.0F;

    [[nodiscard]] bool operator==(const Sprite3DComponent& other) const noexcept {
        return textureAssetGuid == other.textureAssetGuid && modulate == other.modulate &&
               opacity == other.opacity && pixelSizeMetres == other.pixelSizeMetres &&
               quadSizeMetres() == other.quadSizeMetres() &&
               billboardMode == other.billboardMode && flipH == other.flipH &&
               flipV == other.flipV && hframes == other.hframes && vframes == other.vframes &&
               frame == other.frame && framesPerSecond == other.framesPerSecond &&
               playing == other.playing && loop == other.loop;
    }

    [[nodiscard]] Math::Vector2 quadSizeMetres() const noexcept {
        return quadSizePixels * pixelSizeMetres;
    }
};

[[nodiscard]] bool IsSprite3DComponentValid(const Sprite3DComponent& value) noexcept;

[[nodiscard]] Sprite3DComponent SanitizeSprite3DComponent(
    const Sprite3DComponent& value) noexcept;

struct Sprite3DObjectDefinition final {
    static constexpr std::string_view defaultName = "Sprite3D";
    Sprite3DComponent sprite{};
};

struct AnimatedSprite3DObjectDefinition final {
    static constexpr std::string_view defaultName = "AnimatedSprite3D";
    Sprite3DComponent sprite{};

    constexpr AnimatedSprite3DObjectDefinition() noexcept { sprite.playing = true; }
};

[[nodiscard]] bool IsSprite3DObjectDefinitionValid(
    const Sprite3DObjectDefinition& value) noexcept;
[[nodiscard]] bool IsAnimatedSprite3DObjectDefinitionValid(
    const AnimatedSprite3DObjectDefinition& value) noexcept;

void ApplySprite3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                      const Sprite3DObjectDefinition& value);
void ApplyAnimatedSprite3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                              const AnimatedSprite3DObjectDefinition& value);

class Sprite3D final {
public:
    Sprite3D() = delete;

    /// Resolves the world rotation of a 3D quad under `mode` given `entityWorldRotation` and
    /// `cameraWorldRotation` (full camera alignment for `Enabled`, yaw-only around world +Y for
    /// `FixedY`).
    [[nodiscard]] static Math::Quaternion ResolveBillboardWorldRotation(
        BillboardMode3D mode, const Math::Quaternion& entityWorldRotation,
        const Math::Quaternion& cameraWorldRotation) noexcept;

    /// Computes the normalized UV sub-rectangle `[uvMin, uvSize]` for the current `frame` in an
    /// `hframes x vframes` sprite sheet (including `flipH` / `flipV`).
    [[nodiscard]] static Math::Rect ResolveFrameUVRect(
        const Sprite3DComponent& sprite) noexcept;

    /// Advances animated sprite playback by `deltaTimeSeconds`.
    static void StepAnimation(Sprite3DComponent& sprite, float deltaTimeSeconds) noexcept;
};

std::size_t UpdateSprite3DObjects(IEntityManager& entityManager, float deltaTimeSeconds);

} // namespace UVE::Scene
