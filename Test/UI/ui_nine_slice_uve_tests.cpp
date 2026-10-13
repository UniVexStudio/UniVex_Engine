// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <filesystem>
#include <limits>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/input/input_system_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/scene/scene_serializer_uve.h"
#include "uve/ui/ui_runtime_uve.h"

namespace UVE::UI::Tests {
namespace {

constexpr std::uint64_t kPanelTextureUVE = 0x4040U;

class UINineSliceUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Input::InputSystemUVE inputSystem{eventSystem};
    UIRuntimeUVE runtime;
    Scene::SceneSerializerUVE serializer;

    void FrameUVE() {
        inputSystem.UpdateUVE();
        runtime.TickUVE(entityManager, inputSystem);
    }

    Scene::EntityUVE MakeImageUVE(const Math::RectUVE rect, const std::uint64_t texture) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UIImageComponentUVE image;
        image.textureAssetGuid = Asset::AssetGuidUVE{texture};
        image.rect = rect;
        entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(entity, image);
        return entity;
    }

    Scene::UIImageComponentUVE& ImageUVE(const Scene::EntityUVE entity) {
        return entityManager.GetComponentUVE<Scene::UIImageComponentUVE>(entity);
    }
};

TEST_F(UINineSliceUVETest, Validator_RejectsNegativeMarginsAndOutOfRangeUVs) {
    Scene::UIImageComponentUVE image;
    EXPECT_TRUE(IsUIImageComponentValidUVE(image));
    image.sliceMarginMin = {10.0F, 5.0F};
    image.sliceMarginMax = {10.0F, 5.0F};
    image.sliceUVMin = {0.25F, 0.25F};
    image.sliceUVMax = {0.25F, 0.25F};
    EXPECT_TRUE(IsUIImageComponentValidUVE(image));
    image.sliceMarginMax = {-1.0F, 5.0F};
    EXPECT_FALSE(IsUIImageComponentValidUVE(image));
    image.sliceMarginMax = {10.0F, 5.0F};
    image.sliceUVMax = {1.5F, 0.25F};
    EXPECT_FALSE(IsUIImageComponentValidUVE(image));
}

TEST_F(UINineSliceUVETest, DisabledOrFlat_DrawsOneQuad) {
    const Scene::EntityUVE plain = MakeImageUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 60.0F}}, kPanelTextureUVE);
    ImageUVE(plain).sliceMarginMin = {10.0F, 5.0F}; // ignored while disabled
    const Scene::EntityUVE flat = MakeImageUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 60.0F}}, 0U);
    ImageUVE(flat).nineSliceEnabled = true; // ignored without a texture
    ImageUVE(flat).sliceMarginMin = {10.0F, 5.0F};
    ImageUVE(flat).sliceMarginMax = {10.0F, 5.0F};

    FrameUVE();

    const std::vector<UIQuadUVE>& quads = runtime.GetDrawBatchUVE().quads;
    ASSERT_EQ(quads.size(), 2U);
    EXPECT_EQ(quads[0].rect, (Math::RectUVE{{0.0F, 0.0F}, {100.0F, 60.0F}}));
    EXPECT_EQ(quads[0].kind, UIDrawItemKindUVE::Image);
    EXPECT_EQ(quads[1].kind, UIDrawItemKindUVE::SolidColor);
}

