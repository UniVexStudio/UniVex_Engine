// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/ui_anchor_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_layout_container_component_uve.h"
#include "uve/component/ui_scroll_container_component_uve.h"
#include "uve/component/ui_scrollbar_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/input/input_system_uve.h"
#include "uve/input/key_code_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/scene/scene_serializer_uve.h"
#include "uve/ui/ui_runtime_uve.h"

namespace UVE::UI::Tests {
namespace {

class UIScrollUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Input::InputSystemUVE inputSystem{eventSystem};
    UIRuntimeUVE runtime;
    Scene::SceneSerializerUVE serializer;

    void FrameUVE(const Math::Vector2UVE mousePosition, const bool mouseDown, const float scroll = 0.0F) {
        inputSystem.SetMousePositionUVE(mousePosition);
        inputSystem.SetMouseButtonStateUVE(Input::MouseButtonUVE::Left, mouseDown);
        inputSystem.SetMouseScrollDeltaUVE(scroll);
        inputSystem.UpdateUVE();
        runtime.SetDeltaTimeUVE(0.016F);
        runtime.TickUVE(entityManager, inputSystem);
    }

    Scene::EntityUVE MakeScrollUVE(const Math::RectUVE rect) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UIScrollContainerComponentUVE scroll;
        scroll.rect = rect;
        entityManager.AddComponentUVE<Scene::UIScrollContainerComponentUVE>(entity, scroll);
        return entity;
    }

    Scene::EntityUVE MakeScrollbarUVE(const Scene::EntityUVE scrollEntity, const Math::RectUVE track,
                                      const Math::Vector3UVE trackColor, const Math::Vector3UVE thumbColor) {
        Scene::UIScrollbarComponentUVE scrollbar;
        scrollbar.rect = track;
        scrollbar.trackColor = trackColor;
        scrollbar.thumbColor = thumbColor;
        entityManager.AddComponentUVE<Scene::UIScrollbarComponentUVE>(scrollEntity, scrollbar);
        return scrollEntity;
    }

    const Scene::UIScrollbarComponentUVE& ScrollbarOfUVE(const Scene::EntityUVE entity) {
        return entityManager.GetComponentUVE<Scene::UIScrollbarComponentUVE>(entity);
    }

    const UIQuadUVE* FindQuadByColorUVE(const Math::Vector3UVE& color) {
        for (const UIQuadUVE& quad : runtime.GetDrawBatchUVE().quads) {
            if (quad.color == color) {
                return &quad;
            }
        }
        return nullptr;
    }

    Scene::EntityUVE MakeButtonUVE(const Math::Vector2UVE size) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UIButtonComponentUVE button;
        button.rect.size = size;
        entityManager.AddComponentUVE<Scene::UIButtonComponentUVE>(entity, button);
        return entity;
    }

    void LinkUVE(const Scene::EntityUVE child, const Scene::EntityUVE parent, const std::int64_t order) {
        Scene::HierarchyComponentUVE link;
        link.parent = parent;
        link.siblingOrder = order;
        entityManager.AddComponentUVE<Scene::HierarchyComponentUVE>(child, link);
    }

    const Scene::UIScrollContainerComponentUVE& ScrollOfUVE(const Scene::EntityUVE entity) {
        return entityManager.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(entity);
    }
};

TEST_F(UIScrollUVETest, ValidatorAcceptsDefaultsRejectsBadConfig) {
    Scene::UIScrollContainerComponentUVE scroll;
    EXPECT_TRUE(IsUIScrollContainerComponentValidUVE(scroll));
    scroll.padding = -1.0F;
    EXPECT_FALSE(IsUIScrollContainerComponentValidUVE(scroll));
    scroll.padding = 8.0F;
    scroll.scrollOffset = {-1.0F, 0.0F};
    EXPECT_FALSE(IsUIScrollContainerComponentValidUVE(scroll));
    scroll.scrollOffset = {0.0F, 0.0F};
    scroll.wheelStep = -1.0F;
    EXPECT_FALSE(IsUIScrollContainerComponentValidUVE(scroll));
}

