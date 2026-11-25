// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "entity.h"
#include "hierarchy_component.h"
#include "ui_anchor_component.h"
#include "ui_layout_container_component.h"
#include "ui_text_input_component.h"
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

class UITextInputTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Input::InputSystem inputSystem{eventSystem};
    UIRuntime runtime;
    Scene::SceneSerializer serializer;

    void Frame(const Math::Vector2 mousePosition, const bool mouseDown, const float dt = 0.016F) {
        inputSystem.SetMousePosition(mousePosition);
        inputSystem.SetMouseButtonState(Input::MouseButton::Left, mouseDown);
        inputSystem.Update();
        runtime.SetDeltaTime(dt);
        runtime.Tick(entityManager, inputSystem);
    }

    void Key(const Input::KeyCode key, const bool shift = false) {
        inputSystem.SetKeyState(Input::KeyCode::LeftShift, shift);
        inputSystem.SetKeyState(key, true);
        inputSystem.Update();
        runtime.SetDeltaTime(0.016F);
        runtime.Tick(entityManager, inputSystem);
        inputSystem.SetKeyState(key, false);
        inputSystem.SetKeyState(Input::KeyCode::LeftShift, false);
        // Commit the release: without this Update, pressing the same key twice in a row would
        // read as still held and the second press would never edge.
        inputSystem.Update();
    }

    Scene::Entity MakeField(const Math::Rect rect) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UITextInputComponent field;
        field.rect = rect;
        entityManager.AddComponent<Scene::UITextInputComponent>(entity, field);
        return entity;
    }

    const Scene::UITextInputComponent& FieldOf(const Scene::Entity entity) {
        return entityManager.GetComponent<Scene::UITextInputComponent>(entity);
    }

    std::size_t CaretQuadCount() {
        std::size_t carets = 0U;
        for (const UIQuad& quad : runtime.GetDrawBatch().quads) {
            if (quad.kind == UIDrawItemKind::SolidColor && quad.rect.size.x == 2.0F &&
                quad.rect.size.y == 16.0F) {
                ++carets;
            }
        }
        return carets;
    }
};

TEST_F(UITextInputTest, ValidatorAcceptsDefaultsRejectsBadConfig) {
    Scene::UITextInputComponent field;
    EXPECT_TRUE(IsUITextInputComponentValid(field));
    field.maxLength = 0;
    EXPECT_FALSE(IsUITextInputComponentValid(field));
    field.maxLength = 32;
    field.text = std::string(Scene::kMaximumUITextBytes + 1U, 'x');
    EXPECT_FALSE(IsUITextInputComponentValid(field));
    field.text.clear();
    field.caretIndex = -1;
    EXPECT_FALSE(IsUITextInputComponentValid(field));
}

TEST_F(UITextInputTest, Click_FocusesClickElsewhereBlursAndSwitches) {
    const Scene::Entity first = MakeField(Math::Rect{{10.0F, 10.0F}, {160.0F, 28.0F}});
    const Scene::Entity second = MakeField(Math::Rect{{10.0F, 100.0F}, {160.0F, 28.0F}});
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({50.0F, 20.0F}, true);
    Frame({50.0F, 20.0F}, false);
    EXPECT_TRUE(FieldOf(first).focused);
    EXPECT_FALSE(FieldOf(second).focused);

    Frame({50.0F, 110.0F}, true);
    Frame({50.0F, 110.0F}, false);
    EXPECT_FALSE(FieldOf(first).focused);
    EXPECT_TRUE(FieldOf(second).focused);

    Frame({700.0F, 500.0F}, true);
    Frame({700.0F, 500.0F}, false);
    EXPECT_FALSE(FieldOf(first).focused);
    EXPECT_FALSE(FieldOf(second).focused);
}

TEST_F(UITextInputTest, Typing_InsertsAtCaretWithShiftAndDigits) {
    const Scene::Entity field = MakeField(Math::Rect{{10.0F, 10.0F}, {160.0F, 28.0F}});
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({50.0F, 20.0F}, true);
    Frame({50.0F, 20.0F}, false);
    Key(Input::KeyCode::A);
    Key(Input::KeyCode::B, true);
    Key(Input::KeyCode::Space);
    Key(Input::KeyCode::Num1);
    Key(Input::KeyCode::Num1, true);

    EXPECT_EQ(FieldOf(field).text, "aB 1!");
    EXPECT_EQ(FieldOf(field).caretIndex, 5);
}

