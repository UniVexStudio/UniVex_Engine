#include "integration/OverlayRasterStateToPipeline.h"

namespace univex::integration {

namespace {

[[nodiscard]] UVE::Render::DepthCompareUVE ToDepthCompareUVE(const render::OverlayDepthCompare compare) {
    switch (compare) {
        case render::OverlayDepthCompare::Less: return UVE::Render::DepthCompareUVE::Less;
        case render::OverlayDepthCompare::LessOrEqual: return UVE::Render::DepthCompareUVE::LessOrEqual;
        case render::OverlayDepthCompare::Always: return UVE::Render::DepthCompareUVE::Always;
    }
    return UVE::Render::DepthCompareUVE::Less;
}

} // namespace

UVE::Render::PipelineDescUVE ApplyOverlayRasterStateUVE(UVE::Render::PipelineDescUVE base,
                                                        const render::OverlayRasterState& state) {
    base.depthTestEnabled = state.depthTest;
    base.depthWriteEnabled = state.depthWrite;
    base.depthCompare = ToDepthCompareUVE(state.depthCompare);
    base.blendMode = state.blend == render::OverlayBlendMode::Alpha ? UVE::Render::PipelineBlendModeUVE::SourceAlphaOver
                                                                    : UVE::Render::PipelineBlendModeUVE::Opaque;
    switch (state.cull) {
        case render::OverlayCullMode::Inherit:
            break;
        case render::OverlayCullMode::None:
            base.cullMode = UVE::Render::CullModeUVE::None;
            break;
        case render::OverlayCullMode::Back:
            base.cullMode = UVE::Render::CullModeUVE::Back;
            break;
    }
    return base;
}

} // namespace univex::integration
