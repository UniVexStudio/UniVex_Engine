// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/ui_anchor_component_uve.h"
#include "uve/component/ui_layout_container_component_uve.h"
#include "uve/component/ui_text_input_component_uve.h"
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

class UITextInputUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Input::InputSystemUVE inputSystem{eventSystem};
    UIRuntimeUVE runtime;
    Scene::SceneSerializerUVE serializer;

    void FrameUVE(const Math::Vector2UVE mousePosition, const bool mouseDown, const float dt = 0.016F) {
        inputSystem.SetMousePositionUVE(mousePosition);
        inputSystem.SetMouseButtonStateUVE(Input::MouseButtonUVE::Left, mouseDown);
        inputSystem.UpdateUVE();
        runtime.SetDeltaTimeUVE(dt);
        runtime.TickUVE(entityManager, inputSystem);
    }

    void KeyUVE(const Input::KeyCodeUVE key, const bool shift = false) {
        inputSystem.SetKeyStateUVE(Input::KeyCodeUVE::LeftShift, shift);
        inputSystem.SetKeyStateUVE(key, true);
        inputSystem.UpdateUVE();
        runtime.SetDeltaTimeUVE(0.016F);
        runtime.TickUVE(entityManager, inputSystem);
        inputSystem.SetKeyStateUVE(key, false);
        inputSystem.SetKeyStateUVE(Input::KeyCodeUVE::LeftShift, false);
        // Commit the release: without this Update, pressing the same key twice in a row would
        // read as still held and the second press would never edge.
        inputSystem.UpdateUVE();
    }

    Scene::EntityUVE MakeFieldUVE(const Math::RectUVE rect) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UITextInputComponentUVE field;
        field.rect = rect;
        entityManager.AddComponentUVE<Scene::UITextInputComponentUVE>(entity, field);
        return entity;
    }

    const Scene::UITextInputComponentUVE& FieldOfUVE(const Scene::EntityUVE entity) {
        return entityManager.GetComponentUVE<Scene::UITextInputComponentUVE>(entity);
    }

    std::size_t CaretQuadCountUVE() {
        std::size_t carets = 0U;
        for (const UIQuadUVE& quad : runtime.GetDrawBatchUVE().quads) {
            if (quad.kind == UIDrawItemKindUVE::SolidColor && quad.rect.size.x == 2.0F &&
                quad.rect.size.y == 16.0F) {
                ++carets;
            }
        }
        return carets;
    }
};

TEST_F(UITextInputUVETest, ValidatorAcceptsDefaultsRejectsBadConfig) {
    Scene::UITextInputComponentUVE field;
    EXPECT_TRUE(IsUITextInputComponentValidUVE(field));
    field.maxLength = 0;
    EXPECT_FALSE(IsUITextInputComponentValidUVE(field));
    field.maxLength = 32;
    field.text = std::string(513U, 'x');
    EXPECT_FALSE(IsUITextInputComponentValidUVE(field));
    field.text.clear();
    field.caretIndex = -1;
    EXPECT_FALSE(IsUITextInputComponentValidUVE(field));
}

TEST_F(UITextInputUVETest, Click_FocusesClickElsewhereBlursAndSwitches) {
    const Scene::EntityUVE first = MakeFieldUVE(Math::RectUVE{{10.0F, 10.0F}, {160.0F, 28.0F}});
    const Scene::EntityUVE second = MakeFieldUVE(Math::RectUVE{{10.0F, 100.0F}, {160.0F, 28.0F}});
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({50.0F, 20.0F}, true);
    FrameUVE({50.0F, 20.0F}, false);
    EXPECT_TRUE(FieldOfUVE(first).focused);
    EXPECT_FALSE(FieldOfUVE(second).focused);

    FrameUVE({50.0F, 110.0F}, true);
    FrameUVE({50.0F, 110.0F}, false);
    EXPECT_FALSE(FieldOfUVE(first).focused);
    EXPECT_TRUE(FieldOfUVE(second).focused);

    FrameUVE({700.0F, 500.0F}, true);
    FrameUVE({700.0F, 500.0F}, false);
    EXPECT_FALSE(FieldOfUVE(first).focused);
    EXPECT_FALSE(FieldOfUVE(second).focused);
}

TEST_F(UITextInputUVETest, Typing_InsertsAtCaretWithShiftAndDigits) {
    const Scene::EntityUVE field = MakeFieldUVE(Math::RectUVE{{10.0F, 10.0F}, {160.0F, 28.0F}});
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({50.0F, 20.0F}, true);
    FrameUVE({50.0F, 20.0F}, false);
    KeyUVE(Input::KeyCodeUVE::A);
    KeyUVE(Input::KeyCodeUVE::B, true);
    KeyUVE(Input::KeyCodeUVE::Space);
    KeyUVE(Input::KeyCodeUVE::Num1);
    KeyUVE(Input::KeyCodeUVE::Num1, true);

    EXPECT_EQ(FieldOfUVE(field).text, "aB 1!");
    EXPECT_EQ(FieldOfUVE(field).caretIndex, 5);
}

