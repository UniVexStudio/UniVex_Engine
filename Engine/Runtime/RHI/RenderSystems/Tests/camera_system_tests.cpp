// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "camera_system.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "event_system.h"
#include "memory_manager.h"
#include "platform.h"
#include "camera_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "scene_graph.h"

namespace UVE::Render::Tests {
namespace {

constexpr float kEpsilon = 1e-3F;

class CameraSystemTest : public ::testing::Test {
protected:
    // Entities reach CameraSystem only via SceneGraph::AttachTransform (see
    // ICameraSystem's doc comment), so every fixture entity goes through the same path a real
    // scene would use, exactly like SceneGraphTest's own fixture.
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    CameraSystem cameraSystem;

    [[nodiscard]] Scene::Entity MakeCameraEntity(Math::Vector3 position, Math::Quaternion rotation,
                                                        Scene::CameraComponent camera) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = position;
        local.localRotation = rotation;
        sceneGraph.AttachTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::CameraComponent>(entity, camera);
        return entity;
    }
};

TEST_F(CameraSystemTest, ComputeViewMatrix_IdentityRotation_TransformsWorldPointRelativeToEye) {
    const Scene::Entity camera =
        MakeCameraEntity(Math::Vector3{0.0F, 0.0F, 5.0F}, Math::Quaternion{}, Scene::CameraComponent{});

    const Math::Matrix4x4 view = cameraSystem.ComputeViewMatrix(entityManager, camera);
    const Math::Vector3 viewSpaceOrigin = Math::TransformPoint(view, Math::Vector3{0.0F, 0.0F, 0.0F});

    EXPECT_NEAR(viewSpaceOrigin.x, 0.0F, kEpsilon);
    EXPECT_NEAR(viewSpaceOrigin.y, 0.0F, kEpsilon);
    EXPECT_NEAR(viewSpaceOrigin.z, -5.0F, kEpsilon);
}

TEST_F(CameraSystemTest, ComputeProjectionMatrix_MapsNearAndFarPlanesToExpectedDepth) {
    Scene::CameraComponent camera;
    camera.fieldOfViewDegrees = 90.0F;
    camera.nearPlane = 1.0F;
    camera.farPlane = 100.0F;
    const Scene::Entity cameraEntity =
        MakeCameraEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{}, camera);

    const Math::Matrix4x4 projection = cameraSystem.ComputeProjectionMatrix(entityManager, cameraEntity, 1.0F);

    // Manual homogeneous multiply (TransformPoint assumes an affine w=1 matrix and doesn't
    // perform a perspective divide, so it can't be used for a projection matrix).
    const auto clipZ = [&](float viewZ) {
        return projection.m[2][2] * viewZ + projection.m[2][3];
    };
    const auto clipW = [&](float viewZ) { return projection.m[3][2] * viewZ + projection.m[3][3]; };

    EXPECT_NEAR(clipZ(-1.0F) / clipW(-1.0F), 0.0F, kEpsilon);
    EXPECT_NEAR(clipZ(-100.0F) / clipW(-100.0F), 1.0F, kEpsilon);
}

TEST_F(CameraSystemTest, ComputeProjectionMatrix_OrthographicMatchesBox) {
    Scene::CameraComponent camera;
    camera.projection = Scene::CameraProjectionMode::Orthographic;
    camera.orthographicSize = 10.0F;
    camera.nearPlane = 1.0F;
    camera.farPlane = 100.0F;
    const Scene::Entity cameraEntity =
        MakeCameraEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{}, camera);

    const Math::Matrix4x4 projection = cameraSystem.ComputeProjectionMatrix(entityManager, cameraEntity, 2.0F);
    const Math::Matrix4x4 expected =
        Math::Matrix4x4::Orthographic(-20.0F, 20.0F, -10.0F, 10.0F, 1.0F, 100.0F);
    EXPECT_EQ(projection, expected);
}

