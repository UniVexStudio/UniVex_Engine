// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// Click-to-select picking. The cases that matter are not "does a ray hit a box" - they are the
// ones that decide whether the feature feels right: that the exact geometry test actually earns
// its keep over the bounds test, that a nearer object wins, and that the viewport's own hidden
// entities stay unselectable.

#include <cmath>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include "integration/EntityPicker.h"
#include "integration/SelectionOutlineGeometry.h"

#include "univex/camera/OrbitCamera.h"

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/render_systems/primitive_geometry_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/core/engine_core_uve.h"
#include "uve/math/ray_uve.h"

namespace {

using namespace UVE;
using univex::integration::EntityPickResultUVE;
using univex::integration::PickEntityAlongRayUVE;

[[nodiscard]] Core::EngineConfigUVE MakePickerTestConfigUVE() {
    Core::EngineConfigUVE config{};
    config.headlessUVE = true;
    config.logFilePath = "uve_entity_picker_tests.log";
    config.settingsFilePath = "uve_entity_picker_tests_settings.json";
    config.assetDatabaseFilePath = "uve_entity_picker_tests_assets.json";
    config.saveDirectoryPath = "uve_entity_picker_tests_saves";
    config.shaderCachePath = "uve_entity_picker_tests_shader_cache";
    config.shaderSourceRealDirectoryUVE =
        ::UVE::Tests::RepositoryRootUVE() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefixUVE = "shaders";
    return config;
}

/// Creates a primitive at `position` with uniform `scale`, attached through the scene graph so its
/// WorldTransformComponentUVE is real and clean - a dirty transform is deliberately unpickable, so
/// a test that skipped the graph would silently test nothing.
[[nodiscard]] Scene::EntityUVE SpawnPrimitiveUVE(Core::EngineServicesUVE& services,
                                                 const Scene::PrimitiveMeshKindUVE kind,
                                                 const Math::Vector3UVE position, const float scale) {
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();

    Scene::TransformComponentUVE transform{};
    transform.localPosition = position;
    transform.localScale = Math::Vector3UVE{scale, scale, scale};
    services.GetSceneGraphUVE().AttachTransformUVE(entityManager, entity, transform);

    Scene::PrimitiveMeshComponentUVE primitive{};
    primitive.kind = kind;
    static_cast<void>(entityManager.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity, primitive));

    services.GetSceneGraphUVE().UpdateUVE(entityManager);
    return entity;
}

class EntityPickerUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        m_engine = std::make_unique<Core::EngineCoreUVE>(MakePickerTestConfigUVE());
        m_engine->Init();
        ASSERT_TRUE(m_engine->Load());
    }

    [[nodiscard]] Core::EngineServicesUVE& ServicesUVE() { return m_engine->GetServicesUVE(); }
    [[nodiscard]] Scene::IEntityManagerUVE& EntitiesUVE() { return ServicesUVE().GetEntityManagerUVE(); }

    std::unique_ptr<Core::EngineCoreUVE> m_engine;
};

TEST_F(EntityPickerUVETest, RayDownTheAxis_SelectsTheCubeAtTheDistanceItActuallySits) {
    const Scene::EntityUVE cube =
        SpawnPrimitiveUVE(ServicesUVE(), Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.0F, 0.0F, 0.0F}, 2.0F);

    // Cube geometry is a 0.5 half extent, so a scale of 2 puts its near face at z = 1.
    const Math::RayUVE ray{Math::Vector3UVE{0.0F, 0.0F, 10.0F}, Math::Vector3UVE{0.0F, 0.0F, -1.0F}};
    const EntityPickResultUVE result = PickEntityAlongRayUVE(EntitiesUVE(), ray, 1000.0F);

    ASSERT_TRUE(result.hit);
    EXPECT_EQ(result.entity, cube);
    EXPECT_NEAR(result.distance, 9.0F, 1e-3F);
}