TEST_F(UINineSliceUVETest, Slice_EmitsNineCellsWithMatchingRectsAndUVs) {
    const Scene::EntityUVE panel = MakeImageUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 60.0F}}, kPanelTextureUVE);
    Scene::UIImageComponentUVE& image = ImageUVE(panel);
    image.nineSliceEnabled = true;
    image.sliceMarginMin = {10.0F, 5.0F};
    image.sliceMarginMax = {10.0F, 5.0F};
    image.sliceUVMin = {0.25F, 0.25F};
    image.sliceUVMax = {0.25F, 0.25F};

    FrameUVE();

    const std::vector<UIQuadUVE>& quads = runtime.GetDrawBatchUVE().quads;
    ASSERT_EQ(quads.size(), 9U);
    EXPECT_EQ(quads[0].rect, (Math::RectUVE{{0.0F, 0.0F}, {10.0F, 5.0F}}));
    EXPECT_FLOAT_EQ(quads[0].u0, 0.0F);
    EXPECT_FLOAT_EQ(quads[0].v0, 0.0F);
    EXPECT_FLOAT_EQ(quads[0].u1, 0.25F);
    EXPECT_FLOAT_EQ(quads[0].v1, 0.25F);
    EXPECT_EQ(quads[1].rect, (Math::RectUVE{{10.0F, 0.0F}, {80.0F, 5.0F}}));
    EXPECT_FLOAT_EQ(quads[1].u0, 0.25F);
    EXPECT_FLOAT_EQ(quads[1].u1, 0.75F);
    EXPECT_EQ(quads[4].rect, (Math::RectUVE{{10.0F, 5.0F}, {80.0F, 50.0F}}));
    EXPECT_FLOAT_EQ(quads[4].u0, 0.25F);
    EXPECT_FLOAT_EQ(quads[4].v0, 0.25F);
    EXPECT_FLOAT_EQ(quads[4].u1, 0.75F);
    EXPECT_FLOAT_EQ(quads[4].v1, 0.75F);
    EXPECT_EQ(quads[8].rect, (Math::RectUVE{{90.0F, 55.0F}, {10.0F, 5.0F}}));
    EXPECT_FLOAT_EQ(quads[8].u0, 0.75F);
    EXPECT_FLOAT_EQ(quads[8].v0, 0.75F);
    EXPECT_FLOAT_EQ(quads[8].u1, 1.0F);
    EXPECT_FLOAT_EQ(quads[8].v1, 1.0F);
    EXPECT_EQ(quads[4].imageAssetGuid.value, kPanelTextureUVE);
}

TEST_F(UINineSliceUVETest, ZeroMargins_DrawOnlyTheCenterRegion) {
    const Scene::EntityUVE panel = MakeImageUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 60.0F}}, kPanelTextureUVE);
    Scene::UIImageComponentUVE& image = ImageUVE(panel);
    image.nineSliceEnabled = true;
    image.sliceUVMin = {0.25F, 0.25F};
    image.sliceUVMax = {0.25F, 0.25F};

    FrameUVE();

    // No border drawn, so the border texture goes unsampled with it: the survivor covers the
    // whole rect but samples only the texture's middle half.
    const std::vector<UIQuadUVE>& quads = runtime.GetDrawBatchUVE().quads;
    ASSERT_EQ(quads.size(), 1U);
    EXPECT_EQ(quads[0].rect, (Math::RectUVE{{0.0F, 0.0F}, {100.0F, 60.0F}}));
    EXPECT_FLOAT_EQ(quads[0].u0, 0.25F);
    EXPECT_FLOAT_EQ(quads[0].v0, 0.25F);
    EXPECT_FLOAT_EQ(quads[0].u1, 0.75F);
    EXPECT_FLOAT_EQ(quads[0].v1, 0.75F);
}

TEST_F(UINineSliceUVETest, OversizedMargins_ShrinkProportionallyAndDropTheMiddle) {
    const Scene::EntityUVE panel = MakeImageUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 60.0F}}, kPanelTextureUVE);
    Scene::UIImageComponentUVE& image = ImageUVE(panel);
    image.nineSliceEnabled = true;
    image.sliceMarginMin = {60.0F, 5.0F};
    image.sliceMarginMax = {60.0F, 5.0F};
    image.sliceUVMin = {0.25F, 0.25F};
    image.sliceUVMax = {0.25F, 0.25F};

    FrameUVE();

    const std::vector<UIQuadUVE>& quads = runtime.GetDrawBatchUVE().quads;
    ASSERT_EQ(quads.size(), 6U);
    EXPECT_FLOAT_EQ(quads[0].rect.size.x, 50.0F);
    EXPECT_FLOAT_EQ(quads[0].rect.size.y, 5.0F);
}

