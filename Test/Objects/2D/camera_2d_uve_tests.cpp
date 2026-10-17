// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <limits>
#include <type_traits>

#include <gtest/gtest.h>

#include "uve/component/camera_2d_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/2d/all_objects_2d_uve.h"

namespace UVE::Scene::Tests {
namespace {

// The 2D camera definition is reachable from the aggregate header, matching the Sprite2D
// guarantee: no 2D kind's home file can be silently dropped without breaking this compile.
static_assert(std::is_class_v<Camera2DObjectDefinitionUVE>); // Camera2D

TEST(Camera2DComponentUVETest, DefaultsAreAValidOneToOneViewUVE) {
    const Camera2DComponentUVE camera{};
    EXPECT_TRUE(IsCamera2DComponentValidUVE(camera));
    EXPECT_FLOAT_EQ(camera.zoom.x, 1.0F);
    EXPECT_FLOAT_EQ(camera.zoom.y, 1.0F);
    EXPECT_FALSE(camera.current);
}

TEST(Camera2DComponentUVETest, ZoomStaysAboveItsMinimumUVE) {
    Camera2DComponentUVE edge{};
    edge.zoom = Math::Vector2UVE{kMinimumCamera2DZoomUVE, kMinimumCamera2DZoomUVE};
    EXPECT_TRUE(IsCamera2DComponentValidUVE(edge));

    Camera2DComponentUVE zero{};
    zero.zoom = Math::Vector2UVE{0.0F, 1.0F};
    EXPECT_FALSE(IsCamera2DComponentValidUVE(zero));
    Camera2DComponentUVE negative{};
    negative.zoom = Math::Vector2UVE{1.0F, -2.0F};
    EXPECT_FALSE(IsCamera2DComponentValidUVE(negative));
    Camera2DComponentUVE notANumber{};
    notANumber.zoom = Math::Vector2UVE{std::numeric_limits<float>::quiet_NaN(), 1.0F};
    EXPECT_FALSE(IsCamera2DComponentValidUVE(notANumber));
}

TEST(Camera2DComponentUVETest, OffsetMustBeFiniteUVE) {
    Camera2DComponentUVE badOffset{};
    badOffset.offset = Math::Vector2UVE{0.0F, std::numeric_limits<float>::infinity()};
    EXPECT_FALSE(IsCamera2DComponentValidUVE(badOffset));
}

TEST(Camera2DObjectDefinitionUVETest, DefinitionValidityMirrorsTheCameraUVE) {
    EXPECT_TRUE(IsCamera2DObjectDefinitionValidUVE(Camera2DObjectDefinitionUVE{}));
    Camera2DObjectDefinitionUVE invalid{};
    invalid.camera.zoom = Math::Vector2UVE{0.0F, 0.0F};
    EXPECT_FALSE(IsCamera2DObjectDefinitionValidUVE(invalid));
}

class Camera2DObjectUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
};

TEST_F(Camera2DObjectUVETest, ApplyAttachesTheAuthoredCameraUVE) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    Camera2DObjectDefinitionUVE definition{};
    definition.camera.zoom = Math::Vector2UVE{2.0F, 2.0F};
    definition.camera.offset = Math::Vector2UVE{16.0F, -8.0F};
    definition.camera.current = true;
    ApplyCamera2DObjectDefinitionUVE(entityManager, entity, definition);

    const Camera2DComponentUVE& stored = entityManager.GetComponentUVE<Camera2DComponentUVE>(entity);
    EXPECT_FLOAT_EQ(stored.zoom.x, 2.0F);
    EXPECT_FLOAT_EQ(stored.offset.x, 16.0F);
    EXPECT_FLOAT_EQ(stored.offset.y, -8.0F);
    EXPECT_TRUE(stored.current);
}