TEST_F(EntityPickerUVETest, NearerObjectWins_EvenWhenBothAreUnderTheCursor) {
    const Scene::EntityUVE farCube =
        SpawnPrimitiveUVE(ServicesUVE(), Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.0F, 0.0F, -6.0F}, 1.0F);
    const Scene::EntityUVE nearCube =
        SpawnPrimitiveUVE(ServicesUVE(), Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.0F, 0.0F, 0.0F}, 1.0F);

    const Math::RayUVE ray{Math::Vector3UVE{0.0F, 0.0F, 10.0F}, Math::Vector3UVE{0.0F, 0.0F, -1.0F}};
    const EntityPickResultUVE result = PickEntityAlongRayUVE(EntitiesUVE(), ray, 1000.0F);

    ASSERT_TRUE(result.hit);
    EXPECT_EQ(result.entity, nearCube);
    EXPECT_NE(result.entity, farCube);
}

// The case the narrow phase exists for. A sphere's world AABB is a cube, so a ray through the
// corner of that box passes the bounds test and misses the sphere itself. Selecting on bounds
// alone would hand the user an object they can plainly see they did not click.
TEST_F(EntityPickerUVETest, RayThroughTheCornerGapOfASphere_HitsItsBoundsButSelectsNothing) {
    const Scene::EntityUVE sphere = SpawnPrimitiveUVE(
        ServicesUVE(), Scene::PrimitiveMeshKindUVE::UVSphere, Math::Vector3UVE{0.0F, 0.0F, 0.0F}, 2.0F);
    ASSERT_NE(sphere, Scene::kInvalidEntityUVE);

    // Radius is 0.5 * 2 = 1. A ray at x = y = 0.9 is inside the AABB's corner (|x|, |y| <= 1) but
    // at a radial distance of ~1.27 from the axis, well outside the sphere.
    const Math::RayUVE cornerRay{Math::Vector3UVE{0.9F, 0.9F, 10.0F}, Math::Vector3UVE{0.0F, 0.0F, -1.0F}};
    EXPECT_FALSE(PickEntityAlongRayUVE(EntitiesUVE(), cornerRay, 1000.0F).hit);

    // ... and the very same sphere is still selectable straight through its middle, so the test
    // above is proving the corner gap and not merely that the sphere is unpickable.
    const Math::RayUVE centreRay{Math::Vector3UVE{0.0F, 0.0F, 10.0F}, Math::Vector3UVE{0.0F, 0.0F, -1.0F}};
    EXPECT_TRUE(PickEntityAlongRayUVE(EntitiesUVE(), centreRay, 1000.0F).hit);
}

TEST_F(EntityPickerUVETest, EmptySpaceSelectsNothing) {
    static_cast<void>(SpawnPrimitiveUVE(ServicesUVE(), Scene::PrimitiveMeshKindUVE::Cube,
                                        Math::Vector3UVE{0.0F, 0.0F, 0.0F}, 1.0F));

    const Math::RayUVE ray{Math::Vector3UVE{0.0F, 50.0F, 10.0F}, Math::Vector3UVE{0.0F, 0.0F, -1.0F}};
    EXPECT_FALSE(PickEntityAlongRayUVE(EntitiesUVE(), ray, 1000.0F).hit);
}

TEST_F(EntityPickerUVETest, MaxDistanceRejectsAnObjectBeyondIt) {
    static_cast<void>(SpawnPrimitiveUVE(ServicesUVE(), Scene::PrimitiveMeshKindUVE::Cube,
                                        Math::Vector3UVE{0.0F, 0.0F, 0.0F}, 1.0F));

    const Math::RayUVE ray{Math::Vector3UVE{0.0F, 0.0F, 10.0F}, Math::Vector3UVE{0.0F, 0.0F, -1.0F}};
    EXPECT_TRUE(PickEntityAlongRayUVE(EntitiesUVE(), ray, 1000.0F).hit);
    EXPECT_FALSE(PickEntityAlongRayUVE(EntitiesUVE(), ray, 5.0F).hit);
}

