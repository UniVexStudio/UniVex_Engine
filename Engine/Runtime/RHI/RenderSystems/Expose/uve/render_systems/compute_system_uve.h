// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "uve/rhi/i_render_device_uve.h"
#include "uve/render_systems/i_compute_system_uve.h"

namespace UVE::Render {

/// ComputeSystemUVE is the concrete, engine-standard implementation of IComputeSystemUVE. Takes
/// its IRenderDeviceUVE& by dependency injection (same shape as RenderSystemUVE) - it never
/// constructs or owns a render device itself, so tests and future backend swaps pass in whichever
/// IRenderDeviceUVE& they need. It DOES own every program it created: the destructor destroys all
/// still-live programs through the device, which means the device must outlive the system - the
/// same ordering rule every injected-dependency system in this engine already follows.
class ComputeSystemUVE final : public IComputeSystemUVE {
public:
    explicit ComputeSystemUVE(IRenderDeviceUVE& renderDevice) noexcept;
    ~ComputeSystemUVE() override;

    ComputeSystemUVE(const ComputeSystemUVE&) = delete;
    ComputeSystemUVE& operator=(const ComputeSystemUVE&) = delete;

    [[nodiscard]] PipelineHandleUVE CreateProgramUVE(const ComputeProgramDescUVE& desc,
                                                     std::string* outInfoLog = nullptr) override;
    void DestroyProgramUVE(PipelineHandleUVE program) override;
    [[nodiscard]] bool IsProgramLiveUVE(PipelineHandleUVE program) const noexcept override;
    [[nodiscard]] bool EnqueueDispatchUVE(const ComputeDispatchDescUVE& desc) override;
    std::size_t ExecuteQueuedDispatchesUVE(ICommandBufferUVE& commands) override;
    [[nodiscard]] std::size_t GetQueuedDispatchCountUVE() const noexcept override;
    void ClearQueueUVE() override;
    [[nodiscard]] const ComputeSystemDiagnosticsUVE& GetDiagnosticsUVE() const noexcept override;

private:
    /// What the system remembers about a program it owns. The RHI keeps the GPU-side object; this
    /// record exists so warnings and diagnostics can name the kernel instead of a bare handle.
    struct ProgramRecordUVE {
        std::string debugName;
    };

    IRenderDeviceUVE& m_device;
    std::unordered_map<PipelineHandleUVE, ProgramRecordUVE> m_programs;
    std::vector<ComputeDispatchDescUVE> m_queue;
    ComputeSystemDiagnosticsUVE m_diagnostics;
};

} // namespace UVE::Render
