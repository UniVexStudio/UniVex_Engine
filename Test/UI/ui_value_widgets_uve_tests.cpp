// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <filesystem>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/ui_layout_container_component_uve.h"
#include "uve/component/ui_progress_bar_component_uve.h"
#include "uve/component/ui_slider_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/input/input_system_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/scene/scene_serializer_uve.h"
#include "uve/ui/ui_layout_uve.h"
#include "uve/ui/ui_runtime_uve.h"

namespace UVE::UI::Tests {
namespace {

class UIValueWidgetsUVETest : public ::testing::Test {
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

    Scene::EntityUVE MakeSliderUVE(const Math::RectUVE rect) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UISliderComponentUVE slider;
        slider.rect = rect;
        entityManager.AddComponentUVE<Scene::UISliderComponentUVE>(entity, slider);
        return entity;
    }

    Scene::EntityUVE MakeProgressUVE(const Math::RectUVE rect, const float value) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UIProgressBarComponentUVE bar;
        bar.rect = rect;
        bar.value = value;
        entityManager.AddComponentUVE<Scene::UIProgressBarComponentUVE>(entity, bar);
        return entity;
    }
};

TEST_F(UIValueWidgetsUVETest, Validators_RejectIncoherentRangesButClampWildValuesAtUse) {
    Scene::UISliderComponentUVE slider;
    EXPECT_TRUE(IsUISliderComponentValidUVE(slider));
    slider.value = 5.0F;
    EXPECT_TRUE(IsUISliderComponentValidUVE(slider)); // out of range clamps, never invalidates
    slider.value = 0.0F;
    slider.step = -0.25F;
    EXPECT_FALSE(IsUISliderComponentValidUVE(slider));
    slider.step = 0.0F;
    slider.minValue = 1.0F;
    slider.maxValue = 0.0F;
    EXPECT_FALSE(IsUISliderComponentValidUVE(slider));

    Scene::UIProgressBarComponentUVE bar;
    EXPECT_TRUE(IsUIProgressBarComponentValidUVE(bar));
    bar.minValue = 1.0F;
    bar.maxValue = 1.0F;
    EXPECT_TRUE(IsUIProgressBarComponentValidUVE(bar)); // degenerate range draws full-or-empty
    bar.maxValue = 0.0F;
    EXPECT_FALSE(IsUIProgressBarComponentValidUVE(bar));
}

TEST_F(UIValueWidgetsUVETest, ClickOnTrack_JumpsValueAndDrawsThreeQuads) {
    const Scene::EntityUVE slider = MakeSliderUVE(Math::RectUVE{{100.0F, 100.0F}, {200.0F, 20.0F}});

    FrameUVE({200.0F, 110.0F}, true);

    const Scene::UISliderComponentUVE& updated =
        entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(slider);
    EXPECT_FLOAT_EQ(updated.value, 0.5F);
    EXPECT_TRUE(updated.isHovered);
    EXPECT_TRUE(updated.isDragging);
    EXPECT_TRUE(updated.wasChangedThisFrame);
    const std::vector<UIQuadUVE>& quads = runtime.GetDrawBatchUVE().quads;
    ASSERT_EQ(quads.size(), 3U);
    EXPECT_EQ(quads[0].rect, (Math::RectUVE{{100.0F, 100.0F}, {200.0F, 20.0F}}));
    EXPECT_EQ(quads[0].color, updated.trackColor);
    EXPECT_EQ(quads[1].rect, (Math::RectUVE{{100.0F, 100.0F}, {100.0F, 20.0F}}));
    EXPECT_EQ(quads[1].color, updated.fillColor);
    EXPECT_EQ(quads[2].rect, (Math::RectUVE{{194.0F, 100.0F}, {12.0F, 20.0F}}));
    EXPECT_EQ(quads[2].color, updated.thumbColor);
}

TEST_F(UIValueWidgetsUVETest, Drag_TracksPointerUntilReleaseThenFreezes) {
    const Scene::EntityUVE slider = MakeSliderUVE(Math::RectUVE{{100.0F, 100.0F}, {200.0F, 20.0F}});

    FrameUVE({110.0F, 110.0F}, true);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(slider).value, 4.0F / 188.0F);
    FrameUVE({150.0F, 110.0F}, true);
    const Scene::UISliderComponentUVE& held =
        entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(slider);
    EXPECT_FLOAT_EQ(held.value, 44.0F / 188.0F);
    EXPECT_TRUE(held.isDragging);
    EXPECT_TRUE(held.wasChangedThisFrame);

    FrameUVE({150.0F, 110.0F}, false);
    const Scene::UISliderComponentUVE& released =
        entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(slider);
    EXPECT_FALSE(released.isDragging);
    EXPECT_FALSE(released.wasChangedThisFrame);
    EXPECT_FLOAT_EQ(released.value, 44.0F / 188.0F);

    FrameUVE({180.0F, 110.0F}, false);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(slider).value, 44.0F / 188.0F);
}

