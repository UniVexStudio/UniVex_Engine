// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "sprite_3d.h"

#include <algorithm>
#include <cmath>

#include "disabled_component.h"
#include "i_entity_manager.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {

bool IsSprite3DComponentValid(const Sprite3DComponent& value) noexcept {
    if (static_cast<std::uint8_t>(value.billboardMode) >
        static_cast<std::uint8_t>(BillboardMode3D::FixedY)) {
        return false;
    }
    const std::uint32_t totalFrames = value.hframes * value.vframes;
    return std::isfinite(value.modulate.r) && std::isfinite(value.modulate.g) &&
           std::isfinite(value.modulate.b) && std::isfinite(value.opacity) &&
           value.opacity >= 0.0F && value.opacity <= 1.0F &&
           std::isfinite(value.pixelSizeMetres) && value.pixelSizeMetres > 0.0F &&
           Math::IsFinite(value.quadSizePixels) && value.quadSizePixels.x > 0.0F &&
           value.quadSizePixels.y > 0.0F && value.hframes >= 1U && value.vframes >= 1U &&
           value.frame < totalFrames && std::isfinite(value.framesPerSecond) &&
           value.framesPerSecond >= 0.0F;
}

Sprite3DComponent SanitizeSprite3DComponent(const Sprite3DComponent& value) noexcept {
    Sprite3DComponent out = value;
    if (static_cast<std::uint8_t>(out.billboardMode) >
        static_cast<std::uint8_t>(BillboardMode3D::FixedY)) {
        out.billboardMode = BillboardMode3D::Disabled;
    }
    if (!std::isfinite(out.modulate.r) || !std::isfinite(out.modulate.g) ||
        !std::isfinite(out.modulate.b)) {
        out.modulate = Math::Color{1.0F, 1.0F, 1.0F};
    }
    if (!std::isfinite(out.opacity)) {
        out.opacity = 1.0F;
    } else {
        out.opacity = std::clamp(out.opacity, 0.0F, 1.0F);
    }
    if (!std::isfinite(out.pixelSizeMetres) || out.pixelSizeMetres <= 0.0F) {
        out.pixelSizeMetres = 0.01F;
    }
    if (!Math::IsFinite(out.quadSizePixels) || out.quadSizePixels.x <= 0.0F ||
        out.quadSizePixels.y <= 0.0F) {
        out.quadSizePixels = Math::Vector2{64.0F, 64.0F};
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

bool IsSprite3DObjectDefinitionValid(const Sprite3DObjectDefinition& value) noexcept {
    return IsSprite3DComponentValid(value.sprite);
}

bool IsAnimatedSprite3DObjectDefinitionValid(
    const AnimatedSprite3DObjectDefinition& value) noexcept {
    return IsSprite3DComponentValid(value.sprite);
}

void ApplySprite3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                      const Sprite3DObjectDefinition& value) {
    ApplySurfaceInstance3DBase(entityManager, entity, Sprite3DObjectDefinition::defaultName);
    entityManager.AddComponent<Sprite3DComponent>(
        entity, SanitizeSprite3DComponent(value.sprite));
}

void ApplyAnimatedSprite3DObjectDefinition(IEntityManager& entityManager,
                                              const Entity entity,
                                              const AnimatedSprite3DObjectDefinition& value) {
    ApplySurfaceInstance3DBase(entityManager, entity,
                                  AnimatedSprite3DObjectDefinition::defaultName);
    entityManager.AddComponent<Sprite3DComponent>(
        entity, SanitizeSprite3DComponent(value.sprite));
}

Math::Quaternion Sprite3D::ResolveBillboardWorldRotation(
    const BillboardMode3D mode, const Math::Quaternion& entityWorldRotation,
    const Math::Quaternion& cameraWorldRotation) noexcept {
    if (mode == BillboardMode3D::Disabled) {
        return entityWorldRotation;
    }
    if (mode == BillboardMode3D::Enabled) {
        return cameraWorldRotation;
    }
    // FixedY: keep only the camera's yaw rotation around world +Y.
    Math::Quaternion yawOnly{0.0F, cameraWorldRotation.y, 0.0F, cameraWorldRotation.w};
    Math::Quaternion normalized{};
    if (Math::TryNormalize(yawOnly, normalized)) {
        return normalized;
    }
    return entityWorldRotation;
}

Math::Rect Sprite3D::ResolveFrameUVRect(const Sprite3DComponent& sprite) noexcept {
    const Sprite3DComponent safe = SanitizeSprite3DComponent(sprite);
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
    return Math::Rect{Math::Vector2{u0, v0}, Math::Vector2{w, h}};
}

void Sprite3D::StepAnimation(Sprite3DComponent& sprite,
                                   const float deltaTimeSeconds) noexcept {
    if (!IsSprite3DComponentValid(sprite)) {
        sprite = SanitizeSprite3DComponent(sprite);
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

std::size_t UpdateSprite3DObjects(IEntityManager& entityManager,
                                     const float deltaTimeSeconds) {
    std::size_t stepped = 0U;
    entityManager.ForEach<Sprite3DComponent>(
        [&](const Entity entity, Sprite3DComponent& sprite) {
            if (entityManager.HasComponent<DisabledComponent>(entity) || !sprite.playing) {
                return;
            }
            Sprite3D::StepAnimation(sprite, deltaTimeSeconds);
            ++stepped;
        });
    return stepped;
}

} // namespace UVE::Scene