TEST_F(UIScrollUVETest, StacksChildrenVerticallyAndMeasuresContent) {
    const Scene::EntityUVE scroll = MakeScrollUVE(Math::RectUVE{{10.0F, 10.0F}, {200.0F, 100.0F}});
    const Scene::EntityUVE first = MakeButtonUVE({60.0F, 20.0F});
    const Scene::EntityUVE second = MakeButtonUVE({60.0F, 20.0F});
    const Scene::EntityUVE third = MakeButtonUVE({60.0F, 20.0F});
    LinkUVE(first, scroll, 0);
    LinkUVE(second, scroll, 1);
    LinkUVE(third, scroll, 2);
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({700.0F, 500.0F}, false);

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(first).rect.position,
              (Math::Vector2UVE{18.0F, 18.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(second).rect.position,
              (Math::Vector2UVE{18.0F, 42.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(third).rect.position,
              (Math::Vector2UVE{18.0F, 66.0F}));
    EXPECT_EQ(ScrollOfUVE(scroll).contentSize, (Math::Vector2UVE{76.0F, 84.0F}));
}

TEST_F(UIScrollUVETest, Wheel_ScrollsAndClampsToContent) {
    const Scene::EntityUVE scroll = MakeScrollUVE(Math::RectUVE{{10.0F, 10.0F}, {200.0F, 100.0F}});
    for (std::int64_t i = 0; i < 6; ++i) {
        LinkUVE(MakeButtonUVE({60.0F, 20.0F}), scroll, i);
    }
    runtime.SetViewportSizeUVE({800.0F, 600.0F});
    FrameUVE({50.0F, 50.0F}, false);
    EXPECT_EQ(ScrollOfUVE(scroll).contentSize, (Math::Vector2UVE{76.0F, 156.0F}));

    FrameUVE({50.0F, 50.0F}, false, -5.0F);
    EXPECT_EQ(ScrollOfUVE(scroll).scrollOffset, (Math::Vector2UVE{0.0F, 56.0F}));

    FrameUVE({50.0F, 50.0F}, false, 5.0F);
    EXPECT_EQ(ScrollOfUVE(scroll).scrollOffset, (Math::Vector2UVE{0.0F, 0.0F}));

    // Horizontal has nothing to show (content 76px in a 200px box): Shift+wheel clamps to zero.
    inputSystem.SetKeyStateUVE(Input::KeyCodeUVE::LeftShift, true);
    FrameUVE({50.0F, 50.0F}, false, -5.0F);
    inputSystem.SetKeyStateUVE(Input::KeyCodeUVE::LeftShift, false);
    EXPECT_EQ(ScrollOfUVE(scroll).scrollOffset, (Math::Vector2UVE{0.0F, 0.0F}));
}

TEST_F(UIScrollUVETest, DropsFullyClippedQuads) {
    const Scene::EntityUVE scroll = MakeScrollUVE(Math::RectUVE{{10.0F, 10.0F}, {200.0F, 100.0F}});
    for (std::int64_t i = 0; i < 6; ++i) {
        LinkUVE(MakeButtonUVE({60.0F, 20.0F}), scroll, i);
    }
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({50.0F, 50.0F}, false);

    const std::vector<UIQuadUVE>& quads = runtime.GetDrawBatchUVE().quads;
    ASSERT_EQ(quads.size(), 4U) << "the scroll box draws nothing; buttons five and six are out";
    for (const UIQuadUVE& quad : quads) {
        EXPECT_LE(quad.rect.position.y + quad.rect.size.y, 110.0F);
    }
}

TEST_F(UIScrollUVETest, PartiallyClippedQuads_IntersectExactly) {
    const Scene::EntityUVE scroll = MakeScrollUVE(Math::RectUVE{{10.0F, 10.0F}, {200.0F, 100.0F}});
    for (std::int64_t i = 0; i < 6; ++i) {
        LinkUVE(MakeButtonUVE({60.0F, 20.0F}), scroll, i);
    }
    entityManager.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(scroll).scrollOffset = {0.0F, 50.0F};
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({700.0F, 500.0F}, false);

    bool foundSliver = false;
    for (const UIQuadUVE& quad : runtime.GetDrawBatchUVE().quads) {
        EXPECT_GE(quad.rect.position.y, 10.0F);
        EXPECT_LE(quad.rect.position.y + quad.rect.size.y, 110.0F);
        if (quad.rect == (Math::RectUVE{{18.0F, 10.0F}, {60.0F, 2.0F}})) {
            foundSliver = true; // button two, -8..12 against the 10..110 clip
        }
    }
    EXPECT_TRUE(foundSliver);
}

TEST_F(UIScrollUVETest, GlyphClipping_PreservesOrderAndBounds) {
    const Scene::EntityUVE scroll = MakeScrollUVE(Math::RectUVE{{10.0F, 10.0F}, {200.0F, 100.0F}});
    for (std::int64_t i = 0; i < 6; ++i) {
        LinkUVE(MakeButtonUVE({60.0F, 20.0F}), scroll, i);
    }
    const Scene::EntityUVE label = entityManager.CreateEntityUVE();
    Scene::UITextComponentUVE text;
    text.text = "Hello";
    text.fontSize = 16.0F;
    entityManager.AddComponentUVE<Scene::UITextComponentUVE>(label, text);
    LinkUVE(label, scroll, 6);
    entityManager.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(scroll).scrollOffset = {0.0F, 64.0F};
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({700.0F, 500.0F}, false);

    std::size_t glyphs = 0U;
    for (const UIQuadUVE& quad : runtime.GetDrawBatchUVE().quads) {
        if (quad.kind != UIDrawItemKindUVE::Glyph) {
            continue;
        }
        ++glyphs;
        EXPECT_LE(quad.rect.position.y + quad.rect.size.y, 110.001F);
        EXPECT_LT(quad.u0, quad.u1);
        EXPECT_LT(quad.v0, quad.v1);
    }
    EXPECT_GT(glyphs, 0U) << "the label straddles the clip edge: some of it survives";
}

TEST_F(UIScrollUVETest, ScrolledOutWidgets_DoNotHitTest) {
    const Scene::EntityUVE scroll = MakeScrollUVE(Math::RectUVE{{10.0F, 10.0F}, {200.0F, 100.0F}});
    std::vector<Scene::EntityUVE> buttons;
    for (std::int64_t i = 0; i < 6; ++i) {
        const Scene::EntityUVE button = MakeButtonUVE({60.0F, 20.0F});
        LinkUVE(button, scroll, i);
        buttons.push_back(button);
    }
    entityManager.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(scroll).scrollOffset = {0.0F, 56.0F};
    runtime.SetViewportSizeUVE({800.0F, 600.0F});
    FrameUVE({700.0F, 500.0F}, false);

    // Inside button one's scrolled rect (-38..-18) but outside the clip: gated silent.
    FrameUVE({48.0F, -28.0F}, true);
    for (const Scene::EntityUVE button : buttons) {
        EXPECT_FALSE(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(button).wasClickedThisFrame);
    }
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(buttons[0]).isHovered);
    FrameUVE({48.0F, -28.0F}, false);

    // Button six's scrolled home (82..102): alive and clickable at its shifted rect.
    FrameUVE({48.0F, 92.0F}, true);
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(buttons[5]).wasClickedThisFrame);
    FrameUVE({48.0F, 92.0F}, false);
}

TEST_F(UIScrollUVETest, NestedScroll_StacksWithoutOffset) {
    const Scene::EntityUVE outer = MakeScrollUVE(Math::RectUVE{{10.0F, 10.0F}, {200.0F, 200.0F}});
    const Scene::EntityUVE inner = MakeScrollUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 60.0F}});
    LinkUVE(inner, outer, 0);
    const Scene::EntityUVE button = MakeButtonUVE({60.0F, 20.0F});
    LinkUVE(button, inner, 0);
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({30.0F, 30.0F}, false, -5.0F);

    EXPECT_EQ(ScrollOfUVE(inner).scrollOffset, (Math::Vector2UVE{0.0F, 0.0F}));
    EXPECT_EQ(ScrollOfUVE(inner).contentSize, (Math::Vector2UVE{76.0F, 36.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(button).rect.position,
              (Math::Vector2UVE{26.0F, 26.0F}));
}

TEST_F(UIScrollUVETest, Layout_ScrollBoxStacksAndAnchors) {
    const Scene::EntityUVE container = entityManager.CreateEntityUVE();
    Scene::UILayoutContainerComponentUVE layout;
    layout.rect = Math::RectUVE{{0.0F, 0.0F}, {300.0F, 300.0F}};
    layout.padding = 10.0F;
    entityManager.AddComponentUVE<Scene::UILayoutContainerComponentUVE>(container, layout);
    const Scene::EntityUVE stacked = MakeScrollUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 100.0F}});
    Scene::HierarchyComponentUVE link;
    link.parent = container;
    link.siblingOrder = 0;
    entityManager.AddComponentUVE<Scene::HierarchyComponentUVE>(stacked, link);
    const Scene::EntityUVE stretched = MakeScrollUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 100.0F}});
    entityManager.AddComponentUVE<Scene::UIAnchorComponentUVE>(stretched, Scene::UIAnchorComponentUVE{});
    runtime.SetViewportSizeUVE({400.0F, 300.0F});

    FrameUVE({350.0F, 250.0F}, false);

    EXPECT_EQ(ScrollOfUVE(stacked).rect.position, (Math::Vector2UVE{10.0F, 10.0F}));
    EXPECT_EQ(ScrollOfUVE(stretched).rect, (Math::RectUVE{{0.0F, 0.0F}, {400.0F, 300.0F}}));
}

