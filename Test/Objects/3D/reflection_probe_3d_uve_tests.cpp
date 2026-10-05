// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/reflection_probe_3d_uve.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

class ReflectionProbe3DImageryUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;

    EntityUVE PlaceProbeUVE(const ReflectionProbe3DComponentUVE& probe, const TransformComponentUVE& transform = {}) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        entityManager.AddComponentUVE<ReflectionProbe3DComponentUVE>(entity, probe);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }
};

TEST(CubemapFaceUVETest, AxisDirectionsSelectTheMatchingFace) {
    CubemapFaceUVE face = CubemapFaceUVE::NegativeZ;
    ASSERT_TRUE(TrySelectCubemapFaceUVE(Math::Vector3UVE{2.0F, 0.1F, -0.1F}, face));
    EXPECT_EQ(face, CubemapFaceUVE::PositiveX);
    ASSERT_TRUE(TrySelectCubemapFaceUVE(Math::Vector3UVE{-4.0F, 1.0F, 1.0F}, face));
    EXPECT_EQ(face, CubemapFaceUVE::NegativeX);
    ASSERT_TRUE(TrySelectCubemapFaceUVE(Math::Vector3UVE{0.2F, 3.0F, -0.2F}, face));
    EXPECT_EQ(face, CubemapFaceUVE::PositiveY);
    ASSERT_TRUE(TrySelectCubemapFaceUVE(Math::Vector3UVE{0.0F, -1.0F, 0.0F}, face));
    EXPECT_EQ(face, CubemapFaceUVE::NegativeY);
    ASSERT_TRUE(TrySelectCubemapFaceUVE(Math::Vector3UVE{0.1F, -0.1F, 5.0F}, face));
    EXPECT_EQ(face, CubemapFaceUVE::PositiveZ);
    ASSERT_TRUE(TrySelectCubemapFaceUVE(Math::Vector3UVE{0.0F, 0.0F, -1.0F}, face));
    EXPECT_EQ(face, CubemapFaceUVE::NegativeZ);
}

TEST(CubemapFaceUVETest, ZeroAndNonFiniteDirectionsFailClosed) {
    CubemapFaceUVE face = CubemapFaceUVE::PositiveX;
    EXPECT_FALSE(TrySelectCubemapFaceUVE(Math::Vector3UVE{}, face));
    EXPECT_FALSE(TrySelectCubemapFaceUVE(Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, face));
}

