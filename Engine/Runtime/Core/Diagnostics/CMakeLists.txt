# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# ProfilerDiagnosticsUVE - a generic CPU/task/memory/render/audio/streaming diagnostics capture
# system (spans, counters, breadcrumbs by DiagnosticCategoryUVE). Zero uve/* dependencies at all.
#
# Physically colocated with EngineCoreUVE in the reference's engine/core/ folder purely because it
# was authored alongside it - direct grep confirms EngineCoreUVE/EngineServicesUVE never include
# or reference ProfilerDiagnosticsUVE. Same "historical colocation, not a real dependency" pattern
# as uve_scheduling (see that module's own CMakeLists comment) and already resolved several times
# earlier this session.
#
# Given its own logical home instead of folding into Engine/Runtime/Engine: checked real Unreal
# Engine source (Engine/Source/Runtime/Core/Public/ProfilingDebugging/) and confirmed profiling
# infrastructure lives inside Unreal's own Core module, not the separate Engine module that owns
# UEngine. Landed here as a new sibling of this repo's existing
# Core/{Logging,Memory,Threading,Math,Utilities} split for that reason.

add_library(uve_diagnostics STATIC
    src/profiler_diagnostics_uve.cpp
)
target_include_directories(uve_diagnostics PUBLIC include)
target_compile_features(uve_diagnostics PUBLIC cxx_std_20)
uve_set_warnings(uve_diagnostics)
