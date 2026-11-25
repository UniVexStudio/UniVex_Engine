// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <memory>

#include "i_file_system.h"
#include "i_event_system.h"
#include "i_render_device.h"
#include "i_shader_manager.h"
#include "shader_manager_config.h"
#include "i_thread_pool.h"

namespace UVE::Render::Shader {

/// The concrete, backend-agnostic IShaderManager implementation (Increment 21) — see
/// IShaderManager's doc comment for the full architecture rationale. Public (like
/// GlRenderDevice/RenderSystem/Renderer3D) because EngineCore constructs it directly
/// by concrete type.
class ShaderManager final : public IShaderManager {
public:
    /// `threadPool`/`eventSystem`/`renderDevice`/`fileSystem` must all outlive this
    /// ShaderManager. All renderDevice operations are made on the main thread; asynchronous
    /// compile/link runs inside Update(), while synchronous on-demand mode runs it in Create*().
    ShaderManager(Threading::IThreadPool& threadPool, Events::IEventSystem& eventSystem,
                      IRenderDevice& renderDevice, Asset::IFileSystem& fileSystem,
                      ShaderManagerConfig config);
    ~ShaderManager() override;

    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    [[nodiscard]] std::shared_ptr<ShaderSource> CreateSource(const ShaderSourceCompileDesc& desc) override;
    [[nodiscard]] std::shared_ptr<ShaderProgram> CreateProgram(const ShaderProgramDesc& desc) override;
    [[nodiscard]] std::shared_ptr<ShaderProgram> CreateProgramFromStages(
        const ShaderProgramStagesDesc& desc) override;
    void Update(double deltaTimeSeconds) override;

    /// Test-only hook (not part of IShaderManager): how many source-preparation jobs/results
    /// are pending processing by the manager (submitted or completed but not yet drained).
    [[nodiscard]] std::size_t GetPendingJobCount() const noexcept;

    /// Test-only hook: whether the most recently completed CreateProgram()'s GL compile
    /// resolved via the on-disk program-binary cache (true) or a full from-source compile
    /// (false) — lets a test assert the cache-hit path was actually taken without inspecting GL
    /// state directly.
    [[nodiscard]] bool GetLastCompileUsedCache() const noexcept;

    /// Not part of the public API surface - only nameable from shader_manager_uve.cpp, where it's
    /// fully defined. Declared accessible (rather than private) purely so that translation unit's
    /// own free helper functions (which aren't members/friends of ShaderManager) can name this
    /// type and its nested job/tracking record types; every other translation unit only ever sees
    /// the forward declaration below and can do nothing with it regardless.
    struct Impl;

private:
    // Every method below is implemented entirely in shader_manager_uve.cpp and exists purely so
    // its body can access ShaderSource/ShaderProgram's private fields (both `friend class
    // ShaderManager` — a grant only members of this class inherit, not free functions in the
    // same translation unit). Each signature deliberately never names an Impl nested type
    // (only `Impl&` itself, plus already-public types) so it stays declarable here even though
    // Impl itself is only forward-declared at this point.
    [[nodiscard]] static std::shared_ptr<ShaderSource> MakeSource(IRenderDevice& renderDevice,
                                                                         ShaderStage stage);
    [[nodiscard]] static std::shared_ptr<ShaderProgram> MakeProgram(IRenderDevice& renderDevice);
    static void SubmitSourceCompileJob(Impl& impl, const std::shared_ptr<ShaderSource>& target,
                                           const ShaderSourceCompileDesc& desc);
    static void DrainCompletedSourceJobs(Impl& impl);
    static void ApplyPendingProgramLinks(Impl& impl);
    static void PollHotReload(Impl& impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Render::Shader
