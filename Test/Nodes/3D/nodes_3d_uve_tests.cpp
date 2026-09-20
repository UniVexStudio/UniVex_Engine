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

TEST(LevelStreamer3DNearestViewerUVETest, EmptyViewerListMeansNoDistanceAtAll) {
    // A streamer with no valid viewers must report "no distance"; the verdict function relies on
    // that absence to never load or unload on a viewerless tick.
    EXPECT_FALSE(ResolveLevelStreamer3DNearestViewerDistanceSquaredUVE({1.0F, 2.0F, 3.0F}, {})
                     .has_value());
}

TEST(LevelStreamer3DNearestViewerUVETest, NearestWinsAndSquaredStaysSquared) {
    // The three viewers sit 5, 2, and 3 units away on the X axis. The nearest (2 units) yields
    // 4.0 - squared, matching the ordering-only discipline that needs no sqrt anywhere in the
    // streaming path.
    const std::array<Math::Vector3UVE, 3U> viewers{
        Math::Vector3UVE{5.0F, 0.0F, 0.0F},
        Math::Vector3UVE{2.0F, 0.0F, 0.0F},
        Math::Vector3UVE{-3.0F, 0.0F, 0.0F}};
    const std::optional<float> nearest =
        ResolveLevelStreamer3DNearestViewerDistanceSquaredUVE({0.0F, 0.0F, 0.0F}, viewers);
    ASSERT_TRUE(nearest.has_value());
    EXPECT_FLOAT_EQ(*nearest, 4.0F);
}

TEST(LevelStreamer3DNearestViewerUVETest, ANonFiniteViewerSkipsRatherThanPoisons) {
    // A camera with a degenerate world pose must not turn the whole decision NaN: it simply
    // stops being a viewer for this tick and the surviving one decides.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<Math::Vector3UVE, 3U> viewers{
        Math::Vector3UVE{nan, 0.0F, 0.0F},
        Math::Vector3UVE{4.0F, 0.0F, 0.0F},
        Math::Vector3UVE{0.0F, nan, 0.0F}};
    const std::optional<float> nearest =
        ResolveLevelStreamer3DNearestViewerDistanceSquaredUVE({0.0F, 0.0F, 0.0F}, viewers);
    ASSERT_TRUE(nearest.has_value()) << "one finite viewer must still answer";
    EXPECT_FLOAT_EQ(*nearest, 16.0F);

    // Every viewer dead -> no distance at all, as if nobody is watching.
    const std::array<Math::Vector3UVE, 1U> onlyDead{Math::Vector3UVE{nan, nan, nan}};
    EXPECT_FALSE(ResolveLevelStreamer3DNearestViewerDistanceSquaredUVE({0.0F, 0.0F, 0.0F}, onlyDead)
                     .has_value());

    // A streamer sitting dead-center in a NaN pose answers nothing either.
    const std::array<Math::Vector3UVE, 1U> aliveViewer{Math::Vector3UVE{1.0F, 0.0F, 0.0F}};
    EXPECT_FALSE(ResolveLevelStreamer3DNearestViewerDistanceSquaredUVE({nan, 0.0F, 0.0F}, aliveViewer)
                     .has_value());
}

TEST(LevelStreamer3DActionUVETest, InvalidAuthoredConfigurationAlwaysDecidesNothing) {
    // loadDistance <= 0, non-finite distances, or no hysteresis band at all: the verdict is a
    // hard None no matter what the viewer does - fail-closed, measured.
    LevelStreamer3DStreamingFrameUVE frame;
    frame.hasViewer = true;
    frame.nearestViewerDistanceSquared = 0.0F;
    frame.enabled = true;

    frame.loadDistance = 0.0F;
    frame.unloadDistance = 10.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "a zero load distance is never a trigger";

    frame.loadDistance = 10.0F;
    frame.unloadDistance = 10.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "unload <= load gives no hysteresis band to believe in";

    frame.loadDistance = std::numeric_limits<float>::infinity();
    frame.unloadDistance = 100.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "non-finite configuration is never a trigger";

    frame.loadDistance = 10.0F;
    frame.unloadDistance = 20.0F;
    frame.nearestViewerDistanceSquared = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "a NaN viewer distance is never a trigger";
}

TEST(LevelStreamer3DActionUVETest, EnteringTheLoadRadiusRequestsLoadLeavingTheUnloadRadiusRequestsUnload) {
    // Happy path: a viewer inside the load radius starts the stream, inside the unload radius
    // keeps it, outside the unload radius ends it.
    LevelStreamer3DStreamingFrameUVE frame;
    frame.hasViewer = true;
    frame.enabled = true;
    frame.loadDistance = 10.0F;
    frame.unloadDistance = 20.0F;

    frame.nearestViewerDistanceSquared = 99.0F; // 9.949... < 10
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::RequestLoad);

    frame.loaded = true;
    frame.nearestViewerDistanceSquared = 150.0F; // between 10 and 20: hold the line
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "the hysteresis band keeps a loaded level in place";

    frame.nearestViewerDistanceSquared = 401.0F; // 20.02... > 20
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::RequestUnload);
}

