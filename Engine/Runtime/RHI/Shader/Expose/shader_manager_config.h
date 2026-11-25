// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <filesystem>

namespace UVE::Render::Shader {

/// Construction-time configuration for ShaderManager, built from EngineConfig by
/// EngineCore::Init() (mirrors ShaderManagerConfig's role to WindowDesc's for
/// WindowManager).
struct ShaderManagerConfig {
    /// Directory the on-disk program-binary cache is stored under (a per-platform subdirectory
    /// is appended automatically — see Detail's shader_binary_cache_uve.h).
    std::filesystem::path cachePath = "shader_cache/";

    /// Run source preprocessing and GPU compile/link in the Create* call instead of queuing
    /// preprocessing work to the thread pool and completing GPU work while Update() drains it.
    /// Call the creation APIs on the render thread in this mode, as required by IRenderDevice.
    /// Synchronous mode is useful for deterministic tools/tests but may block the caller; it does
    /// not introduce ahead-of-time compilation or placeholder programs.
    bool compileSynchronously = false;

    /// Whether ShaderManager::Update() polls hot-reload-tracked programs' dependency
    /// closures for on-disk changes at all.
    bool hotReloadEnabled = true;

    /// Poll interval, in seconds, between hot-reload mtime checks.
    double hotReloadPollIntervalSeconds = 1.0;

    /// Forwarded into every compile's injected `#define UVE_DEBUG 0|1` block.
    bool injectDebugDefine = true;
};

} // namespace UVE::Render::Shader
