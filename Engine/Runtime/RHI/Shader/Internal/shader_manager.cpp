// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "shader_manager.h"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "shader_binary_cache.h"
#include "shader_diagnostics_parser.h"
#include "shader_preprocessor.h"
#include "logging_macros.h"
#include "shader_events.h"
#include "job_counter.h"
#include "hash.h"

namespace UVE::Render::Shader {

struct ShaderManager::Impl {
    Threading::IThreadPool& threadPool;
    Events::IEventSystem& eventSystem;
    IRenderDevice& renderDevice;
    Asset::IFileSystem& fileSystem;
    ShaderManagerConfig config;

    Threading::JobCounter pendingJobs;
    mutable std::mutex mutex;
    std::size_t pendingJobCount = 0;

    struct SourceJob {
        std::shared_ptr<ShaderSource> target;
        ShaderSourceCompileDesc desc;
        Detail::PreprocessResult preprocess;
    };
    std::vector<SourceJob> completedSourceJobs; // guarded by mutex - written from worker threads

    /// Internal normalized representation shared by the legacy unified-source API and the
    /// Increment 34 separate-stage API. Source hot-reload flags are disabled here because linked
    /// program tracking owns the union of both stage dependency closures.
    struct ProgramRequestDesc {
        ShaderSourceCompileDesc vertexSource;
        ShaderSourceCompileDesc fragmentSource;
        std::vector<VertexAttribute> vertexLayout;
        std::uint32_t vertexStride = 0;
        PrimitiveTopology topology = PrimitiveTopology::Triangles;
        bool depthTestEnabled = true;
        bool depthWriteEnabled = true;
        PipelineBlendMode blendMode = PipelineBlendMode::Opaque;
        CullMode cullMode = CullMode::None;
        FrontFace frontFace = FrontFace::CounterClockwise;
        FillMode fillMode = FillMode::Fill;
        bool depthBiasEnabled = false;
        float depthBiasConstantFactor = 0.0F;
        float depthBiasSlopeFactor = 0.0F;
        DepthCompare depthCompare = DepthCompare::Less;
        bool hotReloadEnabled = true;
        std::string debugName;
    };

    struct PendingProgramLink {
        std::shared_ptr<ShaderProgram> program;
        std::shared_ptr<ShaderSource> vertexSource;
        std::shared_ptr<ShaderSource> fragmentSource;
        ProgramRequestDesc desc;
    };
    std::vector<PendingProgramLink> pendingProgramLinks; // main-thread only

    struct TrackedDependencies {
        std::vector<std::string> dependencyClosure;
        std::unordered_map<std::string, std::filesystem::file_time_type> lastKnownWriteTimes;
    };
    struct TrackedSource {
        std::weak_ptr<ShaderSource> target;
        ShaderSourceCompileDesc desc;
        TrackedDependencies dependencies;
    };
    struct TrackedProgram {
        std::weak_ptr<ShaderProgram> target;
        ProgramRequestDesc desc;
        TrackedDependencies dependencies;
    };
    std::vector<TrackedSource> trackedSources;   // main-thread only
    std::vector<TrackedProgram> trackedPrograms; // main-thread only

    double hotReloadAccumulatorSeconds = 0.0;
    bool lastCompileUsedCache = false;