TEST(LevelStreamer3DActionUVETest, BoundariesBelongToExactlyOneVerdictEach) {
    // loadDistance is inclusive on approach, unloadDistance is inclusive on retreat: each
    // boundary fires exactly one transition, never both, never neither across them.
    LevelStreamer3DStreamingFrameUVE frame;
    frame.hasViewer = true;
    frame.enabled = true;
    frame.loadDistance = 10.0F;
    frame.unloadDistance = 20.0F;

    frame.nearestViewerDistanceSquared = 100.0F; // d == loadDistance exactly
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::RequestLoad);

    frame.loaded = true;
    frame.nearestViewerDistanceSquared = 400.0F; // d == unloadDistance exactly
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::RequestUnload);

    // One epsilon INSIDE the band (d strictly between the two): hysteresis seesaws no transitions.
    frame.nearestViewerDistanceSquared = 399.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "loaded and 19.97 from a 20-unload stays loaded";
    frame.loaded = false;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "unloaded and 19.97 from a 10-load stays unloaded";
}

TEST(LevelStreamer3DActionUVETest, NoViewerNeverMovesAnythingAndDisabledAlwaysUnloads) {
    LevelStreamer3DStreamingFrameUVE frame;
    frame.enabled = true;
    frame.loadDistance = 10.0F;
    frame.unloadDistance = 20.0F;

    // Nobody is watching: neither the load trigger nor a stale unload verdict may move content.
    frame.hasViewer = false;
    frame.nearestViewerDistanceSquared = 0.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None);
    frame.loaded = true;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "a viewerless tick never unloads a loaded level (the toggle does, not silence)";

    // The latch an async path would set: a pending load must not double-issue on the boundary.
    frame.hasViewer = true;
    frame.loaded = false;
    frame.loadRequested = true;
    frame.nearestViewerDistanceSquared = 99.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::None)
        << "a load already in flight never loads twice";
    frame.loadRequested = false;

    // Disabling a streamer ALWAYS pulls its content, regardless of where the viewer stands.
    frame.enabled = false;
    frame.loaded = true;
    frame.nearestViewerDistanceSquared = 0.0F;
    EXPECT_EQ(ResolveLevelStreamer3DStreamingActionUVE(frame), LevelStreamer3DActionUVE::RequestUnload)
        << "viewer at zero distance still unloads a disabled streamer";
}

TEST(LevelStreamer3DLoadBudgetUVETest, BudgetIsPositiveAndStructurallySmall) {
    // The per-tick load budget is the load-burst contract: greater than zero is mandatory (a
    // zero budget would wedge streaming forever), structurally bounded so a teleport can never
    // hitch beyond it, and deliberately finite so tests can drive past it and measure carry-over.
    static_assert(kMaximumLevelStreamer3DLoadsPerTickUVE > 0U);
    static_assert(kMaximumLevelStreamer3DLoadsPerTickUVE <= 16U);
    EXPECT_EQ(kMaximumLevelStreamer3DLoadsPerTickUVE, 4U) << "the documented burst budget";
}

TEST(ReflectionProbe3DInfluenceUVETest, CenterIsOneFaceIsZeroHalfwayIsHalf) {
    // The whole falloff contract in three exact points: the undisturbed center, the face that
    // ends the influence (the boundary itself belongs to the outside), and the geometric halfway.
    const Math::Vector3UVE half{2.0F, 2.0F, 2.0F};
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeightUVE({0.0F, 0.0F, 0.0F}, half), 1.0F);
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeightUVE({2.0F, 0.0F, 0.0F}, half), 0.0F)
        << "the face belongs to the outside - hard edges do not leak a floating sliver";
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeightUVE({1.0F, 0.0F, 0.0F}, half), 0.5F);
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeightUVE({-1.0F, 0.0F, 0.0F}, half), 0.5F)
        << "weight is radially symmetric on each axis";
}

TEST(ReflectionProbe3DInfluenceUVETest, ChebyshevRuleUsesTheWorstAxisAndAnisotropicBoxesRespectTheirSize) {
    // On {2,4,8} a point sitting (1,1,3) is far past the Y and Z wedges differently: each axis
    // normalizes against its OWN half extent (50%, 25%, 37.5%) and the maximum - the Chebyshev
    // distance - decides. 0.5 is the legal answer, not 0.75, not 0.625.
    const Math::Vector3UVE half{2.0F, 4.0F, 8.0F};
    EXPECT_FLOAT_EQ(
        ResolveReflectionProbe3DInfluenceWeightUVE({1.0F, 1.0F, 3.0F}, half), 0.5F)
        << "worst-normalized axis wins, every axis readers its own extent";

    // Exactly one tick beyond the face on the shortest axis: the probe is done.
    EXPECT_FLOAT_EQ(
        ResolveReflectionProbe3DInfluenceWeightUVE({2.01F, 0.0F, 0.0F}, half), 0.0F);
}

