// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <memory>

#include "i_render_device.h"
#include "i_render_system.h"

namespace UVE::Render {

/// RenderSystem is the concrete, engine-standard implementation of IRenderSystem. Takes its
/// IRenderDevice& by dependency injection (same shape as
/// `FileSystem(IAssetBundle&)`/`AssetManager(IThreadPool&, IEventSystem&, ...)`) —
/// it never constructs or owns a render device itself, so tests and future backend swaps can pass
/// in whichever IRenderDevice& they need.
class RenderSystem final : public IRenderSystem {
public:
    explicit RenderSystem(IRenderDevice& renderDevice);
    ~RenderSystem() override;

    RenderSystem(const RenderSystem&) = delete;
    RenderSystem& operator=(const RenderSystem&) = delete;

    void BeginFrame() override;
    [[nodiscard]] ICommandBuffer& GetFrameCommandBuffer() override;
    void EndFrame() override;
    [[nodiscard]] std::uint64_t GetFrameIndex() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Render
