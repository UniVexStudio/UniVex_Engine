// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <filesystem>
#include <limits>

#include <gtest/gtest.h>

#include "entity.h"
#include "ui_image_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "input_system.h"
#include "rect.h"
#include "vector2.h"
#include "vector3.h"
#include "memory_manager.h"
#include "scene_serializer.h"
#include "ui_runtime.h"

namespace UVE::UI::Tests {
namespace {

constexpr std::uint64_t kPanelTexture = 0x4040U;

class UINineSliceTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Input::InputSystem inputSystem{eventSystem};
    UIRuntime runtime;
    Scene::SceneSerializer serializer;

    void Frame() {
        inputSystem.Update();
        runtime.Tick(entityManager, inputSystem);
    }

    Scene::Entity MakeImage(const Math::Rect rect, const std::uint64_t texture) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UIImageComponent image;
        image.textureAssetGuid = Asset::AssetGuid{texture};
        image.rect = rect;
        entityManager.AddComponent<Scene::UIImageComponent>(entity, image);
        return entity;
    }

    Scene::UIImageComponent& Image(const Scene::Entity entity) {
        return entityManager.GetComponent<Scene::UIImageComponent>(entity);
    }
};

TEST_F(UINineSliceTest, Validator_RejectsNegativeMarginsAndOutOfRangeUVs) {
    Scene::UIImageComponent image;
    EXPECT_TRUE(IsUIImageComponentValid(image));
    image.sliceMarginMin = {10.0F, 5.0F};
    image.sliceMarginMax = {10.0F, 5.0F};
    image.sliceUVMin = {0.25F, 0.25F};
    image.sliceUVMax = {0.25F, 0.25F};
    EXPECT_TRUE(IsUIImageComponentValid(image));
    image.sliceMarginMax = {-1.0F, 5.0F};
    EXPECT_FALSE(IsUIImageComponentValid(image));
    image.sliceMarginMax = {10.0F, 5.0F};
    image.sliceUVMax = {1.5F, 0.25F};
    EXPECT_FALSE(IsUIImageComponentValid(image));
}

TEST_F(UINineSliceTest, DisabledOrFlat_DrawsOneQuad) {
    const Scene::Entity plain = MakeImage(Math::Rect{{0.0F, 0.0F}, {100.0F, 60.0F}}, kPanelTexture);
    Image(plain).sliceMarginMin = {10.0F, 5.0F}; // ignored while disabled
    const Scene::Entity flat = MakeImage(Math::Rect{{0.0F, 0.0F}, {100.0F, 60.0F}}, 0U);
    Image(flat).nineSliceEnabled = true; // ignored without a texture
    Image(flat).sliceMarginMin = {10.0F, 5.0F};
    Image(flat).sliceMarginMax = {10.0F, 5.0F};

    Frame();

    const std::vector<UIQuad>& quads = runtime.GetDrawBatch().quads;
    ASSERT_EQ(quads.size(), 2U);
    EXPECT_EQ(quads[0].rect, (Math::Rect{{0.0F, 0.0F}, {100.0F, 60.0F}}));
    EXPECT_EQ(quads[0].kind, UIDrawItemKind::Image);
    EXPECT_EQ(quads[1].kind, UIDrawItemKind::SolidColor);
}