// The viewport owns hidden entities of its own (the camera proxy, the headlight). They are never
// serialized and never shown in the Scene panel, so clicking through where one sits must not
// select it either.
TEST_F(EntityPickerUVETest, EditorInternalEntitiesAreNeverSelectable) {
    const Scene::EntityUVE hidden =
        SpawnPrimitiveUVE(ServicesUVE(), Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.0F, 0.0F, 0.0F}, 1.0F);
    static_cast<void>(EntitiesUVE().AddComponentUVE<Scene::EditorInternalEntityComponentUVE>(
        hidden, Scene::EditorInternalEntityComponentUVE{}));

    const Math::RayUVE ray{Math::Vector3UVE{0.0F, 0.0F, 10.0F}, Math::Vector3UVE{0.0F, 0.0F, -1.0F}};
    EXPECT_FALSE(PickEntityAlongRayUVE(EntitiesUVE(), ray, 1000.0F).hit);
}

// Non-uniform scale is handled by transforming the ray into local space, not by rescaling the
// geometry - so a stretched cube must be pickable across its stretched extent and miss just past
// it. This is the property that breaks first if the local/world conversion is wrong.
TEST_F(EntityPickerUVETest, NonUniformScaleIsPickedAcrossItsRealExtent) {
    Scene::IEntityManagerUVE& entityManager = EntitiesUVE();
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();

    Scene::TransformComponentUVE transform{};
    transform.localScale = Math::Vector3UVE{8.0F, 1.0F, 1.0F}; // half extents become 4, 0.5, 0.5
    ServicesUVE().GetSceneGraphUVE().AttachTransformUVE(entityManager, entity, transform);
    Scene::PrimitiveMeshComponentUVE primitive{};
    primitive.kind = Scene::PrimitiveMeshKindUVE::Cube;
    static_cast<void>(entityManager.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity, primitive));
    ServicesUVE().GetSceneGraphUVE().UpdateUVE(entityManager);

    const Math::RayUVE inside{Math::Vector3UVE{3.5F, 0.0F, 10.0F}, Math::Vector3UVE{0.0F, 0.0F, -1.0F}};
    const Math::RayUVE outside{Math::Vector3UVE{4.5F, 0.0F, 10.0F}, Math::Vector3UVE{0.0F, 0.0F, -1.0F}};

    EXPECT_TRUE(PickEntityAlongRayUVE(entityManager, inside, 1000.0F).hit);
    EXPECT_FALSE(PickEntityAlongRayUVE(entityManager, outside, 1000.0F).hit);
}

// The cursor ray must actually point at what is drawn under the cursor. Rather than assert
// hand-derived numbers, this picks an object through the centre pixel and confirms it is the one
// the camera is framing - the round trip that would break if the NDC mapping or the Y flip were
// wrong.
TEST_F(EntityPickerUVETest, CursorRayThroughTheCentrePixelSelectsWhatTheCameraIsLookingAt) {
    const Scene::EntityUVE framed =
        SpawnPrimitiveUVE(ServicesUVE(), Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.0F, 0.0F, 0.0F}, 1.0F);
    static_cast<void>(SpawnPrimitiveUVE(ServicesUVE(), Scene::PrimitiveMeshKindUVE::Cube,
                                        Math::Vector3UVE{40.0F, 0.0F, 0.0F}, 1.0F));

    univex::camera::OrbitCamera camera; // default orbit target is the origin, where `framed` sits
    camera.SetDistance(12.0F);

    constexpr int kWidth = 1280;
    constexpr int kHeight = 800;
    const EntityPickResultUVE centre = univex::integration::PickEntityAtPixelUVE(
        EntitiesUVE(), camera, kWidth, kHeight, static_cast<float>(kWidth) * 0.5F,
        static_cast<float>(kHeight) * 0.5F);

    ASSERT_TRUE(centre.hit);
    EXPECT_EQ(centre.entity, framed);

    // A corner pixel looks past everything in this scene.
    EXPECT_FALSE(univex::integration::PickEntityAtPixelUVE(EntitiesUVE(), camera, kWidth, kHeight, 2.0F, 2.0F).hit);
}

