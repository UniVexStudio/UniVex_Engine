// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>

#include <gtest/gtest.h>

#include "entity.h"
#include "hierarchy_component.h"
#include "ui_anchor_component.h"
#include "ui_button_component.h"
#include "ui_layout_container_component.h"
#include "ui_scroll_container_component.h"
#include "ui_scrollbar_component.h"
#include "ui_text_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "input_system.h"
#include "key_code.h"
#include "rect.h"
#include "vector2.h"
#include "vector3.h"
#include "memory_manager.h"
#include "scene_serializer.h"
#include "ui_runtime.h"

namespace UVE::UI::Tests {
namespace {

class UIScrollTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Input::InputSystem inputSystem{eventSystem};
    UIRuntime runtime;
    Scene::SceneSerializer serializer;

    void Frame(const Math::Vector2 mousePosition, const bool mouseDown, const float scroll = 0.0F) {
        inputSystem.SetMousePosition(mousePosition);
        inputSystem.SetMouseButtonState(Input::MouseButton::Left, mouseDown);
        inputSystem.SetMouseScrollDelta(scroll);
        inputSystem.Update();
        runtime.SetDeltaTime(0.016F);
        runtime.Tick(entityManager, inputSystem);
    }

    Scene::Entity MakeScroll(const Math::Rect rect) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UIScrollContainerComponent scroll;
        scroll.rect = rect;
        entityManager.AddComponent<Scene::UIScrollContainerComponent>(entity, scroll);
        return entity;
    }

    Scene::Entity MakeScrollbar(const Scene::Entity scrollEntity, const Math::Rect track,
                                      const Math::Vector3 trackColor, const Math::Vector3 thumbColor) {
        Scene::UIScrollbarComponent scrollbar;
        scrollbar.rect = track;
        scrollbar.trackColor = trackColor;
        scrollbar.thumbColor = thumbColor;
        entityManager.AddComponent<Scene::UIScrollbarComponent>(scrollEntity, scrollbar);
        return scrollEntity;
    }

    const Scene::UIScrollbarComponent& ScrollbarOf(const Scene::Entity entity) {
        return entityManager.GetComponent<Scene::UIScrollbarComponent>(entity);
    }

    const UIQuad* FindQuadByColor(const Math::Vector3& color) {
        for (const UIQuad& quad : runtime.GetDrawBatch().quads) {
            if (quad.color == color) {
                return &quad;
            }
        }
        return nullptr;
    }

    Scene::Entity MakeButton(const Math::Vector2 size) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UIButtonComponent button;
        button.rect.size = size;
        entityManager.AddComponent<Scene::UIButtonComponent>(entity, button);
        return entity;
    }

    void Link(const Scene::Entity child, const Scene::Entity parent, const std::int64_t order) {
        Scene::HierarchyComponent link;
        link.parent = parent;
        link.siblingOrder = order;
        entityManager.AddComponent<Scene::HierarchyComponent>(child, link);
    }

    const Scene::UIScrollContainerComponent& ScrollOf(const Scene::Entity entity) {
        return entityManager.GetComponent<Scene::UIScrollContainerComponent>(entity);
    }
};

TEST_F(UIScrollTest, ValidatorAcceptsDefaultsRejectsBadConfig) {
    Scene::UIScrollContainerComponent scroll;
    EXPECT_TRUE(IsUIScrollContainerComponentValid(scroll));
    scroll.padding = -1.0F;
    EXPECT_FALSE(IsUIScrollContainerComponentValid(scroll));
    scroll.padding = 8.0F;
    scroll.scrollOffset = {-1.0F, 0.0F};
    EXPECT_FALSE(IsUIScrollContainerComponentValid(scroll));
    scroll.scrollOffset = {0.0F, 0.0F};
    scroll.wheelStep = -1.0F;
    EXPECT_FALSE(IsUIScrollContainerComponentValid(scroll));
}

