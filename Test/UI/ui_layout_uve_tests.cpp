// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/ui_layout_uve.h"

#include <cstdint>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/ui_anchor_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_layout_container_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/input/input_system_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/ui/ui_anchors_uve.h"
#include "uve/ui/ui_runtime_uve.h"

namespace UVE::UI::Tests {
namespace {

const Math::Vector2UVE kAuthoredElsewhereUVE{400.0F, 300.0F};

class UILayoutUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Input::InputSystemUVE inputSystem{eventSystem};
    UIRuntimeUVE runtime;

    void LinkUVE(const Scene::EntityUVE entity, const Scene::EntityUVE parent, const std::int64_t order) {
        Scene::HierarchyComponentUVE link;
        link.parent = parent;
        link.siblingOrder = order;
        entityManager.AddComponentUVE<Scene::HierarchyComponentUVE>(entity, link);
    }

    Scene::EntityUVE MakeContainerUVE(const Math::RectUVE rect, const Scene::UILayoutDirectionUVE direction,
                                      const Scene::UILayoutAlignmentUVE alignment, const float padding,
                                      const float spacing, const Scene::EntityUVE parent = Scene::kInvalidEntityUVE,
                                      const std::int64_t order = 0, const std::uint32_t wrapAfter = 0U,
                                      const bool autoSizeWidth = false, const bool autoSizeHeight = false) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UILayoutContainerComponentUVE container;
        container.rect = rect;
        container.direction = direction;
        container.alignment = alignment;
        container.padding = padding;
        container.spacing = spacing;
        container.wrapAfter = wrapAfter;
        container.autoSizeWidth = autoSizeWidth;
        container.autoSizeHeight = autoSizeHeight;
        entityManager.AddComponentUVE<Scene::UILayoutContainerComponentUVE>(entity, container);
        LinkUVE(entity, parent, order);
        return entity;
    }

    Scene::EntityUVE MakeButtonUVE(const Scene::EntityUVE parent, const std::int64_t order,
                                   const Math::Vector2UVE size) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UIButtonComponentUVE button;
        button.rect.position = kAuthoredElsewhereUVE;
        button.rect.size = size;
        entityManager.AddComponentUVE<Scene::UIButtonComponentUVE>(entity, button);
        LinkUVE(entity, parent, order);
        return entity;
    }

    Scene::EntityUVE MakeImageUVE(const Scene::EntityUVE parent, const std::int64_t order,
                                  const Math::Vector2UVE size) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UIImageComponentUVE image;
        image.rect.position = kAuthoredElsewhereUVE;
        image.rect.size = size;
        entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(entity, image);
        LinkUVE(entity, parent, order);
        return entity;
    }

    Scene::EntityUVE MakeTextUVE(const Scene::EntityUVE parent, const std::int64_t order, const float fontSize) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UITextComponentUVE text;
        text.text = "Play";
        text.positionPixels = kAuthoredElsewhereUVE;
        text.fontSize = fontSize;
        entityManager.AddComponentUVE<Scene::UITextComponentUVE>(entity, text);
        LinkUVE(entity, parent, order);
        return entity;
    }
};

TEST_F(UILayoutUVETest, Container_ValidatorRejectsNegativeSizesAndUnknownEnums) {
    Scene::UILayoutContainerComponentUVE container;
    EXPECT_TRUE(IsUILayoutContainerComponentValidUVE(container));
    container.spacing = -1.0F;
    EXPECT_FALSE(IsUILayoutContainerComponentValidUVE(container));
    container.spacing = 0.0F;
    container.rect.size.x = -10.0F;
    EXPECT_FALSE(IsUILayoutContainerComponentValidUVE(container));
    container.rect.size.x = 320.0F;
    container.direction = static_cast<Scene::UILayoutDirectionUVE>(7);
    EXPECT_FALSE(IsUILayoutContainerComponentValidUVE(container));
}