TEST_F(CameraSystemTest, ComputeProjectionMatrix_HumanEyeIgnoresAuthoredFov) {
    Scene::CameraComponent humanEye;
    humanEye.projection = Scene::CameraProjectionMode::HumanEye;
    humanEye.fieldOfViewDegrees = 30.0F;
    humanEye.nearPlane = 1.0F;
    humanEye.farPlane = 100.0F;
    const float aspect = 16.0F / 9.0F;
    const Scene::Entity humanEyeEntity =
        MakeCameraEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{}, humanEye);

    Scene::CameraComponent matchingPerspective;
    matchingPerspective.fieldOfViewDegrees = Scene::HumanEyeVerticalFieldOfViewDegrees(aspect);
    matchingPerspective.nearPlane = 1.0F;
    matchingPerspective.farPlane = 100.0F;
    const Scene::Entity perspectiveEntity =
        MakeCameraEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{}, matchingPerspective);

    Scene::CameraComponent authoredThirty;
    authoredThirty.fieldOfViewDegrees = 30.0F;
    authoredThirty.nearPlane = 1.0F;
    authoredThirty.farPlane = 100.0F;
    const Scene::Entity thirtyEntity =
        MakeCameraEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{}, authoredThirty);

    const Math::Matrix4x4 humanEyeProjection =
        cameraSystem.ComputeProjectionMatrix(entityManager, humanEyeEntity, aspect);
    const Math::Matrix4x4 matchingProjection =
        cameraSystem.ComputeProjectionMatrix(entityManager, perspectiveEntity, aspect);
    const Math::Matrix4x4 thirtyProjection =
        cameraSystem.ComputeProjectionMatrix(entityManager, thirtyEntity, aspect);
    EXPECT_EQ(humanEyeProjection, matchingProjection);
    EXPECT_NE(humanEyeProjection, thirtyProjection);
}

TEST_F(CameraSystemTest, ComputeFrustumCorners_OrthographicKeepsNearAndFarTheSameSize) {
    Scene::CameraComponent camera;
    camera.projection = Scene::CameraProjectionMode::Orthographic;
    camera.orthographicSize = 1.0F;
    camera.nearPlane = 1.0F;
    camera.farPlane = 3.0F;
    const Scene::Entity cameraEntity =
        MakeCameraEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{}, camera);

    const CameraFrustumCorners corners = cameraSystem.ComputeFrustumCorners(entityManager, cameraEntity, 2.0F);
    EXPECT_EQ(corners[0], (Math::Vector3{-2.0F, -1.0F, -1.0F}));
    EXPECT_EQ(corners[3], (Math::Vector3{2.0F, 1.0F, -1.0F}));
    EXPECT_EQ(corners[4], (Math::Vector3{-2.0F, -1.0F, -3.0F}));
    EXPECT_EQ(corners[7], (Math::Vector3{2.0F, 1.0F, -3.0F}));
}

TEST_F(CameraSystemTest, ComputeViewProjection_EqualsProjectionTimesView) {
    Scene::CameraComponent camera;
    camera.fieldOfViewDegrees = 60.0F;
    const Scene::Entity cameraEntity =
        MakeCameraEntity(Math::Vector3{1.0F, 2.0F, 3.0F}, Math::Quaternion{}, camera);

    const Math::Matrix4x4 view = cameraSystem.ComputeViewMatrix(entityManager, cameraEntity);
    const Math::Matrix4x4 projection = cameraSystem.ComputeProjectionMatrix(entityManager, cameraEntity, 1.5F);
    const Math::Matrix4x4 expected = projection * view;

    const Math::Matrix4x4 actual = cameraSystem.ComputeViewProjection(entityManager, cameraEntity, 1.5F);

    EXPECT_EQ(actual, expected);
}

TEST_F(CameraSystemTest, ComputeFrustumCorners_IdentityCamera_ReturnsExpectedWorldCorners) {
    Scene::CameraComponent camera;
    camera.fieldOfViewDegrees = 90.0F;
    camera.nearPlane = 1.0F;
    camera.farPlane = 3.0F;
    const Scene::Entity cameraEntity =
        MakeCameraEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{}, camera);

    const CameraFrustumCorners corners = cameraSystem.ComputeFrustumCorners(entityManager, cameraEntity, 2.0F);

    EXPECT_EQ(corners[0], (Math::Vector3{-2.0F, -1.0F, -1.0F}));
    EXPECT_EQ(corners[3], (Math::Vector3{2.0F, 1.0F, -1.0F}));
    EXPECT_EQ(corners[4], (Math::Vector3{-6.0F, -3.0F, -3.0F}));
    EXPECT_EQ(corners[7], (Math::Vector3{6.0F, 3.0F, -3.0F}));
}

