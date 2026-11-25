// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "resource_handle.h"

namespace UVE::Render {

/// Opaque handle to a pipeline state object created via IRenderDevice::CreatePipeline().
/// Instantiation of the shared ResourceHandle template - see resource_handle_uve.h for the
/// shared wrapper/equality/hash semantics.
struct PipelineTag {};
using PipelineHandle = ResourceHandle<PipelineTag>;

/// The sentinel "no pipeline" value. Never returned by a successful CreatePipeline() call.
inline constexpr PipelineHandle kInvalidPipelineHandle{};

} // namespace UVE::Render
