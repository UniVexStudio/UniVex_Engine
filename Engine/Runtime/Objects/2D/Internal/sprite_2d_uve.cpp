// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/2d/sprite_2d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsSprite2DObjectDefinitionValidUVE(const Sprite2DObjectDefinitionUVE& value) noexcept {
    return IsSprite2DComponentValidUVE(value.sprite);
}

void ApplySprite2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                      const Sprite2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<Sprite2DComponentUVE>(entity, value.sprite);
}

std::optional<Sprite2DQuadUVE> ComposeSprite2DQuadUVE(
    const Sprite2DComponentUVE& sprite, const Math::Vector2UVE& textureSize) noexcept {
    if (!IsSprite2DComponentValidUVE(sprite) || !Math::IsFiniteUVE(textureSize) ||
        textureSize.x <= 0.0F || textureSize.y <= 0.0F) {
        return std::nullopt;
    }
    const bool whole =
        sprite.sourceRect.size.x == 0.0F || sprite.sourceRect.size.y == 0.0F;
    const Math::Vector2UVE origin = whole ? Math::Vector2UVE{} : sprite.sourceRect.position;
    const Math::Vector2UVE extent = whole ? textureSize : sprite.sourceRect.size;
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

} // namespace UVE::Scene
