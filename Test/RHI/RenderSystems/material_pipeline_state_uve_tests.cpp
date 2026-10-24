// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>

#include <gtest/gtest.h>

#include "uve/asset/material_asset_uve.h"
#include "uve/render_systems/material_pipeline_state_uve.h"
#include "uve/rhi/render_resource_descs_uve.h"

namespace UVE::Render::Tests {
namespace {

using Asset::MaterialAssetUVE;
using Asset::MaterialBlendModeUVE;
using Asset::MaterialCullModeUVE;
using Asset::MaterialDepthDrawModeUVE;

// Every authored blend mode, so a mode added later without a pipeline home fails here instead of
// silently drawing opaque.
TEST(MaterialPipelineStateUVETest, EveryBlendModeHasAPipelineEquivalent) {
    EXPECT_EQ(ToPipelineBlendModeUVE(MaterialBlendModeUVE::Opaque), PipelineBlendModeUVE::Opaque);
    EXPECT_EQ(ToPipelineBlendModeUVE(MaterialBlendModeUVE::AlphaBlend), PipelineBlendModeUVE::SourceAlphaOver);
    EXPECT_EQ(ToPipelineBlendModeUVE(MaterialBlendModeUVE::PremultipliedAlpha),
              PipelineBlendModeUVE::PremultipliedAlpha);
    EXPECT_EQ(ToPipelineBlendModeUVE(MaterialBlendModeUVE::Additive), PipelineBlendModeUVE::Additive);
    EXPECT_EQ(ToPipelineBlendModeUVE(MaterialBlendModeUVE::Subtractive), PipelineBlendModeUVE::Subtractive);
    EXPECT_EQ(ToPipelineBlendModeUVE(MaterialBlendModeUVE::Multiply), PipelineBlendModeUVE::Multiply);

    constexpr MaterialBlendModeUVE kAll[] = {
        MaterialBlendModeUVE::Opaque,       MaterialBlendModeUVE::AlphaBlend,
        MaterialBlendModeUVE::PremultipliedAlpha, MaterialBlendModeUVE::Additive,
        MaterialBlendModeUVE::Subtractive,  MaterialBlendModeUVE::Multiply};
    for (const MaterialBlendModeUVE mode : kAll) {
        // A blend mode the pipeline cannot express would be a control that composites as
        // something else, which is the bug this whole table exists to remove.
        EXPECT_TRUE(IsPipelineBlendModeValidUVE(ToPipelineBlendModeUVE(mode)));
    }
}

TEST(MaterialPipelineStateUVETest, CullModesReachTheRasterizer) {
    EXPECT_EQ(ToCullModeUVE(MaterialCullModeUVE::Back), CullModeUVE::Back);
    EXPECT_EQ(ToCullModeUVE(MaterialCullModeUVE::Front), CullModeUVE::Front);
    EXPECT_EQ(ToCullModeUVE(MaterialCullModeUVE::Disabled), CullModeUVE::None);
}

// The importer's own intent, finally honoured: a glTF material with doubleSided=true is written as
// Disabled, which has to mean the rasterizer draws both faces.
TEST(MaterialPipelineStateUVETest, ATwoSidedMaterialDisablesCulling) {
    MaterialAssetUVE material;
    material.cullMode = MaterialCullModeUVE::Disabled;
    EXPECT_EQ(ResolveMaterialPipelineStateUVE(material).cullMode, CullModeUVE::None);
}

TEST(MaterialPipelineStateUVETest, OpaqueOnlyWritesDepthForOpaqueAndNotForTranslucent) {
    MaterialAssetUVE opaque;
    opaque.depthDrawMode = MaterialDepthDrawModeUVE::OpaqueOnly;
    opaque.blendMode = MaterialBlendModeUVE::Opaque;
    EXPECT_TRUE(MaterialWritesDepthUVE(opaque));

    MaterialAssetUVE blended = opaque;
    blended.blendMode = MaterialBlendModeUVE::AlphaBlend;
    EXPECT_FALSE(MaterialWritesDepthUVE(blended)) << "an alpha blend must not occlude what is behind it";

    // The legacy spelling has to reach the same answer, or old files start writing depth again.
    MaterialAssetUVE legacy = opaque;
    legacy.isTransparent = true;
    EXPECT_FALSE(MaterialWritesDepthUVE(legacy));
    EXPECT_EQ(ResolveMaterialPipelineStateUVE(legacy).blendMode, PipelineBlendModeUVE::SourceAlphaOver);
}

// albedoAlpha is the third spelling of translucency, and the one that was easiest to get wrong:
// the shader has always been able to emit the alpha (now that it reaches the fragment output), so a
// material that only halves albedoAlpha has to get a blending pipeline - otherwise it is bucketed
// into the transparent pass and then composited as if it were solid.
TEST(MaterialPipelineStateUVETest, AHalfAlbedoAlphaGetsABlendingPipelineEvenWithAnOpaqueMode) {
    MaterialAssetUVE material;
    material.blendMode = MaterialBlendModeUVE::Opaque;
    material.albedoAlpha = 1.0F;
    EXPECT_EQ(ResolveMaterialPipelineStateUVE(material).blendMode, PipelineBlendModeUVE::Opaque);
    EXPECT_TRUE(MaterialWritesDepthUVE(material));

    material.albedoAlpha = 0.5F;
    EXPECT_EQ(ResolveMaterialPipelineStateUVE(material).blendMode, PipelineBlendModeUVE::SourceAlphaOver);
    EXPECT_FALSE(MaterialWritesDepthUVE(material));
    // The queue and the renderer have to agree about it, or it sorts as translucent and draws solid.
    EXPECT_TRUE(Asset::ShouldBucketMaterialTransparentUVE(material));
}

// The bucketing predicate and the pipeline state have to answer the same question for every
// material an author can produce. This walks the combinations rather than asserting one example,
// because the failure mode is a single cell disagreeing in the corner of the table.
TEST(MaterialPipelineStateUVETest, EveryTranslucentMaterialIsBothBucketedAndBlended) {
    for (const MaterialBlendModeUVE mode : {MaterialBlendModeUVE::Opaque, MaterialBlendModeUVE::AlphaBlend,
                                            MaterialBlendModeUVE::PremultipliedAlpha,
                                            MaterialBlendModeUVE::Additive, MaterialBlendModeUVE::Subtractive,
                                            MaterialBlendModeUVE::Multiply}) {
        for (const bool legacyFlag : {false, true}) {
            for (const float alpha : {1.0F, 0.5F}) {
                MaterialAssetUVE material;
                material.blendMode = mode;
                material.isTransparent = legacyFlag;
                material.albedoAlpha = alpha;
                const bool bucketed = Asset::ShouldBucketMaterialTransparentUVE(material);
                const bool blended =
                    ResolveMaterialPipelineStateUVE(material).blendMode != PipelineBlendModeUVE::Opaque;
                EXPECT_EQ(bucketed, blended)
                    << "mode " << static_cast<int>(mode) << " flag " << legacyFlag << " alpha " << alpha;
            }
        }
    }
}

TEST(MaterialPipelineStateUVETest, TheDepthRuleCanOverrideTheBlendModeInBothDirections) {
    MaterialAssetUVE translucentDepth = MaterialAssetUVE{};
    translucentDepth.blendMode = MaterialBlendModeUVE::AlphaBlend;
    translucentDepth.depthDrawMode = MaterialDepthDrawModeUVE::Always;
    EXPECT_TRUE(MaterialWritesDepthUVE(translucentDepth)) << "Always means always, even when blending";

    MaterialAssetUVE opaqueNoDepth = MaterialAssetUVE{};
    opaqueNoDepth.blendMode = MaterialBlendModeUVE::Opaque;
    opaqueNoDepth.depthDrawMode = MaterialDepthDrawModeUVE::Never;
    EXPECT_FALSE(MaterialWritesDepthUVE(opaqueNoDepth));

    MaterialAssetUVE prePass = MaterialAssetUVE{};
    prePass.blendMode = MaterialBlendModeUVE::Additive;
    prePass.depthDrawMode = MaterialDepthDrawModeUVE::DepthPrePass;
    EXPECT_TRUE(MaterialWritesDepthUVE(prePass));
}

// The capability switch sits UNDER the rule: turning it off has to win over every mode, or the
// control is decorative.
TEST(MaterialPipelineStateUVETest, TheDepthWriteSwitchBeatsEveryDrawMode) {
    MaterialAssetUVE material;
    material.depthWriteEnabled = false;
    for (const MaterialDepthDrawModeUVE mode : {MaterialDepthDrawModeUVE::OpaqueOnly,
                                                MaterialDepthDrawModeUVE::Always,
                                                MaterialDepthDrawModeUVE::DepthPrePass}) {
        material.depthDrawMode = mode;
        EXPECT_FALSE(MaterialWritesDepthUVE(material)) << static_cast<int>(mode);
    }
}

TEST(MaterialPipelineStateUVETest, DepthTestingIsTheMaterialsOwnSwitch) {
    MaterialAssetUVE material;
    EXPECT_TRUE(ResolveMaterialPipelineStateUVE(material).depthTestEnabled);
    material.depthTestEnabled = false;
    EXPECT_FALSE(ResolveMaterialPipelineStateUVE(material).depthTestEnabled);
}

TEST(MaterialPipelineStateUVETest, ABlendModeWinsOverTheLegacyTransparencyFlag) {
    // isTransparent is what a file written before blend modes existed carries. Once an author picks
    // a mode, the mode is the answer - otherwise clearing the flag would silently turn an additive
    // material opaque again.
    MaterialAssetUVE material;
    material.isTransparent = false;
    material.blendMode = MaterialBlendModeUVE::Additive;
    EXPECT_EQ(ResolveMaterialPipelineStateUVE(material).blendMode, PipelineBlendModeUVE::Additive);
    EXPECT_FALSE(MaterialWritesDepthUVE(material));
}

TEST(MaterialPipelineStateUVETest, PackedKeysDistinguishStateAndCollideAcrossNothingElse) {
    MaterialPipelineStateUVE first;
    EXPECT_EQ(first.PackUVE(), MaterialPipelineStateUVE{}.PackUVE());
    EXPECT_EQ(first, MaterialPipelineStateUVE{});

    // Each field alone has to change the key, or two materials would share one cached program.
    MaterialPipelineStateUVE culled = first;
    culled.cullMode = CullModeUVE::Back;
    EXPECT_NE(culled.PackUVE(), first.PackUVE());

    MaterialPipelineStateUVE noTest = first;
    noTest.depthTestEnabled = false;
    EXPECT_NE(noTest.PackUVE(), first.PackUVE());

    MaterialPipelineStateUVE noWrite = first;
    noWrite.depthWriteEnabled = false;
    EXPECT_NE(noWrite.PackUVE(), first.PackUVE());

    // Fill mode and front face are not part of a material's choice, so every combination the
    // renderer can produce has to fit the packing without two of them landing on one key.
    std::uint32_t keys[6U * 4U * 2U * 2U]{};
    std::size_t count = 0U;
    for (std::uint32_t blend = 0U; blend < 6U; ++blend) {
        for (std::uint32_t cullIndex = 0U; cullIndex < 4U; ++cullIndex) {
            for (std::uint32_t depthTest = 0U; depthTest < 2U; ++depthTest) {
                for (std::uint32_t depthWrite = 0U; depthWrite < 2U; ++depthWrite) {
                    const MaterialPipelineStateUVE state{
                        static_cast<PipelineBlendModeUVE>(blend), static_cast<CullModeUVE>(cullIndex),
                        depthTest != 0U, depthWrite != 0U};
                    keys[count++] = state.PackUVE();
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

TEST(MaterialPipelineStateUVETest, OnlyThePremultipliedModeAsksForPremultiplication) {
    MaterialAssetUVE material;
    for (const MaterialBlendModeUVE mode : {MaterialBlendModeUVE::Opaque, MaterialBlendModeUVE::AlphaBlend,
                                            MaterialBlendModeUVE::Additive, MaterialBlendModeUVE::Subtractive,
                                            MaterialBlendModeUVE::Multiply}) {
        material.blendMode = mode;
        EXPECT_FALSE(MaterialPremultipliesAlphaUVE(material)) << static_cast<int>(mode);
    }
    material.blendMode = MaterialBlendModeUVE::PremultipliedAlpha;
    EXPECT_TRUE(MaterialPremultipliesAlphaUVE(material));
}

// The one predicate both the render queue and the renderer ask.
TEST(MaterialPipelineStateUVETest, OnlyOpaqueIsNotTranslucent) {
    EXPECT_FALSE(Asset::IsMaterialBlendModeTranslucentUVE(MaterialBlendModeUVE::Opaque));
    for (const MaterialBlendModeUVE mode : {MaterialBlendModeUVE::AlphaBlend,
                                            MaterialBlendModeUVE::PremultipliedAlpha,
                                            MaterialBlendModeUVE::Additive, MaterialBlendModeUVE::Subtractive,
                                            MaterialBlendModeUVE::Multiply}) {
        EXPECT_TRUE(Asset::IsMaterialBlendModeTranslucentUVE(mode)) << static_cast<int>(mode);
    }
}

} // namespace
} // namespace UVE::Render::Tests
