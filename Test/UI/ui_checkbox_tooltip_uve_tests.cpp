// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/ui_anchor_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_checkbox_component_uve.h"
#include "uve/component/ui_layout_container_component_uve.h"
#include "uve/component/ui_tooltip_component_uve.h"
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

class UICheckboxTooltipUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Input::InputSystemUVE inputSystem{eventSystem};
    UIRuntimeUVE runtime;
    Scene::SceneSerializerUVE serializer;

    void FrameUVE(const Math::Vector2UVE mousePosition, const bool mouseDown, const float dt) {
        inputSystem.SetMousePositionUVE(mousePosition);
        inputSystem.SetMouseButtonStateUVE(Input::MouseButtonUVE::Left, mouseDown);
        inputSystem.UpdateUVE();
        runtime.SetDeltaTimeUVE(dt);
        runtime.TickUVE(entityManager, inputSystem);
    }

    Scene::EntityUVE MakeCheckboxUVE(const Math::RectUVE rect) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UICheckboxComponentUVE checkbox;
        checkbox.rect = rect;
        entityManager.AddComponentUVE<Scene::UICheckboxComponentUVE>(entity, checkbox);
        return entity;
    }

    Scene::EntityUVE MakeButtonUVE(const Math::RectUVE rect) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UIButtonComponentUVE button;
        button.rect = rect;
        entityManager.AddComponentUVE<Scene::UIButtonComponentUVE>(entity, button);
        return entity;
    }
};

TEST_F(UICheckboxTooltipUVETest, Checkbox_ValidatorAcceptsDefaultsRejectsBadRects) {
    Scene::UICheckboxComponentUVE checkbox;
    EXPECT_TRUE(IsUICheckboxComponentValidUVE(checkbox));
    checkbox.rect.size = {0.0F, 20.0F};
    EXPECT_FALSE(IsUICheckboxComponentValidUVE(checkbox));
    checkbox.rect.size = {20.0F, 20.0F};
    checkbox.checkColor = {1.0F, 1.0F, 1.0F};
    EXPECT_TRUE(IsUICheckboxComponentValidUVE(checkbox));
}

TEST_F(UICheckboxTooltipUVETest, Click_TogglesCheckedAndSignalsExactlyOnce) {
    const Scene::EntityUVE box = MakeCheckboxUVE(Math::RectUVE{{10.0F, 10.0F}, {20.0F, 20.0F}});

    FrameUVE({20.0F, 20.0F}, true, 0.016F);
    const Scene::UICheckboxComponentUVE& pressed =
        entityManager.GetComponentUVE<Scene::UICheckboxComponentUVE>(box);
    EXPECT_TRUE(pressed.checked);
    EXPECT_TRUE(pressed.isHovered);
    EXPECT_TRUE(pressed.wasToggledThisFrame);

    FrameUVE({20.0F, 20.0F}, true, 0.016F);
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::UICheckboxComponentUVE>(box).checked);
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::UICheckboxComponentUVE>(box).wasToggledThisFrame);

    FrameUVE({20.0F, 20.0F}, false, 0.016F);
    FrameUVE({20.0F, 20.0F}, true, 0.016F);
    const Scene::UICheckboxComponentUVE& flipped =
        entityManager.GetComponentUVE<Scene::UICheckboxComponentUVE>(box);
    EXPECT_FALSE(flipped.checked);
    EXPECT_TRUE(flipped.wasToggledThisFrame);
}

TEST_F(UICheckboxTooltipUVETest, ClickOutside_DoesNothingButStillDraws) {
    const Scene::EntityUVE box = MakeCheckboxUVE(Math::RectUVE{{10.0F, 10.0F}, {20.0F, 20.0F}});

    FrameUVE({200.0F, 200.0F}, true, 0.016F);

    const Scene::UICheckboxComponentUVE& updated =
        entityManager.GetComponentUVE<Scene::UICheckboxComponentUVE>(box);
    EXPECT_FALSE(updated.checked);
    EXPECT_FALSE(updated.isHovered);
    EXPECT_FALSE(updated.wasToggledThisFrame);
    ASSERT_EQ(runtime.GetDrawBatchUVE().quads.size(), 1U);
    EXPECT_EQ(runtime.GetDrawBatchUVE().quads[0].color, updated.boxColor);
}

TEST_F(UICheckboxTooltipUVETest, Checked_DrawsBoxAndInsetCheck) {
    const Scene::EntityUVE box = MakeCheckboxUVE(Math::RectUVE{{10.0F, 10.0F}, {20.0F, 20.0F}});
    entityManager.GetComponentUVE<Scene::UICheckboxComponentUVE>(box).checked = true;

    FrameUVE({200.0F, 200.0F}, false, 0.016F);

    const std::vector<UIQuadUVE>& quads = runtime.GetDrawBatchUVE().quads;
    ASSERT_EQ(quads.size(), 2U);
    EXPECT_EQ(quads[0].rect, (Math::RectUVE{{10.0F, 10.0F}, {20.0F, 20.0F}}));
    EXPECT_EQ(quads[1].rect, (Math::RectUVE{{15.0F, 15.0F}, {10.0F, 10.0F}}));
    EXPECT_EQ(quads[1].color,
              entityManager.GetComponentUVE<Scene::UICheckboxComponentUVE>(box).checkColor);
}

