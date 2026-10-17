// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/sprite_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/disabled_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

bool IsSprite3DComponentValidUVE(const Sprite3DComponentUVE& value) noexcept {
    if (static_cast<std::uint8_t>(value.billboardMode) >
        static_cast<std::uint8_t>(BillboardMode3DUVE::FixedY)) {
        return false;
    }
    const std::uint32_t totalFrames = value.hframes * value.vframes;
    return std::isfinite(value.modulate.r) && std::isfinite(value.modulate.g) &&
           std::isfinite(value.modulate.b) && std::isfinite(value.opacity) &&
           value.opacity >= 0.0F && value.opacity <= 1.0F &&
           std::isfinite(value.pixelSizeMetres) && value.pixelSizeMetres > 0.0F &&
           Math::IsFiniteUVE(value.quadSizePixels) && value.quadSizePixels.x > 0.0F &&
           value.quadSizePixels.y > 0.0F && value.hframes >= 1U && value.vframes >= 1U &&
           value.frame < totalFrames && std::isfinite(value.framesPerSecond) &&
           value.framesPerSecond >= 0.0F;
}

Sprite3DComponentUVE SanitizeSprite3DComponentUVE(const Sprite3DComponentUVE& value) noexcept {
    Sprite3DComponentUVE out = value;
    if (static_cast<std::uint8_t>(out.billboardMode) >
        static_cast<std::uint8_t>(BillboardMode3DUVE::FixedY)) {
        out.billboardMode = BillboardMode3DUVE::Disabled;
    }
    if (!std::isfinite(out.modulate.r) || !std::isfinite(out.modulate.g) ||
        !std::isfinite(out.modulate.b)) {
        out.modulate = Math::ColorUVE{1.0F, 1.0F, 1.0F};
    }
    if (!std::isfinite(out.opacity)) {
        out.opacity = 1.0F;
    } else {
        out.opacity = std::clamp(out.opacity, 0.0F, 1.0F);
    }
    if (!std::isfinite(out.pixelSizeMetres) || out.pixelSizeMetres <= 0.0F) {
        out.pixelSizeMetres = 0.01F;
    }
    if (!Math::IsFiniteUVE(out.quadSizePixels) || out.quadSizePixels.x <= 0.0F ||
        out.quadSizePixels.y <= 0.0F) {
        out.quadSizePixels = Math::Vector2UVE{64.0F, 64.0F};
    }
    if (out.hframes == 0U) {
        out.hframes = 1U;
    }
    if (out.vframes == 0U) {
        out.vframes = 1U;
    }
    const std::uint32_t totalFrames = out.hframes * out.vframes;
    if (out.frame >= totalFrames) {
        out.frame = totalFrames - 1U;
    }
    if (!std::isfinite(out.framesPerSecond) || out.framesPerSecond < 0.0F) {
        out.framesPerSecond = 10.0F;
    }
    if (!std::isfinite(out.frameAccumulatorSeconds) || out.frameAccumulatorSeconds < 0.0F) {
        out.frameAccumulatorSeconds = 0.0F;
    }
    return out;
}

bool IsSprite3DObjectDefinitionValidUVE(const Sprite3DObjectDefinitionUVE& value) noexcept {
    return IsSprite3DComponentValidUVE(value.sprite);
}

bool IsAnimatedSprite3DObjectDefinitionValidUVE(
    const AnimatedSprite3DObjectDefinitionUVE& value) noexcept {
    return IsSprite3DComponentValidUVE(value.sprite);
}

