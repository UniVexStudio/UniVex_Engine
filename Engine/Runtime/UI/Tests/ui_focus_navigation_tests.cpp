// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_runtime.h"

#include <gtest/gtest.h>

#include "ui_button_component.h"
#include "ui_checkbox_component.h"
#include "ui_dropdown_component.h"
#include "ui_slider_component.h"
#include "ui_text_input_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "input_system.h"
#include "memory_manager.h"

namespace UVE::UI::Tests {
namespace {

class UIFocusNavigationTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Input::InputSystem inputSystem{eventSystem};
    UIRuntime runtime;

    void SetUp() override { runtime.SetViewportSize({800.0F, 600.0F}); }

    void Tick() {
        inputSystem.Update();
        runtime.SetDeltaTime(0.016F);
        runtime.Tick(entityManager, inputSystem);
    }

    void Key(const Input::KeyCode key, const bool shift = false) {
        inputSystem.SetKeyState(Input::KeyCode::LeftShift, shift);
        inputSystem.SetKeyState(key, true);
        Tick();
        inputSystem.SetKeyState(key, false);
        inputSystem.SetKeyState(Input::KeyCode::LeftShift, false);
        inputSystem.Update();
    }

    void Mouse(const Math::Vector2 position, const bool down) {
        inputSystem.SetMousePosition(position);
        inputSystem.SetMouseButtonState(Input::MouseButton::Left, down);
        Tick();
    }

    Scene::Entity MakeButton(const float y) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UIButtonComponent button{};
        button.rect = Math::Rect{Math::Vector2{10.0F, y}, Math::Vector2{120.0F, 28.0F}};
        entityManager.AddComponent<Scene::UIButtonComponent>(entity, button);
        return entity;
    }

    Scene::Entity MakeCheckbox(const float y) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UICheckboxComponent checkbox{};
        checkbox.rect = Math::Rect{Math::Vector2{10.0F, y}, Math::Vector2{24.0F, 24.0F}};
        entityManager.AddComponent<Scene::UICheckboxComponent>(entity, checkbox);
        return entity;
    }

    Scene::Entity MakeSlider(const float y) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UISliderComponent slider{};
        slider.rect = Math::Rect{Math::Vector2{10.0F, y}, Math::Vector2{200.0F, 24.0F}};
        slider.value = 0.5F;
        entityManager.AddComponent<Scene::UISliderComponent>(entity, slider);
        return entity;
    }

    Scene::Entity MakeDropdown(const float y) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UIDropdownComponent dropdown{};
        dropdown.rect = Math::Rect{Math::Vector2{10.0F, y}, Math::Vector2{160.0F, 28.0F}};
        dropdown.options = "Easy\nNormal\nHard";
        entityManager.AddComponent<Scene::UIDropdownComponent>(entity, dropdown);
        return entity;
    }

    Scene::Entity MakeField(const float y) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UITextInputComponent field{};
        field.rect = Math::Rect{Math::Vector2{10.0F, y}, Math::Vector2{160.0F, 28.0F}};
        entityManager.AddComponent<Scene::UITextInputComponent>(entity, field);
        return entity;
    }
};

TEST_F(UIFocusNavigationTest, TabUsesReadingOrderRatherThanCreationOrderAndWraps) {
    const Scene::Entity button = MakeButton(100.0F);
    const Scene::Entity checkbox = MakeCheckbox(10.0F);
    const Scene::Entity slider = MakeSlider(50.0F);

    Key(Input::KeyCode::Tab);
    EXPECT_EQ(runtime.GetFocusedEntity(), checkbox);
    Key(Input::KeyCode::Tab);
    EXPECT_EQ(runtime.GetFocusedEntity(), slider);
    Key(Input::KeyCode::Tab);
    EXPECT_EQ(runtime.GetFocusedEntity(), button);
    Key(Input::KeyCode::Tab);
    EXPECT_EQ(runtime.GetFocusedEntity(), checkbox);
    Key(Input::KeyCode::Tab, true);
    EXPECT_EQ(runtime.GetFocusedEntity(), button);
}