    Impl(Threading::IThreadPool& threadPoolIn, Events::IEventSystem& eventSystemIn,
            IRenderDevice& renderDeviceIn, Asset::IFileSystem& fileSystemIn, ShaderManagerConfig configIn)
        : threadPool(threadPoolIn),
          eventSystem(eventSystemIn),
          renderDevice(renderDeviceIn),
          fileSystem(fileSystemIn),
          config(std::move(configIn)) {}
};

namespace {

[[nodiscard]] const char* ShaderStageDefineName(ShaderStage stage) noexcept {
    switch (stage) {
        case ShaderStage::Vertex:
            return "VERTEX_SHADER";
        case ShaderStage::Fragment:
            return "FRAGMENT_SHADER";
        case ShaderStage::Compute:
            return "COMPUTE_SHADER";
        case ShaderStage::Geometry:
            return "GEOMETRY_SHADER";
    }
    return "";
}

[[nodiscard]] std::vector<std::pair<std::string, std::string>> BuildDefines(
    ShaderStage stage, bool injectDebugDefine, const std::vector<std::pair<std::string, std::string>>& extraDefines) {
    std::vector<std::pair<std::string, std::string>> defines;
    defines.reserve(extraDefines.size() + 4);
    defines.emplace_back("UVE_DEBUG", injectDebugDefine ? "1" : "0");
#if defined(__ANDROID__)
    defines.emplace_back("UVE_MOBILE", "1");
#else
    defines.emplace_back("UVE_MOBILE", "0");
#endif
    defines.emplace_back("UVE_BACKEND_GL", "1"); // Reserved for a future non-GL backend to define its own instead.
    defines.emplace_back(ShaderStageDefineName(stage), "1");
    defines.insert(defines.end(), extraDefines.begin(), extraDefines.end());
    return defines;
}

[[nodiscard]] std::filesystem::file_time_type GetRealFileWriteTime(Asset::IFileSystem& fileSystem,
                                                                       const std::string& virtualPath) {
    const std::filesystem::path realPath = fileSystem.ResolveRealPath(virtualPath);
    std::error_code errorCode;
    const auto writeTime = std::filesystem::last_write_time(realPath, errorCode);
    return errorCode ? std::filesystem::file_time_type{} : writeTime;
}

[[nodiscard]] std::uint64_t ComputeSourceContentHash(IRenderDevice& renderDevice, const std::string& resolvedSource,
                                                         ShaderStage stage, const std::string& entryPoint) {
    std::string combined = resolvedSource;
    combined += '|';
    combined += ShaderStageDefineName(stage);
    combined += '|';
    combined += entryPoint;
    combined += '|';
    combined += std::string(renderDevice.GetBackendName());
    return Utilities::HashString(combined);
}

[[nodiscard]] std::uint64_t ComputeProgramContentHash(const ShaderSource& vertexSource,
                                                          const ShaderSource& fragmentSource) {
    const std::string combined =
        std::to_string(vertexSource.GetContentHash()) + "|" + std::to_string(fragmentSource.GetContentHash());
    return Utilities::HashString(combined);
}

[[nodiscard]] ShaderSourceCompileDesc NormalizeProgramStageDesc(ShaderSourceCompileDesc desc,
                                                                        ShaderStage stage,
                                                                        const std::string& programDebugName) {
    desc.stage = stage;
    // Dependency tracking happens at the linked-program level (the union of both stage closures),
    // so individual child sources never create competing tracking entries.
    desc.hotReloadEnabled = false;
    if (desc.debugName.empty()) {
        desc.debugName = programDebugName;
    }
    desc.debugName += stage == ShaderStage::Vertex ? " (vertex)" : " (fragment)";
    return desc;
}

[[nodiscard]] ShaderManager::Impl::ProgramRequestDesc BuildProgramRequestDesc(
    const ShaderProgramDesc& desc) {
    ShaderManager::Impl::ProgramRequestDesc request;
    request.debugName = desc.debugName;
    request.vertexSource = NormalizeProgramStageDesc(
        ShaderSourceCompileDesc{ShaderStage::Vertex, desc.virtualFilePath, desc.embeddedFallbackSourceCode,
                                   desc.extraDefines, desc.entryPointName, false, {}},
        ShaderStage::Vertex, request.debugName);
    request.fragmentSource = NormalizeProgramStageDesc(
        ShaderSourceCompileDesc{ShaderStage::Fragment, desc.virtualFilePath, desc.embeddedFallbackSourceCode,
                                   desc.extraDefines, desc.entryPointName, false, {}},
        ShaderStage::Fragment, request.debugName);
    request.vertexLayout = desc.vertexLayout;
    request.vertexStride = desc.vertexStride;
    request.topology = desc.topology;
    request.depthTestEnabled = desc.depthTestEnabled;
    request.depthWriteEnabled = desc.depthWriteEnabled;
    request.blendMode = desc.blendMode;
    request.cullMode = desc.cullMode;
    request.frontFace = desc.frontFace;
    request.fillMode = desc.fillMode;
    request.depthBiasEnabled = desc.depthBiasEnabled;
    request.depthBiasConstantFactor = desc.depthBiasConstantFactor;
    request.depthBiasSlopeFactor = desc.depthBiasSlopeFactor;
    request.depthCompare = desc.depthCompare;
    request.hotReloadEnabled = desc.hotReloadEnabled;
    return request;
}

[[nodiscard]] ShaderManager::Impl::ProgramRequestDesc BuildProgramRequestDesc(
    const ShaderProgramStagesDesc& desc) {
    ShaderManager::Impl::ProgramRequestDesc request;
    request.debugName = desc.debugName;
    request.vertexSource = NormalizeProgramStageDesc(desc.vertexSource, ShaderStage::Vertex, request.debugName);
    request.fragmentSource =
        NormalizeProgramStageDesc(desc.fragmentSource, ShaderStage::Fragment, request.debugName);
    request.vertexLayout = desc.vertexLayout;
    request.vertexStride = desc.vertexStride;
    request.topology = desc.topology;
    request.depthTestEnabled = desc.depthTestEnabled;
    request.depthWriteEnabled = desc.depthWriteEnabled;
    request.blendMode = desc.blendMode;
    request.cullMode = desc.cullMode;
    request.frontFace = desc.frontFace;
    request.fillMode = desc.fillMode;
    request.depthBiasEnabled = desc.depthBiasEnabled;
    request.depthBiasConstantFactor = desc.depthBiasConstantFactor;
    request.depthBiasSlopeFactor = desc.depthBiasSlopeFactor;
    request.depthCompare = desc.depthCompare;
    request.hotReloadEnabled = desc.hotReloadEnabled;
    return request;
}

} // namespace

std::shared_ptr<ShaderSource> ShaderManager::MakeSource(IRenderDevice& renderDevice, ShaderStage stage) {
    auto* const renderDevicePtr = &renderDevice;
    std::shared_ptr<ShaderSource> source(new ShaderSource(), [renderDevicePtr](ShaderSource* pointer) {
        if (pointer->m_valid) {
            renderDevicePtr->DestroyShader(pointer->m_handle);
        }
        delete pointer;
    });
    source->m_stage = stage;
    return source;
}

std::shared_ptr<ShaderProgram> ShaderManager::MakeProgram(IRenderDevice& renderDevice) {
    auto* const renderDevicePtr = &renderDevice;
    std::shared_ptr<ShaderProgram> program(new ShaderProgram(), [renderDevicePtr](ShaderProgram* pointer) {
        if (pointer->m_valid) {
            renderDevicePtr->DestroyPipeline(pointer->m_pipeline);
        }
        delete pointer;
    });
    return program;
}

void ShaderManager::SubmitSourceCompileJob(Impl& impl, const std::shared_ptr<ShaderSource>& target,
                                                  const ShaderSourceCompileDesc& desc) {
    if (impl.config.compileSynchronously) {
        const std::vector<std::pair<std::string, std::string>> defines =
            BuildDefines(desc.stage, impl.config.injectDebugDefine, desc.extraDefines);
        Detail::PreprocessResult preprocess = Detail::PreprocessShaderSource(
            impl.fileSystem, desc.virtualFilePath, desc.embeddedFallbackSourceCode, defines);
        std::lock_guard<std::mutex> lock(impl.mutex);
        ++impl.pendingJobCount;
        impl.completedSourceJobs.push_back(
            Impl::SourceJob{target, desc, std::move(preprocess)});
        return;
    }

    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        ++impl.pendingJobCount;
    }
    impl.threadPool.Submit(
        // Init-captures: a plain capture of these const& parameters would be const, and the
        // moves below would quietly copy.
        [&impl, target = target, desc = desc]() mutable {
            const std::vector<std::pair<std::string, std::string>> defines =
                BuildDefines(desc.stage, impl.config.injectDebugDefine, desc.extraDefines);
            Detail::PreprocessResult preprocess = Detail::PreprocessShaderSource(
                impl.fileSystem, desc.virtualFilePath, desc.embeddedFallbackSourceCode, defines);

            // Hand the target over rather than copying it. A copy left in this job's captures
            // can be the last reference once the main thread has drained and linked, and the
            // worker then runs the shader's deleter itself: off the render thread, at whatever
            // moment the pool gets round to destroying the job.
            std::lock_guard<std::mutex> lock(impl.mutex);
            impl.completedSourceJobs.push_back(
                Impl::SourceJob{std::move(target), std::move(desc), std::move(preprocess)});
        },
        impl.pendingJobs);
}