TEST_F(Camera2DObjectUVETest, MakeCurrentAndFindCurrentCamera2DEntityUVE) {
    const EntityUVE first = entityManager.CreateEntityUVE();
    const EntityUVE second = entityManager.CreateEntityUVE();
    ApplyCamera2DObjectDefinitionUVE(entityManager, first, Camera2DObjectDefinitionUVE{});
    ApplyCamera2DObjectDefinitionUVE(entityManager, second, Camera2DObjectDefinitionUVE{});

    EXPECT_TRUE(Camera2DUVE::IsDocumentCamera2DEntityUVE(entityManager, first));
    EXPECT_EQ(Camera2DUVE::FindCurrentCamera2DEntityUVE(entityManager), kInvalidEntityUVE);

    Camera2DUVE::MakeCamera2DCurrentUVE(entityManager, first);
    EXPECT_EQ(Camera2DUVE::FindCurrentCamera2DEntityUVE(entityManager), first);
    EXPECT_TRUE(entityManager.GetComponentUVE<Camera2DComponentUVE>(first).current);
    EXPECT_FALSE(entityManager.GetComponentUVE<Camera2DComponentUVE>(second).current);

    Camera2DUVE::MakeCamera2DCurrentUVE(entityManager, second);
    EXPECT_EQ(Camera2DUVE::FindCurrentCamera2DEntityUVE(entityManager), second);
    EXPECT_FALSE(entityManager.GetComponentUVE<Camera2DComponentUVE>(first).current);
    EXPECT_TRUE(entityManager.GetComponentUVE<Camera2DComponentUVE>(second).current);
}

TEST(Camera2DNodePolicyUVETest, OrthoBoundsHalfExtentsAndWorldViewRectUVE) {
    Camera2DComponentUVE camera{};
    camera.zoom = Math::Vector2UVE{2.0F, 2.0F};
    camera.offset = Math::Vector2UVE{10.0F, 20.0F};
    const Math::Vector2UVE viewportSize{800.0F, 600.0F};

    Math::Vector2UVE halfExtents{};
    ASSERT_TRUE(Camera2DUVE::TryResolveViewHalfExtentsUVE(camera, viewportSize, halfExtents));
    EXPECT_FLOAT_EQ(halfExtents.x, 200.0F);
    EXPECT_FLOAT_EQ(halfExtents.y, 150.0F);

    Camera2DOrthoBoundsUVE ortho{};
    ASSERT_TRUE(Camera2DUVE::TryResolveOrthoBoundsUVE(camera, viewportSize, ortho));
    EXPECT_FLOAT_EQ(ortho.left, -190.0F);
    EXPECT_FLOAT_EQ(ortho.right, 210.0F);
    EXPECT_FLOAT_EQ(ortho.bottom, -130.0F);
    EXPECT_FLOAT_EQ(ortho.top, 170.0F);

    const Math::RectUVE worldRect =
        Camera2DUVE::ResolveWorldViewRectUVE(camera, Math::Vector2UVE{100.0F, 200.0F}, viewportSize);
    EXPECT_FLOAT_EQ(worldRect.position.x, -90.0F);
    EXPECT_FLOAT_EQ(worldRect.position.y, 70.0F);
    EXPECT_FLOAT_EQ(worldRect.size.x, 400.0F);
    EXPECT_FLOAT_EQ(worldRect.size.y, 300.0F);

    EXPECT_FALSE(Camera2DUVE::TryResolveOrthoBoundsUVE(
        camera, Math::Vector2UVE{0.0F, 600.0F}, ortho));
}