TEST_F(UIScrollUVETest, SaveThenLoad_ScrollContainer_RoundTrip) {
    const Scene::EntityUVE scroll = entityManager.CreateEntityUVE();
    Scene::UIScrollContainerComponentUVE authored;
    authored.rect = Math::RectUVE{{10.0F, 20.0F}, {200.0F, 100.0F}};
    authored.padding = 10.0F;
    authored.gap = 6.0F;
    authored.wheelStep = 30.0F;
    authored.scrollOffset = {5.0F, 10.0F};
    authored.contentSize = {999.0F, 999.0F};
    entityManager.AddComponentUVE<Scene::UIScrollContainerComponentUVE>(scroll, authored);

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_scroll.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {scroll}, path, Scene::SceneAssetTypeUVE::Scene));

    Scene::EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<Scene::EntityUVE> loaded = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(loaded.size(), 1U);
    const Scene::UIScrollContainerComponentUVE& restored =
        loadedManager.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(loaded[0]);
    EXPECT_EQ(restored.rect, (Math::RectUVE{{10.0F, 20.0F}, {200.0F, 100.0F}}));
    EXPECT_FLOAT_EQ(restored.padding, 10.0F);
    EXPECT_FLOAT_EQ(restored.gap, 6.0F);
    EXPECT_FLOAT_EQ(restored.wheelStep, 30.0F);
    EXPECT_EQ(restored.scrollOffset, (Math::Vector2UVE{5.0F, 10.0F}));
    EXPECT_EQ(restored.contentSize, (Math::Vector2UVE{0.0F, 0.0F}));

    std::filesystem::remove(path);
}


