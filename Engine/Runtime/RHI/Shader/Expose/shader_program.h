// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "matrix3x3.h"
#include "matrix4x4.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "i_command_buffer.h"
#include "pipeline_handle.h"
#include "shader_compile_diagnostics.h"
#include "uniform_reflection.h"

namespace UVE::Render::Shader {

class ShaderManager;

/// A linked vertex+fragment program (a wrapper over PipelineHandle) plus its reflected
/// uniforms and a small pending-uniform-value cache (Increment 21). Always handed out as a
/// std::shared_ptr<ShaderProgram> by IShaderManager::CreateProgram() — never constructed
/// directly. A hot-reload swap mutates this same object's handle/diagnostics/uniform table in
/// place; every currently-set pending uniform value survives the swap unchanged.
/// Thread-safety: not thread-safe — see ShaderSource's identical contract.
class ShaderProgram final {
public:
    [[nodiscard]] PipelineHandle GetPipelineHandle() const noexcept { return m_pipeline; }
    [[nodiscard]] bool IsReady() const noexcept { return m_ready; }
    [[nodiscard]] bool IsValid() const noexcept { return m_valid; }
    [[nodiscard]] const ShaderCompileDiagnostics& GetDiagnostics() const noexcept { return m_diagnostics; }
    [[nodiscard]] std::span<const UniformReflection> GetUniforms() const noexcept { return m_uniforms; }
    [[nodiscard]] std::uint64_t GetContentHash() const noexcept { return m_contentHash; }

    /// Looks up one reflected uniform by name, or std::nullopt if `name` isn't active on this
    /// program's linked shaders.
    [[nodiscard]] std::optional<UniformReflection> FindUniform(std::string_view name) const;

    /// Queues a uniform value, applied the next time ApplyTo() runs (not immediately) — so a
    /// caller can set uniforms in any order, any number of times per frame, before the program is
    /// ever bound. Setting a name this program doesn't declare is harmless (the value is simply
    /// never applied; ApplyTo()'s underlying ICommandBuffer::SetUniform*UVE() call logs a
    /// low-severity warning for an unknown name, matching the RHI's own contract).
    void SetFloat(std::string_view name, float value);
    void SetInt(std::string_view name, std::int32_t value);
    void SetBool(std::string_view name, bool value);
    void SetVector2(std::string_view name, const Math::Vector2& value);
    void SetVector3(std::string_view name, const Math::Vector3& value);
    void SetVector4(std::string_view name, const Math::Vector4& value);
    void SetMatrix4x4(std::string_view name, const Math::Matrix4x4& value);
    /// Queues a 3x3 uniform value (the renderer's normal matrix), embedded in a 4x4 for upload:
    /// the RHI uniform chain (ICommandBuffer) has no mat3 setter, and every built-in shader
    /// consumes this uniform through mat3(...) anyway, so the embedded fourth row/column never
    /// reaches a computation. When the chain grows a mat3 arm, only this setter's body changes —
    /// callers stay as-is.
    void SetMatrix3x3(std::string_view name, const Math::Matrix3x3& value);

    /// Binds this program's pipeline on `commandBuffer` and flushes every currently-queued
    /// uniform value onto it. Must be called inside a render pass, exactly where a raw
    /// BindPipeline() call would go. A safe, logged no-op if !IsValid() (e.g. still
    /// compiling, or its most recent compile failed) — never binds a stale/invalid handle.
    void ApplyTo(ICommandBuffer& commandBuffer) const;

private:
    friend class ShaderManager;
    ShaderProgram() = default;

    using PendingUniformValue =
        std::variant<float, std::int32_t, bool, Math::Vector2, Math::Vector3, Math::Vector4,
                     Math::Matrix4x4>;

    struct TransparentStringHash {
        using is_transparent = void;

        [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }
        [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }
    };

    struct TransparentStringEqual {
        using is_transparent = void;

        [[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
            return lhs == rhs;
        }
    };

    struct PendingUniformEntry final {
        std::string name;
        PendingUniformValue value;
        bool dirty = true;
    };

    void SetPendingUniform(std::string_view name, PendingUniformValue value);

    PipelineHandle m_pipeline = kInvalidPipelineHandle;
    bool m_ready = false;
    bool m_valid = false;
    ShaderCompileDiagnostics m_diagnostics;
    std::vector<UniformReflection> m_uniforms;
    std::uint64_t m_contentHash = 0;
    mutable std::vector<PendingUniformEntry> m_uniformEntries;
    std::unordered_map<std::string, std::size_t, TransparentStringHash, TransparentStringEqual>
        m_uniformIndexByName;
    mutable std::vector<std::size_t> m_dirtyUniformIndices;
    mutable PipelineHandle m_lastSyncedPipeline = kInvalidPipelineHandle;
    mutable std::uint64_t m_lastSyncedBufferId = 0U;
};

} // namespace UVE::Render::Shader
