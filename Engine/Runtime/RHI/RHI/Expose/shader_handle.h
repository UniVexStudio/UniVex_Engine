// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "resource_handle.h"

namespace UVE::Render {

/// Opaque handle to a shader resource created via IRenderDevice::CreateShader().
/// Instantiation of the shared ResourceHandle template - see resource_handle_uve.h for the
/// shared wrapper/equality/hash semantics.
struct ShaderTag {};
using ShaderHandle = ResourceHandle<ShaderTag>;

/// The sentinel "no shader" value. Never returned by a successful CreateShader() call.
inline constexpr ShaderHandle kInvalidShaderHandle{};

} // namespace UVE::Render
