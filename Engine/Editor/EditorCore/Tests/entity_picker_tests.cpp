// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// Click-to-select picking. The cases that matter are not "does a ray hit a box" - they are the
// ones that decide whether the feature feels right: that the exact geometry test actually earns
// its keep over the bounds test, that a nearer object wins, and that the viewport's own hidden
// entities stay unselectable.

#include <cmath>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "integration/entity_picker.h"
#include "integration/selection_outline_geometry.h"

#include "univex/camera/orbit_camera.h"

#include "editor_internal_entity_component.h"
#include "primitive_mesh_component.h"
#include "visibility_component.h"
#include "primitive_geometry.h"
#include "transform_component.h"
#include "engine_core.h"
#include "ray.h"

namespace {

using namespace UVE;
using univex::integration::EntityPickResult;
using univex::integration::PickEntityAlongRay;

[[nodiscard]] Core::EngineConfig MakePickerTestConfig() {
    Core::EngineConfig config{};
    config.headless = true;
    config.logFilePath = "uve_entity_picker_tests.log";
    config.settingsFilePath = "uve_entity_picker_tests_settings.json";
    config.assetDatabaseFilePath = "uve_entity_picker_tests_assets.json";
    config.saveDirectoryPath = "uve_entity_picker_tests_saves";
    config.shaderCachePath = "uve_entity_picker_tests_shader_cache";
    config.shaderSourceRealDirectory =
        ::UVE::Tests::RepositoryRoot() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefix = "shaders";
    return config;
}

/// Creates a primitive at `position` with uniform `scale`, attached through the scene graph so its
/// WorldTransformComponent is real and clean - a dirty transform is deliberately unpickable, so
/// a test that skipped the graph would silently test nothing.
[[nodiscard]] Scene::Entity SpawnPrimitive(Core::EngineServices& services,
                                                 const Scene::PrimitiveMeshKind kind,
                                                 const Math::Vector3 position, const float scale) {
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    const Scene::Entity entity = entityManager.CreateEntity();

    Scene::TransformComponent transform{};
    transform.localPosition = position;
    transform.localScale = Math::Vector3{scale, scale, scale};
    services.GetSceneGraph().AttachTransform(entityManager, entity, transform);

    Scene::PrimitiveMeshComponent primitive{};
    primitive.kind = kind;
    static_cast<void>(entityManager.AddComponent<Scene::PrimitiveMeshComponent>(entity, primitive));

    services.GetSceneGraph().Update(entityManager);
    return entity;
}

class EntityPickerTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_engine = std::make_unique<Core::EngineCore>(MakePickerTestConfig());
        m_engine->Init();
        ASSERT_TRUE(m_engine->Load());
    }

    [[nodiscard]] Core::EngineServices& Services() { return m_engine->GetServices(); }
    [[nodiscard]] Scene::IEntityManager& Entities() { return Services().GetEntityManager(); }

    std::unique_ptr<Core::EngineCore> m_engine;
};

TEST_F(EntityPickerTest, RayDownTheAxis_SelectsTheCubeAtTheDistanceItActuallySits) {
    const Scene::Entity cube =
        SpawnPrimitive(Services(), Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.0F, 0.0F, 0.0F}, 2.0F);

    // Cube geometry is a 0.5 half extent, so a scale of 2 puts its near face at z = 1.
    const Math::Ray ray{Math::Vector3{0.0F, 0.0F, 10.0F}, Math::Vector3{0.0F, 0.0F, -1.0F}};
    const EntityPickResult result = PickEntityAlongRay(Entities(), ray, 1000.0F);

    ASSERT_TRUE(result.hit);
    EXPECT_EQ(result.entity, cube);
    EXPECT_NEAR(result.distance, 9.0F, 1e-3F);
}