TEST_F(UIScrollUVETest, ScrollbarValidatorAcceptsDefaultsRejectsBadConfig) {
    Scene::UIScrollbarComponentUVE scrollbar;
    EXPECT_TRUE(IsUIScrollbarComponentValidUVE(scrollbar));
    scrollbar.rect.size = Math::Vector2UVE{0.0F, 200.0F};
    EXPECT_FALSE(IsUIScrollbarComponentValidUVE(scrollbar));
    scrollbar.rect.size = Math::Vector2UVE{12.0F, 200.0F};
    scrollbar.minThumbHeight = -1.0F;
    EXPECT_FALSE(IsUIScrollbarComponentValidUVE(scrollbar));
    scrollbar.minThumbHeight = 16.0F;
    scrollbar.thumbColor = Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    EXPECT_FALSE(IsUIScrollbarComponentValidUVE(scrollbar));
}

TEST_F(UIScrollUVETest, ScrollbarWithoutScrollContainer_DrawsNothingAndStaysIdle) {
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::UIScrollbarComponentUVE scrollbar;
    scrollbar.rect = Math::RectUVE{{0.0F, 0.0F}, {12.0F, 200.0F}};
    entityManager.AddComponentUVE<Scene::UIScrollbarComponentUVE>(entity, scrollbar);

    FrameUVE(Math::Vector2UVE{6.0F, 100.0F}, true);
    EXPECT_TRUE(runtime.GetDrawBatchUVE().quads.empty());
    const Scene::UIScrollbarComponentUVE& updated = ScrollbarOfUVE(entity);
    EXPECT_FALSE(updated.isHovered);
    EXPECT_FALSE(updated.isDragging);
    EXPECT_FALSE(updated.wasChangedThisFrame);
}

