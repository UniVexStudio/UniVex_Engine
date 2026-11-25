// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "resource_handle.h"

namespace UVE::Render {

/// Opaque handle to a GPU sampler object created via IRenderDevice::CreateSampler().
/// Instantiation of the shared ResourceHandle template - see resource_handle_uve.h for the
/// shared wrapper/equality/hash semantics.
struct SamplerTag {};
using SamplerHandle = ResourceHandle<SamplerTag>;

/// The sentinel "no sampler" value. Never returned by a successful CreateSampler() call.
inline constexpr SamplerHandle kInvalidSamplerHandle{};

} // namespace UVE::Render
