// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "entity.h"
#include "vector3.h"
#include "hitbox_3d.h"

namespace UVE::Scene::Tests {
namespace {

using Hitbox3D = Scene::Hitbox3D;

constexpr Entity kHitbox{1U, 1U};
constexpr Entity kHurtboxA{2U, 1U};
constexpr Entity kHurtboxB{3U, 1U};
constexpr Entity kOwner{4U, 1U};

[[nodiscard]] Hitbox3DStrike MakeOverlap(const Entity hurtbox, const float depth) {
    return Hitbox3DStrike{hurtbox, depth, Math::Vector3{1.0F, 0.0F, 0.0F}};
}

TEST(Hitbox3DTest, DefaultComponentIsArmedAndIgnoresNobody) {
    const Hitbox3DComponent hitbox{};
    EXPECT_TRUE(IsHitbox3DObjectComponentValid(hitbox));
    EXPECT_TRUE(Hitbox3D::IsArmed(hitbox));
    EXPECT_EQ(hitbox.ignoreEntity, kInvalidEntity);
    EXPECT_EQ(hitbox.strikeCount, 0U);
    EXPECT_EQ(hitbox.struckCount, 0U);
}

TEST(Hitbox3DTest, IsArmed_FalseWhenDisabledOrInvalid) {
    Hitbox3DComponent hitbox{};
    hitbox.enabled = false;
    EXPECT_FALSE(Hitbox3D::IsArmed(hitbox));
    hitbox.enabled = true;
    hitbox.collisionLayer = 0U;
    EXPECT_FALSE(Hitbox3D::IsArmed(hitbox));
}

TEST(Hitbox3DTest, AcceptsTarget_SkipsSelfIgnoreLayerAndChannel) {
    Hitbox3DComponent hitbox{};
    hitbox.collisionMask = 1U;
    hitbox.damageChannel = "melee";
    EXPECT_TRUE(Hitbox3D::AcceptsTarget(kHitbox, hitbox, kHurtboxA, 1U, 0xFFFFFFFFU, "melee"));
    EXPECT_FALSE(Hitbox3D::AcceptsTarget(kHitbox, hitbox, kHitbox, 1U, 0xFFFFFFFFU, "melee"));
    EXPECT_FALSE(Hitbox3D::AcceptsTarget(kHitbox, hitbox, kInvalidEntity, 1U, 0xFFFFFFFFU, "melee"));

    hitbox.ignoreEntity = kOwner;
    EXPECT_FALSE(Hitbox3D::AcceptsTarget(kHitbox, hitbox, kOwner, 1U, 0xFFFFFFFFU, "melee"));
    EXPECT_TRUE(Hitbox3D::AcceptsTarget(kHitbox, hitbox, kHurtboxA, 1U, 0xFFFFFFFFU, "melee"));

    EXPECT_FALSE(Hitbox3D::AcceptsTarget(kHitbox, hitbox, kHurtboxA, 2U, 0xFFFFFFFFU, "melee"));
    EXPECT_FALSE(Hitbox3D::AcceptsTarget(kHitbox, hitbox, kHurtboxA, 1U, 0x02U, "melee"));
    EXPECT_FALSE(Hitbox3D::AcceptsTarget(kHitbox, hitbox, kHurtboxA, 1U, 0xFFFFFFFFU, "projectile"));
}

TEST(Hitbox3DTest, CommitStrikes_UniquesDeepestFirstAndCaps) {
    Hitbox3DComponent hitbox{};
    const Hitbox3DStrike overlaps[] = {
        MakeOverlap(kHurtboxA, 0.25F),
        MakeOverlap(kHurtboxB, 0.80F),
        MakeOverlap(kHurtboxA, 0.40F),
    };
    Hitbox3D::CommitStrikes(hitbox, overlaps, 3U);

    ASSERT_EQ(hitbox.strikeCount, 2U);
    EXPECT_FALSE(hitbox.strikesTruncated);
    EXPECT_EQ(hitbox.strikes[0].hurtboxEntity, kHurtboxB);
    EXPECT_FLOAT_EQ(hitbox.strikes[0].penetrationDepth, 0.80F);
    EXPECT_EQ(hitbox.strikes[1].hurtboxEntity, kHurtboxA);
    EXPECT_FLOAT_EQ(hitbox.strikes[1].penetrationDepth, 0.40F);
    EXPECT_TRUE(Hitbox3D::HasStrikeAgainst(hitbox, kHurtboxA));
    EXPECT_EQ(Hitbox3D::GetStrike(hitbox, 2U), nullptr);

    std::vector<Hitbox3DStrike> overflow;
    overflow.reserve(kMaximumHitbox3DStrikes + 1U);
    for (std::uint32_t index = 0U; index < kMaximumHitbox3DStrikes + 1U; ++index) {
        overflow.push_back(MakeOverlap(Entity{10U + index, 1U}, 1.0F - static_cast<float>(index) * 0.01F));
    }
    Hitbox3D::ResetActivation(hitbox);
    Hitbox3D::CommitStrikes(hitbox, overflow.data(), overflow.size());
    EXPECT_EQ(hitbox.strikeCount, kMaximumHitbox3DStrikes);
    EXPECT_TRUE(hitbox.strikesTruncated);
    EXPECT_EQ(hitbox.strikes[0].hurtboxEntity, overflow.front().hurtboxEntity);
}

TEST(Hitbox3DTest, CommitStrikes_LandsOncePerActivationWhileKeepingTheOverlap) {
    Hitbox3DComponent hitbox{};
    const Hitbox3DStrike overlaps[] = {MakeOverlap(kHurtboxA, 0.5F)};

    Hitbox3D::CommitStrikes(hitbox, overlaps, 1U);
    ASSERT_EQ(hitbox.strikeCount, 1U);
    EXPECT_TRUE(hitbox.strikes[0].landed);
    EXPECT_TRUE(Hitbox3D::HasStruck(hitbox, kHurtboxA));
    EXPECT_EQ(hitbox.struckCount, 1U);

    Hitbox3D::CommitStrikes(hitbox, overlaps, 1U);
    ASSERT_EQ(hitbox.strikeCount, 1U);
    EXPECT_FALSE(hitbox.strikes[0].landed);
    EXPECT_TRUE(Hitbox3D::HasStrikeAgainst(hitbox, kHurtboxA));
    EXPECT_EQ(hitbox.struckCount, 1U);

    Hitbox3D::ClearStrikes(hitbox);
    EXPECT_EQ(hitbox.strikeCount, 0U);
    EXPECT_TRUE(Hitbox3D::HasStruck(hitbox, kHurtboxA));

    Hitbox3D::ResetActivation(hitbox);
    EXPECT_FALSE(Hitbox3D::HasStruck(hitbox, kHurtboxA));
    Hitbox3D::CommitStrikes(hitbox, overlaps, 1U);
    ASSERT_EQ(hitbox.strikeCount, 1U);
    EXPECT_TRUE(hitbox.strikes[0].landed);
}

TEST(Hitbox3DTest, CommitStrikes_DropsInvalidOverlaps) {
    Hitbox3DComponent hitbox{};
    const Hitbox3DStrike overlaps[] = {
        MakeOverlap(kInvalidEntity, 0.5F),
        MakeOverlap(kHurtboxA, 0.0F),
        Hitbox3DStrike{kHurtboxB, std::numeric_limits<float>::infinity(), {1.0F, 0.0F, 0.0F}},
        MakeOverlap(kHurtboxA, 0.3F),
    };
    Hitbox3D::CommitStrikes(hitbox, overlaps, 4U);
    ASSERT_EQ(hitbox.strikeCount, 1U);
    EXPECT_EQ(hitbox.strikes[0].hurtboxEntity, kHurtboxA);
    EXPECT_FLOAT_EQ(hitbox.strikes[0].penetrationDepth, 0.3F);
}

} // namespace
} // namespace UVE::Scene::Tests