TEST_F(EntityPickerTest, NearerObjectWins_EvenWhenBothAreUnderTheCursor) {
    const Scene::Entity farCube =
        SpawnPrimitive(Services(), Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.0F, 0.0F, -6.0F}, 1.0F);
    const Scene::Entity nearCube =
        SpawnPrimitive(Services(), Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.0F, 0.0F, 0.0F}, 1.0F);

    const Math::Ray ray{Math::Vector3{0.0F, 0.0F, 10.0F}, Math::Vector3{0.0F, 0.0F, -1.0F}};
    const EntityPickResult result = PickEntityAlongRay(Entities(), ray, 1000.0F);

    ASSERT_TRUE(result.hit);
    EXPECT_EQ(result.entity, nearCube);
    EXPECT_NE(result.entity, farCube);
}

// The case the narrow phase exists for. A sphere's world AABB is a cube, so a ray through the
// corner of that box passes the bounds test and misses the sphere itself. Selecting on bounds
// alone would hand the user an object they can plainly see they did not click.
TEST_F(EntityPickerTest, RayThroughTheCornerGapOfASphere_HitsItsBoundsButSelectsNothing) {
    const Scene::Entity sphere = SpawnPrimitive(
        Services(), Scene::PrimitiveMeshKind::UVSphere, Math::Vector3{0.0F, 0.0F, 0.0F}, 2.0F);
    ASSERT_NE(sphere, Scene::kInvalidEntity);

    // Radius is 0.5 * 2 = 1. A ray at x = y = 0.9 is inside the AABB's corner (|x|, |y| <= 1) but
    // at a radial distance of ~1.27 from the axis, well outside the sphere.
    const Math::Ray cornerRay{Math::Vector3{0.9F, 0.9F, 10.0F}, Math::Vector3{0.0F, 0.0F, -1.0F}};
    EXPECT_FALSE(PickEntityAlongRay(Entities(), cornerRay, 1000.0F).hit);

    // ... and the very same sphere is still selectable straight through its middle, so the test
    // above is proving the corner gap and not merely that the sphere is unpickable.
    const Math::Ray centreRay{Math::Vector3{0.0F, 0.0F, 10.0F}, Math::Vector3{0.0F, 0.0F, -1.0F}};
    EXPECT_TRUE(PickEntityAlongRay(Entities(), centreRay, 1000.0F).hit);
}

TEST_F(EntityPickerTest, EmptySpaceSelectsNothing) {
    static_cast<void>(SpawnPrimitive(Services(), Scene::PrimitiveMeshKind::Cube,
                                        Math::Vector3{0.0F, 0.0F, 0.0F}, 1.0F));

    const Math::Ray ray{Math::Vector3{0.0F, 50.0F, 10.0F}, Math::Vector3{0.0F, 0.0F, -1.0F}};
    EXPECT_FALSE(PickEntityAlongRay(Entities(), ray, 1000.0F).hit);
}

TEST_F(EntityPickerTest, MaxDistanceRejectsAnObjectBeyondIt) {
    static_cast<void>(SpawnPrimitive(Services(), Scene::PrimitiveMeshKind::Cube,
                                        Math::Vector3{0.0F, 0.0F, 0.0F}, 1.0F));

    const Math::Ray ray{Math::Vector3{0.0F, 0.0F, 10.0F}, Math::Vector3{0.0F, 0.0F, -1.0F}};
    EXPECT_TRUE(PickEntityAlongRay(Entities(), ray, 1000.0F).hit);
    EXPECT_FALSE(PickEntityAlongRay(Entities(), ray, 5.0F).hit);
}

// The viewport owns hidden entities of its own (the camera proxy, the headlight). They are never
// serialized and never shown in the Scene panel, so clicking through where one sits must not
// select it either.
TEST_F(EntityPickerTest, EditorInternalEntitiesAreNeverSelectable) {
    const Scene::Entity hidden =
        SpawnPrimitive(Services(), Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.0F, 0.0F, 0.0F}, 1.0F);
    static_cast<void>(Entities().AddComponent<Scene::EditorInternalEntityComponent>(
        hidden, Scene::EditorInternalEntityComponent{}));

    const Math::Ray ray{Math::Vector3{0.0F, 0.0F, 10.0F}, Math::Vector3{0.0F, 0.0F, -1.0F}};
    EXPECT_FALSE(PickEntityAlongRay(Entities(), ray, 1000.0F).hit);
}

