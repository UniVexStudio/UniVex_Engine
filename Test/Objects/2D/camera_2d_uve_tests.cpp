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

} // namespace
} // namespace UVE::Scene::Tests
