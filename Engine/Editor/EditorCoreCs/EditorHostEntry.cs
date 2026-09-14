// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using System;
using System.Runtime.InteropServices;

namespace UniVex.EditorCoreCs;

/// The managed side of the Phase 0 interop proof (see the plan checkpoint "C# (ImGui.NET) editor
/// migration - Phase 0"). Engine/App/src/editor_cs's native host loads this assembly through
/// hostfxr and calls TickFrame once per frame via its `UnmanagedCallersOnly` export, passing the
/// live `UveEngineHandleUVE*` it already owns.
///
/// This slice deliberately does not render anything with ImGui.NET yet - it only has to prove two
/// things end-to-end: that the hostfxr embedding + per-frame call path genuinely works, and that
/// the real GL viewport texture the native side's own current GL context produced is visible and
/// usable from managed code with zero cross-context sharing (both sides run on the same OS thread,
/// in the same process, against the same current GL context). Verification is therefore log-based
/// (a native UVE_INFO line, written through uve_capi_log_info, naming the frame number and the
/// live, non-zero GL texture id this call received) rather than a screenshot - the ImGui.NET
/// render pass that would make this visible on screen is the next slice (Phase 0b).
public static class EditorHostEntry {
    [UnmanagedCallersOnly(EntryPoint = "uve_managed_tick_frame")]
    public static void TickFrame(IntPtr engineHandle, float availWidth, float availHeight, int frameNumber) {
        try {
            NativeMethods.UveViewportFrame frame =
                NativeMethods.uve_capi_render_viewport(engineHandle, availWidth, availHeight);
            NativeMethods.uve_capi_log_info(
                $"EditorCoreCs.TickFrame: frame={frameNumber} glTexture={frame.GlTexture} " +
                $"usedSize={frame.UsedWidth}x{frame.UsedHeight}");
        } catch (Exception exception) {
            // An UnmanagedCallersOnly export must never let an exception unwind into native code -
            // log it through the same native log stream instead, matching this project's own
            // exception-boundary discipline at every other native entry point.
            NativeMethods.uve_capi_log_info($"EditorCoreCs.TickFrame: unhandled exception: {exception}");
        }
    }
}
