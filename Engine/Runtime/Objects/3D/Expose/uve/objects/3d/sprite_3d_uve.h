// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/math/color_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

enum class BillboardMode3DUVE : std::uint8_t {
    Disabled = 0,
    Enabled = 1,
    FixedY = 2,
};

/// 3D sprite and frame-animated billboard quad component built on `SurfaceInstance3DBaseUVE`.
/// Supports `BillboardMode3DUVE` (`Disabled`, `Enabled`, `FixedY`), sprite-sheet grid slicing
/// (`hframes`, `vframes`, `frame`, `framesPerSecond`, `playing`, `loop`), and `pixelSizeMetres`.
struct Sprite3DComponentUVE final {
    Asset::AssetGuidUVE textureAssetGuid = Asset::kInvalidAssetGuidUVE;
    Math::ColorUVE modulate{1.0F, 1.0F, 1.0F};
    float opacity = 1.0F;
    float pixelSizeMetres = 0.01F;
    Math::Vector2UVE quadSizePixels{64.0F, 64.0F};
    BillboardMode3DUVE billboardMode = BillboardMode3DUVE::Disabled;
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

    [[nodiscard]] bool operator==(const Sprite3DComponentUVE& other) const noexcept {
        return textureAssetGuid == other.textureAssetGuid && modulate == other.modulate &&
               opacity == other.opacity && pixelSizeMetres == other.pixelSizeMetres &&
               quadSizeMetresUVE() == other.quadSizeMetresUVE() &&
               billboardMode == other.billboardMode && flipH == other.flipH &&
               flipV == other.flipV && hframes == other.hframes && vframes == other.vframes &&
               frame == other.frame && framesPerSecond == other.framesPerSecond &&
               playing == other.playing && loop == other.loop;
    }

    [[nodiscard]] Math::Vector2UVE quadSizeMetresUVE() const noexcept {
        return quadSizePixels * pixelSizeMetres;
    }
};

[[nodiscard]] bool IsSprite3DComponentValidUVE(const Sprite3DComponentUVE& value) noexcept;

[[nodiscard]] Sprite3DComponentUVE SanitizeSprite3DComponentUVE(
    const Sprite3DComponentUVE& value) noexcept;

struct Sprite3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Sprite3D";
    Sprite3DComponentUVE sprite{};
};

struct AnimatedSprite3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "AnimatedSprite3D";
    Sprite3DComponentUVE sprite{};

    constexpr AnimatedSprite3DObjectDefinitionUVE() noexcept { sprite.playing = true; }
};

[[nodiscard]] bool IsSprite3DObjectDefinitionValidUVE(
    const Sprite3DObjectDefinitionUVE& value) noexcept;
[[nodiscard]] bool IsAnimatedSprite3DObjectDefinitionValidUVE(
    const AnimatedSprite3DObjectDefinitionUVE& value) noexcept;

void ApplySprite3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                      const Sprite3DObjectDefinitionUVE& value);
void ApplyAnimatedSprite3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                              const AnimatedSprite3DObjectDefinitionUVE& value);

class Sprite3DUVE final {
public:
    Sprite3DUVE() = delete;

    /// Resolves the world rotation of a 3D quad under `mode` given `entityWorldRotation` and
    /// `cameraWorldRotation` (full camera alignment for `Enabled`, yaw-only around world +Y for
    /// `FixedY`).
    [[nodiscard]] static Math::QuaternionUVE ResolveBillboardWorldRotationUVE(
        BillboardMode3DUVE mode, const Math::QuaternionUVE& entityWorldRotation,
        const Math::QuaternionUVE& cameraWorldRotation) noexcept;

    /// Computes the normalized UV sub-rectangle `[uvMin, uvSize]` for the current `frame` in an
    /// `hframes x vframes` sprite sheet (including `flipH` / `flipV`).
    [[nodiscard]] static Math::RectUVE ResolveFrameUVRectUVE(
        const Sprite3DComponentUVE& sprite) noexcept;

    /// Advances animated sprite playback by `deltaTimeSeconds`.
    static void StepAnimationUVE(Sprite3DComponentUVE& sprite, float deltaTimeSeconds) noexcept;
};

std::size_t UpdateSprite3DObjectsUVE(IEntityManagerUVE& entityManager, float deltaTimeSeconds);

} // namespace UVE::Scene
