// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/2d/sprite_2d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsSprite2DObjectDefinitionValidUVE(const Sprite2DObjectDefinitionUVE& value) noexcept {
    return IsSprite2DComponentValidUVE(value.sprite);
}

void ApplySprite2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                      const Sprite2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<Sprite2DComponentUVE>(entity,
                                                        SanitizeSprite2DComponentUVE(value.sprite));
}

bool Sprite2DUVE::UsesWholeTextureRegionUVE(const Sprite2DComponentUVE& sprite) noexcept {
    return !HasCustomSprite2DSourceRegionUVE(sprite);
}

Math::Vector2UVE Sprite2DUVE::ResolveDrawExtentPixelsUVE(
    const Sprite2DComponentUVE& sprite, const Math::Vector2UVE& textureSize) noexcept {
    return UsesWholeTextureRegionUVE(sprite) ? textureSize : sprite.sourceRect.size;
}

std::optional<Sprite2DQuadUVE> ComposeSprite2DQuadUVE(
    const Sprite2DComponentUVE& sprite, const Math::Vector2UVE& textureSize) noexcept {
    if (!IsSprite2DComponentValidUVE(sprite) || !Math::IsFiniteUVE(textureSize) ||
        textureSize.x <= 0.0F || textureSize.y <= 0.0F) {
        return std::nullopt;
    }
    const bool whole = Sprite2DUVE::UsesWholeTextureRegionUVE(sprite);
    const Math::Vector2UVE origin = whole ? Math::Vector2UVE{} : sprite.sourceRect.position;
    const Math::Vector2UVE extent =
        Sprite2DUVE::ResolveDrawExtentPixelsUVE(sprite, textureSize);
    if (origin.x + extent.x > textureSize.x || origin.y + extent.y > textureSize.y) {
        return std::nullopt;
    }
    const float halfX = extent.x * 0.5F;
    const float halfY = extent.y * 0.5F;
    float u0 = origin.x / textureSize.x;
    float u1 = (origin.x + extent.x) / textureSize.x;
    float v0 = origin.y / textureSize.y;
    float v1 = (origin.y + extent.y) / textureSize.y;
    if (sprite.flipH) {
        const float swapped = u0;
        u0 = u1;
        u1 = swapped;
    }
    if (sprite.flipV) {
        const float swapped = v0;
        v0 = v1;
        v1 = swapped;
    }
    Sprite2DQuadUVE quad;
    quad.corners = {Math::Vector2UVE{sprite.offset.x - halfX, sprite.offset.y - halfY},
                    Math::Vector2UVE{sprite.offset.x + halfX, sprite.offset.y - halfY},
                    Math::Vector2UVE{sprite.offset.x + halfX, sprite.offset.y + halfY},
                    Math::Vector2UVE{sprite.offset.x - halfX, sprite.offset.y + halfY}};
    quad.uvs = {Math::Vector2UVE{u0, v0}, Math::Vector2UVE{u1, v0}, Math::Vector2UVE{u1, v1},
                Math::Vector2UVE{u0, v1}};
    return quad;
}

std::optional<Sprite2DQuadUVE> Sprite2DUVE::ComposeQuadUVE(
    const Sprite2DComponentUVE& sprite, const Math::Vector2UVE& textureSize) noexcept {
    return ComposeSprite2DQuadUVE(sprite, textureSize);
}

std::optional<Math::RectUVE> Sprite2DUVE::ComputeLocalBoundingRectUVE(
    const Sprite2DComponentUVE& sprite, const Math::Vector2UVE& textureSize) noexcept {
    const std::optional<Sprite2DQuadUVE> quad = ComposeQuadUVE(sprite, textureSize);
    if (!quad.has_value()) {
        return std::nullopt;
    }
    const Math::Vector2UVE topLeft = quad->corners[0U];
    const Math::Vector2UVE bottomRight = quad->corners[2U];
    return Math::RectUVE{
        .position = topLeft,
        .size = bottomRight - topLeft,
    };
}

bool Sprite2DUVE::ContainsLocalPointUVE(const Sprite2DComponentUVE& sprite,
                                        const Math::Vector2UVE& textureSize,
                                        const Math::Vector2UVE& localPoint) noexcept {
    if (!Math::IsFiniteUVE(localPoint)) {
        return false;
    }
    const std::optional<Math::RectUVE> bounds = ComputeLocalBoundingRectUVE(sprite, textureSize);
    if (!bounds.has_value()) {
        return false;
    }
    return localPoint.x >= bounds->position.x &&
           localPoint.x <= (bounds->position.x + bounds->size.x) &&
           localPoint.y >= bounds->position.y &&
           localPoint.y <= (bounds->position.y + bounds->size.y);
}

} // namespace UVE::Scene
