// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "uve/rhi/resource_handle_uve.h"

namespace UVE::Render {

/// Opaque handle to a GPU buffer resource created via IRenderDeviceUVE::CreateBufferUVE().
/// Instantiation of the shared ResourceHandleUVE template - see resource_handle_uve.h for why
/// this is a small wrapper struct rather than a bare std::uint32_t alias, and for the
/// equality/hash semantics shared by all four kinds.
struct BufferTagUVE {};
using BufferHandleUVE = ResourceHandleUVE<BufferTagUVE>;

/// The sentinel "no buffer" value. Never returned by a successful CreateBufferUVE() call.
inline constexpr BufferHandleUVE kInvalidBufferHandleUVE{};

} // namespace UVE::Render