// ProjectWorldPointToPixelUVE is BuildCursorRayUVE's inverse: what the camera is framing (its own
// orbit target) must project to the centre pixel, the same point CursorRayThroughTheCentrePixel...
// above picks through.
TEST(EntityScreenProjectorUVETest, PointAtCameraTargetProjectsToViewportCentre) {
    univex::camera::OrbitCamera camera; // default orbit target is the origin
    camera.SetDistance(12.0F);

    constexpr int kWidth = 1280;
    constexpr int kHeight = 800;
    float pixelX = 0.0F;
    float pixelY = 0.0F;
    ASSERT_TRUE(univex::integration::ProjectWorldPointToPixelUVE(
        camera, kWidth, kHeight, Math::Vector3UVE{0.0F, 0.0F, 0.0F}, pixelX, pixelY));

    EXPECT_NEAR(pixelX, static_cast<float>(kWidth) * 0.5F, 0.5F);
    EXPECT_NEAR(pixelY, static_cast<float>(kHeight) * 0.5F, 0.5F);
}

TEST(EntityScreenProjectorUVETest, PointBehindCameraReturnsFalse) {
    univex::camera::OrbitCamera camera; // eye sits behind the target along -forward
    camera.SetDistance(12.0F);

    // Twice the eye's own distance further back along the same axis is guaranteed behind it.
    const Math::Vector3UVE behindCamera =
        Math::Vector3UVE{camera.Eye().x, camera.Eye().y, camera.Eye().z} * 3.0F;

    float pixelX = 0.0F;
    float pixelY = 0.0F;
    EXPECT_FALSE(univex::integration::ProjectWorldPointToPixelUVE(camera, 1280, 800, behindCamera,
                                                                   pixelX, pixelY));
}

// Locks ProjectWorldPointToPixelUVE and BuildCursorRayUVE to the same coordinate convention: a
// point projected to a pixel, then re-unprojected as a cursor ray through that same pixel, must
// have the ray pass back through (close to) the original point.
TEST(EntityScreenProjectorUVETest, ProjectedPixelRoundTripsThroughBuildCursorRayUVE) {
    univex::camera::OrbitCamera camera;
    camera.SetDistance(12.0F);

    constexpr int kWidth = 1280;
    constexpr int kHeight = 800;
    const Math::Vector3UVE original{1.5F, 0.75F, -0.5F};

    float pixelX = 0.0F;
    float pixelY = 0.0F;
    ASSERT_TRUE(univex::integration::ProjectWorldPointToPixelUVE(camera, kWidth, kHeight, original,
                                                                  pixelX, pixelY));

    const Math::RayUVE ray =
        univex::integration::BuildCursorRayUVE(camera, kWidth, kHeight, pixelX, pixelY);
    const Math::Vector3UVE toOriginal = original - ray.origin;
    const float projectionLength = toOriginal.x * ray.direction.x + toOriginal.y * ray.direction.y +
                                   toOriginal.z * ray.direction.z;
    const Math::Vector3UVE closestPointOnRay =
        ray.origin + Math::Vector3UVE{ray.direction.x * projectionLength,
                                      ray.direction.y * projectionLength,
                                      ray.direction.z * projectionLength};
    const Math::Vector3UVE delta = closestPointOnRay - original;
    const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    EXPECT_LT(distance, 0.01F);
}

// ---- selection outline geometry --------------------------------------------------------------

using univex::integration::CollectSelectionOutlineTrianglesUVE;
using univex::render::SelectionOutlineVertex;

[[nodiscard]] std::size_t CubeIndexCountUVE() {
    const std::size_t count = Render::GetPrimitiveGeometryUVE(Scene::PrimitiveMeshKindUVE::Cube).indices.size();
    return count - (count % 3U);
}

TEST_F(EntityPickerUVETest, SelectionOutline_IsTheSelectedMeshInWorldSpaceAtFullStrength) {
    const Scene::EntityUVE cube =
        SpawnPrimitiveUVE(ServicesUVE(), Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{3.0F, 0.0F, 0.0F}, 2.0F);
    static_cast<void>(
        SpawnPrimitiveUVE(ServicesUVE(), Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{-5.0F, 0.0F, 0.0F}, 1.0F));

    EXPECT_TRUE(CollectSelectionOutlineTrianglesUVE(EntitiesUVE(), {}, Scene::kInvalidEntityUVE).empty());

    const std::vector<SelectionOutlineVertex> triangles = CollectSelectionOutlineTrianglesUVE(EntitiesUVE(), {cube}, cube);
    ASSERT_EQ(triangles.size(), CubeIndexCountUVE());
    for (const SelectionOutlineVertex& vertex : triangles) {
        // Half extent 0.5 scaled by 2 around x = 3: the unselected cube at x = -5 contributes nothing.
        EXPECT_GE(vertex.x, 2.0F - 1e-4F);
        EXPECT_LE(vertex.x, 4.0F + 1e-4F);
        EXPECT_FLOAT_EQ(vertex.weight, univex::render::kSelectionOutlineActiveWeight);
    }
}

