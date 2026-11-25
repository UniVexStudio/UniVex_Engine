// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_anchors.h"

#include <cstdint>
#include <filesystem>
#include <limits>

#include <gtest/gtest.h>

#include "entity.h"
#include "hierarchy_component.h"
#include "ui_anchor_component.h"
#include "ui_button_component.h"
#include "ui_layout_container_component.h"
#include "ui_text_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "input_system.h"
#include "rect.h"
#include "vector2.h"
#include "memory_manager.h"
#include "scene_serializer.h"
#include "ui_runtime.h"

namespace UVE::UI::Tests {
namespace {

class UIAnchorsTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Input::InputSystem inputSystem{eventSystem};
    UIRuntime runtime;
    Scene::SceneSerializer serializer;

    void Link(const Scene::Entity entity, const Scene::Entity parent, const std::int64_t order) {
        Scene::HierarchyComponent link;
        link.parent = parent;
        link.siblingOrder = order;
        entityManager.AddComponent<Scene::HierarchyComponent>(entity, link);
    }

    void Anchor(const Scene::Entity entity, const Math::Vector2 anchorMin,
                   const Math::Vector2 anchorMax, const Math::Vector2 offsetMin,
                   const Math::Vector2 offsetMax) {
        Scene::UIAnchorComponent anchor;
        anchor.anchorMin = anchorMin;
        anchor.anchorMax = anchorMax;
        anchor.offsetMin = offsetMin;
        anchor.offsetMax = offsetMax;
        entityManager.AddComponent<Scene::UIAnchorComponent>(entity, anchor);
    }

    Scene::Entity MakeButton(const Math::Vector2 size) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UIButtonComponent button;
        button.rect.size = size;
        entityManager.AddComponent<Scene::UIButtonComponent>(entity, button);
        return entity;
    }
};

TEST_F(UIAnchorsTest, Anchor_ValidatorAcceptsOverflowRejectsNonFinite) {
    Scene::UIAnchorComponent anchor;
    anchor.anchorMin = {1.0F, 1.0F};
    anchor.anchorMax = {1.5F, 1.0F};
    EXPECT_TRUE(IsUIAnchorComponentValid(anchor));
    anchor.offsetMax = {0.0F, std::numeric_limits<float>::infinity()};
    EXPECT_FALSE(IsUIAnchorComponentValid(anchor));
}

TEST_F(UIAnchorsTest, Stretch_FillsViewportMinusMargins) {
    const Scene::Entity widget = MakeButton({10.0F, 10.0F});
    Anchor(widget, {0.0F, 0.0F}, {1.0F, 1.0F}, {10.0F, 20.0F}, {-10.0F, -20.0F});

    ResolveUIAnchors(entityManager, {1280.0F, 720.0F});

    const Scene::UIButtonComponent& resolved =
        entityManager.GetComponent<Scene::UIButtonComponent>(widget);
    EXPECT_EQ(resolved.rect.position, (Math::Vector2{10.0F, 20.0F}));
    EXPECT_EQ(resolved.rect.size, (Math::Vector2{1260.0F, 680.0F}));
}

TEST_F(UIAnchorsTest, CornerPin_FollowsViewportResizeAtFixedSize) {
    const Scene::Entity widget = MakeButton({10.0F, 10.0F});
    Link(widget, Scene::kInvalidEntity, 0);
    Anchor(widget, {1.0F, 1.0F}, {1.0F, 1.0F}, {-120.0F, -32.0F}, {0.0F, 0.0F});

    ResolveUIAnchors(entityManager, {1280.0F, 720.0F});
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(widget).rect.position,
              (Math::Vector2{1160.0F, 688.0F}));

    ResolveUIAnchors(entityManager, {800.0F, 600.0F});
    const Scene::UIButtonComponent& resolved =
        entityManager.GetComponent<Scene::UIButtonComponent>(widget);
    EXPECT_EQ(resolved.rect.position, (Math::Vector2{680.0F, 568.0F}));
    EXPECT_EQ(resolved.rect.size, (Math::Vector2{120.0F, 32.0F}));
}

TEST_F(UIAnchorsTest, Nested_ChildResolvesAgainstAnchoredParentNotAuthoredRect) {
    const Scene::Entity parent = entityManager.CreateEntity();
    Scene::UILayoutContainerComponent container;
    container.rect = Math::Rect{{0.0F, 0.0F}, {50.0F, 50.0F}}; // deliberately wrong
    entityManager.AddComponent<Scene::UILayoutContainerComponent>(parent, container);
    Link(parent, Scene::kInvalidEntity, 0);
    Anchor(parent, {0.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 0.0F}, {0.0F, 0.0F});
    const Scene::Entity child = MakeButton({10.0F, 10.0F});
    Link(child, parent, 0);
    Anchor(child, {0.25F, 0.25F}, {0.75F, 0.75F}, {0.0F, 0.0F}, {0.0F, 0.0F});

    ResolveUIAnchors(entityManager, {400.0F, 400.0F});

    EXPECT_EQ(entityManager.GetComponent<Scene::UILayoutContainerComponent>(parent).rect.size,
              (Math::Vector2{400.0F, 400.0F}));
    const Scene::UIButtonComponent& resolved =
        entityManager.GetComponent<Scene::UIButtonComponent>(child);
    EXPECT_EQ(resolved.rect.position, (Math::Vector2{100.0F, 100.0F}));
    EXPECT_EQ(resolved.rect.size, (Math::Vector2{200.0F, 200.0F}));
}

