// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include "entity.h"
#include "interaction_area_3d.h"

namespace UVE::Scene::Tests {
namespace {

constexpr Entity kArea{1U, 1U};
constexpr Entity kFirst{4U, 1U};
constexpr Entity kSecond{2U, 1U};
constexpr Entity kThird{2U, 0U};
constexpr Entity kOwner{9U, 1U};

TEST(InteractionArea3DTest, DefaultComponentIsArmedAndIgnoresNobody) {
    const InteractionArea3DComponent area{};
    EXPECT_TRUE(IsInteractionArea3DObjectComponentValid(area));
    EXPECT_TRUE(InteractionArea3D::IsArmed(area));
    EXPECT_EQ(area.ignoreEntity, kInvalidEntity);
    EXPECT_EQ(area.interactorCount, 0U);
}

TEST(InteractionArea3DTest, IsArmed_FalseWhenDisabledOrInvalid) {
    InteractionArea3DComponent area{};
    area.enabled = false;
    EXPECT_FALSE(InteractionArea3D::IsArmed(area));
    area.enabled = true;
    area.collisionLayer = 0U;
    EXPECT_FALSE(InteractionArea3D::IsArmed(area));
    area.collisionLayer = 1U;
    area.maximumCandidates = 0U;
    EXPECT_FALSE(InteractionArea3D::IsArmed(area));
}

TEST(InteractionArea3DTest, AcceptsInteractor_SkipsSelfIgnoreAndMask) {
    InteractionArea3DComponent area{};
    area.collisionMask = 1U;
    EXPECT_TRUE(InteractionArea3D::AcceptsInteractor(kArea, area, kFirst, 1U, 0xFFFFFFFFU));
    EXPECT_FALSE(InteractionArea3D::AcceptsInteractor(kArea, area, kArea, 1U, 0xFFFFFFFFU));
    EXPECT_FALSE(InteractionArea3D::AcceptsInteractor(kArea, area, kInvalidEntity, 1U, 0xFFFFFFFFU));

    area.ignoreEntity = kOwner;
    EXPECT_FALSE(InteractionArea3D::AcceptsInteractor(kArea, area, kOwner, 1U, 0xFFFFFFFFU));
    EXPECT_TRUE(InteractionArea3D::AcceptsInteractor(kArea, area, kFirst, 1U, 0xFFFFFFFFU));

    EXPECT_FALSE(InteractionArea3D::AcceptsInteractor(kArea, area, kFirst, 2U, 0xFFFFFFFFU));
    EXPECT_FALSE(InteractionArea3D::AcceptsInteractor(kArea, area, kFirst, 1U, 0x02U));
}

TEST(InteractionArea3DTest, CommitInteractors_UniquesContentOrderAndCaps) {
    InteractionArea3DComponent area{};
    const Entity incoming[] = {kFirst, kSecond, kFirst, kThird};
    InteractionArea3D::CommitInteractors(area, incoming, 4U);

    ASSERT_EQ(area.interactorCount, 3U);
    EXPECT_FALSE(area.interactorsTruncated);
    EXPECT_EQ(area.interactors[0], kThird);
    EXPECT_EQ(area.interactors[1], kSecond);
    EXPECT_EQ(area.interactors[2], kFirst);
    EXPECT_TRUE(InteractionArea3D::HasInteractor(area, kSecond));
    EXPECT_EQ(InteractionArea3D::GetInteractor(area, 3U), nullptr);

    area.maximumCandidates = 2U;
    InteractionArea3D::CommitInteractors(area, incoming, 4U);
    EXPECT_EQ(area.interactorCount, 2U);
    EXPECT_TRUE(area.interactorsTruncated);
    EXPECT_EQ(area.interactors[0], kThird);
    EXPECT_EQ(area.interactors[1], kSecond);
}

TEST(InteractionArea3DTest, ClearInteractors_DropsFocusAndCount) {
    InteractionArea3DComponent area{};
    const Entity incoming[] = {kFirst};
    InteractionArea3D::CommitInteractors(area, incoming, 1U);
    area.focusedByPrimaryInteractor = true;
    InteractionArea3D::ClearInteractors(area);
    EXPECT_EQ(area.interactorCount, 0U);
    EXPECT_FALSE(area.focusedByPrimaryInteractor);
}

} // namespace
} // namespace UVE::Scene::Tests
