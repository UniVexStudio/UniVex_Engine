// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <limits>
#include <optional>
#include <type_traits>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/component/sprite_2d_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/2d/all_objects_2d_uve.h"

namespace UVE::Scene::Tests {
namespace {

// The 2D object definition is reachable from the aggregate header, mirroring the 3D
// definitions test's guarantee: no 2D kind's home file can be silently dropped without
// breaking this compile.
static_assert(std::is_class_v<Sprite2DObjectDefinitionUVE>); // Sprite2D

TEST(Sprite2DComponentUVETest, DefaultsAreValidUVE) {
    EXPECT_TRUE(IsSprite2DComponentValidUVE(Sprite2DComponentUVE{}));
}

TEST(Sprite2DComponentUVETest, AlphaIsAClosedUnitIntervalUVE) {
    Sprite2DComponentUVE low{};
    low.alpha = 0.0F;
    EXPECT_TRUE(IsSprite2DComponentValidUVE(low));
    Sprite2DComponentUVE high{};
    high.alpha = 1.0F;
    EXPECT_TRUE(IsSprite2DComponentValidUVE(high));

    Sprite2DComponentUVE below{};
    below.alpha = -0.001F;
    EXPECT_FALSE(IsSprite2DComponentValidUVE(below));
    Sprite2DComponentUVE above{};
    above.alpha = 1.001F;
    EXPECT_FALSE(IsSprite2DComponentValidUVE(above));
    Sprite2DComponentUVE notANumber{};
    notANumber.alpha = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsSprite2DComponentValidUVE(notANumber));
}

TEST(Sprite2DComponentUVETest, ModulateAndOffsetMustBeFiniteUVE) {
    Sprite2DComponentUVE badTint{};
    badTint.modulate = Math::Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 1.0F, 1.0F};
    EXPECT_FALSE(IsSprite2DComponentValidUVE(badTint));
    Sprite2DComponentUVE badOffset{};
    badOffset.offset = Math::Vector2UVE{std::numeric_limits<float>::infinity(), 0.0F};
    EXPECT_FALSE(IsSprite2DComponentValidUVE(badOffset));
}

TEST(Sprite2DComponentUVETest, SourceRectStaysInsideItsPixelBoundsUVE) {
    Sprite2DComponentUVE edge{};
    edge.sourceRect.position = Math::Vector2UVE{8192.0F, 8192.0F};
    EXPECT_TRUE(IsSprite2DComponentValidUVE(edge));

    Sprite2DComponentUVE pastOrigin{};
    pastOrigin.sourceRect.position = Math::Vector2UVE{8192.01F, 0.0F};
    EXPECT_FALSE(IsSprite2DComponentValidUVE(pastOrigin));
    Sprite2DComponentUVE negativeOrigin{};
    negativeOrigin.sourceRect.position = Math::Vector2UVE{-1.0F, 0.0F};
    EXPECT_FALSE(IsSprite2DComponentValidUVE(negativeOrigin));
    Sprite2DComponentUVE pastSize{};
    pastSize.sourceRect.size = Math::Vector2UVE{0.0F, 8193.0F};
    EXPECT_FALSE(IsSprite2DComponentValidUVE(pastSize));
    Sprite2DComponentUVE notANumber{};
    notANumber.sourceRect.size = Math::Vector2UVE{std::numeric_limits<float>::quiet_NaN(), 1.0F};
    EXPECT_FALSE(IsSprite2DComponentValidUVE(notANumber));
}

TEST(Sprite2DComponentUVETest, ZeroAreaIsTheWholeTextureSentinelUVE) {
    Sprite2DComponentUVE zeroWidth{};
    zeroWidth.sourceRect.size = Math::Vector2UVE{0.0F, 64.0F};
    EXPECT_TRUE(IsSprite2DComponentValidUVE(zeroWidth));
}

TEST(ComposeSprite2DQuadUVETest, DefaultsComposeTheWholeTextureCenteredUVE) {
    const std::optional<Sprite2DQuadUVE> quad =
        ComposeSprite2DQuadUVE(Sprite2DComponentUVE{}, Math::Vector2UVE{256.0F, 128.0F});
    ASSERT_TRUE(quad.has_value());
    EXPECT_FLOAT_EQ(quad->corners[0].x, -128.0F);
    EXPECT_FLOAT_EQ(quad->corners[0].y, -64.0F);
    EXPECT_FLOAT_EQ(quad->corners[1].x, 128.0F);
    EXPECT_FLOAT_EQ(quad->corners[1].y, -64.0F);
    EXPECT_FLOAT_EQ(quad->corners[2].x, 128.0F);
    EXPECT_FLOAT_EQ(quad->corners[2].y, 64.0F);
    EXPECT_FLOAT_EQ(quad->corners[3].x, -128.0F);
    EXPECT_FLOAT_EQ(quad->corners[3].y, 64.0F);
    EXPECT_FLOAT_EQ(quad->uvs[0].x, 0.0F);
    EXPECT_FLOAT_EQ(quad->uvs[0].y, 0.0F);
    EXPECT_FLOAT_EQ(quad->uvs[1].x, 1.0F);
    EXPECT_FLOAT_EQ(quad->uvs[1].y, 0.0F);
    EXPECT_FLOAT_EQ(quad->uvs[2].x, 1.0F);
    EXPECT_FLOAT_EQ(quad->uvs[2].y, 1.0F);
    EXPECT_FLOAT_EQ(quad->uvs[3].x, 0.0F);
    EXPECT_FLOAT_EQ(quad->uvs[3].y, 1.0F);
}

