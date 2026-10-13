// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "uve/rhi/resource_handle_uve.h"

namespace UVE::Render {

/// Opaque handle to a GPU sampler object created via IRenderDeviceUVE::CreateSamplerUVE().
/// Instantiation of the shared ResourceHandleUVE template - see resource_handle_uve.h for the
/// shared wrapper/equality/hash semantics.
struct SamplerTagUVE {};
using SamplerHandleUVE = ResourceHandleUVE<SamplerTagUVE>;

/// The sentinel "no sampler" value. Never returned by a successful CreateSamplerUVE() call.
inline constexpr SamplerHandleUVE kInvalidSamplerHandleUVE{};

} // namespace UVE::Render
