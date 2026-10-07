// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <memory>

#include "uve/rhi_shader/shader_program_desc_uve.h"
#include "uve/rhi_shader/shader_program_stages_desc_uve.h"
#include "uve/rhi_shader/shader_program_uve.h"
#include "uve/rhi_shader/shader_source_compile_desc_uve.h"
#include "uve/rhi_shader/shader_source_uve.h"

namespace UVE::Render::Shader {

/// IShaderManagerUVE is the backend-agnostic interface for runtime shader loading, `#include`
/// resolution, conditional-compilation preprocessing, hot-reload, uniform reflection, and
/// on-disk program-binary caching (Increment 21). Built entirely on top of IRenderDeviceUVE — it
/// never talks to any GL type directly, reusing IRenderDeviceUVE's own
/// CreateShaderUVE/CreatePipelineUVE compile+link+error-capture logic instead of duplicating it —
/// so it works identically against NullRenderDeviceUVE (headless) and GlRenderDeviceUVE (real
/// GL), with no separate Null/Gl ShaderManager implementation needed.
/// Thread-safety: Call creation and update methods from the main/render thread. In asynchronous
/// on-demand mode, source preprocessing runs on an IThreadPoolUVE worker and UpdateUVE() performs
/// RHI compile/link. In synchronous on-demand mode, each creation call preprocesses and performs
/// RHI compile/link before returning (and may block); this still honors IRenderDeviceUVE's
/// main-thread-only contract. UpdateUVE() must be called once per frame for asynchronous job
/// completion and hot-reload polling.
class IShaderManagerUVE {
public:
    virtual ~IShaderManagerUVE() = default;

    /// Starts compiling one shader stage. In asynchronous on-demand mode, returns with a
    /// not-yet-ready ShaderSourceUVE; poll IsReadyUVE()/IsValidUVE() or subscribe to hot-reload
    /// events to observe completion. In synchronous on-demand mode, completes before returning.
    [[nodiscard]] virtual std::shared_ptr<ShaderSourceUVE> CreateSourceUVE(
        const ShaderSourceCompileDescUVE& desc) = 0;

    /// Compiles and links a vertex+fragment program from the same resolved source. Its readiness
    /// on return follows the configured asynchronous or synchronous on-demand mode.
    [[nodiscard]] virtual std::shared_ptr<ShaderProgramUVE> CreateProgramUVE(const ShaderProgramDescUVE& desc) = 0;

    /// Begins compiling and linking a vertex+fragment program from two independent source
    /// descriptors. This is the managed path for MaterialAssetUVE's separate vertexShader and
    /// fragmentShader assets; it has the same configured readiness mode and program-level
    /// hot-reload contract as CreateProgramUVE().
    [[nodiscard]] virtual std::shared_ptr<ShaderProgramUVE> CreateProgramFromStagesUVE(
        const ShaderProgramStagesDescUVE& desc) = 0;

    /// Drains completed background preprocessing in asynchronous mode, running RHI compile/link
    /// and cache lookup on the calling thread. If hot-reload is enabled, also polls every tracked
    /// program's dependency closure for on-disk changes and schedules a recompile when one is
    /// found. Call exactly once per frame from the main thread.
    virtual void UpdateUVE(double deltaTimeSeconds) = 0;
};

} // namespace UVE::Render::Shader
