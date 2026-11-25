// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_runtime.h"

#include <algorithm>

#include <gtest/gtest.h>

#include "event_system.h"
#include "input_system.h"
#include "memory_manager.h"
#include "ui_button_component.h"
#include "ui_image_component.h"
#include "ui_text_component.h"
#include "entity_manager.h"
#include "localization.h"

namespace UVE::UI::Tests {
namespace {

class UIRuntimeTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Input::InputSystem inputSystem{eventSystem};
    UIRuntime runtime;
};

[[nodiscard]] std::size_t CountGlyphQuads(const UIDrawBatch& batch) {
    return static_cast<std::size_t>(std::count_if(batch.quads.cbegin(), batch.quads.cend(), [](const UIQuad& quad) {
        return quad.kind == UIDrawItemKind::Glyph;
    }));
}

[[nodiscard]] bool AreQuadsIdentical(const UIDrawBatch& left, const UIDrawBatch& right) {
    if (left.quads.size() != right.quads.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.quads.size(); ++index) {
        const UIQuad& a = left.quads[index];
        const UIQuad& b = right.quads[index];
        if (a.rect != b.rect || a.u0 != b.u0 ||
            a.v0 != b.v0 || a.u1 != b.u1 || a.v1 != b.v1 || a.color != b.color || a.alpha != b.alpha ||
            a.kind != b.kind || a.imageAssetGuid != b.imageAssetGuid) {
            return false;
        }
    }
    return true;
}

TEST_F(UIRuntimeTest, Tick_LocalizingWithNoTableDrawsExactlyWhatWasAuthored) {
    // The safety property that lets localization be on by default: a project nobody has
    // translated must render identically, quad for quad, whether or not a service is attached.
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::UITextComponent text{};
    text.text = "Play";
    entityManager.AddComponent<Scene::UITextComponent>(entity, text);
    inputSystem.Update();

    runtime.Tick(entityManager, inputSystem);
    const UIDrawBatch unlocalized = runtime.GetDrawBatch();

    const Localization::LocalizationService emptyService;
    runtime.Tick(entityManager, inputSystem, UITextLocalization{&emptyService, {}});
    EXPECT_TRUE(AreQuadsIdentical(unlocalized, runtime.GetDrawBatch()));
    EXPECT_EQ(CountGlyphQuads(runtime.GetDrawBatch()), 4U);
}

TEST_F(UIRuntimeTest, Tick_TranslatesOnlyTheTextWhoseModeSaysTo) {
    const Scene::Entity translatedLabel = entityManager.CreateEntity();
    const Scene::Entity optedOutLabel = entityManager.CreateEntity();
    Scene::UITextComponent text{};
    text.text = "Play";
    entityManager.AddComponent<Scene::UITextComponent>(translatedLabel, text);
    entityManager.AddComponent<Scene::UITextComponent>(optedOutLabel, text);
    inputSystem.Update();

    Localization::LocalizationService service;
    Localization::StringTable filipino{Localization::Locale{"fil", ""}};
    ASSERT_TRUE(filipino.SetTranslation("Play", "Maglaro"));
    ASSERT_TRUE(service.AddStringTable(std::move(filipino)));
    service.SetActiveLocale(Localization::Locale{"fil", ""});

    runtime.Tick(entityManager, inputSystem,
                    UITextLocalization{&service, [translatedLabel](const Scene::Entity entity) {
                                              return entity == translatedLabel;
                                          }});

    // "Maglaro" is seven glyphs and the opted-out "Play" stays four. Counting glyphs is enough to
    // tell which string each entity drew without depending on atlas metrics.
    EXPECT_EQ(CountGlyphQuads(runtime.GetDrawBatch()), 7U + 4U);
}

TEST_F(UIRuntimeTest, Tick_BuildsOneSolidQuadPerImageEntity) {
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::UIImageComponent image{};
    image.rect.position = Math::Vector2{10.0F, 20.0F};
    image.rect.size = Math::Vector2{64.0F, 48.0F};
    image.tintColor = Math::Vector3{0.2F, 0.4F, 0.6F};
    image.alpha = 0.75F;
    entityManager.AddComponent<Scene::UIImageComponent>(entity, image);

    inputSystem.Update();
    runtime.Tick(entityManager, inputSystem);

    const UIDrawBatch& batch = runtime.GetDrawBatch();
    ASSERT_EQ(batch.quads.size(), 1U);
    EXPECT_EQ(batch.quads[0].kind, UIDrawItemKind::SolidColor);
    EXPECT_FLOAT_EQ(batch.quads[0].rect.position.x, 10.0F);
    EXPECT_FLOAT_EQ(batch.quads[0].rect.size.y, 48.0F);
    EXPECT_FLOAT_EQ(batch.quads[0].alpha, 0.75F);
}

TEST_F(UIRuntimeTest, Tick_ClassifiesImageWithRealAssetGuidAsImageKind) {
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::UIImageComponent image{};
    image.textureAssetGuid = Asset::AssetGuid{123U};
    entityManager.AddComponent<Scene::UIImageComponent>(entity, image);

    inputSystem.Update();
    runtime.Tick(entityManager, inputSystem);

    ASSERT_EQ(runtime.GetDrawBatch().quads.size(), 1U);
    EXPECT_EQ(runtime.GetDrawBatch().quads[0].kind, UIDrawItemKind::Image);
    EXPECT_EQ(runtime.GetDrawBatch().quads[0].imageAssetGuid.value, 123U);
}

TEST_F(UIRuntimeTest, Tick_BuildsGlyphQuadsForTextEntity) {
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::UITextComponent text{};
    text.text = "Hi";
    text.positionPixels = Math::Vector2{5.0F, 5.0F};
    text.fontSize = 24.0F;
    entityManager.AddComponent<Scene::UITextComponent>(entity, text);

    inputSystem.Update();
    runtime.Tick(entityManager, inputSystem);

    const UIDrawBatch& batch = runtime.GetDrawBatch();
    ASSERT_EQ(batch.quads.size(), 2U);
    EXPECT_EQ(batch.quads[0].kind, UIDrawItemKind::Glyph);
    EXPECT_EQ(batch.quads[1].kind, UIDrawItemKind::Glyph);
}

TEST_F(UIRuntimeTest, Tick_ButtonHitTest_ClickInsideRectSetsHoveredAndClicked) {
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::UIButtonComponent button{};
    button.rect.position = Math::Vector2{100.0F, 100.0F};
    button.rect.size = Math::Vector2{120.0F, 32.0F};
    entityManager.AddComponent<Scene::UIButtonComponent>(entity, button);

    inputSystem.SetMousePosition(Math::Vector2{150.0F, 110.0F});
    inputSystem.SetMouseButtonState(Input::MouseButton::Left, true);
    inputSystem.Update();

    runtime.Tick(entityManager, inputSystem);

    const Scene::UIButtonComponent& updated = entityManager.GetComponent<Scene::UIButtonComponent>(entity);
    EXPECT_TRUE(updated.isHovered);
    EXPECT_TRUE(updated.wasClickedThisFrame);

    ASSERT_EQ(runtime.GetDrawBatch().quads.size(), 1U);
    EXPECT_EQ(runtime.GetDrawBatch().quads[0].color, button.pressedColor);
}

TEST_F(UIRuntimeTest, Tick_ButtonHitTest_ClickOutsideRectLeavesUnhoveredAndUnclicked) {
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::UIButtonComponent button{};
    button.rect.position = Math::Vector2{100.0F, 100.0F};
    button.rect.size = Math::Vector2{120.0F, 32.0F};
    entityManager.AddComponent<Scene::UIButtonComponent>(entity, button);

    inputSystem.SetMousePosition(Math::Vector2{0.0F, 0.0F});
    inputSystem.SetMouseButtonState(Input::MouseButton::Left, true);
    inputSystem.Update();

    runtime.Tick(entityManager, inputSystem);

    const Scene::UIButtonComponent& updated = entityManager.GetComponent<Scene::UIButtonComponent>(entity);
    EXPECT_FALSE(updated.isHovered);
    EXPECT_FALSE(updated.wasClickedThisFrame);
    EXPECT_EQ(runtime.GetDrawBatch().quads[0].color, button.normalColor);
}

TEST_F(UIRuntimeTest, Tick_ButtonHitTest_HoverWithoutClickUsesHoverColorNotClicked) {
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::UIButtonComponent button{};
    button.rect.position = Math::Vector2{0.0F, 0.0F};
    button.rect.size = Math::Vector2{50.0F, 50.0F};
    entityManager.AddComponent<Scene::UIButtonComponent>(entity, button);

    inputSystem.SetMousePosition(Math::Vector2{10.0F, 10.0F});
    inputSystem.Update();

    runtime.Tick(entityManager, inputSystem);

    const Scene::UIButtonComponent& updated = entityManager.GetComponent<Scene::UIButtonComponent>(entity);
    EXPECT_TRUE(updated.isHovered);
    EXPECT_FALSE(updated.wasClickedThisFrame);
    EXPECT_EQ(runtime.GetDrawBatch().quads[0].color, button.hoverColor);
}

TEST_F(UIRuntimeTest, CoordinateTransform_ScalesDrawQuadsAndMapsPointerForHitTesting) {
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::UIButtonComponent button{};
    button.rect.position = Math::Vector2{100.0F, 100.0F};
    button.rect.size = Math::Vector2{120.0F, 32.0F};
    entityManager.AddComponent<Scene::UIButtonComponent>(entity, button);
    runtime.SetCoordinateTransform(UICoordinateTransform{2.0F, 2.0F, 10.0F, 20.0F,
                                                                2.0F, 2.0F, 100.0F, 200.0F});

    inputSystem.SetMousePosition(Math::Vector2{205.0F, 220.0F});
    inputSystem.SetMouseButtonState(Input::MouseButton::Left, true);
    inputSystem.Update();
    runtime.Tick(entityManager, inputSystem);

    const Scene::UIButtonComponent& updated = entityManager.GetComponent<Scene::UIButtonComponent>(entity);
    EXPECT_TRUE(updated.isHovered);
    EXPECT_TRUE(updated.wasClickedThisFrame);
    ASSERT_EQ(runtime.GetDrawBatch().quads.size(), 1U);
    EXPECT_FLOAT_EQ(runtime.GetDrawBatch().quads[0].rect.position.x, 210.0F);
    EXPECT_FLOAT_EQ(runtime.GetDrawBatch().quads[0].rect.position.y, 220.0F);
    EXPECT_FLOAT_EQ(runtime.GetDrawBatch().quads[0].rect.size.x, 240.0F);
    EXPECT_FLOAT_EQ(runtime.GetDrawBatch().quads[0].rect.size.y, 64.0F);
}

TEST_F(UIRuntimeTest, InvalidCoordinateTransformFallsBackToIdentity) {
    runtime.SetCoordinateTransform(UICoordinateTransform{0.0F, 1.0F, 4.0F, 5.0F,
                                                                1.0F, 1.0F, 0.0F, 0.0F});
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::UIImageComponent image{};
    image.rect.position = Math::Vector2{10.0F, 20.0F};
    image.rect.size = Math::Vector2{30.0F, 40.0F};
    entityManager.AddComponent<Scene::UIImageComponent>(entity, image);

    inputSystem.Update();
    runtime.Tick(entityManager, inputSystem);

    ASSERT_EQ(runtime.GetDrawBatch().quads.size(), 1U);
    EXPECT_FLOAT_EQ(runtime.GetDrawBatch().quads[0].rect.position.x, 10.0F);
    EXPECT_FLOAT_EQ(runtime.GetDrawBatch().quads[0].rect.position.y, 20.0F);
    EXPECT_FLOAT_EQ(runtime.GetDrawBatch().quads[0].rect.size.x, 30.0F);
    EXPECT_FLOAT_EQ(runtime.GetDrawBatch().quads[0].rect.size.y, 40.0F);
}

TEST_F(UIRuntimeTest, Tick_ClearsPreviousBatchEachCall) {
    const Scene::Entity entity = entityManager.CreateEntity();
    entityManager.AddComponent<Scene::UIImageComponent>(entity, Scene::UIImageComponent{});

    inputSystem.Update();
    runtime.Tick(entityManager, inputSystem);
    ASSERT_EQ(runtime.GetDrawBatch().quads.size(), 1U);

    entityManager.DestroyEntity(entity);
    runtime.Tick(entityManager, inputSystem);
    EXPECT_TRUE(runtime.GetDrawBatch().quads.empty());
}

} // namespace
} // namespace UVE::UI::Tests