TEST_F(Camera2DObjectUVETest, Marker2DRemoteTransform2DAndVisibleOnScreenNotifier2DContractsUVE) {
    const EntityUVE markerEntity = entityManager.CreateEntityUVE();
    Marker2DObjectDefinitionUVE markerDef{};
    markerDef.marker.positionOffset = Math::Vector2UVE{10.0F, 5.0F};
    ApplyMarker2DObjectDefinitionUVE(entityManager, markerEntity, markerDef);
    const Math::Vector2UVE worldMarkerPos = Marker2DUVE::ComposeWorldPositionUVE(
        entityManager.GetComponentUVE<Marker2DComponentUVE>(markerEntity),
        Math::Vector2UVE{100.0F, 200.0F}, 0.0F);
    EXPECT_FLOAT_EQ(worldMarkerPos.x, 110.0F);
    EXPECT_FLOAT_EQ(worldMarkerPos.y, 205.0F);

    const EntityUVE src2D = entityManager.CreateEntityUVE();
    const EntityUVE dst2D = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(
        src2D, TransformComponentUVE{Math::Vector3UVE{42.0F, 84.0F, 0.0F}});
    entityManager.AddComponentUVE<TransformComponentUVE>(
        dst2D, TransformComponentUVE{Math::Vector3UVE{0.0F, 0.0F, 9.0F}});
    RemoteTransform2DObjectDefinitionUVE remoteDef{};
    remoteDef.remoteTransform.remoteTarget = dst2D;
    remoteDef.remoteTransform.useGlobalCoordinates = false;
    ApplyRemoteTransform2DObjectDefinitionUVE(entityManager, src2D, remoteDef);
    EXPECT_EQ(UpdateRemoteTransform2DObjectsUVE(entityManager), 1U);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(dst2D).localPosition.x,
                    42.0F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(dst2D).localPosition.y,
                    84.0F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(dst2D).localPosition.z,
                    9.0F);

    const EntityUVE notifier2D = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(
        notifier2D, TransformComponentUVE{Math::Vector3UVE{50.0F, 50.0F, 0.0F}});
    ApplyVisibleOnScreenNotifier2DObjectDefinitionUVE(
        entityManager, notifier2D, VisibleOnScreenNotifier2DObjectDefinitionUVE{});
    const Math::RectUVE viewRect{Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{100.0F, 100.0F}};
    EXPECT_EQ(UpdateVisibleOnScreenNotifier2DObjectsUVE(entityManager, &viewRect), 1U);
    const auto& nState =
        entityManager.GetComponentUVE<VisibleOnScreenNotifier2DComponentUVE>(notifier2D);
    EXPECT_TRUE(nState.isOnScreen);
    EXPECT_TRUE(nState.enteredScreenThisFrame);

    entityManager.GetComponentUVE<TransformComponentUVE>(notifier2D).localPosition =
        Math::Vector3UVE{500.0F, 500.0F, 0.0F};
    EXPECT_EQ(UpdateVisibleOnScreenNotifier2DObjectsUVE(entityManager, &viewRect), 0U);
    EXPECT_FALSE(nState.isOnScreen);
    EXPECT_TRUE(nState.exitedScreenThisFrame);
}

