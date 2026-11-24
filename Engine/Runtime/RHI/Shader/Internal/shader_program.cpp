// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "shader_program.h"

#include <algorithm>
#include <type_traits>
#include <utility>

#include "logging_macros.h"

namespace UVE::Render::Shader {

std::optional<UniformReflectionUVE> ShaderProgramUVE::FindUniformUVE(std::string_view name) const {
    const auto it = std::find_if(m_uniforms.begin(), m_uniforms.end(),
                                  [&](const UniformReflectionUVE& uniform) { return uniform.name == name; });
    if (it == m_uniforms.end()) {
        return std::nullopt;
    }
    return *it;
}

void ShaderProgramUVE::SetPendingUniformUVE(std::string_view name, PendingUniformValueUVE value) {
    const auto existingIt = m_uniformIndexByName.find(name);
    if (existingIt != m_uniformIndexByName.end()) {
        PendingUniformEntryUVE& entry = m_uniformEntries[existingIt->second];
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
    m_uniformEntries.push_back(PendingUniformEntryUVE{ownedName, std::move(value), true});
    m_uniformIndexByName.emplace(std::move(ownedName), index);
    m_dirtyUniformIndices.push_back(index);
}

void ShaderProgramUVE::SetFloatUVE(std::string_view name, float value) {
    SetPendingUniformUVE(name, value);
}

void ShaderProgramUVE::SetIntUVE(std::string_view name, std::int32_t value) {
    SetPendingUniformUVE(name, value);
}

void ShaderProgramUVE::SetBoolUVE(std::string_view name, bool value) {
    SetPendingUniformUVE(name, value);
}

void ShaderProgramUVE::SetVector2UVE(std::string_view name, const Math::Vector2UVE& value) {
    SetPendingUniformUVE(name, value);
}

void ShaderProgramUVE::SetVector3UVE(std::string_view name, const Math::Vector3UVE& value) {
    SetPendingUniformUVE(name, value);
}

void ShaderProgramUVE::SetVector4UVE(std::string_view name, const Math::Vector4UVE& value) {
    SetPendingUniformUVE(name, value);
}

void ShaderProgramUVE::SetMatrix4x4UVE(std::string_view name, const Math::Matrix4x4UVE& value) {
    SetPendingUniformUVE(name, value);
}

void ShaderProgramUVE::SetMatrix3x3UVE(std::string_view name, const Math::Matrix3x3UVE& value) {
    // See the header: embed in an identity-bordered 4x4 for the mat4-only uniform chain.
    SetPendingUniformUVE(name, Math::ToMatrix4x4UVE(value));
}

void ShaderProgramUVE::ApplyToUVE(ICommandBufferUVE& commandBuffer) const {
    if (!m_valid) {
        UVE_WARNING("ShaderProgramUVE: ApplyToUVE called on a program that never linked successfully - skipping");
        return;
    }

    commandBuffer.BindPipelineUVE(m_pipeline);
    const auto flushEntry = [&commandBuffer](const PendingUniformEntryUVE& entry) {
        std::visit(
            [&](const auto& concreteValue) {
                using ValueTypeUVE = std::decay_t<decltype(concreteValue)>;
                if constexpr (std::is_same_v<ValueTypeUVE, float>) {
                    commandBuffer.SetUniformFloatUVE(entry.name, concreteValue);
                } else if constexpr (std::is_same_v<ValueTypeUVE, std::int32_t>) {
                    commandBuffer.SetUniformIntUVE(entry.name, concreteValue);
                } else if constexpr (std::is_same_v<ValueTypeUVE, bool>) {
                    commandBuffer.SetUniformBoolUVE(entry.name, concreteValue);
                } else if constexpr (std::is_same_v<ValueTypeUVE, Math::Vector2UVE>) {
                    commandBuffer.SetUniformVector2UVE(entry.name, concreteValue);
                } else if constexpr (std::is_same_v<ValueTypeUVE, Math::Vector3UVE>) {
                    commandBuffer.SetUniformVector3UVE(entry.name, concreteValue);
                } else if constexpr (std::is_same_v<ValueTypeUVE, Math::Vector4UVE>) {
                    commandBuffer.SetUniformVector4UVE(entry.name, concreteValue);
                } else if constexpr (std::is_same_v<ValueTypeUVE, Math::Matrix4x4UVE>) {
                    commandBuffer.SetUniformMatrix4x4UVE(entry.name, concreteValue);
                }
            },
            entry.value);
    };

    const bool retainsState = commandBuffer.RetainsUniformStatePerPipelineUVE();
    const std::uint64_t bufferId = commandBuffer.GetInstanceIdUVE();
    if (retainsState && m_lastSyncedPipeline == m_pipeline && m_lastSyncedBufferId == bufferId && bufferId != 0U) {
        for (const std::size_t index : m_dirtyUniformIndices) {
            PendingUniformEntryUVE& entry = m_uniformEntries[index];
            flushEntry(entry);
            entry.dirty = false;
        }
        m_dirtyUniformIndices.clear();
        return;
    }

    for (PendingUniformEntryUVE& entry : m_uniformEntries) {
        flushEntry(entry);
        entry.dirty = false;
    }
    m_dirtyUniformIndices.clear();
    if (retainsState) {
        m_lastSyncedPipeline = m_pipeline;
        m_lastSyncedBufferId = bufferId;
    } else {
        m_lastSyncedPipeline = kInvalidPipelineHandleUVE;
        m_lastSyncedBufferId = 0U;
    }
}

} // namespace UVE::Render::Shader
