// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "resource_handle.h"

namespace UVE::Render {

/// Opaque handle to a GPU buffer resource created via IRenderDevice::CreateBuffer().
/// Instantiation of the shared ResourceHandle template - see resource_handle_uve.h for why
/// this is a small wrapper struct rather than a bare std::uint32_t alias, and for the
/// equality/hash semantics shared by all four kinds.
struct BufferTag {};
using BufferHandle = ResourceHandle<BufferTag>;

/// The sentinel "no buffer" value. Never returned by a successful CreateBuffer() call.
inline constexpr BufferHandle kInvalidBufferHandle{};

} // namespace UVE::Render