void ShaderManager::DrainCompletedSourceJobs(Impl& impl) {
    std::vector<Impl::SourceJob> completedJobs;
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        completedJobs.swap(impl.completedSourceJobs);
        impl.pendingJobCount -= completedJobs.size();
    }

    for (auto& job : completedJobs) {
        ShaderSource& source = *job.target;

        if (!job.preprocess.success) {
            source.m_ready = true;
            source.m_valid = false; // A failed hot-reload preprocess leaves any prior valid handle untouched.
            source.m_diagnostics = ShaderCompileDiagnostics{false, {}, job.preprocess.errorMessage};
            impl.eventSystem.QueueEvent(
                ShaderCompileFailedEvent{job.desc.debugName, job.desc.stage, source.m_diagnostics});
        } else {
            std::string infoLog;
            const ShaderHandle newHandle = impl.renderDevice.CreateShader(
                ShaderDesc{job.desc.stage, job.preprocess.resolvedSource, job.desc.entryPointName}, &infoLog);
            const bool succeeded = (newHandle != kInvalidShaderHandle);
            const std::vector<ShaderCompileError> diagnosticsList =
                Detail::ParseGlInfoLog(infoLog, job.preprocess.fileIndexTable);

            if (succeeded) {
                if (source.m_valid) {
                    // Hot-reload swap: the new shader compiled successfully, so it's safe to
                    // destroy the old one now - never before a replacement is confirmed live.
                    impl.renderDevice.DestroyShader(source.m_handle);
                }
                source.m_handle = newHandle;
                source.m_resolvedSource = job.preprocess.resolvedSource;
                source.m_contentHash = ComputeSourceContentHash(impl.renderDevice, job.preprocess.resolvedSource,
                                                                    job.desc.stage, job.desc.entryPointName);
                source.m_dependencyClosure = job.preprocess.dependencyClosure;
                source.m_valid = true;
            }
            // else: leave the prior handle/valid/resolvedSource untouched - a program using this
            // source keeps rendering its last-known-good state rather than going dark on a typo.

            source.m_ready = true;
            source.m_diagnostics = ShaderCompileDiagnostics{succeeded, diagnosticsList, infoLog};

            if (!succeeded) {
                impl.eventSystem.QueueEvent(
                    ShaderCompileFailedEvent{job.desc.debugName, job.desc.stage, source.m_diagnostics});
            }
        }

        if (job.desc.hotReloadEnabled && !job.preprocess.dependencyClosure.empty()) {
            bool found = false;
            for (auto& tracked : impl.trackedSources) {
                if (tracked.target.lock() == job.target) {
                    tracked.desc = job.desc;
                    tracked.dependencies.dependencyClosure = job.preprocess.dependencyClosure;
                    for (const std::string& path : job.preprocess.dependencyClosure) {
                        if (!tracked.dependencies.lastKnownWriteTimes.contains(path)) {
                            tracked.dependencies.lastKnownWriteTimes[path] =
                                GetRealFileWriteTime(impl.fileSystem, path);
                        }
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                Impl::TrackedSource entry;
                entry.target = job.target;
                entry.desc = job.desc;
                entry.dependencies.dependencyClosure = job.preprocess.dependencyClosure;
                for (const std::string& path : job.preprocess.dependencyClosure) {
                    entry.dependencies.lastKnownWriteTimes[path] = GetRealFileWriteTime(impl.fileSystem, path);
                }
                impl.trackedSources.push_back(std::move(entry));
            }
        }
    }
}

void ShaderManager::ApplyPendingProgramLinks(Impl& impl) {
    std::vector<Impl::PendingProgramLink> stillPendingLinks;
    for (auto& pending : impl.pendingProgramLinks) {
        if (!(pending.vertexSource->IsReady() && pending.fragmentSource->IsReady())) {
            stillPendingLinks.push_back(std::move(pending));
            continue;
        }

        ShaderProgram& program = *pending.program;
        const bool stagesValid = pending.vertexSource->IsValid() && pending.fragmentSource->IsValid();

        bool linkSucceeded = false;
        std::vector<ShaderCompileError> diagnosticsList;
        std::string infoLog;

        if (stagesValid) {
            const std::uint64_t programHash =
                ComputeProgramContentHash(*pending.vertexSource, *pending.fragmentSource);
            const std::filesystem::path cacheFilePath =
                Detail::GetCacheFilePath(impl.config.cachePath, programHash);
            PipelineBinaryDesc binaryDesc;
            binaryDesc.vertexLayout = pending.desc.vertexLayout;
            binaryDesc.vertexStride = pending.desc.vertexStride;
            binaryDesc.topology = pending.desc.topology;
            binaryDesc.depthTestEnabled = pending.desc.depthTestEnabled;
            binaryDesc.depthWriteEnabled = pending.desc.depthWriteEnabled;
            binaryDesc.blendMode = pending.desc.blendMode;
            binaryDesc.cullMode = pending.desc.cullMode;
            binaryDesc.frontFace = pending.desc.frontFace;
            binaryDesc.fillMode = pending.desc.fillMode;
            binaryDesc.depthBiasEnabled = pending.desc.depthBiasEnabled;
            binaryDesc.depthBiasConstantFactor = pending.desc.depthBiasConstantFactor;
            binaryDesc.depthBiasSlopeFactor = pending.desc.depthBiasSlopeFactor;
            binaryDesc.depthCompare = pending.desc.depthCompare;

            PipelineHandle newPipeline = kInvalidPipelineHandle;
            bool usedCache = false;

            const std::optional<Detail::CacheEntry> cacheEntry = Detail::ReadCacheEntry(cacheFilePath);
            if (cacheEntry.has_value()) {
                newPipeline = impl.renderDevice.CreatePipelineFromBinary(
                    cacheEntry->payload, cacheEntry->glBinaryFormat, binaryDesc);
                usedCache = (newPipeline != kInvalidPipelineHandle);
            }

            if (newPipeline == kInvalidPipelineHandle) {
                PipelineDesc pipelineDesc;
                pipelineDesc.vertexShader = pending.vertexSource->GetHandle();
                pipelineDesc.fragmentShader = pending.fragmentSource->GetHandle();
                pipelineDesc.vertexLayout = pending.desc.vertexLayout;
                pipelineDesc.topology = pending.desc.topology;
                pipelineDesc.depthTestEnabled = pending.desc.depthTestEnabled;
                pipelineDesc.depthWriteEnabled = pending.desc.depthWriteEnabled;
                pipelineDesc.blendMode = pending.desc.blendMode;
                pipelineDesc.cullMode = pending.desc.cullMode;
                pipelineDesc.frontFace = pending.desc.frontFace;
                pipelineDesc.fillMode = pending.desc.fillMode;
                pipelineDesc.depthBiasEnabled = pending.desc.depthBiasEnabled;
                pipelineDesc.depthBiasConstantFactor = pending.desc.depthBiasConstantFactor;
                pipelineDesc.depthBiasSlopeFactor = pending.desc.depthBiasSlopeFactor;
                pipelineDesc.depthCompare = pending.desc.depthCompare;
                pipelineDesc.vertexStride = pending.desc.vertexStride;
                newPipeline = impl.renderDevice.CreatePipeline(pipelineDesc, &infoLog);

                if (newPipeline != kInvalidPipelineHandle) {
                    std::vector<std::byte> binary;
                    std::uint32_t binaryFormat = 0;
                    if (impl.renderDevice.GetPipelineBinary(newPipeline, binary, binaryFormat)) {
                        // Best-effort: a cache write failure is never fatal, just logged by
                        // WriteCacheEntry itself - the next startup simply recompiles from
                        // source.
                        static_cast<void>(Detail::WriteCacheEntry(cacheFilePath, binaryFormat, binary));
                    }
                }
            }

            impl.lastCompileUsedCache = usedCache;
            diagnosticsList = Detail::ParseGlInfoLog(infoLog, {});
            linkSucceeded = (newPipeline != kInvalidPipelineHandle);

            if (linkSucceeded) {
                if (program.m_valid) {
                    impl.renderDevice.DestroyPipeline(program.m_pipeline);
                }
                program.m_pipeline = newPipeline;
                program.m_uniforms = impl.renderDevice.GetPipelineUniforms(newPipeline);
                program.m_contentHash = programHash;
                program.m_valid = true;
            }
        } else {
            impl.lastCompileUsedCache = false;
            diagnosticsList = pending.vertexSource->GetDiagnostics().diagnostics;
            const std::vector<ShaderCompileError>& fragmentDiagnostics =
                pending.fragmentSource->GetDiagnostics().diagnostics;
            diagnosticsList.insert(diagnosticsList.end(), fragmentDiagnostics.begin(), fragmentDiagnostics.end());
            infoLog = pending.vertexSource->GetDiagnostics().rawInfoLog +
                      pending.fragmentSource->GetDiagnostics().rawInfoLog;
        }

        program.m_ready = true;
        program.m_diagnostics = ShaderCompileDiagnostics{linkSucceeded, diagnosticsList, infoLog};
        impl.eventSystem.QueueEvent(ShaderProgramReloadedEvent{pending.desc.debugName, linkSucceeded});

        if (pending.desc.hotReloadEnabled) {
            std::vector<std::string> combinedClosure = pending.vertexSource->m_dependencyClosure;
            for (const std::string& path : pending.fragmentSource->m_dependencyClosure) {
                if (std::find(combinedClosure.begin(), combinedClosure.end(), path) == combinedClosure.end()) {
                    combinedClosure.push_back(path);
                }
            }
            if (!combinedClosure.empty()) {
                bool found = false;
                for (auto& tracked : impl.trackedPrograms) {
                    if (tracked.target.lock() == pending.program) {
                        tracked.desc = pending.desc;
                        tracked.dependencies.dependencyClosure = combinedClosure;
                        for (const std::string& path : combinedClosure) {
                            if (!tracked.dependencies.lastKnownWriteTimes.contains(path)) {
                                tracked.dependencies.lastKnownWriteTimes[path] =
                                    GetRealFileWriteTime(impl.fileSystem, path);
                            }
                        }
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    Impl::TrackedProgram entry;
                    entry.target = pending.program;
                    entry.desc = pending.desc;
                    entry.dependencies.dependencyClosure = combinedClosure;
                    for (const std::string& path : combinedClosure) {
                        entry.dependencies.lastKnownWriteTimes[path] = GetRealFileWriteTime(impl.fileSystem, path);
                    }
                    impl.trackedPrograms.push_back(std::move(entry));
                }
            }
        }
    }
    impl.pendingProgramLinks = std::move(stillPendingLinks);
}

void ShaderManager::PollHotReload(Impl& impl) {
    std::vector<Impl::TrackedSource> stillTrackedSources;
    for (auto& tracked : impl.trackedSources) {
        std::shared_ptr<ShaderSource> target = tracked.target.lock();
        if (!target) {
            continue; // Expired - the caller released it; drop the tracking entry.
        }
        bool dirty = false;
        for (const std::string& path : tracked.dependencies.dependencyClosure) {
            const auto currentWriteTime = GetRealFileWriteTime(impl.fileSystem, path);
            std::filesystem::file_time_type& lastKnown = tracked.dependencies.lastKnownWriteTimes[path];
            if (currentWriteTime != std::filesystem::file_time_type{} && currentWriteTime != lastKnown) {
                dirty = true;
            }
            lastKnown = currentWriteTime;
        }
        if (dirty) {
            SubmitSourceCompileJob(impl, target, tracked.desc);
        }
        stillTrackedSources.push_back(std::move(tracked));
    }
    impl.trackedSources = std::move(stillTrackedSources);

    std::vector<Impl::TrackedProgram> stillTrackedPrograms;
    for (auto& tracked : impl.trackedPrograms) {
        std::shared_ptr<ShaderProgram> target = tracked.target.lock();
        if (!target) {
            continue;
        }
        bool dirty = false;
        for (const std::string& path : tracked.dependencies.dependencyClosure) {
            const auto currentWriteTime = GetRealFileWriteTime(impl.fileSystem, path);
            std::filesystem::file_time_type& lastKnown = tracked.dependencies.lastKnownWriteTimes[path];
            if (currentWriteTime != std::filesystem::file_time_type{} && currentWriteTime != lastKnown) {
                dirty = true;
            }
            lastKnown = currentWriteTime;
        }
        if (dirty) {
            std::shared_ptr<ShaderSource> vertexSource = MakeSource(impl.renderDevice, ShaderStage::Vertex);
            std::shared_ptr<ShaderSource> fragmentSource =
                MakeSource(impl.renderDevice, ShaderStage::Fragment);
            SubmitSourceCompileJob(impl, vertexSource, tracked.desc.vertexSource);
            SubmitSourceCompileJob(impl, fragmentSource, tracked.desc.fragmentSource);
            std::lock_guard<std::mutex> lock(impl.mutex);
            impl.pendingProgramLinks.push_back(
                Impl::PendingProgramLink{target, vertexSource, fragmentSource, tracked.desc});
        }
        stillTrackedPrograms.push_back(std::move(tracked));
    }
    impl.trackedPrograms = std::move(stillTrackedPrograms);
}

ShaderManager::ShaderManager(Threading::IThreadPool& threadPool, Events::IEventSystem& eventSystem,
                                    IRenderDevice& renderDevice, Asset::IFileSystem& fileSystem,
                                    ShaderManagerConfig config)
    : m_impl(std::make_unique<Impl>(threadPool, eventSystem, renderDevice, fileSystem, std::move(config))) {}

ShaderManager::~ShaderManager() {
    // Drains every background job still referencing this Impl, which must never outlive it.
    //
    // This also has to hold the shader deleters: MakeSource/MakeProgram capture a raw
    // IRenderDevice*, so a ShaderSource released after the render device is gone calls a
    // virtual function on a destroyed object. Compile jobs capture their target shared_ptr, so
    // the last reference can be the worker's. ThreadPool::RunJob therefore releases a
    // job's captured state before decrementing the counter this waits on - without that
    // ordering, this drain returns while a worker still owns a shader, and the deleter fires
    // into a dead device. An earlier revision of this comment claimed the deleters capturing
    // only IRenderDevice& made a late release safe; that was exactly backwards.
    m_impl->pendingJobs.Wait();
}

std::shared_ptr<ShaderSource> ShaderManager::CreateSource(const ShaderSourceCompileDesc& desc) {
    std::shared_ptr<ShaderSource> target = MakeSource(m_impl->renderDevice, desc.stage);
    SubmitSourceCompileJob(*m_impl, target, desc);
    if (m_impl->config.compileSynchronously) {
        DrainCompletedSourceJobs(*m_impl);
    }
    return target;
}

std::shared_ptr<ShaderProgram> ShaderManager::CreateProgram(const ShaderProgramDesc& desc) {
    const Impl::ProgramRequestDesc request = BuildProgramRequestDesc(desc);
    std::shared_ptr<ShaderProgram> program = MakeProgram(m_impl->renderDevice);
    std::shared_ptr<ShaderSource> vertexSource = MakeSource(m_impl->renderDevice, ShaderStage::Vertex);
    std::shared_ptr<ShaderSource> fragmentSource = MakeSource(m_impl->renderDevice, ShaderStage::Fragment);

    SubmitSourceCompileJob(*m_impl, vertexSource, request.vertexSource);
    SubmitSourceCompileJob(*m_impl, fragmentSource, request.fragmentSource);

    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->pendingProgramLinks.push_back(
            Impl::PendingProgramLink{program, vertexSource, fragmentSource, request});
    }
    if (m_impl->config.compileSynchronously) {
        DrainCompletedSourceJobs(*m_impl);
        ApplyPendingProgramLinks(*m_impl);
    }
    return program;
}

std::shared_ptr<ShaderProgram> ShaderManager::CreateProgramFromStages(
    const ShaderProgramStagesDesc& desc) {
    const Impl::ProgramRequestDesc request = BuildProgramRequestDesc(desc);
    std::shared_ptr<ShaderProgram> program = MakeProgram(m_impl->renderDevice);
    std::shared_ptr<ShaderSource> vertexSource = MakeSource(m_impl->renderDevice, ShaderStage::Vertex);
    std::shared_ptr<ShaderSource> fragmentSource = MakeSource(m_impl->renderDevice, ShaderStage::Fragment);

    SubmitSourceCompileJob(*m_impl, vertexSource, request.vertexSource);
    SubmitSourceCompileJob(*m_impl, fragmentSource, request.fragmentSource);

    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->pendingProgramLinks.push_back(
            Impl::PendingProgramLink{program, vertexSource, fragmentSource, request});
    }
    if (m_impl->config.compileSynchronously) {
        DrainCompletedSourceJobs(*m_impl);
        ApplyPendingProgramLinks(*m_impl);
    }
    return program;
}

void ShaderManager::Update(double deltaTimeSeconds) {
    DrainCompletedSourceJobs(*m_impl);
    ApplyPendingProgramLinks(*m_impl);

    if (!m_impl->config.hotReloadEnabled) {
        return;
    }
    m_impl->hotReloadAccumulatorSeconds += deltaTimeSeconds;
    if (m_impl->hotReloadAccumulatorSeconds < m_impl->config.hotReloadPollIntervalSeconds) {
        return;
    }
    m_impl->hotReloadAccumulatorSeconds = 0.0;
    PollHotReload(*m_impl);
    if (m_impl->config.compileSynchronously) {
        DrainCompletedSourceJobs(*m_impl);
        ApplyPendingProgramLinks(*m_impl);
    }
}

std::size_t ShaderManager::GetPendingJobCount() const noexcept {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->pendingJobCount;
}

bool ShaderManager::GetLastCompileUsedCache() const noexcept {
    return m_impl->lastCompileUsedCache;
}

} // namespace UVE::Render::Shader
