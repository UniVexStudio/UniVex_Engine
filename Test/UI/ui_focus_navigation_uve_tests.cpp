// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/ui_runtime_uve.h"

#include <gtest/gtest.h>

#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_checkbox_component_uve.h"
#include "uve/component/ui_dropdown_component_uve.h"
#include "uve/component/ui_slider_component_uve.h"
#include "uve/component/ui_text_input_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/input/input_system_uve.h"
#include "uve/memory/memory_manager_uve.h"

namespace UVE::UI::Tests {
namespace {

class UIFocusNavigationUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Input::InputSystemUVE inputSystem{eventSystem};
    UIRuntimeUVE runtime;

    void SetUp() override { runtime.SetViewportSizeUVE({800.0F, 600.0F}); }

    void TickUVE() {
        inputSystem.UpdateUVE();
        runtime.SetDeltaTimeUVE(0.016F);
        runtime.TickUVE(entityManager, inputSystem);
    }

    void KeyUVE(const Input::KeyCodeUVE key, const bool shift = false) {
        inputSystem.SetKeyStateUVE(Input::KeyCodeUVE::LeftShift, shift);
        inputSystem.SetKeyStateUVE(key, true);
        TickUVE();
        inputSystem.SetKeyStateUVE(key, false);
        inputSystem.SetKeyStateUVE(Input::KeyCodeUVE::LeftShift, false);
        inputSystem.UpdateUVE();
    }

    void MouseUVE(const Math::Vector2UVE position, const bool down) {
        inputSystem.SetMousePositionUVE(position);
        inputSystem.SetMouseButtonStateUVE(Input::MouseButtonUVE::Left, down);
        TickUVE();
    }

    Scene::EntityUVE MakeButtonUVE(const float y) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UIButtonComponentUVE button{};
        button.rect = Math::RectUVE{Math::Vector2UVE{10.0F, y}, Math::Vector2UVE{120.0F, 28.0F}};
        entityManager.AddComponentUVE<Scene::UIButtonComponentUVE>(entity, button);
        return entity;
    }

    Scene::EntityUVE MakeCheckboxUVE(const float y) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UICheckboxComponentUVE checkbox{};
        checkbox.rect = Math::RectUVE{Math::Vector2UVE{10.0F, y}, Math::Vector2UVE{24.0F, 24.0F}};
        entityManager.AddComponentUVE<Scene::UICheckboxComponentUVE>(entity, checkbox);
        return entity;
    }

    Scene::EntityUVE MakeSliderUVE(const float y) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UISliderComponentUVE slider{};
        slider.rect = Math::RectUVE{Math::Vector2UVE{10.0F, y}, Math::Vector2UVE{200.0F, 24.0F}};
        slider.value = 0.5F;
        entityManager.AddComponentUVE<Scene::UISliderComponentUVE>(entity, slider);
        return entity;
    }

    Scene::EntityUVE MakeDropdownUVE(const float y) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UIDropdownComponentUVE dropdown{};
        dropdown.rect = Math::RectUVE{Math::Vector2UVE{10.0F, y}, Math::Vector2UVE{160.0F, 28.0F}};
        dropdown.options = "Easy\nNormal\nHard";
        entityManager.AddComponentUVE<Scene::UIDropdownComponentUVE>(entity, dropdown);
        return entity;
    }

    Scene::EntityUVE MakeFieldUVE(const float y) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UITextInputComponentUVE field{};
        field.rect = Math::RectUVE{Math::Vector2UVE{10.0F, y}, Math::Vector2UVE{160.0F, 28.0F}};
        entityManager.AddComponentUVE<Scene::UITextInputComponentUVE>(entity, field);
        return entity;
    }
};

TEST_F(UIFocusNavigationUVETest, TabUsesReadingOrderRatherThanCreationOrderAndWraps) {
    const Scene::EntityUVE button = MakeButtonUVE(100.0F);
    const Scene::EntityUVE checkbox = MakeCheckboxUVE(10.0F);
    const Scene::EntityUVE slider = MakeSliderUVE(50.0F);

    KeyUVE(Input::KeyCodeUVE::Tab);
    EXPECT_EQ(runtime.GetFocusedEntityUVE(), checkbox);
    KeyUVE(Input::KeyCodeUVE::Tab);
    EXPECT_EQ(runtime.GetFocusedEntityUVE(), slider);
    KeyUVE(Input::KeyCodeUVE::Tab);
    EXPECT_EQ(runtime.GetFocusedEntityUVE(), button);
    KeyUVE(Input::KeyCodeUVE::Tab);
    EXPECT_EQ(runtime.GetFocusedEntityUVE(), checkbox);
    KeyUVE(Input::KeyCodeUVE::Tab, true);
    EXPECT_EQ(runtime.GetFocusedEntityUVE(), button);
}