TEST(ComposeSprite2DQuadUVETest, AnyZeroExtentFallsBackToTheWholeTextureUVE) {
    Sprite2DComponentUVE zeroWidth{};
    zeroWidth.sourceRect.position = Math::Vector2UVE{64.0F, 32.0F};
    zeroWidth.sourceRect.size = Math::Vector2UVE{0.0F, 64.0F};
    const std::optional<Sprite2DQuadUVE> widthQuad =
        ComposeSprite2DQuadUVE(zeroWidth, Math::Vector2UVE{256.0F, 128.0F});
    ASSERT_TRUE(widthQuad.has_value());
    EXPECT_FLOAT_EQ(widthQuad->corners[1].x, 128.0F);
    EXPECT_FLOAT_EQ(widthQuad->uvs[1].x, 1.0F);

    Sprite2DComponentUVE zeroHeight{};
    zeroHeight.sourceRect.size = Math::Vector2UVE{64.0F, 0.0F};
    const std::optional<Sprite2DQuadUVE> heightQuad =
        ComposeSprite2DQuadUVE(zeroHeight, Math::Vector2UVE{256.0F, 128.0F});
    ASSERT_TRUE(heightQuad.has_value());
    EXPECT_FLOAT_EQ(heightQuad->corners[2].y, 64.0F);
    EXPECT_FLOAT_EQ(heightQuad->uvs[2].y, 1.0F);
}

TEST(ComposeSprite2DQuadUVETest, RegionQuadCentersOnTheOffsetUVE) {
    Sprite2DComponentUVE sprite{};
    sprite.sourceRect.position = Math::Vector2UVE{64.0F, 32.0F};
    sprite.sourceRect.size = Math::Vector2UVE{32.0F, 16.0F};
    sprite.offset = Math::Vector2UVE{10.0F, -5.0F};
    const std::optional<Sprite2DQuadUVE> quad =
        ComposeSprite2DQuadUVE(sprite, Math::Vector2UVE{256.0F, 128.0F});
    ASSERT_TRUE(quad.has_value());
    EXPECT_FLOAT_EQ(quad->corners[0].x, -6.0F);
    EXPECT_FLOAT_EQ(quad->corners[0].y, -13.0F);
    EXPECT_FLOAT_EQ(quad->corners[1].x, 26.0F);
    EXPECT_FLOAT_EQ(quad->corners[1].y, -13.0F);
    EXPECT_FLOAT_EQ(quad->corners[2].x, 26.0F);
    EXPECT_FLOAT_EQ(quad->corners[2].y, 3.0F);
    EXPECT_FLOAT_EQ(quad->corners[3].x, -6.0F);
    EXPECT_FLOAT_EQ(quad->corners[3].y, 3.0F);
    EXPECT_FLOAT_EQ(quad->uvs[0].x, 0.25F);
    EXPECT_FLOAT_EQ(quad->uvs[0].y, 0.25F);
    EXPECT_FLOAT_EQ(quad->uvs[1].x, 0.375F);
    EXPECT_FLOAT_EQ(quad->uvs[1].y, 0.25F);
    EXPECT_FLOAT_EQ(quad->uvs[2].x, 0.375F);
    EXPECT_FLOAT_EQ(quad->uvs[2].y, 0.375F);
    EXPECT_FLOAT_EQ(quad->uvs[3].x, 0.25F);
    EXPECT_FLOAT_EQ(quad->uvs[3].y, 0.375F);
}

