// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "compute_system.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

#include "logging_macros.h"

namespace UVE::Render {

ComputeSystem::ComputeSystem(IRenderDevice& renderDevice) noexcept : m_device(renderDevice) {}

ComputeSystem::~ComputeSystem() {
    // Own every program to the end: release the still-live ones through the device rather than
    // leaking GPU-side pipelines when the system dies first (the device outliving its systems is
    // the engine's standard teardown ordering).
    for (const auto& [handle, record] : m_programs) {
        m_device.DestroyPipeline(handle);
    }
    m_programs.clear();
}

PipelineHandle ComputeSystem::CreateProgram(const ComputeProgramDesc& desc,
                                                     std::string* outInfoLog) {
    if (desc.sourceCode.empty()) {
        ++m_diagnostics.programCreationsFailed;
        UVE_WARNING("ComputeSystem: CreateProgram rejected \"{}\" - empty compute source",
                    desc.debugName);
        if (outInfoLog != nullptr) {
            *outInfoLog = "empty compute source";
        }
        return kInvalidPipelineHandle;
    }

    std::string compileLog;
    ShaderDesc shaderDesc;
    shaderDesc.stage = ShaderStage::Compute;
    shaderDesc.sourceCode = desc.sourceCode;
    shaderDesc.entryPointName = desc.entryPointName;
    const ShaderHandle shader = m_device.CreateShader(shaderDesc, &compileLog);
    if (shader == kInvalidShaderHandle) {
        ++m_diagnostics.programCreationsFailed;
        UVE_WARNING("ComputeSystem: compute shader compile failed for \"{}\": {}",
                    desc.debugName, compileLog);
        if (outInfoLog != nullptr) {
            *outInfoLog = compileLog;
        }
        return kInvalidPipelineHandle;
    }

    ComputePipelineDesc pipelineDesc;
    pipelineDesc.computeShader = shader;
    std::string linkLog;
    const PipelineHandle program = m_device.CreateComputePipeline(pipelineDesc, &linkLog);
    // The RHI shader object's job ends at pipeline creation: GL defers deletion while the shader
    // is still attached to the program, Vulkan permits destroying the module once a pipeline
    // exists, and Null just drops the handle - so destroying here leaks nothing and callers only
    // ever track the one pipeline handle.
    m_device.DestroyShader(shader);
    if (program == kInvalidPipelineHandle) {
        ++m_diagnostics.programCreationsFailed;
        UVE_WARNING("ComputeSystem: compute pipeline link failed for \"{}\": {}",
                    desc.debugName, linkLog);
        if (outInfoLog != nullptr) {
            *outInfoLog = linkLog;
        }
        return kInvalidPipelineHandle;
    }

    m_programs.emplace(program, ProgramRecord{desc.debugName});
    ++m_diagnostics.programsCreated;
    m_diagnostics.livePrograms = m_programs.size();
    return program;
}

void ComputeSystem::DestroyProgram(PipelineHandle program) {
    const auto found = m_programs.find(program);
    if (found == m_programs.end()) {
        UVE_WARNING("ComputeSystem: DestroyProgram ignored - handle {} was not created by "
                    "this system (or was already destroyed)",
                    program.value);
        return;
    }
    m_device.DestroyPipeline(program);
    m_programs.erase(found);
    m_diagnostics.livePrograms = m_programs.size();
}

bool ComputeSystem::IsProgramLive(PipelineHandle program) const noexcept {
    return m_programs.contains(program);
}

bool ComputeSystem::EnqueueDispatch(const ComputeDispatchDesc& desc) {
    const auto reject = [this, &desc](const char* reason) {
        ++m_diagnostics.dispatchesRejected;
        UVE_WARNING("ComputeSystem: EnqueueDispatch rejected dispatch for program {}: {}",
                    desc.program.value, reason);
        return false;
    };

    const auto program = m_programs.find(desc.program);
    if (program == m_programs.end()) {
        return reject("program is not live in this system (never created here, or already destroyed)");
    }
    if (desc.groupCountX == 0U || desc.groupCountY == 0U || desc.groupCountZ == 0U) {
        return reject("every group count must be at least 1 - a zero axis launches no work at all");
    }
    for (const ComputeStorageBufferBinding& binding : desc.storageBuffers) {
        if (binding.buffer == kInvalidBufferHandle) {
            return reject("storage-buffer binding holds an invalid buffer handle");
        }
    }
    for (const ComputeTextureBinding& binding : desc.textures) {
        if (binding.texture == kInvalidTextureHandle) {
            return reject("texture binding holds an invalid texture handle");
        }
    }
    for (const ComputeUniformWrite& uniform : desc.uniforms) {
        if (uniform.name.empty()) {
            return reject("uniform write has an empty name");
        }
    }

    m_queue.push_back(desc);
    ++m_diagnostics.dispatchesEnqueued;
    return true;
}

std::size_t ComputeSystem::ExecuteQueuedDispatches(ICommandBuffer& commands) {
    std::size_t recorded = 0U;
    for (const ComputeDispatchDesc& dispatch : m_queue) {
        if (!m_programs.contains(dispatch.program)) {
            ++m_diagnostics.dispatchesSkipped;
            UVE_WARNING("ComputeSystem: skipping queued dispatch - its program {} died between "
                        "enqueue and execute",
                        dispatch.program.value);
            continue;
        }

        // The recording order is the order the RHI's uniform and binding contracts require:
        // pipeline bind first (uniform writes resolve against the bound pipeline), then the
        // resource binds, then the dispatch itself. Outside pass markers, per the interface
        // contract - backends that tolerate an in-pass dispatch do so by breaking their own
        // passes, which is exactly the portability trap this layer exists to prevent.
        commands.BindPipeline(dispatch.program);
        for (const ComputeStorageBufferBinding& binding : dispatch.storageBuffers) {
            commands.BindStorageBuffer(binding.buffer, binding.slot);
        }
        for (const ComputeTextureBinding& binding : dispatch.textures) {
            commands.BindTexture(binding.texture, binding.slot);
        }
        for (const ComputeUniformWrite& uniform : dispatch.uniforms) {
            std::visit(
                [&commands, &uniform](const auto& value) {
                    using ValueType = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<ValueType, float>) {
                        commands.SetUniformFloat(uniform.name, value);
                    } else if constexpr (std::is_same_v<ValueType, std::int32_t>) {
                        commands.SetUniformInt(uniform.name, value);
                    } else if constexpr (std::is_same_v<ValueType, bool>) {
                        commands.SetUniformBool(uniform.name, value);
                    } else if constexpr (std::is_same_v<ValueType, Math::Vector3>) {
                        commands.SetUniformVector3(uniform.name, value);
                    } else if constexpr (std::is_same_v<ValueType, Math::Matrix4x4>) {
                        commands.SetUniformMatrix4x4(uniform.name, value);
                    }
                },
                uniform.value);
        }
        commands.Dispatch(dispatch.groupCountX, dispatch.groupCountY, dispatch.groupCountZ);
        ++recorded;
    }

    m_queue.clear();
    m_diagnostics.dispatchesRecorded += static_cast<std::uint32_t>(recorded);
    return recorded;
}

std::size_t ComputeSystem::GetQueuedDispatchCount() const noexcept {
    return m_queue.size();
}

void ComputeSystem::ClearQueue() {
    m_diagnostics.dispatchesCleared += static_cast<std::uint32_t>(m_queue.size());
    m_queue.clear();
}

const ComputeSystemDiagnostics& ComputeSystem::GetDiagnostics() const noexcept {
    return m_diagnostics;
}

} // namespace UVE::Render