TEST_F(UICheckboxTooltipUVETest, Layout_CheckboxStacksAndAnchorsLikeButtons) {
    const Scene::EntityUVE container = entityManager.CreateEntityUVE();
    Scene::UILayoutContainerComponentUVE layout;
    layout.rect = Math::RectUVE{{0.0F, 0.0F}, {300.0F, 300.0F}};
    layout.padding = 10.0F;
    entityManager.AddComponentUVE<Scene::UILayoutContainerComponentUVE>(container, layout);
    const Scene::EntityUVE stacked = MakeCheckboxUVE(Math::RectUVE{{0.0F, 0.0F}, {20.0F, 20.0F}});
    Scene::HierarchyComponentUVE link;
    link.parent = container;
    link.siblingOrder = 0;
    entityManager.AddComponentUVE<Scene::HierarchyComponentUVE>(stacked, link);
    const Scene::EntityUVE stretched = MakeCheckboxUVE(Math::RectUVE{{0.0F, 0.0F}, {20.0F, 20.0F}});
    entityManager.AddComponentUVE<Scene::UIAnchorComponentUVE>(stretched, Scene::UIAnchorComponentUVE{});
    runtime.SetViewportSizeUVE({400.0F, 300.0F});

    FrameUVE({350.0F, 250.0F}, false, 0.016F);

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UICheckboxComponentUVE>(stacked).rect.position,
              (Math::Vector2UVE{10.0F, 10.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UICheckboxComponentUVE>(stretched).rect,
              (Math::RectUVE{{0.0F, 0.0F}, {400.0F, 300.0F}}));
}

TEST_F(UICheckboxTooltipUVETest, Tooltip_ValidatorRejectsOverlongTextAndBadTiming) {
    Scene::UITooltipComponentUVE tooltip;
    tooltip.text = "Save";
    EXPECT_TRUE(IsUITooltipComponentValidUVE(tooltip));
    tooltip.text = std::string(513U, 'x');
    EXPECT_FALSE(IsUITooltipComponentValidUVE(tooltip));
    tooltip.text = "Save";
    tooltip.delay = -1.0F;
    EXPECT_FALSE(IsUITooltipComponentValidUVE(tooltip));
    tooltip.delay = 0.5F;
    tooltip.fontSize = 0.0F;
    EXPECT_FALSE(IsUITooltipComponentValidUVE(tooltip));
}

TEST_F(UICheckboxTooltipUVETest, Tooltip_AppearsAfterDelayNearCursor) {
    const Scene::EntityUVE button = MakeButtonUVE(Math::RectUVE{{100.0F, 100.0F}, {200.0F, 30.0F}});
    Scene::UITooltipComponentUVE tooltip;
    tooltip.text = "Save";
    entityManager.AddComponentUVE<Scene::UITooltipComponentUVE>(button, tooltip);
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({150.0F, 115.0F}, false, 0.3F);
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::UITooltipComponentUVE>(button).visibleThisFrame);
    EXPECT_EQ(runtime.GetDrawBatchUVE().quads.size(), 1U);

    FrameUVE({150.0F, 115.0F}, false, 0.3F);
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::UITooltipComponentUVE>(button).visibleThisFrame);
    const std::vector<UIQuadUVE>& quads = runtime.GetDrawBatchUVE().quads;
    ASSERT_EQ(quads.size(), 6U); // the button, the popup background, four glyphs
    EXPECT_EQ(quads[1].rect.position, (Math::Vector2UVE{162.0F, 127.0F}));
    const float expectedWidth = runtime.GetFontAtlasUVE().MeasureTextWidthUVE("Save", 16.0F) + 8.0F;
    EXPECT_FLOAT_EQ(quads[1].rect.size.x, expectedWidth);
    EXPECT_FLOAT_EQ(quads[1].rect.size.y, 24.0F);
    for (std::size_t i = 2U; i < 6U; ++i) {
        EXPECT_EQ(quads[i].kind, UIDrawItemKindUVE::Glyph);
    }
}

TEST_F(UICheckboxTooltipUVETest, Tooltip_HidesOnLeaveAndResetsTheClock) {
    const Scene::EntityUVE button = MakeButtonUVE(Math::RectUVE{{100.0F, 100.0F}, {200.0F, 30.0F}});
    Scene::UITooltipComponentUVE tooltip;
    tooltip.text = "Save";
    entityManager.AddComponentUVE<Scene::UITooltipComponentUVE>(button, tooltip);
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({150.0F, 115.0F}, false, 1.0F);
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::UITooltipComponentUVE>(button).visibleThisFrame);
    FrameUVE({10.0F, 10.0F}, false, 1.0F);
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::UITooltipComponentUVE>(button).visibleThisFrame);
    EXPECT_EQ(runtime.GetDrawBatchUVE().quads.size(), 1U);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UITooltipComponentUVE>(button).hoverTime, 0.0F);

    // Showing again costs another full delay - the interrupted 1.0s bought nothing.
    FrameUVE({150.0F, 115.0F}, false, 0.3F);
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::UITooltipComponentUVE>(button).visibleThisFrame);
}

