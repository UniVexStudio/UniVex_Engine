// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string>

namespace UVE::Render {

/// Describes one compute program for IComputeSystemUVE::CreateProgramUVE() to compile and link on
/// the system's injected render device. `sourceCode` is the backend's own shader language exactly
/// like the RHI's ShaderDescUVE takes it - GLSL text on the OpenGL device, SPIR-V bytes on the
/// Vulkan device; translating between the two is the still-open shader cross-compilation item in
/// ROADMAP section 2, not this layer's job. `entryPointName` is forwarded to the RHI and only
/// means anything to backends with named entry points (Vulkan); the GL device ignores it.
/// `debugName` never reaches the GPU - it exists so creation failures and skipped dispatches can
/// name the kernel in warnings and diagnostics instead of printing bare handles.
/// Thread-safety: value type; safe to copy freely.
struct ComputeProgramDescUVE {
    std::string sourceCode;
    std::string entryPointName = "main";
    std::string debugName;
};

} // namespace UVE::Render
