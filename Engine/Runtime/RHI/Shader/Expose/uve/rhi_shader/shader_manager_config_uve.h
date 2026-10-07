// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <filesystem>

namespace UVE::Render::Shader {

/// Construction-time configuration for ShaderManagerUVE, built from EngineConfigUVE by
/// EngineCoreUVE::Init() (mirrors ShaderManagerConfigUVE's role to WindowDescUVE's for
/// WindowManagerUVE).
struct ShaderManagerConfigUVE {
    /// Directory the on-disk program-binary cache is stored under (a per-platform subdirectory
    /// is appended automatically — see Detail's shader_binary_cache_uve.h).
    std::filesystem::path cachePath = "shader_cache/";

    /// Run source preprocessing and GPU compile/link in the Create* call instead of queuing
    /// preprocessing work to the thread pool and completing GPU work while UpdateUVE() drains it.
    /// Call the creation APIs on the render thread in this mode, as required by IRenderDeviceUVE.
    /// Synchronous mode is useful for deterministic tools/tests but may block the caller; it does
    /// not introduce ahead-of-time compilation or placeholder programs.
    bool compileSynchronouslyUVE = false;

    /// Whether ShaderManagerUVE::UpdateUVE() polls hot-reload-tracked programs' dependency
    /// closures for on-disk changes at all.
    bool hotReloadEnabledUVE = true;

    /// Poll interval, in seconds, between hot-reload mtime checks.
    double hotReloadPollIntervalSecondsUVE = 1.0;

    /// Forwarded into every compile's injected `#define UVE_DEBUG 0|1` block.
    bool injectDebugDefineUVE = true;
};

} // namespace UVE::Render::Shader
