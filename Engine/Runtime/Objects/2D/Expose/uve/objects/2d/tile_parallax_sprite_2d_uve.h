// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector2i_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Frame-animated 2D sprite component (`hframes`, `vframes`, `frame`, `framesPerSecond`,
/// `playing`, `loop`).
struct AnimatedSprite2DComponentUVE final {
    Asset::AssetGuidUVE textureAssetGuid = Asset::kInvalidAssetGuidUVE;
    std::uint32_t hframes = 1U;
    std::uint32_t vframes = 1U;
    std::uint32_t frame = 0U;
    float framesPerSecond = 12.0F;
    bool playing = true;
    bool loop = true;

    // Runtime state, never persisted:
    float frameAccumulatorSeconds = 0.0F;

    [[nodiscard]] bool operator==(const AnimatedSprite2DComponentUVE& other) const noexcept {
        return textureAssetGuid == other.textureAssetGuid && hframes == other.hframes &&
               vframes == other.vframes && frame == other.frame &&
               framesPerSecond == other.framesPerSecond && playing == other.playing &&
               loop == other.loop;
    }
};

struct AnimatedSprite2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "AnimatedSprite2D";
    AnimatedSprite2DComponentUVE sprite{};
};

void ApplyAnimatedSprite2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                              const AnimatedSprite2DObjectDefinitionUVE& value);

std::size_t UpdateAnimatedSprite2DObjectsUVE(IEntityManagerUVE& entityManager,
                                             float deltaTimeSeconds);

/// 2D scrolling-background depth layer (`scrollScale`, `scrollOffset`, `repeatSize`).
struct Parallax2DComponentUVE final {
    Math::Vector2UVE scrollScale{0.5F, 0.5F};
    Math::Vector2UVE scrollOffset{0.0F, 0.0F};
    Math::Vector2UVE repeatSize{0.0F, 0.0F};
    bool enabled = true;

    [[nodiscard]] bool operator==(const Parallax2DComponentUVE&) const = default;
};

struct Parallax2DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Parallax2D";
    Parallax2DComponentUVE parallax{};
};

void ApplyParallax2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                        const Parallax2DObjectDefinitionUVE& value);

class Parallax2DUVE final {
public:
    Parallax2DUVE() = delete;

    /// Computes the effective 2D layer offset for `cameraPosition` under `scrollScale`,
    /// `scrollOffset`, and optional periodic wrapping (`repeatSize`).
    [[nodiscard]] static Math::Vector2UVE ComputeEffectiveOffsetUVE(
        const Parallax2DComponentUVE& parallax, const Math::Vector2UVE& cameraPosition) noexcept;
};

inline constexpr std::int32_t kTileMapInvalidTileUVE = -1;

struct TileMapCell2DUVE final {
    Math::Vector2iUVE coord{0, 0};
    std::int32_t tileId = 0;

    [[nodiscard]] bool operator==(const TileMapCell2DUVE&) const = default;
};

/// 2D grid-based tile layer component (`tileSizePixels`, `cells`, `collisionEnabled`).
struct TileMapLayerComponentUVE final {
    Math::Vector2UVE tileSizePixels{16.0F, 16.0F};
    std::vector<TileMapCell2DUVE> cells{};
    bool collisionEnabled = true;
    bool enabled = true;

    [[nodiscard]] bool operator==(const TileMapLayerComponentUVE&) const = default;
};

struct TileMapLayerObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "TileMapLayer";
    TileMapLayerComponentUVE tileMap{};
};

void ApplyTileMapLayerObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                          const TileMapLayerObjectDefinitionUVE& value);

class TileMapLayerUVE final {
public:
    TileMapLayerUVE() = delete;

    [[nodiscard]] static Math::Vector2UVE MapToLocalPositionUVE(
        const TileMapLayerComponentUVE& layer, const Math::Vector2iUVE& coord) noexcept;

    [[nodiscard]] static Math::Vector2iUVE LocalPositionToMapUVE(
        const TileMapLayerComponentUVE& layer, const Math::Vector2UVE& localPosition) noexcept;

    static void SetCellTileUVE(TileMapLayerComponentUVE& layer, const Math::Vector2iUVE& coord,
                               std::int32_t tileId);

    [[nodiscard]] static std::int32_t GetCellTileUVE(const TileMapLayerComponentUVE& layer,
                                                     const Math::Vector2iUVE& coord) noexcept;
};

} // namespace UVE::Scene
