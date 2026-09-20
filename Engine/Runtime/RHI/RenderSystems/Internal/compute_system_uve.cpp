// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/compute_system_uve.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

#include "uve/logging/logging_macros_uve.h"

namespace UVE::Render {

ComputeSystemUVE::ComputeSystemUVE(IRenderDeviceUVE& renderDevice) noexcept : m_device(renderDevice) {}

ComputeSystemUVE::~ComputeSystemUVE() {
    // Own every program to the end: release the still-live ones through the device rather than
    // leaking GPU-side pipelines when the system dies first (the device outliving its systems is
    // the engine's standard teardown ordering).
    for (const auto& [handle, record] : m_programs) {
        m_device.DestroyPipelineUVE(handle);
    }
    m_programs.clear();
}

PipelineHandleUVE ComputeSystemUVE::CreateProgramUVE(const ComputeProgramDescUVE& desc,
                                                     std::string* outInfoLog) {
    if (desc.sourceCode.empty()) {
        ++m_diagnostics.programCreationsFailed;
        UVE_WARNING("ComputeSystemUVE: CreateProgramUVE rejected \"{}\" - empty compute source",
                    desc.debugName);
        if (outInfoLog != nullptr) {
            *outInfoLog = "empty compute source";
        }
        return kInvalidPipelineHandleUVE;
    }

    std::string compileLog;
    ShaderDescUVE shaderDesc;
    shaderDesc.stage = ShaderStageUVE::Compute;
    shaderDesc.sourceCode = desc.sourceCode;
    shaderDesc.entryPointName = desc.entryPointName;
    const ShaderHandleUVE shader = m_device.CreateShaderUVE(shaderDesc, &compileLog);
    if (shader == kInvalidShaderHandleUVE) {
        ++m_diagnostics.programCreationsFailed;
        UVE_WARNING("ComputeSystemUVE: compute shader compile failed for \"{}\": {}",
                    desc.debugName, compileLog);
        if (outInfoLog != nullptr) {
            *outInfoLog = compileLog;
        }
        return kInvalidPipelineHandleUVE;
    }

    ComputePipelineDescUVE pipelineDesc;
    pipelineDesc.computeShader = shader;
    std::string linkLog;
    const PipelineHandleUVE program = m_device.CreateComputePipelineUVE(pipelineDesc, &linkLog);
    // The RHI shader object's job ends at pipeline creation: GL defers deletion while the shader
    // is still attached to the program, Vulkan permits destroying the module once a pipeline
    // exists, and Null just drops the handle - so destroying here leaks nothing and callers only
    // ever track the one pipeline handle.
    m_device.DestroyShaderUVE(shader);
    if (program == kInvalidPipelineHandleUVE) {
        ++m_diagnostics.programCreationsFailed;
        UVE_WARNING("ComputeSystemUVE: compute pipeline link failed for \"{}\": {}",
                    desc.debugName, linkLog);
        if (outInfoLog != nullptr) {
            *outInfoLog = linkLog;
        }
        return kInvalidPipelineHandleUVE;
    }

    m_programs.emplace(program, ProgramRecordUVE{desc.debugName});
    ++m_diagnostics.programsCreated;
    m_diagnostics.livePrograms = m_programs.size();
    return program;
}

void ComputeSystemUVE::DestroyProgramUVE(PipelineHandleUVE program) {
    const auto found = m_programs.find(program);
    if (found == m_programs.end()) {
        UVE_WARNING("ComputeSystemUVE: DestroyProgramUVE ignored - handle {} was not created by "
                    "this system (or was already destroyed)",
                    program.value);
        return;
    }
    m_device.DestroyPipelineUVE(program);
    m_programs.erase(found);
    m_diagnostics.livePrograms = m_programs.size();
}

bool ComputeSystemUVE::IsProgramLiveUVE(PipelineHandleUVE program) const noexcept {
    return m_programs.contains(program);
}

bool ComputeSystemUVE::EnqueueDispatchUVE(const ComputeDispatchDescUVE& desc) {
    const auto reject = [this, &desc](const char* reason) {
        ++m_diagnostics.dispatchesRejected;
        UVE_WARNING("ComputeSystemUVE: EnqueueDispatchUVE rejected dispatch for program {}: {}",
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
    for (const ComputeStorageBufferBindingUVE& binding : desc.storageBuffers) {
        if (binding.buffer == kInvalidBufferHandleUVE) {
            return reject("storage-buffer binding holds an invalid buffer handle");
        }
    }
    for (const ComputeTextureBindingUVE& binding : desc.textures) {
        if (binding.texture == kInvalidTextureHandleUVE) {
            return reject("texture binding holds an invalid texture handle");
        }
    }
    for (const ComputeUniformWriteUVE& uniform : desc.uniforms) {
        if (uniform.name.empty()) {
            return reject("uniform write has an empty name");
        }
    }

    m_queue.push_back(desc);
    ++m_diagnostics.dispatchesEnqueued;
    return true;
}

std::size_t ComputeSystemUVE::ExecuteQueuedDispatchesUVE(ICommandBufferUVE& commands) {
    std::size_t recorded = 0U;
    for (const ComputeDispatchDescUVE& dispatch : m_queue) {
        if (!m_programs.contains(dispatch.program)) {
            ++m_diagnostics.dispatchesSkipped;
            UVE_WARNING("ComputeSystemUVE: skipping queued dispatch - its program {} died between "
                        "enqueue and execute",
                        dispatch.program.value);
            continue;
        }

        // The recording order is the order the RHI's uniform and binding contracts require:
        // pipeline bind first (uniform writes resolve against the bound pipeline), then the
        // resource binds, then the dispatch itself. Outside pass markers, per the interface
        // contract - backends that tolerate an in-pass dispatch do so by breaking their own
        // passes, which is exactly the portability trap this layer exists to prevent.
        commands.BindPipelineUVE(dispatch.program);
        for (const ComputeStorageBufferBindingUVE& binding : dispatch.storageBuffers) {
            commands.BindStorageBufferUVE(binding.buffer, binding.slot);
        }
        for (const ComputeTextureBindingUVE& binding : dispatch.textures) {
            commands.BindTextureUVE(binding.texture, binding.slot);
        }
        for (const ComputeUniformWriteUVE& uniform : dispatch.uniforms) {
            std::visit(
                [&commands, &uniform](const auto& value) {
                    using ValueType = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<ValueType, float>) {
                        commands.SetUniformFloatUVE(uniform.name, value);
                    } else if constexpr (std::is_same_v<ValueType, std::int32_t>) {
                        commands.SetUniformIntUVE(uniform.name, value);
                    } else if constexpr (std::is_same_v<ValueType, bool>) {
                        commands.SetUniformBoolUVE(uniform.name, value);
                    } else if constexpr (std::is_same_v<ValueType, Math::Vector3UVE>) {
                        commands.SetUniformVector3UVE(uniform.name, value);
                    } else if constexpr (std::is_same_v<ValueType, Math::Matrix4x4UVE>) {
                        commands.SetUniformMatrix4x4UVE(uniform.name, value);
                    }
                },
                uniform.value);
        }
        commands.DispatchUVE(dispatch.groupCountX, dispatch.groupCountY, dispatch.groupCountZ);
        ++recorded;
    }

    m_queue.clear();
    m_diagnostics.dispatchesRecorded += static_cast<std::uint32_t>(recorded);
    return recorded;
}

std::size_t ComputeSystemUVE::GetQueuedDispatchCountUVE() const noexcept {
    return m_queue.size();
}

void ComputeSystemUVE::ClearQueueUVE() {
    m_diagnostics.dispatchesCleared += static_cast<std::uint32_t>(m_queue.size());
    m_queue.clear();
}

const ComputeSystemDiagnosticsUVE& ComputeSystemUVE::GetDiagnosticsUVE() const noexcept {
    return m_diagnostics;
}

} // namespace UVE::Render