TEST_F(CameraSystemTest, ComputeFrustumCorners_PreservesFiniteExtremeFarCornerCancellation) {
    Scene::CameraComponent camera;
    camera.fieldOfViewDegrees = 90.0F;
    camera.nearPlane = 1.0F;
    camera.farPlane = std::numeric_limits<float>::max();
    const float maximum = std::numeric_limits<float>::max();
    const Math::Quaternion yawMinusFortyFive{0.0F, -0.3826834324F, 0.0F, 0.9238795325F};
    const Scene::Entity cameraEntity =
        MakeCameraEntity(Math::Vector3{maximum, maximum, maximum}, yawMinusFortyFive, camera);

    const CameraFrustumCorners corners = cameraSystem.ComputeFrustumCorners(entityManager, cameraEntity, 1.0F);

    EXPECT_TRUE(std::isfinite(corners[4].x));
    EXPECT_TRUE(std::isfinite(corners[4].y));
    EXPECT_TRUE(std::isfinite(corners[4].z));
    EXPECT_GT(corners[4].x, maximum * 0.99F);
}

TEST_F(CameraSystemTest, ExtractFrustum_EndToEnd_ClassifiesKnownBoxesCorrectly) {
    Scene::CameraComponent camera;
    camera.fieldOfViewDegrees = 90.0F;
    camera.nearPlane = 1.0F;
    camera.farPlane = 100.0F;
    const Scene::Entity cameraEntity =
        MakeCameraEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{}, camera);

    const Math::Matrix4x4 viewProjection = cameraSystem.ComputeViewProjection(entityManager, cameraEntity, 1.0F);
    const Math::Frustum frustum = cameraSystem.ExtractFrustum(viewProjection);

    const Math::Aabb visibleBox =
        Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, -10.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Math::Aabb behindCameraBox =
        Math::Aabb::FromCenterExtents(Math::Vector3{0.0F, 0.0F, 10.0F}, Math::Vector3{1.0F, 1.0F, 1.0F});
    const Math::Aabb offToTheSideBox = Math::Aabb::FromCenterExtents(Math::Vector3{1000.0F, 0.0F, -10.0F},
                                                                               Math::Vector3{1.0F, 1.0F, 1.0F});

    EXPECT_TRUE(frustum.Intersects(visibleBox));
    EXPECT_FALSE(frustum.Intersects(behindCameraBox));
    EXPECT_FALSE(frustum.Intersects(offToTheSideBox));
}

TEST_F(CameraSystemTest, GetWorldPosition_ReturnsEntityWorldPosition) {
    const Math::Vector3 position{3.0F, -2.0F, 7.5F};
    const Scene::Entity cameraEntity =
        MakeCameraEntity(position, Math::Quaternion{}, Scene::CameraComponent{});

    EXPECT_EQ(cameraSystem.GetWorldPosition(entityManager, cameraEntity), position);
}