TEST(ComposeSprite2DQuadUVETest, FlipsSwapTheUVOrderAndNeverTheCornersUVE) {
    Sprite2DComponentUVE sprite{};
    sprite.sourceRect.position = Math::Vector2UVE{64.0F, 32.0F};
    sprite.sourceRect.size = Math::Vector2UVE{32.0F, 16.0F};
    const Math::Vector2UVE texture{256.0F, 128.0F};

    Sprite2DComponentUVE flippedH = sprite;
    flippedH.flipH = true;
    const std::optional<Sprite2DQuadUVE> quadH = ComposeSprite2DQuadUVE(flippedH, texture);
    ASSERT_TRUE(quadH.has_value());
    EXPECT_FLOAT_EQ(quadH->corners[0].x, -16.0F);
    EXPECT_FLOAT_EQ(quadH->uvs[0].x, 0.375F);
    EXPECT_FLOAT_EQ(quadH->uvs[0].y, 0.25F);
    EXPECT_FLOAT_EQ(quadH->uvs[1].x, 0.25F);
    EXPECT_FLOAT_EQ(quadH->uvs[2].x, 0.25F);
    EXPECT_FLOAT_EQ(quadH->uvs[2].y, 0.375F);
    EXPECT_FLOAT_EQ(quadH->uvs[3].x, 0.375F);

    Sprite2DComponentUVE flippedV = sprite;
    flippedV.flipV = true;
    const std::optional<Sprite2DQuadUVE> quadV = ComposeSprite2DQuadUVE(flippedV, texture);
    ASSERT_TRUE(quadV.has_value());
    EXPECT_FLOAT_EQ(quadV->corners[2].y, 8.0F);
    EXPECT_FLOAT_EQ(quadV->uvs[0].x, 0.25F);
    EXPECT_FLOAT_EQ(quadV->uvs[0].y, 0.375F);
    EXPECT_FLOAT_EQ(quadV->uvs[2].x, 0.375F);
    EXPECT_FLOAT_EQ(quadV->uvs[2].y, 0.25F);
}

TEST(ComposeSprite2DQuadUVETest, BadInputComposesToNothingUVE) {
    const Math::Vector2UVE texture{256.0F, 256.0F};
    Sprite2DComponentUVE invalid{};
    invalid.alpha = 2.0F;
    EXPECT_FALSE(ComposeSprite2DQuadUVE(invalid, texture).has_value());
    EXPECT_FALSE(ComposeSprite2DQuadUVE(Sprite2DComponentUVE{}, Math::Vector2UVE{0.0F, 256.0F}).has_value());
    EXPECT_FALSE(ComposeSprite2DQuadUVE(Sprite2DComponentUVE{}, Math::Vector2UVE{-1.0F, 256.0F}).has_value());
    EXPECT_FALSE(ComposeSprite2DQuadUVE(Sprite2DComponentUVE{},
                                        Math::Vector2UVE{std::numeric_limits<float>::quiet_NaN(), 256.0F})
                     .has_value());

    Sprite2DComponentUVE escaping{};
    escaping.sourceRect.position = Math::Vector2UVE{240.0F, 0.0F};
    escaping.sourceRect.size = Math::Vector2UVE{32.0F, 32.0F};
    EXPECT_FALSE(ComposeSprite2DQuadUVE(escaping, texture).has_value());

    // Touching the edge is still inside.
    Sprite2DComponentUVE touching{};
    touching.sourceRect.position = Math::Vector2UVE{224.0F, 0.0F};
    touching.sourceRect.size = Math::Vector2UVE{32.0F, 32.0F};
    EXPECT_TRUE(ComposeSprite2DQuadUVE(touching, texture).has_value());
}

TEST(Sprite2DObjectDefinitionUVETest, DefinitionValidityMirrorsTheSpriteUVE) {
    EXPECT_TRUE(IsSprite2DObjectDefinitionValidUVE(Sprite2DObjectDefinitionUVE{}));
    Sprite2DObjectDefinitionUVE invalid{};
    invalid.sprite.alpha = 2.0F;
    EXPECT_FALSE(IsSprite2DObjectDefinitionValidUVE(invalid));
}

class Sprite2DObjectUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
};

TEST_F(Sprite2DObjectUVETest, ApplyAttachesTheAuthoredSpriteUVE) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    Sprite2DObjectDefinitionUVE definition{};
    definition.sprite.textureAssetGuid = Asset::AssetGuidUVE{0x2D2DU};
    definition.sprite.sourceRect.position = Math::Vector2UVE{8.0F, 8.0F};
    definition.sprite.sourceRect.size = Math::Vector2UVE{32.0F, 32.0F};
    definition.sprite.flipH = true;
    definition.sprite.modulate = Math::Vector3UVE{0.5F, 0.5F, 0.5F};
    definition.sprite.alpha = 0.9F;
    definition.sprite.offset = Math::Vector2UVE{4.0F, -4.0F};
    ApplySprite2DObjectDefinitionUVE(entityManager, entity, definition);

    const Sprite2DComponentUVE& stored = entityManager.GetComponentUVE<Sprite2DComponentUVE>(entity);
    EXPECT_EQ(stored.textureAssetGuid.value, 0x2D2DU);
    EXPECT_FLOAT_EQ(stored.sourceRect.size.x, 32.0F);
    EXPECT_TRUE(stored.flipH);
    EXPECT_FALSE(stored.flipV);
    EXPECT_FLOAT_EQ(stored.modulate.z, 0.5F);
    EXPECT_FLOAT_EQ(stored.alpha, 0.9F);
    EXPECT_FLOAT_EQ(stored.offset.y, -4.0F);
}

} // namespace
} // namespace UVE::Scene::Tests
