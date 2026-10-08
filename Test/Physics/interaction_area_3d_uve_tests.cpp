// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/objects/3d/interaction_area_3d_uve.h"

namespace UVE::Scene::Tests {
namespace {

constexpr EntityUVE kArea{1U, 1U};
constexpr EntityUVE kFirst{4U, 1U};
constexpr EntityUVE kSecond{2U, 1U};
constexpr EntityUVE kThird{2U, 0U};
constexpr EntityUVE kOwner{9U, 1U};

TEST(InteractionArea3DUVETest, DefaultComponentIsArmedAndIgnoresNobody) {
    const InteractionArea3DComponentUVE area{};
    EXPECT_TRUE(IsInteractionArea3DObjectComponentValidUVE(area));
    EXPECT_TRUE(InteractionArea3DUVE::IsArmedUVE(area));
    EXPECT_EQ(area.ignoreEntity, kInvalidEntityUVE);
    EXPECT_EQ(area.interactorCount, 0U);
}

TEST(InteractionArea3DUVETest, IsArmedUVE_FalseWhenDisabledOrInvalid) {
    InteractionArea3DComponentUVE area{};
    area.enabled = false;
    EXPECT_FALSE(InteractionArea3DUVE::IsArmedUVE(area));
    area.enabled = true;
    area.collisionLayer = 0U;
    EXPECT_FALSE(InteractionArea3DUVE::IsArmedUVE(area));
    area.collisionLayer = 1U;
    area.maximumCandidates = 0U;
    EXPECT_FALSE(InteractionArea3DUVE::IsArmedUVE(area));
}

TEST(InteractionArea3DUVETest, AcceptsInteractorUVE_SkipsSelfIgnoreAndMask) {
    InteractionArea3DComponentUVE area{};
    area.collisionMask = 1U;
    EXPECT_TRUE(InteractionArea3DUVE::AcceptsInteractorUVE(kArea, area, kFirst, 1U, 0xFFFFFFFFU));
    EXPECT_FALSE(InteractionArea3DUVE::AcceptsInteractorUVE(kArea, area, kArea, 1U, 0xFFFFFFFFU));
    EXPECT_FALSE(InteractionArea3DUVE::AcceptsInteractorUVE(kArea, area, kInvalidEntityUVE, 1U, 0xFFFFFFFFU));

    area.ignoreEntity = kOwner;
    EXPECT_FALSE(InteractionArea3DUVE::AcceptsInteractorUVE(kArea, area, kOwner, 1U, 0xFFFFFFFFU));
    EXPECT_TRUE(InteractionArea3DUVE::AcceptsInteractorUVE(kArea, area, kFirst, 1U, 0xFFFFFFFFU));

    EXPECT_FALSE(InteractionArea3DUVE::AcceptsInteractorUVE(kArea, area, kFirst, 2U, 0xFFFFFFFFU));
    EXPECT_FALSE(InteractionArea3DUVE::AcceptsInteractorUVE(kArea, area, kFirst, 1U, 0x02U));
}

TEST(InteractionArea3DUVETest, CommitInteractorsUVE_UniquesContentOrderAndCaps) {
    InteractionArea3DComponentUVE area{};
    const EntityUVE incoming[] = {kFirst, kSecond, kFirst, kThird};
    InteractionArea3DUVE::CommitInteractorsUVE(area, incoming, 4U);

    ASSERT_EQ(area.interactorCount, 3U);
    EXPECT_FALSE(area.interactorsTruncated);
    EXPECT_EQ(area.interactors[0], kThird);
    EXPECT_EQ(area.interactors[1], kSecond);
    EXPECT_EQ(area.interactors[2], kFirst);
    EXPECT_TRUE(InteractionArea3DUVE::HasInteractorUVE(area, kSecond));
    EXPECT_EQ(InteractionArea3DUVE::GetInteractorUVE(area, 3U), nullptr);

    area.maximumCandidates = 2U;
    InteractionArea3DUVE::CommitInteractorsUVE(area, incoming, 4U);
    EXPECT_EQ(area.interactorCount, 2U);
    EXPECT_TRUE(area.interactorsTruncated);
    EXPECT_EQ(area.interactors[0], kThird);
    EXPECT_EQ(area.interactors[1], kSecond);
}

TEST(InteractionArea3DUVETest, ClearInteractorsUVE_DropsFocusAndCount) {
    InteractionArea3DComponentUVE area{};
    const EntityUVE incoming[] = {kFirst};
    InteractionArea3DUVE::CommitInteractorsUVE(area, incoming, 1U);
    area.focusedByPrimaryInteractor = true;
    InteractionArea3DUVE::ClearInteractorsUVE(area);
    EXPECT_EQ(area.interactorCount, 0U);
    EXPECT_FALSE(area.focusedByPrimaryInteractor);
}

} // namespace
} // namespace UVE::Scene::Tests
