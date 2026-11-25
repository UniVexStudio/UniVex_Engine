// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "mesh_render_eligibility.h"

#include <cmath>
#include <limits>
#include <numbers>

#include <gtest/gtest.h>

namespace UVE::Render::Tests {
namespace {

[[nodiscard]] Math::Frustum MakeEligibilityFrustum() {
    const Math::Matrix4x4 view =
        Math::Matrix4x4::ViewFromPositionAndRotation(Math::Vector3{}, Math::Quaternion{});
    const Math::Matrix4x4 projection =
        Math::Matrix4x4::Perspective(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
    return Math::Frustum::FromViewProjection(projection * view);
}

[[nodiscard]] Scene::MeshComponent MakeEligibleMeshComponent() {
    return Scene::MeshComponent{Asset::AssetGuid{1U}, Asset::AssetGuid{2U}};
}

[[nodiscard]] Asset::MeshAsset MakeUnitMeshAsset() {
    Asset::MeshAsset mesh;
    mesh.localBounds = Math::Aabb::FromCenterExtents(Math::Vector3{}, Math::Vector3{0.5F, 0.5F, 0.5F});
    return mesh;
}

TEST(MeshRenderEligibilityTest, Evaluate_NormalizesFiniteWorldTransformAndPublishesCopiedFacts) {
    const Scene::MeshComponent component = MakeEligibleMeshComponent();
    Scene::WorldTransformComponent transform;
    transform.worldPosition = Math::Vector3{0.0F, 0.0F, -10.0F};
    transform.worldRotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 2.0F};
    transform.worldScale = Math::Vector3{2.0F, 1.0F, 1.0F};
    const Asset::MeshAsset mesh = MakeUnitMeshAsset();
    MeshRenderEligibility eligibility;

    ASSERT_TRUE(EvaluateMeshRenderEligibility(component, transform, mesh, MakeEligibilityFrustum(), eligibility));
    EXPECT_TRUE(eligibility.IsEligible());
    EXPECT_EQ(eligibility.reason, MeshRenderEligibilityReason::Eligible);
    EXPECT_FLOAT_EQ(eligibility.worldMatrix.m[0][0], 2.0F);
    EXPECT_FLOAT_EQ(eligibility.worldBounds.min.x, -1.0F);
    EXPECT_FLOAT_EQ(eligibility.worldBounds.max.x, 1.0F);
    EXPECT_TRUE(std::isfinite(eligibility.sortDepth));
}

TEST(MeshRenderEligibilityTest, Evaluate_RejectsNonFiniteWorldTransformAtomically) {
    const Scene::MeshComponent component = MakeEligibleMeshComponent();
    Scene::WorldTransformComponent transform;
    transform.worldPosition = Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, -10.0F};
    const Asset::MeshAsset mesh = MakeUnitMeshAsset();
    MeshRenderEligibility eligibility;
    eligibility.reason = MeshRenderEligibilityReason::Eligible;

