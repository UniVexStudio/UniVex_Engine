// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/ui_anchor_component_uve.h"
#include "uve/component/ui_dropdown_component_uve.h"
#include "uve/component/ui_layout_container_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/input/input_system_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/scene/scene_serializer_uve.h"
#include "uve/ui/ui_runtime_uve.h"

namespace UVE::UI::Tests {
namespace {

class UIDropdownUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Input::InputSystemUVE inputSystem{eventSystem};
    UIRuntimeUVE runtime;
    Scene::SceneSerializerUVE serializer;

    void FrameUVE(const Math::Vector2UVE mousePosition, const bool mouseDown) {
        inputSystem.SetMousePositionUVE(mousePosition);
        inputSystem.SetMouseButtonStateUVE(Input::MouseButtonUVE::Left, mouseDown);
        inputSystem.UpdateUVE();
        runtime.TickUVE(entityManager, inputSystem);
    }

    Scene::EntityUVE MakeDropdownUVE(const Math::RectUVE rect, const std::string& options,
                                     const std::int32_t selected) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UIDropdownComponentUVE dropdown;
        dropdown.rect = rect;
        dropdown.options = options;
        dropdown.selectedIndex = selected;
        entityManager.AddComponentUVE<Scene::UIDropdownComponentUVE>(entity, dropdown);
        return entity;
    }
};

TEST_F(UIDropdownUVETest, ValidatorAcceptsDefaultsRejectsBadConfig) {
    Scene::UIDropdownComponentUVE dropdown;
    EXPECT_TRUE(IsUIDropdownComponentValidUVE(dropdown));
    dropdown.rect.size = {160.0F, 0.0F};
    EXPECT_FALSE(IsUIDropdownComponentValidUVE(dropdown));
    dropdown.rect.size = {160.0F, 28.0F};
    dropdown.selectedIndex = -1;
    EXPECT_FALSE(IsUIDropdownComponentValidUVE(dropdown));
    dropdown.selectedIndex = 0;
    dropdown.optionHeight = 0.0F;
    EXPECT_FALSE(IsUIDropdownComponentValidUVE(dropdown));
    dropdown.optionHeight = 24.0F;
    dropdown.options = std::string(2049U, 'x');
    EXPECT_FALSE(IsUIDropdownComponentValidUVE(dropdown));
}

TEST_F(UIDropdownUVETest, Closed_DrawsBoxAndSelectedLabel) {
    MakeDropdownUVE(Math::RectUVE{{10.0F, 10.0F}, {160.0F, 28.0F}}, "Easy\nNormal\nHard", 1);
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({700.0F, 500.0F}, false);

    const std::vector<UIQuadUVE>& quads = runtime.GetDrawBatchUVE().quads;
    ASSERT_EQ(quads.size(), 7U); // the box plus "Normal"
    EXPECT_EQ(quads[0].rect, (Math::RectUVE{{10.0F, 10.0F}, {160.0F, 28.0F}}));
    EXPECT_EQ(quads[0].kind, UIDrawItemKindUVE::SolidColor);
    for (std::size_t i = 1U; i < 7U; ++i) {
        EXPECT_EQ(quads[i].kind, UIDrawItemKindUVE::Glyph);
    }
    EXPECT_GE(quads[1].rect.position.x, 16.0F) << "the label starts after the text padding";
}

TEST_F(UIDropdownUVETest, Click_OpensPopupListingAllOptions) {
    MakeDropdownUVE(Math::RectUVE{{10.0F, 10.0F}, {160.0F, 28.0F}}, "Easy\nNormal\nHard", 0);
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({50.0F, 20.0F}, true);

    const std::vector<UIQuadUVE>& quads = runtime.GetDrawBatchUVE().quads;
    ASSERT_EQ(quads.size(), 21U); // box + box label + popup + one highlight + three row labels
    EXPECT_EQ(quads[5].rect, (Math::RectUVE{{10.0F, 38.0F}, {160.0F, 72.0F}}));
    EXPECT_EQ(quads[5].kind, UIDrawItemKindUVE::SolidColor);
    EXPECT_EQ(quads[6].rect, (Math::RectUVE{{10.0F, 38.0F}, {160.0F, 24.0F}}));
}

