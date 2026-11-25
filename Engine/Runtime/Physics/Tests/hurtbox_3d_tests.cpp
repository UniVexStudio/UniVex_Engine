// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "entity.h"
#include "vector3.h"
#include "hurtbox_3d.h"

namespace UVE::Scene::Tests {
namespace {

constexpr Entity kHurtbox{1U, 1U};
constexpr Entity kHitboxA{2U, 1U};
constexpr Entity kHitboxB{3U, 1U};
constexpr Entity kOwner{4U, 1U};

[[nodiscard]] Hurtbox3DHit MakeIncoming(const Entity hitbox, const float depth) {
    return Hurtbox3DHit{hitbox, depth, Math::Vector3{1.0F, 0.0F, 0.0F}};
}

TEST(Hurtbox3DTest, DefaultComponentIsVulnerableAndIgnoresNobody) {
    const Hurtbox3DComponent hurtbox{};
    EXPECT_TRUE(IsHurtbox3DObjectComponentValid(hurtbox));
    EXPECT_TRUE(Hurtbox3D::IsVulnerable(hurtbox));
    EXPECT_EQ(hurtbox.ignoreEntity, kInvalidEntity);
    EXPECT_EQ(hurtbox.hitCount, 0U);
    EXPECT_EQ(hurtbox.receivedCount, 0U);
}

TEST(Hurtbox3DTest, IsVulnerable_FalseWhenDisabledOrInvalid) {
    Hurtbox3DComponent hurtbox{};
    hurtbox.enabled = false;
    EXPECT_FALSE(Hurtbox3D::IsVulnerable(hurtbox));
    hurtbox.enabled = true;
    hurtbox.collisionLayer = 0U;
    EXPECT_FALSE(Hurtbox3D::IsVulnerable(hurtbox));
}

TEST(Hurtbox3DTest, AcceptsAttacker_SkipsSelfIgnoreLayerAndChannel) {
    Hurtbox3DComponent hurtbox{};
    hurtbox.collisionMask = 1U;
    hurtbox.damageChannel = "melee";
    EXPECT_TRUE(Hurtbox3D::AcceptsAttacker(kHurtbox, hurtbox, kHitboxA, 1U, 0xFFFFFFFFU, "melee"));
    EXPECT_FALSE(Hurtbox3D::AcceptsAttacker(kHurtbox, hurtbox, kHurtbox, 1U, 0xFFFFFFFFU, "melee"));
    EXPECT_FALSE(Hurtbox3D::AcceptsAttacker(kHurtbox, hurtbox, kInvalidEntity, 1U, 0xFFFFFFFFU, "melee"));

    hurtbox.ignoreEntity = kOwner;
    EXPECT_FALSE(Hurtbox3D::AcceptsAttacker(kHurtbox, hurtbox, kOwner, 1U, 0xFFFFFFFFU, "melee"));
    EXPECT_TRUE(Hurtbox3D::AcceptsAttacker(kHurtbox, hurtbox, kHitboxA, 1U, 0xFFFFFFFFU, "melee"));

    EXPECT_FALSE(Hurtbox3D::AcceptsAttacker(kHurtbox, hurtbox, kHitboxA, 2U, 0xFFFFFFFFU, "melee"));
    EXPECT_FALSE(Hurtbox3D::AcceptsAttacker(kHurtbox, hurtbox, kHitboxA, 1U, 0x02U, "melee"));
    EXPECT_FALSE(Hurtbox3D::AcceptsAttacker(kHurtbox, hurtbox, kHitboxA, 1U, 0xFFFFFFFFU, "projectile"));
}

TEST(Hurtbox3DTest, CommitHits_UniquesDeepestFirstAndCaps) {
    Hurtbox3DComponent hurtbox{};
    const Hurtbox3DHit incoming[] = {
        MakeIncoming(kHitboxA, 0.25F),
        MakeIncoming(kHitboxB, 0.80F),
        MakeIncoming(kHitboxA, 0.40F),
    };
    Hurtbox3D::CommitHits(hurtbox, incoming, 3U);

    ASSERT_EQ(hurtbox.hitCount, 2U);
    EXPECT_FALSE(hurtbox.hitsTruncated);
    EXPECT_EQ(hurtbox.hits[0].hitboxEntity, kHitboxB);
    EXPECT_FLOAT_EQ(hurtbox.hits[0].penetrationDepth, 0.80F);
    EXPECT_EQ(hurtbox.hits[1].hitboxEntity, kHitboxA);
    EXPECT_FLOAT_EQ(hurtbox.hits[1].penetrationDepth, 0.40F);
    EXPECT_TRUE(Hurtbox3D::HasHitFrom(hurtbox, kHitboxA));
    EXPECT_EQ(Hurtbox3D::GetHit(hurtbox, 2U), nullptr);

    std::vector<Hurtbox3DHit> overflow;
    overflow.reserve(kMaximumHurtbox3DHits + 1U);
    for (std::uint32_t index = 0U; index < kMaximumHurtbox3DHits + 1U; ++index) {
        overflow.push_back(MakeIncoming(Entity{10U + index, 1U}, 1.0F - static_cast<float>(index) * 0.01F));
    }
    Hurtbox3D::ResetReceived(hurtbox);
    Hurtbox3D::CommitHits(hurtbox, overflow.data(), overflow.size());
    EXPECT_EQ(hurtbox.hitCount, kMaximumHurtbox3DHits);
    EXPECT_TRUE(hurtbox.hitsTruncated);
    EXPECT_EQ(hurtbox.hits[0].hitboxEntity, overflow.front().hitboxEntity);
}

TEST(Hurtbox3DTest, CommitHits_ReceivesOncePerEnableWhileKeepingTheHit) {
    Hurtbox3DComponent hurtbox{};
    const Hurtbox3DHit incoming[] = {MakeIncoming(kHitboxA, 0.5F)};

    Hurtbox3D::CommitHits(hurtbox, incoming, 1U);
    ASSERT_EQ(hurtbox.hitCount, 1U);
    EXPECT_TRUE(hurtbox.hits[0].received);
    EXPECT_TRUE(Hurtbox3D::HasReceived(hurtbox, kHitboxA));
    EXPECT_EQ(hurtbox.receivedCount, 1U);

    Hurtbox3D::CommitHits(hurtbox, incoming, 1U);
    ASSERT_EQ(hurtbox.hitCount, 1U);
    EXPECT_FALSE(hurtbox.hits[0].received);
    EXPECT_TRUE(Hurtbox3D::HasHitFrom(hurtbox, kHitboxA));
    EXPECT_EQ(hurtbox.receivedCount, 1U);

    Hurtbox3D::ClearHits(hurtbox);
    EXPECT_EQ(hurtbox.hitCount, 0U);
    EXPECT_TRUE(Hurtbox3D::HasReceived(hurtbox, kHitboxA));

    Hurtbox3D::ResetReceived(hurtbox);
    EXPECT_FALSE(Hurtbox3D::HasReceived(hurtbox, kHitboxA));
    Hurtbox3D::CommitHits(hurtbox, incoming, 1U);
    ASSERT_EQ(hurtbox.hitCount, 1U);
    EXPECT_TRUE(hurtbox.hits[0].received);
}

TEST(Hurtbox3DTest, CommitHits_DropsInvalidIncoming) {
    Hurtbox3DComponent hurtbox{};
    const Hurtbox3DHit incoming[] = {
        MakeIncoming(kInvalidEntity, 0.5F),
        MakeIncoming(kHitboxA, 0.0F),
        Hurtbox3DHit{kHitboxB, std::numeric_limits<float>::infinity(), {1.0F, 0.0F, 0.0F}},
        MakeIncoming(kHitboxA, 0.3F),
    };
    Hurtbox3D::CommitHits(hurtbox, incoming, 4U);
    ASSERT_EQ(hurtbox.hitCount, 1U);
    EXPECT_EQ(hurtbox.hits[0].hitboxEntity, kHitboxA);
    EXPECT_FLOAT_EQ(hurtbox.hits[0].penetrationDepth, 0.3F);
}

} // namespace
} // namespace UVE::Scene::Tests