TEST_F(UICheckboxTooltipUVETest, Tooltip_EmptyTextNeverShows) {
    const Scene::EntityUVE button = MakeButtonUVE(Math::RectUVE{{100.0F, 100.0F}, {200.0F, 30.0F}});
    entityManager.AddComponentUVE<Scene::UITooltipComponentUVE>(button, Scene::UITooltipComponentUVE{});
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({150.0F, 115.0F}, false, 5.0F);

    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::UITooltipComponentUVE>(button).visibleThisFrame);
    EXPECT_EQ(runtime.GetDrawBatchUVE().quads.size(), 1U);
}

TEST_F(UICheckboxTooltipUVETest, Tooltip_ClampsIntoViewport) {
    const Scene::EntityUVE button = MakeButtonUVE(Math::RectUVE{{700.0F, 550.0F}, {90.0F, 40.0F}});
    Scene::UITooltipComponentUVE tooltip;
    tooltip.text = "Save";
    entityManager.AddComponentUVE<Scene::UITooltipComponentUVE>(button, tooltip);
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({790.0F, 590.0F}, false, 1.0F);

    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::UITooltipComponentUVE>(button).visibleThisFrame);
    const Math::RectUVE popup = runtime.GetDrawBatchUVE().quads[1].rect;
    EXPECT_GE(popup.position.x, 0.0F);
    EXPECT_GE(popup.position.y, 0.0F);
    EXPECT_LE(popup.position.x + popup.size.x, 800.0F);
    EXPECT_LE(popup.position.y + popup.size.y, 600.0F);
}

TEST_F(UICheckboxTooltipUVETest, SaveThenLoad_CheckboxAndTooltip_RoundTrip) {
    const Scene::EntityUVE box = entityManager.CreateEntityUVE();
    Scene::UICheckboxComponentUVE checkbox;
    checkbox.rect = Math::RectUVE{{10.0F, 20.0F}, {24.0F, 24.0F}};
    checkbox.boxColor = Math::Vector3UVE{0.1F, 0.1F, 0.1F};
    checkbox.checkColor = Math::Vector3UVE{0.2F, 0.8F, 0.2F};
    checkbox.checked = true;
    checkbox.isHovered = true;
    checkbox.wasToggledThisFrame = true;
    entityManager.AddComponentUVE<Scene::UICheckboxComponentUVE>(box, checkbox);
    const Scene::EntityUVE tip = entityManager.CreateEntityUVE();
    Scene::UITooltipComponentUVE tooltip;
    tooltip.text = "Save the game";
    tooltip.delay = 1.5F;
    tooltip.offset = {20.0F, 20.0F};
    tooltip.padding = 6.0F;
    tooltip.fontSize = 20.0F;
    tooltip.backgroundColor = Math::Vector3UVE{0.05F, 0.05F, 0.05F};
    tooltip.hoverTime = 5.0F;
    tooltip.visibleThisFrame = true;
    entityManager.AddComponentUVE<Scene::UITooltipComponentUVE>(tip, tooltip);

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_checktip.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {box, tip}, path, Scene::SceneAssetTypeUVE::Scene));

    Scene::EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<Scene::EntityUVE> loaded = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(loaded.size(), 2U);
    const Scene::EntityUVE boxEntity =
        loadedManager.HasComponentUVE<Scene::UICheckboxComponentUVE>(loaded[0]) ? loaded[0] : loaded[1];
    const Scene::EntityUVE tipEntity = boxEntity == loaded[0] ? loaded[1] : loaded[0];
    const Scene::UICheckboxComponentUVE& restoredBox =
        loadedManager.GetComponentUVE<Scene::UICheckboxComponentUVE>(boxEntity);
    EXPECT_TRUE(restoredBox.checked);
    EXPECT_FLOAT_EQ(restoredBox.checkColor.y, 0.8F);
    EXPECT_FALSE(restoredBox.isHovered);
    EXPECT_FALSE(restoredBox.wasToggledThisFrame);
    const Scene::UITooltipComponentUVE& restoredTip =
        loadedManager.GetComponentUVE<Scene::UITooltipComponentUVE>(tipEntity);
    EXPECT_EQ(restoredTip.text, "Save the game");
    EXPECT_FLOAT_EQ(restoredTip.delay, 1.5F);
    EXPECT_FLOAT_EQ(restoredTip.fontSize, 20.0F);
    EXPECT_FLOAT_EQ(restoredTip.hoverTime, 0.0F);
    EXPECT_FALSE(restoredTip.visibleThisFrame);

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::UI::Tests