TEST_F(UIScrollUVETest, ScrollbarWithoutOverflow_ThumbFillsTrackAndDragIsInert) {
    const Scene::EntityUVE scroll = MakeScrollUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 200.0F}});
    const Math::RectUVE track{{188.0F, 0.0F}, {12.0F, 200.0F}};
    MakeScrollbarUVE(scroll, track, Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    LinkUVE(MakeButtonUVE(Math::Vector2UVE{50.0F, 50.0F}), scroll, 0);

    FrameUVE(Math::Vector2UVE{194.0F, 100.0F}, false);
    const UIQuadUVE* thumb = FindQuadByColorUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    ASSERT_NE(thumb, nullptr);
    EXPECT_EQ(thumb->rect, track);

    FrameUVE(Math::Vector2UVE{194.0F, 100.0F}, true);
    FrameUVE(Math::Vector2UVE{194.0F, 180.0F}, true);
    EXPECT_EQ(ScrollOfUVE(scroll).scrollOffset.y, 0.0F);
    EXPECT_FALSE(ScrollbarOfUVE(scroll).wasChangedThisFrame);
}

TEST_F(UIScrollUVETest, ScrollbarThumbHeight_MatchesViewportOverContentFraction) {
    const Scene::EntityUVE scroll = MakeScrollUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 200.0F}});
    MakeScrollbarUVE(scroll, Math::RectUVE{{188.0F, 0.0F}, {12.0F, 200.0F}},
                     Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    for (std::int64_t i = 0; i < 4; ++i) {
        LinkUVE(MakeButtonUVE(Math::Vector2UVE{100.0F, 100.0F}), scroll, i);
    }

    FrameUVE(Math::Vector2UVE{0.0F, 0.0F}, false);
    const float contentH = ScrollOfUVE(scroll).contentSize.y;
    ASSERT_GT(contentH, 200.0F);
    const UIQuadUVE* thumb = FindQuadByColorUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    ASSERT_NE(thumb, nullptr);
    EXPECT_FLOAT_EQ(thumb->rect.size.y, 200.0F * (200.0F / contentH));
    EXPECT_FLOAT_EQ(thumb->rect.position.x, 188.0F);
    EXPECT_FLOAT_EQ(thumb->rect.position.y, 0.0F);
    EXPECT_FLOAT_EQ(thumb->rect.size.x, 12.0F);
}

TEST_F(UIScrollUVETest, ScrollbarDrag_ScrollsContentOnTheSameTickAsTheThumb) {
    const Scene::EntityUVE scroll = MakeScrollUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 200.0F}});
    MakeScrollbarUVE(scroll, Math::RectUVE{{188.0F, 0.0F}, {12.0F, 200.0F}},
                     Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    const Scene::EntityUVE firstChild = MakeButtonUVE(Math::Vector2UVE{100.0F, 100.0F});
    LinkUVE(firstChild, scroll, 0);
    for (std::int64_t i = 1; i < 4; ++i) {
        LinkUVE(MakeButtonUVE(Math::Vector2UVE{100.0F, 100.0F}), scroll, i);
    }
    FrameUVE(Math::Vector2UVE{0.0F, 0.0F}, false);
    const float contentH = ScrollOfUVE(scroll).contentSize.y;
    const float maxOffset = contentH - 200.0F;
    ASSERT_GT(maxOffset, 0.0F);

    FrameUVE(Math::Vector2UVE{194.0F, 100.0F}, true);
    EXPECT_TRUE(ScrollbarOfUVE(scroll).isDragging);
    EXPECT_EQ(ScrollOfUVE(scroll).scrollOffset.y, 0.0F);

    FrameUVE(Math::Vector2UVE{194.0F, 100.0F}, true);
    EXPECT_FLOAT_EQ(ScrollOfUVE(scroll).scrollOffset.y, 0.5F * maxOffset);
    EXPECT_TRUE(ScrollbarOfUVE(scroll).wasChangedThisFrame);
    // Zero lag: the children stacked from the dragged offset in the SAME tick the drag applied...
    const Scene::UIButtonComponentUVE& child =
        entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(firstChild);
    EXPECT_FLOAT_EQ(child.rect.position.y, 8.0F - 0.5F * maxOffset);
    // ...and the thumb sits where the content is.
    const UIQuadUVE* thumb = FindQuadByColorUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    ASSERT_NE(thumb, nullptr);
    EXPECT_FLOAT_EQ(thumb->rect.position.y, 0.5F * (200.0F - 200.0F * (200.0F / contentH)));
}