TEST(CameraComponentTest, IsCameraComponentValid_RejectsProjectionUnsafeValues) {
    EXPECT_TRUE(Scene::IsCameraComponentValid(Scene::CameraComponent{}));

    Scene::CameraComponent invalidFov = {};
    invalidFov.fieldOfViewDegrees = 0.0F;
    EXPECT_FALSE(Scene::IsCameraComponentValid(invalidFov));
    invalidFov.fieldOfViewDegrees = Scene::kMinimumCameraFieldOfViewDegrees * 0.5F;
    EXPECT_FALSE(Scene::IsCameraComponentValid(invalidFov));
    invalidFov.fieldOfViewDegrees = Scene::kMinimumCameraFieldOfViewDegrees;
    EXPECT_TRUE(Scene::IsCameraComponentValid(invalidFov));
    invalidFov.fieldOfViewDegrees = 180.0F;
    EXPECT_FALSE(Scene::IsCameraComponentValid(invalidFov));
    invalidFov.fieldOfViewDegrees = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Scene::IsCameraComponentValid(invalidFov));

    Scene::CameraComponent invalidPlanes = {};
    invalidPlanes.nearPlane = 0.0F;
    EXPECT_FALSE(Scene::IsCameraComponentValid(invalidPlanes));
    invalidPlanes = {};
    invalidPlanes.farPlane = invalidPlanes.nearPlane;
    EXPECT_FALSE(Scene::IsCameraComponentValid(invalidPlanes));
    invalidPlanes = {};
    invalidPlanes.farPlane = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(Scene::IsCameraComponentValid(invalidPlanes));

    Scene::CameraComponent invalidSize = {};
    invalidSize.orthographicSize = 0.0F;
    EXPECT_FALSE(Scene::IsCameraComponentValid(invalidSize));
    invalidSize.orthographicSize = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Scene::IsCameraComponentValid(invalidSize));

    Scene::CameraComponent invalidMode = {};
    invalidMode.projection = static_cast<Scene::CameraProjectionMode>(99);
    EXPECT_FALSE(Scene::IsCameraComponentValid(invalidMode));
}

TEST(CameraComponentTest, HumanEyeVerticalFieldIgnoresAuthoredFovAndLocksHorizontal) {
    const float aspect = 16.0F / 9.0F;
    const float humanEyeFovY = Scene::HumanEyeVerticalFieldOfViewDegrees(aspect);
    EXPECT_GT(humanEyeFovY, 60.0F);
    EXPECT_LT(humanEyeFovY, 120.0F);

    Scene::CameraComponent humanEye{};
    humanEye.projection = Scene::CameraProjectionMode::HumanEye;
    humanEye.fieldOfViewDegrees = 30.0F;
    EXPECT_NEAR(Scene::VerticalFieldOfViewDegrees(humanEye, aspect), humanEyeFovY, 1.0e-4F);

    Scene::CameraComponent perspective{};
    perspective.fieldOfViewDegrees = 30.0F;
    EXPECT_NEAR(Scene::VerticalFieldOfViewDegrees(perspective, aspect), 30.0F, 1.0e-4F);

    EXPECT_LT(Scene::HumanEyeCenterScale(aspect), 1.0F);
    EXPECT_GT(Scene::HumanEyeCenterScale(aspect), 0.0F);
    EXPECT_NEAR(Scene::HumanEyeCenterScale(4.0F), 1.0F, 1.0e-4F);
}

#if UVE_DEBUG
TEST_F(CameraSystemTest, ComputeViewMatrix_EntityWithoutWorldTransform_Asserts) {
    const Scene::Entity entity = entityManager.CreateEntity();
    EXPECT_DEATH({ static_cast<void>(cameraSystem.ComputeViewMatrix(entityManager, entity)); }, "");
}

TEST_F(CameraSystemTest, ComputeProjectionMatrix_EntityWithoutCameraComponent_Asserts) {
    const Scene::Entity entity = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, entity, Scene::TransformComponent{});
    EXPECT_DEATH({ static_cast<void>(cameraSystem.ComputeProjectionMatrix(entityManager, entity, 1.0F)); }, "");
}

TEST_F(CameraSystemTest, ComputeProjectionMatrix_InvalidAspectRatio_Asserts) {
    const Scene::Entity entity = MakeCameraEntity(Math::Vector3{}, Math::Quaternion{}, Scene::CameraComponent{});
    EXPECT_DEATH({ static_cast<void>(cameraSystem.ComputeProjectionMatrix(entityManager, entity, 0.0F)); }, "");
    EXPECT_DEATH({ static_cast<void>(cameraSystem.ComputeProjectionMatrix(
                       entityManager, entity, std::numeric_limits<float>::quiet_NaN())); }, "");
}

