// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/rhi/buffer_handle_uve.h"
#include "uve/rhi/pipeline_handle_uve.h"
#include "uve/rhi/texture_handle_uve.h"

namespace UVE::Render {

/// One shader-storage-buffer binding for a queued compute dispatch. `slot` is the binding index
/// the kernel itself declares (GLSL `layout(std430, binding = N) buffer ...`, SPIR-V
/// `Binding = N`); the RHI maps it to the backend's own binding space (GL indexed shader-storage
/// target, Vulkan descriptor binding). `buffer` must be a live Storage-usage buffer - liveness is
/// the RHI's bookkeeping, so binding a destroyed handle degrades the way every RHI replay already
/// documents (loud warning, no GPU access), but IComputeSystemUVE rejects an outright invalid
/// handle at enqueue time.
struct ComputeStorageBufferBindingUVE {
    BufferHandleUVE buffer;
    std::uint32_t slot = 0U;
};

/// One texture binding for a queued compute dispatch - either a sampled texture (`texture()` in
/// GLSL) or a storage image (`imageLoad`/`imageStore`; the M5b slice made storage images first-
/// class RHI citizens with their own usage flag and layout discipline). Like storage buffers, the
/// slot is the binding index the kernel declares, and handle liveness stays the RHI's contract.
struct ComputeTextureBindingUVE {
    TextureHandleUVE texture;
    std::uint32_t slot = 0U;
};

/// One named uniform write applied to a dispatch's program after its pipeline bind and before the
/// dispatch itself. The alternative set is exactly the five uniform types ICommandBufferUVE can
/// set (float, int32, bool, Vector3, Matrix4x4) - unsigned ints and doubles are not expressible
/// anywhere in this RHI, so they are not expressible here either. Build these through the
/// MakeComputeUniform*UVE() factories below: initializing the variant directly from a literal is
/// ambiguous between the float/int32/bool alternatives, and the factories make the chosen type
/// explicit at every call site.
struct ComputeUniformWriteUVE {
    std::string name;
    std::variant<float, std::int32_t, bool, Math::Vector3UVE, Math::Matrix4x4UVE> value = 0.0F;
};

[[nodiscard]] inline ComputeUniformWriteUVE MakeComputeUniformFloatUVE(std::string name,
                                                                       const float value) {
    ComputeUniformWriteUVE write;
    write.name = std::move(name);
    write.value.emplace<float>(value);
    return write;
}

[[nodiscard]] inline ComputeUniformWriteUVE MakeComputeUniformIntUVE(std::string name,
                                                                     const std::int32_t value) {
    ComputeUniformWriteUVE write;
    write.name = std::move(name);
    write.value.emplace<std::int32_t>(value);
    return write;
}

[[nodiscard]] inline ComputeUniformWriteUVE MakeComputeUniformBoolUVE(std::string name,
                                                                      const bool value) {
    ComputeUniformWriteUVE write;
    write.name = std::move(name);
    write.value.emplace<bool>(value);
    return write;
}

[[nodiscard]] inline ComputeUniformWriteUVE MakeComputeUniformVector3UVE(std::string name,
                                                                         const Math::Vector3UVE& value) {
    ComputeUniformWriteUVE write;
    write.name = std::move(name);
    write.value.emplace<Math::Vector3UVE>(value);
    return write;
}

[[nodiscard]] inline ComputeUniformWriteUVE MakeComputeUniformMatrix4x4UVE(std::string name,
                                                                           const Math::Matrix4x4UVE& value) {
    ComputeUniformWriteUVE write;
    write.name = std::move(name);
    write.value.emplace<Math::Matrix4x4UVE>(value);
    return write;
}

/// One compute dispatch as authored by engine or game code and queued on IComputeSystemUVE:
/// which program to run, how many workgroups to launch, which buffers/textures to bind at which
/// slots, and which uniforms to write first. Copied by value into the queue, so the caller's desc
/// may be reused or destroyed immediately after EnqueueDispatchUVE() returns. `program` must be a
/// handle IComputeSystemUVE::CreateProgramUVE() returned and that is still live at enqueue time;
/// if it dies before the queue executes, the dispatch is skipped loudly rather than recorded
/// against a dead handle. All three group counts must be at least 1: a zero on any axis launches
/// no work at all, which after this engine's experience with silent no-ops is treated as the
/// authoring bug it nearly always is.
/// Thread-safety: value type; safe to copy freely.
struct ComputeDispatchDescUVE {
    PipelineHandleUVE program;
    std::uint32_t groupCountX = 1U;
    std::uint32_t groupCountY = 1U;
    std::uint32_t groupCountZ = 1U;
    std::vector<ComputeStorageBufferBindingUVE> storageBuffers;
    std::vector<ComputeTextureBindingUVE> textures;
    std::vector<ComputeUniformWriteUVE> uniforms;
};

} // namespace UVE::Render