TEST_F(UIScrollUVETest, ScrollbarDragPastEnds_ClampsToTopAndBottom) {
    const Scene::EntityUVE scroll = MakeScrollUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 200.0F}});
    MakeScrollbarUVE(scroll, Math::RectUVE{{188.0F, 0.0F}, {12.0F, 200.0F}},
                     Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    for (std::int64_t i = 0; i < 4; ++i) {
        LinkUVE(MakeButtonUVE(Math::Vector2UVE{100.0F, 100.0F}), scroll, i);
    }
    FrameUVE(Math::Vector2UVE{0.0F, 0.0F}, false);
    const float maxOffset = ScrollOfUVE(scroll).contentSize.y - 200.0F;
    ASSERT_GT(maxOffset, 0.0F);

    FrameUVE(Math::Vector2UVE{194.0F, 100.0F}, true);
    FrameUVE(Math::Vector2UVE{194.0F, -80.0F}, true);
    EXPECT_EQ(ScrollOfUVE(scroll).scrollOffset.y, 0.0F);
    FrameUVE(Math::Vector2UVE{194.0F, 400.0F}, true);
    EXPECT_FLOAT_EQ(ScrollOfUVE(scroll).scrollOffset.y, maxOffset);
}

TEST_F(UIScrollUVETest, ScrollbarRelease_StopsTrackingThePointer) {
    const Scene::EntityUVE scroll = MakeScrollUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 200.0F}});
    MakeScrollbarUVE(scroll, Math::RectUVE{{188.0F, 0.0F}, {12.0F, 200.0F}},
                     Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    for (std::int64_t i = 0; i < 4; ++i) {
        LinkUVE(MakeButtonUVE(Math::Vector2UVE{100.0F, 100.0F}), scroll, i);
    }
    FrameUVE(Math::Vector2UVE{0.0F, 0.0F}, false);
    FrameUVE(Math::Vector2UVE{194.0F, 100.0F}, true);
    FrameUVE(Math::Vector2UVE{194.0F, 100.0F}, true);
    const float dragged = ScrollOfUVE(scroll).scrollOffset.y;
    ASSERT_GT(dragged, 0.0F);

    FrameUVE(Math::Vector2UVE{194.0F, 100.0F}, false);
    EXPECT_FALSE(ScrollbarOfUVE(scroll).isDragging);
    EXPECT_FALSE(ScrollbarOfUVE(scroll).wasChangedThisFrame);
    FrameUVE(Math::Vector2UVE{194.0F, 180.0F}, false);
    EXPECT_EQ(ScrollOfUVE(scroll).scrollOffset.y, dragged);
    EXPECT_FALSE(ScrollbarOfUVE(scroll).isDragging);
}

TEST_F(UIScrollUVETest, SaveThenLoad_Scrollbar_RoundTripsConfigButNotDragState) {
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::UIScrollbarComponentUVE authored;
    authored.rect = Math::RectUVE{{188.0F, 10.0F}, {12.0F, 180.0F}};
    authored.trackColor = Math::Vector3UVE{0.25F, 0.25F, 0.25F};
    authored.thumbColor = Math::Vector3UVE{0.75F, 0.5F, 0.25F};
    authored.minThumbHeight = 20.0F;
    authored.isHovered = true;
    authored.isDragging = true;
    authored.wasChangedThisFrame = true;
    entityManager.AddComponentUVE<Scene::UIScrollbarComponentUVE>(entity, authored);

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_scrollbar.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, Scene::SceneAssetTypeUVE::Scene));

    Scene::EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<Scene::EntityUVE> loaded = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(loaded.size(), 1U);
    const Scene::UIScrollbarComponentUVE& restored =
        loadedManager.GetComponentUVE<Scene::UIScrollbarComponentUVE>(loaded[0]);
    EXPECT_EQ(restored.rect, (Math::RectUVE{{188.0F, 10.0F}, {12.0F, 180.0F}}));
    EXPECT_FLOAT_EQ(restored.trackColor.x, 0.25F);
    EXPECT_FLOAT_EQ(restored.trackColor.y, 0.25F);
    EXPECT_FLOAT_EQ(restored.trackColor.z, 0.25F);
    EXPECT_FLOAT_EQ(restored.thumbColor.x, 0.75F);
    EXPECT_FLOAT_EQ(restored.thumbColor.y, 0.5F);
    EXPECT_FLOAT_EQ(restored.thumbColor.z, 0.25F);
    EXPECT_FLOAT_EQ(restored.minThumbHeight, 20.0F);
    EXPECT_FALSE(restored.isHovered);
    EXPECT_FALSE(restored.isDragging);
    EXPECT_FALSE(restored.wasChangedThisFrame);

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::UI::Tests