TEST_F(Camera2DObjectUVETest, Path2DPathFollow2DLine2DPolygon2DAndCanvasModulateContractsUVE) {
    const EntityUVE pathEntity = entityManager.CreateEntityUVE();
    Path2DObjectDefinitionUVE pathDef{};
    pathDef.path.points = {Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{100.0F, 0.0F}};
    ApplyPath2DObjectDefinitionUVE(entityManager, pathEntity, pathDef);
    EXPECT_FLOAT_EQ(
        Path2DUVE::ComputeTotalLengthUVE(entityManager.GetComponentUVE<Path2DComponentUVE>(pathEntity)),
        100.0F);

    const EntityUVE followerEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(followerEntity, TransformComponentUVE{});
    PathFollow2DObjectDefinitionUVE followDef{};
    followDef.follower.pathEntity = pathEntity;
    followDef.follower.speedPixelsPerSecond = 50.0F;
    followDef.follower.loop = false;
    ApplyPathFollow2DObjectDefinitionUVE(entityManager, followerEntity, followDef);
    EXPECT_EQ(UpdatePathFollow2DObjectsUVE(entityManager, 1.0F), 1U);
    EXPECT_FLOAT_EQ(
        entityManager.GetComponentUVE<TransformComponentUVE>(followerEntity).localPosition.x, 50.0F);
    EXPECT_FLOAT_EQ(
        entityManager.GetComponentUVE<PathFollow2DComponentUVE>(followerEntity).progressRatio,
        0.5F);

    const EntityUVE lineEntity = entityManager.CreateEntityUVE();
    Line2DObjectDefinitionUVE lineDef{};
    lineDef.line.points = {Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{10.0F, 0.0F},
                           Math::Vector2UVE{10.0F, 10.0F}};
    lineDef.line.widthPixels = 2.0F;
    ApplyLine2DObjectDefinitionUVE(entityManager, lineEntity, lineDef);
    const auto& lineComp = entityManager.GetComponentUVE<Line2DComponentUVE>(lineEntity);
    EXPECT_EQ(Line2DUVE::ResolveSegmentCountUVE(lineComp), 2U);
    const Math::RectUVE lineBounds = Line2DUVE::ComputeLocalBoundingRectUVE(lineComp);
    EXPECT_FLOAT_EQ(lineBounds.size.x, 12.0F);
    EXPECT_FLOAT_EQ(lineBounds.size.y, 12.0F);

    const EntityUVE polyEntity = entityManager.CreateEntityUVE();
    Polygon2DObjectDefinitionUVE polyDef{};
    polyDef.polygon.vertices = {Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{10.0F, 0.0F},
                                Math::Vector2UVE{10.0F, 10.0F}, Math::Vector2UVE{0.0F, 10.0F}};
    ApplyPolygon2DObjectDefinitionUVE(entityManager, polyEntity, polyDef);
    const auto& polyComp = entityManager.GetComponentUVE<Polygon2DComponentUVE>(polyEntity);
    EXPECT_FLOAT_EQ(Polygon2DUVE::ComputeSignedAreaUVE(polyComp), 100.0F);
    EXPECT_TRUE(Polygon2DUVE::ContainsLocalPointUVE(polyComp, Math::Vector2UVE{5.0F, 5.0F}));
    EXPECT_FALSE(Polygon2DUVE::ContainsLocalPointUVE(polyComp, Math::Vector2UVE{25.0F, 5.0F}));

    const EntityUVE modEntity = entityManager.CreateEntityUVE();
    CanvasModulateObjectDefinitionUVE modDef{};
    modDef.modulate.color = Math::ColorUVE{0.5F, 0.25F, 1.0F};
    modDef.modulate.opacity = 0.8F;
    ApplyCanvasModulateObjectDefinitionUVE(entityManager, modEntity, modDef);
    const CanvasModulateComponentUVE resolvedMod = ResolveActiveCanvasModulateUVE(entityManager);
    EXPECT_FLOAT_EQ(resolvedMod.color.r, 0.5F);
    EXPECT_FLOAT_EQ(resolvedMod.color.g, 0.25F);
    EXPECT_FLOAT_EQ(resolvedMod.opacity, 0.8F);
}

