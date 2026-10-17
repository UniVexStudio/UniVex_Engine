// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/2d/tile_parallax_sprite_2d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/disabled_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

void ApplyAnimatedSprite2DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                              const EntityUVE entity,
                                              const AnimatedSprite2DObjectDefinitionUVE& value) {
    AnimatedSprite2DComponentUVE out = value.sprite;
    if (out.hframes == 0U) {
        out.hframes = 1U;
    }
    if (out.vframes == 0U) {
        out.vframes = 1U;
    }
    const std::uint32_t total = out.hframes * out.vframes;
    if (out.frame >= total) {
        out.frame = total - 1U;
    }
    if (!std::isfinite(out.framesPerSecond) || out.framesPerSecond < 0.0F) {
        out.framesPerSecond = 12.0F;
    }
    entityManager.AddComponentUVE<AnimatedSprite2DComponentUVE>(entity, out);
}

std::size_t UpdateAnimatedSprite2DObjectsUVE(IEntityManagerUVE& entityManager,
                                             const float deltaTimeSeconds) {
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return 0U;
    }
    std::size_t stepped = 0U;
    entityManager.ForEachUVE<AnimatedSprite2DComponentUVE>(
        [&](const EntityUVE entity, AnimatedSprite2DComponentUVE& sprite) {
            if (!sprite.playing || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                sprite.framesPerSecond <= 0.0F) {
                return;
            }
            const std::uint32_t totalFrames = std::max(1U, sprite.hframes * sprite.vframes);
            if (totalFrames <= 1U) {
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
            ++stepped;
        });
    return stepped;
}

void ApplyParallax2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                        const Parallax2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<Parallax2DComponentUVE>(entity, value.parallax);
}

Math::Vector2UVE Parallax2DUVE::ComputeEffectiveOffsetUVE(
    const Parallax2DComponentUVE& parallax, const Math::Vector2UVE& cameraPosition) noexcept {
    if (!Math::IsFiniteUVE(cameraPosition) || !Math::IsFiniteUVE(parallax.scrollScale) ||
        !Math::IsFiniteUVE(parallax.scrollOffset)) {
        return Math::Vector2UVE{0.0F, 0.0F};
    }
    Math::Vector2UVE offset{cameraPosition.x * parallax.scrollScale.x + parallax.scrollOffset.x,
                            cameraPosition.y * parallax.scrollScale.y + parallax.scrollOffset.y};
    if (std::isfinite(parallax.repeatSize.x) && parallax.repeatSize.x > 0.0F) {
        offset.x = std::fmod(offset.x, parallax.repeatSize.x);
        if (offset.x < 0.0F) {
            offset.x += parallax.repeatSize.x;
        }
    }
    if (std::isfinite(parallax.repeatSize.y) && parallax.repeatSize.y > 0.0F) {
        offset.y = std::fmod(offset.y, parallax.repeatSize.y);
        if (offset.y < 0.0F) {
            offset.y += parallax.repeatSize.y;
        }
    }
    return offset;
}

void ApplyTileMapLayerObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                          const TileMapLayerObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<TileMapLayerComponentUVE>(entity, value.tileMap);
}

Math::Vector2UVE TileMapLayerUVE::MapToLocalPositionUVE(const TileMapLayerComponentUVE& layer,
                                                        const Math::Vector2iUVE& coord) noexcept {
    const Math::Vector2UVE safeSize =
        (Math::IsFiniteUVE(layer.tileSizePixels) && layer.tileSizePixels.x > 0.0F &&
         layer.tileSizePixels.y > 0.0F)
            ? layer.tileSizePixels
            : Math::Vector2UVE{16.0F, 16.0F};
    return Math::Vector2UVE{(static_cast<float>(coord.x) + 0.5F) * safeSize.x,
                            (static_cast<float>(coord.y) + 0.5F) * safeSize.y};
}

Math::Vector2iUVE TileMapLayerUVE::LocalPositionToMapUVE(
    const TileMapLayerComponentUVE& layer, const Math::Vector2UVE& localPosition) noexcept {
    if (!Math::IsFiniteUVE(localPosition)) {
        return Math::Vector2iUVE{0, 0};
    }
    const Math::Vector2UVE safeSize =
        (Math::IsFiniteUVE(layer.tileSizePixels) && layer.tileSizePixels.x > 0.0F &&
         layer.tileSizePixels.y > 0.0F)
            ? layer.tileSizePixels
            : Math::Vector2UVE{16.0F, 16.0F};
    return Math::Vector2iUVE{static_cast<std::int32_t>(std::floor(localPosition.x / safeSize.x)),
                             static_cast<std::int32_t>(std::floor(localPosition.y / safeSize.y))};
}

void TileMapLayerUVE::SetCellTileUVE(TileMapLayerComponentUVE& layer,
                                     const Math::Vector2iUVE& coord, const std::int32_t tileId) {
    const auto it = std::find_if(layer.cells.begin(), layer.cells.end(),
                                 [&](const TileMapCell2DUVE& c) { return c.coord == coord; });
    if (tileId < 0) {
        if (it != layer.cells.end()) {
            layer.cells.erase(it);
        }
        return;
    }
    if (it != layer.cells.end()) {
        it->tileId = tileId;
    } else if (layer.cells.size() < 16384U) {
        layer.cells.push_back(TileMapCell2DUVE{coord, tileId});
    }
}

std::int32_t TileMapLayerUVE::GetCellTileUVE(const TileMapLayerComponentUVE& layer,
                                             const Math::Vector2iUVE& coord) noexcept {
    for (const TileMapCell2DUVE& c : layer.cells) {
        if (c.coord == coord) {
            return c.tileId;
        }
    }
    return kTileMapInvalidTileUVE;
}

} // namespace UVE::Scene
