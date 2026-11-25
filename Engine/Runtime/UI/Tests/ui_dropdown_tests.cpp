// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "entity.h"
#include "hierarchy_component.h"
#include "ui_anchor_component.h"
#include "ui_dropdown_component.h"
#include "ui_layout_container_component.h"
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

class UIDropdownTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Input::InputSystem inputSystem{eventSystem};
    UIRuntime runtime;
    Scene::SceneSerializer serializer;

    void Frame(const Math::Vector2 mousePosition, const bool mouseDown) {
        inputSystem.SetMousePosition(mousePosition);
        inputSystem.SetMouseButtonState(Input::MouseButton::Left, mouseDown);
        inputSystem.Update();
        runtime.Tick(entityManager, inputSystem);
    }

    Scene::Entity MakeDropdown(const Math::Rect rect, const std::string& options,
                                     const std::int32_t selected) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UIDropdownComponent dropdown;
        dropdown.rect = rect;
        dropdown.options = options;
        dropdown.selectedIndex = selected;
        entityManager.AddComponent<Scene::UIDropdownComponent>(entity, dropdown);
        return entity;
    }
};

TEST_F(UIDropdownTest, ValidatorAcceptsDefaultsRejectsBadConfig) {
    Scene::UIDropdownComponent dropdown;
    EXPECT_TRUE(IsUIDropdownComponentValid(dropdown));
    dropdown.rect.size = {160.0F, 0.0F};
    EXPECT_FALSE(IsUIDropdownComponentValid(dropdown));
    dropdown.rect.size = {160.0F, 28.0F};
    dropdown.selectedIndex = -1;
    EXPECT_FALSE(IsUIDropdownComponentValid(dropdown));
    dropdown.selectedIndex = 0;
    dropdown.optionHeight = 0.0F;
    EXPECT_FALSE(IsUIDropdownComponentValid(dropdown));
    dropdown.optionHeight = 24.0F;
    dropdown.options = std::string(2049U, 'x');
    EXPECT_FALSE(IsUIDropdownComponentValid(dropdown));
}

TEST_F(UIDropdownTest, Closed_DrawsBoxAndSelectedLabel) {
    MakeDropdown(Math::Rect{{10.0F, 10.0F}, {160.0F, 28.0F}}, "Easy\nNormal\nHard", 1);
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({700.0F, 500.0F}, false);

    const std::vector<UIQuad>& quads = runtime.GetDrawBatch().quads;
    ASSERT_EQ(quads.size(), 7U); // the box plus "Normal"
    EXPECT_EQ(quads[0].rect, (Math::Rect{{10.0F, 10.0F}, {160.0F, 28.0F}}));
    EXPECT_EQ(quads[0].kind, UIDrawItemKind::SolidColor);
    for (std::size_t i = 1U; i < 7U; ++i) {
        EXPECT_EQ(quads[i].kind, UIDrawItemKind::Glyph);
    }
    EXPECT_GE(quads[1].rect.position.x, 16.0F) << "the label starts after the text padding";
}

TEST_F(UIDropdownTest, Click_OpensPopupListingAllOptions) {
    MakeDropdown(Math::Rect{{10.0F, 10.0F}, {160.0F, 28.0F}}, "Easy\nNormal\nHard", 0);
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({50.0F, 20.0F}, true);

    const std::vector<UIQuad>& quads = runtime.GetDrawBatch().quads;
    ASSERT_EQ(quads.size(), 21U); // box + box label + popup + one highlight + three row labels
    EXPECT_EQ(quads[5].rect, (Math::Rect{{10.0F, 38.0F}, {160.0F, 72.0F}}));
    EXPECT_EQ(quads[5].kind, UIDrawItemKind::SolidColor);
    EXPECT_EQ(quads[6].rect, (Math::Rect{{10.0F, 38.0F}, {160.0F, 24.0F}}));
}

