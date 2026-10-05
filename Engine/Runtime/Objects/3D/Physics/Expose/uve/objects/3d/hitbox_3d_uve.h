// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumHitbox3DStrikesUVE = 16U;
inline constexpr std::size_t kMaximumHitbox3DStruckUVE = 64U;

struct Hitbox3DStrikeUVE final {
    EntityUVE hurtboxEntity{};
    float penetrationDepth = 0.0F;
    Math::Vector3UVE axis{};
    bool landed = false;

    [[nodiscard]] bool operator==(const Hitbox3DStrikeUVE&) const noexcept = default;
};

struct Hitbox3DComponentUVE final {
    Math::Vector3UVE halfExtents{0.5F, 0.5F, 0.5F};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::string damageChannel = "default";
    bool enabled = true;
    EntityUVE ignoreEntity = kInvalidEntityUVE;
    std::array<Hitbox3DStrikeUVE, kMaximumHitbox3DStrikesUVE> strikes{};
    std::uint8_t strikeCount = 0U;
    bool strikesTruncated = false;
    std::array<EntityUVE, kMaximumHitbox3DStruckUVE> struckEntities =
        MakeEmptyEntityReferencesUVE<kMaximumHitbox3DStruckUVE>();
    std::uint8_t struckCount = 0U;
    bool struckTruncated = false;
};

[[nodiscard]] bool IsHitbox3DObjectComponentValidUVE(const Hitbox3DComponentUVE& value) noexcept;

class Hitbox3DUVE final {
public:
    [[nodiscard]] static bool IsArmedUVE(const Hitbox3DComponentUVE& hitbox) noexcept;

    [[nodiscard]] static bool AcceptsTargetUVE(EntityUVE hitboxEntity, const Hitbox3DComponentUVE& hitbox,
                                               EntityUVE hurtboxEntity, std::uint32_t hurtboxLayer,
                                               std::uint32_t hurtboxMask,
                                               std::string_view hurtboxChannel) noexcept;

    static void ClearStrikesUVE(Hitbox3DComponentUVE& hitbox) noexcept;
    static void ResetActivationUVE(Hitbox3DComponentUVE& hitbox) noexcept;
    static void CommitStrikesUVE(Hitbox3DComponentUVE& hitbox, const Hitbox3DStrikeUVE* overlaps,
                                 std::size_t overlapCount);

    [[nodiscard]] static const Hitbox3DStrikeUVE* GetStrikeUVE(const Hitbox3DComponentUVE& hitbox,
                                                               std::size_t index) noexcept;
    [[nodiscard]] static bool HasStrikeAgainstUVE(const Hitbox3DComponentUVE& hitbox,
                                                  EntityUVE hurtbox) noexcept;
    [[nodiscard]] static bool HasStruckUVE(const Hitbox3DComponentUVE& hitbox, EntityUVE hurtbox) noexcept;
};

} // namespace UVE::Scene
