// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <filesystem>

#include <gtest/gtest.h>

#include "entity.h"
#include "hierarchy_component.h"
#include "ui_layout_container_component.h"
#include "ui_progress_bar_component.h"
#include "ui_slider_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "input_system.h"
#include "rect.h"
#include "vector2.h"
#include "vector3.h"
#include "memory_manager.h"
#include "scene_serializer.h"
#include "ui_layout.h"
#include "ui_runtime.h"

namespace UVE::UI::Tests {
namespace {

class UIValueWidgetsTest : public ::testing::Test {
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

    Scene::Entity MakeSlider(const Math::Rect rect) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UISliderComponent slider;
        slider.rect = rect;
        entityManager.AddComponent<Scene::UISliderComponent>(entity, slider);
        return entity;
    }

    Scene::Entity MakeProgress(const Math::Rect rect, const float value) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UIProgressBarComponent bar;
        bar.rect = rect;
        bar.value = value;
        entityManager.AddComponent<Scene::UIProgressBarComponent>(entity, bar);
        return entity;
    }
};

TEST_F(UIValueWidgetsTest, Validators_RejectIncoherentRangesButClampWildValuesAtUse) {
    Scene::UISliderComponent slider;
    EXPECT_TRUE(IsUISliderComponentValid(slider));
    slider.value = 5.0F;
    EXPECT_TRUE(IsUISliderComponentValid(slider)); // out of range clamps, never invalidates
    slider.value = 0.0F;
    slider.step = -0.25F;
    EXPECT_FALSE(IsUISliderComponentValid(slider));
    slider.step = 0.0F;
    slider.minValue = 1.0F;
    slider.maxValue = 0.0F;
    EXPECT_FALSE(IsUISliderComponentValid(slider));

    Scene::UIProgressBarComponent bar;
    EXPECT_TRUE(IsUIProgressBarComponentValid(bar));
    bar.minValue = 1.0F;
    bar.maxValue = 1.0F;
    EXPECT_TRUE(IsUIProgressBarComponentValid(bar)); // degenerate range draws full-or-empty
    bar.maxValue = 0.0F;
    EXPECT_FALSE(IsUIProgressBarComponentValid(bar));
}

TEST_F(UIValueWidgetsTest, ClickOnTrack_JumpsValueAndDrawsThreeQuads) {
    const Scene::Entity slider = MakeSlider(Math::Rect{{100.0F, 100.0F}, {200.0F, 20.0F}});

    Frame({200.0F, 110.0F}, true);

    const Scene::UISliderComponent& updated =
        entityManager.GetComponent<Scene::UISliderComponent>(slider);
    EXPECT_FLOAT_EQ(updated.value, 0.5F);
    EXPECT_TRUE(updated.isHovered);
    EXPECT_TRUE(updated.isDragging);
    EXPECT_TRUE(updated.wasChangedThisFrame);
    const std::vector<UIQuad>& quads = runtime.GetDrawBatch().quads;
    ASSERT_EQ(quads.size(), 3U);
    EXPECT_EQ(quads[0].rect, (Math::Rect{{100.0F, 100.0F}, {200.0F, 20.0F}}));
    EXPECT_EQ(quads[0].color, updated.trackColor);
    EXPECT_EQ(quads[1].rect, (Math::Rect{{100.0F, 100.0F}, {100.0F, 20.0F}}));
    EXPECT_EQ(quads[1].color, updated.fillColor);
    EXPECT_EQ(quads[2].rect, (Math::Rect{{194.0F, 100.0F}, {12.0F, 20.0F}}));
    EXPECT_EQ(quads[2].color, updated.thumbColor);
}

TEST_F(UIValueWidgetsTest, Drag_TracksPointerUntilReleaseThenFreezes) {
    const Scene::Entity slider = MakeSlider(Math::Rect{{100.0F, 100.0F}, {200.0F, 20.0F}});

    Frame({110.0F, 110.0F}, true);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UISliderComponent>(slider).value, 4.0F / 188.0F);
    Frame({150.0F, 110.0F}, true);
    const Scene::UISliderComponent& held =
        entityManager.GetComponent<Scene::UISliderComponent>(slider);
    EXPECT_FLOAT_EQ(held.value, 44.0F / 188.0F);
    EXPECT_TRUE(held.isDragging);
    EXPECT_TRUE(held.wasChangedThisFrame);

    Frame({150.0F, 110.0F}, false);
    const Scene::UISliderComponent& released =
        entityManager.GetComponent<Scene::UISliderComponent>(slider);
    EXPECT_FALSE(released.isDragging);
    EXPECT_FALSE(released.wasChangedThisFrame);
    EXPECT_FLOAT_EQ(released.value, 44.0F / 188.0F);

    Frame({180.0F, 110.0F}, false);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UISliderComponent>(slider).value, 44.0F / 188.0F);
}

