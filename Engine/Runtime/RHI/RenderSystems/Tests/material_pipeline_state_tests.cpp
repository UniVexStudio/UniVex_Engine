// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>

#include <gtest/gtest.h>

#include "material_asset.h"
#include "material_pipeline_state.h"
#include "render_resource_descs.h"

namespace UVE::Render::Tests {
namespace {

using Asset::MaterialAsset;
using Asset::MaterialBlendMode;
using Asset::MaterialCullMode;
using Asset::MaterialDepthDrawMode;

// Every authored blend mode, so a mode added later without a pipeline home fails here instead of
// silently drawing opaque.
TEST(MaterialPipelineStateTest, EveryBlendModeHasAPipelineEquivalent) {
    EXPECT_EQ(ToPipelineBlendMode(MaterialBlendMode::Opaque), PipelineBlendMode::Opaque);
    EXPECT_EQ(ToPipelineBlendMode(MaterialBlendMode::AlphaBlend), PipelineBlendMode::SourceAlphaOver);
    EXPECT_EQ(ToPipelineBlendMode(MaterialBlendMode::PremultipliedAlpha),
              PipelineBlendMode::PremultipliedAlpha);
    EXPECT_EQ(ToPipelineBlendMode(MaterialBlendMode::Additive), PipelineBlendMode::Additive);
    EXPECT_EQ(ToPipelineBlendMode(MaterialBlendMode::Subtractive), PipelineBlendMode::Subtractive);
    EXPECT_EQ(ToPipelineBlendMode(MaterialBlendMode::Multiply), PipelineBlendMode::Multiply);

    constexpr MaterialBlendMode kAll[] = {
        MaterialBlendMode::Opaque,       MaterialBlendMode::AlphaBlend,
        MaterialBlendMode::PremultipliedAlpha, MaterialBlendMode::Additive,
        MaterialBlendMode::Subtractive,  MaterialBlendMode::Multiply};
    for (const MaterialBlendMode mode : kAll) {
        // A blend mode the pipeline cannot express would be a control that composites as
        // something else, which is the bug this whole table exists to remove.
        EXPECT_TRUE(IsPipelineBlendModeValid(ToPipelineBlendMode(mode)));
    }
}

TEST(MaterialPipelineStateTest, CullModesReachTheRasterizer) {
    EXPECT_EQ(ToCullMode(MaterialCullMode::Back), CullMode::Back);
    EXPECT_EQ(ToCullMode(MaterialCullMode::Front), CullMode::Front);
    EXPECT_EQ(ToCullMode(MaterialCullMode::Disabled), CullMode::None);
}

// The importer's own intent, finally honoured: a glTF material with doubleSided=true is written as
// Disabled, which has to mean the rasterizer draws both faces.
TEST(MaterialPipelineStateTest, ATwoSidedMaterialDisablesCulling) {
    MaterialAsset material;
    material.cullMode = MaterialCullMode::Disabled;
    EXPECT_EQ(ResolveMaterialPipelineState(material).cullMode, CullMode::None);
}

TEST(MaterialPipelineStateTest, OpaqueOnlyWritesDepthForOpaqueAndNotForTranslucent) {
    MaterialAsset opaque;
    opaque.depthDrawMode = MaterialDepthDrawMode::OpaqueOnly;
    opaque.blendMode = MaterialBlendMode::Opaque;
    EXPECT_TRUE(MaterialWritesDepth(opaque));

    MaterialAsset blended = opaque;
    blended.blendMode = MaterialBlendMode::AlphaBlend;
    EXPECT_FALSE(MaterialWritesDepth(blended)) << "an alpha blend must not occlude what is behind it";

    // The legacy spelling has to reach the same answer, or old files start writing depth again.
    MaterialAsset legacy = opaque;
    legacy.isTransparent = true;
    EXPECT_FALSE(MaterialWritesDepth(legacy));
    EXPECT_EQ(ResolveMaterialPipelineState(legacy).blendMode, PipelineBlendMode::SourceAlphaOver);
}

// albedoAlpha is the third spelling of translucency, and the one that was easiest to get wrong:
// the shader has always been able to emit the alpha (now that it reaches the fragment output), so a
// material that only halves albedoAlpha has to get a blending pipeline - otherwise it is bucketed
// into the transparent pass and then composited as if it were solid.
TEST(MaterialPipelineStateTest, AHalfAlbedoAlphaGetsABlendingPipelineEvenWithAnOpaqueMode) {
    MaterialAsset material;
    material.blendMode = MaterialBlendMode::Opaque;
    material.albedoAlpha = 1.0F;
    EXPECT_EQ(ResolveMaterialPipelineState(material).blendMode, PipelineBlendMode::Opaque);
    EXPECT_TRUE(MaterialWritesDepth(material));

    material.albedoAlpha = 0.5F;
    EXPECT_EQ(ResolveMaterialPipelineState(material).blendMode, PipelineBlendMode::SourceAlphaOver);
    EXPECT_FALSE(MaterialWritesDepth(material));
    // The queue and the renderer have to agree about it, or it sorts as translucent and draws solid.
    EXPECT_TRUE(Asset::ShouldBucketMaterialTransparent(material));
}

// The bucketing predicate and the pipeline state have to answer the same question for every
// material an author can produce. This walks the combinations rather than asserting one example,
// because the failure mode is a single cell disagreeing in the corner of the table.
TEST(MaterialPipelineStateTest, EveryTranslucentMaterialIsBothBucketedAndBlended) {
    for (const MaterialBlendMode mode : {MaterialBlendMode::Opaque, MaterialBlendMode::AlphaBlend,
                                            MaterialBlendMode::PremultipliedAlpha,
                                            MaterialBlendMode::Additive, MaterialBlendMode::Subtractive,
                                            MaterialBlendMode::Multiply}) {
        for (const bool legacyFlag : {false, true}) {
            for (const float alpha : {1.0F, 0.5F}) {
                MaterialAsset material;
                material.blendMode = mode;
                material.isTransparent = legacyFlag;
                material.albedoAlpha = alpha;
                const bool bucketed = Asset::ShouldBucketMaterialTransparent(material);
                const bool blended =
                    ResolveMaterialPipelineState(material).blendMode != PipelineBlendMode::Opaque;
                EXPECT_EQ(bucketed, blended)
                    << "mode " << static_cast<int>(mode) << " flag " << legacyFlag << " alpha " << alpha;
            }
        }
    }
}

TEST(MaterialPipelineStateTest, TheDepthRuleCanOverrideTheBlendModeInBothDirections) {
    MaterialAsset translucentDepth = MaterialAsset{};
    translucentDepth.blendMode = MaterialBlendMode::AlphaBlend;
    translucentDepth.depthDrawMode = MaterialDepthDrawMode::Always;
    EXPECT_TRUE(MaterialWritesDepth(translucentDepth)) << "Always means always, even when blending";

    MaterialAsset opaqueNoDepth = MaterialAsset{};
    opaqueNoDepth.blendMode = MaterialBlendMode::Opaque;
    opaqueNoDepth.depthDrawMode = MaterialDepthDrawMode::Never;
    EXPECT_FALSE(MaterialWritesDepth(opaqueNoDepth));

    MaterialAsset prePass = MaterialAsset{};
    prePass.blendMode = MaterialBlendMode::Additive;
    prePass.depthDrawMode = MaterialDepthDrawMode::DepthPrePass;
    EXPECT_TRUE(MaterialWritesDepth(prePass));
}

// The capability switch sits UNDER the rule: turning it off has to win over every mode, or the
// control is decorative.
TEST(MaterialPipelineStateTest, TheDepthWriteSwitchBeatsEveryDrawMode) {
    MaterialAsset material;
    material.depthWriteEnabled = false;
    for (const MaterialDepthDrawMode mode : {MaterialDepthDrawMode::OpaqueOnly,
                                                MaterialDepthDrawMode::Always,
                                                MaterialDepthDrawMode::DepthPrePass}) {
        material.depthDrawMode = mode;
        EXPECT_FALSE(MaterialWritesDepth(material)) << static_cast<int>(mode);
    }
}

TEST(MaterialPipelineStateTest, DepthTestingIsTheMaterialsOwnSwitch) {
    MaterialAsset material;
    EXPECT_TRUE(ResolveMaterialPipelineState(material).depthTestEnabled);
    material.depthTestEnabled = false;
    EXPECT_FALSE(ResolveMaterialPipelineState(material).depthTestEnabled);
}

TEST(MaterialPipelineStateTest, ABlendModeWinsOverTheLegacyTransparencyFlag) {
    // isTransparent is what a file written before blend modes existed carries. Once an author picks
    // a mode, the mode is the answer - otherwise clearing the flag would silently turn an additive
    // material opaque again.
    MaterialAsset material;
    material.isTransparent = false;
    material.blendMode = MaterialBlendMode::Additive;
    EXPECT_EQ(ResolveMaterialPipelineState(material).blendMode, PipelineBlendMode::Additive);
    EXPECT_FALSE(MaterialWritesDepth(material));
}

TEST(MaterialPipelineStateTest, PackedKeysDistinguishStateAndCollideAcrossNothingElse) {
    MaterialPipelineState first;
    EXPECT_EQ(first.Pack(), MaterialPipelineState{}.Pack());
    EXPECT_EQ(first, MaterialPipelineState{});

    // Each field alone has to change the key, or two materials would share one cached program.
    MaterialPipelineState culled = first;
    culled.cullMode = CullMode::Back;
    EXPECT_NE(culled.Pack(), first.Pack());

    MaterialPipelineState noTest = first;
    noTest.depthTestEnabled = false;
    EXPECT_NE(noTest.Pack(), first.Pack());

    MaterialPipelineState noWrite = first;
    noWrite.depthWriteEnabled = false;
    EXPECT_NE(noWrite.Pack(), first.Pack());

    // Fill mode and front face are not part of a material's choice, so every combination the
    // renderer can produce has to fit the packing without two of them landing on one key.
    std::uint32_t keys[6U * 4U * 2U * 2U]{};
    std::size_t count = 0U;
    for (std::uint32_t blend = 0U; blend < 6U; ++blend) {
        for (std::uint32_t cullIndex = 0U; cullIndex < 4U; ++cullIndex) {
            for (std::uint32_t depthTest = 0U; depthTest < 2U; ++depthTest) {
                for (std::uint32_t depthWrite = 0U; depthWrite < 2U; ++depthWrite) {
                    const MaterialPipelineState state{
                        static_cast<PipelineBlendMode>(blend), static_cast<CullMode>(cullIndex),
                        depthTest != 0U, depthWrite != 0U};
                    keys[count++] = state.Pack();
                }
            }
        }
    }
    for (std::size_t left = 0U; left < count; ++left) {
        for (std::size_t right = left + 1U; right < count; ++right) {
            EXPECT_NE(keys[left], keys[right]) << "states " << left << " and " << right << " share a key";
        }
    }
}

TEST(MaterialPipelineStateTest, OnlyThePremultipliedModeAsksForPremultiplication) {
    MaterialAsset material;
    for (const MaterialBlendMode mode : {MaterialBlendMode::Opaque, MaterialBlendMode::AlphaBlend,
                                            MaterialBlendMode::Additive, MaterialBlendMode::Subtractive,
                                            MaterialBlendMode::Multiply}) {
        material.blendMode = mode;
        EXPECT_FALSE(MaterialPremultipliesAlpha(material)) << static_cast<int>(mode);
    }
    material.blendMode = MaterialBlendMode::PremultipliedAlpha;
    EXPECT_TRUE(MaterialPremultipliesAlpha(material));
}

// The one predicate both the render queue and the renderer ask.
TEST(MaterialPipelineStateTest, OnlyOpaqueIsNotTranslucent) {
    EXPECT_FALSE(Asset::IsMaterialBlendModeTranslucent(MaterialBlendMode::Opaque));
    for (const MaterialBlendMode mode : {MaterialBlendMode::AlphaBlend,
                                            MaterialBlendMode::PremultipliedAlpha,
                                            MaterialBlendMode::Additive, MaterialBlendMode::Subtractive,
                                            MaterialBlendMode::Multiply}) {
        EXPECT_TRUE(Asset::IsMaterialBlendModeTranslucent(mode)) << static_cast<int>(mode);
    }
}

} // namespace
} // namespace UVE::Render::Tests
