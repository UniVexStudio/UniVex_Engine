// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "entity.h"
#include "hierarchy_component.h"
#include "ui_anchor_component.h"
#include "ui_button_component.h"
#include "ui_checkbox_component.h"
#include "ui_layout_container_component.h"
#include "ui_tooltip_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "input_system.h"
#include "rect.h"
#include "vector2.h"
#include "vector3.h"
#include "memory_manager.h"
#include "scene_serializer.h"
#include "ui_runtime.h"

namespace UVE::UI::Tests {
namespace {

class UICheckboxTooltipTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Input::InputSystem inputSystem{eventSystem};
    UIRuntime runtime;
    Scene::SceneSerializer serializer;

    void Frame(const Math::Vector2 mousePosition, const bool mouseDown, const float dt) {
        inputSystem.SetMousePosition(mousePosition);
        inputSystem.SetMouseButtonState(Input::MouseButton::Left, mouseDown);
        inputSystem.Update();
        runtime.SetDeltaTime(dt);
        runtime.Tick(entityManager, inputSystem);
    }

    Scene::Entity MakeCheckbox(const Math::Rect rect) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UICheckboxComponent checkbox;
        checkbox.rect = rect;
        entityManager.AddComponent<Scene::UICheckboxComponent>(entity, checkbox);
        return entity;
    }

    Scene::Entity MakeButton(const Math::Rect rect) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UIButtonComponent button;
        button.rect = rect;
        entityManager.AddComponent<Scene::UIButtonComponent>(entity, button);
        return entity;
    }
};

TEST_F(UICheckboxTooltipTest, Checkbox_ValidatorAcceptsDefaultsRejectsBadRects) {
    Scene::UICheckboxComponent checkbox;
    EXPECT_TRUE(IsUICheckboxComponentValid(checkbox));
    checkbox.rect.size = {0.0F, 20.0F};
    EXPECT_FALSE(IsUICheckboxComponentValid(checkbox));
    checkbox.rect.size = {20.0F, 20.0F};
    checkbox.checkColor = {1.0F, 1.0F, 1.0F};
    EXPECT_TRUE(IsUICheckboxComponentValid(checkbox));
}

TEST_F(UICheckboxTooltipTest, Click_TogglesCheckedAndSignalsExactlyOnce) {
    const Scene::Entity box = MakeCheckbox(Math::Rect{{10.0F, 10.0F}, {20.0F, 20.0F}});

    Frame({20.0F, 20.0F}, true, 0.016F);
    const Scene::UICheckboxComponent& pressed =
        entityManager.GetComponent<Scene::UICheckboxComponent>(box);
    EXPECT_TRUE(pressed.checked);
    EXPECT_TRUE(pressed.isHovered);
    EXPECT_TRUE(pressed.wasToggledThisFrame);

    Frame({20.0F, 20.0F}, true, 0.016F);
    EXPECT_TRUE(entityManager.GetComponent<Scene::UICheckboxComponent>(box).checked);
    EXPECT_FALSE(entityManager.GetComponent<Scene::UICheckboxComponent>(box).wasToggledThisFrame);

    Frame({20.0F, 20.0F}, false, 0.016F);
    Frame({20.0F, 20.0F}, true, 0.016F);
    const Scene::UICheckboxComponent& flipped =
        entityManager.GetComponent<Scene::UICheckboxComponent>(box);
    EXPECT_FALSE(flipped.checked);
    EXPECT_TRUE(flipped.wasToggledThisFrame);
}

TEST_F(UICheckboxTooltipTest, ClickOutside_DoesNothingButStillDraws) {
    const Scene::Entity box = MakeCheckbox(Math::Rect{{10.0F, 10.0F}, {20.0F, 20.0F}});

    Frame({200.0F, 200.0F}, true, 0.016F);

    const Scene::UICheckboxComponent& updated =
        entityManager.GetComponent<Scene::UICheckboxComponent>(box);
    EXPECT_FALSE(updated.checked);
    EXPECT_FALSE(updated.isHovered);
    EXPECT_FALSE(updated.wasToggledThisFrame);
    ASSERT_EQ(runtime.GetDrawBatch().quads.size(), 1U);
    EXPECT_EQ(runtime.GetDrawBatch().quads[0].color, updated.boxColor);
}

TEST_F(UICheckboxTooltipTest, Checked_DrawsBoxAndInsetCheck) {
    const Scene::Entity box = MakeCheckbox(Math::Rect{{10.0F, 10.0F}, {20.0F, 20.0F}});
    entityManager.GetComponent<Scene::UICheckboxComponent>(box).checked = true;

    Frame({200.0F, 200.0F}, false, 0.016F);

    const std::vector<UIQuad>& quads = runtime.GetDrawBatch().quads;
    ASSERT_EQ(quads.size(), 2U);
    EXPECT_EQ(quads[0].rect, (Math::Rect{{10.0F, 10.0F}, {20.0F, 20.0F}}));
    EXPECT_EQ(quads[1].rect, (Math::Rect{{15.0F, 15.0F}, {10.0F, 10.0F}}));
    EXPECT_EQ(quads[1].color,
              entityManager.GetComponent<Scene::UICheckboxComponent>(box).checkColor);
}