TEST_F(UIScrollTest, StacksChildrenVerticallyAndMeasuresContent) {
    const Scene::Entity scroll = MakeScroll(Math::Rect{{10.0F, 10.0F}, {200.0F, 100.0F}});
    const Scene::Entity first = MakeButton({60.0F, 20.0F});
    const Scene::Entity second = MakeButton({60.0F, 20.0F});
    const Scene::Entity third = MakeButton({60.0F, 20.0F});
    Link(first, scroll, 0);
    Link(second, scroll, 1);
    Link(third, scroll, 2);
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({700.0F, 500.0F}, false);

    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(first).rect.position,
              (Math::Vector2{18.0F, 18.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(second).rect.position,
              (Math::Vector2{18.0F, 42.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(third).rect.position,
              (Math::Vector2{18.0F, 66.0F}));
    EXPECT_EQ(ScrollOf(scroll).contentSize, (Math::Vector2{76.0F, 84.0F}));
}

TEST_F(UIScrollTest, Wheel_ScrollsAndClampsToContent) {
    const Scene::Entity scroll = MakeScroll(Math::Rect{{10.0F, 10.0F}, {200.0F, 100.0F}});
    for (std::int64_t i = 0; i < 6; ++i) {
        Link(MakeButton({60.0F, 20.0F}), scroll, i);
    }
    runtime.SetViewportSize({800.0F, 600.0F});
    Frame({50.0F, 50.0F}, false);
    EXPECT_EQ(ScrollOf(scroll).contentSize, (Math::Vector2{76.0F, 156.0F}));

    Frame({50.0F, 50.0F}, false, -5.0F);
    EXPECT_EQ(ScrollOf(scroll).scrollOffset, (Math::Vector2{0.0F, 56.0F}));

    Frame({50.0F, 50.0F}, false, 5.0F);
    EXPECT_EQ(ScrollOf(scroll).scrollOffset, (Math::Vector2{0.0F, 0.0F}));

    // Horizontal has nothing to show (content 76px in a 200px box): Shift+wheel clamps to zero.
    inputSystem.SetKeyState(Input::KeyCode::LeftShift, true);
    Frame({50.0F, 50.0F}, false, -5.0F);
    inputSystem.SetKeyState(Input::KeyCode::LeftShift, false);
    EXPECT_EQ(ScrollOf(scroll).scrollOffset, (Math::Vector2{0.0F, 0.0F}));
}

TEST_F(UIScrollTest, Wheel_HonorsCoordinateTransformWhenScaledOrOffset) {
    const Scene::Entity scroll = MakeScroll(Math::Rect{{10.0F, 10.0F}, {200.0F, 100.0F}});
    for (std::int64_t i = 0; i < 6; ++i) {
        Link(MakeButton({60.0F, 20.0F}), scroll, i);
    }
    runtime.SetViewportSize({800.0F, 600.0F});
    UICoordinateTransform transform;
    transform.scaleX = 2.0F;
    transform.scaleY = 2.0F;
    transform.offsetX = 100.0F;
    transform.offsetY = 50.0F;
    runtime.SetCoordinateTransform(transform);

    // Device pointer (200, 150) maps to authored ((200 - 100)/2, (150 - 50)/2) = (50, 50), inside scroll rect.
    Frame({200.0F, 150.0F}, false, -5.0F);
    EXPECT_EQ(ScrollOf(scroll).scrollOffset, (Math::Vector2{0.0F, 56.0F}));
}

TEST_F(UIScrollTest, DropsFullyClippedQuads) {
    const Scene::Entity scroll = MakeScroll(Math::Rect{{10.0F, 10.0F}, {200.0F, 100.0F}});
    for (std::int64_t i = 0; i < 6; ++i) {
        Link(MakeButton({60.0F, 20.0F}), scroll, i);
    }
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({50.0F, 50.0F}, false);

    const std::vector<UIQuad>& quads = runtime.GetDrawBatch().quads;
    ASSERT_EQ(quads.size(), 4U) << "the scroll box draws nothing; buttons five and six are out";
    for (const UIQuad& quad : quads) {
        EXPECT_LE(quad.rect.position.y + quad.rect.size.y, 110.0F);
    }
}

TEST_F(UIScrollTest, PartiallyClippedQuads_IntersectExactly) {
    const Scene::Entity scroll = MakeScroll(Math::Rect{{10.0F, 10.0F}, {200.0F, 100.0F}});
    for (std::int64_t i = 0; i < 6; ++i) {
        Link(MakeButton({60.0F, 20.0F}), scroll, i);
    }
    entityManager.GetComponent<Scene::UIScrollContainerComponent>(scroll).scrollOffset = {0.0F, 50.0F};
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({700.0F, 500.0F}, false);

    bool foundSliver = false;
    for (const UIQuad& quad : runtime.GetDrawBatch().quads) {
        EXPECT_GE(quad.rect.position.y, 10.0F);
        EXPECT_LE(quad.rect.position.y + quad.rect.size.y, 110.0F);
        if (quad.rect == (Math::Rect{{18.0F, 10.0F}, {60.0F, 2.0F}})) {
            foundSliver = true; // button two, -8..12 against the 10..110 clip
        }
    }
    EXPECT_TRUE(foundSliver);
}

TEST_F(UIScrollTest, GlyphClipping_PreservesOrderAndBounds) {
    const Scene::Entity scroll = MakeScroll(Math::Rect{{10.0F, 10.0F}, {200.0F, 100.0F}});
    for (std::int64_t i = 0; i < 6; ++i) {
        Link(MakeButton({60.0F, 20.0F}), scroll, i);
    }
    const Scene::Entity label = entityManager.CreateEntity();
    Scene::UITextComponent text;
    text.text = "Hello";
    text.fontSize = 16.0F;
    entityManager.AddComponent<Scene::UITextComponent>(label, text);
    Link(label, scroll, 6);
    entityManager.GetComponent<Scene::UIScrollContainerComponent>(scroll).scrollOffset = {0.0F, 64.0F};
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({700.0F, 500.0F}, false);

    std::size_t glyphs = 0U;
    for (const UIQuad& quad : runtime.GetDrawBatch().quads) {
        if (quad.kind != UIDrawItemKind::Glyph) {
            continue;
        }
        ++glyphs;
        EXPECT_LE(quad.rect.position.y + quad.rect.size.y, 110.001F);
        EXPECT_LT(quad.u0, quad.u1);
        EXPECT_LT(quad.v0, quad.v1);
    }
    EXPECT_GT(glyphs, 0U) << "the label straddles the clip edge: some of it survives";
}

TEST_F(UIScrollTest, ScrolledOutWidgets_DoNotHitTest) {
    const Scene::Entity scroll = MakeScroll(Math::Rect{{10.0F, 10.0F}, {200.0F, 100.0F}});
    std::vector<Scene::Entity> buttons;
    for (std::int64_t i = 0; i < 6; ++i) {
        const Scene::Entity button = MakeButton({60.0F, 20.0F});
        Link(button, scroll, i);
        buttons.push_back(button);
    }
    entityManager.GetComponent<Scene::UIScrollContainerComponent>(scroll).scrollOffset = {0.0F, 56.0F};
    runtime.SetViewportSize({800.0F, 600.0F});
    Frame({700.0F, 500.0F}, false);

    // Inside button one's scrolled rect (-38..-18) but outside the clip: gated silent.
    Frame({48.0F, -28.0F}, true);
    for (const Scene::Entity button : buttons) {
        EXPECT_FALSE(entityManager.GetComponent<Scene::UIButtonComponent>(button).wasClickedThisFrame);
    }
    EXPECT_FALSE(entityManager.GetComponent<Scene::UIButtonComponent>(buttons[0]).isHovered);
    Frame({48.0F, -28.0F}, false);

    // Button six's scrolled home (82..102): alive and clickable at its shifted rect.
    Frame({48.0F, 92.0F}, true);
    EXPECT_TRUE(entityManager.GetComponent<Scene::UIButtonComponent>(buttons[5]).wasClickedThisFrame);
    Frame({48.0F, 92.0F}, false);
}

TEST_F(UIScrollTest, NestedScroll_StacksWithoutOffset) {
    const Scene::Entity outer = MakeScroll(Math::Rect{{10.0F, 10.0F}, {200.0F, 200.0F}});
    const Scene::Entity inner = MakeScroll(Math::Rect{{0.0F, 0.0F}, {100.0F, 60.0F}});
    Link(inner, outer, 0);
    const Scene::Entity button = MakeButton({60.0F, 20.0F});
    Link(button, inner, 0);
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({30.0F, 30.0F}, false, -5.0F);

    EXPECT_EQ(ScrollOf(inner).scrollOffset, (Math::Vector2{0.0F, 0.0F}));
    EXPECT_EQ(ScrollOf(inner).contentSize, (Math::Vector2{76.0F, 36.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(button).rect.position,
              (Math::Vector2{26.0F, 26.0F}));
}

TEST_F(UIScrollTest, Layout_ScrollBoxStacksAndAnchors) {
    const Scene::Entity container = entityManager.CreateEntity();
    Scene::UILayoutContainerComponent layout;
    layout.rect = Math::Rect{{0.0F, 0.0F}, {300.0F, 300.0F}};
    layout.padding = 10.0F;
    entityManager.AddComponent<Scene::UILayoutContainerComponent>(container, layout);
    const Scene::Entity stacked = MakeScroll(Math::Rect{{0.0F, 0.0F}, {200.0F, 100.0F}});
    Scene::HierarchyComponent link;
    link.parent = container;
    link.siblingOrder = 0;
    entityManager.AddComponent<Scene::HierarchyComponent>(stacked, link);
    const Scene::Entity stretched = MakeScroll(Math::Rect{{0.0F, 0.0F}, {200.0F, 100.0F}});
    entityManager.AddComponent<Scene::UIAnchorComponent>(stretched, Scene::UIAnchorComponent{});
    runtime.SetViewportSize({400.0F, 300.0F});

    Frame({350.0F, 250.0F}, false);

    EXPECT_EQ(ScrollOf(stacked).rect.position, (Math::Vector2{10.0F, 10.0F}));
    EXPECT_EQ(ScrollOf(stretched).rect, (Math::Rect{{0.0F, 0.0F}, {400.0F, 300.0F}}));
}

TEST_F(UIScrollTest, SaveThenLoad_ScrollContainer_RoundTrip) {
    const Scene::Entity scroll = entityManager.CreateEntity();
    Scene::UIScrollContainerComponent authored;
    authored.rect = Math::Rect{{10.0F, 20.0F}, {200.0F, 100.0F}};
    authored.padding = 10.0F;
    authored.gap = 6.0F;
    authored.wheelStep = 30.0F;
    authored.scrollOffset = {5.0F, 10.0F};
    authored.contentSize = {999.0F, 999.0F};
    entityManager.AddComponent<Scene::UIScrollContainerComponent>(scroll, authored);

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_scroll.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.Save(entityManager, {scroll}, path, Scene::SceneAssetType::Scene));

    Scene::EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    const std::vector<Scene::Entity> loaded = serializer.Load(loadedManager, path);
    ASSERT_EQ(loaded.size(), 1U);
    const Scene::UIScrollContainerComponent& restored =
        loadedManager.GetComponent<Scene::UIScrollContainerComponent>(loaded[0]);
    EXPECT_EQ(restored.rect, (Math::Rect{{10.0F, 20.0F}, {200.0F, 100.0F}}));
    EXPECT_FLOAT_EQ(restored.padding, 10.0F);
    EXPECT_FLOAT_EQ(restored.gap, 6.0F);
    EXPECT_FLOAT_EQ(restored.wheelStep, 30.0F);
    EXPECT_EQ(restored.scrollOffset, (Math::Vector2{5.0F, 10.0F}));
    EXPECT_EQ(restored.contentSize, (Math::Vector2{0.0F, 0.0F}));

    std::filesystem::remove(path);
}


TEST_F(UIScrollTest, ScrollbarValidatorAcceptsDefaultsRejectsBadConfig) {
    Scene::UIScrollbarComponent scrollbar;
    EXPECT_TRUE(IsUIScrollbarComponentValid(scrollbar));
    scrollbar.rect.size = Math::Vector2{0.0F, 200.0F};
    EXPECT_FALSE(IsUIScrollbarComponentValid(scrollbar));
    scrollbar.rect.size = Math::Vector2{12.0F, 200.0F};
    scrollbar.minThumbHeight = -1.0F;
    EXPECT_FALSE(IsUIScrollbarComponentValid(scrollbar));
    scrollbar.minThumbHeight = 16.0F;
    scrollbar.thumbColor = Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    EXPECT_FALSE(IsUIScrollbarComponentValid(scrollbar));
}

TEST_F(UIScrollTest, ScrollbarWithoutScrollContainer_DrawsNothingAndStaysIdle) {
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::UIScrollbarComponent scrollbar;
    scrollbar.rect = Math::Rect{{0.0F, 0.0F}, {12.0F, 200.0F}};
    entityManager.AddComponent<Scene::UIScrollbarComponent>(entity, scrollbar);

    Frame(Math::Vector2{6.0F, 100.0F}, true);
    EXPECT_TRUE(runtime.GetDrawBatch().quads.empty());
    const Scene::UIScrollbarComponent& updated = ScrollbarOf(entity);
    EXPECT_FALSE(updated.isHovered);
    EXPECT_FALSE(updated.isDragging);
    EXPECT_FALSE(updated.wasChangedThisFrame);
}

TEST_F(UIScrollTest, ScrollbarWithoutOverflow_ThumbFillsTrackAndDragIsInert) {
    const Scene::Entity scroll = MakeScroll(Math::Rect{{0.0F, 0.0F}, {200.0F, 200.0F}});
    const Math::Rect track{{188.0F, 0.0F}, {12.0F, 200.0F}};
    MakeScrollbar(scroll, track, Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F});
    Link(MakeButton(Math::Vector2{50.0F, 50.0F}), scroll, 0);

    Frame(Math::Vector2{194.0F, 100.0F}, false);
    const UIQuad* thumb = FindQuadByColor(Math::Vector3{0.0F, 1.0F, 0.0F});
    ASSERT_NE(thumb, nullptr);
    EXPECT_EQ(thumb->rect, track);

    Frame(Math::Vector2{194.0F, 100.0F}, true);
    Frame(Math::Vector2{194.0F, 180.0F}, true);
    EXPECT_EQ(ScrollOf(scroll).scrollOffset.y, 0.0F);
    EXPECT_FALSE(ScrollbarOf(scroll).wasChangedThisFrame);
}

TEST_F(UIScrollTest, ScrollbarThumbHeight_MatchesViewportOverContentFraction) {
    const Scene::Entity scroll = MakeScroll(Math::Rect{{0.0F, 0.0F}, {200.0F, 200.0F}});
    MakeScrollbar(scroll, Math::Rect{{188.0F, 0.0F}, {12.0F, 200.0F}},
                     Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F});
    for (std::int64_t i = 0; i < 4; ++i) {
        Link(MakeButton(Math::Vector2{100.0F, 100.0F}), scroll, i);
    }

    Frame(Math::Vector2{0.0F, 0.0F}, false);
    const float contentH = ScrollOf(scroll).contentSize.y;
    ASSERT_GT(contentH, 200.0F);
    const UIQuad* thumb = FindQuadByColor(Math::Vector3{0.0F, 1.0F, 0.0F});
    ASSERT_NE(thumb, nullptr);
    EXPECT_FLOAT_EQ(thumb->rect.size.y, 200.0F * (200.0F / contentH));
    EXPECT_FLOAT_EQ(thumb->rect.position.x, 188.0F);
    EXPECT_FLOAT_EQ(thumb->rect.position.y, 0.0F);
    EXPECT_FLOAT_EQ(thumb->rect.size.x, 12.0F);
}

TEST_F(UIScrollTest, ScrollbarDrag_ScrollsContentOnTheSameTickAsTheThumb) {
    const Scene::Entity scroll = MakeScroll(Math::Rect{{0.0F, 0.0F}, {200.0F, 200.0F}});
    MakeScrollbar(scroll, Math::Rect{{188.0F, 0.0F}, {12.0F, 200.0F}},
                     Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F});
    const Scene::Entity firstChild = MakeButton(Math::Vector2{100.0F, 100.0F});
    Link(firstChild, scroll, 0);
    for (std::int64_t i = 1; i < 4; ++i) {
        Link(MakeButton(Math::Vector2{100.0F, 100.0F}), scroll, i);
    }
    Frame(Math::Vector2{0.0F, 0.0F}, false);
    const float contentH = ScrollOf(scroll).contentSize.y;
    const float maxOffset = contentH - 200.0F;
    ASSERT_GT(maxOffset, 0.0F);

    Frame(Math::Vector2{194.0F, 100.0F}, true);
    EXPECT_TRUE(ScrollbarOf(scroll).isDragging);
    EXPECT_EQ(ScrollOf(scroll).scrollOffset.y, 0.0F);

    Frame(Math::Vector2{194.0F, 100.0F}, true);
    EXPECT_FLOAT_EQ(ScrollOf(scroll).scrollOffset.y, 0.5F * maxOffset);
    EXPECT_TRUE(ScrollbarOf(scroll).wasChangedThisFrame);
    // Zero lag: the children stacked from the dragged offset in the SAME tick the drag applied...
    const Scene::UIButtonComponent& child =
        entityManager.GetComponent<Scene::UIButtonComponent>(firstChild);
    EXPECT_FLOAT_EQ(child.rect.position.y, 8.0F - 0.5F * maxOffset);
    // ...and the thumb sits where the content is.
    const UIQuad* thumb = FindQuadByColor(Math::Vector3{0.0F, 1.0F, 0.0F});
    ASSERT_NE(thumb, nullptr);
    EXPECT_FLOAT_EQ(thumb->rect.position.y, 0.5F * (200.0F - 200.0F * (200.0F / contentH)));
}

TEST_F(UIScrollTest, ScrollbarDragPastEnds_ClampsToTopAndBottom) {
    const Scene::Entity scroll = MakeScroll(Math::Rect{{0.0F, 0.0F}, {200.0F, 200.0F}});
    MakeScrollbar(scroll, Math::Rect{{188.0F, 0.0F}, {12.0F, 200.0F}},
                     Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F});
    for (std::int64_t i = 0; i < 4; ++i) {
        Link(MakeButton(Math::Vector2{100.0F, 100.0F}), scroll, i);
    }
    Frame(Math::Vector2{0.0F, 0.0F}, false);
    const float maxOffset = ScrollOf(scroll).contentSize.y - 200.0F;
    ASSERT_GT(maxOffset, 0.0F);

    Frame(Math::Vector2{194.0F, 100.0F}, true);
    Frame(Math::Vector2{194.0F, -80.0F}, true);
    EXPECT_EQ(ScrollOf(scroll).scrollOffset.y, 0.0F);
    Frame(Math::Vector2{194.0F, 400.0F}, true);
    EXPECT_FLOAT_EQ(ScrollOf(scroll).scrollOffset.y, maxOffset);
}

