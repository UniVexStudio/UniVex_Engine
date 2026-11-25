// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "i_render_device.h"
#include "i_compute_system.h"

namespace UVE::Render {

/// ComputeSystem is the concrete, engine-standard implementation of IComputeSystem. Takes
/// its IRenderDevice& by dependency injection (same shape as RenderSystem) - it never
/// constructs or owns a render device itself, so tests and future backend swaps pass in whichever
/// IRenderDevice& they need. It DOES own every program it created: the destructor destroys all
/// still-live programs through the device, which means the device must outlive the system - the
/// same ordering rule every injected-dependency system in this engine already follows.
class ComputeSystem final : public IComputeSystem {
public:
    explicit ComputeSystem(IRenderDevice& renderDevice) noexcept;
    ~ComputeSystem() override;

    ComputeSystem(const ComputeSystem&) = delete;
    ComputeSystem& operator=(const ComputeSystem&) = delete;

    [[nodiscard]] PipelineHandle CreateProgram(const ComputeProgramDesc& desc,
                                                     std::string* outInfoLog = nullptr) override;
    void DestroyProgram(PipelineHandle program) override;
    [[nodiscard]] bool IsProgramLive(PipelineHandle program) const noexcept override;
    [[nodiscard]] bool EnqueueDispatch(const ComputeDispatchDesc& desc) override;
    std::size_t ExecuteQueuedDispatches(ICommandBuffer& commands) override;
    [[nodiscard]] std::size_t GetQueuedDispatchCount() const noexcept override;
    void ClearQueue() override;
    [[nodiscard]] const ComputeSystemDiagnostics& GetDiagnostics() const noexcept override;

private:
    /// What the system remembers about a program it owns. The RHI keeps the GPU-side object; this
    /// record exists so warnings and diagnostics can name the kernel instead of a bare handle.
    struct ProgramRecord {
        std::string debugName;
    };

    IRenderDevice& m_device;
    std::unordered_map<PipelineHandle, ProgramRecord> m_programs;
    std::vector<ComputeDispatchDesc> m_queue;
    ComputeSystemDiagnostics m_diagnostics;
};

} // namespace UVE::Render
