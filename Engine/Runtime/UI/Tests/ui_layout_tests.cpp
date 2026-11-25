// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_layout.h"

#include <cstdint>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "entity.h"
#include "hierarchy_component.h"
#include "ui_anchor_component.h"
#include "ui_button_component.h"
#include "ui_image_component.h"
#include "ui_layout_container_component.h"
#include "ui_text_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "input_system.h"
#include "rect.h"
#include "vector2.h"
#include "memory_manager.h"
#include "ui_anchors.h"
#include "ui_runtime.h"

namespace UVE::UI::Tests {
namespace {

const Math::Vector2 kAuthoredElsewhere{400.0F, 300.0F};

class UILayoutTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Input::InputSystem inputSystem{eventSystem};
    UIRuntime runtime;

    void Link(const Scene::Entity entity, const Scene::Entity parent, const std::int64_t order) {
        Scene::HierarchyComponent link;
        link.parent = parent;
        link.siblingOrder = order;
        entityManager.AddComponent<Scene::HierarchyComponent>(entity, link);
    }

    Scene::Entity MakeContainer(const Math::Rect rect, const Scene::UILayoutDirection direction,
                                      const Scene::UILayoutAlignment alignment, const float padding,
                                      const float spacing, const Scene::Entity parent = Scene::kInvalidEntity,
                                      const std::int64_t order = 0, const std::uint32_t wrapAfter = 0U,
                                      const bool autoSizeWidth = false, const bool autoSizeHeight = false) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UILayoutContainerComponent container;
        container.rect = rect;
        container.direction = direction;
        container.alignment = alignment;
        container.padding = padding;
        container.spacing = spacing;
        container.wrapAfter = wrapAfter;
        container.autoSizeWidth = autoSizeWidth;
        container.autoSizeHeight = autoSizeHeight;
        entityManager.AddComponent<Scene::UILayoutContainerComponent>(entity, container);
        Link(entity, parent, order);
        return entity;
    }

    Scene::Entity MakeButton(const Scene::Entity parent, const std::int64_t order,
                                   const Math::Vector2 size) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UIButtonComponent button;
        button.rect.position = kAuthoredElsewhere;
        button.rect.size = size;
        entityManager.AddComponent<Scene::UIButtonComponent>(entity, button);
        Link(entity, parent, order);
        return entity;
    }

    Scene::Entity MakeImage(const Scene::Entity parent, const std::int64_t order,
                                  const Math::Vector2 size) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UIImageComponent image;
        image.rect.position = kAuthoredElsewhere;
        image.rect.size = size;
        entityManager.AddComponent<Scene::UIImageComponent>(entity, image);
        Link(entity, parent, order);
        return entity;
    }

    Scene::Entity MakeText(const Scene::Entity parent, const std::int64_t order, const float fontSize) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UITextComponent text;
        text.text = "Play";
        text.positionPixels = kAuthoredElsewhere;
        text.fontSize = fontSize;
        entityManager.AddComponent<Scene::UITextComponent>(entity, text);
        Link(entity, parent, order);
        return entity;
    }
};

TEST_F(UILayoutTest, Container_ValidatorRejectsNegativeSizesAndUnknownEnums) {
    Scene::UILayoutContainerComponent container;
    EXPECT_TRUE(IsUILayoutContainerComponentValid(container));
    container.spacing = -1.0F;
    EXPECT_FALSE(IsUILayoutContainerComponentValid(container));
    container.spacing = 0.0F;
    container.rect.size.x = -10.0F;
    EXPECT_FALSE(IsUILayoutContainerComponentValid(container));
    container.rect.size.x = 320.0F;
    container.direction = static_cast<Scene::UILayoutDirection>(7);
    EXPECT_FALSE(IsUILayoutContainerComponentValid(container));
}

