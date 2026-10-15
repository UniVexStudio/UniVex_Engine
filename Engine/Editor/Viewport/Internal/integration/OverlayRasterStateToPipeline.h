// integration/OverlayRasterStateToPipeline.h
// -----------------------------------------------------------------------
// Foundation WP6: the same OverlayRasterState the GL overlay renderers apply,
// expressed as RHI pipeline state. A host that draws the editor overlays
// through IRenderDeviceUVE (any backend) builds its PipelineDescUVE from this
// so the depth modes are identical by construction, not by convention.
// -----------------------------------------------------------------------
#pragma once

#include "univex/render/OverlayRasterState.h"

#include "uve/rhi/render_resource_descs_uve.h"

namespace univex::integration {

/// Copies `base` (shaders, vertex layout, topology, ...) and overwrites exactly the raster fields
/// the overlay state owns: depthTestEnabled, depthWriteEnabled, depthCompare, blendMode and -
/// unless the state says Inherit - cullMode.
[[nodiscard]] UVE::Render::PipelineDescUVE ApplyOverlayRasterStateUVE(UVE::Render::PipelineDescUVE base,
                                                                      const render::OverlayRasterState& state);

} // namespace univex::integration
