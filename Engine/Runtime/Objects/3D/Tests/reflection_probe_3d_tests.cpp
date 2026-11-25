// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "reflection_probe_3d.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "editor_internal_entity_component.h"
#include "transform_component.h"
#include "visibility_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "quaternion.h"
#include "memory_manager.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

class ReflectionProbe3DImageryTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;

    Entity PlaceProbe(const ReflectionProbe3DComponent& probe, const TransformComponent& transform = {}) {
        const Entity entity = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, entity, transform);
        entityManager.AddComponent<ReflectionProbe3DComponent>(entity, probe);
        sceneGraph.Update(entityManager);
        return entity;
    }
};

TEST(CubemapFaceTest, AxisDirectionsSelectTheMatchingFace) {
    CubemapFace face = CubemapFace::NegativeZ;
    ASSERT_TRUE(TrySelectCubemapFace(Math::Vector3{2.0F, 0.1F, -0.1F}, face));
    EXPECT_EQ(face, CubemapFace::PositiveX);
    ASSERT_TRUE(TrySelectCubemapFace(Math::Vector3{-4.0F, 1.0F, 1.0F}, face));
    EXPECT_EQ(face, CubemapFace::NegativeX);
    ASSERT_TRUE(TrySelectCubemapFace(Math::Vector3{0.2F, 3.0F, -0.2F}, face));
    EXPECT_EQ(face, CubemapFace::PositiveY);
    ASSERT_TRUE(TrySelectCubemapFace(Math::Vector3{0.0F, -1.0F, 0.0F}, face));
    EXPECT_EQ(face, CubemapFace::NegativeY);
    ASSERT_TRUE(TrySelectCubemapFace(Math::Vector3{0.1F, -0.1F, 5.0F}, face));
    EXPECT_EQ(face, CubemapFace::PositiveZ);
    ASSERT_TRUE(TrySelectCubemapFace(Math::Vector3{0.0F, 0.0F, -1.0F}, face));
    EXPECT_EQ(face, CubemapFace::NegativeZ);
}

TEST(CubemapFaceTest, ZeroAndNonFiniteDirectionsFailClosed) {
    CubemapFace face = CubemapFace::PositiveX;
    EXPECT_FALSE(TrySelectCubemapFace(Math::Vector3{}, face));
    EXPECT_FALSE(TrySelectCubemapFace(Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, face));
}

TEST(CubemapFaceTest, CaptureCameraLooksAlongTheFaceForward) {
    static constexpr CubemapFace kFaces[] = {
        CubemapFace::PositiveX, CubemapFace::NegativeX, CubemapFace::PositiveY,
        CubemapFace::NegativeY, CubemapFace::PositiveZ, CubemapFace::NegativeZ};
    for (const CubemapFace face : kFaces) {
        Math::Vector3 forward{};
        Math::Vector3 up{};
        ASSERT_TRUE(TryGetCubemapFaceBasis(face, forward, up));
        Math::Quaternion rotation{};
        ASSERT_TRUE(TryMakeCubemapFaceCameraRotation(face, rotation));
        const Math::Vector3 look = Math::RotateVector(rotation, Math::Vector3{0.0F, 0.0F, -1.0F});
        EXPECT_NEAR(look.x, forward.x, 1.0e-5F);
        EXPECT_NEAR(look.y, forward.y, 1.0e-5F);
        EXPECT_NEAR(look.z, forward.z, 1.0e-5F);
        const Math::Vector3 cameraUp = Math::RotateVector(rotation, Math::Vector3{0.0F, 1.0F, 0.0F});
        EXPECT_GT(Math::Dot(cameraUp, up), 0.0F);
    }
}

TEST(CubemapFaceTest, FaceCenterProjectsToUvHalf) {
    Math::Vector2 uv{};
    ASSERT_TRUE(TryMakeCubemapFaceUv(Math::Vector3{1.0F, 0.0F, 0.0F}, CubemapFace::PositiveX, uv));
    EXPECT_NEAR(uv.x, 0.5F, 1.0e-5F);
    EXPECT_NEAR(uv.y, 0.5F, 1.0e-5F);
    ASSERT_TRUE(TryMakeCubemapFaceUv(Math::Vector3{0.0F, 1.0F, 0.0F}, CubemapFace::PositiveY, uv));
    EXPECT_NEAR(uv.x, 0.5F, 1.0e-5F);
    EXPECT_NEAR(uv.y, 0.5F, 1.0e-5F);
    ASSERT_TRUE(TryMakeCubemapFaceUv(Math::Vector3{0.0F, 0.0F, -1.0F}, CubemapFace::NegativeZ, uv));
    EXPECT_NEAR(uv.x, 0.5F, 1.0e-5F);
    EXPECT_NEAR(uv.y, 0.5F, 1.0e-5F);
}