TEST_F(UIDropdownTest, ClickOption_SelectsAndClosesWithAOneShotSignal) {
    const Scene::Entity menu =
        MakeDropdown(Math::Rect{{10.0F, 10.0F}, {160.0F, 28.0F}}, "Easy\nNormal\nHard", 0);
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({50.0F, 20.0F}, true);
    Frame({50.0F, 20.0F}, false);
    EXPECT_TRUE(entityManager.GetComponent<Scene::UIDropdownComponent>(menu).open);
    Frame({50.0F, 98.0F}, true);

    const Scene::UIDropdownComponent& selected =
        entityManager.GetComponent<Scene::UIDropdownComponent>(menu);
    EXPECT_EQ(selected.selectedIndex, 2);
    EXPECT_FALSE(selected.open);
    EXPECT_TRUE(selected.wasSelectionChangedThisFrame);

    Frame({50.0F, 98.0F}, true);
    const Scene::UIDropdownComponent& settled =
        entityManager.GetComponent<Scene::UIDropdownComponent>(menu);
    EXPECT_EQ(settled.selectedIndex, 2);
    EXPECT_FALSE(settled.wasSelectionChangedThisFrame);
    EXPECT_EQ(runtime.GetDrawBatch().quads.size(), 5U) << "the closed box now shows Hard";
}

TEST_F(UIDropdownTest, ClickOutside_ClosesWithoutChanging) {
    const Scene::Entity menu =
        MakeDropdown(Math::Rect{{10.0F, 10.0F}, {160.0F, 28.0F}}, "Easy\nNormal\nHard", 1);
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({50.0F, 20.0F}, true);
    Frame({50.0F, 20.0F}, false);
    Frame({700.0F, 500.0F}, true);

    const Scene::UIDropdownComponent& closed =
        entityManager.GetComponent<Scene::UIDropdownComponent>(menu);
    EXPECT_FALSE(closed.open);
    EXPECT_EQ(closed.selectedIndex, 1);
    EXPECT_FALSE(closed.wasSelectionChangedThisFrame);
}

TEST_F(UIDropdownTest, ClickBoxWhileOpen_TogglesClosed) {
    const Scene::Entity menu =
        MakeDropdown(Math::Rect{{10.0F, 10.0F}, {160.0F, 28.0F}}, "Easy\nNormal\nHard", 0);
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({50.0F, 20.0F}, true);
    Frame({50.0F, 20.0F}, false);
    Frame({50.0F, 20.0F}, true);

    const Scene::UIDropdownComponent& toggled =
        entityManager.GetComponent<Scene::UIDropdownComponent>(menu);
    EXPECT_FALSE(toggled.open);
    EXPECT_EQ(toggled.selectedIndex, 0);
    EXPECT_FALSE(toggled.wasSelectionChangedThisFrame);
}

TEST_F(UIDropdownTest, RowHover_HighlightsHoveredRow) {
    const Scene::Entity menu =
        MakeDropdown(Math::Rect{{10.0F, 10.0F}, {160.0F, 28.0F}}, "Easy\nNormal\nHard", 0);
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({50.0F, 20.0F}, true);
    Frame({50.0F, 20.0F}, false);
    Frame({50.0F, 74.0F}, false);

    EXPECT_EQ(entityManager.GetComponent<Scene::UIDropdownComponent>(menu).hoveredIndex, 1);
    const Scene::UIDropdownComponent& hovered =
        entityManager.GetComponent<Scene::UIDropdownComponent>(menu);
    std::size_t hoverHighlights = 0U;
    for (const UIQuad& quad : runtime.GetDrawBatch().quads) {
        if (quad.kind == UIDrawItemKind::SolidColor && quad.color == hovered.optionHoverColor) {
            ++hoverHighlights;
            EXPECT_EQ(quad.rect, (Math::Rect{{10.0F, 62.0F}, {160.0F, 24.0F}}));
        }
    }
    EXPECT_EQ(hoverHighlights, 1U);
}