TEST_F(UIFocusNavigationUVETest, EnterClicksFocusedButtonAndSpaceTogglesFocusedCheckbox) {
    const Scene::EntityUVE button = MakeButtonUVE(10.0F);
    const Scene::EntityUVE checkbox = MakeCheckboxUVE(50.0F);

    KeyUVE(Input::KeyCodeUVE::Tab);
    ASSERT_EQ(runtime.GetFocusedEntityUVE(), button);
    KeyUVE(Input::KeyCodeUVE::Enter);
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(button).wasClickedThisFrame);

    KeyUVE(Input::KeyCodeUVE::Tab);
    ASSERT_EQ(runtime.GetFocusedEntityUVE(), checkbox);
    KeyUVE(Input::KeyCodeUVE::Space);
    const Scene::UICheckboxComponentUVE& updated =
        entityManager.GetComponentUVE<Scene::UICheckboxComponentUVE>(checkbox);
    EXPECT_TRUE(updated.wasToggledThisFrame);
    EXPECT_TRUE(updated.checked);
}

TEST_F(UIFocusNavigationUVETest, FocusedSliderRespondsToArrowKeysWithStep) {
    const Scene::EntityUVE sliderEntity = MakeSliderUVE(10.0F);
    Scene::UISliderComponentUVE& slider = entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(sliderEntity);
    slider.step = 0.25F;
    slider.value = 0.5F;

    KeyUVE(Input::KeyCodeUVE::Tab);
    KeyUVE(Input::KeyCodeUVE::Right);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(sliderEntity).value, 0.75F);
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(sliderEntity).wasChangedThisFrame);
    KeyUVE(Input::KeyCodeUVE::Left);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(sliderEntity).value, 0.5F);
}

TEST_F(UIFocusNavigationUVETest, DropdownOpensMovesAndSelectsFromKeyboard) {
    const Scene::EntityUVE dropdownEntity = MakeDropdownUVE(10.0F);

    KeyUVE(Input::KeyCodeUVE::Tab);
    KeyUVE(Input::KeyCodeUVE::Enter);
    Scene::UIDropdownComponentUVE& dropdown =
        entityManager.GetComponentUVE<Scene::UIDropdownComponentUVE>(dropdownEntity);
    EXPECT_TRUE(dropdown.open);
    EXPECT_EQ(dropdown.hoveredIndex, 0);

    KeyUVE(Input::KeyCodeUVE::Down);
    EXPECT_EQ(dropdown.hoveredIndex, 1);
    KeyUVE(Input::KeyCodeUVE::Enter);
    EXPECT_EQ(dropdown.selectedIndex, 1);
    EXPECT_FALSE(dropdown.open);
    EXPECT_TRUE(dropdown.wasSelectionChangedThisFrame);
}

TEST_F(UIFocusNavigationUVETest, TabFocusesTextFieldForTypingAndEscapeReleasesIt) {
    static_cast<void>(MakeButtonUVE(10.0F));
    const Scene::EntityUVE fieldEntity = MakeFieldUVE(50.0F);

    KeyUVE(Input::KeyCodeUVE::Tab);
    KeyUVE(Input::KeyCodeUVE::Tab);
    ASSERT_EQ(runtime.GetFocusedEntityUVE(), fieldEntity);
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::UITextInputComponentUVE>(fieldEntity).focused);

    KeyUVE(Input::KeyCodeUVE::A);
    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UITextInputComponentUVE>(fieldEntity).text, "a");
    KeyUVE(Input::KeyCodeUVE::Escape);
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::UITextInputComponentUVE>(fieldEntity).focused);
    EXPECT_EQ(runtime.GetFocusedEntityUVE(), Scene::kInvalidEntityUVE);
}

TEST_F(UIFocusNavigationUVETest, MouseFocusedTextFieldBecomesTheNextTabStart) {
    const Scene::EntityUVE fieldEntity = MakeFieldUVE(10.0F);
    const Scene::EntityUVE button = MakeButtonUVE(50.0F);

    MouseUVE({50.0F, 20.0F}, true);
    MouseUVE({50.0F, 20.0F}, false);
    ASSERT_TRUE(entityManager.GetComponentUVE<Scene::UITextInputComponentUVE>(fieldEntity).focused);
    EXPECT_EQ(runtime.GetFocusedEntityUVE(), fieldEntity);

    KeyUVE(Input::KeyCodeUVE::Tab);
    EXPECT_EQ(runtime.GetFocusedEntityUVE(), button);
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::UITextInputComponentUVE>(fieldEntity).focused);
}

TEST_F(UIFocusNavigationUVETest, DestroyedFocusedWidgetLosesFocusOnNextTick) {
    const Scene::EntityUVE button = MakeButtonUVE(10.0F);
    KeyUVE(Input::KeyCodeUVE::Tab);
    ASSERT_EQ(runtime.GetFocusedEntityUVE(), button);

    entityManager.DestroyEntityUVE(button);
    TickUVE();
    EXPECT_EQ(runtime.GetFocusedEntityUVE(), Scene::kInvalidEntityUVE);
}

} // namespace
} // namespace UVE::UI::Tests