TEST_F(UIDropdownUVETest, ClickOption_SelectsAndClosesWithAOneShotSignal) {
    const Scene::EntityUVE menu =
        MakeDropdownUVE(Math::RectUVE{{10.0F, 10.0F}, {160.0F, 28.0F}}, "Easy\nNormal\nHard", 0);
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({50.0F, 20.0F}, true);
    FrameUVE({50.0F, 20.0F}, false);
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::UIDropdownComponentUVE>(menu).open);
    FrameUVE({50.0F, 98.0F}, true);

    const Scene::UIDropdownComponentUVE& selected =
        entityManager.GetComponentUVE<Scene::UIDropdownComponentUVE>(menu);
    EXPECT_EQ(selected.selectedIndex, 2);
    EXPECT_FALSE(selected.open);
    EXPECT_TRUE(selected.wasSelectionChangedThisFrame);

    FrameUVE({50.0F, 98.0F}, true);
    const Scene::UIDropdownComponentUVE& settled =
        entityManager.GetComponentUVE<Scene::UIDropdownComponentUVE>(menu);
    EXPECT_EQ(settled.selectedIndex, 2);
    EXPECT_FALSE(settled.wasSelectionChangedThisFrame);
    EXPECT_EQ(runtime.GetDrawBatchUVE().quads.size(), 5U) << "the closed box now shows Hard";
}

TEST_F(UIDropdownUVETest, ClickOutside_ClosesWithoutChanging) {
    const Scene::EntityUVE menu =
        MakeDropdownUVE(Math::RectUVE{{10.0F, 10.0F}, {160.0F, 28.0F}}, "Easy\nNormal\nHard", 1);
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({50.0F, 20.0F}, true);
    FrameUVE({50.0F, 20.0F}, false);
    FrameUVE({700.0F, 500.0F}, true);

    const Scene::UIDropdownComponentUVE& closed =
        entityManager.GetComponentUVE<Scene::UIDropdownComponentUVE>(menu);
    EXPECT_FALSE(closed.open);
    EXPECT_EQ(closed.selectedIndex, 1);
    EXPECT_FALSE(closed.wasSelectionChangedThisFrame);
}

TEST_F(UIDropdownUVETest, ClickBoxWhileOpen_TogglesClosed) {
    const Scene::EntityUVE menu =
        MakeDropdownUVE(Math::RectUVE{{10.0F, 10.0F}, {160.0F, 28.0F}}, "Easy\nNormal\nHard", 0);
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({50.0F, 20.0F}, true);
    FrameUVE({50.0F, 20.0F}, false);
    FrameUVE({50.0F, 20.0F}, true);

    const Scene::UIDropdownComponentUVE& toggled =
        entityManager.GetComponentUVE<Scene::UIDropdownComponentUVE>(menu);
    EXPECT_FALSE(toggled.open);
    EXPECT_EQ(toggled.selectedIndex, 0);
    EXPECT_FALSE(toggled.wasSelectionChangedThisFrame);
}

TEST_F(UIDropdownUVETest, RowHover_HighlightsHoveredRow) {
    const Scene::EntityUVE menu =
        MakeDropdownUVE(Math::RectUVE{{10.0F, 10.0F}, {160.0F, 28.0F}}, "Easy\nNormal\nHard", 0);
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({50.0F, 20.0F}, true);
    FrameUVE({50.0F, 20.0F}, false);
    FrameUVE({50.0F, 74.0F}, false);

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIDropdownComponentUVE>(menu).hoveredIndex, 1);
    const Scene::UIDropdownComponentUVE& hovered =
        entityManager.GetComponentUVE<Scene::UIDropdownComponentUVE>(menu);
    std::size_t hoverHighlights = 0U;
    for (const UIQuadUVE& quad : runtime.GetDrawBatchUVE().quads) {
        if (quad.kind == UIDrawItemKindUVE::SolidColor && quad.color == hovered.optionHoverColor) {
            ++hoverHighlights;
            EXPECT_EQ(quad.rect, (Math::RectUVE{{10.0F, 62.0F}, {160.0F, 24.0F}}));
        }
    }
    EXPECT_EQ(hoverHighlights, 1U);
}