TEST_F(UIScrollTest, ScrollbarRelease_StopsTrackingThePointer) {
    const Scene::Entity scroll = MakeScroll(Math::Rect{{0.0F, 0.0F}, {200.0F, 200.0F}});
    MakeScrollbar(scroll, Math::Rect{{188.0F, 0.0F}, {12.0F, 200.0F}},
                     Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F});
    for (std::int64_t i = 0; i < 4; ++i) {
        Link(MakeButton(Math::Vector2{100.0F, 100.0F}), scroll, i);
    }
    Frame(Math::Vector2{0.0F, 0.0F}, false);
    Frame(Math::Vector2{194.0F, 100.0F}, true);
    Frame(Math::Vector2{194.0F, 100.0F}, true);
    const float dragged = ScrollOf(scroll).scrollOffset.y;
    ASSERT_GT(dragged, 0.0F);

    Frame(Math::Vector2{194.0F, 100.0F}, false);
    EXPECT_FALSE(ScrollbarOf(scroll).isDragging);
    EXPECT_FALSE(ScrollbarOf(scroll).wasChangedThisFrame);
    Frame(Math::Vector2{194.0F, 180.0F}, false);
    EXPECT_EQ(ScrollOf(scroll).scrollOffset.y, dragged);
    EXPECT_FALSE(ScrollbarOf(scroll).isDragging);
}

TEST_F(UIScrollTest, SaveThenLoad_Scrollbar_RoundTripsConfigButNotDragState) {
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::UIScrollbarComponent authored;
    authored.rect = Math::Rect{{188.0F, 10.0F}, {12.0F, 180.0F}};
    authored.trackColor = Math::Vector3{0.25F, 0.25F, 0.25F};
    authored.thumbColor = Math::Vector3{0.75F, 0.5F, 0.25F};
    authored.minThumbHeight = 20.0F;
    authored.isHovered = true;
    authored.isDragging = true;
    authored.wasChangedThisFrame = true;
    entityManager.AddComponent<Scene::UIScrollbarComponent>(entity, authored);

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_scrollbar.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.Save(entityManager, {entity}, path, Scene::SceneAssetType::Scene));

    Scene::EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    const std::vector<Scene::Entity> loaded = serializer.Load(loadedManager, path);
    ASSERT_EQ(loaded.size(), 1U);
    const Scene::UIScrollbarComponent& restored =
        loadedManager.GetComponent<Scene::UIScrollbarComponent>(loaded[0]);
    EXPECT_EQ(restored.rect, (Math::Rect{{188.0F, 10.0F}, {12.0F, 180.0F}}));
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