// Non-uniform scale is handled by transforming the ray into local space, not by rescaling the
// geometry - so a stretched cube must be pickable across its stretched extent and miss just past
// it. This is the property that breaks first if the local/world conversion is wrong.
TEST_F(EntityPickerTest, NonUniformScaleIsPickedAcrossItsRealExtent) {
    Scene::IEntityManager& entityManager = Entities();
    const Scene::Entity entity = entityManager.CreateEntity();

    Scene::TransformComponent transform{};
    transform.localScale = Math::Vector3{8.0F, 1.0F, 1.0F}; // half extents become 4, 0.5, 0.5
    Services().GetSceneGraph().AttachTransform(entityManager, entity, transform);
    Scene::PrimitiveMeshComponent primitive{};
    primitive.kind = Scene::PrimitiveMeshKind::Cube;
    static_cast<void>(entityManager.AddComponent<Scene::PrimitiveMeshComponent>(entity, primitive));
    Services().GetSceneGraph().Update(entityManager);

    const Math::Ray inside{Math::Vector3{3.5F, 0.0F, 10.0F}, Math::Vector3{0.0F, 0.0F, -1.0F}};
    const Math::Ray outside{Math::Vector3{4.5F, 0.0F, 10.0F}, Math::Vector3{0.0F, 0.0F, -1.0F}};

    EXPECT_TRUE(PickEntityAlongRay(entityManager, inside, 1000.0F).hit);
    EXPECT_FALSE(PickEntityAlongRay(entityManager, outside, 1000.0F).hit);
}

// The cursor ray must actually point at what is drawn under the cursor. Rather than assert
// hand-derived numbers, this picks an object through the centre pixel and confirms it is the one
// the camera is framing - the round trip that would break if the NDC mapping or the Y flip were
// wrong.
TEST_F(EntityPickerTest, CursorRayThroughTheCentrePixelSelectsWhatTheCameraIsLookingAt) {
    const Scene::Entity framed =
        SpawnPrimitive(Services(), Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.0F, 0.0F, 0.0F}, 1.0F);
    static_cast<void>(SpawnPrimitive(Services(), Scene::PrimitiveMeshKind::Cube,
                                        Math::Vector3{40.0F, 0.0F, 0.0F}, 1.0F));

    univex::camera::OrbitCamera camera; // default orbit target is the origin, where `framed` sits
    camera.SetDistance(12.0F);

    constexpr int kWidth = 1280;
    constexpr int kHeight = 800;
    const EntityPickResult centre = univex::integration::PickEntityAtPixel(
        Entities(), camera, kWidth, kHeight, static_cast<float>(kWidth) * 0.5F,
        static_cast<float>(kHeight) * 0.5F);

    ASSERT_TRUE(centre.hit);
    EXPECT_EQ(centre.entity, framed);

    // A corner pixel looks past everything in this scene.
    EXPECT_FALSE(univex::integration::PickEntityAtPixel(Entities(), camera, kWidth, kHeight, 2.0F, 2.0F).hit);
}

TEST_F(EntityPickerTest, OrthographicCursorRaySelectsDistantObjectsWithinSymmetricOrthoDepth) {
    const Scene::Entity distantCube =
        SpawnPrimitive(Services(), Scene::PrimitiveMeshKind::Cube, Math::Vector3{-120.0F, 0.0F, 0.0F}, 2.0F);

    univex::camera::OrbitCamera camera;
    camera.SetYawPitch(0.0F, 0.0F);
    camera.SetDistance(12.0F);
    camera.SetOrthographic(true);

    constexpr int kWidth = 1280;
    constexpr int kHeight = 800;
    const EntityPickResult centre = univex::integration::PickEntityAtPixel(
        Entities(), camera, kWidth, kHeight, static_cast<float>(kWidth) * 0.5F,
        static_cast<float>(kHeight) * 0.5F);

    ASSERT_TRUE(centre.hit);
    EXPECT_EQ(centre.entity, distantCube);
}