TEST_F(Camera2DObjectUVETest, Physics2DAnimatedSprite2DParallax2DAndTileMapLayerContractsUVE) {
    const EntityUVE dynBody = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(
        dynBody, TransformComponentUVE{Math::Vector3UVE{0.0F, 10.0F, 0.0F}});
    ApplyRigid2DObjectDefinitionUVE(entityManager, dynBody, Rigid2DObjectDefinitionUVE{});
    ApplyCollisionShape2DObjectDefinitionUVE(entityManager, dynBody,
                                             CollisionShape2DObjectDefinitionUVE{});

    const EntityUVE areaEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(
        areaEntity, TransformComponentUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}});
    ApplyArea2DObjectDefinitionUVE(entityManager, areaEntity, Area2DObjectDefinitionUVE{});

    const EntityUVE rayEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(
        rayEntity, TransformComponentUVE{Math::Vector3UVE{0.0F, 50.0F, 0.0F}});
    ApplyRayCast2DObjectDefinitionUVE(entityManager, rayEntity, RayCast2DObjectDefinitionUVE{});

    EXPECT_GE(UpdatePhysics2DObjectsUVE(entityManager, 0.1F, Math::Vector2UVE{0.0F, -100.0F}), 1U);
    EXPECT_EQ(entityManager.GetComponentUVE<Area2DComponentUVE>(areaEntity)
                  .overlappingEntities.size(),
              1U);
    EXPECT_TRUE(entityManager.GetComponentUVE<RayCast2DComponentUVE>(rayEntity).hit);

    const EntityUVE anim2D = entityManager.CreateEntityUVE();
    AnimatedSprite2DObjectDefinitionUVE animDef{};
    animDef.sprite.hframes = 4U;
    animDef.sprite.framesPerSecond = 10.0F;
    ApplyAnimatedSprite2DObjectDefinitionUVE(entityManager, anim2D, animDef);
    EXPECT_EQ(UpdateAnimatedSprite2DObjectsUVE(entityManager, 0.25F), 1U);
    EXPECT_EQ(entityManager.GetComponentUVE<AnimatedSprite2DComponentUVE>(anim2D).frame, 2U);

    const EntityUVE parallaxEntity = entityManager.CreateEntityUVE();
    ApplyParallax2DObjectDefinitionUVE(entityManager, parallaxEntity,
                                       Parallax2DObjectDefinitionUVE{});
    const Math::Vector2UVE effOffset = Parallax2DUVE::ComputeEffectiveOffsetUVE(
        entityManager.GetComponentUVE<Parallax2DComponentUVE>(parallaxEntity),
        Math::Vector2UVE{100.0F, 40.0F});
    EXPECT_FLOAT_EQ(effOffset.x, 50.0F);
    EXPECT_FLOAT_EQ(effOffset.y, 20.0F);

    const EntityUVE tileEntity = entityManager.CreateEntityUVE();
    ApplyTileMapLayerObjectDefinitionUVE(entityManager, tileEntity,
                                         TileMapLayerObjectDefinitionUVE{});
    auto& tileComp = entityManager.GetComponentUVE<TileMapLayerComponentUVE>(tileEntity);
    TileMapLayerUVE::SetCellTileUVE(tileComp, Math::Vector2iUVE{3, 4}, 9);
    EXPECT_EQ(TileMapLayerUVE::GetCellTileUVE(tileComp, Math::Vector2iUVE{3, 4}), 9);
}

TEST_F(Camera2DObjectUVETest,
       AppliesBatch5RenderingLightingAudioAnimNavAndInput2DObjectsUVE) {
    const EntityUVE mesh2d = entityManager.CreateEntityUVE();
    ApplyMeshInstance2DObjectDefinitionUVE(entityManager, mesh2d,
                                           MeshInstance2DObjectDefinitionUVE{});
    EXPECT_TRUE(entityManager.HasComponentUVE<MeshInstance2DComponentUVE>(mesh2d));

    const EntityUVE light2d = entityManager.CreateEntityUVE();
    ApplyPointLight2DObjectDefinitionUVE(entityManager, light2d,
                                         PointLight2DObjectDefinitionUVE{});
    const Math::ColorUVE lit =
        EvaluateLight2DIlluminationAtPointUVE(entityManager, Math::Vector2UVE{10.0F, 0.0F});
    EXPECT_GT(lit.r, 0.0F);

    const EntityUVE navAgent = entityManager.CreateEntityUVE();
    NavigationAgent2DObjectDefinitionUVE agentDef{};
    agentDef.agent.targetPosition = Math::Vector2UVE{100.0F, 0.0F};
    agentDef.agent.maxSpeedPixelsPerSecond = 200.0F;
    ApplyNavigationAgent2DObjectDefinitionUVE(entityManager, navAgent, agentDef);
    EXPECT_GE(UpdateAudioAnimNavInput2DObjectsUVE(entityManager, 0.25F), 1U);
    EXPECT_GT(entityManager.GetComponentUVE<TransformComponentUVE>(navAgent).localPosition.x,
              0.0F);

    const EntityUVE touchBtn = entityManager.CreateEntityUVE();
    ApplyTouchScreenButtonObjectDefinitionUVE(entityManager, touchBtn,
                                              TouchScreenButtonObjectDefinitionUVE{});
    EXPECT_TRUE(Math::ContainsUVE(
        entityManager.GetComponentUVE<TouchScreenButtonComponentUVE>(touchBtn).bounds,
        Math::Vector2UVE{10.0F, 10.0F}));
}

} // namespace
} // namespace UVE::Scene::Tests
