// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>

#include "i_command_buffer.h"

namespace UVE::Render {

/// IRenderSystem is the engine's top-level per-frame render orchestrator (the spec's
/// `RenderSystem`, Part 7.2 — "Abstract rendering backend"): it owns the one command buffer a
/// frame records into and drives its lifecycle against an injected IRenderDevice. Later
/// increments (CameraSystem, MeshRenderer, Renderer3D) record draw calls into the buffer
/// this returns; IRenderSystem itself knows nothing about cameras, meshes, or materials.
/// Thread-safety: not thread-safe. BeginFrame()/GetFrameCommandBuffer()/EndFrame() must
/// be called only from the main engine thread, once per frame, matching EngineCore's
/// single-threaded frame-loop contract.
class IRenderSystem {
public:
    virtual ~IRenderSystem() = default;

    /// Begins a new frame: creates a fresh command buffer for GetFrameCommandBuffer() to
    /// return. Must not be called while a frame is already active.
    virtual void BeginFrame() = 0;

    /// Returns the current frame's command buffer for recording. Must be called only between
    /// BeginFrame() and EndFrame().
    [[nodiscard]] virtual ICommandBuffer& GetFrameCommandBuffer() = 0;

    /// Submits the current frame's command buffer and ends the frame. Must be called exactly
    /// once per BeginFrame().
    virtual void EndFrame() = 0;

    /// How many frames have completed (EndFrame() returned) so far. Zero before the first
    /// EndFrame() call.
    [[nodiscard]] virtual std::uint64_t GetFrameIndex() const noexcept = 0;
};

} // namespace UVE::Render
