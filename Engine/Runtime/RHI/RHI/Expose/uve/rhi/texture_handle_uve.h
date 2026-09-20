// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "uve/rhi/resource_handle_uve.h"

namespace UVE::Render {

/// Opaque handle to a GPU texture resource created via IRenderDeviceUVE::CreateTextureUVE().
/// Instantiation of the shared ResourceHandleUVE template - see resource_handle_uve.h for the
/// shared wrapper/equality/hash semantics.
struct TextureTagUVE {};
using TextureHandleUVE = ResourceHandleUVE<TextureTagUVE>;

/// The sentinel "no texture" value. Never returned by a successful CreateTextureUVE() call; also
/// used as RenderPassDescUVE::depthAttachment for a color-only render pass.
inline constexpr TextureHandleUVE kInvalidTextureHandleUVE{};

} // namespace UVE::Render
