// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/mesh_render_eligibility_uve.h"

#include <cmath>
#include <limits>
#include <numbers>

#include <gtest/gtest.h>

namespace UVE::Render::Tests {
namespace {

[[nodiscard]] Math::FrustumUVE MakeEligibilityFrustumUVE() {
    const Math::Matrix4x4UVE view =
        Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(Math::Vector3UVE{}, Math::QuaternionUVE{});
    const Math::Matrix4x4UVE projection =
        Math::Matrix4x4UVE::PerspectiveUVE(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
    return Math::FrustumUVE::FromViewProjectionUVE(projection * view);
}

[[nodiscard]] Scene::MeshComponentUVE MakeEligibleMeshComponentUVE() {
    return Scene::MeshComponentUVE{Asset::AssetGuidUVE{1U}, Asset::AssetGuidUVE{2U}};
}

[[nodiscard]] Asset::MeshAssetUVE MakeUnitMeshAssetUVE() {
    Asset::MeshAssetUVE mesh;
    mesh.localBounds = Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{}, Math::Vector3UVE{0.5F, 0.5F, 0.5F});
    return mesh;
}

TEST(MeshRenderEligibilityUVETest, EvaluateUVE_NormalizesFiniteWorldTransformAndPublishesCopiedFacts) {
    const Scene::MeshComponentUVE component = MakeEligibleMeshComponentUVE();
    Scene::WorldTransformComponentUVE transform;
    transform.worldPosition = Math::Vector3UVE{0.0F, 0.0F, -10.0F};
    transform.worldRotation = Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 2.0F};
    transform.worldScale = Math::Vector3UVE{2.0F, 1.0F, 1.0F};
    const Asset::MeshAssetUVE mesh = MakeUnitMeshAssetUVE();
    MeshRenderEligibilityUVE eligibility;

    ASSERT_TRUE(EvaluateMeshRenderEligibilityUVE(component, transform, mesh, MakeEligibilityFrustumUVE(), eligibility));
    EXPECT_TRUE(eligibility.IsEligibleUVE());
    EXPECT_EQ(eligibility.reason, MeshRenderEligibilityReasonUVE::Eligible);
    EXPECT_FLOAT_EQ(eligibility.worldMatrix.m[0][0], 2.0F);
    EXPECT_FLOAT_EQ(eligibility.worldBounds.min.x, -1.0F);
    EXPECT_FLOAT_EQ(eligibility.worldBounds.max.x, 1.0F);
    EXPECT_TRUE(std::isfinite(eligibility.sortDepth));
}

TEST(MeshRenderEligibilityUVETest, EvaluateUVE_RejectsNonFiniteWorldTransformAtomically) {
    const Scene::MeshComponentUVE component = MakeEligibleMeshComponentUVE();
    Scene::WorldTransformComponentUVE transform;
    transform.worldPosition = Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, -10.0F};
    const Asset::MeshAssetUVE mesh = MakeUnitMeshAssetUVE();
    MeshRenderEligibilityUVE eligibility;
    eligibility.reason = MeshRenderEligibilityReasonUVE::Eligible;

    EXPECT_FALSE(EvaluateMeshRenderEligibilityUVE(component, transform, mesh, MakeEligibilityFrustumUVE(), eligibility));
    EXPECT_EQ(eligibility.reason, MeshRenderEligibilityReasonUVE::InvalidWorldTransform);
    EXPECT_FALSE(eligibility.IsEligibleUVE());
}

TEST(MeshRenderEligibilityUVETest, EvaluateUVE_RejectsUnorderedLocalBounds) {
    const Scene::MeshComponentUVE component = MakeEligibleMeshComponentUVE();
    const Scene::WorldTransformComponentUVE transform{
        Math::Vector3UVE{0.0F, 0.0F, -10.0F}, Math::QuaternionUVE{}, Math::Vector3UVE{1.0F, 1.0F, 1.0F}, false};
    Asset::MeshAssetUVE mesh = MakeUnitMeshAssetUVE();
    mesh.localBounds.min.x = 1.0F;
    mesh.localBounds.max.x = -1.0F;
    MeshRenderEligibilityUVE eligibility;

    EXPECT_FALSE(EvaluateMeshRenderEligibilityUVE(component, transform, mesh, MakeEligibilityFrustumUVE(), eligibility));
    EXPECT_EQ(eligibility.reason, MeshRenderEligibilityReasonUVE::InvalidLocalBounds);
}

TEST(MeshRenderEligibilityUVETest, EvaluateUVE_RejectsOutsideFrustumWithoutAssetFailure) {
    const Scene::MeshComponentUVE component = MakeEligibleMeshComponentUVE();
    const Scene::WorldTransformComponentUVE transform{
        Math::Vector3UVE{0.0F, 0.0F, 10.0F}, Math::QuaternionUVE{}, Math::Vector3UVE{1.0F, 1.0F, 1.0F}, false};
    const Asset::MeshAssetUVE mesh = MakeUnitMeshAssetUVE();
    MeshRenderEligibilityUVE eligibility;

    EXPECT_FALSE(EvaluateMeshRenderEligibilityUVE(component, transform, mesh, MakeEligibilityFrustumUVE(), eligibility));
    EXPECT_EQ(eligibility.reason, MeshRenderEligibilityReasonUVE::OutsideFrustum);
}