TEST_F(UILayoutUVETest, VerticalStack_PositionsChildrenWithPaddingAndSpacing) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{10.0F, 20.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirectionUVE::Vertical,
                         Scene::UILayoutAlignmentUVE::Start, 5.0F, 10.0F);
    const Scene::EntityUVE first = MakeButtonUVE(container, 0, {100.0F, 30.0F});
    const Scene::EntityUVE second = MakeImageUVE(container, 1, {50.0F, 20.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(first).rect.position,
              (Math::Vector2UVE{15.0F, 25.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(first).rect.size,
              (Math::Vector2UVE{100.0F, 30.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIImageComponentUVE>(second).rect.position,
              (Math::Vector2UVE{15.0F, 65.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIImageComponentUVE>(second).rect.size,
              (Math::Vector2UVE{50.0F, 20.0F}));
}

TEST_F(UILayoutUVETest, HorizontalStack_CentersChildrenOnCrossAxis) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {300.0F, 100.0F}}, Scene::UILayoutDirectionUVE::Horizontal,
                         Scene::UILayoutAlignmentUVE::Center, 10.0F, 5.0F);
    const Scene::EntityUVE first = MakeButtonUVE(container, 0, {60.0F, 20.0F});
    const Scene::EntityUVE second = MakeButtonUVE(container, 1, {60.0F, 30.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(first).rect.position,
              (Math::Vector2UVE{10.0F, 40.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(second).rect.position,
              (Math::Vector2UVE{75.0F, 35.0F}));
}

TEST_F(UILayoutUVETest, EndAlignment_ClampsOversizedChildToInnerEdge) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 200.0F}}, Scene::UILayoutDirectionUVE::Vertical,
                         Scene::UILayoutAlignmentUVE::End, 0.0F, 4.0F);
    const Scene::EntityUVE wide = MakeImageUVE(container, 0, {150.0F, 10.0F});
    const Scene::EntityUVE narrow = MakeImageUVE(container, 1, {40.0F, 10.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIImageComponentUVE>(wide).rect.position,
              (Math::Vector2UVE{0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIImageComponentUVE>(narrow).rect.position,
              (Math::Vector2UVE{60.0F, 14.0F}));
}

TEST_F(UILayoutUVETest, NestedContainers_InnerLaysOutAfterOuterPositionsIt) {
    const Scene::EntityUVE outer =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {400.0F, 400.0F}}, Scene::UILayoutDirectionUVE::Vertical,
                         Scene::UILayoutAlignmentUVE::Start, 20.0F, 0.0F);
    const Scene::EntityUVE top = MakeButtonUVE(outer, 0, {100.0F, 50.0F});
    const Scene::EntityUVE inner =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 100.0F}}, Scene::UILayoutDirectionUVE::Horizontal,
                         Scene::UILayoutAlignmentUVE::Start, 5.0F, 5.0F, outer, 1);
    const Scene::EntityUVE leaf = MakeButtonUVE(inner, 0, {40.0F, 20.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    // The outer stack puts the button at (20, 20) and the inner container below it at (20, 70);
    // the inner stack then offsets its own child by its padding: (25, 75).
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(top).rect.position,
              (Math::Vector2UVE{20.0F, 20.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(inner).rect.position,
              (Math::Vector2UVE{20.0F, 70.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(leaf).rect.position,
              (Math::Vector2UVE{25.0F, 75.0F}));
}

TEST_F(UILayoutUVETest, SiblingOrderDecidesSequenceRegardlessOfCreationOrder) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirectionUVE::Vertical,
                         Scene::UILayoutAlignmentUVE::Start, 0.0F, 10.0F);
    const Scene::EntityUVE createdFirst = MakeButtonUVE(container, 10, {50.0F, 20.0F});
    const Scene::EntityUVE createdSecond = MakeButtonUVE(container, 0, {50.0F, 20.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(createdSecond).rect.position,
              (Math::Vector2UVE{0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(createdFirst).rect.position,
              (Math::Vector2UVE{0.0F, 30.0F}));
}

TEST_F(UILayoutUVETest, InvalidContainerIsSkippedEntirely) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirectionUVE::Vertical,
                         Scene::UILayoutAlignmentUVE::Start, 0.0F, -5.0F);
    const Scene::EntityUVE child = MakeButtonUVE(container, 0, {50.0F, 20.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(child).rect.position, kAuthoredElsewhereUVE);
}

TEST_F(UILayoutUVETest, NonWidgetChildrenAreIgnoredNotSpaced) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirectionUVE::Vertical,
                         Scene::UILayoutAlignmentUVE::Start, 0.0F, 10.0F);
    const Scene::EntityUVE first = MakeButtonUVE(container, 0, {50.0F, 20.0F});
    const Scene::EntityUVE bare = entityManager.CreateEntityUVE();
    LinkUVE(bare, container, 1);
    const Scene::EntityUVE second = MakeButtonUVE(container, 2, {50.0F, 20.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(first).rect.position,
              (Math::Vector2UVE{0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(second).rect.position,
              (Math::Vector2UVE{0.0F, 30.0F}));
}

TEST_F(UILayoutUVETest, TextAdvancesTheStackByFontSize) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirectionUVE::Vertical,
                         Scene::UILayoutAlignmentUVE::Start, 0.0F, 0.0F);
    const Scene::EntityUVE label = MakeTextUVE(container, 0, 16.0F);
    const Scene::EntityUVE below = MakeButtonUVE(container, 1, {50.0F, 20.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UITextComponentUVE>(label).positionPixels,
              (Math::Vector2UVE{0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(below).rect.position,
              (Math::Vector2UVE{0.0F, 16.0F}));
}

TEST_F(UILayoutUVETest, TickIntegration_LaidOutButtonHitTestsAtItsLaidOutRect) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{100.0F, 100.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirectionUVE::Vertical,
                         Scene::UILayoutAlignmentUVE::Start, 10.0F, 0.0F);
    const Scene::EntityUVE child = MakeButtonUVE(container, 0, {120.0F, 32.0F});

    // The click lands inside the laid-out rect (110, 110)-(230, 142), far from anywhere authored.
    inputSystem.SetMousePositionUVE(Math::Vector2UVE{150.0F, 120.0F});
    inputSystem.SetMouseButtonStateUVE(Input::MouseButtonUVE::Left, true);
    inputSystem.UpdateUVE();

    runtime.TickUVE(entityManager, inputSystem);

    const Scene::UIButtonComponentUVE& updated = entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(child);
    EXPECT_EQ(updated.rect.position, (Math::Vector2UVE{110.0F, 110.0F}));
    EXPECT_TRUE(updated.isHovered);
    EXPECT_TRUE(updated.wasClickedThisFrame);
}

TEST_F(UILayoutUVETest, HorizontalWrap_FlowsAcrossThenDownWithPartialLastLine) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {400.0F, 400.0F}}, Scene::UILayoutDirectionUVE::Horizontal,
                         Scene::UILayoutAlignmentUVE::Start, 0.0F, 5.0F, Scene::kInvalidEntityUVE, 0, 2U);
    const Scene::EntityUVE first = MakeButtonUVE(container, 0, {60.0F, 20.0F});
    const Scene::EntityUVE second = MakeButtonUVE(container, 1, {60.0F, 20.0F});
    const Scene::EntityUVE third = MakeButtonUVE(container, 2, {60.0F, 20.0F});
    const Scene::EntityUVE fourth = MakeButtonUVE(container, 3, {60.0F, 20.0F});
    const Scene::EntityUVE fifth = MakeButtonUVE(container, 4, {60.0F, 20.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(first).rect.position,
              (Math::Vector2UVE{0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(second).rect.position,
              (Math::Vector2UVE{65.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(third).rect.position,
              (Math::Vector2UVE{0.0F, 25.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(fourth).rect.position,
              (Math::Vector2UVE{65.0F, 25.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(fifth).rect.position,
              (Math::Vector2UVE{0.0F, 50.0F}));
}

TEST_F(UILayoutUVETest, VerticalWrap_FlowsDownThenAcross) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {400.0F, 400.0F}}, Scene::UILayoutDirectionUVE::Vertical,
                         Scene::UILayoutAlignmentUVE::Start, 10.0F, 4.0F, Scene::kInvalidEntityUVE, 0, 2U);
    const Scene::EntityUVE first = MakeButtonUVE(container, 0, {50.0F, 30.0F});
    const Scene::EntityUVE second = MakeButtonUVE(container, 1, {50.0F, 30.0F});
    const Scene::EntityUVE third = MakeButtonUVE(container, 2, {50.0F, 30.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(first).rect.position,
              (Math::Vector2UVE{10.0F, 10.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(second).rect.position,
              (Math::Vector2UVE{10.0F, 44.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(third).rect.position,
              (Math::Vector2UVE{64.0F, 10.0F}));
}

TEST_F(UILayoutUVETest, Wrap_AlignsSmallerChildrenWithinUniformCells) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {400.0F, 400.0F}}, Scene::UILayoutDirectionUVE::Horizontal,
                         Scene::UILayoutAlignmentUVE::Center, 0.0F, 10.0F, Scene::kInvalidEntityUVE, 0, 2U);
    const Scene::EntityUVE large = MakeButtonUVE(container, 0, {100.0F, 30.0F});
    const Scene::EntityUVE small = MakeButtonUVE(container, 1, {40.0F, 10.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    // Cells are (100, 30): the small child starts a full cell over and centers in its own.
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(large).rect.position,
              (Math::Vector2UVE{0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(small).rect.position,
              (Math::Vector2UVE{110.0F, 10.0F}));
}

TEST_F(UILayoutUVETest, Wrap_IgnoresNonWidgetChildrenInCellMeasureAndSlots) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {400.0F, 400.0F}}, Scene::UILayoutDirectionUVE::Vertical,
                         Scene::UILayoutAlignmentUVE::Start, 0.0F, 10.0F, Scene::kInvalidEntityUVE, 0, 2U);
    const Scene::EntityUVE first = MakeButtonUVE(container, 0, {50.0F, 20.0F});
    const Scene::EntityUVE bare = entityManager.CreateEntityUVE();
    LinkUVE(bare, container, 1);
    const Scene::EntityUVE second = MakeButtonUVE(container, 2, {50.0F, 20.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    // The bare entity takes no slot: both buttons share the first line, packed as a grid.
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(first).rect.position,
              (Math::Vector2UVE{0.0F, 0.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(second).rect.position,
              (Math::Vector2UVE{0.0F, 30.0F}));
}

TEST_F(UILayoutUVETest, MeasureTextWidth_MatchesThePenWalkOfAppendTextQuads) {
    const UIFontAtlasUVE& atlas = runtime.GetFontAtlasUVE();
    ASSERT_TRUE(atlas.IsValidUVE());
    float cursorX = 0.0F;
    float cursorY = 0.0F;
    std::vector<UIGlyphQuadUVE> quads;
    atlas.AppendTextQuadsUVE("Play", cursorX, cursorY, 16.0F, quads);
    EXPECT_FLOAT_EQ(atlas.MeasureTextWidthUVE("Play", 16.0F), cursorX);
    // Unrenderable characters walk neither the pen nor the measure.
    EXPECT_FLOAT_EQ(atlas.MeasureTextWidthUVE("P\nl" "\x7F" "ay", 16.0F),
                    atlas.MeasureTextWidthUVE("Play", 16.0F));
    EXPECT_EQ(atlas.MeasureTextWidthUVE("", 16.0F), 0.0F);
    EXPECT_EQ(atlas.MeasureTextWidthUVE("Play", 0.0F), 0.0F);
    EXPECT_EQ(atlas.MeasureTextWidthUVE("Play", -2.0F), 0.0F);
}

TEST_F(UILayoutUVETest, HorizontalStack_TextAdvancesByMeasuredWidth) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {400.0F, 100.0F}}, Scene::UILayoutDirectionUVE::Horizontal,
                         Scene::UILayoutAlignmentUVE::Start, 0.0F, 5.0F);
    MakeTextUVE(container, 0, 16.0F);
    const Scene::EntityUVE after = MakeButtonUVE(container, 1, {50.0F, 20.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    const float expectedX = runtime.GetFontAtlasUVE().MeasureTextWidthUVE("Play", 16.0F) + 5.0F;
    EXPECT_GT(expectedX, 5.0F); // guards against a silently-zero measure passing vacuously
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(after).rect.position.x, expectedX);
}

TEST_F(UILayoutUVETest, AutoSizeWidth_FitsWidestChildPlusPadding) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{5.0F, 5.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirectionUVE::Vertical,
                         Scene::UILayoutAlignmentUVE::Start, 10.0F, 5.0F, Scene::kInvalidEntityUVE, 0, 0U, true,
                         false);
    MakeButtonUVE(container, 0, {100.0F, 20.0F});
    MakeButtonUVE(container, 1, {60.0F, 30.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    const Scene::UILayoutContainerComponentUVE& resized =
        entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(container);
    EXPECT_EQ(resized.rect.position, (Math::Vector2UVE{5.0F, 5.0F}));
    EXPECT_EQ(resized.rect.size, (Math::Vector2UVE{120.0F, 200.0F}));
}

TEST_F(UILayoutUVETest, AutoSizeHeight_SumsChildrenSpacingAndPadding) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirectionUVE::Vertical,
                         Scene::UILayoutAlignmentUVE::Start, 10.0F, 5.0F, Scene::kInvalidEntityUVE, 0, 0U, false,
                         true);
    MakeButtonUVE(container, 0, {100.0F, 20.0F});
    MakeButtonUVE(container, 1, {60.0F, 30.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(container).rect.size,
              (Math::Vector2UVE{200.0F, 75.0F}));
}

TEST_F(UILayoutUVETest, AutoSize_EmptyContainerKeepsPaddingOnly) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 200.0F}}, Scene::UILayoutDirectionUVE::Vertical,
                         Scene::UILayoutAlignmentUVE::Start, 8.0F, 5.0F, Scene::kInvalidEntityUVE, 0, 0U, true,
                         true);

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(container).rect.size,
              (Math::Vector2UVE{16.0F, 16.0F}));
}

TEST_F(UILayoutUVETest, AutoSize_NestedInnerSizesBeforeOuterMeasures) {
    const Scene::EntityUVE outer =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {400.0F, 400.0F}}, Scene::UILayoutDirectionUVE::Vertical,
                         Scene::UILayoutAlignmentUVE::Start, 20.0F, 0.0F, Scene::kInvalidEntityUVE, 0, 0U, true,
                         true);
    const Scene::EntityUVE inner =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {10.0F, 10.0F}}, Scene::UILayoutDirectionUVE::Horizontal,
                         Scene::UILayoutAlignmentUVE::Start, 5.0F, 5.0F, outer, 0, 0U, true, true);
    const Scene::EntityUVE leaf = MakeButtonUVE(inner, 0, {40.0F, 20.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(inner).rect.size,
              (Math::Vector2UVE{50.0F, 30.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(outer).rect.size,
              (Math::Vector2UVE{90.0F, 70.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(inner).rect.position,
              (Math::Vector2UVE{20.0F, 20.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(leaf).rect.position,
              (Math::Vector2UVE{25.0F, 25.0F}));
}

TEST_F(UILayoutUVETest, AutoSize_GridFitsCellsLinesAndSpacing) {
    const Scene::EntityUVE container =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {400.0F, 400.0F}}, Scene::UILayoutDirectionUVE::Vertical,
                         Scene::UILayoutAlignmentUVE::Start, 10.0F, 4.0F, Scene::kInvalidEntityUVE, 0, 2U, true,
                         true);
    MakeButtonUVE(container, 0, {50.0F, 30.0F});
    MakeButtonUVE(container, 1, {50.0F, 30.0F});
    MakeButtonUVE(container, 2, {50.0F, 30.0F});

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    // Two 30-tall slots down the main axis, two 50-wide lines across, spacing throughout.
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(container).rect.size,
              (Math::Vector2UVE{124.0F, 84.0F}));
}

TEST_F(UILayoutUVETest, AutoSize_AnchoredStretchChildSettlesOneShotFromPreLayoutSize) {
    const Scene::EntityUVE parent =
        MakeContainerUVE(Math::RectUVE{{0.0F, 0.0F}, {50.0F, 50.0F}}, Scene::UILayoutDirectionUVE::Vertical,
                         Scene::UILayoutAlignmentUVE::Start, 0.0F, 0.0F, Scene::kInvalidEntityUVE, 0, 0U, true,
                         true);
    const Scene::EntityUVE child = MakeButtonUVE(parent, 0, {10.0F, 10.0F});
    entityManager.AddComponentUVE<Scene::UIAnchorComponentUVE>(child, Scene::UIAnchorComponentUVE{});

    for (int frame = 0; frame < 2; ++frame) {
        ResolveUIAnchorsUVE(entityManager, {400.0F, 300.0F});
        LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());
    }

    // The anchor saw the pre-layout 50x50; the parent then fit the resolved child - stable across
    // frames because nothing iterates.
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(child).rect.size,
              (Math::Vector2UVE{50.0F, 50.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(parent).rect.size,
              (Math::Vector2UVE{50.0F, 50.0F}));
}

} // namespace
} // namespace UVE::UI::Tests
