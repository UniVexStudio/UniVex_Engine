// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// A minimal, additive, extern "C" boundary letting a non-C++ host (initially: a C# assembly
// embedded into the same OS process via .NET's native hosting API) drive EngineCoreUVE's
// lifecycle and read the same live viewport GL texture Engine/App/src/editor/main.cpp's
// ViewportPanelBackendUVE already produces for the C++ ImGui editor. Deliberately does not wrap
// EditorUVE/EditorBridgeUVE yet - that lands once a managed panel actually needs the bridge's
// snapshot/dispatch surface (Scene Hierarchy/Inspector). This first slice only has to prove two
// things end-to-end: the native-hosting call path works, and the real GL viewport texture this
// process's own GL context produced is visible/usable from managed code without any cross-context
// sharing (both sides run on the same thread, in the same process, against the same current GL
// context - see this library's own CMakeLists.txt comment for why that removes the sharing problem
// entirely rather than working around it).
//
// No native window/GL/ImGui type crosses this boundary as a parameter or return type - only
// opaque handles and plain scalars - matching the same "no raw handle crosses the boundary"
// discipline already established by EditorBridgeUVE for the non-viewport panels.

#include <cstdint>

extern "C" {

/// Opaque handle to one owned EngineCoreUVE + minimal viewport render pass. Never dereferenced by
/// a caller - passed back into every other uve_capi_* call unchanged.
typedef struct UveEngineHandleUVE UveEngineHandleUVE;

/// Constructs a real, non-headless EngineCoreUVE (Init + Load) and a minimal viewport render pass
/// (grid + orbit camera + one proxy cube per live scene entity, matching
/// Engine/App/src/editor/main.cpp's ViewportPanelBackendUVE, minus its EditorUVE-specific
/// selection/overlay wiring, which is out of scope until a managed panel needs it). Returns
/// nullptr if engine Init/Load or window/GL-context creation failed; check the process's own log
/// file (see EngineConfigUVE::logFilePath) for the reason, matching every other native entry
/// point's own failure-reporting convention.
[[nodiscard]] UveEngineHandleUVE* uve_capi_create(int argc, const char* const* argv);

/// Destroys the handle, tearing the engine (and its GL objects) down in the same safe order
/// Engine/App/src/editor/main.cpp already uses (viewport render pass destroyed while the window's
/// GL context is still valid, then EngineCoreUVE::Shutdown()). Safe to call once on any non-null
/// handle uve_capi_create returned; never call twice on the same handle.
void uve_capi_destroy(UveEngineHandleUVE* handle);

/// Pumps window events and runs exactly one EngineCoreUVE::TickFrameUVE(). Returns 0 once the
/// native window's close button (or OS close request) has been observed - the caller should stop
/// calling this and destroy the handle - or 1 to keep running.
[[nodiscard]] int uve_capi_tick_frame(UveEngineHandleUVE* handle);

/// One rendered viewport frame's live GL texture and the size actually rendered. gl_texture is 0
/// when nothing was ready to render this call (mirrors ViewportPanelRendererUVE's own "0 means not
/// ready yet" convention) - never a stale handle from a prior frame.
typedef struct {
    std::uint32_t gl_texture;
    float used_width;
    float used_height;
} UveViewportFrameUVE;

/// Renders the real grid/orbit-camera/proxy-cube viewport content into an internally owned
/// MSAA-then-resolve framebuffer pair sized to (avail_width, avail_height) - recreated only when
/// that size changes, matching ViewportPanelBackendUVE's own resize-on-demand behavior - and
/// returns the resolved 2D texture's live GL name. Must be called on the same thread that holds
/// the engine's current GL context (the same thread uve_capi_tick_frame is called from).
[[nodiscard]] UveViewportFrameUVE uve_capi_render_viewport(UveEngineHandleUVE* handle, float avail_width,
                                                            float avail_height);

/// A native trampoline invoked once per frame, immediately before the window back buffer is
/// presented (see EngineCoreUVE::SetPostRenderCallbackUVE's own doc comment) - the correct place
/// for managed code to render into the default framebuffer, since anything drawn after the next
/// frame's own clear+present cycle begins would never reach the screen. `userdata` is passed back
/// unchanged; ownership stays with the caller.
typedef void (*UvePostRenderCallbackUVE)(void* userdata);

/// Registers (or clears, by passing callback == nullptr) the post-render callback. Only one
/// callback may be registered at a time, matching EngineCoreUVE::SetPostRenderCallbackUVE's own
/// single-callback contract.
void uve_capi_set_post_render_callback(UveEngineHandleUVE* handle, UvePostRenderCallbackUVE callback,
                                        void* userdata);

/// Writes one line to this process's own engine log (UVE_INFO) so activity originating from
/// managed code is visible in the same log stream as native diagnostics, without exposing any
/// native logging type across the boundary. utf8_message must be a null-terminated UTF-8 string;
/// truncated to a bounded length if implausibly long, matching EditorBridgeUVE's own bounded-text
/// discipline for anything a non-C++ caller can supply.
void uve_capi_log_info(const char* utf8_message);

} // extern "C"