TEST_F(UIValueWidgetsUVETest, DragPastEnds_ClampsToMinAndMax) {
    const Scene::EntityUVE slider = MakeSliderUVE(Math::RectUVE{{100.0F, 100.0F}, {200.0F, 20.0F}});

    FrameUVE({150.0F, 110.0F}, true);
    FrameUVE({1000.0F, 110.0F}, true);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(slider).value, 1.0F);
    FrameUVE({-500.0F, 110.0F}, true);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(slider).value, 0.0F);
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(slider).isDragging);
}

TEST_F(UIValueWidgetsUVETest, Step_SnapsDraggedValue) {
    const Scene::EntityUVE slider = MakeSliderUVE(Math::RectUVE{{100.0F, 100.0F}, {200.0F, 20.0F}});
    entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(slider).step = 0.25F;

    FrameUVE({162.4F, 110.0F}, true); // fraction 0.3 snaps down to 0.25
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(slider).value, 0.25F);
    FrameUVE({275.2F, 110.0F}, true); // fraction 0.9 snaps up to 1.0
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(slider).value, 1.0F);
}

TEST_F(UIValueWidgetsUVETest, ClickOutsideTrack_DoesNothingButStillDraws) {
    const Scene::EntityUVE slider = MakeSliderUVE(Math::RectUVE{{100.0F, 100.0F}, {200.0F, 20.0F}});

    FrameUVE({50.0F, 50.0F}, true);

    const Scene::UISliderComponentUVE& updated =
        entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(slider);
    EXPECT_FLOAT_EQ(updated.value, 0.0F);
    EXPECT_FALSE(updated.isHovered);
    EXPECT_FALSE(updated.isDragging);
    EXPECT_FALSE(updated.wasChangedThisFrame);
    EXPECT_EQ(runtime.GetDrawBatchUVE().quads.size(), 3U);
}

TEST_F(UIValueWidgetsUVETest, InvalidSlider_EmitsNoQuadsAndResetsState) {
    const Scene::EntityUVE slider = MakeSliderUVE(Math::RectUVE{{100.0F, 100.0F}, {200.0F, 20.0F}});
    Scene::UISliderComponentUVE& broken = entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(slider);
    broken.minValue = 1.0F;
    broken.maxValue = 0.0F;
    broken.isDragging = true;

    FrameUVE({200.0F, 110.0F}, true);

    const Scene::UISliderComponentUVE& updated =
        entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(slider);
    EXPECT_FALSE(updated.isHovered);
    EXPECT_FALSE(updated.isDragging);
    EXPECT_FALSE(updated.wasChangedThisFrame);
    EXPECT_TRUE(runtime.GetDrawBatchUVE().quads.empty());
}

TEST_F(UIValueWidgetsUVETest, ProgressBar_EmitsBackgroundAndClampedFill) {
    const Scene::EntityUVE bar = MakeProgressUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 20.0F}}, 0.25F);

    FrameUVE({400.0F, 300.0F}, false);

    const std::vector<UIQuadUVE>& quads = runtime.GetDrawBatchUVE().quads;
    ASSERT_EQ(quads.size(), 2U);
    EXPECT_EQ(quads[0].rect, (Math::RectUVE{{0.0F, 0.0F}, {200.0F, 20.0F}}));
    EXPECT_EQ(quads[1].rect, (Math::RectUVE{{0.0F, 0.0F}, {50.0F, 20.0F}}));

    entityManager.GetComponentUVE<Scene::UIProgressBarComponentUVE>(bar).value = 5.0F;
    FrameUVE({400.0F, 300.0F}, false);
    EXPECT_EQ(runtime.GetDrawBatchUVE().quads[1].rect, (Math::RectUVE{{0.0F, 0.0F}, {200.0F, 20.0F}}));
}