TEST(MeshRenderVisibilityUVETest, RejectedCandidatesPublishTheSameFieldsTheyAlwaysDid) {
    // Pins what each rejection path publishes, including the fields a caller might not think to
    // check. Written while profiling this function: the rejection paths fill the output struct
    // before the verdict is known, which looked like an obvious waste at four culls a frame. It
    // measured as no change at all - the copies vanish into memory bandwidth the plane tests are
    // already using - so the code was left alone and the tests kept. Anyone who tries the same
    // rewrite again now has the behaviour they must preserve written down.
    const Math::AabbUVE localBounds =
        Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.5F, 0.5F, 0.5F});
    const Math::Matrix4x4UVE view =
        Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{});
    const Math::Matrix4x4UVE projection =
        Math::Matrix4x4UVE::PerspectiveUVE(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
    const Math::FrustumUVE frustum = Math::FrustumUVE::FromViewProjectionUVE(projection * view);

    // An unplaced placement: its own reason survives, and no geometry is published for it.
    {
        MeshRenderPlacementUVE unplaced;
        unplaced.reason = MeshRenderEligibilityReasonUVE::InvalidLocalBounds;
        MeshRenderEligibilityUVE eligibility;
        EXPECT_FALSE(TestMeshRenderVisibilityUVE(unplaced, frustum, eligibility));
        EXPECT_EQ(eligibility.reason, MeshRenderEligibilityReasonUVE::InvalidLocalBounds)
            << "an invalid placement must not be relabelled as a frustum rejection";
        EXPECT_FLOAT_EQ(eligibility.sortDepth, 0.0F);
    }

    // Behind the camera: rejected as OutsideFrustum, with the geometry still reported and the
    // sort depth left untouched at zero - exactly as before this change.
    {
        MeshRenderPlacementUVE behind;
        behind.reason = MeshRenderEligibilityReasonUVE::Eligible;
        behind.worldMatrix = Math::Matrix4x4UVE::IdentityUVE();
        behind.worldBounds = localBounds.TransformUVE(
            Math::Matrix4x4UVE::ComposeTrsUVE(Math::Vector3UVE{0.0F, 0.0F, 50.0F}, Math::QuaternionUVE{},
                                              Math::Vector3UVE{1.0F, 1.0F, 1.0F}));
        MeshRenderEligibilityUVE eligibility;
        EXPECT_FALSE(TestMeshRenderVisibilityUVE(behind, frustum, eligibility));
        EXPECT_EQ(eligibility.reason, MeshRenderEligibilityReasonUVE::OutsideFrustum);
        EXPECT_FLOAT_EQ(eligibility.sortDepth, 0.0F) << "a rejected candidate has no meaningful depth";
        EXPECT_FLOAT_EQ(eligibility.worldBounds.min.x, behind.worldBounds.min.x)
            << "the bounds that were tested must still be reported";
    }

    // And a visible one still publishes everything, so the fast path was not trimmed too far.
    {
        MeshRenderPlacementUVE visible;
        visible.reason = MeshRenderEligibilityReasonUVE::Eligible;
        visible.worldMatrix = Math::Matrix4x4UVE::ComposeTrsUVE(
            Math::Vector3UVE{0.0F, 0.0F, -10.0F}, Math::QuaternionUVE{}, Math::Vector3UVE{1.0F, 1.0F, 1.0F});
        visible.worldBounds = localBounds.TransformUVE(visible.worldMatrix);
        MeshRenderEligibilityUVE eligibility;
        ASSERT_TRUE(TestMeshRenderVisibilityUVE(visible, frustum, eligibility));
        EXPECT_EQ(eligibility.reason, MeshRenderEligibilityReasonUVE::Eligible);
        EXPECT_GT(eligibility.sortDepth, 0.0F);
        EXPECT_FLOAT_EQ(eligibility.worldMatrix.m[3][2], visible.worldMatrix.m[3][2])
            << "the published matrix must be the placement's, element for element";
        EXPECT_FLOAT_EQ(eligibility.worldBounds.min.z, visible.worldBounds.min.z);
    }
}

TEST(MeshRenderVisibilityUVETest, OutputIsNotContaminatedByAPreviousCall) {
    // The call site reuses one MeshRenderEligibilityUVE across every candidate in a cull, so a
    // rejection must not leave the previous candidate's values behind. Currently that holds
    // because the function builds a fresh local and assigns it wholesale - but that is an
    // implementation detail, and the property is what callers actually depend on.
    const Math::AabbUVE localBounds =
        Math::AabbUVE::FromCenterExtentsUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.5F, 0.5F, 0.5F});
    const Math::Matrix4x4UVE view =
        Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{});
    const Math::Matrix4x4UVE projection =
        Math::Matrix4x4UVE::PerspectiveUVE(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
    const Math::FrustumUVE frustum = Math::FrustumUVE::FromViewProjectionUVE(projection * view);

    MeshRenderPlacementUVE visible;
    visible.reason = MeshRenderEligibilityReasonUVE::Eligible;
    visible.worldMatrix = Math::Matrix4x4UVE::ComposeTrsUVE(
        Math::Vector3UVE{0.0F, 0.0F, -10.0F}, Math::QuaternionUVE{}, Math::Vector3UVE{1.0F, 1.0F, 1.0F});
    visible.worldBounds = localBounds.TransformUVE(visible.worldMatrix);

    MeshRenderEligibilityUVE reused;
    ASSERT_TRUE(TestMeshRenderVisibilityUVE(visible, frustum, reused));
    ASSERT_GT(reused.sortDepth, 0.0F);

    // Same output struct, now given a placement that cannot be placed at all.
    MeshRenderPlacementUVE unplaced;
    unplaced.reason = MeshRenderEligibilityReasonUVE::InvalidWorldTransform;
    EXPECT_FALSE(TestMeshRenderVisibilityUVE(unplaced, frustum, reused));
    EXPECT_EQ(reused.reason, MeshRenderEligibilityReasonUVE::InvalidWorldTransform);
    EXPECT_FLOAT_EQ(reused.sortDepth, 0.0F)
        << "the previous candidate's depth must not survive into this one's rejection";
}

} // namespace
} // namespace UVE::Render::Tests
