// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>

namespace UVE::Render {

/// Engine-native description of a reflected shader uniform's data type — never a raw GL enum, so
/// this stays meaningful across any future non-GL backend. Covers the uniform shapes
/// ShaderProgram's SetFloat/SetInt/SetBool/SetVector3/SetMatrix4x4 API sets and
/// keeps an explicit marker for reflected shapes that have no setter yet. Unsupported shapes must
/// not be silently collapsed into Int, because that would issue an invalid GL uniform call.
enum class ShaderDataType : std::uint8_t {
    Float,
    Vec2,
    Vec3,
    Vec4,
    Mat3,
    Mat4,
    Int,
    Bool,
    Unsupported,
};

/// Size, in bytes, of one value of `type` (array stride for a reflected uniform array). Every
/// enumerator is handled explicitly — no default case — so a future addition to
/// ShaderDataType fails to compile here until this function is updated too.
[[nodiscard]] std::size_t GetShaderDataTypeSizeBytes(ShaderDataType type) noexcept;

} // namespace UVE::Render
