// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "shader_program.h"

#include <algorithm>
#include <type_traits>
#include <utility>

#include "logging_macros.h"

namespace UVE::Render::Shader {

std::optional<UniformReflection> ShaderProgram::FindUniform(std::string_view name) const {
    const auto it = std::find_if(m_uniforms.begin(), m_uniforms.end(),
                                  [&](const UniformReflection& uniform) { return uniform.name == name; });
    if (it == m_uniforms.end()) {
        return std::nullopt;
    }
    return *it;
}

void ShaderProgram::SetPendingUniform(std::string_view name, PendingUniformValue value) {
    const auto existingIt = m_uniformIndexByName.find(name);
    if (existingIt != m_uniformIndexByName.end()) {
        PendingUniformEntry& entry = m_uniformEntries[existingIt->second];
        if (entry.value == value) {
            return;
        }
        entry.value = std::move(value);
        if (!entry.dirty) {
            entry.dirty = true;
            m_dirtyUniformIndices.push_back(existingIt->second);
        }
        return;
    }
    const std::size_t index = m_uniformEntries.size();
    std::string ownedName(name);
    m_uniformEntries.push_back(PendingUniformEntry{ownedName, std::move(value), true});
    m_uniformIndexByName.emplace(std::move(ownedName), index);
    m_dirtyUniformIndices.push_back(index);
}

void ShaderProgram::SetFloat(std::string_view name, float value) {
    SetPendingUniform(name, value);
}

void ShaderProgram::SetInt(std::string_view name, std::int32_t value) {
    SetPendingUniform(name, value);
}

void ShaderProgram::SetBool(std::string_view name, bool value) {
    SetPendingUniform(name, value);
}

void ShaderProgram::SetVector2(std::string_view name, const Math::Vector2& value) {
    SetPendingUniform(name, value);
}

void ShaderProgram::SetVector3(std::string_view name, const Math::Vector3& value) {
    SetPendingUniform(name, value);
}

void ShaderProgram::SetVector4(std::string_view name, const Math::Vector4& value) {
    SetPendingUniform(name, value);
}

void ShaderProgram::SetMatrix4x4(std::string_view name, const Math::Matrix4x4& value) {
    SetPendingUniform(name, value);
}

void ShaderProgram::SetMatrix3x3(std::string_view name, const Math::Matrix3x3& value) {
    // See the header: embed in an identity-bordered 4x4 for the mat4-only uniform chain.
    SetPendingUniform(name, Math::ToMatrix4x4(value));
}

void ShaderProgram::ApplyTo(ICommandBuffer& commandBuffer) const {
    if (!m_valid) {
        UVE_WARNING("ShaderProgram: ApplyTo called on a program that never linked successfully - skipping");
        return;
    }

    commandBuffer.BindPipeline(m_pipeline);
    const auto flushEntry = [&commandBuffer](const PendingUniformEntry& entry) {
        std::visit(
            [&](const auto& concreteValue) {
                using ValueType = std::decay_t<decltype(concreteValue)>;
                if constexpr (std::is_same_v<ValueType, float>) {
                    commandBuffer.SetUniformFloat(entry.name, concreteValue);
                } else if constexpr (std::is_same_v<ValueType, std::int32_t>) {
                    commandBuffer.SetUniformInt(entry.name, concreteValue);
                } else if constexpr (std::is_same_v<ValueType, bool>) {
                    commandBuffer.SetUniformBool(entry.name, concreteValue);
                } else if constexpr (std::is_same_v<ValueType, Math::Vector2>) {
                    commandBuffer.SetUniformVector2(entry.name, concreteValue);
                } else if constexpr (std::is_same_v<ValueType, Math::Vector3>) {
                    commandBuffer.SetUniformVector3(entry.name, concreteValue);
                } else if constexpr (std::is_same_v<ValueType, Math::Vector4>) {
                    commandBuffer.SetUniformVector4(entry.name, concreteValue);
                } else if constexpr (std::is_same_v<ValueType, Math::Matrix4x4>) {
                    commandBuffer.SetUniformMatrix4x4(entry.name, concreteValue);
                }
            },
            entry.value);
    };

    const bool retainsState = commandBuffer.RetainsUniformStatePerPipeline();
    const std::uint64_t bufferId = commandBuffer.GetUniformStateEpoch();
    if (retainsState && m_lastSyncedPipeline == m_pipeline && m_lastSyncedBufferId == bufferId && bufferId != 0U) {
        for (const std::size_t index : m_dirtyUniformIndices) {
            PendingUniformEntry& entry = m_uniformEntries[index];
            flushEntry(entry);
            entry.dirty = false;
        }
        m_dirtyUniformIndices.clear();
        return;
    }

    for (PendingUniformEntry& entry : m_uniformEntries) {
        flushEntry(entry);
        entry.dirty = false;
    }
    m_dirtyUniformIndices.clear();
    if (retainsState) {
        m_lastSyncedPipeline = m_pipeline;
        m_lastSyncedBufferId = bufferId;
    } else {
        m_lastSyncedPipeline = kInvalidPipelineHandle;
        m_lastSyncedBufferId = 0U;
    }
}

} // namespace UVE::Render::Shader