TEST_F(UITextInputUVETest, Backspace_DeletesBeforeCaret) {
    const Scene::EntityUVE field = MakeFieldUVE(Math::RectUVE{{10.0F, 10.0F}, {160.0F, 28.0F}});
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({50.0F, 20.0F}, true);
    FrameUVE({50.0F, 20.0F}, false);
    KeyUVE(Input::KeyCodeUVE::A);
    KeyUVE(Input::KeyCodeUVE::B);
    KeyUVE(Input::KeyCodeUVE::C);
    KeyUVE(Input::KeyCodeUVE::Backspace);
    EXPECT_EQ(FieldOfUVE(field).text, "ab");
    EXPECT_EQ(FieldOfUVE(field).caretIndex, 2);

    KeyUVE(Input::KeyCodeUVE::Left);
    KeyUVE(Input::KeyCodeUVE::Left);
    KeyUVE(Input::KeyCodeUVE::Backspace);
    EXPECT_EQ(FieldOfUVE(field).text, "ab");
    EXPECT_EQ(FieldOfUVE(field).caretIndex, 0);
}

TEST_F(UITextInputUVETest, Arrows_MoveCaretAndInsertGoesMidString) {
    const Scene::EntityUVE field = MakeFieldUVE(Math::RectUVE{{10.0F, 10.0F}, {160.0F, 28.0F}});
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({50.0F, 20.0F}, true);
    FrameUVE({50.0F, 20.0F}, false);
    KeyUVE(Input::KeyCodeUVE::A);
    KeyUVE(Input::KeyCodeUVE::C);
    KeyUVE(Input::KeyCodeUVE::Left);
    EXPECT_EQ(FieldOfUVE(field).caretIndex, 1);
    KeyUVE(Input::KeyCodeUVE::B);
    EXPECT_EQ(FieldOfUVE(field).text, "abc");
    EXPECT_EQ(FieldOfUVE(field).caretIndex, 2);

    KeyUVE(Input::KeyCodeUVE::Right);
    KeyUVE(Input::KeyCodeUVE::Right);
    EXPECT_EQ(FieldOfUVE(field).caretIndex, 3);
}

TEST_F(UITextInputUVETest, Enter_SubmitsWithOneShot_Escape_Blurs) {
    const Scene::EntityUVE field = MakeFieldUVE(Math::RectUVE{{10.0F, 10.0F}, {160.0F, 28.0F}});
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({50.0F, 20.0F}, true);
    FrameUVE({50.0F, 20.0F}, false);
    KeyUVE(Input::KeyCodeUVE::G);
    KeyUVE(Input::KeyCodeUVE::O);
    KeyUVE(Input::KeyCodeUVE::Enter);
    EXPECT_TRUE(FieldOfUVE(field).wasSubmittedThisFrame);
    EXPECT_TRUE(FieldOfUVE(field).focused) << "submit keeps focus";

    FrameUVE({50.0F, 20.0F}, false);
    EXPECT_FALSE(FieldOfUVE(field).wasSubmittedThisFrame);

    KeyUVE(Input::KeyCodeUVE::Escape);
    EXPECT_FALSE(FieldOfUVE(field).focused);
    EXPECT_FALSE(FieldOfUVE(field).wasSubmittedThisFrame);
}

TEST_F(UITextInputUVETest, MaxLength_BlocksFurtherInput) {
    const Scene::EntityUVE field = MakeFieldUVE(Math::RectUVE{{10.0F, 10.0F}, {160.0F, 28.0F}});
    entityManager.GetComponentUVE<Scene::UITextInputComponentUVE>(field).maxLength = 2;
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({50.0F, 20.0F}, true);
    FrameUVE({50.0F, 20.0F}, false);
    KeyUVE(Input::KeyCodeUVE::A);
    KeyUVE(Input::KeyCodeUVE::B);
    KeyUVE(Input::KeyCodeUVE::C);

    EXPECT_EQ(FieldOfUVE(field).text, "ab");
    EXPECT_EQ(FieldOfUVE(field).caretIndex, 2);
}