    EXPECT_FALSE(EvaluateMeshRenderEligibility(component, transform, mesh, MakeEligibilityFrustum(), eligibility));
    EXPECT_EQ(eligibility.reason, MeshRenderEligibilityReason::InvalidWorldTransform);
    EXPECT_FALSE(eligibility.IsEligible());
}

TEST(MeshRenderEligibilityTest, Evaluate_RejectsUnorderedLocalBounds) {
    const Scene::MeshComponent component = MakeEligibleMeshComponent();
    const Scene::WorldTransformComponent transform{
        Math::Vector3{0.0F, 0.0F, -10.0F}, Math::Quaternion{}, Math::Vector3{1.0F, 1.0F, 1.0F}, false};
    Asset::MeshAsset mesh = MakeUnitMeshAsset();
    mesh.localBounds.min.x = 1.0F;
    mesh.localBounds.max.x = -1.0F;
    MeshRenderEligibility eligibility;

    EXPECT_FALSE(EvaluateMeshRenderEligibility(component, transform, mesh, MakeEligibilityFrustum(), eligibility));
    EXPECT_EQ(eligibility.reason, MeshRenderEligibilityReason::InvalidLocalBounds);
}

TEST(MeshRenderEligibilityTest, Evaluate_RejectsOutsideFrustumWithoutAssetFailure) {
    const Scene::MeshComponent component = MakeEligibleMeshComponent();
    const Scene::WorldTransformComponent transform{
        Math::Vector3{0.0F, 0.0F, 10.0F}, Math::Quaternion{}, Math::Vector3{1.0F, 1.0F, 1.0F}, false};
    const Asset::MeshAsset mesh = MakeUnitMeshAsset();
    MeshRenderEligibility eligibility;

    EXPECT_FALSE(EvaluateMeshRenderEligibility(component, transform, mesh, MakeEligibilityFrustum(), eligibility));
    EXPECT_EQ(eligibility.reason, MeshRenderEligibilityReason::OutsideFrustum);
}

TEST(MeshRenderVisibilityTest, RejectedCandidatesPublishTheSameFieldsTheyAlwaysDid) {
    // Pins what each rejection path publishes, including the fields a caller might not think to
    // check. Written while profiling this function: the rejection paths fill the output struct
    // before the verdict is known, which looked like an obvious waste at four culls a frame. It
    // measured as no change at all - the copies vanish into memory bandwidth the plane tests are
    // already using - so the code was left alone and the tests kept. Anyone who tries the same
    // rewrite again now has the behaviour they must preserve written down.
    const Math::Aabb localBounds =
        Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F});
    const Math::Matrix4x4 view =
        Math::Matrix4x4::ViewFromPositionAndRotation(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{});
    const Math::Matrix4x4 projection =
        Math::Matrix4x4::Perspective(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
    const Math::Frustum frustum = Math::Frustum::FromViewProjection(projection * view);

    // An unplaced placement: its own reason survives, and no geometry is published for it.
    {
        MeshRenderPlacement unplaced;
        unplaced.reason = MeshRenderEligibilityReason::InvalidLocalBounds;
        MeshRenderEligibility eligibility;
        EXPECT_FALSE(TestMeshRenderVisibility(unplaced, frustum, eligibility));
        EXPECT_EQ(eligibility.reason, MeshRenderEligibilityReason::InvalidLocalBounds)
            << "an invalid placement must not be relabelled as a frustum rejection";
        EXPECT_FLOAT_EQ(eligibility.sortDepth, 0.0F);
    }

    // Behind the camera: rejected as OutsideFrustum, with the geometry still reported and the
    // sort depth left untouched at zero - exactly as before this change.
    {
        MeshRenderPlacement behind;
        behind.reason = MeshRenderEligibilityReason::Eligible;
        behind.worldMatrix = Math::Matrix4x4::Identity();
        behind.worldBounds = localBounds.Transform(
            Math::Matrix4x4::ComposeTrs(Math::Vector3{0.0F, 0.0F, 50.0F}, Math::Quaternion{},
                                              Math::Vector3{1.0F, 1.0F, 1.0F}));
        MeshRenderEligibility eligibility;
        EXPECT_FALSE(TestMeshRenderVisibility(behind, frustum, eligibility));
        EXPECT_EQ(eligibility.reason, MeshRenderEligibilityReason::OutsideFrustum);
        EXPECT_FLOAT_EQ(eligibility.sortDepth, 0.0F) << "a rejected candidate has no meaningful depth";
        EXPECT_FLOAT_EQ(eligibility.worldBounds.min.x, behind.worldBounds.min.x)
            << "the bounds that were tested must still be reported";
    }

    // And a visible one still publishes everything, so the fast path was not trimmed too far.
    {
        MeshRenderPlacement visible;
        visible.reason = MeshRenderEligibilityReason::Eligible;
        visible.worldMatrix = Math::Matrix4x4::ComposeTrs(
            Math::Vector3{0.0F, 0.0F, -10.0F}, Math::Quaternion{}, Math::Vector3{1.0F, 1.0F, 1.0F});
        visible.worldBounds = localBounds.Transform(visible.worldMatrix);
        MeshRenderEligibility eligibility;
        ASSERT_TRUE(TestMeshRenderVisibility(visible, frustum, eligibility));
        EXPECT_EQ(eligibility.reason, MeshRenderEligibilityReason::Eligible);
        EXPECT_GT(eligibility.sortDepth, 0.0F);
        EXPECT_FLOAT_EQ(eligibility.worldMatrix.m[3][2], visible.worldMatrix.m[3][2])
            << "the published matrix must be the placement's, element for element";
        EXPECT_FLOAT_EQ(eligibility.worldBounds.min.z, visible.worldBounds.min.z);
    }
}