TEST_F(UITextInputTest, Backspace_DeletesBeforeCaret) {
    const Scene::Entity field = MakeField(Math::Rect{{10.0F, 10.0F}, {160.0F, 28.0F}});
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({50.0F, 20.0F}, true);
    Frame({50.0F, 20.0F}, false);
    Key(Input::KeyCode::A);
    Key(Input::KeyCode::B);
    Key(Input::KeyCode::C);
    Key(Input::KeyCode::Backspace);
    EXPECT_EQ(FieldOf(field).text, "ab");
    EXPECT_EQ(FieldOf(field).caretIndex, 2);

    Key(Input::KeyCode::Left);
    Key(Input::KeyCode::Left);
    Key(Input::KeyCode::Backspace);
    EXPECT_EQ(FieldOf(field).text, "ab");
    EXPECT_EQ(FieldOf(field).caretIndex, 0);
}

TEST_F(UITextInputTest, Arrows_MoveCaretAndInsertGoesMidString) {
    const Scene::Entity field = MakeField(Math::Rect{{10.0F, 10.0F}, {160.0F, 28.0F}});
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({50.0F, 20.0F}, true);
    Frame({50.0F, 20.0F}, false);
    Key(Input::KeyCode::A);
    Key(Input::KeyCode::C);
    Key(Input::KeyCode::Left);
    EXPECT_EQ(FieldOf(field).caretIndex, 1);
    Key(Input::KeyCode::B);
    EXPECT_EQ(FieldOf(field).text, "abc");
    EXPECT_EQ(FieldOf(field).caretIndex, 2);

    Key(Input::KeyCode::Right);
    Key(Input::KeyCode::Right);
    EXPECT_EQ(FieldOf(field).caretIndex, 3);
}

TEST_F(UITextInputTest, Enter_SubmitsWithOneShot_Escape_Blurs) {
    const Scene::Entity field = MakeField(Math::Rect{{10.0F, 10.0F}, {160.0F, 28.0F}});
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({50.0F, 20.0F}, true);
    Frame({50.0F, 20.0F}, false);
    Key(Input::KeyCode::G);
    Key(Input::KeyCode::O);
    Key(Input::KeyCode::Enter);
    EXPECT_TRUE(FieldOf(field).wasSubmittedThisFrame);
    EXPECT_TRUE(FieldOf(field).focused) << "submit keeps focus";

    Frame({50.0F, 20.0F}, false);
    EXPECT_FALSE(FieldOf(field).wasSubmittedThisFrame);

    Key(Input::KeyCode::Escape);
    EXPECT_FALSE(FieldOf(field).focused);
    EXPECT_FALSE(FieldOf(field).wasSubmittedThisFrame);
}

TEST_F(UITextInputTest, MaxLength_BlocksFurtherInput) {
    const Scene::Entity field = MakeField(Math::Rect{{10.0F, 10.0F}, {160.0F, 28.0F}});
    entityManager.GetComponent<Scene::UITextInputComponent>(field).maxLength = 2;
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({50.0F, 20.0F}, true);
    Frame({50.0F, 20.0F}, false);
    Key(Input::KeyCode::A);
    Key(Input::KeyCode::B);
    Key(Input::KeyCode::C);

    EXPECT_EQ(FieldOf(field).text, "ab");
    EXPECT_EQ(FieldOf(field).caretIndex, 2);
}

