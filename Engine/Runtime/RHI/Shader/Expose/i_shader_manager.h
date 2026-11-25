// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <memory>

#include "shader_program_desc.h"
#include "shader_program_stages_desc.h"
#include "shader_program.h"
#include "shader_source_compile_desc.h"
#include "shader_source.h"

namespace UVE::Render::Shader {

/// IShaderManager is the backend-agnostic interface for runtime shader loading, `#include`
/// resolution, conditional-compilation preprocessing, hot-reload, uniform reflection, and
/// on-disk program-binary caching (Increment 21). Built entirely on top of IRenderDevice — it
/// never talks to any GL type directly, reusing IRenderDevice's own
/// CreateShader/CreatePipeline compile+link+error-capture logic instead of duplicating it —
/// so it works identically against NullRenderDevice (headless) and GlRenderDevice (real
/// GL), with no separate Null/Gl ShaderManager implementation needed.
/// Thread-safety: Call creation and update methods from the main/render thread. In asynchronous
/// on-demand mode, source preprocessing runs on an IThreadPool worker and Update() performs
/// RHI compile/link. In synchronous on-demand mode, each creation call preprocesses and performs
/// RHI compile/link before returning (and may block); this still honors IRenderDevice's
/// main-thread-only contract. Update() must be called once per frame for asynchronous job
/// completion and hot-reload polling.
class IShaderManager {
public:
    virtual ~IShaderManager() = default;

    /// Starts compiling one shader stage. In asynchronous on-demand mode, returns with a
    /// not-yet-ready ShaderSource; poll IsReady()/IsValid() or subscribe to hot-reload
    /// events to observe completion. In synchronous on-demand mode, completes before returning.
    [[nodiscard]] virtual std::shared_ptr<ShaderSource> CreateSource(
        const ShaderSourceCompileDesc& desc) = 0;

    /// Compiles and links a vertex+fragment program from the same resolved source. Its readiness
    /// on return follows the configured asynchronous or synchronous on-demand mode.
    [[nodiscard]] virtual std::shared_ptr<ShaderProgram> CreateProgram(const ShaderProgramDesc& desc) = 0;

    /// Begins compiling and linking a vertex+fragment program from two independent source
    /// descriptors. This is the managed path for MaterialAsset's separate vertexShader and
    /// fragmentShader assets; it has the same configured readiness mode and program-level
    /// hot-reload contract as CreateProgram().
    [[nodiscard]] virtual std::shared_ptr<ShaderProgram> CreateProgramFromStages(
        const ShaderProgramStagesDesc& desc) = 0;

    /// Drains completed background preprocessing in asynchronous mode, running RHI compile/link
    /// and cache lookup on the calling thread. If hot-reload is enabled, also polls every tracked
    /// program's dependency closure for on-disk changes and schedules a recompile when one is
    /// found. Call exactly once per frame from the main thread.
    virtual void Update(double deltaTimeSeconds) = 0;
};

} // namespace UVE::Render::Shader
