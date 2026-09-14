// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using System;
using System.Numerics;
using System.Runtime.InteropServices;
using ImGuiNET;

namespace UniVex.EditorCoreCs;

/// The managed side of the C#/ImGui.NET editor migration's Phase 0b (see the plan checkpoint
/// "C# (ImGui.NET) editor migration - Phase 0"). Phase 0 proved the hostfxr embedding and the
/// shared-GL-context viewport texture handoff work, verified through native log output alone.
/// This phase makes that real: a genuine ImGui.NET context, drawing a window with an
/// `ImGui.Image()` of the live viewport texture, rendered by ImGuiRendererUVE directly into the
/// same default framebuffer the native side is about to present - verified with a real screenshot
/// this time, not just a log line.
public static class EditorHostEntry {
    private static IntPtr s_imguiContext;
    private static readonly ImGuiRendererUVE Renderer = new();

    [UnmanagedCallersOnly(EntryPoint = "uve_managed_tick_frame")]
    public static void TickFrame(IntPtr engineHandle, float availWidth, float availHeight, int frameNumber) {
        try {
            if (s_imguiContext == IntPtr.Zero) {
                s_imguiContext = ImGui.CreateContext();
            }
            ImGui.SetCurrentContext(s_imguiContext);

            NativeMethods.UveViewportFrame viewport =
                NativeMethods.uve_capi_render_viewport(engineHandle, availWidth, availHeight);

            ImGuiIOPtr io = ImGui.GetIO();
            io.DisplaySize = new Vector2(availWidth, availHeight);
            io.DisplayFramebufferScale = Vector2.One;
            io.DeltaTime = 1.0f / 60.0f;

            Renderer.EnsureInitializedUVE(io);

            ImGui.NewFrame();
            ImGui.SetNextWindowPos(new Vector2(20, 20), ImGuiCond.FirstUseEver);
            ImGui.SetNextWindowSize(new Vector2(480, 340), ImGuiCond.FirstUseEver);
            if (ImGui.Begin("Viewport (C#/ImGui.NET) - Phase 0b")) {
                ImGui.Text($"frame {frameNumber}");
                ImGui.Text($"viewport texture = {viewport.GlTexture}, size = {viewport.UsedWidth}x{viewport.UsedHeight}");
                if (viewport.GlTexture != 0) {
                    ImGui.Image(new IntPtr((long)viewport.GlTexture), new Vector2(420, 240));
                }
            }
            ImGui.End();
            ImGui.Render();

            ImDrawDataPtr drawData = ImGui.GetDrawData();
            Renderer.RenderUVE(drawData, (int)availWidth, (int)availHeight);
            uint glError = GlProc.glGetError();

            if (frameNumber % 60 == 0) {
                NativeMethods.uve_capi_log_info(
                    $"EditorCoreCs.TickFrame: frame={frameNumber} glTexture={viewport.GlTexture} " +
                    $"usedSize={viewport.UsedWidth}x{viewport.UsedHeight} cmdLists={drawData.CmdListsCount} " +
                    $"totalVtx={drawData.TotalVtxCount} totalIdx={drawData.TotalIdxCount} glError={glError} " +
                    $"displaySize={io.DisplaySize} valid={drawData.Valid}");
            }
        } catch (Exception exception) {
            // An UnmanagedCallersOnly export must never let an exception unwind into native code -
            // log it through the same native log stream instead, matching this project's own
            // exception-boundary discipline at every other native entry point.
            NativeMethods.uve_capi_log_info($"EditorCoreCs.TickFrame: unhandled exception: {exception}");
        }
    }
}