TEST(CubemapFaceTest, PlusYOnPlusXRaisesVAndWrongHemisphereFails) {
    Math::Vector2 uv{};
    ASSERT_TRUE(TryMakeCubemapFaceUv(Math::Vector3{1.0F, 0.25F, 0.0F}, CubemapFace::PositiveX, uv));
    EXPECT_NEAR(uv.x, 0.5F, 1.0e-5F);
    EXPECT_GT(uv.y, 0.5F);
    EXPECT_FALSE(TryMakeCubemapFaceUv(Math::Vector3{-1.0F, 0.0F, 0.0F}, CubemapFace::PositiveX, uv));
}

TEST(CubemapFaceTest, CubeCornerLandsOnTheFaceCorner) {
    CubemapFace face = CubemapFace::NegativeZ;
    Math::Vector2 uv{};
    ASSERT_TRUE(TryProjectCubemapDirection(Math::Vector3{1.0F, 1.0F, 1.0F}, face, uv));
    EXPECT_EQ(face, CubemapFace::PositiveX);
    EXPECT_NEAR(uv.x, 1.0F, 1.0e-4F);
    EXPECT_NEAR(uv.y, 1.0F, 1.0e-4F);
}

TEST_F(ReflectionProbe3DImageryTest, FrameAxesFollowRotationAndInfluenceMatchesTheChebyshevRule) {
    ReflectionProbe3DComponent probe{};
    probe.size = Math::Vector3{4.0F, 4.0F, 4.0F};
    probe.resolution = ReflectionProbeResolution::High;
    ReflectionProbe3DFrame frame{};
    ASSERT_TRUE(TryMakeReflectionProbe3DFrame(probe, Math::Vector3{1.0F, 2.0F, 3.0F}, Math::Quaternion{},
                                                 frame));
    EXPECT_EQ(frame.halfExtents.x, 2.0F);
    EXPECT_EQ(frame.captureResolution, 256U);
    EXPECT_FLOAT_EQ(SampleReflectionProbe3DInfluence(frame, Math::Vector3{1.0F, 2.0F, 3.0F}), 1.0F);
    EXPECT_FLOAT_EQ(SampleReflectionProbe3DInfluence(frame, Math::Vector3{3.0F, 2.0F, 3.0F}), 0.0F);
    EXPECT_FLOAT_EQ(SampleReflectionProbe3DInfluence(frame, Math::Vector3{2.0F, 2.0F, 3.0F}), 0.5F);
}

TEST_F(ReflectionProbe3DImageryTest, ResolutionTiersAreBoundedAndMapToSquareCaptureSizes) {
    const std::array<ReflectionProbeResolution, 4> tiers{
        ReflectionProbeResolution::Low, ReflectionProbeResolution::Medium,
        ReflectionProbeResolution::High, ReflectionProbeResolution::Ultra};
    const std::array<std::uint32_t, 4> pixels{64U, 128U, 256U, 512U};
    ReflectionProbe3DComponent probe{};
    EXPECT_EQ(probe.resolution, ReflectionProbeResolution::Medium);
    EXPECT_EQ(GetReflectionProbeResolutionPixels(probe.resolution), 128U);

    for (std::size_t i = 0; i < tiers.size(); ++i) {
        probe.resolution = tiers[i];
        EXPECT_TRUE(IsReflectionProbe3DObjectComponentValid(probe));
        EXPECT_EQ(GetReflectionProbeResolutionPixels(tiers[i]), pixels[i]);
        ReflectionProbe3DFrame frame{};
        ASSERT_TRUE(TryMakeReflectionProbe3DFrame(probe, Math::Vector3{}, Math::Quaternion{}, frame));
        EXPECT_EQ(frame.captureResolution, pixels[i]);
    }

    probe.resolution = static_cast<ReflectionProbeResolution>(255U);
    EXPECT_FALSE(IsReflectionProbe3DObjectComponentValid(probe));
    ReflectionProbe3DFrame invalidFrame{};
    EXPECT_FALSE(TryMakeReflectionProbe3DFrame(probe, Math::Vector3{}, Math::Quaternion{}, invalidFrame));
}

TEST_F(ReflectionProbe3DImageryTest, BoxProjectionFromTheCentreHitsTheFace) {
    ReflectionProbe3DComponent probe{};
    probe.size = Math::Vector3{4.0F, 6.0F, 8.0F};
    ReflectionProbe3DFrame frame{};
    ASSERT_TRUE(TryMakeReflectionProbe3DFrame(probe, Math::Vector3{}, Math::Quaternion{}, frame));
    Math::Vector3 hit{};
    ASSERT_TRUE(TryBoxProjectReflection(frame, Math::Vector3{}, Math::Vector3{1.0F, 0.0F, 0.0F}, hit));
    EXPECT_NEAR(hit.x, 2.0F, 1.0e-4F);
    EXPECT_NEAR(hit.y, 0.0F, 1.0e-4F);
    EXPECT_NEAR(hit.z, 0.0F, 1.0e-4F);
    ASSERT_TRUE(TryBoxProjectReflection(frame, Math::Vector3{-1.0F, 1.0F, 0.0F},
                                           Math::Vector3{1.0F, 0.0F, 0.0F}, hit));
    EXPECT_NEAR(hit.x, 2.0F, 1.0e-4F);
    EXPECT_NEAR(hit.y, 1.0F, 1.0e-4F);
}

