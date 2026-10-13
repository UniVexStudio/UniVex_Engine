// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/ui_anchors_uve.h"

#include <cstdint>
#include <filesystem>
#include <limits>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/ui_anchor_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_layout_container_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/input/input_system_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/scene/scene_serializer_uve.h"
#include "uve/ui/ui_runtime_uve.h"

namespace UVE::UI::Tests {
namespace {

class UIAnchorsUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Input::InputSystemUVE inputSystem{eventSystem};
    UIRuntimeUVE runtime;
    Scene::SceneSerializerUVE serializer;

    void LinkUVE(const Scene::EntityUVE entity, const Scene::EntityUVE parent, const std::int64_t order) {
        Scene::HierarchyComponentUVE link;
        link.parent = parent;
        link.siblingOrder = order;
        entityManager.AddComponentUVE<Scene::HierarchyComponentUVE>(entity, link);
    }

    void AnchorUVE(const Scene::EntityUVE entity, const Math::Vector2UVE anchorMin,
                   const Math::Vector2UVE anchorMax, const Math::Vector2UVE offsetMin,
                   const Math::Vector2UVE offsetMax) {
        Scene::UIAnchorComponentUVE anchor;
        anchor.anchorMin = anchorMin;
        anchor.anchorMax = anchorMax;
        anchor.offsetMin = offsetMin;
        anchor.offsetMax = offsetMax;
        entityManager.AddComponentUVE<Scene::UIAnchorComponentUVE>(entity, anchor);
    }

    Scene::EntityUVE MakeButtonUVE(const Math::Vector2UVE size) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UIButtonComponentUVE button;
        button.rect.size = size;
        entityManager.AddComponentUVE<Scene::UIButtonComponentUVE>(entity, button);
        return entity;
    }
};

TEST_F(UIAnchorsUVETest, Anchor_ValidatorAcceptsOverflowRejectsNonFinite) {
    Scene::UIAnchorComponentUVE anchor;
    anchor.anchorMin = {1.0F, 1.0F};
    anchor.anchorMax = {1.5F, 1.0F};
    EXPECT_TRUE(IsUIAnchorComponentValidUVE(anchor));
    anchor.offsetMax = {0.0F, std::numeric_limits<float>::infinity()};
    EXPECT_FALSE(IsUIAnchorComponentValidUVE(anchor));
}

TEST_F(UIAnchorsUVETest, Stretch_FillsViewportMinusMargins) {
    const Scene::EntityUVE widget = MakeButtonUVE({10.0F, 10.0F});
    AnchorUVE(widget, {0.0F, 0.0F}, {1.0F, 1.0F}, {10.0F, 20.0F}, {-10.0F, -20.0F});

    ResolveUIAnchorsUVE(entityManager, {1280.0F, 720.0F});

    const Scene::UIButtonComponentUVE& resolved =
        entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget);
    EXPECT_EQ(resolved.rect.position, (Math::Vector2UVE{10.0F, 20.0F}));
    EXPECT_EQ(resolved.rect.size, (Math::Vector2UVE{1260.0F, 680.0F}));
}

TEST_F(UIAnchorsUVETest, CornerPin_FollowsViewportResizeAtFixedSize) {
    const Scene::EntityUVE widget = MakeButtonUVE({10.0F, 10.0F});
    LinkUVE(widget, Scene::kInvalidEntityUVE, 0);
    AnchorUVE(widget, {1.0F, 1.0F}, {1.0F, 1.0F}, {-120.0F, -32.0F}, {0.0F, 0.0F});

    ResolveUIAnchorsUVE(entityManager, {1280.0F, 720.0F});
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget).rect.position,
              (Math::Vector2UVE{1160.0F, 688.0F}));

    ResolveUIAnchorsUVE(entityManager, {800.0F, 600.0F});
    const Scene::UIButtonComponentUVE& resolved =
        entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget);
    EXPECT_EQ(resolved.rect.position, (Math::Vector2UVE{680.0F, 568.0F}));
    EXPECT_EQ(resolved.rect.size, (Math::Vector2UVE{120.0F, 32.0F}));
}

TEST_F(UIAnchorsUVETest, Nested_ChildResolvesAgainstAnchoredParentNotAuthoredRect) {
    const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
    Scene::UILayoutContainerComponentUVE container;
    container.rect = Math::RectUVE{{0.0F, 0.0F}, {50.0F, 50.0F}}; // deliberately wrong
    entityManager.AddComponentUVE<Scene::UILayoutContainerComponentUVE>(parent, container);
    LinkUVE(parent, Scene::kInvalidEntityUVE, 0);
    AnchorUVE(parent, {0.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 0.0F}, {0.0F, 0.0F});
    const Scene::EntityUVE child = MakeButtonUVE({10.0F, 10.0F});
    LinkUVE(child, parent, 0);
    AnchorUVE(child, {0.25F, 0.25F}, {0.75F, 0.75F}, {0.0F, 0.0F}, {0.0F, 0.0F});

    ResolveUIAnchorsUVE(entityManager, {400.0F, 400.0F});

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(parent).rect.size,
              (Math::Vector2UVE{400.0F, 400.0F}));
    const Scene::UIButtonComponentUVE& resolved =
        entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(child);
    EXPECT_EQ(resolved.rect.position, (Math::Vector2UVE{100.0F, 100.0F}));
    EXPECT_EQ(resolved.rect.size, (Math::Vector2UVE{200.0F, 200.0F}));
}