TEST_F(UIDropdownUVETest, Popup_FlipsUpWhenNoRoomBelow) {
    const Scene::EntityUVE menu =
        MakeDropdownUVE(Math::RectUVE{{10.0F, 580.0F}, {160.0F, 20.0F}}, "Easy\nNormal\nHard", 0);
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({50.0F, 590.0F}, true);
    FrameUVE({50.0F, 590.0F}, false);

    const std::vector<UIQuadUVE>& quads = runtime.GetDrawBatchUVE().quads;
    ASSERT_GE(quads.size(), 6U);
    EXPECT_FLOAT_EQ(quads[5].rect.position.y, 508.0F) << "580 minus three 24px rows";

    FrameUVE({50.0F, 520.0F}, false);
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIDropdownComponentUVE>(menu).hoveredIndex, 0);
}

TEST_F(UIDropdownUVETest, Layout_DropdownStacksAndAnchorsLikeButtons) {
    const Scene::EntityUVE container = entityManager.CreateEntityUVE();
    Scene::UILayoutContainerComponentUVE layout;
    layout.rect = Math::RectUVE{{0.0F, 0.0F}, {300.0F, 300.0F}};
    layout.padding = 10.0F;
    entityManager.AddComponentUVE<Scene::UILayoutContainerComponentUVE>(container, layout);
    const Scene::EntityUVE stacked =
        MakeDropdownUVE(Math::RectUVE{{0.0F, 0.0F}, {160.0F, 28.0F}}, "A\nB", 0);
    Scene::HierarchyComponentUVE link;
    link.parent = container;
    link.siblingOrder = 0;
    entityManager.AddComponentUVE<Scene::HierarchyComponentUVE>(stacked, link);
    const Scene::EntityUVE stretched =
        MakeDropdownUVE(Math::RectUVE{{0.0F, 0.0F}, {160.0F, 28.0F}}, "A\nB", 0);
    entityManager.AddComponentUVE<Scene::UIAnchorComponentUVE>(stretched, Scene::UIAnchorComponentUVE{});
    runtime.SetViewportSizeUVE({400.0F, 300.0F});

    FrameUVE({350.0F, 250.0F}, false);

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIDropdownComponentUVE>(stacked).rect.position,
              (Math::Vector2UVE{10.0F, 10.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIDropdownComponentUVE>(stretched).rect,
              (Math::RectUVE{{0.0F, 0.0F}, {400.0F, 300.0F}}));
}

TEST_F(UIDropdownUVETest, SaveThenLoad_Dropdown_RoundTrip) {
    const Scene::EntityUVE menu = entityManager.CreateEntityUVE();
    Scene::UIDropdownComponentUVE dropdown;
    dropdown.rect = Math::RectUVE{{10.0F, 20.0F}, {160.0F, 28.0F}};
    dropdown.options = "Easy\nNormal\nHard";
    dropdown.selectedIndex = 2;
    dropdown.placeholder = "Pick one";
    dropdown.fontSize = 20.0F;
    dropdown.textColor = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    dropdown.open = true;
    dropdown.isHovered = true;
    dropdown.hoveredIndex = 5;
    dropdown.wasSelectionChangedThisFrame = true;
    entityManager.AddComponentUVE<Scene::UIDropdownComponentUVE>(menu, dropdown);

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_dropdown.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {menu}, path, Scene::SceneAssetTypeUVE::Scene));

    Scene::EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<Scene::EntityUVE> loaded = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(loaded.size(), 1U);
    const Scene::UIDropdownComponentUVE& restored =
        loadedManager.GetComponentUVE<Scene::UIDropdownComponentUVE>(loaded[0]);
    EXPECT_EQ(restored.options, "Easy\nNormal\nHard");
    EXPECT_EQ(restored.selectedIndex, 2);
    EXPECT_EQ(restored.placeholder, "Pick one");
    EXPECT_FLOAT_EQ(restored.fontSize, 20.0F);
    EXPECT_EQ(restored.textColor, (Math::Vector3UVE{1.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(restored.open);
    EXPECT_FALSE(restored.isHovered);
    EXPECT_EQ(restored.hoveredIndex, -1);
    EXPECT_FALSE(restored.wasSelectionChangedThisFrame);

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::UI::Tests