// ProjectWorldPointToPixel is BuildCursorRay's inverse: what the camera is framing (its own
// orbit target) must project to the centre pixel, the same point CursorRayThroughTheCentrePixel...
// above picks through.
TEST(EntityScreenProjectorTest, PointAtCameraTargetProjectsToViewportCentre) {
    univex::camera::OrbitCamera camera; // default orbit target is the origin
    camera.SetDistance(12.0F);

    constexpr int kWidth = 1280;
    constexpr int kHeight = 800;
    float pixelX = 0.0F;
    float pixelY = 0.0F;
    ASSERT_TRUE(univex::integration::ProjectWorldPointToPixel(
        camera, kWidth, kHeight, Math::Vector3{0.0F, 0.0F, 0.0F}, pixelX, pixelY));

    EXPECT_NEAR(pixelX, static_cast<float>(kWidth) * 0.5F, 0.5F);
    EXPECT_NEAR(pixelY, static_cast<float>(kHeight) * 0.5F, 0.5F);
}

TEST(EntityScreenProjectorTest, PointBehindCameraReturnsFalse) {
    univex::camera::OrbitCamera camera; // eye sits behind the target along -forward
    camera.SetDistance(12.0F);

    // Twice the eye's own distance further back along the same axis is guaranteed behind it.
    const Math::Vector3 behindCamera =
        Math::Vector3{camera.Eye().x, camera.Eye().y, camera.Eye().z} * 3.0F;

    float pixelX = 0.0F;
    float pixelY = 0.0F;
    EXPECT_FALSE(univex::integration::ProjectWorldPointToPixel(camera, 1280, 800, behindCamera,
                                                                   pixelX, pixelY));
}

// Locks ProjectWorldPointToPixel and BuildCursorRay to the same coordinate convention: a
// point projected to a pixel, then re-unprojected as a cursor ray through that same pixel, must
// have the ray pass back through (close to) the original point.
TEST(EntityScreenProjectorTest, ProjectedPixelRoundTripsThroughBuildCursorRay) {
    univex::camera::OrbitCamera camera;
    camera.SetDistance(12.0F);

    constexpr int kWidth = 1280;
    constexpr int kHeight = 800;
    const Math::Vector3 original{1.5F, 0.75F, -0.5F};

    float pixelX = 0.0F;
    float pixelY = 0.0F;
    ASSERT_TRUE(univex::integration::ProjectWorldPointToPixel(camera, kWidth, kHeight, original,
                                                                  pixelX, pixelY));

    const Math::Ray ray =
        univex::integration::BuildCursorRay(camera, kWidth, kHeight, pixelX, pixelY);
    const Math::Vector3 toOriginal = original - ray.origin;
    const float projectionLength = toOriginal.x * ray.direction.x + toOriginal.y * ray.direction.y +
                                   toOriginal.z * ray.direction.z;
    const Math::Vector3 closestPointOnRay =
        ray.origin + Math::Vector3{ray.direction.x * projectionLength,
                                      ray.direction.y * projectionLength,
                                      ray.direction.z * projectionLength};
    const Math::Vector3 delta = closestPointOnRay - original;
    const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    EXPECT_LT(distance, 0.01F);
}

// ---- selection outline geometry --------------------------------------------------------------

using univex::integration::CollectSelectionOutlineTriangles;
using univex::render::SelectionOutlineVertex;

[[nodiscard]] std::size_t CubeIndexCount() {
    const std::size_t count = Render::GetPrimitiveGeometry(Scene::PrimitiveMeshKind::Cube).indices.size();
    return count - (count % 3U);
}

TEST_F(EntityPickerTest, SelectionOutline_IsTheSelectedMeshInWorldSpaceAtFullStrength) {
    const Scene::Entity cube =
        SpawnPrimitive(Services(), Scene::PrimitiveMeshKind::Cube, Math::Vector3{3.0F, 0.0F, 0.0F}, 2.0F);
    static_cast<void>(
        SpawnPrimitive(Services(), Scene::PrimitiveMeshKind::Cube, Math::Vector3{-5.0F, 0.0F, 0.0F}, 1.0F));

    EXPECT_TRUE(CollectSelectionOutlineTriangles(Entities(), {}, Scene::kInvalidEntity).empty());

    const std::vector<SelectionOutlineVertex> triangles = CollectSelectionOutlineTriangles(Entities(), {cube}, cube);
    ASSERT_EQ(triangles.size(), CubeIndexCount());
    for (const SelectionOutlineVertex& vertex : triangles) {
        // Half extent 0.5 scaled by 2 around x = 3: the unselected cube at x = -5 contributes nothing.
        EXPECT_GE(vertex.x, 2.0F - 1e-4F);
        EXPECT_LE(vertex.x, 4.0F + 1e-4F);
        EXPECT_FLOAT_EQ(vertex.weight, univex::render::kSelectionOutlineActiveWeight);
    }
}

