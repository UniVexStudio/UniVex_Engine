// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "shader_data_type.h"

#include "engine_assert.h"

namespace UVE::Render {

std::size_t GetShaderDataTypeSizeBytes(ShaderDataType type) noexcept {
    switch (type) {
        case ShaderDataType::Float:
            return 4U;
        case ShaderDataType::Vec2:
            return 8U;
        case ShaderDataType::Vec3:
            return 12U;
        case ShaderDataType::Vec4:
            return 16U;
        case ShaderDataType::Mat3:
            return 36U;
        case ShaderDataType::Mat4:
            return 64U;
        case ShaderDataType::Int:
            return 4U;
        case ShaderDataType::Bool:
            return 4U;
        case ShaderDataType::Unsupported:
            return 0U;
    }
    UVE_ASSERT(false && "Unhandled ShaderDataType");
    return 0U;
}

} // namespace UVE::Render