TEST_F(UIDropdownTest, Popup_FlipsUpWhenNoRoomBelow) {
    const Scene::Entity menu =
        MakeDropdown(Math::Rect{{10.0F, 580.0F}, {160.0F, 20.0F}}, "Easy\nNormal\nHard", 0);
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({50.0F, 590.0F}, true);
    Frame({50.0F, 590.0F}, false);

    const std::vector<UIQuad>& quads = runtime.GetDrawBatch().quads;
    ASSERT_GE(quads.size(), 6U);
    EXPECT_FLOAT_EQ(quads[5].rect.position.y, 508.0F) << "580 minus three 24px rows";

    Frame({50.0F, 520.0F}, false);
    EXPECT_EQ(entityManager.GetComponent<Scene::UIDropdownComponent>(menu).hoveredIndex, 0);
}

TEST_F(UIDropdownTest, Layout_DropdownStacksAndAnchorsLikeButtons) {
    const Scene::Entity container = entityManager.CreateEntity();
    Scene::UILayoutContainerComponent layout;
    layout.rect = Math::Rect{{0.0F, 0.0F}, {300.0F, 300.0F}};
    layout.padding = 10.0F;
    entityManager.AddComponent<Scene::UILayoutContainerComponent>(container, layout);
    const Scene::Entity stacked =
        MakeDropdown(Math::Rect{{0.0F, 0.0F}, {160.0F, 28.0F}}, "A\nB", 0);
    Scene::HierarchyComponent link;
    link.parent = container;
    link.siblingOrder = 0;
    entityManager.AddComponent<Scene::HierarchyComponent>(stacked, link);
    const Scene::Entity stretched =
        MakeDropdown(Math::Rect{{0.0F, 0.0F}, {160.0F, 28.0F}}, "A\nB", 0);
    entityManager.AddComponent<Scene::UIAnchorComponent>(stretched, Scene::UIAnchorComponent{});
    runtime.SetViewportSize({400.0F, 300.0F});

    Frame({350.0F, 250.0F}, false);

    EXPECT_EQ(entityManager.GetComponent<Scene::UIDropdownComponent>(stacked).rect.position,
              (Math::Vector2{10.0F, 10.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIDropdownComponent>(stretched).rect,
              (Math::Rect{{0.0F, 0.0F}, {400.0F, 300.0F}}));
}

TEST_F(UIDropdownTest, SaveThenLoad_Dropdown_RoundTrip) {
    const Scene::Entity menu = entityManager.CreateEntity();
    Scene::UIDropdownComponent dropdown;
    dropdown.rect = Math::Rect{{10.0F, 20.0F}, {160.0F, 28.0F}};
    dropdown.options = "Easy\nNormal\nHard";
    dropdown.selectedIndex = 2;
    dropdown.placeholder = "Pick one";
    dropdown.fontSize = 20.0F;
    dropdown.textColor = Math::Vector3{1.0F, 0.0F, 0.0F};
    dropdown.open = true;
    dropdown.isHovered = true;
    dropdown.hoveredIndex = 5;
    dropdown.wasSelectionChangedThisFrame = true;
    entityManager.AddComponent<Scene::UIDropdownComponent>(menu, dropdown);

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_dropdown.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.Save(entityManager, {menu}, path, Scene::SceneAssetType::Scene));

    Scene::EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    const std::vector<Scene::Entity> loaded = serializer.Load(loadedManager, path);
    ASSERT_EQ(loaded.size(), 1U);
    const Scene::UIDropdownComponent& restored =
        loadedManager.GetComponent<Scene::UIDropdownComponent>(loaded[0]);
    EXPECT_EQ(restored.options, "Easy\nNormal\nHard");
    EXPECT_EQ(restored.selectedIndex, 2);
    EXPECT_EQ(restored.placeholder, "Pick one");
    EXPECT_FLOAT_EQ(restored.fontSize, 20.0F);
    EXPECT_EQ(restored.textColor, (Math::Vector3{1.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(restored.open);
    EXPECT_FALSE(restored.isHovered);
    EXPECT_EQ(restored.hoveredIndex, -1);
    EXPECT_FALSE(restored.wasSelectionChangedThisFrame);

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::UI::Tests