TEST_F(UIValueWidgetsTest, DragPastEnds_ClampsToMinAndMax) {
    const Scene::Entity slider = MakeSlider(Math::Rect{{100.0F, 100.0F}, {200.0F, 20.0F}});

    Frame({150.0F, 110.0F}, true);
    Frame({1000.0F, 110.0F}, true);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UISliderComponent>(slider).value, 1.0F);
    Frame({-500.0F, 110.0F}, true);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UISliderComponent>(slider).value, 0.0F);
    EXPECT_TRUE(entityManager.GetComponent<Scene::UISliderComponent>(slider).isDragging);
}

TEST_F(UIValueWidgetsTest, Step_SnapsDraggedValue) {
    const Scene::Entity slider = MakeSlider(Math::Rect{{100.0F, 100.0F}, {200.0F, 20.0F}});
    entityManager.GetComponent<Scene::UISliderComponent>(slider).step = 0.25F;

    Frame({162.4F, 110.0F}, true); // fraction 0.3 snaps down to 0.25
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UISliderComponent>(slider).value, 0.25F);
    Frame({275.2F, 110.0F}, true); // fraction 0.9 snaps up to 1.0
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UISliderComponent>(slider).value, 1.0F);
}

TEST_F(UIValueWidgetsTest, ClickOutsideTrack_DoesNothingButStillDraws) {
    const Scene::Entity slider = MakeSlider(Math::Rect{{100.0F, 100.0F}, {200.0F, 20.0F}});

    Frame({50.0F, 50.0F}, true);

    const Scene::UISliderComponent& updated =
        entityManager.GetComponent<Scene::UISliderComponent>(slider);
    EXPECT_FLOAT_EQ(updated.value, 0.0F);
    EXPECT_FALSE(updated.isHovered);
    EXPECT_FALSE(updated.isDragging);
    EXPECT_FALSE(updated.wasChangedThisFrame);
    EXPECT_EQ(runtime.GetDrawBatch().quads.size(), 3U);
}

TEST_F(UIValueWidgetsTest, InvalidSlider_EmitsNoQuadsAndResetsState) {
    const Scene::Entity slider = MakeSlider(Math::Rect{{100.0F, 100.0F}, {200.0F, 20.0F}});
    Scene::UISliderComponent& broken = entityManager.GetComponent<Scene::UISliderComponent>(slider);
    broken.minValue = 1.0F;
    broken.maxValue = 0.0F;
    broken.isDragging = true;

    Frame({200.0F, 110.0F}, true);

    const Scene::UISliderComponent& updated =
        entityManager.GetComponent<Scene::UISliderComponent>(slider);
    EXPECT_FALSE(updated.isHovered);
    EXPECT_FALSE(updated.isDragging);
    EXPECT_FALSE(updated.wasChangedThisFrame);
    EXPECT_TRUE(runtime.GetDrawBatch().quads.empty());
}

TEST_F(UIValueWidgetsTest, ProgressBar_EmitsBackgroundAndClampedFill) {
    const Scene::Entity bar = MakeProgress(Math::Rect{{0.0F, 0.0F}, {200.0F, 20.0F}}, 0.25F);

    Frame({400.0F, 300.0F}, false);

    const std::vector<UIQuad>& quads = runtime.GetDrawBatch().quads;
    ASSERT_EQ(quads.size(), 2U);
    EXPECT_EQ(quads[0].rect, (Math::Rect{{0.0F, 0.0F}, {200.0F, 20.0F}}));
    EXPECT_EQ(quads[1].rect, (Math::Rect{{0.0F, 0.0F}, {50.0F, 20.0F}}));

    entityManager.GetComponent<Scene::UIProgressBarComponent>(bar).value = 5.0F;
    Frame({400.0F, 300.0F}, false);
    EXPECT_EQ(runtime.GetDrawBatch().quads[1].rect, (Math::Rect{{0.0F, 0.0F}, {200.0F, 20.0F}}));
}