TEST_F(CameraSystemTest, ComputeProjectionMatrix_InvalidCameraParameters_Asserts) {
    const Scene::Entity entity = MakeCameraEntity(Math::Vector3{}, Math::Quaternion{},
                                                        Scene::CameraComponent{180.0F, 0.1F, 100.0F});
    EXPECT_DEATH({ static_cast<void>(cameraSystem.ComputeProjectionMatrix(entityManager, entity, 1.0F)); }, "");
}

TEST_F(CameraSystemTest, ComputeFrustumCorners_InvalidAspectRatio_Asserts) {
    const Scene::Entity entity = MakeCameraEntity(Math::Vector3{}, Math::Quaternion{}, Scene::CameraComponent{});
    EXPECT_DEATH({ static_cast<void>(cameraSystem.ComputeFrustumCorners(entityManager, entity, -1.0F)); }, "");
}

TEST_F(CameraSystemTest, GetWorldPosition_EntityWithoutWorldTransform_Asserts) {
    const Scene::Entity entity = entityManager.CreateEntity();
    EXPECT_DEATH({ static_cast<void>(cameraSystem.GetWorldPosition(entityManager, entity)); }, "");
}

TEST_F(CameraSystemTest, ComputeViewMatrix_InvalidWorldTransform_Asserts) {
    const Scene::Entity entity = MakeCameraEntity(Math::Vector3{}, Math::Quaternion{}, Scene::CameraComponent{});
    // Construct a malformed cache fixture; ordinary live cache access is read-only.
    auto invalidWorld = entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    invalidWorld.worldPosition.x = std::numeric_limits<float>::quiet_NaN();
    entityManager.RemoveComponent<Scene::WorldTransformComponent>(entity);
    entityManager.AddComponent<Scene::WorldTransformComponent>(entity, invalidWorld);

    EXPECT_DEATH({ static_cast<void>(cameraSystem.ComputeViewMatrix(entityManager, entity)); }, "");
}
#endif

#if !UVE_DEBUG
TEST_F(CameraSystemTest, InvalidCameraAndAspectInputsReturnFiniteFallbacksInRelease) {
    Scene::CameraComponent invalidCamera{180.0F, 0.1F, 100.0F};
    const Scene::Entity cameraEntity =
        MakeCameraEntity(Math::Vector3{}, Math::Quaternion{}, invalidCamera);

    const Math::Matrix4x4 invalidProjection =
        cameraSystem.ComputeProjectionMatrix(entityManager, cameraEntity, 1.0F);
    EXPECT_EQ(invalidProjection, Math::Matrix4x4::Identity());

    const Math::Matrix4x4 invalidAspectProjection =
        cameraSystem.ComputeProjectionMatrix(entityManager, cameraEntity, 0.0F);
    EXPECT_EQ(invalidAspectProjection, Math::Matrix4x4::Identity());

    const CameraFrustumCorners invalidCorners =
        cameraSystem.ComputeFrustumCorners(entityManager, cameraEntity, 1.0F);
    for (const Math::Vector3 corner : invalidCorners) {
        EXPECT_EQ(corner, Math::Vector3{});
    }
}

TEST_F(CameraSystemTest, InvalidWorldTransformReturnsFiniteFallbacksInRelease) {
    const Scene::Entity entity = MakeCameraEntity(Math::Vector3{}, Math::Quaternion{}, Scene::CameraComponent{});
    // Construct a malformed cache fixture; ordinary live cache access is read-only.
    auto invalidWorld = entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    invalidWorld.worldPosition.x = std::numeric_limits<float>::quiet_NaN();
    entityManager.RemoveComponent<Scene::WorldTransformComponent>(entity);
    entityManager.AddComponent<Scene::WorldTransformComponent>(entity, invalidWorld);

    EXPECT_EQ(cameraSystem.ComputeViewMatrix(entityManager, entity), Math::Matrix4x4::Identity());
    EXPECT_EQ(cameraSystem.GetWorldPosition(entityManager, entity), Math::Vector3{});
    const CameraFrustumCorners corners = cameraSystem.ComputeFrustumCorners(entityManager, entity, 1.0F);
    for (const Math::Vector3 corner : corners) {
        EXPECT_EQ(corner, Math::Vector3{});
    }
}
#endif

} // namespace
} // namespace UVE::Render::Tests