TEST_F(UINineSliceTest, Slice_EmitsNineCellsWithMatchingRectsAndUVs) {
    const Scene::Entity panel = MakeImage(Math::Rect{{0.0F, 0.0F}, {100.0F, 60.0F}}, kPanelTexture);
    Scene::UIImageComponent& image = Image(panel);
    image.nineSliceEnabled = true;
    image.sliceMarginMin = {10.0F, 5.0F};
    image.sliceMarginMax = {10.0F, 5.0F};
    image.sliceUVMin = {0.25F, 0.25F};
    image.sliceUVMax = {0.25F, 0.25F};

    Frame();

    const std::vector<UIQuad>& quads = runtime.GetDrawBatch().quads;
    ASSERT_EQ(quads.size(), 9U);
    EXPECT_EQ(quads[0].rect, (Math::Rect{{0.0F, 0.0F}, {10.0F, 5.0F}}));
    EXPECT_FLOAT_EQ(quads[0].u0, 0.0F);
    EXPECT_FLOAT_EQ(quads[0].v0, 0.0F);
    EXPECT_FLOAT_EQ(quads[0].u1, 0.25F);
    EXPECT_FLOAT_EQ(quads[0].v1, 0.25F);
    EXPECT_EQ(quads[1].rect, (Math::Rect{{10.0F, 0.0F}, {80.0F, 5.0F}}));
    EXPECT_FLOAT_EQ(quads[1].u0, 0.25F);
    EXPECT_FLOAT_EQ(quads[1].u1, 0.75F);
    EXPECT_EQ(quads[4].rect, (Math::Rect{{10.0F, 5.0F}, {80.0F, 50.0F}}));
    EXPECT_FLOAT_EQ(quads[4].u0, 0.25F);
    EXPECT_FLOAT_EQ(quads[4].v0, 0.25F);
    EXPECT_FLOAT_EQ(quads[4].u1, 0.75F);
    EXPECT_FLOAT_EQ(quads[4].v1, 0.75F);
    EXPECT_EQ(quads[8].rect, (Math::Rect{{90.0F, 55.0F}, {10.0F, 5.0F}}));
    EXPECT_FLOAT_EQ(quads[8].u0, 0.75F);
    EXPECT_FLOAT_EQ(quads[8].v0, 0.75F);
    EXPECT_FLOAT_EQ(quads[8].u1, 1.0F);
    EXPECT_FLOAT_EQ(quads[8].v1, 1.0F);
    EXPECT_EQ(quads[4].imageAssetGuid.value, kPanelTexture);
}

TEST_F(UINineSliceTest, ZeroMargins_DrawOnlyTheCenterRegion) {
    const Scene::Entity panel = MakeImage(Math::Rect{{0.0F, 0.0F}, {100.0F, 60.0F}}, kPanelTexture);
    Scene::UIImageComponent& image = Image(panel);
    image.nineSliceEnabled = true;
    image.sliceUVMin = {0.25F, 0.25F};
    image.sliceUVMax = {0.25F, 0.25F};

    Frame();

    // No border drawn, so the border texture goes unsampled with it: the survivor covers the
    // whole rect but samples only the texture's middle half.
    const std::vector<UIQuad>& quads = runtime.GetDrawBatch().quads;
    ASSERT_EQ(quads.size(), 1U);
    EXPECT_EQ(quads[0].rect, (Math::Rect{{0.0F, 0.0F}, {100.0F, 60.0F}}));
    EXPECT_FLOAT_EQ(quads[0].u0, 0.25F);
    EXPECT_FLOAT_EQ(quads[0].v0, 0.25F);
    EXPECT_FLOAT_EQ(quads[0].u1, 0.75F);
    EXPECT_FLOAT_EQ(quads[0].v1, 0.75F);
}

TEST_F(UINineSliceTest, OversizedMargins_ShrinkProportionallyAndDropTheMiddle) {
    const Scene::Entity panel = MakeImage(Math::Rect{{0.0F, 0.0F}, {100.0F, 60.0F}}, kPanelTexture);
    Scene::UIImageComponent& image = Image(panel);
    image.nineSliceEnabled = true;
    image.sliceMarginMin = {60.0F, 5.0F};
    image.sliceMarginMax = {60.0F, 5.0F};
    image.sliceUVMin = {0.25F, 0.25F};
    image.sliceUVMax = {0.25F, 0.25F};

    Frame();

    const std::vector<UIQuad>& quads = runtime.GetDrawBatch().quads;
    ASSERT_EQ(quads.size(), 6U);
    EXPECT_FLOAT_EQ(quads[0].rect.size.x, 50.0F);
    EXPECT_FLOAT_EQ(quads[0].rect.size.y, 5.0F);
}

