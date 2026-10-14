// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// Foundation WP6: the overlay depth/blend/cull state is data. These cases pin the presets the GL
// overlay renderers apply and prove the engine bridge maps the same values onto RHI pipeline
// state field by field, so a host drawing the overlays through IRenderDeviceUVE cannot drift
// from the GL path's depth semantics.

#include <gtest/gtest.h>

#include "integration/OverlayRasterStateToPipeline.h"
#include "univex/render/OverlayRasterState.h"

namespace UVE::Render::Tests {
namespace {

using univex::render::OverlayBlendMode;
using univex::render::OverlayCullMode;
using univex::render::OverlayDepthCompare;
using univex::render::OverlayRasterState;

TEST(OverlayRasterStateUVETest, PresetsPinTheOverlayDepthModes) {
    EXPECT_TRUE(univex::render::kGizmoOpaqueSolidState.depthTest);
    EXPECT_TRUE(univex::render::kGizmoOpaqueSolidState.depthWrite) << "opaque solids occlude the widget itself";
    EXPECT_FALSE(univex::render::kGizmoTranslucentSolidState.depthWrite)
        << "a half-transparent handle must not reject the geometry showing through it";
    EXPECT_FALSE(univex::render::kGizmoStrokeState.depthWrite)
        << "an anti-aliased stroke edge must not punch a hole into what is behind it";
    EXPECT_EQ(univex::render::kGizmoStrokeState.cull, OverlayCullMode::None) << "gizmos are viewed from every side";
    EXPECT_TRUE(univex::render::kInfiniteGridState.depthTest);
    EXPECT_FALSE(univex::render::kInfiniteGridState.depthWrite) << "the grid never occludes the scene";
    EXPECT_EQ(univex::render::kInfiniteGridState.cull, OverlayCullMode::Inherit);
    EXPECT_EQ(univex::render::kStudioBackdropSkyState.depthCompare, OverlayDepthCompare::LessOrEqual)
        << "the sky sits exactly at the cleared far plane";
    EXPECT_EQ(univex::render::kStudioBackdropSkyState.blend, OverlayBlendMode::Opaque);
    EXPECT_FALSE(univex::render::kScreenSpaceCompositeState.depthTest);

    const OverlayRasterState hidden = univex::render::WithDepthTest(univex::render::kGizmoStrokeState, false);
    EXPECT_FALSE(hidden.depthTest);
    EXPECT_EQ(univex::render::WithDepthTest(hidden, true), univex::render::kGizmoStrokeState);
    EXPECT_TRUE(univex::render::WithDepthWrite(univex::render::kGizmoStrokeState, true).depthWrite);
}

TEST(OverlayRasterStateUVETest, BridgeMapsTheStateOntoPipelineDescFieldByField) {
    PipelineDescUVE base{};
    base.vertexStride = 24U;
    base.topology = PrimitiveTopologyUVE::Lines;
    base.cullMode = CullModeUVE::Back;
    base.frontFace = FrontFaceUVE::Clockwise;

    const PipelineDescUVE opaque = univex::integration::ApplyOverlayRasterStateUVE(
        base, univex::render::kGizmoOpaqueSolidState);
    EXPECT_TRUE(opaque.depthTestEnabled);
    EXPECT_TRUE(opaque.depthWriteEnabled);
    EXPECT_EQ(opaque.depthCompare, DepthCompareUVE::Less);
    EXPECT_EQ(opaque.blendMode, PipelineBlendModeUVE::SourceAlphaOver);
    EXPECT_EQ(opaque.cullMode, CullModeUVE::None) << "the gizmo state owns the cull mode";
    EXPECT_EQ(opaque.vertexStride, 24U) << "fields the state does not own are untouched";
    EXPECT_EQ(opaque.topology, PrimitiveTopologyUVE::Lines);
    EXPECT_EQ(opaque.frontFace, FrontFaceUVE::Clockwise);

    const PipelineDescUVE grid =
        univex::integration::ApplyOverlayRasterStateUVE(base, univex::render::kInfiniteGridState);
    EXPECT_FALSE(grid.depthWriteEnabled);
    EXPECT_EQ(grid.cullMode, CullModeUVE::Back) << "Inherit keeps the host's cull mode";

    const PipelineDescUVE sky =
        univex::integration::ApplyOverlayRasterStateUVE(base, univex::render::kStudioBackdropSkyState);
    EXPECT_EQ(sky.depthCompare, DepthCompareUVE::LessOrEqual);
    EXPECT_EQ(sky.blendMode, PipelineBlendModeUVE::Opaque);
    EXPECT_FALSE(sky.depthWriteEnabled);

    const PipelineDescUVE composite =
        univex::integration::ApplyOverlayRasterStateUVE(base, univex::render::kScreenSpaceCompositeState);
    EXPECT_FALSE(composite.depthTestEnabled);
    EXPECT_EQ(composite.depthCompare, DepthCompareUVE::Always);

    OverlayRasterState backCulled = univex::render::kGizmoStrokeState;
    backCulled.cull = OverlayCullMode::Back;
    EXPECT_EQ(univex::integration::ApplyOverlayRasterStateUVE(base, backCulled).cullMode, CullModeUVE::Back);
}

} // namespace
} // namespace UVE::Render::Tests
