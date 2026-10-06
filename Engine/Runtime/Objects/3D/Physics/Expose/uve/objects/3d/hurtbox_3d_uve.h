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

inline constexpr std::size_t kMaximumHurtbox3DHitsUVE = 16U;
inline constexpr std::size_t kMaximumHurtbox3DReceivedUVE = 64U;

struct Hurtbox3DHitUVE final {
    EntityUVE hitboxEntity{};
    float penetrationDepth = 0.0F;
    Math::Vector3UVE axis{};
    bool received = false;

    [[nodiscard]] bool operator==(const Hurtbox3DHitUVE&) const noexcept = default;
};

struct Hurtbox3DComponentUVE final {
    Math::Vector3UVE halfExtents{0.5F, 0.5F, 0.5F};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::string damageChannel = "default";
    bool enabled = true;
    EntityUVE ignoreEntity = kInvalidEntityUVE;
    std::array<Hurtbox3DHitUVE, kMaximumHurtbox3DHitsUVE> hits{};
    std::uint8_t hitCount = 0U;
    bool hitsTruncated = false;
    std::array<EntityUVE, kMaximumHurtbox3DReceivedUVE> receivedEntities =
        MakeEmptyEntityReferencesUVE<kMaximumHurtbox3DReceivedUVE>();
    std::uint8_t receivedCount = 0U;
    bool receivedTruncated = false;
};

[[nodiscard]] bool IsHurtbox3DObjectComponentValidUVE(const Hurtbox3DComponentUVE& value) noexcept;

class Hurtbox3DUVE final {
public:
    [[nodiscard]] static bool IsVulnerableUVE(const Hurtbox3DComponentUVE& hurtbox) noexcept;

    [[nodiscard]] static bool AcceptsAttackerUVE(EntityUVE hurtboxEntity, const Hurtbox3DComponentUVE& hurtbox,
                                                 EntityUVE hitboxEntity, std::uint32_t hitboxLayer,
                                                 std::uint32_t hitboxMask,
                                                 std::string_view hitboxChannel) noexcept;

    static void ClearHitsUVE(Hurtbox3DComponentUVE& hurtbox) noexcept;
    static void ResetReceivedUVE(Hurtbox3DComponentUVE& hurtbox) noexcept;
    static void CommitHitsUVE(Hurtbox3DComponentUVE& hurtbox, const Hurtbox3DHitUVE* incoming,
                              std::size_t incomingCount);

    [[nodiscard]] static const Hurtbox3DHitUVE* GetHitUVE(const Hurtbox3DComponentUVE& hurtbox,
                                                          std::size_t index) noexcept;
    [[nodiscard]] static bool HasHitFromUVE(const Hurtbox3DComponentUVE& hurtbox, EntityUVE hitbox) noexcept;
    [[nodiscard]] static bool HasReceivedUVE(const Hurtbox3DComponentUVE& hurtbox, EntityUVE hitbox) noexcept;
};

} // namespace UVE::Scene