TEST_F(UINineSliceUVETest, NonFiniteMargins_FallBackToOneStretchedQuad) {
    const Scene::EntityUVE panel = MakeImageUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 60.0F}}, kPanelTextureUVE);
    Scene::UIImageComponentUVE& image = ImageUVE(panel);
    image.nineSliceEnabled = true;
    image.sliceMarginMin = {std::numeric_limits<float>::quiet_NaN(), 5.0F};
    image.sliceMarginMax = {10.0F, 5.0F};

    FrameUVE();

    const std::vector<UIQuadUVE>& quads = runtime.GetDrawBatchUVE().quads;
    ASSERT_EQ(quads.size(), 1U);
    EXPECT_EQ(quads[0].rect, (Math::RectUVE{{0.0F, 0.0F}, {100.0F, 60.0F}}));
    EXPECT_FLOAT_EQ(quads[0].u1, 1.0F);
    EXPECT_FLOAT_EQ(quads[0].v1, 1.0F);
}

TEST_F(UINineSliceUVETest, NoFill_SkipsTheCenterCell) {
    const Scene::EntityUVE panel = MakeImageUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 60.0F}}, kPanelTextureUVE);
    Scene::UIImageComponentUVE& image = ImageUVE(panel);
    image.nineSliceEnabled = true;
    image.sliceFillCenter = false;
    image.sliceMarginMin = {10.0F, 5.0F};
    image.sliceMarginMax = {10.0F, 5.0F};
    image.sliceUVMin = {0.25F, 0.25F};
    image.sliceUVMax = {0.25F, 0.25F};

    FrameUVE();

    const std::vector<UIQuadUVE>& quads = runtime.GetDrawBatchUVE().quads;
    ASSERT_EQ(quads.size(), 8U);
    EXPECT_EQ(quads[3].rect, (Math::RectUVE{{0.0F, 5.0F}, {10.0F, 50.0F}}));
    EXPECT_EQ(quads[4].rect, (Math::RectUVE{{90.0F, 5.0F}, {10.0F, 50.0F}}));
}

TEST_F(UINineSliceUVETest, SaveThenLoad_SliceConfig_RoundTripsExactly) {
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::UIImageComponentUVE image;
    image.textureAssetGuid = Asset::AssetGuidUVE{kPanelTextureUVE};
    image.rect = Math::RectUVE{{10.0F, 20.0F}, {300.0F, 200.0F}};
    image.tintColor = Math::Vector3UVE{0.9F, 0.9F, 0.9F};
    image.alpha = 0.5F;
    image.nineSliceEnabled = true;
    image.sliceFillCenter = false;
    image.sliceMarginMin = {12.0F, 8.0F};
    image.sliceMarginMax = {12.0F, 8.0F};
    image.sliceUVMin = {0.1F, 0.2F};
    image.sliceUVMax = {0.1F, 0.2F};
    entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(entity, image);

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_nine_slice.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, Scene::SceneAssetTypeUVE::Scene));

    Scene::EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<Scene::EntityUVE> loaded = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(loaded.size(), 1U);
    const Scene::UIImageComponentUVE& restored =
        loadedManager.GetComponentUVE<Scene::UIImageComponentUVE>(loaded[0]);
    EXPECT_EQ(restored.textureAssetGuid.value, kPanelTextureUVE);
    EXPECT_FLOAT_EQ(restored.alpha, 0.5F);
    EXPECT_TRUE(restored.nineSliceEnabled);
    EXPECT_FALSE(restored.sliceFillCenter);
    EXPECT_EQ(restored.sliceMarginMin, (Math::Vector2UVE{12.0F, 8.0F}));
    EXPECT_EQ(restored.sliceMarginMax, (Math::Vector2UVE{12.0F, 8.0F}));
    EXPECT_EQ(restored.sliceUVMin, (Math::Vector2UVE{0.1F, 0.2F}));
    EXPECT_EQ(restored.sliceUVMax, (Math::Vector2UVE{0.1F, 0.2F}));

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::UI::Tests
