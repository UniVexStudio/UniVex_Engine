// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <limits>

#include <array>
#include <utility>
#include <gtest/gtest.h>

#include "uve/nodes/3d/all_nodes_3d_uve.h"
#include "uve/component/area_component_uve.h"

namespace UVE::Scene::Tests {
namespace {

TEST(Expanded3DNodeComponentsUVETest, DefaultContractsAreValid) {
    EXPECT_TRUE(IsAreaComponentValidUVE(AreaComponentUVE{}));
    EXPECT_TRUE(IsRayCast3DNodeComponentValidUVE(RayCast3DNodeComponentUVE{}));
    EXPECT_TRUE(IsAnimatableBody3DNodeComponentValidUVE(AnimatableBody3DNodeComponentUVE{}));
    EXPECT_TRUE(IsNavigationRegion3DNodeComponentValidUVE(NavigationRegion3DNodeComponentUVE{}));
    EXPECT_TRUE(IsNavigationAgent3DNodeComponentValidUVE(NavigationAgent3DNodeComponentUVE{}));
    EXPECT_TRUE(IsSkeleton3DNodeComponentValidUVE(Skeleton3DNodeComponentUVE{}));
    EXPECT_TRUE(IsBoneAttachment3DNodeComponentValidUVE(BoneAttachment3DNodeComponentUVE{}));
    EXPECT_TRUE(IsSpringArm3DNodeComponentValidUVE(SpringArm3DNodeComponentUVE{}));
    EXPECT_TRUE(IsMarker3DNodeComponentValidUVE(Marker3DNodeComponentUVE{}));
    EXPECT_TRUE(IsHitbox3DNodeComponentValidUVE(Hitbox3DNodeComponentUVE{}));
    EXPECT_TRUE(IsHurtbox3DNodeComponentValidUVE(Hurtbox3DNodeComponentUVE{}));
    EXPECT_TRUE(IsProjectile3DNodeComponentValidUVE(Projectile3DNodeComponentUVE{}));
    EXPECT_TRUE(IsInteractionArea3DNodeComponentValidUVE(InteractionArea3DNodeComponentUVE{}));
    EXPECT_TRUE(IsWorldEnvironment3DNodeComponentValidUVE(WorldEnvironment3DNodeComponentUVE{}));
    EXPECT_TRUE(IsReflectionProbe3DNodeComponentValidUVE(ReflectionProbe3DNodeComponentUVE{}));
    EXPECT_TRUE(IsDecal3DNodeComponentValidUVE(Decal3DNodeComponentUVE{}));
    EXPECT_TRUE(IsLodGroup3DNodeComponentValidUVE(LodGroup3DNodeComponentUVE{}));
    EXPECT_TRUE(IsOccluder3DNodeComponentValidUVE(Occluder3DNodeComponentUVE{}));
    EXPECT_TRUE(IsVisibilityRegion3DNodeComponentValidUVE(VisibilityRegion3DNodeComponentUVE{}));
    EXPECT_TRUE(IsSpawnPoint3DNodeComponentValidUVE(SpawnPoint3DNodeComponentUVE{}));
    EXPECT_TRUE(IsLevelStreamer3DNodeComponentValidUVE(LevelStreamer3DNodeComponentUVE{}));
    EXPECT_TRUE(IsWorldPartition3DNodeComponentValidUVE(WorldPartition3DNodeComponentUVE{}));
}

TEST(Expanded3DNodeComponentsUVETest, Skeleton3DDefaultIsEmptyAndBoneAttachmentIsInert) {
    const Skeleton3DNodeComponentUVE skeleton;
    const BoneAttachment3DNodeComponentUVE attachment;

    EXPECT_TRUE(skeleton.skeletonAssetPath.empty());
    EXPECT_TRUE(skeleton.bones.empty());
    EXPECT_FALSE(IsBoneAttachment3DNodeComponentResolvableUVE(attachment));
}

TEST(Expanded3DNodeComponentsUVETest, ExplicitSkeletonAssetBindingIsFailureAtomic) {
    Skeleton3DNodeComponentUVE skeleton;
    skeleton.enabled = false;
    const Skeleton3DNodeComponentUVE original = skeleton;

    EXPECT_FALSE(TryBindExplicitSkeleton3DAssetUVE(
        skeleton, {}, {SkeletonBoneUVE{"root", -1, {}, {}, {1.0F, 1.0F, 1.0F}}}));
    EXPECT_EQ(skeleton.skeletonAssetPath, original.skeletonAssetPath);
    EXPECT_TRUE(skeleton.bones.empty());
    EXPECT_EQ(skeleton.enabled, original.enabled);

    EXPECT_FALSE(TryBindExplicitSkeleton3DAssetUVE(skeleton, "assets/character.uveskel", {}));
    EXPECT_EQ(skeleton.skeletonAssetPath, original.skeletonAssetPath);
    EXPECT_TRUE(skeleton.bones.empty());
}

TEST(Expanded3DNodeComponentsUVETest, ExplicitSkeletonAssetBindingHydratesOnlySuppliedHierarchy) {
    Skeleton3DNodeComponentUVE skeleton;
    const std::vector<SkeletonBoneUVE> authoredBones{
        SkeletonBoneUVE{"root", -1, {}, {}, {1.0F, 1.0F, 1.0F}},
        SkeletonBoneUVE{"hand", 0, {1.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};

    EXPECT_TRUE(TryBindExplicitSkeleton3DAssetUVE(
        skeleton, "assets/character.uveskel", authoredBones));
    EXPECT_EQ(skeleton.skeletonAssetPath, "assets/character.uveskel");
    ASSERT_EQ(skeleton.bones.size(), authoredBones.size());
    EXPECT_EQ(skeleton.bones[0].name, authoredBones[0].name);
    EXPECT_EQ(skeleton.bones[1].name, authoredBones[1].name);
    EXPECT_EQ(skeleton.bones[1].parentIndex, authoredBones[1].parentIndex);
}

TEST(Expanded3DNodeComponentsUVETest, BoneAttachmentBecomesResolvableOnlyWithExplicitReferences) {
    BoneAttachment3DNodeComponentUVE attachment;
    attachment.skeletonLocalId = 7U;
    attachment.boneName = "hand";

    EXPECT_TRUE(IsBoneAttachment3DNodeComponentResolvableUVE(attachment));
    attachment.enabled = false;
    EXPECT_FALSE(IsBoneAttachment3DNodeComponentResolvableUVE(attachment));
}

TEST(Expanded3DNodeComponentsUVETest, Hitbox3DStrikeStateIsRuntimeOnlyAndNeverAuthored) {
    Hitbox3DNodeComponentUVE hitbox;
    EXPECT_EQ(hitbox.strikeCount, 0U);
    EXPECT_FALSE(hitbox.strikesTruncated);

    // Mid-frame runtime state must not change authoring validity - a live hitbox with fresh
    // strikes still passes the same validation a freshly authored one does.
    hitbox.strikeCount = 3U;
    hitbox.strikesTruncated = true;
    hitbox.strikes[0U] = Hitbox3DStrikeUVE{EntityUVE{}, 0.25F};
    EXPECT_TRUE(IsHitbox3DNodeComponentValidUVE(hitbox));
}

TEST(Expanded3DNodeComponentsUVETest, BoundedContractsRejectUnsafeValues) {
    RayCast3DNodeComponentUVE ray;
    ray.exclusionCount = static_cast<std::uint8_t>(kMaximumRayCastExclusionsUVE + 1U);
    EXPECT_FALSE(IsRayCast3DNodeComponentValidUVE(ray));

    Skeleton3DNodeComponentUVE skeleton;
    skeleton.bones.push_back(SkeletonBoneUVE{"root", -1, {}, {}, {1.0F, 1.0F, 1.0F}});
    skeleton.bones.push_back(SkeletonBoneUVE{"root", 0, {}, {}, {1.0F, 1.0F, 1.0F}});
    EXPECT_FALSE(IsSkeleton3DNodeComponentValidUVE(skeleton));

    LodGroup3DNodeComponentUVE lod;
    lod.distanceThresholds[1] = lod.distanceThresholds[0];
    EXPECT_FALSE(IsLodGroup3DNodeComponentValidUVE(lod));

    LevelStreamer3DNodeComponentUVE streamer;
    streamer.enabled = true;
    streamer.levelPath.clear();
    EXPECT_FALSE(IsLevelStreamer3DNodeComponentValidUVE(streamer));

    WorldPartition3DNodeComponentUVE partition;
    partition.cellCounts[1] = 0U;
    EXPECT_FALSE(IsWorldPartition3DNodeComponentValidUVE(partition));
}

TEST(Expanded3DNodeComponentsUVETest, FiniteAndBoundedValuesRejectNonFinitePayloads) {
    WorldEnvironment3DNodeComponentUVE environment;
    environment.exposure = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsWorldEnvironment3DNodeComponentValidUVE(environment));

    SpringArm3DNodeComponentUVE springArm;
    springArm.currentLength = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsSpringArm3DNodeComponentValidUVE(springArm));
}

} // namespace
TEST(LodGroup3DResolveUVETest, EachThresholdSelectsItsOwnLevel) {
    // The core rule: the first threshold the distance fits under wins. Boundaries are inclusive,
    // so an object sitting exactly on a threshold picks the nearer level rather than flickering
    // between two as it drifts by a millimetre.
    LodGroup3DNodeComponentUVE group;
    group.distanceThresholds = {10.0F, 25.0F, 60.0F, 120.0F, 240.0F, 480.0F, 960.0F, 1920.0F};
    group.levelCount = 4U;

    const std::array<std::pair<float, std::uint8_t>, 8U> cases{{
        {0.0F, 0U}, {9.9F, 0U}, {10.0F, 0U}, {10.1F, 1U},
        {25.0F, 1U}, {25.1F, 2U}, {60.0F, 2U}, {60.1F, 3U}}};
    for (const auto& [distance, expectedLevel] : cases) {
        ResolveLodGroup3DLevelUVE(group, distance);
        EXPECT_EQ(group.currentLevel, expectedLevel) << "at distance " << distance;
        EXPECT_FALSE(group.culledByDistance) << "at distance " << distance;
    }
}

TEST(LodGroup3DResolveUVETest, PastTheLastThresholdIsCulledWithoutRunningOffTheChain) {
    // Two things at once. The entity must be culled, and currentLevel must stay at the last REAL
    // level - a consumer that indexes a mesh array by level would otherwise read out of bounds
    // simply because an object moved too far away.
    LodGroup3DNodeComponentUVE group;
    group.levelCount = 4U;
    ResolveLodGroup3DLevelUVE(group, 500.0F);

    EXPECT_TRUE(group.culledByDistance);
    EXPECT_EQ(group.currentLevel, 3U) << "must clamp to the last level, not run one past it";
    EXPECT_LT(group.currentLevel, group.levelCount);
}

TEST(LodGroup3DResolveUVETest, LevelCountShortensTheChainWithoutRewritingDistances) {
    // An author shortening a chain should not have to edit the distances. With levelCount 2 the
    // third threshold is ignored entirely, so 30 m is past the end even though the array still
    // holds a 60 m entry.
    LodGroup3DNodeComponentUVE group;
    group.distanceThresholds = {10.0F, 25.0F, 60.0F, 120.0F, 240.0F, 480.0F, 960.0F, 1920.0F};
    group.levelCount = 2U;

    ResolveLodGroup3DLevelUVE(group, 20.0F);
    EXPECT_EQ(group.currentLevel, 1U);
    EXPECT_FALSE(group.culledByDistance);

    ResolveLodGroup3DLevelUVE(group, 30.0F);
    EXPECT_TRUE(group.culledByDistance) << "past entry 1 is past the end when levelCount is 2";
    EXPECT_EQ(group.currentLevel, 1U);
}

TEST(LodGroup3DResolveUVETest, EveryDegenerateCaseDrawsAtFullDetailRatherThanHiding) {
    // A configuration mistake should be visible so it gets fixed. Hiding geometry instead looks
    // exactly like a missing asset, which sends whoever hits it looking in the wrong place.
    LodGroup3DNodeComponentUVE disabled;
    disabled.enabled = false;
    ResolveLodGroup3DLevelUVE(disabled, 100000.0F);
    EXPECT_EQ(disabled.currentLevel, 0U);
    EXPECT_FALSE(disabled.culledByDistance) << "a disabled group must never distance-cull";

    LodGroup3DNodeComponentUVE emptyChain;
    emptyChain.levelCount = 0U;
    ResolveLodGroup3DLevelUVE(emptyChain, 100000.0F);
    EXPECT_EQ(emptyChain.currentLevel, 0U);
    EXPECT_FALSE(emptyChain.culledByDistance);

    LodGroup3DNodeComponentUVE notFinite;
    ResolveLodGroup3DLevelUVE(notFinite, std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(notFinite.currentLevel, 0U);
    EXPECT_FALSE(notFinite.culledByDistance) << "a NaN distance must not hide an object";

    LodGroup3DNodeComponentUVE overLong;
    overLong.levelCount = static_cast<std::uint8_t>(kMaximumLodLevelsUVE + 1U);
    ResolveLodGroup3DLevelUVE(overLong, 50.0F);
    EXPECT_EQ(overLong.currentLevel, 0U);
    EXPECT_FALSE(overLong.culledByDistance);
}

TEST(LodGroup3DResolveUVETest, ResolvingLeavesTheComponentValid) {
    // The resolver writes two derived fields, and the validator checks currentLevel against
    // levelCount. A resolve that produced an out-of-range level would make a previously valid
    // component fail validation on save - a corruption that only shows up at serialisation time.
    LodGroup3DNodeComponentUVE group;
    ASSERT_TRUE(IsLodGroup3DNodeComponentValidUVE(group));
    for (const float distance : {0.0F, 5.0F, 50.0F, 500.0F, 100000.0F}) {
        ResolveLodGroup3DLevelUVE(group, distance);
        EXPECT_TRUE(IsLodGroup3DNodeComponentValidUVE(group)) << "after resolving at " << distance;
    }
}

} // namespace UVE::Scene::Tests