TEST_F(EntityPickerTest, SelectionOutline_TakesMeshesBelowASelectedObjectAndDimsTheRestOfAMultiSelection) {
    Core::EngineServices& services = Services();
    Scene::IEntityManager& entityManager = Entities();
    // A group object with no mesh of its own, holding one cube.
    const Scene::Entity group = entityManager.CreateEntity();
    services.GetSceneGraph().AttachTransform(entityManager, group, Scene::TransformComponent{});
    const Scene::Entity child =
        SpawnPrimitive(services, Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.0F, 0.0F, 0.0F}, 1.0F);
    services.GetSceneGraph().SetParent(entityManager, child, group);
    const Scene::Entity other =
        SpawnPrimitive(services, Scene::PrimitiveMeshKind::Cube, Math::Vector3{5.0F, 0.0F, 0.0F}, 1.0F);
    services.GetSceneGraph().Update(entityManager);

    // Selecting the group outlines what is in it.
    EXPECT_EQ(CollectSelectionOutlineTriangles(entityManager, {group}, group).size(), CubeIndexCount());

    // Group active, the other cube also selected: two cubes, the second dimmer.
    const std::vector<SelectionOutlineVertex> both =
        CollectSelectionOutlineTriangles(entityManager, {group, other}, group);
    ASSERT_EQ(both.size(), CubeIndexCount() * 2U);
    std::size_t dim = 0U;
    for (const SelectionOutlineVertex& vertex : both) {
        if (vertex.weight == univex::render::kSelectionOutlineOtherWeight) {
            ++dim;
            EXPECT_GT(vertex.x, 4.0F);
        }
    }
    EXPECT_EQ(dim, CubeIndexCount());

    // A mesh reached both as the active object's child and as a dimmer selection of its own keeps
    // the stronger weight, and is listed once.
    const std::vector<SelectionOutlineVertex> overlap =
        CollectSelectionOutlineTriangles(entityManager, {child, group}, group);
    ASSERT_EQ(overlap.size(), CubeIndexCount());
    for (const SelectionOutlineVertex& vertex : overlap) {
        EXPECT_FLOAT_EQ(vertex.weight, univex::render::kSelectionOutlineActiveWeight);
    }
}

TEST_F(EntityPickerTest, SelectionOutline_SkipsHiddenAndEditorInternalMeshes) {
    Core::EngineServices& services = Services();
    Scene::IEntityManager& entityManager = Entities();
    const Scene::Entity hidden =
        SpawnPrimitive(services, Scene::PrimitiveMeshKind::Cube, Math::Vector3{0.0F, 0.0F, 0.0F}, 1.0F);
    Scene::VisibilityComponent visibility{};
    visibility.visible = false;
    static_cast<void>(entityManager.AddComponent<Scene::VisibilityComponent>(hidden, visibility));
    const Scene::Entity internal =
        SpawnPrimitive(services, Scene::PrimitiveMeshKind::Cube, Math::Vector3{2.0F, 0.0F, 0.0F}, 1.0F);
    static_cast<void>(entityManager.AddComponent<Scene::EditorInternalEntityComponent>(
        internal, Scene::EditorInternalEntityComponent{}));
    services.GetSceneGraph().Update(entityManager);
    ASSERT_FALSE(entityManager.GetComponent<Scene::VisibilityComponent>(hidden).visibleInHierarchy);

    EXPECT_TRUE(CollectSelectionOutlineTriangles(entityManager, {hidden, internal}, hidden).empty());
}

} // namespace