TEST_F(ReflectionProbe3DImageryTest, CollectRanksByInfluenceAndSkipsHiddenDirtyAndDisabled) {
    ReflectionProbe3DComponent nearProbe{};
    nearProbe.size = Math::Vector3{10.0F, 10.0F, 10.0F};
    nearProbe.capturedOnce = true;
    TransformComponent nearTransform{};
    nearTransform.localPosition = Math::Vector3{1.0F, 0.0F, 0.0F};
    const Entity nearEntity = PlaceProbe(nearProbe, nearTransform);

    ReflectionProbe3DComponent farProbe = nearProbe;
    TransformComponent farTransform{};
    farTransform.localPosition = Math::Vector3{4.0F, 0.0F, 0.0F};
    const Entity farEntity = PlaceProbe(farProbe, farTransform);

    ReflectionProbe3DComponent hiddenProbe = nearProbe;
    TransformComponent hiddenTransform{};
    hiddenTransform.localPosition = Math::Vector3{};
    const Entity hiddenEntity = PlaceProbe(hiddenProbe, hiddenTransform);
    VisibilityComponent hiddenVisibility{};
    hiddenVisibility.visible = false;
    if (!entityManager.HasComponent<VisibilityComponent>(hiddenEntity)) {
        entityManager.AddComponent<VisibilityComponent>(hiddenEntity, hiddenVisibility);
    } else {
        entityManager.GetComponent<VisibilityComponent>(hiddenEntity).visible = false;
    }
    sceneGraph.Update(entityManager);
    ASSERT_FALSE(entityManager.GetComponent<VisibilityComponent>(hiddenEntity).visibleInHierarchy);

    ReflectionProbe3DComponent disabledProbe = nearProbe;
    disabledProbe.enabled = false;
    PlaceProbe(disabledProbe);

    std::array<ReflectionProbe3DFrame, 4> frames{};
    const std::size_t count =
        CollectReflectionProbe3DFrames(entityManager, Math::Vector3{}, frames);
    EXPECT_EQ(count, 2U);
    EXPECT_EQ(frames[0].entity, nearEntity);
    EXPECT_EQ(frames[1].entity, farEntity);
    EXPECT_GT(frames[0].influenceWeight, frames[1].influenceWeight);
}

TEST_F(ReflectionProbe3DImageryTest, GizmoSkipsEditorInternalAndDisabled) {
    ReflectionProbe3DComponent probe{};
    const Entity live = PlaceProbe(probe);
    static_cast<void>(live);
    const Entity internal = PlaceProbe(probe);
    entityManager.AddComponent<EditorInternalEntityComponent>(internal);
    ReflectionProbe3DComponent disabled{};
    disabled.enabled = false;
    PlaceProbe(disabled);

    std::vector<ReflectionProbe3DGizmo> gizmos;
    CollectReflectionProbe3DGizmos(entityManager, gizmos);
    EXPECT_EQ(gizmos.size(), 1U);
    EXPECT_NEAR(gizmos[0].halfExtents.x, 2.5F, 1.0e-5F);
}

TEST_F(ReflectionProbe3DImageryTest, FramePreservesVisibilityLayersAndResolutionForRendererCapture) {
    ReflectionProbe3DComponent probe{};
    probe.size = Math::Vector3{8.0F, 6.0F, 10.0F};
    probe.visibilityLayers = 0x00000004U;
    probe.resolution = ReflectionProbeResolution::High;
    probe.capturedOnce = true;
    probe.captureGeneration = 3U;

    ReflectionProbe3DFrame frame{};
    ASSERT_TRUE(TryMakeReflectionProbe3DFrame(probe, Math::Vector3{1.0F, 2.0F, 3.0F},
                                                 Math::Quaternion{}, frame));
    EXPECT_EQ(frame.visibilityLayers, 0x00000004U);
    EXPECT_EQ(frame.captureResolution, 256U);
    EXPECT_FLOAT_EQ(frame.halfExtents.x, 4.0F);
    EXPECT_FLOAT_EQ(frame.halfExtents.y, 3.0F);
    EXPECT_FLOAT_EQ(frame.halfExtents.z, 5.0F);
}

} // namespace
} // namespace UVE::Scene::Tests
