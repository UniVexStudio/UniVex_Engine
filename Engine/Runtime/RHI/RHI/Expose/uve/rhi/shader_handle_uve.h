// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "uve/rhi/resource_handle_uve.h"

namespace UVE::Render {

/// Opaque handle to a shader resource created via IRenderDeviceUVE::CreateShaderUVE().
/// Instantiation of the shared ResourceHandleUVE template - see resource_handle_uve.h for the
/// shared wrapper/equality/hash semantics.
struct ShaderTagUVE {};
using ShaderHandleUVE = ResourceHandleUVE<ShaderTagUVE>;

/// The sentinel "no shader" value. Never returned by a successful CreateShaderUVE() call.
inline constexpr ShaderHandleUVE kInvalidShaderHandleUVE{};

} // namespace UVE::Render