TEST(ReflectionProbe3DInfluenceUVETest, DegenerateBoxesAndNonFinitePointsInfluenceNothing) {
    const Math::Vector3UVE half{2.0F, 2.0F, 2.0F};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeightUVE({0.0F, 0.0F, 0.0F}, {0.0F, 2.0F, 2.0F}),
                    0.0F)
        << "a collapsed axis decides nothing - fail-closed instead of dividing by it";
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeightUVE({nan, 0.0F, 0.0F}, half), 0.0F);
    EXPECT_FLOAT_EQ(ResolveReflectionProbe3DInfluenceWeightUVE({0.0F, 0.0F, 0.0F}, {nan, 2.0F, 2.0F}),
                    0.0F);
    EXPECT_FLOAT_EQ(
        ResolveReflectionProbe3DInfluenceWeightUVE({std::numeric_limits<float>::infinity(), 0.0F, 0.0F},
                                                   half),
        0.0F);
}

TEST(ReflectionProbe3DCaptureUVETest, OnceCapturesExactlyOnceAndEveryFrameOnlyCapturesForTheEye) {
    // The Once mode must capture when it has never captured and then stop; EveryFrame must
    // capture only while the probe influences the eye - the measurable saving Godot's Always
    // mode never finds, because a probe influencing nothing re-renders nothing.
    ReflectionProbe3DCaptureFrameUVE frame;
    frame.hasCameraViewer = true;
    frame.cameraInfluenceWeight = 0.5F;
    frame.updateMode = ReflectionProbeUpdateModeUVE::Once;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::Capture);
    frame.capturedOnce = true;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None)
        << "Once means once - the second verdict stays quiet";

    frame.capturedOnce = false;
    frame.updateMode = ReflectionProbeUpdateModeUVE::EveryFrame;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::Capture);

    frame.cameraInfluenceWeight = 0.0F;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None)
        << "the probe beyond the face does not re-render for an eye it cannot affect";
    frame.cameraInfluenceWeight = 0.5F;

    frame.hasCameraViewer = false;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None)
        << "no camera, no capture - even inside the influence box on paper";
}

TEST(ReflectionProbe3DCaptureUVETest, OnDemandListensOnlyToItsLatchAndDisabledNeverCaptures) {
    ReflectionProbe3DCaptureFrameUVE frame;
    frame.hasCameraViewer = true;
    frame.cameraInfluenceWeight = 0.5F;
    frame.updateMode = ReflectionProbeUpdateModeUVE::OnDemand;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None)
        << "OnDemand without the latch is silent";
    frame.updateRequested = true;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::Capture);

    // A disabled probe answers None under every mode, every latch state.
    frame.enabled = false;
    frame.capturedOnce = false;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None);
    frame.updateMode = ReflectionProbeUpdateModeUVE::Once;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None);
    frame.updateMode = ReflectionProbeUpdateModeUVE::EveryFrame;
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None);
}

TEST(ReflectionProbe3DCaptureUVETest, CorruptUpdateModeAndNaNWeightFailClosed) {
    // Serialization is validation-gated, but a hand-mutated or bit-rotten runtime can still
    // carry an out-of-range update mode or a NaN weight; the verdict must say nothing at all.
    ReflectionProbe3DCaptureFrameUVE frame;
    frame.hasCameraViewer = true;
    frame.cameraInfluenceWeight = 0.5F;
    frame.updateRequested = true;

    frame.updateMode = static_cast<ReflectionProbeUpdateModeUVE>(17U);
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None)
        << "an out-of-range mode is never a trigger";

    frame.updateMode = ReflectionProbeUpdateModeUVE::EveryFrame;
    frame.cameraInfluenceWeight = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(ResolveReflectionProbe3DCaptureActionUVE(frame), ReflectionProbe3DCaptureActionUVE::None)
        << "a NaN weight is never a trigger";
}

TEST(ReflectionProbe3DCaptureBudgetUVETest, BudgetIsPositiveAndStructurallySmall) {
    // Captures cost six scene passes each; the per-tick budget must be usable (> 0) and
    // structurally tiny so carrying over is the norm-ahead behavior, not an exception.
    static_assert(kMaximumReflectionProbeCapturesPerTickUVE > 0U);
    static_assert(kMaximumReflectionProbeCapturesPerTickUVE <= 8U);
    EXPECT_EQ(kMaximumReflectionProbeCapturesPerTickUVE, 2U) << "the documented capture budget";
}

} // namespace UVE::Scene::Tests
