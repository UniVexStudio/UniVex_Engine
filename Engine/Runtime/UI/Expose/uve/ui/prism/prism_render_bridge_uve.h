// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <memory>
#include "uve/math/matrix4x4_uve.h"
#include "uve/rhi/buffer_handle_uve.h"
#include "uve/rhi/i_command_buffer_uve.h"
#include "uve/rhi/i_render_device_uve.h"
#include "uve/rhi/pipeline_handle_uve.h"
#include "uve/rhi/texture_handle_uve.h"
#include "uve/ui/prism/prism_draw_list_uve.h"

namespace UVE::UI::Prism {

/// Diagnostics and telemetry for Prism GPU submissions.
struct PrismRenderDiagnosticsUVE final {
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t drawCallsRecorded = 0;
    std::uint32_t batchesCulled = 0;

    [[nodiscard]] constexpr bool operator==(const PrismRenderDiagnosticsUVE&) const noexcept = default;
};

/// RHI-Agnostic Render Bridge for Prism UI.
/// Translates PrismDrawListUVE into dynamic GPU vertex/index buffers and records
/// draw commands into an ICommandBufferUVE using an orthographic screen-space projection.
class PrismRenderBridgeUVE final {
public:
    explicit PrismRenderBridgeUVE(Render::IRenderDeviceUVE& renderDevice);
    ~PrismRenderBridgeUVE();

    PrismRenderBridgeUVE(const PrismRenderBridgeUVE&) = delete;
    PrismRenderBridgeUVE& operator=(const PrismRenderBridgeUVE&) = delete;

    /// Sets the default fallback white texture (used when draw command textureId == 0).
    void SetFallbackWhiteTexture(Render::TextureHandleUVE texture) noexcept { m_whiteTexture = texture; }

    /// Sets the UI overlay pipeline state object.
    void SetPipeline(Render::PipelineHandleUVE pipeline) noexcept { m_pipeline = pipeline; }

    /// Records all draw commands from `drawList` into `commandBuffer`.
    /// `targetWidth` and `targetHeight` define the screen-space viewport bounds.
    void RecordDrawCommandsUVE(const PrismDrawListUVE& drawList,
                               Render::ICommandBufferUVE& commandBuffer,
                               std::uint32_t targetWidth,
                               std::uint32_t targetHeight);

    [[nodiscard]] PrismRenderDiagnosticsUVE GetLastDiagnostics() const noexcept { return m_lastDiagnostics; }

private:
    void EnsureBuffers(std::size_t vertexCount, std::size_t indexCount);

    Render::IRenderDeviceUVE&     m_renderDevice;
    Render::BufferHandleUVE       m_vertexBuffer = Render::kInvalidBufferHandleUVE;
    Render::BufferHandleUVE       m_indexBuffer = Render::kInvalidBufferHandleUVE;
    std::size_t                   m_vertexCapacity = 0;
    std::size_t                   m_indexCapacity = 0;

    Render::PipelineHandleUVE     m_pipeline = Render::kInvalidPipelineHandleUVE;
    Render::TextureHandleUVE      m_whiteTexture = Render::kInvalidTextureHandleUVE;
    PrismRenderDiagnosticsUVE     m_lastDiagnostics{};
};

} // namespace UVE::UI::Prism