TEST_F(UIAnchorsUVETest, Text_TakesResolvedMinimumAndIgnoresMaximum) {
    const Scene::EntityUVE label = entityManager.CreateEntityUVE();
    Scene::UITextComponentUVE text;
    text.text = "Score";
    entityManager.AddComponentUVE<Scene::UITextComponentUVE>(label, text);
    AnchorUVE(label, {0.5F, 0.5F}, {1.0F, 1.0F}, {5.0F, 5.0F}, {50.0F, 50.0F});

    ResolveUIAnchorsUVE(entityManager, {200.0F, 100.0F});

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UITextComponentUVE>(label).positionPixels,
              (Math::Vector2UVE{105.0F, 55.0F}));
}

TEST_F(UIAnchorsUVETest, InvalidAnchorAndBareEntitiesAreSkipped) {
    const Scene::EntityUVE poisoned = MakeButtonUVE({10.0F, 10.0F});
    Scene::UIAnchorComponentUVE anchor;
    anchor.anchorMin = {std::numeric_limits<float>::quiet_NaN(), 0.0F};
    entityManager.AddComponentUVE<Scene::UIAnchorComponentUVE>(poisoned, anchor);
    const Scene::EntityUVE bare = entityManager.CreateEntityUVE();
    AnchorUVE(bare, {0.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 0.0F}, {0.0F, 0.0F});

    ResolveUIAnchorsUVE(entityManager, {1280.0F, 720.0F});

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(poisoned).rect.size,
              (Math::Vector2UVE{10.0F, 10.0F}));
}

TEST_F(UIAnchorsUVETest, TickIntegration_ContainerWinsPositionAnchorsKeepSize) {
    const Scene::EntityUVE parent = entityManager.CreateEntityUVE();
    Scene::UILayoutContainerComponentUVE container;
    container.rect = Math::RectUVE{{0.0F, 0.0F}, {400.0F, 400.0F}};
    entityManager.AddComponentUVE<Scene::UILayoutContainerComponentUVE>(parent, container);
    LinkUVE(parent, Scene::kInvalidEntityUVE, 0);
    const Scene::EntityUVE child = MakeButtonUVE({10.0F, 10.0F});
    LinkUVE(child, parent, 0);
    AnchorUVE(child, {0.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 0.0F}, {0.0F, 0.0F});

    inputSystem.SetMousePositionUVE(Math::Vector2UVE{200.0F, 200.0F});
    inputSystem.SetMouseButtonStateUVE(Input::MouseButtonUVE::Left, true);
    inputSystem.UpdateUVE();
    runtime.TickUVE(entityManager, inputSystem);

    // Anchors stretched it to the 400x400 parent, then the stack pinned its position.
    const Scene::UIButtonComponentUVE& resolved =
        entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(child);
    EXPECT_EQ(resolved.rect.position, (Math::Vector2UVE{0.0F, 0.0F}));
    EXPECT_EQ(resolved.rect.size, (Math::Vector2UVE{400.0F, 400.0F}));
    EXPECT_TRUE(resolved.isHovered);
    EXPECT_TRUE(resolved.wasClickedThisFrame);
}

TEST_F(UIAnchorsUVETest, ViewportSize_InvalidSizesAreIgnored) {
    runtime.SetViewportSizeUVE({1280.0F, 720.0F});
    runtime.SetViewportSizeUVE({-1.0F, std::numeric_limits<float>::quiet_NaN()});
    EXPECT_EQ(runtime.GetViewportSizeUVE(), (Math::Vector2UVE{1280.0F, 720.0F}));
}

TEST_F(UIAnchorsUVETest, SaveThenLoad_Anchor_RoundTripsExactly) {
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::UIButtonComponentUVE button;
    button.rect.size = {120.0F, 32.0F};
    entityManager.AddComponentUVE<Scene::UIButtonComponentUVE>(entity, button);
    AnchorUVE(entity, {0.0F, 1.0F}, {1.0F, 1.0F}, {8.0F, -40.0F}, {-8.0F, -8.0F});

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_anchor.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, Scene::SceneAssetTypeUVE::Scene));

    Scene::EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<Scene::EntityUVE> loaded = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(loaded.size(), 1U);
    const Scene::UIAnchorComponentUVE& restored =
        loadedManager.GetComponentUVE<Scene::UIAnchorComponentUVE>(loaded[0]);
    EXPECT_EQ(restored.anchorMin, (Math::Vector2UVE{0.0F, 1.0F}));
    EXPECT_EQ(restored.anchorMax, (Math::Vector2UVE{1.0F, 1.0F}));
    EXPECT_EQ(restored.offsetMin, (Math::Vector2UVE{8.0F, -40.0F}));
    EXPECT_EQ(restored.offsetMax, (Math::Vector2UVE{-8.0F, -8.0F}));

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::UI::Tests
