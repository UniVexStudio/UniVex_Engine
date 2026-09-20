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

TEST(WorldPartition3DCellIdUVETest, OriginAndInteriorPointsLandInTheExpectedCells) {
    // The grid origin is the minimum corner: the partition's own position is cell (0,0,0)'s
    // start, and a point clear inside another cell reads its integer coordinates exactly.
    WorldPartition3DNodeComponentUVE config;
    config.cellSize = 10.0F;
    config.cellCounts = {4U, 1U, 4U};

    const Math::Vector3UVE origin{100.0F, 0.0F, -50.0F};
    const auto atOrigin = ResolveWorldPartition3DCellIdForPositionUVE(config, origin, origin);
    ASSERT_TRUE(atOrigin.has_value());
    EXPECT_EQ(atOrigin->x, 0);
    EXPECT_EQ(atOrigin->y, 0);
    EXPECT_EQ(atOrigin->z, 0);

    // origin + (25, 3, 11): inside cell (2, 0, 1) - every axis divides independently.
    const Math::Vector3UVE interior{125.0F, 3.0F, -39.0F};
    const auto inside = ResolveWorldPartition3DCellIdForPositionUVE(config, origin, interior);
    ASSERT_TRUE(inside.has_value());
    EXPECT_EQ(inside->x, 2);
    EXPECT_EQ(inside->y, 0);
    EXPECT_EQ(inside->z, 1);
}

TEST(WorldPartition3DCellIdUVETest, BoundariesBelongToExactlyOneCellEachAndTheOutsideIsUnmanaged) {
    // The floor rule at every edge class: exact cell starts belong to the cell they start,
    // one epsilon before belongs to the previous cell, the volume boundary itself is outside,
    // and anything behind or beyond the grid answers no cell at all (unmanaged: it stays
    // rendered, because volume-rejection must never hide geometry).
    WorldPartition3DNodeComponentUVE config;
    config.cellSize = 10.0F;
    config.cellCounts = {2U, 1U, 1U}; // volume: x in [0,20), y in [0,10), z in [0,10)
    const Math::Vector3UVE origin{0.0F, 0.0F, 0.0F};

    const auto onBoundary = ResolveWorldPartition3DCellIdForPositionUVE(
        config, origin, Math::Vector3UVE{10.0F, 0.0F, 0.0F});
    ASSERT_TRUE(onBoundary.has_value());
    EXPECT_EQ(onBoundary->x, 1) << "an exact cell start belongs to the cell it starts";

    const auto justBefore = ResolveWorldPartition3DCellIdForPositionUVE(
        config, origin, Math::Vector3UVE{9.9999F, 0.0F, 0.0F});
    ASSERT_TRUE(justBefore.has_value());
    EXPECT_EQ(justBefore->x, 0) << "one epsilon earlier is still the previous cell";

    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPositionUVE(
                     config, origin, Math::Vector3UVE{-0.0001F, 0.0F, 0.0F})
                     .has_value())
        << "behind the origin corner is outside the volume";
    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPositionUVE(
                     config, origin, Math::Vector3UVE{20.0F, 0.0F, 0.0F})
                     .has_value())
        << "the volume's upper bound on an axis is outside it";
    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPositionUVE(
                     config, origin, Math::Vector3UVE{5.0F, 15.0F, 5.0F})
                     .has_value())
        << "inside on two axes but outside the short third is still outside";
}

TEST(WorldPartition3DCellIdUVETest, DegenerateConfigAndNonFinitePosesNeverAnswerACell) {
    WorldPartition3DNodeComponentUVE broken;
    broken.cellSize = 0.0F; // never valid
    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPositionUVE(
                     broken, Math::Vector3UVE{}, Math::Vector3UVE{})
                     .has_value());

    WorldPartition3DNodeComponentUVE valid;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPositionUVE(
                     valid, Math::Vector3UVE{}, Math::Vector3UVE{nan, 0.0F, 0.0F})
                     .has_value());
    EXPECT_FALSE(ResolveWorldPartition3DCellIdForPositionUVE(
                     valid, Math::Vector3UVE{0.0F, nan, 0.0F}, Math::Vector3UVE{})
                     .has_value())
        << "a NaN grid origin manages nothing either";
}

