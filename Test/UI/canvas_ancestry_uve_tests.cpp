// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/canvas_ancestry_uve.h"

#include <gtest/gtest.h>

#include "uve/component/canvas_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/input/input_system_uve.h"
#include "uve/input/mouse_button_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/scene/scene_graph_uve.h"
#include "uve/ui/ui_runtime_uve.h"

namespace UVE::UI::Tests {
namespace {

class CanvasAncestryUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    Input::InputSystemUVE inputSystem{eventSystem};
    UIRuntimeUVE runtime;

    Scene::EntityUVE PlaceUVE(const Scene::TransformComponentUVE& transform = {}) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    void ParentUVE(const Scene::EntityUVE child, const Scene::EntityUVE parent) {
        sceneGraph.SetParentUVE(entityManager, child, parent);
        sceneGraph.UpdateUVE(entityManager);
    }
};

TEST_F(CanvasAncestryUVETest, OrphanWidgetHasNoCanvasAndDraws) {
    const Scene::EntityUVE image = PlaceUVE();
    entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(image);
    const CanvasAncestryUVE ancestry = ResolveCanvasAncestryUVE(entityManager, image);
    EXPECT_FALSE(ancestry.hasCanvas);
    EXPECT_TRUE(ancestry.visible);
    EXPECT_EQ(ancestry.sortOrder, 0);
    EXPECT_TRUE(ShouldDrawUiWidgetUVE(entityManager, image));
    inputSystem.UpdateUVE();
    runtime.TickUVE(entityManager, inputSystem);
    EXPECT_EQ(runtime.GetDrawBatchUVE().quads.size(), 1U);
}

TEST_F(CanvasAncestryUVETest, ClosestCanvasOwnsSortOrder) {
    const Scene::EntityUVE outer = PlaceUVE();
    Scene::CanvasComponentUVE outerCanvas{};
    outerCanvas.sortOrder = 4;
    entityManager.AddComponentUVE<Scene::CanvasComponentUVE>(outer, outerCanvas);
    const Scene::EntityUVE inner = PlaceUVE();
    Scene::CanvasComponentUVE innerCanvas{};
    innerCanvas.sortOrder = 9;
    entityManager.AddComponentUVE<Scene::CanvasComponentUVE>(inner, innerCanvas);
    const Scene::EntityUVE image = PlaceUVE();
    entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(image);
    ParentUVE(inner, outer);
    ParentUVE(image, inner);
    const CanvasAncestryUVE ancestry = ResolveCanvasAncestryUVE(entityManager, image);
    EXPECT_TRUE(ancestry.hasCanvas);
    EXPECT_EQ(ancestry.canvas, inner);
    EXPECT_EQ(ancestry.sortOrder, 9);
    EXPECT_TRUE(ancestry.visible);
}

TEST_F(CanvasAncestryUVETest, HiddenAncestorCanvasHidesEvenWhenInnerIsVisible) {
    const Scene::EntityUVE outer = PlaceUVE();
    Scene::CanvasComponentUVE outerCanvas{};
    outerCanvas.visible = false;
    entityManager.AddComponentUVE<Scene::CanvasComponentUVE>(outer, outerCanvas);
    const Scene::EntityUVE inner = PlaceUVE();
    entityManager.AddComponentUVE<Scene::CanvasComponentUVE>(inner, Scene::CanvasComponentUVE{});
    const Scene::EntityUVE image = PlaceUVE();
    entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(image);
    ParentUVE(inner, outer);
    ParentUVE(image, inner);
    EXPECT_FALSE(ResolveCanvasAncestryUVE(entityManager, image).visible);
    EXPECT_FALSE(ShouldDrawUiWidgetUVE(entityManager, image));
    inputSystem.UpdateUVE();
    runtime.TickUVE(entityManager, inputSystem);
    EXPECT_TRUE(runtime.GetDrawBatchUVE().quads.empty());
}

TEST_F(CanvasAncestryUVETest, HiddenCanvasDropsQuadsAndClearsButtonClicks) {
    const Scene::EntityUVE canvas = PlaceUVE();
    Scene::CanvasComponentUVE canvasComponent{};
    canvasComponent.visible = false;
    entityManager.AddComponentUVE<Scene::CanvasComponentUVE>(canvas, canvasComponent);
    const Scene::EntityUVE button = PlaceUVE();
    Scene::UIButtonComponentUVE buttonComponent{};
    buttonComponent.rect.position = Math::Vector2UVE{0.0F, 0.0F};
    buttonComponent.rect.size = Math::Vector2UVE{50.0F, 50.0F};
    entityManager.AddComponentUVE<Scene::UIButtonComponentUVE>(button, buttonComponent);
    ParentUVE(button, canvas);

    inputSystem.SetMousePositionUVE(Math::Vector2UVE{10.0F, 10.0F});
    inputSystem.SetMouseButtonStateUVE(Input::MouseButtonUVE::Left, true);
    inputSystem.UpdateUVE();
    runtime.TickUVE(entityManager, inputSystem);

    const Scene::UIButtonComponentUVE& live = entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(button);
    EXPECT_FALSE(live.isHovered);
    EXPECT_FALSE(live.wasClickedThisFrame);
    EXPECT_TRUE(runtime.GetDrawBatchUVE().quads.empty());
}

TEST_F(CanvasAncestryUVETest, HigherSortOrderPaintsLater) {
    const Scene::EntityUVE back = PlaceUVE();
    Scene::CanvasComponentUVE backCanvas{};
    backCanvas.sortOrder = 1;
    entityManager.AddComponentUVE<Scene::CanvasComponentUVE>(back, backCanvas);
    const Scene::EntityUVE front = PlaceUVE();
    Scene::CanvasComponentUVE frontCanvas{};
    frontCanvas.sortOrder = 5;
    entityManager.AddComponentUVE<Scene::CanvasComponentUVE>(front, frontCanvas);

    Scene::UIImageComponentUVE backImage{};
    backImage.tintColor = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    const Scene::EntityUVE backWidget = PlaceUVE();
    entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(backWidget, backImage);
    ParentUVE(backWidget, back);

    Scene::UIImageComponentUVE frontImage{};
    frontImage.tintColor = Math::Vector3UVE{0.0F, 0.0F, 1.0F};
    const Scene::EntityUVE frontWidget = PlaceUVE();
    entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(frontWidget, frontImage);
    ParentUVE(frontWidget, front);

    inputSystem.UpdateUVE();
    runtime.TickUVE(entityManager, inputSystem);
    ASSERT_EQ(runtime.GetDrawBatchUVE().quads.size(), 2U);
    EXPECT_EQ(runtime.GetDrawBatchUVE().quads[0].color, backImage.tintColor);
    EXPECT_EQ(runtime.GetDrawBatchUVE().quads[1].color, frontImage.tintColor);
}

TEST_F(CanvasAncestryUVETest, WidgetVisibilityHidesWithoutACanvas) {
    const Scene::EntityUVE image = PlaceUVE();
    entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(image);
    entityManager.AddComponentUVE<Scene::VisibilityComponentUVE>(image, Scene::VisibilityComponentUVE{});
    entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(image).visible = false;
    entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(image).visibleInHierarchy = false;
    EXPECT_FALSE(ShouldDrawUiWidgetUVE(entityManager, image));
    inputSystem.UpdateUVE();
    runtime.TickUVE(entityManager, inputSystem);
    EXPECT_TRUE(runtime.GetDrawBatchUVE().quads.empty());
}

TEST_F(CanvasAncestryUVETest, SelfCanvasOnTheWidgetEntityCounts) {
    const Scene::EntityUVE entity = PlaceUVE();
    Scene::CanvasComponentUVE canvas{};
    canvas.sortOrder = 3;
    entityManager.AddComponentUVE<Scene::CanvasComponentUVE>(entity, canvas);
    entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(entity);
    const CanvasAncestryUVE ancestry = ResolveCanvasAncestryUVE(entityManager, entity);
    EXPECT_TRUE(ancestry.hasCanvas);
    EXPECT_EQ(ancestry.canvas, entity);
    EXPECT_EQ(ancestry.sortOrder, 3);
}

} // namespace
} // namespace UVE::UI::Tests