void ApplySprite3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                      const Sprite3DObjectDefinitionUVE& value) {
    ApplySurfaceInstance3DBaseUVE(entityManager, entity, Sprite3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<Sprite3DComponentUVE>(
        entity, SanitizeSprite3DComponentUVE(value.sprite));
}

void ApplyAnimatedSprite3DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                              const EntityUVE entity,
                                              const AnimatedSprite3DObjectDefinitionUVE& value) {
    ApplySurfaceInstance3DBaseUVE(entityManager, entity,
                                  AnimatedSprite3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<Sprite3DComponentUVE>(
        entity, SanitizeSprite3DComponentUVE(value.sprite));
}

Math::QuaternionUVE Sprite3DUVE::ResolveBillboardWorldRotationUVE(
    const BillboardMode3DUVE mode, const Math::QuaternionUVE& entityWorldRotation,
    const Math::QuaternionUVE& cameraWorldRotation) noexcept {
    if (mode == BillboardMode3DUVE::Disabled) {
        return entityWorldRotation;
    }
    if (mode == BillboardMode3DUVE::Enabled) {
        return cameraWorldRotation;
    }
    // FixedY: keep only the camera's yaw rotation around world +Y.
    Math::QuaternionUVE yawOnly{0.0F, cameraWorldRotation.y, 0.0F, cameraWorldRotation.w};
    Math::QuaternionUVE normalized{};
    if (Math::TryNormalizeUVE(yawOnly, normalized)) {
        return normalized;
    }
    return entityWorldRotation;
}

Math::RectUVE Sprite3DUVE::ResolveFrameUVRectUVE(const Sprite3DComponentUVE& sprite) noexcept {
    const Sprite3DComponentUVE safe = SanitizeSprite3DComponentUVE(sprite);
    const std::uint32_t col = safe.frame % safe.hframes;
    const std::uint32_t row = safe.frame / safe.hframes;
    const float cellW = 1.0F / static_cast<float>(safe.hframes);
    const float cellH = 1.0F / static_cast<float>(safe.vframes);
    float u0 = static_cast<float>(col) * cellW;
    float v0 = static_cast<float>(row) * cellH;
    float w = cellW;
    float h = cellH;
    if (safe.flipH) {
        u0 += cellW;
        w = -cellW;
    }
    if (safe.flipV) {
        v0 += cellH;
        h = -cellH;
    }
    return Math::RectUVE{Math::Vector2UVE{u0, v0}, Math::Vector2UVE{w, h}};
}

void Sprite3DUVE::StepAnimationUVE(Sprite3DComponentUVE& sprite,
                                   const float deltaTimeSeconds) noexcept {
    if (!IsSprite3DComponentValidUVE(sprite)) {
        sprite = SanitizeSprite3DComponentUVE(sprite);
    }
    const std::uint32_t totalFrames = sprite.hframes * sprite.vframes;
    if (!sprite.playing || totalFrames <= 1U || sprite.framesPerSecond <= 0.0F ||
        !std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return;
    }
    const float frameDuration = 1.0F / sprite.framesPerSecond;
    sprite.frameAccumulatorSeconds += deltaTimeSeconds;
    while (sprite.frameAccumulatorSeconds >= frameDuration) {
        sprite.frameAccumulatorSeconds -= frameDuration;
        if (sprite.frame + 1U < totalFrames) {
            ++sprite.frame;
        } else if (sprite.loop) {
            sprite.frame = 0U;
        } else {
            sprite.frame = totalFrames - 1U;
            sprite.playing = false;
            sprite.frameAccumulatorSeconds = 0.0F;
            break;
        }
    }
}

std::size_t UpdateSprite3DObjectsUVE(IEntityManagerUVE& entityManager,
                                     const float deltaTimeSeconds) {
    std::size_t stepped = 0U;
    entityManager.ForEachUVE<Sprite3DComponentUVE>(
        [&](const EntityUVE entity, Sprite3DComponentUVE& sprite) {
            if (entityManager.HasComponentUVE<DisabledComponentUVE>(entity) || !sprite.playing) {
                return;
            }
            Sprite3DUVE::StepAnimationUVE(sprite, deltaTimeSeconds);
            ++stepped;
        });
    return stepped;
}

} // namespace UVE::Scene
