// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "canvas_ancestry.h"

#include <gtest/gtest.h>

#include "canvas_component.h"
#include "transform_component.h"
#include "ui_button_component.h"
#include "ui_image_component.h"
#include "visibility_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "input_system.h"
#include "mouse_button.h"
#include "memory_manager.h"
#include "scene_graph.h"
#include "ui_runtime.h"

namespace UVE::UI::Tests {
namespace {

class CanvasAncestryTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    Input::InputSystem inputSystem{eventSystem};
    UIRuntime runtime;

    Scene::Entity Place(const Scene::TransformComponent& transform = {}) {
        const Scene::Entity entity = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
        return entity;
    }

    void Parent(const Scene::Entity child, const Scene::Entity parent) {
        sceneGraph.SetParent(entityManager, child, parent);
        sceneGraph.Update(entityManager);
    }
};

TEST_F(CanvasAncestryTest, OrphanWidgetHasNoCanvasAndDraws) {
    const Scene::Entity image = Place();
    entityManager.AddComponent<Scene::UIImageComponent>(image);
    const CanvasAncestry ancestry = ResolveCanvasAncestry(entityManager, image);
    EXPECT_FALSE(ancestry.hasCanvas);
    EXPECT_TRUE(ancestry.visible);
    EXPECT_EQ(ancestry.sortOrder, 0);
    EXPECT_TRUE(ShouldDrawUiWidget(entityManager, image));
    inputSystem.Update();
    runtime.Tick(entityManager, inputSystem);
    EXPECT_EQ(runtime.GetDrawBatch().quads.size(), 1U);
}

TEST_F(CanvasAncestryTest, ClosestCanvasOwnsSortOrder) {
    const Scene::Entity outer = Place();
    Scene::CanvasComponent outerCanvas{};
    outerCanvas.sortOrder = 4;
    entityManager.AddComponent<Scene::CanvasComponent>(outer, outerCanvas);
    const Scene::Entity inner = Place();
    Scene::CanvasComponent innerCanvas{};
    innerCanvas.sortOrder = 9;
    entityManager.AddComponent<Scene::CanvasComponent>(inner, innerCanvas);
    const Scene::Entity image = Place();
    entityManager.AddComponent<Scene::UIImageComponent>(image);
    Parent(inner, outer);
    Parent(image, inner);
    const CanvasAncestry ancestry = ResolveCanvasAncestry(entityManager, image);
    EXPECT_TRUE(ancestry.hasCanvas);
    EXPECT_EQ(ancestry.canvas, inner);
    EXPECT_EQ(ancestry.sortOrder, 9);
    EXPECT_TRUE(ancestry.visible);
}

TEST_F(CanvasAncestryTest, HiddenAncestorCanvasHidesEvenWhenInnerIsVisible) {
    const Scene::Entity outer = Place();
    Scene::CanvasComponent outerCanvas{};
    outerCanvas.visible = false;
    entityManager.AddComponent<Scene::CanvasComponent>(outer, outerCanvas);
    const Scene::Entity inner = Place();
    entityManager.AddComponent<Scene::CanvasComponent>(inner, Scene::CanvasComponent{});
    const Scene::Entity image = Place();
    entityManager.AddComponent<Scene::UIImageComponent>(image);
    Parent(inner, outer);
    Parent(image, inner);
    EXPECT_FALSE(ResolveCanvasAncestry(entityManager, image).visible);
    EXPECT_FALSE(ShouldDrawUiWidget(entityManager, image));
    inputSystem.Update();
    runtime.Tick(entityManager, inputSystem);
    EXPECT_TRUE(runtime.GetDrawBatch().quads.empty());
}

TEST_F(CanvasAncestryTest, HiddenCanvasDropsQuadsAndClearsButtonClicks) {
    const Scene::Entity canvas = Place();
    Scene::CanvasComponent canvasComponent{};
    canvasComponent.visible = false;
    entityManager.AddComponent<Scene::CanvasComponent>(canvas, canvasComponent);
    const Scene::Entity button = Place();
    Scene::UIButtonComponent buttonComponent{};
    buttonComponent.rect.position = Math::Vector2{0.0F, 0.0F};
    buttonComponent.rect.size = Math::Vector2{50.0F, 50.0F};
    entityManager.AddComponent<Scene::UIButtonComponent>(button, buttonComponent);
    Parent(button, canvas);

    inputSystem.SetMousePosition(Math::Vector2{10.0F, 10.0F});
    inputSystem.SetMouseButtonState(Input::MouseButton::Left, true);
    inputSystem.Update();
    runtime.Tick(entityManager, inputSystem);

    const Scene::UIButtonComponent& live = entityManager.GetComponent<Scene::UIButtonComponent>(button);
    EXPECT_FALSE(live.isHovered);
    EXPECT_FALSE(live.wasClickedThisFrame);
    EXPECT_TRUE(runtime.GetDrawBatch().quads.empty());
}

TEST_F(CanvasAncestryTest, HigherSortOrderPaintsLater) {
    const Scene::Entity back = Place();
    Scene::CanvasComponent backCanvas{};
    backCanvas.sortOrder = 1;
    entityManager.AddComponent<Scene::CanvasComponent>(back, backCanvas);
    const Scene::Entity front = Place();
    Scene::CanvasComponent frontCanvas{};
    frontCanvas.sortOrder = 5;
    entityManager.AddComponent<Scene::CanvasComponent>(front, frontCanvas);

    Scene::UIImageComponent backImage{};
    backImage.tintColor = Math::Vector3{1.0F, 0.0F, 0.0F};
    const Scene::Entity backWidget = Place();
    entityManager.AddComponent<Scene::UIImageComponent>(backWidget, backImage);
    Parent(backWidget, back);

    Scene::UIImageComponent frontImage{};
    frontImage.tintColor = Math::Vector3{0.0F, 0.0F, 1.0F};
    const Scene::Entity frontWidget = Place();
    entityManager.AddComponent<Scene::UIImageComponent>(frontWidget, frontImage);
    Parent(frontWidget, front);

    inputSystem.Update();
    runtime.Tick(entityManager, inputSystem);
    ASSERT_EQ(runtime.GetDrawBatch().quads.size(), 2U);
    EXPECT_EQ(runtime.GetDrawBatch().quads[0].color, backImage.tintColor);
    EXPECT_EQ(runtime.GetDrawBatch().quads[1].color, frontImage.tintColor);
}

TEST_F(CanvasAncestryTest, WidgetVisibilityHidesWithoutACanvas) {
    const Scene::Entity image = Place();
    entityManager.AddComponent<Scene::UIImageComponent>(image);
    entityManager.AddComponent<Scene::VisibilityComponent>(image, Scene::VisibilityComponent{});
    entityManager.GetComponent<Scene::VisibilityComponent>(image).visible = false;
    entityManager.GetComponent<Scene::VisibilityComponent>(image).visibleInHierarchy = false;
    EXPECT_FALSE(ShouldDrawUiWidget(entityManager, image));
    inputSystem.Update();
    runtime.Tick(entityManager, inputSystem);
    EXPECT_TRUE(runtime.GetDrawBatch().quads.empty());
}

TEST_F(CanvasAncestryTest, SelfCanvasOnTheWidgetEntityCounts) {
    const Scene::Entity entity = Place();
    Scene::CanvasComponent canvas{};
    canvas.sortOrder = 3;
    entityManager.AddComponent<Scene::CanvasComponent>(entity, canvas);
    entityManager.AddComponent<Scene::UIImageComponent>(entity);
    const CanvasAncestry ancestry = ResolveCanvasAncestry(entityManager, entity);
    EXPECT_TRUE(ancestry.hasCanvas);
    EXPECT_EQ(ancestry.canvas, entity);
    EXPECT_EQ(ancestry.sortOrder, 3);
}

} // namespace
} // namespace UVE::UI::Tests
