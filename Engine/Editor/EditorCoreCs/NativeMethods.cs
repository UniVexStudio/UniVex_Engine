// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using System;
using System.Reflection;
using System.Runtime.InteropServices;

namespace UniVex.EditorCoreCs;

/// P/Invoke declarations matching Engine/Editor/EditorCapi/include/uve/editor_capi/uve_engine_capi.h
/// exactly, field-for-field. `uve_engine_capi` is built STATIC (see that module's own CMakeLists.txt
/// comment on why - a real .so there would need every static library in this repo recompiled with
/// -fPIC) and linked directly into Engine/App/src/editor_cs's hosting executable, which is built
/// with exported symbols. The static constructor below resolves the nominal "uve_engine_capi"
/// library name to the current process's own symbol table instead of a real loaded shared object,
/// so these DllImport declarations still work unmodified.
internal static class NativeMethods {
    private const string LibraryName = "uve_engine_capi";

    static NativeMethods() {
        NativeLibrary.SetDllImportResolver(typeof(NativeMethods).Assembly, ResolveLibraryUVE);
    }

    private static IntPtr ResolveLibraryUVE(string libraryName, Assembly assembly, DllImportSearchPath? searchPath) {
        return libraryName == LibraryName ? NativeLibrary.GetMainProgramHandle() : IntPtr.Zero;
    }

    /// Matches UveViewportFrameUVE exactly: one uint32_t then two floats, standard sequential
    /// layout (no packing surprises - all three fields are already naturally aligned).
    [StructLayout(LayoutKind.Sequential)]
    public struct UveViewportFrame {
        public uint GlTexture;
        public float UsedWidth;
        public float UsedHeight;
    }

    [DllImport(LibraryName)]
    public static extern IntPtr uve_capi_create(int argc, string[] argv);

    [DllImport(LibraryName)]
    public static extern void uve_capi_destroy(IntPtr handle);

    [DllImport(LibraryName)]
    public static extern int uve_capi_tick_frame(IntPtr handle);

    [DllImport(LibraryName)]
    public static extern UveViewportFrame uve_capi_render_viewport(IntPtr handle, float availWidth, float availHeight);

    [DllImport(LibraryName)]
    public static extern void uve_capi_log_info([MarshalAs(UnmanagedType.LPUTF8Str)] string utf8Message);
}
