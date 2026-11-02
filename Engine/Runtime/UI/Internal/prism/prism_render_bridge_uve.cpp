// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_render_bridge_uve.h"
#include <algorithm>
#include <span>

namespace UVE::UI::Prism {

PrismRenderBridgeUVE::PrismRenderBridgeUVE(Render::IRenderDeviceUVE& renderDevice)
    : m_renderDevice(renderDevice) {}

PrismRenderBridgeUVE::~PrismRenderBridgeUVE() {
    if (m_vertexBuffer != Render::kInvalidBufferHandleUVE) {
        m_renderDevice.DestroyBufferUVE(m_vertexBuffer);
        m_vertexBuffer = Render::kInvalidBufferHandleUVE;
    }
    if (m_indexBuffer != Render::kInvalidBufferHandleUVE) {
        m_renderDevice.DestroyBufferUVE(m_indexBuffer);
        m_indexBuffer = Render::kInvalidBufferHandleUVE;
    }
}

void PrismRenderBridgeUVE::EnsureBuffers(std::size_t vertexCount, std::size_t indexCount) {
    if (vertexCount > m_vertexCapacity) {
        if (m_vertexBuffer != Render::kInvalidBufferHandleUVE) {
            m_renderDevice.DestroyBufferUVE(m_vertexBuffer);
        }
        m_vertexCapacity = std::max(vertexCount * 2U, static_cast<std::size_t>(1024U));
        m_vertexBuffer = m_renderDevice.CreateBufferUVE(Render::BufferDescUVE{
            m_vertexCapacity * sizeof(PrismVertexUVE),
            Render::BufferUsageUVE::Vertex,
            Render::BufferMemoryUVE::Upload,
            Render::BufferUpdateFrequencyUVE::Stream
        });
    }

    if (indexCount > m_indexCapacity) {
        if (m_indexBuffer != Render::kInvalidBufferHandleUVE) {
            m_renderDevice.DestroyBufferUVE(m_indexBuffer);
        }
        m_indexCapacity = std::max(indexCount * 2U, static_cast<std::size_t>(2048U));
        m_indexBuffer = m_renderDevice.CreateBufferUVE(Render::BufferDescUVE{
            m_indexCapacity * sizeof(std::uint32_t),
            Render::BufferUsageUVE::Index,
            Render::BufferMemoryUVE::Upload,
            Render::BufferUpdateFrequencyUVE::Stream
        });
    }
}

void PrismRenderBridgeUVE::RecordDrawCommandsUVE(const PrismDrawListUVE& drawList,
                                                 Render::ICommandBufferUVE& commandBuffer,
                                                 std::uint32_t targetWidth,
                                                 std::uint32_t targetHeight) {
    m_lastDiagnostics = PrismRenderDiagnosticsUVE{};
    const auto& vertices = drawList.GetVertices();
    const auto& indices = drawList.GetIndices();
    const auto& commands = drawList.GetCommands();

    if (vertices.empty() || indices.empty() || commands.empty() || targetWidth == 0U || targetHeight == 0U) {
        return;
    }

    EnsureBuffers(vertices.size(), indices.size());
    if (m_vertexBuffer == Render::kInvalidBufferHandleUVE || m_indexBuffer == Render::kInvalidBufferHandleUVE) {
        return;
    }

    // Upload vertices
    const std::span<const PrismVertexUVE> vSpan(vertices.data(), vertices.size());
    m_renderDevice.UpdateBufferUVE(m_vertexBuffer, std::as_bytes(vSpan), 0U);

    // Upload indices
    const std::span<const std::uint32_t> iSpan(indices.data(), indices.size());
    m_renderDevice.UpdateBufferUVE(m_indexBuffer, std::as_bytes(iSpan), 0U);

    // Orthographic projection matrix: maps (0,0) at Top-Left to (targetWidth, targetHeight) at Bottom-Right
    const float w = static_cast<float>(targetWidth);
    const float h = static_cast<float>(targetHeight);
    Math::Matrix4x4UVE ortho{};
    ortho.m[0][0] = 2.0F / w;
    ortho.m[0][1] = 0.0F;
    ortho.m[0][2] = 0.0F;
    ortho.m[0][3] = 0.0F;

    ortho.m[1][0] = 0.0F;
    ortho.m[1][1] = -2.0F / h; // Y-flip for screen coordinates
    ortho.m[1][2] = 0.0F;
    ortho.m[1][3] = 0.0F;

    ortho.m[2][0] = 0.0F;
    ortho.m[2][1] = 0.0F;
    ortho.m[2][2] = -1.0F;
    ortho.m[2][3] = 0.0F;

    ortho.m[3][0] = -1.0F;
    ortho.m[3][1] = 1.0F;
    ortho.m[3][2] = 0.0F;
    ortho.m[3][3] = 1.0F;

    if (m_pipeline != Render::kInvalidPipelineHandleUVE) {
        commandBuffer.BindPipelineUVE(m_pipeline);
    }
    commandBuffer.SetUniformMatrix4x4UVE("uProjection", ortho);
    commandBuffer.BindVertexBufferUVE(m_vertexBuffer, 0U);
    commandBuffer.BindIndexBufferUVE(m_indexBuffer);

    m_lastDiagnostics.vertexCount = static_cast<std::uint32_t>(vertices.size());
    m_lastDiagnostics.indexCount = static_cast<std::uint32_t>(indices.size());

    for (const auto& cmd : commands) {
        if (cmd.indexCount == 0U) {
            continue;
        }

        // Texture bind: use custom texture if provided, otherwise default white
        Render::TextureHandleUVE texToBind = m_whiteTexture;
        if (cmd.textureId != 0U) {
            texToBind = Render::TextureHandleUVE{static_cast<std::uint32_t>(cmd.textureId)};
        }

        if (texToBind != Render::kInvalidTextureHandleUVE) {
            commandBuffer.BindTextureUVE(texToBind, 0U);
        }

        commandBuffer.DrawIndexedUVE(cmd.indexCount, 1U, cmd.indexOffset);
        m_lastDiagnostics.drawCallsRecorded++;
    }
}

} // namespace UVE::UI::Prism