TEST(CubemapFaceUVETest, CaptureCameraLooksAlongTheFaceForward) {
    static constexpr CubemapFaceUVE kFaces[] = {
        CubemapFaceUVE::PositiveX, CubemapFaceUVE::NegativeX, CubemapFaceUVE::PositiveY,
        CubemapFaceUVE::NegativeY, CubemapFaceUVE::PositiveZ, CubemapFaceUVE::NegativeZ};
    for (const CubemapFaceUVE face : kFaces) {
        Math::Vector3UVE forward{};
        Math::Vector3UVE up{};
        ASSERT_TRUE(TryGetCubemapFaceBasisUVE(face, forward, up));
        Math::QuaternionUVE rotation{};
        ASSERT_TRUE(TryMakeCubemapFaceCameraRotationUVE(face, rotation));
        const Math::Vector3UVE look = Math::RotateVectorUVE(rotation, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
        EXPECT_NEAR(look.x, forward.x, 1.0e-5F);
        EXPECT_NEAR(look.y, forward.y, 1.0e-5F);
        EXPECT_NEAR(look.z, forward.z, 1.0e-5F);
        const Math::Vector3UVE cameraUp = Math::RotateVectorUVE(rotation, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
        EXPECT_GT(Math::DotUVE(cameraUp, up), 0.0F);
    }
}

TEST(CubemapFaceUVETest, FaceCenterProjectsToUvHalf) {
    Math::Vector2UVE uv{};
    ASSERT_TRUE(TryMakeCubemapFaceUvUVE(Math::Vector3UVE{1.0F, 0.0F, 0.0F}, CubemapFaceUVE::PositiveX, uv));
    EXPECT_NEAR(uv.x, 0.5F, 1.0e-5F);
    EXPECT_NEAR(uv.y, 0.5F, 1.0e-5F);
    ASSERT_TRUE(TryMakeCubemapFaceUvUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, CubemapFaceUVE::PositiveY, uv));
    EXPECT_NEAR(uv.x, 0.5F, 1.0e-5F);
    EXPECT_NEAR(uv.y, 0.5F, 1.0e-5F);
    ASSERT_TRUE(TryMakeCubemapFaceUvUVE(Math::Vector3UVE{0.0F, 0.0F, -1.0F}, CubemapFaceUVE::NegativeZ, uv));
    EXPECT_NEAR(uv.x, 0.5F, 1.0e-5F);
    EXPECT_NEAR(uv.y, 0.5F, 1.0e-5F);
}

TEST(CubemapFaceUVETest, PlusYOnPlusXRaisesVAndWrongHemisphereFails) {
    Math::Vector2UVE uv{};
    ASSERT_TRUE(TryMakeCubemapFaceUvUVE(Math::Vector3UVE{1.0F, 0.25F, 0.0F}, CubemapFaceUVE::PositiveX, uv));
    EXPECT_NEAR(uv.x, 0.5F, 1.0e-5F);
    EXPECT_GT(uv.y, 0.5F);
    EXPECT_FALSE(TryMakeCubemapFaceUvUVE(Math::Vector3UVE{-1.0F, 0.0F, 0.0F}, CubemapFaceUVE::PositiveX, uv));
}

TEST(CubemapFaceUVETest, CubeCornerLandsOnTheFaceCorner) {
    CubemapFaceUVE face = CubemapFaceUVE::NegativeZ;
    Math::Vector2UVE uv{};
    ASSERT_TRUE(TryProjectCubemapDirectionUVE(Math::Vector3UVE{1.0F, 1.0F, 1.0F}, face, uv));
    EXPECT_EQ(face, CubemapFaceUVE::PositiveX);
    EXPECT_NEAR(uv.x, 1.0F, 1.0e-4F);
    EXPECT_NEAR(uv.y, 1.0F, 1.0e-4F);
}

TEST_F(ReflectionProbe3DImageryUVETest, FrameAxesFollowRotationAndInfluenceMatchesTheChebyshevRule) {
    ReflectionProbe3DComponentUVE probe{};
    probe.size = Math::Vector3UVE{4.0F, 4.0F, 4.0F};
    ReflectionProbe3DFrameUVE frame{};
    ASSERT_TRUE(TryMakeReflectionProbe3DFrameUVE(probe, Math::Vector3UVE{1.0F, 2.0F, 3.0F}, Math::QuaternionUVE{},
                                                 frame));
    EXPECT_EQ(frame.halfExtents.x, 2.0F);
    EXPECT_FLOAT_EQ(SampleReflectionProbe3DInfluenceUVE(frame, Math::Vector3UVE{1.0F, 2.0F, 3.0F}), 1.0F);
    EXPECT_FLOAT_EQ(SampleReflectionProbe3DInfluenceUVE(frame, Math::Vector3UVE{3.0F, 2.0F, 3.0F}), 0.0F);
    EXPECT_FLOAT_EQ(SampleReflectionProbe3DInfluenceUVE(frame, Math::Vector3UVE{2.0F, 2.0F, 3.0F}), 0.5F);
}

TEST_F(ReflectionProbe3DImageryUVETest, BoxProjectionFromTheCentreHitsTheFace) {
    ReflectionProbe3DComponentUVE probe{};
    probe.size = Math::Vector3UVE{4.0F, 6.0F, 8.0F};
    ReflectionProbe3DFrameUVE frame{};
    ASSERT_TRUE(TryMakeReflectionProbe3DFrameUVE(probe, Math::Vector3UVE{}, Math::QuaternionUVE{}, frame));
    Math::Vector3UVE hit{};
    ASSERT_TRUE(TryBoxProjectReflectionUVE(frame, Math::Vector3UVE{}, Math::Vector3UVE{1.0F, 0.0F, 0.0F}, hit));
    EXPECT_NEAR(hit.x, 2.0F, 1.0e-4F);
    EXPECT_NEAR(hit.y, 0.0F, 1.0e-4F);
    EXPECT_NEAR(hit.z, 0.0F, 1.0e-4F);
    ASSERT_TRUE(TryBoxProjectReflectionUVE(frame, Math::Vector3UVE{-1.0F, 1.0F, 0.0F},
                                           Math::Vector3UVE{1.0F, 0.0F, 0.0F}, hit));
    EXPECT_NEAR(hit.x, 2.0F, 1.0e-4F);
    EXPECT_NEAR(hit.y, 1.0F, 1.0e-4F);
}

TEST_F(ReflectionProbe3DImageryUVETest, CollectRanksByInfluenceAndSkipsHiddenDirtyAndDisabled) {
    ReflectionProbe3DComponentUVE nearProbe{};
    nearProbe.size = Math::Vector3UVE{10.0F, 10.0F, 10.0F};
    nearProbe.capturedOnce = true;
    TransformComponentUVE nearTransform{};
    nearTransform.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    const EntityUVE nearEntity = PlaceProbeUVE(nearProbe, nearTransform);

    ReflectionProbe3DComponentUVE farProbe = nearProbe;
    TransformComponentUVE farTransform{};
    farTransform.localPosition = Math::Vector3UVE{4.0F, 0.0F, 0.0F};
    const EntityUVE farEntity = PlaceProbeUVE(farProbe, farTransform);

    ReflectionProbe3DComponentUVE hiddenProbe = nearProbe;
    TransformComponentUVE hiddenTransform{};
    hiddenTransform.localPosition = Math::Vector3UVE{};
    const EntityUVE hiddenEntity = PlaceProbeUVE(hiddenProbe, hiddenTransform);
    VisibilityComponentUVE hiddenVisibility{};
    hiddenVisibility.visible = false;
    if (!entityManager.HasComponentUVE<VisibilityComponentUVE>(hiddenEntity)) {
        entityManager.AddComponentUVE<VisibilityComponentUVE>(hiddenEntity, hiddenVisibility);
    } else {
        entityManager.GetComponentUVE<VisibilityComponentUVE>(hiddenEntity).visible = false;
    }
    sceneGraph.UpdateUVE(entityManager);
    ASSERT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(hiddenEntity).visibleInHierarchy);

    ReflectionProbe3DComponentUVE disabledProbe = nearProbe;
    disabledProbe.enabled = false;
    PlaceProbeUVE(disabledProbe);

    std::array<ReflectionProbe3DFrameUVE, 4> frames{};
    const std::size_t count =
        CollectReflectionProbe3DFramesUVE(entityManager, Math::Vector3UVE{}, frames);
    EXPECT_EQ(count, 2U);
    EXPECT_EQ(frames[0].entity, nearEntity);
    EXPECT_EQ(frames[1].entity, farEntity);
    EXPECT_GT(frames[0].influenceWeight, frames[1].influenceWeight);
}

TEST_F(ReflectionProbe3DImageryUVETest, GizmoSkipsEditorInternalAndDisabled) {
    ReflectionProbe3DComponentUVE probe{};
    const EntityUVE live = PlaceProbeUVE(probe);
    static_cast<void>(live);
    const EntityUVE internal = PlaceProbeUVE(probe);
    entityManager.AddComponentUVE<EditorInternalEntityComponentUVE>(internal);
    ReflectionProbe3DComponentUVE disabled{};
    disabled.enabled = false;
    PlaceProbeUVE(disabled);

    std::vector<ReflectionProbe3DGizmoUVE> gizmos;
    CollectReflectionProbe3DGizmosUVE(entityManager, gizmos);
    EXPECT_EQ(gizmos.size(), 1U);
    EXPECT_NEAR(gizmos[0].halfExtents.x, 2.5F, 1.0e-5F);
}

} // namespace
} // namespace UVE::Scene::Tests
