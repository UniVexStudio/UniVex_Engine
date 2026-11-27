// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "matrix4x4.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "buffer_handle.h"
#include "device_operations.h"
#include "pipeline_handle.h"
#include "render_resource_descs.h"
#include "sampler_handle.h"
#include "texture_handle.h"

namespace UVE::Render {

/// One call recorded by NullCommandBuffer (engine/render/src/, module-private), in the exact
/// shape and order it was issued. Public (unlike NullCommandBuffer itself) because test code
/// — and NullRenderDevice::GetLastSubmittedCommands(), its one intentional test-only public
/// hook — needs to name this type to assert exactly what a real backend would have received.
struct BeginRenderPassCommand {
    RenderPassDesc desc;
};
struct EndRenderPassCommand {};
struct BindPipelineCommand {
    PipelineHandle pipeline;
};
struct BindVertexBufferCommand {
    BufferHandle buffer;
    std::uint32_t slot = 0;
};
struct BindIndexBufferCommand {
    BufferHandle buffer;
};
struct BindTextureCommand {
    TextureHandle texture;
    std::uint32_t slot = 0;
};
struct BindSamplerCommand {
    SamplerHandle sampler;
    std::uint32_t slot = 0;
};
struct BindUniformBufferCommand {
    BufferHandle buffer;
    std::uint32_t slot = 0;
};
struct BindStorageBufferCommand {
    BufferHandle buffer;
    std::uint32_t slot = 0;
};
struct DrawIndexedCommand {
    std::uint32_t indexCount = 0;
    std::uint32_t instanceCount = 1;
    /// First index of the draw's range, in indices. Non-zero only for a mesh surface.
    std::uint32_t firstIndex = 0;
};
struct DrawCommand {
    std::uint32_t vertexCount = 0;
    std::uint32_t instanceCount = 1;
    std::uint32_t firstVertex = 0;
};
/// CS7: one indexed indirect draw. The buffer must be an IndirectStorage buffer holding at least
/// one DrawIndexedIndirectCommand at `offsetBytes`; the GPU reads the parameters from there at
/// execution time, which is the whole point - nothing on the CPU need know the index count.
struct DrawIndexedIndirectCommandRecord {
    BufferHandle buffer;
    std::uint64_t offsetBytes = 0;
};
struct DispatchCommand {
    std::uint32_t groupCountX = 0;
    std::uint32_t groupCountY = 0;
    std::uint32_t groupCountZ = 0;
};
struct SetUniformFloatCommand {
    std::string name;
    float value = 0.0F;
};
struct SetUniformIntCommand {
    std::string name;
    std::int32_t value = 0;
};
struct SetUniformBoolCommand {
    std::string name;
    bool value = false;
};
struct SetUniformVector2Command {
    std::string name;
    Math::Vector2 value;
};
struct SetUniformVector3Command {
    std::string name;
    Math::Vector3 value;
};
struct SetUniformVector4Command {
    std::string name;
    Math::Vector4 value;
};
struct SetUniformMatrix4x4Command {
    std::string name;
    Math::Matrix4x4 value;
};

/// Recorded query brackets and timestamps (Foundation WP3): the backend brackets the draws
/// recorded between Begin and End with the query at replay/issue time.
struct BeginQueryCommand {
    QueryHandle query;
};
struct EndQueryCommand {
    QueryHandle query;
};
struct WriteTimestampCommand {
    QueryHandle query;
};

/// A single recorded ICommandBuffer call, tagged by which method it came from.
using RecordedCommand = std::variant<
    BeginRenderPassCommand, EndRenderPassCommand, BindPipelineCommand, BindVertexBufferCommand,
    BindIndexBufferCommand, BindTextureCommand, BindSamplerCommand, BindUniformBufferCommand,
    BindStorageBufferCommand, DrawIndexedCommand, DrawCommand, DrawIndexedIndirectCommandRecord,
    DispatchCommand, SetUniformFloatCommand, SetUniformIntCommand, SetUniformBoolCommand,
    SetUniformVector2Command, SetUniformVector3Command, SetUniformVector4Command,
    SetUniformMatrix4x4Command, MemoryBarrierCommand,
    BeginQueryCommand, EndQueryCommand, WriteTimestampCommand>;

} // namespace UVE::Render