TEST_F(UITextInputTest, Placeholder_CaretAndBlink) {
    const Scene::Entity field = MakeField(Math::Rect{{10.0F, 10.0F}, {160.0F, 28.0F}});
    Scene::UITextInputComponent& authored =
        entityManager.GetComponent<Scene::UITextInputComponent>(field);
    authored.placeholder = "Name";
    runtime.SetViewportSize({800.0F, 600.0F});

    Frame({50.0F, 20.0F}, true);
    Frame({50.0F, 20.0F}, false);

    const std::vector<UIQuad>& quads = runtime.GetDrawBatch().quads;
    ASSERT_EQ(quads.size(), 6U); // the box, four placeholder glyphs, the caret
    for (std::size_t i = 1U; i < 5U; ++i) {
        EXPECT_EQ(quads[i].kind, UIDrawItemKind::Glyph);
        EXPECT_EQ(quads[i].color, FieldOf(field).placeholderColor);
    }
    EXPECT_FLOAT_EQ(quads[5].rect.position.x, 16.0F);
    EXPECT_EQ(quads[5].rect.size, (Math::Vector2{2.0F, 16.0F}));

    Frame({50.0F, 20.0F}, false, 0.6F);
    EXPECT_EQ(CaretQuadCount(), 0U) << "the blink clock is past its lit half";
    Frame({50.0F, 20.0F}, false, 0.5F);
    EXPECT_EQ(CaretQuadCount(), 1U) << "the next second starts lit";

    // Typing restarts the clock: the caret is lit right after the edit.
    Frame({50.0F, 20.0F}, false, 0.6F);
    EXPECT_EQ(CaretQuadCount(), 0U);
    Key(Input::KeyCode::A, true);
    EXPECT_EQ(CaretQuadCount(), 1U);
    const float expectedX =
        16.0F + runtime.GetFontAtlas().MeasureTextWidth("A", 16.0F);
    for (const UIQuad& quad : runtime.GetDrawBatch().quads) {
        if (quad.kind == UIDrawItemKind::SolidColor && quad.rect.size.x == 2.0F) {
            EXPECT_FLOAT_EQ(quad.rect.position.x, expectedX);
        }
    }
}

TEST_F(UITextInputTest, Layout_TextInputStacksAndAnchorsLikeButtons) {
    const Scene::Entity container = entityManager.CreateEntity();
    Scene::UILayoutContainerComponent layout;
    layout.rect = Math::Rect{{0.0F, 0.0F}, {300.0F, 300.0F}};
    layout.padding = 10.0F;
    entityManager.AddComponent<Scene::UILayoutContainerComponent>(container, layout);
    const Scene::Entity stacked = MakeField(Math::Rect{{0.0F, 0.0F}, {160.0F, 28.0F}});
    Scene::HierarchyComponent link;
    link.parent = container;
    link.siblingOrder = 0;
    entityManager.AddComponent<Scene::HierarchyComponent>(stacked, link);
    const Scene::Entity stretched = MakeField(Math::Rect{{0.0F, 0.0F}, {160.0F, 28.0F}});
    entityManager.AddComponent<Scene::UIAnchorComponent>(stretched, Scene::UIAnchorComponent{});
    runtime.SetViewportSize({400.0F, 300.0F});

    Frame({350.0F, 250.0F}, false);

    EXPECT_EQ(FieldOf(stacked).rect.position, (Math::Vector2{10.0F, 10.0F}));
    EXPECT_EQ(FieldOf(stretched).rect, (Math::Rect{{0.0F, 0.0F}, {400.0F, 300.0F}}));
}

TEST_F(UITextInputTest, SaveThenLoad_TextInput_RoundTrip) {
    const Scene::Entity field = entityManager.CreateEntity();
    Scene::UITextInputComponent authored;
    authored.rect = Math::Rect{{10.0F, 20.0F}, {160.0F, 28.0F}};
    authored.text = "hello";
    authored.maxLength = 64;
    authored.placeholder = "Name";
    authored.fontSize = 20.0F;
    authored.textColor = Math::Vector3{1.0F, 0.0F, 0.0F};
    authored.focused = true;
    authored.caretIndex = 5;
    authored.wasSubmittedThisFrame = true;
    authored.blinkTime = 3.0F;
    authored.scrollOffset = 10.0F;
    entityManager.AddComponent<Scene::UITextInputComponent>(field, authored);

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_text_input.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.Save(entityManager, {field}, path, Scene::SceneAssetType::Scene));

    Scene::EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    const std::vector<Scene::Entity> loaded = serializer.Load(loadedManager, path);
    ASSERT_EQ(loaded.size(), 1U);
    const Scene::UITextInputComponent& restored =
        loadedManager.GetComponent<Scene::UITextInputComponent>(loaded[0]);
    EXPECT_EQ(restored.text, "hello");
    EXPECT_EQ(restored.maxLength, 64);
    EXPECT_EQ(restored.placeholder, "Name");
    EXPECT_FLOAT_EQ(restored.fontSize, 20.0F);
    EXPECT_EQ(restored.textColor, (Math::Vector3{1.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(restored.focused);
    EXPECT_EQ(restored.caretIndex, 0);
    EXPECT_FALSE(restored.wasSubmittedThisFrame);
    EXPECT_FLOAT_EQ(restored.blinkTime, 0.0F);
    EXPECT_FLOAT_EQ(restored.scrollOffset, 0.0F);

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::UI::Tests