TEST_F(UIValueWidgetsUVETest, Layout_SliderAndProgressStackLikeButtons) {
    const Scene::EntityUVE container = entityManager.CreateEntityUVE();
    Scene::UILayoutContainerComponentUVE layout;
    layout.rect = Math::RectUVE{{0.0F, 0.0F}, {300.0F, 300.0F}};
    layout.padding = 10.0F;
    entityManager.AddComponentUVE<Scene::UILayoutContainerComponentUVE>(container, layout);
    const Scene::EntityUVE slider = MakeSliderUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 24.0F}});
    const Scene::EntityUVE bar = MakeProgressUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 20.0F}}, 0.5F);
    Scene::HierarchyComponentUVE firstLink;
    firstLink.parent = container;
    firstLink.siblingOrder = 0;
    entityManager.AddComponentUVE<Scene::HierarchyComponentUVE>(slider, firstLink);
    Scene::HierarchyComponentUVE secondLink;
    secondLink.parent = container;
    secondLink.siblingOrder = 1;
    entityManager.AddComponentUVE<Scene::HierarchyComponentUVE>(bar, secondLink);

    LayoutUIContainersUVE(entityManager, runtime.GetFontAtlasUVE());

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(slider).rect.position,
              (Math::Vector2UVE{10.0F, 10.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIProgressBarComponentUVE>(bar).rect.position,
              (Math::Vector2UVE{10.0F, 34.0F}));
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(slider).rect.size,
              (Math::Vector2UVE{200.0F, 24.0F}));
}

TEST_F(UIValueWidgetsUVETest, SaveThenLoad_ValueWidgets_RoundTripExactly) {
    const Scene::EntityUVE slider = entityManager.CreateEntityUVE();
    Scene::UISliderComponentUVE sliderConfig;
    sliderConfig.rect = Math::RectUVE{{10.0F, 20.0F}, {240.0F, 28.0F}};
    sliderConfig.value = 0.75F;
    sliderConfig.minValue = -1.0F;
    sliderConfig.maxValue = 2.0F;
    sliderConfig.step = 0.05F;
    sliderConfig.trackColor = Math::Vector3UVE{0.1F, 0.1F, 0.1F};
    sliderConfig.fillColor = Math::Vector3UVE{0.2F, 0.4F, 0.8F};
    sliderConfig.thumbColor = Math::Vector3UVE{0.9F, 0.9F, 0.9F};
    sliderConfig.thumbWidth = 16.0F;
    sliderConfig.isHovered = true;
    sliderConfig.isDragging = true;
    sliderConfig.wasChangedThisFrame = true;
    entityManager.AddComponentUVE<Scene::UISliderComponentUVE>(slider, sliderConfig);
    const Scene::EntityUVE bar = entityManager.CreateEntityUVE();
    Scene::UIProgressBarComponentUVE barConfig;
    barConfig.rect = Math::RectUVE{{10.0F, 60.0F}, {240.0F, 16.0F}};
    barConfig.value = 0.3F;
    barConfig.fillColor = Math::Vector3UVE{0.9F, 0.2F, 0.2F};
    entityManager.AddComponentUVE<Scene::UIProgressBarComponentUVE>(bar, barConfig);

    const std::filesystem::path path = "uve_scene_serializer_tests_value_widgets.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {slider, bar}, path, Scene::SceneAssetTypeUVE::Scene));

    Scene::EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<Scene::EntityUVE> loaded = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(loaded.size(), 2U);
    const Scene::EntityUVE sliderEntity =
        loadedManager.HasComponentUVE<Scene::UISliderComponentUVE>(loaded[0]) ? loaded[0] : loaded[1];
    const Scene::EntityUVE barEntity = sliderEntity == loaded[0] ? loaded[1] : loaded[0];
    const Scene::UISliderComponentUVE& restoredSlider =
        loadedManager.GetComponentUVE<Scene::UISliderComponentUVE>(sliderEntity);
    EXPECT_FLOAT_EQ(restoredSlider.value, 0.75F);
    EXPECT_FLOAT_EQ(restoredSlider.minValue, -1.0F);
    EXPECT_FLOAT_EQ(restoredSlider.maxValue, 2.0F);
    EXPECT_FLOAT_EQ(restoredSlider.step, 0.05F);
    EXPECT_FLOAT_EQ(restoredSlider.thumbWidth, 16.0F);
    // Runtime state never round-trips: a mid-drag pointer would be stale on arrival, so the next
    // tick recomputes it from live input.
    EXPECT_FALSE(restoredSlider.isHovered);
    EXPECT_FALSE(restoredSlider.isDragging);
    EXPECT_FALSE(restoredSlider.wasChangedThisFrame);
    const Scene::UIProgressBarComponentUVE& restoredBar =
        loadedManager.GetComponentUVE<Scene::UIProgressBarComponentUVE>(barEntity);
    EXPECT_FLOAT_EQ(restoredBar.value, 0.3F);
    EXPECT_FLOAT_EQ(restoredBar.fillColor.x, 0.9F);

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::UI::Tests
