// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "uve/rhi/resource_handle_uve.h"

namespace UVE::Render {

/// Opaque handle to a pipeline state object created via IRenderDeviceUVE::CreatePipelineUVE().
/// Instantiation of the shared ResourceHandleUVE template - see resource_handle_uve.h for the
/// shared wrapper/equality/hash semantics.
struct PipelineTagUVE {};
using PipelineHandleUVE = ResourceHandleUVE<PipelineTagUVE>;

/// The sentinel "no pipeline" value. Never returned by a successful CreatePipelineUVE() call.
inline constexpr PipelineHandleUVE kInvalidPipelineHandleUVE{};

} // namespace UVE::Render