TEST_F(UINineSliceTest, NonFiniteMargins_FallBackToOneStretchedQuad) {
    const Scene::Entity panel = MakeImage(Math::Rect{{0.0F, 0.0F}, {100.0F, 60.0F}}, kPanelTexture);
    Scene::UIImageComponent& image = Image(panel);
    image.nineSliceEnabled = true;
    image.sliceMarginMin = {std::numeric_limits<float>::quiet_NaN(), 5.0F};
    image.sliceMarginMax = {10.0F, 5.0F};

    Frame();

    const std::vector<UIQuad>& quads = runtime.GetDrawBatch().quads;
    ASSERT_EQ(quads.size(), 1U);
    EXPECT_EQ(quads[0].rect, (Math::Rect{{0.0F, 0.0F}, {100.0F, 60.0F}}));
    EXPECT_FLOAT_EQ(quads[0].u1, 1.0F);
    EXPECT_FLOAT_EQ(quads[0].v1, 1.0F);
}

TEST_F(UINineSliceTest, NoFill_SkipsTheCenterCell) {
    const Scene::Entity panel = MakeImage(Math::Rect{{0.0F, 0.0F}, {100.0F, 60.0F}}, kPanelTexture);
    Scene::UIImageComponent& image = Image(panel);
    image.nineSliceEnabled = true;
    image.sliceFillCenter = false;
    image.sliceMarginMin = {10.0F, 5.0F};
    image.sliceMarginMax = {10.0F, 5.0F};
    image.sliceUVMin = {0.25F, 0.25F};
    image.sliceUVMax = {0.25F, 0.25F};

    Frame();

    const std::vector<UIQuad>& quads = runtime.GetDrawBatch().quads;
    ASSERT_EQ(quads.size(), 8U);
    EXPECT_EQ(quads[3].rect, (Math::Rect{{0.0F, 5.0F}, {10.0F, 50.0F}}));
    EXPECT_EQ(quads[4].rect, (Math::Rect{{90.0F, 5.0F}, {10.0F, 50.0F}}));
}

TEST_F(UINineSliceTest, SaveThenLoad_SliceConfig_RoundTripsExactly) {
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::UIImageComponent image;
    image.textureAssetGuid = Asset::AssetGuid{kPanelTexture};
    image.rect = Math::Rect{{10.0F, 20.0F}, {300.0F, 200.0F}};
    image.tintColor = Math::Vector3{0.9F, 0.9F, 0.9F};
    image.alpha = 0.5F;
    image.nineSliceEnabled = true;
    image.sliceFillCenter = false;
    image.sliceMarginMin = {12.0F, 8.0F};
    image.sliceMarginMax = {12.0F, 8.0F};
    image.sliceUVMin = {0.1F, 0.2F};
    image.sliceUVMax = {0.1F, 0.2F};
    entityManager.AddComponent<Scene::UIImageComponent>(entity, image);

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_nine_slice.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.Save(entityManager, {entity}, path, Scene::SceneAssetType::Scene));

    Scene::EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    const std::vector<Scene::Entity> loaded = serializer.Load(loadedManager, path);
    ASSERT_EQ(loaded.size(), 1U);
    const Scene::UIImageComponent& restored =
        loadedManager.GetComponent<Scene::UIImageComponent>(loaded[0]);
    EXPECT_EQ(restored.textureAssetGuid.value, kPanelTexture);
    EXPECT_FLOAT_EQ(restored.alpha, 0.5F);
    EXPECT_TRUE(restored.nineSliceEnabled);
    EXPECT_FALSE(restored.sliceFillCenter);
    EXPECT_EQ(restored.sliceMarginMin, (Math::Vector2{12.0F, 8.0F}));
    EXPECT_EQ(restored.sliceMarginMax, (Math::Vector2{12.0F, 8.0F}));
    EXPECT_EQ(restored.sliceUVMin, (Math::Vector2{0.1F, 0.2F}));
    EXPECT_EQ(restored.sliceUVMax, (Math::Vector2{0.1F, 0.2F}));

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::UI::Tests
