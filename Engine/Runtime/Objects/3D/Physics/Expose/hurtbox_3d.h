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

inline constexpr std::size_t kMaximumHurtbox3DHits = 16U;
inline constexpr std::size_t kMaximumHurtbox3DReceived = 64U;

struct Hurtbox3DHit final {
    Entity hitboxEntity{};
    float penetrationDepth = 0.0F;
    Math::Vector3 axis{};
    bool received = false;

    [[nodiscard]] bool operator==(const Hurtbox3DHit&) const noexcept = default;
};

struct Hurtbox3DComponent final {
    Math::Vector3 halfExtents{0.5F, 0.5F, 0.5F};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::string damageChannel = "default";
    bool enabled = true;
    Entity ignoreEntity = kInvalidEntity;
    std::array<Hurtbox3DHit, kMaximumHurtbox3DHits> hits{};
    std::uint8_t hitCount = 0U;
    bool hitsTruncated = false;
    std::array<Entity, kMaximumHurtbox3DReceived> receivedEntities =
        MakeEmptyEntityReferences<kMaximumHurtbox3DReceived>();
    std::uint8_t receivedCount = 0U;
    bool receivedTruncated = false;
};

[[nodiscard]] bool IsHurtbox3DObjectComponentValid(const Hurtbox3DComponent& value) noexcept;

class Hurtbox3D final {
public:
    [[nodiscard]] static bool IsVulnerable(const Hurtbox3DComponent& hurtbox) noexcept;

    [[nodiscard]] static bool AcceptsAttacker(Entity hurtboxEntity, const Hurtbox3DComponent& hurtbox,
                                                 Entity hitboxEntity, std::uint32_t hitboxLayer,
                                                 std::uint32_t hitboxMask,
                                                 std::string_view hitboxChannel) noexcept;

    static void ClearHits(Hurtbox3DComponent& hurtbox) noexcept;
    static void ResetReceived(Hurtbox3DComponent& hurtbox) noexcept;
    static void CommitHits(Hurtbox3DComponent& hurtbox, const Hurtbox3DHit* incoming,
                              std::size_t incomingCount);

    [[nodiscard]] static const Hurtbox3DHit* GetHit(const Hurtbox3DComponent& hurtbox,
                                                          std::size_t index) noexcept;
    [[nodiscard]] static bool HasHitFrom(const Hurtbox3DComponent& hurtbox, Entity hitbox) noexcept;
    [[nodiscard]] static bool HasReceived(const Hurtbox3DComponent& hurtbox, Entity hitbox) noexcept;
};

} // namespace UVE::Scene