TEST_F(UICheckboxTooltipTest, Layout_CheckboxStacksAndAnchorsLikeButtons) {
    const Scene::Entity container = entityManager.CreateEntity();
    Scene::UILayoutContainerComponent layout;
    layout.rect = Math::Rect{{0.0F, 0.0F}, {300.0F, 300.0F}};
    layout.padding = 10.0F;
    entityManager.AddComponent<Scene::UILayoutContainerComponent>(container, layout);
    const Scene::Entity stacked = MakeCheckbox(Math::Rect{{0.0F, 0.0F}, {20.0F, 20.0F}});
    Scene::HierarchyComponent link;
    link.parent = container;
    link.siblingOrder = 0;
    entityManager.AddComponent<Scene::HierarchyComponent>(stacked, link);
    const Scene::Entity stretched = MakeCheckbox(Math::Rect{{0.0F, 0.0F}, {20.0F, 20.0F}});
    entityManager.AddComponent<Scene::UIAnchorComponent>(stretched, Scene::UIAnchorComponent{});
    runtime.SetViewportSize({400.0F, 300.0F});

    Frame({350.0F, 250.0F}, false, 0.016F);

    EXPECT_EQ(entityManager.GetComponent<Scene::UICheckboxComponent>(stacked).rect.position,
              (Math::Vector2{10.0F, 10.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UICheckboxComponent>(stretched).rect,
              (Math::Rect{{0.0F, 0.0F}, {400.0F, 300.0F}}));
}

TEST_F(UICheckboxTooltipTest, Tooltip_ValidatorRejectsOverlongTextAndBadTiming) {
    Scene::UITooltipComponent tooltip;
    tooltip.text = "Save";
    EXPECT_TRUE(IsUITooltipComponentValid(tooltip));
    tooltip.text = std::string(Scene::kMaximumUITextBytes + 1U, 'x');
    EXPECT_FALSE(IsUITooltipComponentValid(tooltip));
    tooltip.text = "Save";
    tooltip.delay = -1.0F;
    EXPECT_FALSE(IsUITooltipComponentValid(tooltip));
    tooltip.delay = 0.5F;
    tooltip.fontSize = 0.0F;
    EXPECT_FALSE(IsUITooltipComponentValid(tooltip));
}

TEST_F(UICheckboxTooltipTest, Tooltip_AppearsAfterDelayNearCursor) {
    const Scene::Entity button = MakeButton(Math::Rect{{100.0F, 100.0F}, {200.0F, 30.0F}});
    Scene::UITooltipComponent tooltip;
    tooltip.text = "Save";
    entityManager.AddComponent<Scene::UITooltipComponent>(button, tooltip);
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({150.0F, 115.0F}, false, 0.3F);
    EXPECT_FALSE(entityManager.GetComponent<Scene::UITooltipComponent>(button).visibleThisFrame);
    EXPECT_EQ(runtime.GetDrawBatch().quads.size(), 1U);

    Frame({150.0F, 115.0F}, false, 0.3F);
    EXPECT_TRUE(entityManager.GetComponent<Scene::UITooltipComponent>(button).visibleThisFrame);
    const std::vector<UIQuad>& quads = runtime.GetDrawBatch().quads;
    ASSERT_EQ(quads.size(), 6U); // the button, the popup background, four glyphs
    EXPECT_EQ(quads[1].rect.position, (Math::Vector2{162.0F, 127.0F}));
    const float expectedWidth = runtime.GetFontAtlas().MeasureTextWidth("Save", 16.0F) + 8.0F;
    EXPECT_FLOAT_EQ(quads[1].rect.size.x, expectedWidth);
    EXPECT_FLOAT_EQ(quads[1].rect.size.y, 24.0F);
    for (std::size_t i = 2U; i < 6U; ++i) {
        EXPECT_EQ(quads[i].kind, UIDrawItemKind::Glyph);
    }
}

TEST_F(UICheckboxTooltipTest, Tooltip_HidesOnLeaveAndResetsTheClock) {
    const Scene::Entity button = MakeButton(Math::Rect{{100.0F, 100.0F}, {200.0F, 30.0F}});
    Scene::UITooltipComponent tooltip;
    tooltip.text = "Save";
    entityManager.AddComponent<Scene::UITooltipComponent>(button, tooltip);
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({150.0F, 115.0F}, false, 1.0F);
    EXPECT_TRUE(entityManager.GetComponent<Scene::UITooltipComponent>(button).visibleThisFrame);
    Frame({10.0F, 10.0F}, false, 1.0F);
    EXPECT_FALSE(entityManager.GetComponent<Scene::UITooltipComponent>(button).visibleThisFrame);
    EXPECT_EQ(runtime.GetDrawBatch().quads.size(), 1U);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UITooltipComponent>(button).hoverTime, 0.0F);

    // Showing again costs another full delay - the interrupted 1.0s bought nothing.
    Frame({150.0F, 115.0F}, false, 0.3F);
    EXPECT_FALSE(entityManager.GetComponent<Scene::UITooltipComponent>(button).visibleThisFrame);
}

TEST_F(UICheckboxTooltipTest, Tooltip_EmptyTextNeverShows) {
    const Scene::Entity button = MakeButton(Math::Rect{{100.0F, 100.0F}, {200.0F, 30.0F}});
    entityManager.AddComponent<Scene::UITooltipComponent>(button, Scene::UITooltipComponent{});
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({150.0F, 115.0F}, false, 5.0F);

    EXPECT_FALSE(entityManager.GetComponent<Scene::UITooltipComponent>(button).visibleThisFrame);
    EXPECT_EQ(runtime.GetDrawBatch().quads.size(), 1U);
}

TEST_F(UICheckboxTooltipTest, Tooltip_ClampsIntoViewport) {
    const Scene::Entity button = MakeButton(Math::Rect{{700.0F, 550.0F}, {90.0F, 40.0F}});
    Scene::UITooltipComponent tooltip;
    tooltip.text = "Save";
    entityManager.AddComponent<Scene::UITooltipComponent>(button, tooltip);
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({790.0F, 590.0F}, false, 1.0F);

    EXPECT_TRUE(entityManager.GetComponent<Scene::UITooltipComponent>(button).visibleThisFrame);
    const Math::Rect popup = runtime.GetDrawBatch().quads[1].rect;
    EXPECT_GE(popup.position.x, 0.0F);
    EXPECT_GE(popup.position.y, 0.0F);
    EXPECT_LE(popup.position.x + popup.size.x, 800.0F);
    EXPECT_LE(popup.position.y + popup.size.y, 600.0F);
}

TEST_F(UICheckboxTooltipTest, SaveThenLoad_CheckboxAndTooltip_RoundTrip) {
    const Scene::Entity box = entityManager.CreateEntity();
    Scene::UICheckboxComponent checkbox;
    checkbox.rect = Math::Rect{{10.0F, 20.0F}, {24.0F, 24.0F}};
    checkbox.boxColor = Math::Vector3{0.1F, 0.1F, 0.1F};
    checkbox.checkColor = Math::Vector3{0.2F, 0.8F, 0.2F};
    checkbox.checked = true;
    checkbox.isHovered = true;
    checkbox.wasToggledThisFrame = true;
    entityManager.AddComponent<Scene::UICheckboxComponent>(box, checkbox);
    const Scene::Entity tip = entityManager.CreateEntity();
    Scene::UITooltipComponent tooltip;
    tooltip.text = "Save the game";
    tooltip.delay = 1.5F;
    tooltip.offset = {20.0F, 20.0F};
    tooltip.padding = 6.0F;
    tooltip.fontSize = 20.0F;
    tooltip.backgroundColor = Math::Vector3{0.05F, 0.05F, 0.05F};
    tooltip.hoverTime = 5.0F;
    tooltip.visibleThisFrame = true;
    entityManager.AddComponent<Scene::UITooltipComponent>(tip, tooltip);

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_checktip.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.Save(entityManager, {box, tip}, path, Scene::SceneAssetType::Scene));

    Scene::EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    const std::vector<Scene::Entity> loaded = serializer.Load(loadedManager, path);
    ASSERT_EQ(loaded.size(), 2U);
    const Scene::Entity boxEntity =
        loadedManager.HasComponent<Scene::UICheckboxComponent>(loaded[0]) ? loaded[0] : loaded[1];
    const Scene::Entity tipEntity = boxEntity == loaded[0] ? loaded[1] : loaded[0];
    const Scene::UICheckboxComponent& restoredBox =
        loadedManager.GetComponent<Scene::UICheckboxComponent>(boxEntity);
    EXPECT_TRUE(restoredBox.checked);
    EXPECT_FLOAT_EQ(restoredBox.checkColor.y, 0.8F);
    EXPECT_FALSE(restoredBox.isHovered);
    EXPECT_FALSE(restoredBox.wasToggledThisFrame);
    const Scene::UITooltipComponent& restoredTip =
        loadedManager.GetComponent<Scene::UITooltipComponent>(tipEntity);
    EXPECT_EQ(restoredTip.text, "Save the game");
    EXPECT_FLOAT_EQ(restoredTip.delay, 1.5F);
    EXPECT_FLOAT_EQ(restoredTip.fontSize, 20.0F);
    EXPECT_FLOAT_EQ(restoredTip.hoverTime, 0.0F);
    EXPECT_FALSE(restoredTip.visibleThisFrame);

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::UI::Tests
