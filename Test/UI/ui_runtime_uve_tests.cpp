// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/ui_runtime_uve.h"

#include <algorithm>

#include <gtest/gtest.h>

#include "uve/events/event_system_uve.h"
#include "uve/input/input_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/localization/localization_uve.h"

namespace UVE::UI::Tests {
namespace {

class UIRuntimeUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Input::InputSystemUVE inputSystem{eventSystem};
    UIRuntimeUVE runtime;
};

[[nodiscard]] std::size_t CountGlyphQuadsUVE(const UIDrawBatchUVE& batch) {
    return static_cast<std::size_t>(std::count_if(batch.quads.cbegin(), batch.quads.cend(), [](const UIQuadUVE& quad) {
        return quad.kind == UIDrawItemKindUVE::Glyph;
    }));
}

[[nodiscard]] bool AreQuadsIdenticalUVE(const UIDrawBatchUVE& left, const UIDrawBatchUVE& right) {
    if (left.quads.size() != right.quads.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.quads.size(); ++index) {
        const UIQuadUVE& a = left.quads[index];
        const UIQuadUVE& b = right.quads[index];
        if (a.positionPixels != b.positionPixels || a.sizePixels != b.sizePixels || a.u0 != b.u0 ||
            a.v0 != b.v0 || a.u1 != b.u1 || a.v1 != b.v1 || a.color != b.color || a.alpha != b.alpha ||
            a.kind != b.kind || a.imageAssetGuid != b.imageAssetGuid) {
            return false;
        }
    }
    return true;
}

TEST_F(UIRuntimeUVETest, TickUVE_LocalizingWithNoTableDrawsExactlyWhatWasAuthored) {
    // The safety property that lets localization be on by default: a project nobody has
    // translated must render identically, quad for quad, whether or not a service is attached.
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::UITextComponentUVE text{};
    text.text = "Play";
    entityManager.AddComponentUVE<Scene::UITextComponentUVE>(entity, text);
    inputSystem.UpdateUVE();

    runtime.TickUVE(entityManager, inputSystem);
    const UIDrawBatchUVE unlocalized = runtime.GetDrawBatchUVE();

    const Localization::LocalizationServiceUVE emptyService;
    runtime.TickUVE(entityManager, inputSystem, UITextLocalizationUVE{&emptyService, {}});
    EXPECT_TRUE(AreQuadsIdenticalUVE(unlocalized, runtime.GetDrawBatchUVE()));
    EXPECT_EQ(CountGlyphQuadsUVE(runtime.GetDrawBatchUVE()), 4U);
}

TEST_F(UIRuntimeUVETest, TickUVE_TranslatesOnlyTheTextWhoseModeSaysTo) {
    const Scene::EntityUVE translatedLabel = entityManager.CreateEntityUVE();
    const Scene::EntityUVE optedOutLabel = entityManager.CreateEntityUVE();
    Scene::UITextComponentUVE text{};
    text.text = "Play";
    entityManager.AddComponentUVE<Scene::UITextComponentUVE>(translatedLabel, text);
    entityManager.AddComponentUVE<Scene::UITextComponentUVE>(optedOutLabel, text);
    inputSystem.UpdateUVE();

    Localization::LocalizationServiceUVE service;
    Localization::StringTableUVE filipino{Localization::LocaleUVE{"fil", ""}};
    ASSERT_TRUE(filipino.SetTranslationUVE("Play", "Maglaro"));
    ASSERT_TRUE(service.AddStringTableUVE(std::move(filipino)));
    service.SetActiveLocaleUVE(Localization::LocaleUVE{"fil", ""});

    runtime.TickUVE(entityManager, inputSystem,
                    UITextLocalizationUVE{&service, [translatedLabel](const Scene::EntityUVE entity) {
                                              return entity == translatedLabel;
                                          }});

    // "Maglaro" is seven glyphs and the opted-out "Play" stays four. Counting glyphs is enough to
    // tell which string each entity drew without depending on atlas metrics.
    EXPECT_EQ(CountGlyphQuadsUVE(runtime.GetDrawBatchUVE()), 7U + 4U);
}

TEST_F(UIRuntimeUVETest, TickUVE_BuildsOneSolidQuadPerImageEntity) {
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::UIImageComponentUVE image{};
    image.positionPixels = Math::Vector2UVE{10.0F, 20.0F};
    image.sizePixels = Math::Vector2UVE{64.0F, 48.0F};
    image.tintColor = Math::Vector3UVE{0.2F, 0.4F, 0.6F};
    image.alpha = 0.75F;
    entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(entity, image);

    inputSystem.UpdateUVE();
    runtime.TickUVE(entityManager, inputSystem);

    const UIDrawBatchUVE& batch = runtime.GetDrawBatchUVE();
    ASSERT_EQ(batch.quads.size(), 1U);
    EXPECT_EQ(batch.quads[0].kind, UIDrawItemKindUVE::SolidColor);
    EXPECT_FLOAT_EQ(batch.quads[0].positionPixels.x, 10.0F);
    EXPECT_FLOAT_EQ(batch.quads[0].sizePixels.y, 48.0F);
    EXPECT_FLOAT_EQ(batch.quads[0].alpha, 0.75F);
}

TEST_F(UIRuntimeUVETest, TickUVE_ClassifiesImageWithRealAssetGuidAsImageKind) {
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::UIImageComponentUVE image{};
    image.textureAssetGuid = Asset::AssetGuidUVE{123U};
    entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(entity, image);

    inputSystem.UpdateUVE();
    runtime.TickUVE(entityManager, inputSystem);

    ASSERT_EQ(runtime.GetDrawBatchUVE().quads.size(), 1U);
    EXPECT_EQ(runtime.GetDrawBatchUVE().quads[0].kind, UIDrawItemKindUVE::Image);
    EXPECT_EQ(runtime.GetDrawBatchUVE().quads[0].imageAssetGuid.value, 123U);
}

TEST_F(UIRuntimeUVETest, TickUVE_BuildsGlyphQuadsForTextEntity) {
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::UITextComponentUVE text{};
    text.text = "Hi";
    text.positionPixels = Math::Vector2UVE{5.0F, 5.0F};
    text.fontSize = 24.0F;
    entityManager.AddComponentUVE<Scene::UITextComponentUVE>(entity, text);

    inputSystem.UpdateUVE();
    runtime.TickUVE(entityManager, inputSystem);

    const UIDrawBatchUVE& batch = runtime.GetDrawBatchUVE();
    ASSERT_EQ(batch.quads.size(), 2U);
    EXPECT_EQ(batch.quads[0].kind, UIDrawItemKindUVE::Glyph);
    EXPECT_EQ(batch.quads[1].kind, UIDrawItemKindUVE::Glyph);
}

TEST_F(UIRuntimeUVETest, TickUVE_ButtonHitTest_ClickInsideRectSetsHoveredAndClicked) {
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::UIButtonComponentUVE button{};
    button.positionPixels = Math::Vector2UVE{100.0F, 100.0F};
    button.sizePixels = Math::Vector2UVE{120.0F, 32.0F};
    entityManager.AddComponentUVE<Scene::UIButtonComponentUVE>(entity, button);

    inputSystem.SetMousePositionUVE(Math::Vector2UVE{150.0F, 110.0F});
    inputSystem.SetMouseButtonStateUVE(Input::MouseButtonUVE::Left, true);
    inputSystem.UpdateUVE();

    runtime.TickUVE(entityManager, inputSystem);

    const Scene::UIButtonComponentUVE& updated = entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(entity);
    EXPECT_TRUE(updated.isHovered);
    EXPECT_TRUE(updated.wasClickedThisFrame);

    ASSERT_EQ(runtime.GetDrawBatchUVE().quads.size(), 1U);
    EXPECT_EQ(runtime.GetDrawBatchUVE().quads[0].color, button.pressedColor);
}

TEST_F(UIRuntimeUVETest, TickUVE_ButtonHitTest_ClickOutsideRectLeavesUnhoveredAndUnclicked) {
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::UIButtonComponentUVE button{};
    button.positionPixels = Math::Vector2UVE{100.0F, 100.0F};
    button.sizePixels = Math::Vector2UVE{120.0F, 32.0F};
    entityManager.AddComponentUVE<Scene::UIButtonComponentUVE>(entity, button);

    inputSystem.SetMousePositionUVE(Math::Vector2UVE{0.0F, 0.0F});
    inputSystem.SetMouseButtonStateUVE(Input::MouseButtonUVE::Left, true);
    inputSystem.UpdateUVE();

    runtime.TickUVE(entityManager, inputSystem);

    const Scene::UIButtonComponentUVE& updated = entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(entity);
    EXPECT_FALSE(updated.isHovered);
    EXPECT_FALSE(updated.wasClickedThisFrame);
    EXPECT_EQ(runtime.GetDrawBatchUVE().quads[0].color, button.normalColor);
}

TEST_F(UIRuntimeUVETest, TickUVE_ButtonHitTest_HoverWithoutClickUsesHoverColorNotClicked) {
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::UIButtonComponentUVE button{};
    button.positionPixels = Math::Vector2UVE{0.0F, 0.0F};
    button.sizePixels = Math::Vector2UVE{50.0F, 50.0F};
    entityManager.AddComponentUVE<Scene::UIButtonComponentUVE>(entity, button);

    inputSystem.SetMousePositionUVE(Math::Vector2UVE{10.0F, 10.0F});
    inputSystem.UpdateUVE();

    runtime.TickUVE(entityManager, inputSystem);

    const Scene::UIButtonComponentUVE& updated = entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(entity);
    EXPECT_TRUE(updated.isHovered);
    EXPECT_FALSE(updated.wasClickedThisFrame);
    EXPECT_EQ(runtime.GetDrawBatchUVE().quads[0].color, button.hoverColor);
}

TEST_F(UIRuntimeUVETest, TickUVE_ClearsPreviousBatchEachCall) {
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(entity, Scene::UIImageComponentUVE{});

    inputSystem.UpdateUVE();
    runtime.TickUVE(entityManager, inputSystem);
    ASSERT_EQ(runtime.GetDrawBatchUVE().quads.size(), 1U);

    entityManager.DestroyEntityUVE(entity);
    runtime.TickUVE(entityManager, inputSystem);
    EXPECT_TRUE(runtime.GetDrawBatchUVE().quads.empty());
}

} // namespace
} // namespace UVE::UI::Tests