TEST_F(UIAnchorsTest, Text_TakesResolvedMinimumAndIgnoresMaximum) {
    const Scene::Entity label = entityManager.CreateEntity();
    Scene::UITextComponent text;
    text.text = "Score";
    entityManager.AddComponent<Scene::UITextComponent>(label, text);
    Anchor(label, {0.5F, 0.5F}, {1.0F, 1.0F}, {5.0F, 5.0F}, {50.0F, 50.0F});

    ResolveUIAnchors(entityManager, {200.0F, 100.0F});

    EXPECT_EQ(entityManager.GetComponent<Scene::UITextComponent>(label).positionPixels,
              (Math::Vector2{105.0F, 55.0F}));
}

TEST_F(UIAnchorsTest, InvalidAnchorAndBareEntitiesAreSkipped) {
    const Scene::Entity poisoned = MakeButton({10.0F, 10.0F});
    Scene::UIAnchorComponent anchor;
    anchor.anchorMin = {std::numeric_limits<float>::quiet_NaN(), 0.0F};
    entityManager.AddComponent<Scene::UIAnchorComponent>(poisoned, anchor);
    const Scene::Entity bare = entityManager.CreateEntity();
    Anchor(bare, {0.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 0.0F}, {0.0F, 0.0F});

    ResolveUIAnchors(entityManager, {1280.0F, 720.0F});

    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(poisoned).rect.size,
              (Math::Vector2{10.0F, 10.0F}));
}

TEST_F(UIAnchorsTest, TickIntegration_ContainerWinsPositionAnchorsKeepSize) {
    const Scene::Entity parent = entityManager.CreateEntity();
    Scene::UILayoutContainerComponent container;
    container.rect = Math::Rect{{0.0F, 0.0F}, {400.0F, 400.0F}};
    entityManager.AddComponent<Scene::UILayoutContainerComponent>(parent, container);
    Link(parent, Scene::kInvalidEntity, 0);
    const Scene::Entity child = MakeButton({10.0F, 10.0F});
    Link(child, parent, 0);
    Anchor(child, {0.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 0.0F}, {0.0F, 0.0F});

    inputSystem.SetMousePosition(Math::Vector2{200.0F, 200.0F});
    inputSystem.SetMouseButtonState(Input::MouseButton::Left, true);
    inputSystem.Update();
    runtime.Tick(entityManager, inputSystem);

    // Anchors stretched it to the 400x400 parent, then the stack pinned its position.
    const Scene::UIButtonComponent& resolved =
        entityManager.GetComponent<Scene::UIButtonComponent>(child);
    EXPECT_EQ(resolved.rect.position, (Math::Vector2{0.0F, 0.0F}));
    EXPECT_EQ(resolved.rect.size, (Math::Vector2{400.0F, 400.0F}));
    EXPECT_TRUE(resolved.isHovered);
    EXPECT_TRUE(resolved.wasClickedThisFrame);
}

TEST_F(UIAnchorsTest, ViewportSize_InvalidSizesAreIgnored) {
    runtime.SetViewportSize({1280.0F, 720.0F});
    runtime.SetViewportSize({-1.0F, std::numeric_limits<float>::quiet_NaN()});
    EXPECT_EQ(runtime.GetViewportSize(), (Math::Vector2{1280.0F, 720.0F}));
}

TEST_F(UIAnchorsTest, SaveThenLoad_Anchor_RoundTripsExactly) {
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::UIButtonComponent button;
    button.rect.size = {120.0F, 32.0F};
    entityManager.AddComponent<Scene::UIButtonComponent>(entity, button);
    Anchor(entity, {0.0F, 1.0F}, {1.0F, 1.0F}, {8.0F, -40.0F}, {-8.0F, -8.0F});

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_anchor.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.Save(entityManager, {entity}, path, Scene::SceneAssetType::Scene));

    Scene::EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    const std::vector<Scene::Entity> loaded = serializer.Load(loadedManager, path);
    ASSERT_EQ(loaded.size(), 1U);
    const Scene::UIAnchorComponent& restored =
        loadedManager.GetComponent<Scene::UIAnchorComponent>(loaded[0]);
    EXPECT_EQ(restored.anchorMin, (Math::Vector2{0.0F, 1.0F}));
    EXPECT_EQ(restored.anchorMax, (Math::Vector2{1.0F, 1.0F}));
    EXPECT_EQ(restored.offsetMin, (Math::Vector2{8.0F, -40.0F}));
    EXPECT_EQ(restored.offsetMax, (Math::Vector2{-8.0F, -8.0F}));

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::UI::Tests