TEST(MeshRenderVisibilityTest, FrustumCullingFreezeKeepsOffCameraPlacementsAndTheirSortDepth) {
    // The debug freeze (CullingSettings / MeshVisibilitySet::frustumTestsDisabled) asks for
    // every PLACED mesh, wherever the camera looks. Two things must survive it: the sort depth,
    // which is what keeps a frozen frame's queue ordered instead of dumping everything at depth
    // zero, and placement validity, because a freeze draws off-screen geometry rather than
    // geometry that was never valid to begin with.
    const Math::Aabb localBounds =
        Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F});
    const Math::Matrix4x4 view =
        Math::Matrix4x4::ViewFromPositionAndRotation(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{});
    const Math::Matrix4x4 projection =
        Math::Matrix4x4::Perspective(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
    const Math::Frustum frustum = Math::Frustum::FromViewProjection(projection * view);

    MeshRenderPlacement behind;
    behind.reason = MeshRenderEligibilityReason::Eligible;
    behind.worldMatrix = Math::Matrix4x4::ComposeTrs(Math::Vector3{0.0F, 0.0F, 50.0F}, Math::Quaternion{},
                                                           Math::Vector3{1.0F, 1.0F, 1.0F});
    behind.worldBounds = localBounds.Transform(behind.worldMatrix);

    // Unfrozen, the same placement is the OutsideFrustum rejection the test above pins.
    MeshRenderEligibility culled;
    EXPECT_FALSE(TestMeshRenderVisibility(behind, frustum, culled));
    EXPECT_EQ(culled.reason, MeshRenderEligibilityReason::OutsideFrustum);

    MeshRenderEligibility frozen;
    ASSERT_TRUE(TestMeshRenderVisibility(behind, frustum, frozen, true));
    EXPECT_EQ(frozen.reason, MeshRenderEligibilityReason::Eligible);
    EXPECT_TRUE(std::isfinite(frozen.sortDepth));
    EXPECT_GT(frozen.sortDepth, 0.0F) << "a frozen candidate still needs a real depth to sort by";
    EXPECT_FLOAT_EQ(frozen.worldBounds.min.z, behind.worldBounds.min.z);

    // Depth must still come from this frustum: a placement in front of the camera sorts nearer than
    // one behind it, exactly as it does when culling is on.
    MeshRenderPlacement inFront;
    inFront.reason = MeshRenderEligibilityReason::Eligible;
    inFront.worldMatrix = Math::Matrix4x4::ComposeTrs(Math::Vector3{0.0F, 0.0F, -10.0F},
                                                            Math::Quaternion{},
                                                            Math::Vector3{1.0F, 1.0F, 1.0F});
    inFront.worldBounds = localBounds.Transform(inFront.worldMatrix);
    MeshRenderEligibility frozenInFront;
    MeshRenderEligibility culledInFront;
    ASSERT_TRUE(TestMeshRenderVisibility(inFront, frustum, frozenInFront, true));
    ASSERT_TRUE(TestMeshRenderVisibility(inFront, frustum, culledInFront));
    EXPECT_FLOAT_EQ(frozenInFront.sortDepth, culledInFront.sortDepth)
        << "the freeze skips the plane test, not the depth derivation";
    EXPECT_LT(frozenInFront.sortDepth, frozen.sortDepth);

    // An unplaced candidate stays rejected: the freeze is about where things are, not whether they
    // are drawable at all.
    MeshRenderPlacement unplaced;
    unplaced.reason = MeshRenderEligibilityReason::InvalidWorldTransform;
    MeshRenderEligibility frozenUnplaced;
    EXPECT_FALSE(TestMeshRenderVisibility(unplaced, frustum, frozenUnplaced, true));
    EXPECT_EQ(frozenUnplaced.reason, MeshRenderEligibilityReason::InvalidWorldTransform);
    EXPECT_FLOAT_EQ(frozenUnplaced.sortDepth, 0.0F);
}

TEST(MeshRenderVisibilityTest, OutputIsNotContaminatedByAPreviousCall) {
    // The call site reuses one MeshRenderEligibility across every candidate in a cull, so a
    // rejection must not leave the previous candidate's values behind. Currently that holds
    // because the function builds a fresh local and assigns it wholesale - but that is an
    // implementation detail, and the property is what callers actually depend on.
    const Math::Aabb localBounds =
        Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.5F, 0.5F, 0.5F});
    const Math::Matrix4x4 view =
        Math::Matrix4x4::ViewFromPositionAndRotation(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{});
    const Math::Matrix4x4 projection =
        Math::Matrix4x4::Perspective(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
    const Math::Frustum frustum = Math::Frustum::FromViewProjection(projection * view);

    MeshRenderPlacement visible;
    visible.reason = MeshRenderEligibilityReason::Eligible;
    visible.worldMatrix = Math::Matrix4x4::ComposeTrs(
        Math::Vector3{0.0F, 0.0F, -10.0F}, Math::Quaternion{}, Math::Vector3{1.0F, 1.0F, 1.0F});
    visible.worldBounds = localBounds.Transform(visible.worldMatrix);

    MeshRenderEligibility reused;
    ASSERT_TRUE(TestMeshRenderVisibility(visible, frustum, reused));
    ASSERT_GT(reused.sortDepth, 0.0F);

    // Same output struct, now given a placement that cannot be placed at all.
    MeshRenderPlacement unplaced;
    unplaced.reason = MeshRenderEligibilityReason::InvalidWorldTransform;
    EXPECT_FALSE(TestMeshRenderVisibility(unplaced, frustum, reused));
    EXPECT_EQ(reused.reason, MeshRenderEligibilityReason::InvalidWorldTransform);
    EXPECT_FLOAT_EQ(reused.sortDepth, 0.0F)
        << "the previous candidate's depth must not survive into this one's rejection";
}

} // namespace
} // namespace UVE::Render::Tests
