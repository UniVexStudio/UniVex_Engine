// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/hitbox_3d_uve.h"

namespace UVE::Scene::Tests {
namespace {

using Hitbox3DUVE = Scene::Hitbox3DUVE;

constexpr EntityUVE kHitbox{1U, 1U};
constexpr EntityUVE kHurtboxA{2U, 1U};
constexpr EntityUVE kHurtboxB{3U, 1U};
constexpr EntityUVE kOwner{4U, 1U};

[[nodiscard]] Hitbox3DStrikeUVE MakeOverlapUVE(const EntityUVE hurtbox, const float depth) {
    return Hitbox3DStrikeUVE{hurtbox, depth, Math::Vector3UVE{1.0F, 0.0F, 0.0F}};
}

TEST(Hitbox3DUVETest, DefaultComponentIsArmedAndIgnoresNobody) {
    const Hitbox3DComponentUVE hitbox{};
    EXPECT_TRUE(IsHitbox3DObjectComponentValidUVE(hitbox));
    EXPECT_TRUE(Hitbox3DUVE::IsArmedUVE(hitbox));
    EXPECT_EQ(hitbox.ignoreEntity, kInvalidEntityUVE);
    EXPECT_EQ(hitbox.strikeCount, 0U);
    EXPECT_EQ(hitbox.struckCount, 0U);
}

TEST(Hitbox3DUVETest, IsArmedUVE_FalseWhenDisabledOrInvalid) {
    Hitbox3DComponentUVE hitbox{};
    hitbox.enabled = false;
    EXPECT_FALSE(Hitbox3DUVE::IsArmedUVE(hitbox));
    hitbox.enabled = true;
    hitbox.collisionLayer = 0U;
    EXPECT_FALSE(Hitbox3DUVE::IsArmedUVE(hitbox));
}

TEST(Hitbox3DUVETest, AcceptsTargetUVE_SkipsSelfIgnoreLayerAndChannel) {
    Hitbox3DComponentUVE hitbox{};
    hitbox.damageChannel = "melee";
    EXPECT_TRUE(Hitbox3DUVE::AcceptsTargetUVE(kHitbox, hitbox, kHurtboxA, 1U, 0xFFFFFFFFU, "melee"));
    EXPECT_FALSE(Hitbox3DUVE::AcceptsTargetUVE(kHitbox, hitbox, kHitbox, 1U, 0xFFFFFFFFU, "melee"));
    EXPECT_FALSE(Hitbox3DUVE::AcceptsTargetUVE(kHitbox, hitbox, kInvalidEntityUVE, 1U, 0xFFFFFFFFU, "melee"));

    hitbox.ignoreEntity = kOwner;
    EXPECT_FALSE(Hitbox3DUVE::AcceptsTargetUVE(kHitbox, hitbox, kOwner, 1U, 0xFFFFFFFFU, "melee"));
    EXPECT_TRUE(Hitbox3DUVE::AcceptsTargetUVE(kHitbox, hitbox, kHurtboxA, 1U, 0xFFFFFFFFU, "melee"));

    EXPECT_FALSE(Hitbox3DUVE::AcceptsTargetUVE(kHitbox, hitbox, kHurtboxA, 2U, 0xFFFFFFFFU, "melee"));
    EXPECT_FALSE(Hitbox3DUVE::AcceptsTargetUVE(kHitbox, hitbox, kHurtboxA, 1U, 0x02U, "melee"));
    EXPECT_FALSE(Hitbox3DUVE::AcceptsTargetUVE(kHitbox, hitbox, kHurtboxA, 1U, 0xFFFFFFFFU, "projectile"));
}

TEST(Hitbox3DUVETest, CommitStrikesUVE_UniquesDeepestFirstAndCaps) {
    Hitbox3DComponentUVE hitbox{};
    const Hitbox3DStrikeUVE overlaps[] = {
        MakeOverlapUVE(kHurtboxA, 0.25F),
        MakeOverlapUVE(kHurtboxB, 0.80F),
        MakeOverlapUVE(kHurtboxA, 0.40F),
    };
    Hitbox3DUVE::CommitStrikesUVE(hitbox, overlaps, 3U);

    ASSERT_EQ(hitbox.strikeCount, 2U);
    EXPECT_FALSE(hitbox.strikesTruncated);
    EXPECT_EQ(hitbox.strikes[0].hurtboxEntity, kHurtboxB);
    EXPECT_FLOAT_EQ(hitbox.strikes[0].penetrationDepth, 0.80F);
    EXPECT_EQ(hitbox.strikes[1].hurtboxEntity, kHurtboxA);
    EXPECT_FLOAT_EQ(hitbox.strikes[1].penetrationDepth, 0.40F);
    EXPECT_TRUE(Hitbox3DUVE::HasStrikeAgainstUVE(hitbox, kHurtboxA));
    EXPECT_EQ(Hitbox3DUVE::GetStrikeUVE(hitbox, 2U), nullptr);

    std::vector<Hitbox3DStrikeUVE> overflow;
    overflow.reserve(kMaximumHitbox3DStrikesUVE + 1U);
    for (std::uint32_t index = 0U; index < kMaximumHitbox3DStrikesUVE + 1U; ++index) {
        overflow.push_back(MakeOverlapUVE(EntityUVE{10U + index, 1U}, 1.0F - static_cast<float>(index) * 0.01F));
    }
    Hitbox3DUVE::ResetActivationUVE(hitbox);
    Hitbox3DUVE::CommitStrikesUVE(hitbox, overflow.data(), overflow.size());
    EXPECT_EQ(hitbox.strikeCount, kMaximumHitbox3DStrikesUVE);
    EXPECT_TRUE(hitbox.strikesTruncated);
    EXPECT_EQ(hitbox.strikes[0].hurtboxEntity, overflow.front().hurtboxEntity);
}

TEST(Hitbox3DUVETest, CommitStrikesUVE_LandsOncePerActivationWhileKeepingTheOverlap) {
    Hitbox3DComponentUVE hitbox{};
    const Hitbox3DStrikeUVE overlaps[] = {MakeOverlapUVE(kHurtboxA, 0.5F)};

    Hitbox3DUVE::CommitStrikesUVE(hitbox, overlaps, 1U);
    ASSERT_EQ(hitbox.strikeCount, 1U);
    EXPECT_TRUE(hitbox.strikes[0].landed);
    EXPECT_TRUE(Hitbox3DUVE::HasStruckUVE(hitbox, kHurtboxA));
    EXPECT_EQ(hitbox.struckCount, 1U);

    Hitbox3DUVE::CommitStrikesUVE(hitbox, overlaps, 1U);
    ASSERT_EQ(hitbox.strikeCount, 1U);
    EXPECT_FALSE(hitbox.strikes[0].landed);
    EXPECT_TRUE(Hitbox3DUVE::HasStrikeAgainstUVE(hitbox, kHurtboxA));
    EXPECT_EQ(hitbox.struckCount, 1U);

    Hitbox3DUVE::ClearStrikesUVE(hitbox);
    EXPECT_EQ(hitbox.strikeCount, 0U);
    EXPECT_TRUE(Hitbox3DUVE::HasStruckUVE(hitbox, kHurtboxA));

    Hitbox3DUVE::ResetActivationUVE(hitbox);
    EXPECT_FALSE(Hitbox3DUVE::HasStruckUVE(hitbox, kHurtboxA));
    Hitbox3DUVE::CommitStrikesUVE(hitbox, overlaps, 1U);
    ASSERT_EQ(hitbox.strikeCount, 1U);
    EXPECT_TRUE(hitbox.strikes[0].landed);
}

TEST(Hitbox3DUVETest, CommitStrikesUVE_DropsInvalidOverlaps) {
    Hitbox3DComponentUVE hitbox{};
    const Hitbox3DStrikeUVE overlaps[] = {
        MakeOverlapUVE(kInvalidEntityUVE, 0.5F),
        MakeOverlapUVE(kHurtboxA, 0.0F),
        Hitbox3DStrikeUVE{kHurtboxB, std::numeric_limits<float>::infinity(), {1.0F, 0.0F, 0.0F}},
        MakeOverlapUVE(kHurtboxA, 0.3F),
    };
    Hitbox3DUVE::CommitStrikesUVE(hitbox, overlaps, 4U);
    ASSERT_EQ(hitbox.strikeCount, 1U);
    EXPECT_EQ(hitbox.strikes[0].hurtboxEntity, kHurtboxA);
    EXPECT_FLOAT_EQ(hitbox.strikes[0].penetrationDepth, 0.3F);
}

} // namespace
} // namespace UVE::Scene::Tests