TEST(WorldPartition3DCellLinearIndexUVETest, IndexingIsDeterministicAndXMajor) {
    // The tie-break key consumers sort cells by: a fixed, space-filling index so two runs always
    // admit the same budget candidates even when distances tie exactly.
    const std::array<std::uint32_t, 3U> counts{4U, 2U, 3U};
    EXPECT_EQ(ResolveWorldPartition3DCellLinearIndexUVE({0, 0, 0}, counts), 0U);
    EXPECT_EQ(ResolveWorldPartition3DCellLinearIndexUVE({3, 0, 0}, counts), 3U);
    EXPECT_EQ(ResolveWorldPartition3DCellLinearIndexUVE({0, 1, 0}, counts), 4U) << "y strides by cx";
    EXPECT_EQ(ResolveWorldPartition3DCellLinearIndexUVE({0, 0, 1}, counts), 8U) << "z strides by cx*cy";
    EXPECT_EQ(ResolveWorldPartition3DCellLinearIndexUVE({2, 1, 2}, counts), 2U + 4U + 16U);
}

TEST(WorldPartition3DMembershipLiveUVETest, LiveOwnerTrustsItsFlagDeadOwnerFailsOpen) {
    // The renderer-side contract measured in isolation: while the partition exists, the live
    // flag rules; once it is gone, nobody may hide content with a stale opinion.
    EXPECT_FALSE(ResolveWorldPartition3DMembershipLiveUVE(/*partitionOwnerAlive=*/true, false))
        << "a live partition's fade verdict rules";
    EXPECT_TRUE(ResolveWorldPartition3DMembershipLiveUVE(true, true));
    EXPECT_TRUE(ResolveWorldPartition3DMembershipLiveUVE(/*partitionOwnerAlive=*/false, false))
        << "dead partition: fail open, never delete the world by stale flag";
    EXPECT_TRUE(ResolveWorldPartition3DMembershipLiveUVE(false, true));
}