TEST_F(UIFocusNavigationTest, EnterClicksFocusedButtonAndSpaceTogglesFocusedCheckbox) {
    const Scene::Entity button = MakeButton(10.0F);
    const Scene::Entity checkbox = MakeCheckbox(50.0F);

    Key(Input::KeyCode::Tab);
    ASSERT_EQ(runtime.GetFocusedEntity(), button);
    Key(Input::KeyCode::Enter);
    EXPECT_TRUE(entityManager.GetComponent<Scene::UIButtonComponent>(button).wasClickedThisFrame);

    Key(Input::KeyCode::Tab);
    ASSERT_EQ(runtime.GetFocusedEntity(), checkbox);
    Key(Input::KeyCode::Space);
    const Scene::UICheckboxComponent& updated =
        entityManager.GetComponent<Scene::UICheckboxComponent>(checkbox);
    EXPECT_TRUE(updated.wasToggledThisFrame);
    EXPECT_TRUE(updated.checked);
}

TEST_F(UIFocusNavigationTest, FocusedSliderRespondsToArrowKeysWithStep) {
    const Scene::Entity sliderEntity = MakeSlider(10.0F);
    Scene::UISliderComponent& slider = entityManager.GetComponent<Scene::UISliderComponent>(sliderEntity);
    slider.step = 0.25F;
    slider.value = 0.5F;

    Key(Input::KeyCode::Tab);
    Key(Input::KeyCode::Right);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UISliderComponent>(sliderEntity).value, 0.75F);
    EXPECT_TRUE(entityManager.GetComponent<Scene::UISliderComponent>(sliderEntity).wasChangedThisFrame);
    Key(Input::KeyCode::Left);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UISliderComponent>(sliderEntity).value, 0.5F);
}

TEST_F(UIFocusNavigationTest, DropdownOpensMovesAndSelectsFromKeyboard) {
    const Scene::Entity dropdownEntity = MakeDropdown(10.0F);

    Key(Input::KeyCode::Tab);
    Key(Input::KeyCode::Enter);
    Scene::UIDropdownComponent& dropdown =
        entityManager.GetComponent<Scene::UIDropdownComponent>(dropdownEntity);
    EXPECT_TRUE(dropdown.open);
    EXPECT_EQ(dropdown.hoveredIndex, 0);

    Key(Input::KeyCode::Down);
    EXPECT_EQ(dropdown.hoveredIndex, 1);
    Key(Input::KeyCode::Enter);
    EXPECT_EQ(dropdown.selectedIndex, 1);
    EXPECT_FALSE(dropdown.open);
    EXPECT_TRUE(dropdown.wasSelectionChangedThisFrame);
}

TEST_F(UIFocusNavigationTest, TabFocusesTextFieldForTypingAndEscapeReleasesIt) {
    static_cast<void>(MakeButton(10.0F));
    const Scene::Entity fieldEntity = MakeField(50.0F);

    Key(Input::KeyCode::Tab);
    Key(Input::KeyCode::Tab);
    ASSERT_EQ(runtime.GetFocusedEntity(), fieldEntity);
    EXPECT_TRUE(entityManager.GetComponent<Scene::UITextInputComponent>(fieldEntity).focused);

    Key(Input::KeyCode::A);
    EXPECT_EQ(entityManager.GetComponent<Scene::UITextInputComponent>(fieldEntity).text, "a");
    Key(Input::KeyCode::Escape);
    EXPECT_FALSE(entityManager.GetComponent<Scene::UITextInputComponent>(fieldEntity).focused);
    EXPECT_EQ(runtime.GetFocusedEntity(), Scene::kInvalidEntity);
}

TEST_F(UIFocusNavigationTest, MouseFocusedTextFieldBecomesTheNextTabStart) {
    const Scene::Entity fieldEntity = MakeField(10.0F);
    const Scene::Entity button = MakeButton(50.0F);

    Mouse({50.0F, 20.0F}, true);
    Mouse({50.0F, 20.0F}, false);
    ASSERT_TRUE(entityManager.GetComponent<Scene::UITextInputComponent>(fieldEntity).focused);
    EXPECT_EQ(runtime.GetFocusedEntity(), fieldEntity);

    Key(Input::KeyCode::Tab);
    EXPECT_EQ(runtime.GetFocusedEntity(), button);
    EXPECT_FALSE(entityManager.GetComponent<Scene::UITextInputComponent>(fieldEntity).focused);
}

TEST_F(UIFocusNavigationTest, DestroyedFocusedWidgetLosesFocusOnNextTick) {
    const Scene::Entity button = MakeButton(10.0F);
    Key(Input::KeyCode::Tab);
    ASSERT_EQ(runtime.GetFocusedEntity(), button);

    entityManager.DestroyEntity(button);
    Tick();
    EXPECT_EQ(runtime.GetFocusedEntity(), Scene::kInvalidEntity);
}

} // namespace
} // namespace UVE::UI::Tests
