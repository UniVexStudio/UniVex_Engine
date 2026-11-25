// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string>

#include "render_resource_descs.h"
#include "shader_compile_diagnostics.h"

namespace UVE::Render::Shader {

/// Queued (never Published — may originate from ShaderManager::Update()'s main-thread
/// hot-reload check, always delivered via IEventSystem::QueueEvent) whenever a hot-reload
/// recompile finishes, success or failure — mirrors Asset::AssetReloadedEvent's role.
struct ShaderProgramReloadedEvent {
    std::string debugName;
    bool success = false;
};

/// Queued alongside ShaderProgramReloadedEvent{success=false} with the full structured
/// diagnostics, for callers (e.g. an editor console) that want the detail without re-querying
/// GetDiagnostics() themselves.
struct ShaderCompileFailedEvent {
    std::string debugName;
    ShaderStage stage = ShaderStage::Vertex;
    ShaderCompileDiagnostics diagnostics;
};

} // namespace UVE::Render::Shader