TEST_F(EntityPickerUVETest, SelectionOutline_TakesMeshesBelowASelectedNodeAndDimsTheRestOfAMultiSelection) {
    Core::EngineServicesUVE& services = ServicesUVE();
    Scene::IEntityManagerUVE& entityManager = EntitiesUVE();
    // A group node with no mesh of its own, holding one cube.
    const Scene::EntityUVE group = entityManager.CreateEntityUVE();
    services.GetSceneGraphUVE().AttachTransformUVE(entityManager, group, Scene::TransformComponentUVE{});
    const Scene::EntityUVE child =
        SpawnPrimitiveUVE(services, Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.0F, 0.0F, 0.0F}, 1.0F);
    services.GetSceneGraphUVE().SetParentUVE(entityManager, child, group);
    const Scene::EntityUVE other =
        SpawnPrimitiveUVE(services, Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{5.0F, 0.0F, 0.0F}, 1.0F);
    services.GetSceneGraphUVE().UpdateUVE(entityManager);

    // Selecting the group outlines what is in it.
    EXPECT_EQ(CollectSelectionOutlineTrianglesUVE(entityManager, {group}, group).size(), CubeIndexCountUVE());

    // Group active, the other cube also selected: two cubes, the second dimmer.
    const std::vector<SelectionOutlineVertex> both =
        CollectSelectionOutlineTrianglesUVE(entityManager, {group, other}, group);
    ASSERT_EQ(both.size(), CubeIndexCountUVE() * 2U);
    std::size_t dim = 0U;
    for (const SelectionOutlineVertex& vertex : both) {
        if (vertex.weight == univex::render::kSelectionOutlineOtherWeight) {
            ++dim;
            EXPECT_GT(vertex.x, 4.0F);
        }
    }
    EXPECT_EQ(dim, CubeIndexCountUVE());

    // A mesh reached both as the active node's child and as a dimmer selection of its own keeps
    // the stronger weight, and is listed once.
    const std::vector<SelectionOutlineVertex> overlap =
        CollectSelectionOutlineTrianglesUVE(entityManager, {child, group}, group);
    ASSERT_EQ(overlap.size(), CubeIndexCountUVE());
    for (const SelectionOutlineVertex& vertex : overlap) {
        EXPECT_FLOAT_EQ(vertex.weight, univex::render::kSelectionOutlineActiveWeight);
    }
}

TEST_F(EntityPickerUVETest, SelectionOutline_SkipsHiddenAndEditorInternalMeshes) {
    Core::EngineServicesUVE& services = ServicesUVE();
    Scene::IEntityManagerUVE& entityManager = EntitiesUVE();
    const Scene::EntityUVE hidden =
        SpawnPrimitiveUVE(services, Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{0.0F, 0.0F, 0.0F}, 1.0F);
    Scene::VisibilityComponentUVE visibility{};
    visibility.visible = false;
    static_cast<void>(entityManager.AddComponentUVE<Scene::VisibilityComponentUVE>(hidden, visibility));
    const Scene::EntityUVE internal =
        SpawnPrimitiveUVE(services, Scene::PrimitiveMeshKindUVE::Cube, Math::Vector3UVE{2.0F, 0.0F, 0.0F}, 1.0F);
    static_cast<void>(entityManager.AddComponentUVE<Scene::EditorInternalEntityComponentUVE>(
        internal, Scene::EditorInternalEntityComponentUVE{}));
    services.GetSceneGraphUVE().UpdateUVE(entityManager);
    ASSERT_FALSE(entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(hidden).visibleInHierarchy);

    EXPECT_TRUE(CollectSelectionOutlineTrianglesUVE(entityManager, {hidden, internal}, hidden).empty());
}

} // namespace
