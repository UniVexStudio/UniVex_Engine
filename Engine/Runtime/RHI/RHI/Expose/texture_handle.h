// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "resource_handle.h"

namespace UVE::Render {

/// Opaque handle to a GPU texture resource created via IRenderDevice::CreateTexture().
/// Instantiation of the shared ResourceHandle template - see resource_handle_uve.h for the
/// shared wrapper/equality/hash semantics.
struct TextureTag {};
using TextureHandle = ResourceHandle<TextureTag>;

/// The sentinel "no texture" value. Never returned by a successful CreateTexture() call; also
/// used as RenderPassDesc::depthAttachment for a color-only render pass.
inline constexpr TextureHandle kInvalidTextureHandle{};

} // namespace UVE::Render