TEST(VisibilityRegion3DContainmentUVETest, InteriorAndBoundaryPointsAreInsideTheOutsideIsNot) {
    // The whole room-culling contract rests on one question, so measure it: where is inside?
    // A region at the origin with half extents {2, 1, 4}; the boundary itself is INCLUDED on
    // purpose (unlike the world partition cells, a single box's skin belongs to itself exactly
    // once), and the outside never is.
    VisibilityRegion3DNodeComponentUVE config;
    config.halfExtents = Math::Vector3UVE{2.0F, 1.0F, 4.0F};
    const Math::Vector3UVE origin{};

    EXPECT_TRUE(ResolveVisibilityRegion3DContainsPointUVE(config, origin, Math::Vector3UVE{}))
        << "the center is as inside as it gets";
    EXPECT_TRUE(
        ResolveVisibilityRegion3DContainsPointUVE(config, origin, Math::Vector3UVE{1.5F, 0.5F, -3.5F}))
        << "a routine interior point";
    EXPECT_TRUE(
        ResolveVisibilityRegion3DContainsPointUVE(config, origin, Math::Vector3UVE{2.0F, 1.0F, 4.0F}))
        << "the exact corner is INCLUDED - paint a mesh on the wall and it is managed";
    EXPECT_TRUE(
        ResolveVisibilityRegion3DContainsPointUVE(config, origin, Math::Vector3UVE{-2.0F, -1.0F, -4.0F}))
        << "the opposite corner too";
    EXPECT_FALSE(
        ResolveVisibilityRegion3DContainsPointUVE(config, origin, Math::Vector3UVE{2.01F, 0.0F, 0.0F}))
        << "one millimetre past the wall is outside - the box is not elastic";
    EXPECT_FALSE(
        ResolveVisibilityRegion3DContainsPointUVE(config, origin, Math::Vector3UVE{0.0F, -1.01F, 0.0F}));

    // A second origin proves this is region-relative, not world-relative:
    const Math::Vector3UVE room{100.0F, 0.0F, 0.0F};
    EXPECT_TRUE(ResolveVisibilityRegion3DContainsPointUVE(config, room,
                                                          Math::Vector3UVE{101.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(
        ResolveVisibilityRegion3DContainsPointUVE(config, room, Math::Vector3UVE{}))
        << "the origin is outside the room at x=100";
}

TEST(VisibilityRegion3DContainmentUVETest, DegenerateConfigAndNonFinitePosesContainNothing) {
    // An unusable region manages NOTHING: zero half extent, negative extents, or a NaN pose all
    // answer false, so a typo in the inspector can never silently absorb a corridor.
    VisibilityRegion3DNodeComponentUVE flat;
    flat.halfExtents = Math::Vector3UVE{0.0F, 1.0F, 1.0F};
    EXPECT_FALSE(ResolveVisibilityRegion3DContainsPointUVE(flat, Math::Vector3UVE{},
                                                           Math::Vector3UVE{}));

    VisibilityRegion3DNodeComponentUVE negative;
    negative.halfExtents = Math::Vector3UVE{-2.0F, 1.0F, 1.0F};
    EXPECT_FALSE(ResolveVisibilityRegion3DContainsPointUVE(negative, Math::Vector3UVE{},
                                                           Math::Vector3UVE{}));

    VisibilityRegion3DNodeComponentUVE fine;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ResolveVisibilityRegion3DContainsPointUVE(
        fine, Math::Vector3UVE{}, Math::Vector3UVE{nan, 0.0F, 0.0F}));
    EXPECT_FALSE(ResolveVisibilityRegion3DContainsPointUVE(
        fine, Math::Vector3UVE{nan, 0.0F, 0.0F}, Math::Vector3UVE{}));
}

TEST(VisibilityRegion3DLayerGateUVETest, AnySharedBitManagesAndZeroMasksManageNothing) {
    // Layer gating must be an unsigned mask intersection, measured on corners: default-on-
    // default passes, disjoint masks close the gate, and a ZERO mask on either side manages
    // nothing (a mesh on no layers is never managed - an authoring choice, not an error).
    EXPECT_TRUE(ResolveVisibilityRegion3DLayerGateUVE(0xFFFFFFFFU, 0x00000001U))
        << "region-everything + mesh-layer-0: the out-of-the-box behaviour";
    EXPECT_TRUE(ResolveVisibilityRegion3DLayerGateUVE(0x00000002U, 0x00000002U))
        << "both on layer 1 alone";
    EXPECT_TRUE(ResolveVisibilityRegion3DLayerGateUVE(0x0000000AU, 0x00000008U))
        << "any ONE shared bit is enough";
    EXPECT_FALSE(ResolveVisibilityRegion3DLayerGateUVE(0x00000002U, 0x00000001U))
        << "props-layer region does not manage an NPC on the default layer";
    EXPECT_FALSE(ResolveVisibilityRegion3DLayerGateUVE(0U, 0xFFFFFFFFU))
        << "a region masking zero layers manages nothing";
    EXPECT_FALSE(ResolveVisibilityRegion3DLayerGateUVE(0xFFFFFFFFU, 0U))
        << "a mesh on zero layers is managed by nothing";
}

TEST(VisibilityRegion3DViewerUVETest, AnyViewerInsideActivatesAndAnEmptyCloudDoesNot) {
    // The pure half of the fail-open rule: any one viewer activates the region; an EMPTY cloud
    // answers false here (the engine's policy turns that into "show everything" above this
    // function, keeping the pure answer total).
    VisibilityRegion3DNodeComponentUVE config;
    config.halfExtents = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    const Math::Vector3UVE origin{};

    EXPECT_FALSE(ResolveVisibilityRegion3DAnyViewerInsideUVE(
        config, origin, std::vector<Math::Vector3UVE>{}));
    EXPECT_TRUE(ResolveVisibilityRegion3DAnyViewerInsideUVE(
        config, origin, std::vector<Math::Vector3UVE>{Math::Vector3UVE{1.0F, 0.0F, 0.0F}}));
    EXPECT_FALSE(ResolveVisibilityRegion3DAnyViewerInsideUVE(
        config, origin,
        std::vector<Math::Vector3UVE>{Math::Vector3UVE{50.0F, 0.0F, 0.0F},
                                      Math::Vector3UVE{-50.0F, 0.0F, 0.0F}}))
        << "two viewers, neither nearby";
    EXPECT_TRUE(ResolveVisibilityRegion3DAnyViewerInsideUVE(
        config, origin,
        std::vector<Math::Vector3UVE>{Math::Vector3UVE{50.0F, 0.0F, 0.0F},
                                      Math::Vector3UVE{0.0F, 0.0F, 0.0F}}))
        << "one step over the room line activates for the whole party";
}

TEST(VisibilityRegion3DMembershipLiveUVETest, LiveOwnerTrustsItsFlagDeadOwnerFailsOpen) {
    // The renderer-side contract, same shape the world partition keeps: while the region owns
    // a membership the live flag rules; once the owner is gone nobody may hide content with its
    // stale opinion.
    EXPECT_FALSE(ResolveVisibilityRegion3DMembershipLiveUVE(/*regionOwnerAlive=*/true, false))
        << "inactive region: interior content is skipped";
    EXPECT_TRUE(ResolveVisibilityRegion3DMembershipLiveUVE(true, true));
    EXPECT_TRUE(ResolveVisibilityRegion3DMembershipLiveUVE(/*regionOwnerAlive=*/false, false))
        << "dead region: fail open, never leave a door closed behind a deleted room";
    EXPECT_TRUE(ResolveVisibilityRegion3DMembershipLiveUVE(false, true));
}

TEST(Occluder3DFullyHiddenUVETest, BehindTheWallIsHiddenInFrontBesideAndBeyondOverReachAreNot) {
    // The single strict rule, measured on a wall at the origin with half extents {2,1,2}: the
    // viewer at z=+5 looking down -z. A point on the far side of the wall, on the segment
    // through the wall's interior, is hidden; everything else draws.
    Occluder3DNodeComponentUVE wall;
    wall.halfExtents = Math::Vector3UVE{2.0F, 1.0F, 2.0F};
    const Math::Vector3UVE wallOrigin{};
    const Math::Vector3UVE viewer{0.0F, 0.0F, 5.0F};

    EXPECT_TRUE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                Math::Vector3UVE{0.0F, 0.0F, -5.0F}))
        << "dead center behind the wall: hidden";
    EXPECT_TRUE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                Math::Vector3UVE{1.0F, 0.5F, -30.0F}))
        << "off-axis behind the wall, still covered: hidden";
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                 Math::Vector3UVE{0.0F, 0.0F, 3.0F}))
        << "in FRONT of the wall: nothing was hidden";
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                 Math::Vector3UVE{8.0F, 0.0F, -5.0F}))
        << "beside the wall on the far side: the segment clears the box (minded cone math:"
           " at x=8 the ray exits the wall's x-slab before ever entering its z-slab)";
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin,
                                                 Math::Vector3UVE{20.0F, 0.0F, 5.0F},
                                                 Math::Vector3UVE{0.0F, 0.0F, -5.0F}))
        << "viewer beside the wall, same answer - the cover is directional";
}

