// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/ui_runtime_uve.h"

#include <algorithm>

#include <gtest/gtest.h>

#include "uve/events/event_system_uve.h"
#include "uve/input/input_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/scene/components/ui_button_component_uve.h"
#include "uve/scene/components/ui_image_component_uve.h"
#include "uve/scene/components/ui_text_component_uve.h"
#include "uve/scene/entity_manager_uve.h"

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
