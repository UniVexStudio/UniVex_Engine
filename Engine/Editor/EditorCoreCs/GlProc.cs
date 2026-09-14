// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using System;
using System.Runtime.InteropServices;

namespace UniVex.EditorCoreCs;

/// Loads core-profile OpenGL function pointers against whatever GL context is already current on
/// this thread (the same context the native side's GlRenderDeviceUVE created and is already using
/// for uve_capi_render_viewport - see EditorHostEntry.cs). Only libGL.so.1's pre-1.2 entry points
/// (glGetString, and glXGetProcAddressARB itself) are real linkable symbols on Linux; every
/// GL 1.2+/core-profile function must be resolved through glXGetProcAddressARB at runtime, exactly
/// like GLEW does natively for uve_engine_capi - this is the same requirement, just implemented in
/// C# instead of pulling in a native GL-loader dependency.
internal static class GlProc {
    [DllImport("libGL.so.1")]
    private static extern IntPtr glXGetProcAddressARB([MarshalAs(UnmanagedType.LPUTF8Str)] string name);

    private static T Load<T>(string name) where T : Delegate {
        IntPtr address = glXGetProcAddressARB(name);
        if (address == IntPtr.Zero) {
            throw new InvalidOperationException($"GlProc: glXGetProcAddressARB(\"{name}\") returned null");
        }
        return Marshal.GetDelegateForFunctionPointer<T>(address);
    }

    public delegate uint GenBuffersOneFn();
    public delegate void BindBufferFn(uint target, uint buffer);
    public delegate void BufferDataFn(uint target, IntPtr size, IntPtr data, uint usage);
    public delegate void DeleteBufferOneFn(uint buffer);
    public delegate uint GenVertexArrayOneFn();
    public delegate void BindVertexArrayFn(uint array);
    public delegate void DeleteVertexArrayOneFn(uint array);
    public delegate void EnableVertexAttribArrayFn(uint index);
    public delegate void VertexAttribPointerFn(uint index, int size, uint type, byte normalized, int stride, IntPtr pointer);
    public delegate uint CreateShaderFn(uint type);
    public delegate void ShaderSourceOneFn(uint shader, string source);
    public delegate void CompileShaderFn(uint shader);
    public delegate void GetShaderivFn(uint shader, uint pname, out int result);
    public delegate void GetShaderInfoLogFn(uint shader, int maxLength, out int length, byte[] infoLog);
    public delegate uint CreateProgramFn();
    public delegate void AttachShaderFn(uint program, uint shader);
    public delegate void LinkProgramFn(uint program);
    public delegate void GetProgramivFn(uint program, uint pname, out int result);
    public delegate void DeleteShaderFn(uint shader);
    public delegate void UseProgramFn(uint program);
    public delegate int GetUniformLocationFn(uint program, string name);
    public delegate int GetAttribLocationFn(uint program, string name);
    public delegate void UniformMatrix4fvFn(int location, int count, byte transpose, float[] value);
    public delegate void Uniform1iFn(int location, int value);
    public delegate void ActiveTextureFn(uint texture);
    public delegate void PixelStoreiFn(uint pname, int param);

    public static GenBuffersOneFn GenBufferOne = null!;
    public static BindBufferFn BindBuffer = null!;
    public static BufferDataFn BufferData = null!;
    public static DeleteBufferOneFn DeleteBufferOne = null!;
    public static GenVertexArrayOneFn GenVertexArrayOne = null!;
    public static BindVertexArrayFn BindVertexArray = null!;
    public static DeleteVertexArrayOneFn DeleteVertexArrayOne = null!;
    public static EnableVertexAttribArrayFn EnableVertexAttribArray = null!;
    public static VertexAttribPointerFn VertexAttribPointer = null!;
    public static CreateShaderFn CreateShader = null!;
    public static ShaderSourceOneFn ShaderSourceOne = null!;
    public static CompileShaderFn CompileShader = null!;
    public static GetShaderivFn GetShaderiv = null!;
    public static GetShaderInfoLogFn GetShaderInfoLog = null!;
    public static CreateProgramFn CreateProgram = null!;
    public static AttachShaderFn AttachShader = null!;
    public static LinkProgramFn LinkProgram = null!;
    public static GetProgramivFn GetProgramiv = null!;
    public static DeleteShaderFn DeleteShader = null!;
    public static UseProgramFn UseProgram = null!;
    public static GetUniformLocationFn GetUniformLocation = null!;
    public static GetAttribLocationFn GetAttribLocation = null!;
    public static UniformMatrix4fvFn UniformMatrix4fv = null!;
    public static Uniform1iFn Uniform1i = null!;
    public static ActiveTextureFn ActiveTexture = null!;
    public static PixelStoreiFn PixelStorei = null!;