TEST(Occluder3DFullyHiddenUVETest, HonestAmbiguitiesAllFailOpen) {
    // The no-false-culls edge list, each case measured: grazing the exact skin, standing on the
    // surface, standing inside, the box exactly AT the candidate, an invalid box, a NaN pose -
    // every one of them answers NOT hidden.
    Occluder3DNodeComponentUVE wall;
    wall.halfExtents = Math::Vector3UVE{2.0F, 1.0F, 2.0F};
    const Math::Vector3UVE wallOrigin{};
    const Math::Vector3UVE viewer{0.0F, 0.0F, 5.0F};

    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                 Math::Vector3UVE{4.0F, 0.0F, -1.0F}))
        << "true tangency: the segment kisses the far corner (2,0,2) exactly once - grazing is"
           " no cover, the strict-overlap rule fails open at the boundary";
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                 Math::Vector3UVE{0.0F, 0.0F, -2.0F}))
        << "the candidate stands ON the back face (the wall hides what is BEHIND, not at it)";
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                 Math::Vector3UVE{0.0F, 0.0F, 0.5F}))
        << "inside the box is never hidden by the box";
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin,
                                                 Math::Vector3UVE{0.0F, 0.0F, 1.0F},
                                                 Math::Vector3UVE{0.0F, 0.0F, -5.0F}))
        << "the viewer inside the cover sees past it";

    Occluder3DNodeComponentUVE degenerate;
    degenerate.halfExtents = Math::Vector3UVE{0.0F, 1.0F, 1.0F};
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(degenerate, wallOrigin, viewer,
                                                 Math::Vector3UVE{0.0F, 0.0F, -5.0F}))
        << "a zero-half-extent cover covers nothing";

    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                 Math::Vector3UVE{nan, 0.0F, -5.0F}));
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin,
                                                 Math::Vector3UVE{0.0F, 0.0F, nan},
                                                 Math::Vector3UVE{0.0F, 0.0F, -5.0F}));
}

TEST(Occluder3DFullyHiddenUVETest, CoverIsAboutTheSegmentNotAboutDistance) {
    // Distance is not cover: a candidate VERY far behind but off the wall's silhouette draws,
    // and one centimetre behind the wall on-axis is hidden - the box casts an exact
    // infinite-depth shadow down the segment, nothing shorter, nothing longer.
    Occluder3DNodeComponentUVE wall;
    wall.halfExtents = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    const Math::Vector3UVE wallOrigin{};
    const Math::Vector3UVE viewer{0.0F, 0.0F, 5.0F};

    EXPECT_TRUE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                Math::Vector3UVE{0.0F, 0.0F, -1.01F}))
        << "2 cm behind the back face is already hidden";
    EXPECT_TRUE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                Math::Vector3UVE{0.0F, 0.0F, -10000.0F}))
        << "and the shadow has no draw-distance";
    EXPECT_TRUE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                Math::Vector3UVE{1.5F, 0.0F, -10000.0F}))
        << "perspective narrows at range: even far-off-axis points fall under the cover";
    EXPECT_FALSE(ResolveOccluder3DFullyHiddenUVE(wall, wallOrigin, viewer,
                                                 Math::Vector3UVE{3000.0F, 0.0F, -10000.0F}))
        << "10 km away outside the silhouette cone: still drawn";
}

} // namespace UVE::Scene::Tests
