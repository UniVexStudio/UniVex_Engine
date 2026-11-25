// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "entity.h"
#include "object_3d_common.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumHitbox3DStrikes = 16U;
inline constexpr std::size_t kMaximumHitbox3DStruck = 64U;

struct Hitbox3DStrike final {
    Entity hurtboxEntity{};
    float penetrationDepth = 0.0F;
    Math::Vector3 axis{};
    bool landed = false;

    [[nodiscard]] bool operator==(const Hitbox3DStrike&) const noexcept = default;
};

struct Hitbox3DComponent final {
    Math::Vector3 halfExtents{0.5F, 0.5F, 0.5F};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::string damageChannel = "default";
    bool enabled = true;
    Entity ignoreEntity = kInvalidEntity;
    std::array<Hitbox3DStrike, kMaximumHitbox3DStrikes> strikes{};
    std::uint8_t strikeCount = 0U;
    bool strikesTruncated = false;
    std::array<Entity, kMaximumHitbox3DStruck> struckEntities =
        MakeEmptyEntityReferences<kMaximumHitbox3DStruck>();
    std::uint8_t struckCount = 0U;
    bool struckTruncated = false;
};

[[nodiscard]] bool IsHitbox3DObjectComponentValid(const Hitbox3DComponent& value) noexcept;

class Hitbox3D final {
public:
    [[nodiscard]] static bool IsArmed(const Hitbox3DComponent& hitbox) noexcept;

    [[nodiscard]] static bool AcceptsTarget(Entity hitboxEntity, const Hitbox3DComponent& hitbox,
                                               Entity hurtboxEntity, std::uint32_t hurtboxLayer,
                                               std::uint32_t hurtboxMask,
                                               std::string_view hurtboxChannel) noexcept;

    static void ClearStrikes(Hitbox3DComponent& hitbox) noexcept;
    static void ResetActivation(Hitbox3DComponent& hitbox) noexcept;
    static void CommitStrikes(Hitbox3DComponent& hitbox, const Hitbox3DStrike* overlaps,
                                 std::size_t overlapCount);

    [[nodiscard]] static const Hitbox3DStrike* GetStrike(const Hitbox3DComponent& hitbox,
                                                               std::size_t index) noexcept;
    [[nodiscard]] static bool HasStrikeAgainst(const Hitbox3DComponent& hitbox,
                                                  Entity hurtbox) noexcept;
    [[nodiscard]] static bool HasStruck(const Hitbox3DComponent& hitbox, Entity hurtbox) noexcept;
};

} // namespace UVE::Scene
