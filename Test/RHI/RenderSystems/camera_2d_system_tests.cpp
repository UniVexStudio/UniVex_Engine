// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "camera_2d_system.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "camera_2d_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "scene_graph.h"

namespace UVE::Render::Tests {
namespace {

constexpr float kEpsilon = 1e-3F;

class Camera2DSystemUVETest : public ::testing::Test {
protected:
    // Entities reach Camera2DSystemUVE only via SceneGraphUVE::AttachTransformUVE (see
    // ICamera2DSystemUVE's doc comment), so every fixture entity goes through the same path a
    // real scene would use, exactly like CameraSystemUVETest's own fixture.
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    Camera2DSystemUVE cameraSystem;

    [[nodiscard]] Scene::EntityUVE MakeCameraEntityUVE(Math::Vector3UVE position,
                                                       Math::QuaternionUVE rotation,
                                                       Scene::Camera2DComponentUVE camera) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE local;
        local.localPosition = position;
        local.localRotation = rotation;
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        sceneGraph.UpdateUVE(entityManager);
        entityManager.AddComponentUVE<Scene::Camera2DComponentUVE>(entity, camera);
        return entity;
    }
};

TEST_F(Camera2DSystemUVETest, ComputeViewMatrixUVE_IdentityRotation_TranslatesWorldPointRelativeToEye) {
    const Scene::EntityUVE camera =
        MakeCameraEntityUVE(Math::Vector3UVE{100.0F, 50.0F, 0.0F}, Math::QuaternionUVE{}, Scene::Camera2DComponentUVE{});

    const Math::Matrix4x4UVE view = cameraSystem.ComputeViewMatrixUVE(entityManager, camera);
    const Math::Vector3UVE viewSpaceOrigin = Math::TransformPointUVE(view, Math::Vector3UVE{0.0F, 0.0F, 0.0F});

    EXPECT_NEAR(viewSpaceOrigin.x, -100.0F, kEpsilon);
    EXPECT_NEAR(viewSpaceOrigin.y, -50.0F, kEpsilon);
    EXPECT_NEAR(viewSpaceOrigin.z, 0.0F, kEpsilon);
}

TEST_F(Camera2DSystemUVETest, ComputeProjectionMatrixUVE_DefaultCamera_MapsTheViewportBoxToClip) {
    const Scene::EntityUVE camera =
        MakeCameraEntityUVE(Math::Vector3UVE{}, Math::QuaternionUVE{}, Scene::Camera2DComponentUVE{});

    const Math::Matrix4x4UVE projection =
        cameraSystem.ComputeProjectionMatrixUVE(entityManager, camera, Math::Vector2UVE{800.0F, 600.0F});

    // Orthographic projections are affine (w stays 1), so TransformPointUVE is exact here —
    // unlike the perspective case, no manual divide is needed.
    EXPECT_NEAR(Math::TransformPointUVE(projection, Math::Vector3UVE{400.0F, 0.0F, 0.0F}).x, 1.0F, kEpsilon);
    EXPECT_NEAR(Math::TransformPointUVE(projection, Math::Vector3UVE{-400.0F, 0.0F, 0.0F}).x, -1.0F, kEpsilon);
    EXPECT_NEAR(Math::TransformPointUVE(projection, Math::Vector3UVE{0.0F, 300.0F, 0.0F}).y, 1.0F, kEpsilon);
    EXPECT_NEAR(Math::TransformPointUVE(projection, Math::Vector3UVE{0.0F, -300.0F, 0.0F}).y, -1.0F, kEpsilon);
    EXPECT_NEAR(Math::TransformPointUVE(projection, Math::Vector3UVE{0.0F, 0.0F, 0.0F}).z, 0.5F, kEpsilon);
}

TEST_F(Camera2DSystemUVETest, ComputeProjectionMatrixUVE_DoubledZoom_HalvesTheVisibleExtents) {
    Scene::Camera2DComponentUVE zoomed{};
    zoomed.zoom = Math::Vector2UVE{2.0F, 2.0F};
    const Scene::EntityUVE camera = MakeCameraEntityUVE(Math::Vector3UVE{}, Math::QuaternionUVE{}, zoomed);

    const Math::Matrix4x4UVE projection =
        cameraSystem.ComputeProjectionMatrixUVE(entityManager, camera, Math::Vector2UVE{800.0F, 600.0F});

    EXPECT_NEAR(Math::TransformPointUVE(projection, Math::Vector3UVE{200.0F, 0.0F, 0.0F}).x, 1.0F, kEpsilon);
    EXPECT_NEAR(Math::TransformPointUVE(projection, Math::Vector3UVE{0.0F, 150.0F, 0.0F}).y, 1.0F, kEpsilon);
}

TEST_F(Camera2DSystemUVETest, ComputeProjectionMatrixUVE_Offset_ShiftsTheViewCenter) {
    Scene::Camera2DComponentUVE shifted{};
    shifted.offset = Math::Vector2UVE{100.0F, 0.0F};
    const Scene::EntityUVE camera = MakeCameraEntityUVE(Math::Vector3UVE{}, Math::QuaternionUVE{}, shifted);

    const Math::Matrix4x4UVE projection =
        cameraSystem.ComputeProjectionMatrixUVE(entityManager, camera, Math::Vector2UVE{800.0F, 600.0F});

    EXPECT_NEAR(Math::TransformPointUVE(projection, Math::Vector3UVE{100.0F, 0.0F, 0.0F}).x, 0.0F, kEpsilon);
    EXPECT_NEAR(Math::TransformPointUVE(projection, Math::Vector3UVE{500.0F, 0.0F, 0.0F}).x, 1.0F, kEpsilon);
}

TEST_F(Camera2DSystemUVETest, ComputeViewProjectionUVE_CombinesViewAndProjection) {
    const Scene::EntityUVE camera =
        MakeCameraEntityUVE(Math::Vector3UVE{100.0F, 0.0F, 0.0F}, Math::QuaternionUVE{}, Scene::Camera2DComponentUVE{});

    const Math::Matrix4x4UVE viewProjection =
        cameraSystem.ComputeViewProjectionUVE(entityManager, camera, Math::Vector2UVE{800.0F, 600.0F});
    const Math::Vector3UVE clipOrigin =
        Math::TransformPointUVE(viewProjection, Math::Vector3UVE{0.0F, 0.0F, 0.0F});

    // The world origin sits 100 units left of the eye in an 800-wide view: a quarter left.
    EXPECT_NEAR(clipOrigin.x, -0.25F, kEpsilon);
    EXPECT_NEAR(clipOrigin.y, 0.0F, kEpsilon);
}

TEST_F(Camera2DSystemUVETest, ComputeProjectionMatrixUVE_InvalidViewportSize_Asserts) {
    const Scene::EntityUVE entity =
        MakeCameraEntityUVE(Math::Vector3UVE{}, Math::QuaternionUVE{}, Scene::Camera2DComponentUVE{});
    EXPECT_DEATH({ static_cast<void>(cameraSystem.ComputeProjectionMatrixUVE(
                       entityManager, entity, Math::Vector2UVE{0.0F, 600.0F})); }, "");
    EXPECT_DEATH({ static_cast<void>(cameraSystem.ComputeProjectionMatrixUVE(
                       entityManager, entity, Math::Vector2UVE{800.0F, std::numeric_limits<float>::quiet_NaN()})); }, "");
}

TEST_F(Camera2DSystemUVETest, ComputeProjectionMatrixUVE_InvalidCameraParameters_Asserts) {
    Scene::Camera2DComponentUVE invalid{};
    invalid.zoom = Math::Vector2UVE{0.0F, 1.0F};
    const Scene::EntityUVE entity = MakeCameraEntityUVE(Math::Vector3UVE{}, Math::QuaternionUVE{}, invalid);
    EXPECT_DEATH({ static_cast<void>(cameraSystem.ComputeProjectionMatrixUVE(
                       entityManager, entity, Math::Vector2UVE{800.0F, 600.0F})); }, "");
}

} // namespace
} // namespace UVE::Render::Tests
