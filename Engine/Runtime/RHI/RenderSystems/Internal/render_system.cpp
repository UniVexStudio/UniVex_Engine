// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "render_system.h"

#include <exception>
#include <utility>

#include "engine_assert.h"
#include "logging_macros.h"

namespace UVE::Render {

struct RenderSystem::Impl {
    IRenderDevice& renderDevice;
    std::unique_ptr<ICommandBuffer> currentFrameCommandBuffer;
    std::unique_ptr<ICommandBuffer> invalidAccessCommandBuffer;
    std::uint64_t frameIndex = 0;
    bool frameActive = false;
};

RenderSystem::RenderSystem(IRenderDevice& renderDevice) : m_impl(std::make_unique<Impl>(renderDevice)) {}

RenderSystem::~RenderSystem() = default;

void RenderSystem::BeginFrame() {
    UVE_ASSERT(!m_impl->frameActive);
    if (m_impl->frameActive) {
        UVE_ERROR("RenderSystem: BeginFrame called while a frame is already active");
        return;
    }
    std::unique_ptr<ICommandBuffer> commandBuffer = m_impl->renderDevice.CreateCommandBuffer();
    if (commandBuffer == nullptr) {
        UVE_ERROR("RenderSystem: CreateCommandBuffer returned null");
        return;
    }
    m_impl->currentFrameCommandBuffer = std::move(commandBuffer);
    m_impl->frameActive = true;
}

ICommandBuffer& RenderSystem::GetFrameCommandBuffer() {
    UVE_ASSERT(m_impl->frameActive);
    if (m_impl->frameActive && m_impl->currentFrameCommandBuffer != nullptr) {
        return *m_impl->currentFrameCommandBuffer;
    }
    UVE_ERROR("RenderSystem: GetFrameCommandBuffer called without an active frame");
    if (m_impl->invalidAccessCommandBuffer == nullptr) {
        m_impl->invalidAccessCommandBuffer = m_impl->renderDevice.CreateCommandBuffer();
    }
    UVE_ASSERT(m_impl->invalidAccessCommandBuffer != nullptr);
    if (m_impl->invalidAccessCommandBuffer == nullptr) {
        UVE_FATAL("RenderSystem: unable to create a safe invalid-access command buffer");
        std::terminate();
    }
    return *m_impl->invalidAccessCommandBuffer;
}

void RenderSystem::EndFrame() {
    UVE_ASSERT(m_impl->frameActive);
    if (!m_impl->frameActive || m_impl->currentFrameCommandBuffer == nullptr) {
        UVE_ERROR("RenderSystem: EndFrame called without an active frame");
        return;
    }
    m_impl->renderDevice.Submit(std::move(m_impl->currentFrameCommandBuffer));
    ++m_impl->frameIndex;
    m_impl->frameActive = false;
}

std::uint64_t RenderSystem::GetFrameIndex() const noexcept {
    return m_impl->frameIndex;
}

} // namespace UVE::Render
