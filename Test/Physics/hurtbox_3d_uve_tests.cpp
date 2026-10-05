// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/hurtbox_3d_uve.h"

namespace UVE::Scene::Tests {
namespace {

constexpr EntityUVE kHurtbox{1U, 1U};
constexpr EntityUVE kHitboxA{2U, 1U};
constexpr EntityUVE kHitboxB{3U, 1U};
constexpr EntityUVE kOwner{4U, 1U};

[[nodiscard]] Hurtbox3DHitUVE MakeIncomingUVE(const EntityUVE hitbox, const float depth) {
    return Hurtbox3DHitUVE{hitbox, depth, Math::Vector3UVE{1.0F, 0.0F, 0.0F}};
}

TEST(Hurtbox3DUVETest, DefaultComponentIsVulnerableAndIgnoresNobody) {
    const Hurtbox3DComponentUVE hurtbox{};
    EXPECT_TRUE(IsHurtbox3DObjectComponentValidUVE(hurtbox));
    EXPECT_TRUE(Hurtbox3DUVE::IsVulnerableUVE(hurtbox));
    EXPECT_EQ(hurtbox.ignoreEntity, kInvalidEntityUVE);
    EXPECT_EQ(hurtbox.hitCount, 0U);
    EXPECT_EQ(hurtbox.receivedCount, 0U);
}

TEST(Hurtbox3DUVETest, IsVulnerableUVE_FalseWhenDisabledOrInvalid) {
    Hurtbox3DComponentUVE hurtbox{};
    hurtbox.enabled = false;
    EXPECT_FALSE(Hurtbox3DUVE::IsVulnerableUVE(hurtbox));
    hurtbox.enabled = true;
    hurtbox.collisionLayer = 0U;
    EXPECT_FALSE(Hurtbox3DUVE::IsVulnerableUVE(hurtbox));
}

TEST(Hurtbox3DUVETest, AcceptsAttackerUVE_SkipsSelfIgnoreLayerAndChannel) {
    Hurtbox3DComponentUVE hurtbox{};
    hurtbox.damageChannel = "melee";
    EXPECT_TRUE(Hurtbox3DUVE::AcceptsAttackerUVE(kHurtbox, hurtbox, kHitboxA, 1U, 0xFFFFFFFFU, "melee"));
    EXPECT_FALSE(Hurtbox3DUVE::AcceptsAttackerUVE(kHurtbox, hurtbox, kHurtbox, 1U, 0xFFFFFFFFU, "melee"));
    EXPECT_FALSE(Hurtbox3DUVE::AcceptsAttackerUVE(kHurtbox, hurtbox, kInvalidEntityUVE, 1U, 0xFFFFFFFFU, "melee"));

    hurtbox.ignoreEntity = kOwner;
    EXPECT_FALSE(Hurtbox3DUVE::AcceptsAttackerUVE(kHurtbox, hurtbox, kOwner, 1U, 0xFFFFFFFFU, "melee"));
    EXPECT_TRUE(Hurtbox3DUVE::AcceptsAttackerUVE(kHurtbox, hurtbox, kHitboxA, 1U, 0xFFFFFFFFU, "melee"));

    EXPECT_FALSE(Hurtbox3DUVE::AcceptsAttackerUVE(kHurtbox, hurtbox, kHitboxA, 2U, 0xFFFFFFFFU, "melee"));
    EXPECT_FALSE(Hurtbox3DUVE::AcceptsAttackerUVE(kHurtbox, hurtbox, kHitboxA, 1U, 0x02U, "melee"));
    EXPECT_FALSE(Hurtbox3DUVE::AcceptsAttackerUVE(kHurtbox, hurtbox, kHitboxA, 1U, 0xFFFFFFFFU, "projectile"));
}

TEST(Hurtbox3DUVETest, CommitHitsUVE_UniquesDeepestFirstAndCaps) {
    Hurtbox3DComponentUVE hurtbox{};
    const Hurtbox3DHitUVE incoming[] = {
        MakeIncomingUVE(kHitboxA, 0.25F),
        MakeIncomingUVE(kHitboxB, 0.80F),
        MakeIncomingUVE(kHitboxA, 0.40F),
    };
    Hurtbox3DUVE::CommitHitsUVE(hurtbox, incoming, 3U);

    ASSERT_EQ(hurtbox.hitCount, 2U);
    EXPECT_FALSE(hurtbox.hitsTruncated);
    EXPECT_EQ(hurtbox.hits[0].hitboxEntity, kHitboxB);
    EXPECT_FLOAT_EQ(hurtbox.hits[0].penetrationDepth, 0.80F);
    EXPECT_EQ(hurtbox.hits[1].hitboxEntity, kHitboxA);
    EXPECT_FLOAT_EQ(hurtbox.hits[1].penetrationDepth, 0.40F);
    EXPECT_TRUE(Hurtbox3DUVE::HasHitFromUVE(hurtbox, kHitboxA));
    EXPECT_EQ(Hurtbox3DUVE::GetHitUVE(hurtbox, 2U), nullptr);

    std::vector<Hurtbox3DHitUVE> overflow;
    overflow.reserve(kMaximumHurtbox3DHitsUVE + 1U);
    for (std::uint32_t index = 0U; index < kMaximumHurtbox3DHitsUVE + 1U; ++index) {
        overflow.push_back(MakeIncomingUVE(EntityUVE{10U + index, 1U}, 1.0F - static_cast<float>(index) * 0.01F));
    }
    Hurtbox3DUVE::ResetReceivedUVE(hurtbox);
    Hurtbox3DUVE::CommitHitsUVE(hurtbox, overflow.data(), overflow.size());
    EXPECT_EQ(hurtbox.hitCount, kMaximumHurtbox3DHitsUVE);
    EXPECT_TRUE(hurtbox.hitsTruncated);
    EXPECT_EQ(hurtbox.hits[0].hitboxEntity, overflow.front().hitboxEntity);
}

TEST(Hurtbox3DUVETest, CommitHitsUVE_ReceivesOncePerEnableWhileKeepingTheHit) {
    Hurtbox3DComponentUVE hurtbox{};
    const Hurtbox3DHitUVE incoming[] = {MakeIncomingUVE(kHitboxA, 0.5F)};

    Hurtbox3DUVE::CommitHitsUVE(hurtbox, incoming, 1U);
    ASSERT_EQ(hurtbox.hitCount, 1U);
    EXPECT_TRUE(hurtbox.hits[0].received);
    EXPECT_TRUE(Hurtbox3DUVE::HasReceivedUVE(hurtbox, kHitboxA));
    EXPECT_EQ(hurtbox.receivedCount, 1U);

    Hurtbox3DUVE::CommitHitsUVE(hurtbox, incoming, 1U);
    ASSERT_EQ(hurtbox.hitCount, 1U);
    EXPECT_FALSE(hurtbox.hits[0].received);
    EXPECT_TRUE(Hurtbox3DUVE::HasHitFromUVE(hurtbox, kHitboxA));
    EXPECT_EQ(hurtbox.receivedCount, 1U);

    Hurtbox3DUVE::ClearHitsUVE(hurtbox);
    EXPECT_EQ(hurtbox.hitCount, 0U);
    EXPECT_TRUE(Hurtbox3DUVE::HasReceivedUVE(hurtbox, kHitboxA));

    Hurtbox3DUVE::ResetReceivedUVE(hurtbox);
    EXPECT_FALSE(Hurtbox3DUVE::HasReceivedUVE(hurtbox, kHitboxA));
    Hurtbox3DUVE::CommitHitsUVE(hurtbox, incoming, 1U);
    ASSERT_EQ(hurtbox.hitCount, 1U);
    EXPECT_TRUE(hurtbox.hits[0].received);
}

TEST(Hurtbox3DUVETest, CommitHitsUVE_DropsInvalidIncoming) {
    Hurtbox3DComponentUVE hurtbox{};
    const Hurtbox3DHitUVE incoming[] = {
        MakeIncomingUVE(kInvalidEntityUVE, 0.5F),
        MakeIncomingUVE(kHitboxA, 0.0F),
        Hurtbox3DHitUVE{kHitboxB, std::numeric_limits<float>::infinity(), {1.0F, 0.0F, 0.0F}},
        MakeIncomingUVE(kHitboxA, 0.3F),
    };
    Hurtbox3DUVE::CommitHitsUVE(hurtbox, incoming, 4U);
    ASSERT_EQ(hurtbox.hitCount, 1U);
    EXPECT_EQ(hurtbox.hits[0].hitboxEntity, kHitboxA);
    EXPECT_FLOAT_EQ(hurtbox.hits[0].penetrationDepth, 0.3F);
}

} // namespace
} // namespace UVE::Scene::Tests