TEST_F(UIValueWidgetsTest, Layout_SliderAndProgressStackLikeButtons) {
    const Scene::Entity container = entityManager.CreateEntity();
    Scene::UILayoutContainerComponent layout;
    layout.rect = Math::Rect{{0.0F, 0.0F}, {300.0F, 300.0F}};
    layout.padding = 10.0F;
    entityManager.AddComponent<Scene::UILayoutContainerComponent>(container, layout);
    const Scene::Entity slider = MakeSlider(Math::Rect{{0.0F, 0.0F}, {200.0F, 24.0F}});
    const Scene::Entity bar = MakeProgress(Math::Rect{{0.0F, 0.0F}, {200.0F, 20.0F}}, 0.5F);
    Scene::HierarchyComponent firstLink;
    firstLink.parent = container;
    firstLink.siblingOrder = 0;
    entityManager.AddComponent<Scene::HierarchyComponent>(slider, firstLink);
    Scene::HierarchyComponent secondLink;
    secondLink.parent = container;
    secondLink.siblingOrder = 1;
    entityManager.AddComponent<Scene::HierarchyComponent>(bar, secondLink);

    LayoutUIContainers(entityManager, runtime.GetFontAtlas());

    EXPECT_EQ(entityManager.GetComponent<Scene::UISliderComponent>(slider).rect.position,
              (Math::Vector2{10.0F, 10.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UIProgressBarComponent>(bar).rect.position,
              (Math::Vector2{10.0F, 34.0F}));
    EXPECT_EQ(entityManager.GetComponent<Scene::UISliderComponent>(slider).rect.size,
              (Math::Vector2{200.0F, 24.0F}));
}

TEST_F(UIValueWidgetsTest, SaveThenLoad_ValueWidgets_RoundTripExactly) {
    const Scene::Entity slider = entityManager.CreateEntity();
    Scene::UISliderComponent sliderConfig;
    sliderConfig.rect = Math::Rect{{10.0F, 20.0F}, {240.0F, 28.0F}};
    sliderConfig.value = 0.75F;
    sliderConfig.minValue = -1.0F;
    sliderConfig.maxValue = 2.0F;
    sliderConfig.step = 0.05F;
    sliderConfig.trackColor = Math::Vector3{0.1F, 0.1F, 0.1F};
    sliderConfig.fillColor = Math::Vector3{0.2F, 0.4F, 0.8F};
    sliderConfig.thumbColor = Math::Vector3{0.9F, 0.9F, 0.9F};
    sliderConfig.thumbWidth = 16.0F;
    sliderConfig.isHovered = true;
    sliderConfig.isDragging = true;
    sliderConfig.wasChangedThisFrame = true;
    entityManager.AddComponent<Scene::UISliderComponent>(slider, sliderConfig);
    const Scene::Entity bar = entityManager.CreateEntity();
    Scene::UIProgressBarComponent barConfig;
    barConfig.rect = Math::Rect{{10.0F, 60.0F}, {240.0F, 16.0F}};
    barConfig.value = 0.3F;
    barConfig.fillColor = Math::Vector3{0.9F, 0.2F, 0.2F};
    entityManager.AddComponent<Scene::UIProgressBarComponent>(bar, barConfig);

    const std::filesystem::path path = "uve_scene_serializer_tests_value_widgets.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.Save(entityManager, {slider, bar}, path, Scene::SceneAssetType::Scene));

    Scene::EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    const std::vector<Scene::Entity> loaded = serializer.Load(loadedManager, path);
    ASSERT_EQ(loaded.size(), 2U);
    const Scene::Entity sliderEntity =
        loadedManager.HasComponent<Scene::UISliderComponent>(loaded[0]) ? loaded[0] : loaded[1];
    const Scene::Entity barEntity = sliderEntity == loaded[0] ? loaded[1] : loaded[0];
    const Scene::UISliderComponent& restoredSlider =
        loadedManager.GetComponent<Scene::UISliderComponent>(sliderEntity);
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
    const Scene::UIProgressBarComponent& restoredBar =
        loadedManager.GetComponent<Scene::UIProgressBarComponent>(barEntity);
    EXPECT_FLOAT_EQ(restoredBar.value, 0.3F);
    EXPECT_FLOAT_EQ(restoredBar.fillColor.x, 0.9F);

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::UI::Tests