TEST_F(UITextInputUVETest, Placeholder_CaretAndBlink) {
    const Scene::EntityUVE field = MakeFieldUVE(Math::RectUVE{{10.0F, 10.0F}, {160.0F, 28.0F}});
    Scene::UITextInputComponentUVE& authored =
        entityManager.GetComponentUVE<Scene::UITextInputComponentUVE>(field);
    authored.placeholder = "Name";
    runtime.SetViewportSizeUVE({800.0F, 600.0F});

    FrameUVE({50.0F, 20.0F}, true);
    FrameUVE({50.0F, 20.0F}, false);

    const std::vector<UIQuadUVE>& quads = runtime.GetDrawBatchUVE().quads;
    ASSERT_EQ(quads.size(), 6U); // the box, four placeholder glyphs, the caret
    for (std::size_t i = 1U; i < 5U; ++i) {
        EXPECT_EQ(quads[i].kind, UIDrawItemKindUVE::Glyph);
        EXPECT_EQ(quads[i].color, FieldOfUVE(field).placeholderColor);
    }
    EXPECT_FLOAT_EQ(quads[5].rect.position.x, 16.0F);
    EXPECT_EQ(quads[5].rect.size, (Math::Vector2UVE{2.0F, 16.0F}));

    FrameUVE({50.0F, 20.0F}, false, 0.6F);
    EXPECT_EQ(CaretQuadCountUVE(), 0U) << "the blink clock is past its lit half";
    FrameUVE({50.0F, 20.0F}, false, 0.5F);
    EXPECT_EQ(CaretQuadCountUVE(), 1U) << "the next second starts lit";

    // Typing restarts the clock: the caret is lit right after the edit.
    FrameUVE({50.0F, 20.0F}, false, 0.6F);
    EXPECT_EQ(CaretQuadCountUVE(), 0U);
    KeyUVE(Input::KeyCodeUVE::A, true);
    EXPECT_EQ(CaretQuadCountUVE(), 1U);
    const float expectedX =
        16.0F + runtime.GetFontAtlasUVE().MeasureTextWidthUVE("A", 16.0F);
    for (const UIQuadUVE& quad : runtime.GetDrawBatchUVE().quads) {
        if (quad.kind == UIDrawItemKindUVE::SolidColor && quad.rect.size.x == 2.0F) {
            EXPECT_FLOAT_EQ(quad.rect.position.x, expectedX);
        }
    }
}

TEST_F(UITextInputUVETest, Layout_TextInputStacksAndAnchorsLikeButtons) {
    const Scene::EntityUVE container = entityManager.CreateEntityUVE();
    Scene::UILayoutContainerComponentUVE layout;
    layout.rect = Math::RectUVE{{0.0F, 0.0F}, {300.0F, 300.0F}};
    layout.padding = 10.0F;
    entityManager.AddComponentUVE<Scene::UILayoutContainerComponentUVE>(container, layout);
    const Scene::EntityUVE stacked = MakeFieldUVE(Math::RectUVE{{0.0F, 0.0F}, {160.0F, 28.0F}});
    Scene::HierarchyComponentUVE link;
    link.parent = container;
    link.siblingOrder = 0;
    entityManager.AddComponentUVE<Scene::HierarchyComponentUVE>(stacked, link);
    const Scene::EntityUVE stretched = MakeFieldUVE(Math::RectUVE{{0.0F, 0.0F}, {160.0F, 28.0F}});
    entityManager.AddComponentUVE<Scene::UIAnchorComponentUVE>(stretched, Scene::UIAnchorComponentUVE{});
    runtime.SetViewportSizeUVE({400.0F, 300.0F});

    FrameUVE({350.0F, 250.0F}, false);

    EXPECT_EQ(FieldOfUVE(stacked).rect.position, (Math::Vector2UVE{10.0F, 10.0F}));
    EXPECT_EQ(FieldOfUVE(stretched).rect, (Math::RectUVE{{0.0F, 0.0F}, {400.0F, 300.0F}}));
}

TEST_F(UITextInputUVETest, SaveThenLoad_TextInput_RoundTrip) {
    const Scene::EntityUVE field = entityManager.CreateEntityUVE();
    Scene::UITextInputComponentUVE authored;
    authored.rect = Math::RectUVE{{10.0F, 20.0F}, {160.0F, 28.0F}};
    authored.text = "hello";
    authored.maxLength = 64;
    authored.placeholder = "Name";
    authored.fontSize = 20.0F;
    authored.textColor = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    authored.focused = true;
    authored.caretIndex = 5;
    authored.wasSubmittedThisFrame = true;
    authored.blinkTime = 3.0F;
    authored.scrollOffset = 10.0F;
    entityManager.AddComponentUVE<Scene::UITextInputComponentUVE>(field, authored);

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_text_input.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {field}, path, Scene::SceneAssetTypeUVE::Scene));

    Scene::EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<Scene::EntityUVE> loaded = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(loaded.size(), 1U);
    const Scene::UITextInputComponentUVE& restored =
        loadedManager.GetComponentUVE<Scene::UITextInputComponentUVE>(loaded[0]);
    EXPECT_EQ(restored.text, "hello");
    EXPECT_EQ(restored.maxLength, 64);
    EXPECT_EQ(restored.placeholder, "Name");
    EXPECT_FLOAT_EQ(restored.fontSize, 20.0F);
    EXPECT_EQ(restored.textColor, (Math::Vector3UVE{1.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(restored.focused);
    EXPECT_EQ(restored.caretIndex, 0);
    EXPECT_FALSE(restored.wasSubmittedThisFrame);
    EXPECT_FLOAT_EQ(restored.blinkTime, 0.0F);
    EXPECT_FLOAT_EQ(restored.scrollOffset, 0.0F);

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::UI::Tests
