// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Phase 0 of the C#/ImGui.NET editor migration (see the plan checkpoint "C# (ImGui.NET) editor
// migration - Phase 0"). This is a small, standalone native host - not a copy of
// Engine/App/src/editor/main.cpp's real ImGui editor loop - whose only job is to prove the
// .NET native-hosting call path genuinely works end-to-end: locate and load hostfxr, initialize
// it against EditorCoreCs's own published .runtimeconfig.json, resolve its UnmanagedCallersOnly
// per-frame export, and call into it once per real engine frame, handing it the same live
// UveEngineHandleUVE* (and therefore the same live viewport GL texture) uve_engine_capi already
// produces for the C++ ImGui editor. See EditorHostEntry.cs for what the managed side does with
// that call and why verification here is log-based rather than a screenshot.

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <coreclr_delegates.h>
#include <hostfxr.h>
#include <nethost.h>

#include "uve/editor_capi/uve_engine_capi.h"

namespace {

using TickFrameManagedFnUVE = void (*)(void* engineHandle, float availWidth, float availHeight,
                                        std::int32_t frameNumber);

hostfxr_initialize_for_runtime_config_fn g_hostfxrInitializeForRuntimeConfigUVE = nullptr;
hostfxr_get_runtime_delegate_fn g_hostfxrGetRuntimeDelegateUVE = nullptr;

// Locates and dlopen()s the hostfxr shared library via nethost's own documented lookup (honors
// DOTNET_ROOT/the global registration exactly like the `dotnet` CLI would), then resolves the two
// hostfxr entry points this file needs. hostfxr_close is deliberately not resolved: the loaded
// runtime/delegate must stay valid for this process's whole lifetime, matching Microsoft's own
// native-hosting sample, which never closes the context either.
[[nodiscard]] bool LoadHostfxrUVE() {
    char buffer[4096];
    std::size_t bufferSize = sizeof(buffer) / sizeof(buffer[0]);
    if (const int result = get_hostfxr_path(buffer, &bufferSize, nullptr); result != 0) {
        std::fprintf(stderr, "uve_editor_cs: get_hostfxr_path failed (0x%x) - is a .NET runtime installed?\n",
                     result);
        return false;
    }

    void* const library = dlopen(buffer, RTLD_LAZY | RTLD_LOCAL);
    if (library == nullptr) {
        std::fprintf(stderr, "uve_editor_cs: dlopen(\"%s\") failed: %s\n", buffer, dlerror());
        return false;
    }

    g_hostfxrInitializeForRuntimeConfigUVE = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
        dlsym(library, "hostfxr_initialize_for_runtime_config"));
    g_hostfxrGetRuntimeDelegateUVE =
        reinterpret_cast<hostfxr_get_runtime_delegate_fn>(dlsym(library, "hostfxr_get_runtime_delegate"));
    return g_hostfxrInitializeForRuntimeConfigUVE != nullptr && g_hostfxrGetRuntimeDelegateUVE != nullptr;
}

// Initializes hostfxr against EditorCoreCs's published .runtimeconfig.json and resolves its
// UnmanagedCallersOnly TickFrame export. UVE_EDITOR_CS_PUBLISH_DIR is a compile definition set by
// Engine/App/CMakeLists.txt to the `dotnet publish` output directory its custom target produces.
[[nodiscard]] TickFrameManagedFnUVE LoadManagedTickFrameUVE() {
    const std::string runtimeConfigPath = std::string(UVE_EDITOR_CS_PUBLISH_DIR) + "/EditorCoreCs.runtimeconfig.json";
    const std::string assemblyPath = std::string(UVE_EDITOR_CS_PUBLISH_DIR) + "/EditorCoreCs.dll";

    hostfxr_handle contextHandle = nullptr;
    int result = g_hostfxrInitializeForRuntimeConfigUVE(runtimeConfigPath.c_str(), nullptr, &contextHandle);
    if (result != 0 || contextHandle == nullptr) {
        std::fprintf(stderr, "uve_editor_cs: hostfxr_initialize_for_runtime_config(\"%s\") failed (0x%x)\n",
                     runtimeConfigPath.c_str(), result);
        return nullptr;
    }

    load_assembly_and_get_function_pointer_fn loadAssemblyAndGetFunctionPointer = nullptr;
    result = g_hostfxrGetRuntimeDelegateUVE(contextHandle, hdt_load_assembly_and_get_function_pointer,
                                            reinterpret_cast<void**>(&loadAssemblyAndGetFunctionPointer));
    if (result != 0 || loadAssemblyAndGetFunctionPointer == nullptr) {
        std::fprintf(stderr, "uve_editor_cs: hostfxr_get_runtime_delegate failed (0x%x)\n", result);
        return nullptr;
    }

    TickFrameManagedFnUVE tickFrame = nullptr;
    result = loadAssemblyAndGetFunctionPointer(assemblyPath.c_str(), "UniVex.EditorCoreCs.EditorHostEntry, EditorCoreCs",
                                               "TickFrame", UNMANAGEDCALLERSONLY_METHOD, nullptr,
                                               reinterpret_cast<void**>(&tickFrame));
    if (result != 0 || tickFrame == nullptr) {
        std::fprintf(stderr, "uve_editor_cs: load_assembly_and_get_function_pointer(\"%s\") failed (0x%x)\n",
                     assemblyPath.c_str(), result);
        return nullptr;
    }
    return tickFrame;
}

} // namespace

int main(const int argc, char** argv) {
    if (!LoadHostfxrUVE()) {
        return 1;
    }
    const TickFrameManagedFnUVE tickFrame = LoadManagedTickFrameUVE();
    if (tickFrame == nullptr) {
        return 1;
    }

    UveEngineHandleUVE* const handle = uve_capi_create(argc, argv);
    if (handle == nullptr) {
        std::fprintf(stderr, "uve_editor_cs: uve_capi_create failed - see uve_editor_cs.log\n");
        return 1;
    }

    int frameLimit = -1;
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == "--frames" && index + 1 < argc) {
            frameLimit = std::atoi(argv[++index]);
        }
    }

    int frameNumber = 0;
    while (frameLimit < 0 || frameNumber < frameLimit) {
        if (uve_capi_tick_frame(handle) == 0) {
            break;
        }
        tickFrame(handle, 960.0F, 600.0F, frameNumber);
        ++frameNumber;
    }

    uve_capi_destroy(handle);
    return 0;
}