    // GL 1.1 entry points are real exported symbols on libGL.so.1 - no glXGetProcAddressARB needed.
    [DllImport("libGL.so.1")] public static extern void glGenTextures(int n, out uint textures);
    [DllImport("libGL.so.1")] public static extern void glBindTexture(uint target, uint texture);
    [DllImport("libGL.so.1")] public static extern void glTexParameteri(uint target, uint pname, int param);
    [DllImport("libGL.so.1")] public static extern void glTexImage2D(uint target, int level, int internalFormat, int width,
        int height, int border, uint format, uint type, IntPtr pixels);
    [DllImport("libGL.so.1")] public static extern void glDeleteTextures(int n, ref uint textures);
    [DllImport("libGL.so.1")] public static extern void glEnable(uint cap);
    [DllImport("libGL.so.1")] public static extern void glDisable(uint cap);
    [DllImport("libGL.so.1")] public static extern void glBlendFunc(uint sfactor, uint dfactor);
    [DllImport("libGL.so.1")] public static extern void glViewport(int x, int y, int width, int height);
    [DllImport("libGL.so.1")] public static extern void glScissor(int x, int y, int width, int height);
    [DllImport("libGL.so.1")] public static extern void glDrawElements(uint mode, int count, uint type, IntPtr indices);
    [DllImport("libGL.so.1")] public static extern void glClearColor(float r, float g, float b, float a);
    [DllImport("libGL.so.1")] public static extern void glClear(uint mask);
    [DllImport("libGL.so.1")] public static extern uint glGetError();

    private static bool s_loaded;

    public static void EnsureLoaded() {
        if (s_loaded) {
            return;
        }
        GenBufferOne = () => { uint id; GenBuffersRaw(1, out id); return id; };
        BindBuffer = Load<BindBufferFn>("glBindBuffer");
        BufferData = Load<BufferDataFn>("glBufferData");
        DeleteBufferOne = (id) => { uint local = id; DeleteBuffersRaw(1, ref local); };
        GenVertexArrayOne = () => { uint id; GenVertexArraysRaw(1, out id); return id; };
        BindVertexArray = Load<BindVertexArrayFn>("glBindVertexArray");
        DeleteVertexArrayOne = (id) => { uint local = id; DeleteVertexArraysRaw(1, ref local); };
        EnableVertexAttribArray = Load<EnableVertexAttribArrayFn>("glEnableVertexAttribArray");
        VertexAttribPointer = Load<VertexAttribPointerFn>("glVertexAttribPointer");
        CreateShader = Load<CreateShaderFn>("glCreateShader");
        ShaderSourceOne = ShaderSourceOneImpl;
        CompileShader = Load<CompileShaderFn>("glCompileShader");
        GetShaderiv = Load<GetShaderivFn>("glGetShaderiv");
        GetShaderInfoLog = Load<GetShaderInfoLogFn>("glGetShaderInfoLog");
        CreateProgram = Load<CreateProgramFn>("glCreateProgram");
        AttachShader = Load<AttachShaderFn>("glAttachShader");
        LinkProgram = Load<LinkProgramFn>("glLinkProgram");
        GetProgramiv = Load<GetProgramivFn>("glGetProgramiv");
        DeleteShader = Load<DeleteShaderFn>("glDeleteShader");
        UseProgram = Load<UseProgramFn>("glUseProgram");
        GetUniformLocation = Load<GetUniformLocationFn>("glGetUniformLocation");
        GetAttribLocation = Load<GetAttribLocationFn>("glGetAttribLocation");
        UniformMatrix4fv = Load<UniformMatrix4fvFn>("glUniformMatrix4fv");
        Uniform1i = Load<Uniform1iFn>("glUniform1i");
        ActiveTexture = Load<ActiveTextureFn>("glActiveTexture");
        PixelStorei = Load<PixelStoreiFn>("glPixelStorei");
        s_loaded = true;
    }

    private delegate void GenBuffersRawFn(int n, out uint buffers);
    private delegate void DeleteBuffersRawFn(int n, ref uint buffers);
    private delegate void GenVertexArraysRawFn(int n, out uint arrays);
    private delegate void DeleteVertexArraysRawFn(int n, ref uint arrays);
    private delegate void ShaderSourceRawFn(uint shader, int count, string[] sources, int[]? lengths);

    private static GenBuffersRawFn GenBuffersRaw = null!;
    private static DeleteBuffersRawFn DeleteBuffersRaw = null!;
    private static GenVertexArraysRawFn GenVertexArraysRaw = null!;
    private static DeleteVertexArraysRawFn DeleteVertexArraysRaw = null!;
    private static ShaderSourceRawFn ShaderSourceRaw = null!;

    static GlProc() {
        GenBuffersRaw = Load<GenBuffersRawFn>("glGenBuffers");
        DeleteBuffersRaw = Load<DeleteBuffersRawFn>("glDeleteBuffers");
        GenVertexArraysRaw = Load<GenVertexArraysRawFn>("glGenVertexArrays");
        DeleteVertexArraysRaw = Load<DeleteVertexArraysRawFn>("glDeleteVertexArrays");
        ShaderSourceRaw = Load<ShaderSourceRawFn>("glShaderSource");
    }

    private static void ShaderSourceOneImpl(uint shader, string source) {
        ShaderSourceRaw(shader, 1, new[] { source }, new[] { source.Length });
    }
}