TEST_F(UILayoutTest, VerticalStack_PositionsChildrenWithPaddingAndSpacing) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{10.0F, 20.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirection::Vertical,
                         Scene::UILayoutAlignment::Start, 5.0F, 10.0F);
    const Scene::Entity first = MakeButton(container, 0, {100.0F, 30.0F});
    const Scene::Entity second = MakeImage(container, 1, {50.0F, 20.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(first).rect.position,
              (Math::Vector2{15.0F, 25.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(first).rect.size,
              (Math::Vector2{100.0F, 30.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIImageComponent>(second).rect.position,
              (Math::Vector2{15.0F, 65.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIImageComponent>(second).rect.size,
              (Math::Vector2{50.0F, 20.0F}));
}

TEST_F(UILayoutTest, HorizontalStack_CentersChildrenOnCrossAxis) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {300.0F, 100.0F}}, Scene::UILayoutDirection::Horizontal,
                         Scene::UILayoutAlignment::Center, 10.0F, 5.0F);
    const Scene::Entity first = MakeButton(container, 0, {60.0F, 20.0F});
    const Scene::Entity second = MakeButton(container, 1, {60.0F, 30.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(first).rect.position,
              (Math::Vector2{10.0F, 40.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(second).rect.position,
              (Math::Vector2{75.0F, 35.0F}));
}

TEST_F(UILayoutTest, EndAlignment_ClampsOversizedChildToInnerEdge) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {100.0F, 200.0F}}, Scene::UILayoutDirection::Vertical,
                         Scene::UILayoutAlignment::End, 0.0F, 4.0F);
    const Scene::Entity wide = MakeImage(container, 0, {150.0F, 10.0F});
    const Scene::Entity narrow = MakeImage(container, 1, {40.0F, 10.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    EXPECT_EQ(entityManager.GetComponent<Scene::UIImageComponent>(wide).rect.position,
              (Math::Vector2{0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIImageComponent>(narrow).rect.position,
              (Math::Vector2{60.0F, 14.0F}));
}

TEST_F(UILayoutTest, NestedContainers_InnerLaysOutAfterOuterPositionsIt) {
    const Scene::Entity outer =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {400.0F, 400.0F}}, Scene::UILayoutDirection::Vertical,
                         Scene::UILayoutAlignment::Start, 20.0F, 0.0F);
    const Scene::Entity top = MakeButton(outer, 0, {100.0F, 50.0F});
    const Scene::Entity inner =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {200.0F, 100.0F}}, Scene::UILayoutDirection::Horizontal,
                         Scene::UILayoutAlignment::Start, 5.0F, 5.0F, outer, 1);
    const Scene::Entity leaf = MakeButton(inner, 0, {40.0F, 20.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    // The outer stack puts the button at (20, 20) and the inner container below it at (20, 70);
    // the inner stack then offsets its own child by its padding: (25, 75).
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(top).rect.position,
              (Math::Vector2{20.0F, 20.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UILayoutContainerComponent>(inner).rect.position,
              (Math::Vector2{20.0F, 70.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(leaf).rect.position,
              (Math::Vector2{25.0F, 75.0F}));
}

TEST_F(UILayoutTest, SiblingOrderDecidesSequenceRegardlessOfCreationOrder) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirection::Vertical,
                         Scene::UILayoutAlignment::Start, 0.0F, 10.0F);
    const Scene::Entity createdFirst = MakeButton(container, 10, {50.0F, 20.0F});
    const Scene::Entity createdSecond = MakeButton(container, 0, {50.0F, 20.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(createdSecond).rect.position,
              (Math::Vector2{0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(createdFirst).rect.position,
              (Math::Vector2{0.0F, 30.0F}));
}

TEST_F(UILayoutTest, InvalidContainerIsSkippedEntirely) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirection::Vertical,
                         Scene::UILayoutAlignment::Start, 0.0F, -5.0F);
    const Scene::Entity child = MakeButton(container, 0, {50.0F, 20.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(child).rect.position, kAuthoredElsewhere);
}

TEST_F(UILayoutTest, NonWidgetChildrenAreIgnoredNotSpaced) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirection::Vertical,
                         Scene::UILayoutAlignment::Start, 0.0F, 10.0F);
    const Scene::Entity first = MakeButton(container, 0, {50.0F, 20.0F});
    const Scene::Entity bare = entityManager.CreateEntity();
    Link(bare, container, 1);
    const Scene::Entity second = MakeButton(container, 2, {50.0F, 20.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(first).rect.position,
              (Math::Vector2{0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(second).rect.position,
              (Math::Vector2{0.0F, 30.0F}));
}

TEST_F(UILayoutTest, TextAdvancesTheStackByFontSize) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirection::Vertical,
                         Scene::UILayoutAlignment::Start, 0.0F, 0.0F);
    const Scene::Entity label = MakeText(container, 0, 16.0F);
    const Scene::Entity below = MakeButton(container, 1, {50.0F, 20.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    EXPECT_EQ(entityManager.GetComponent<Scene::UITextComponent>(label).positionPixels,
              (Math::Vector2{0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(below).rect.position,
              (Math::Vector2{0.0F, 16.0F}));
}

TEST_F(UILayoutTest, TickIntegration_LaidOutButtonHitTestsAtItsLaidOutRect) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{100.0F, 100.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirection::Vertical,
                         Scene::UILayoutAlignment::Start, 10.0F, 0.0F);
    const Scene::Entity child = MakeButton(container, 0, {120.0F, 32.0F});

    // The click lands inside the laid-out rect (110, 110)-(230, 142), far from anywhere authored.
    inputSystem.SetMousePosition(Math::Vector2{150.0F, 120.0F});
    inputSystem.SetMouseButtonState(Input::MouseButton::Left, true);
    inputSystem.Update();

    runtime.Tick(entityManager, inputSystem);

    const Scene::UIButtonComponent& updated = entityManager.GetComponent<Scene::UIButtonComponent>(child);
    EXPECT_EQ(updated.rect.position, (Math::Vector2{110.0F, 110.0F}));
    EXPECT_TRUE(updated.isHovered);
    EXPECT_TRUE(updated.wasClickedThisFrame);
}

TEST_F(UILayoutTest, HorizontalWrap_FlowsAcrossThenDownWithPartialLastLine) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {400.0F, 400.0F}}, Scene::UILayoutDirection::Horizontal,
                         Scene::UILayoutAlignment::Start, 0.0F, 5.0F, Scene::kInvalidEntity, 0, 2U);
    const Scene::Entity first = MakeButton(container, 0, {60.0F, 20.0F});
    const Scene::Entity second = MakeButton(container, 1, {60.0F, 20.0F});
    const Scene::Entity third = MakeButton(container, 2, {60.0F, 20.0F});
    const Scene::Entity fourth = MakeButton(container, 3, {60.0F, 20.0F});
    const Scene::Entity fifth = MakeButton(container, 4, {60.0F, 20.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(first).rect.position,
              (Math::Vector2{0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(second).rect.position,
              (Math::Vector2{65.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(third).rect.position,
              (Math::Vector2{0.0F, 25.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(fourth).rect.position,
              (Math::Vector2{65.0F, 25.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(fifth).rect.position,
              (Math::Vector2{0.0F, 50.0F}));
}

TEST_F(UILayoutTest, VerticalWrap_FlowsDownThenAcross) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {400.0F, 400.0F}}, Scene::UILayoutDirection::Vertical,
                         Scene::UILayoutAlignment::Start, 10.0F, 4.0F, Scene::kInvalidEntity, 0, 2U);
    const Scene::Entity first = MakeButton(container, 0, {50.0F, 30.0F});
    const Scene::Entity second = MakeButton(container, 1, {50.0F, 30.0F});
    const Scene::Entity third = MakeButton(container, 2, {50.0F, 30.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(first).rect.position,
              (Math::Vector2{10.0F, 10.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(second).rect.position,
              (Math::Vector2{10.0F, 44.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(third).rect.position,
              (Math::Vector2{64.0F, 10.0F}));
}

TEST_F(UILayoutTest, Wrap_AlignsSmallerChildrenWithinUniformCells) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {400.0F, 400.0F}}, Scene::UILayoutDirection::Horizontal,
                         Scene::UILayoutAlignment::Center, 0.0F, 10.0F, Scene::kInvalidEntity, 0, 2U);
    const Scene::Entity large = MakeButton(container, 0, {100.0F, 30.0F});
    const Scene::Entity small = MakeButton(container, 1, {40.0F, 10.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    // Cells are (100, 30): the small child starts a full cell over and centers in its own.
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(large).rect.position,
              (Math::Vector2{0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(small).rect.position,
              (Math::Vector2{110.0F, 10.0F}));
}

TEST_F(UILayoutTest, Wrap_IgnoresNonWidgetChildrenInCellMeasureAndSlots) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {400.0F, 400.0F}}, Scene::UILayoutDirection::Vertical,
                         Scene::UILayoutAlignment::Start, 0.0F, 10.0F, Scene::kInvalidEntity, 0, 2U);
    const Scene::Entity first = MakeButton(container, 0, {50.0F, 20.0F});
    const Scene::Entity bare = entityManager.CreateEntity();
    Link(bare, container, 1);
    const Scene::Entity second = MakeButton(container, 2, {50.0F, 20.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    // The bare entity takes no slot: both buttons share the first line, packed as a grid.
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(first).rect.position,
              (Math::Vector2{0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(second).rect.position,
              (Math::Vector2{0.0F, 30.0F}));
}

TEST_F(UILayoutTest, MeasureTextWidth_MatchesThePenWalkOfAppendTextQuads) {
    const UIFontAtlas& atlas = runtime.GetFontAtlas();
    ASSERT_TRUE(atlas.IsValid());
    float cursorX = 0.0F;
    float cursorY = 0.0F;
    std::vector<UIGlyphQuad> quads;
    atlas.AppendTextQuads("Play", cursorX, cursorY, 16.0F, quads);
    EXPECT_FLOAT_EQ(atlas.MeasureTextWidth("Play", 16.0F), cursorX);
    // Unrenderable characters walk neither the pen nor the measure.
    EXPECT_FLOAT_EQ(atlas.MeasureTextWidth("P\nl" "\x7F" "ay", 16.0F),
                    atlas.MeasureTextWidth("Play", 16.0F));
    EXPECT_EQ(atlas.MeasureTextWidth("", 16.0F), 0.0F);
    EXPECT_EQ(atlas.MeasureTextWidth("Play", 0.0F), 0.0F);
    EXPECT_EQ(atlas.MeasureTextWidth("Play", -2.0F), 0.0F);
}

TEST_F(UILayoutTest, HorizontalStack_TextAdvancesByMeasuredWidth) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {400.0F, 100.0F}}, Scene::UILayoutDirection::Horizontal,
                         Scene::UILayoutAlignment::Start, 0.0F, 5.0F);
    MakeText(container, 0, 16.0F);
    const Scene::Entity after = MakeButton(container, 1, {50.0F, 20.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    const float expectedX = runtime.GetFontAtlas().MeasureTextWidth("Play", 16.0F) + 5.0F;
    EXPECT_GT(expectedX, 5.0F); // guards against a silently-zero measure passing vacuously
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(after).rect.position.x, expectedX);
}

TEST_F(UILayoutTest, AutoSizeWidth_FitsWidestChildPlusPadding) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{5.0F, 5.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirection::Vertical,
                         Scene::UILayoutAlignment::Start, 10.0F, 5.0F, Scene::kInvalidEntity, 0, 0U, true,
                         false);
    MakeButton(container, 0, {100.0F, 20.0F});
    MakeButton(container, 1, {60.0F, 30.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    const Scene::UILayoutContainerComponent& resized =
        entityManager.GetComponent<Scene::UILayoutContainerComponent>(container);
    EXPECT_EQ(resized.rect.position, (Math::Vector2{5.0F, 5.0F}));
    EXPECT_EQ(resized.rect.size, (Math::Vector2{120.0F, 200.0F}));
}

TEST_F(UILayoutTest, AutoSizeHeight_SumsChildrenSpacingAndPadding) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirection::Vertical,
                         Scene::UILayoutAlignment::Start, 10.0F, 5.0F, Scene::kInvalidEntity, 0, 0U, false,
                         true);
    MakeButton(container, 0, {100.0F, 20.0F});
    MakeButton(container, 1, {60.0F, 30.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    EXPECT_EQ(entityManager.GetComponent<Scene::UILayoutContainerComponent>(container).rect.size,
              (Math::Vector2{200.0F, 75.0F}));
}

TEST_F(UILayoutTest, AutoSize_EmptyContainerKeepsPaddingOnly) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirection::Vertical,
                         Scene::UILayoutAlignment::Start, 8.0F, 5.0F, Scene::kInvalidEntity, 0, 0U, true,
                         true);

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    EXPECT_EQ(entityManager.GetComponent<Scene::UILayoutContainerComponent>(container).rect.size,
              (Math::Vector2{16.0F, 16.0F}));
}

TEST_F(UILayoutTest, AutoSize_NestedInnerSizesBeforeOuterMeasures) {
    const Scene::Entity outer =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {400.0F, 400.0F}}, Scene::UILayoutDirection::Vertical,
                         Scene::UILayoutAlignment::Start, 20.0F, 0.0F, Scene::kInvalidEntity, 0, 0U, true,
                         true);
    const Scene::Entity inner =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {10.0F, 10.0F}}, Scene::UILayoutDirection::Horizontal,
                         Scene::UILayoutAlignment::Start, 5.0F, 5.0F, outer, 0, 0U, true, true);
    const Scene::Entity leaf = MakeButton(inner, 0, {40.0F, 20.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    EXPECT_EQ(entityManager.GetComponent<Scene::UILayoutContainerComponent>(inner).rect.size,
              (Math::Vector2{50.0F, 30.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UILayoutContainerComponent>(outer).rect.size,
              (Math::Vector2{90.0F, 70.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UILayoutContainerComponent>(inner).rect.position,
              (Math::Vector2{20.0F, 20.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(leaf).rect.position,
              (Math::Vector2{25.0F, 25.0F}));
}

TEST_F(UILayoutTest, AutoSize_GridFitsCellsLinesAndSpacing) {
    const Scene::Entity container =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {400.0F, 400.0F}}, Scene::UILayoutDirection::Vertical,
                         Scene::UILayoutAlignment::Start, 10.0F, 4.0F, Scene::kInvalidEntity, 0, 2U, true,
                         true);
    MakeButton(container, 0, {50.0F, 30.0F});
    MakeButton(container, 1, {50.0F, 30.0F});
    MakeButton(container, 2, {50.0F, 30.0F});

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    // Two 30-tall slots down the main axis, two 50-wide lines across, spacing throughout.
    EXPECT_EQ(entityManager.GetComponent<Scene::UILayoutContainerComponent>(container).rect.size,
              (Math::Vector2{124.0F, 84.0F}));
}

TEST_F(UILayoutTest, AutoSize_AnchoredStretchChildSettlesOneShotFromPreLayoutSize) {
    const Scene::Entity parent =
        MakeContainer(Math::Rect{{0.0F, 0.0F}, {50.0F, 50.0F}}, Scene::UILayoutDirection::Vertical,
                         Scene::UILayoutAlignment::Start, 0.0F, 0.0F, Scene::kInvalidEntity, 0, 0U, true,
                         true);
    const Scene::Entity child = MakeButton(parent, 0, {10.0F, 10.0F});
    entityManager.AddComponent<Scene::UIAnchorComponent>(child, Scene::UIAnchorComponent{});

    for (int frame = 0; frame < 2; ++frame) {
        ResolveUIAnchors(entityManager, {400.0F, 300.0F});
        LayoutUIContainers(entityManager, runtime.GetFontAtlas());
    }

    // The anchor saw the pre-layout 50x50; the parent then fit the resolved child - stable across
    // frames because nothing iterates.
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(child).rect.size,
              (Math::Vector2{50.0F, 50.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UILayoutContainerComponent>(parent).rect.size,
              (Math::Vector2{50.0F, 50.0F}));
}

} // namespace
} // namespace UVE::UI::Tests
