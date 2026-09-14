// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

using System;
using System.Runtime.InteropServices;
using ImGuiNET;

namespace UniVex.EditorCoreCs;

/// A minimal OpenGL 3.3-core renderer for ImGui.NET draw data, written directly against the
/// already-current GL context (see GlProc.cs) rather than depending on any windowing/graphics
/// package - this project's C++ side already owns the window/context via WindowManagerUVE, so
/// C# only ever draws into a context someone else created and made current, never its own.
/// Deliberately small: no vertex-array/program state save-restore (this is currently the only GL
/// draw call issued after uve_capi_render_viewport returns each frame, so there is nothing to
/// preserve), no docking/viewports branch, no clipboard/IME/mouse-cursor plumbing yet - those are
/// Phase 1+ concerns once real panel interaction is wired up.
internal sealed class ImGuiRendererUVE {
    private const uint GlArrayBuffer = 0x8892;
    private const uint GlElementArrayBuffer = 0x8893;
    private const uint GlStreamDraw = 0x88E0;
    private const uint GlFragmentShader = 0x8B30;
    private const uint GlVertexShader = 0x8B31;
    private const uint GlCompileStatus = 0x8B81;
    private const uint GlLinkStatus = 0x8B82;
    private const uint GlFloat = 0x1406;
    private const uint GlUnsignedByte = 0x1401;
    private const uint GlUnsignedShort = 0x1403;
    private const uint GlTexture2D = 0x0DE1;
    private const uint GlTexture0 = 0x84C0;
    private const uint GlTextureMinFilter = 0x2801;
    private const uint GlTextureMagFilter = 0x2800;
    private const uint GlLinear = 0x2601;
    private const uint GlRgba = 0x1908;
    private const uint GlBlend = 0x0BE2;
    private const uint GlScissorTest = 0x0C11;
    private const uint GlDepthTest = 0x0B71;
    private const uint GlCullFace = 0x0B44;
    private const uint GlSrcAlpha = 0x0302;
    private const uint GlOneMinusSrcAlpha = 0x0303;
    private const uint GlTriangles = 0x0004;
    private const uint GlUnpackRowLength = 0x0CF2;

    private uint _program;
    private int _uniformProjection;
    private int _uniformTexture;
    private int _attribPosition;
    private int _attribUv;
    private int _attribColor;
    private uint _vao;
    private uint _vbo;
    private uint _ebo;
    private uint _fontTexture;
    private bool _initialized;

    public void EnsureInitializedUVE(ImGuiIOPtr io) {
        if (_initialized) {
            return;
        }
        GlProc.EnsureLoaded();
        CreateProgramUVE();
        CreateBuffersUVE();
        CreateFontsTextureUVE(io);
        _initialized = true;
    }

    private void CreateProgramUVE() {
        const string vertexSource = "#version 330 core\n" +
            "uniform mat4 uProjection;\n" +
            "layout(location = 0) in vec2 aPosition;\n" +
            "layout(location = 1) in vec2 aUv;\n" +
            "layout(location = 2) in vec4 aColor;\n" +
            "out vec2 vUv;\n" +
            "out vec4 vColor;\n" +
            "void main() {\n" +
            "    vUv = aUv;\n" +
            "    vColor = aColor;\n" +
            "    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);\n" +
            "}\n";
        const string fragmentSource = "#version 330 core\n" +
            "uniform sampler2D uTexture;\n" +
            "in vec2 vUv;\n" +
            "in vec4 vColor;\n" +
            "out vec4 outColor;\n" +
            "void main() {\n" +
            "    outColor = vColor * texture(uTexture, vUv);\n" +
            "}\n";

        uint vertexShader = CompileShaderUVE(GlVertexShader, vertexSource);
        uint fragmentShader = CompileShaderUVE(GlFragmentShader, fragmentSource);

        _program = GlProc.CreateProgram();
        GlProc.AttachShader(_program, vertexShader);
        GlProc.AttachShader(_program, fragmentShader);
        GlProc.LinkProgram(_program);
        GlProc.GetProgramiv(_program, GlLinkStatus, out int linked);
        if (linked == 0) {
            throw new InvalidOperationException("ImGuiRendererUVE: shader program link failed");
        }
        GlProc.DeleteShader(vertexShader);
        GlProc.DeleteShader(fragmentShader);

        _uniformProjection = GlProc.GetUniformLocation(_program, "uProjection");
        _uniformTexture = GlProc.GetUniformLocation(_program, "uTexture");
        _attribPosition = GlProc.GetAttribLocation(_program, "aPosition");
        _attribUv = GlProc.GetAttribLocation(_program, "aUv");
        _attribColor = GlProc.GetAttribLocation(_program, "aColor");
    }

    private static uint CompileShaderUVE(uint type, string source) {
        uint shader = GlProc.CreateShader(type);
        GlProc.ShaderSourceOne(shader, source);
        GlProc.CompileShader(shader);
        GlProc.GetShaderiv(shader, GlCompileStatus, out int compiled);
        if (compiled == 0) {
            byte[] log = new byte[1024];
            GlProc.GetShaderInfoLog(shader, log.Length, out int length, log);
            string message = System.Text.Encoding.UTF8.GetString(log, 0, length);
            throw new InvalidOperationException($"ImGuiRendererUVE: shader compile failed: {message}");
        }
        return shader;
    }

    private void CreateBuffersUVE() {
        _vao = GlProc.GenVertexArrayOne();
        _vbo = GlProc.GenBufferOne();
        _ebo = GlProc.GenBufferOne();
    }

    private unsafe void CreateFontsTextureUVE(ImGuiIOPtr io) {
        io.Fonts.GetTexDataAsRGBA32(out IntPtr pixels, out int width, out int height, out _);

        GlProc.glGenTextures(1, out _fontTexture);
        GlProc.glBindTexture(GlTexture2D, _fontTexture);
        GlProc.glTexParameteri(GlTexture2D, GlTextureMinFilter, (int)GlLinear);
        GlProc.glTexParameteri(GlTexture2D, GlTextureMagFilter, (int)GlLinear);
        GlProc.PixelStorei(GlUnpackRowLength, 0);
        GlProc.glTexImage2D(GlTexture2D, 0, (int)GlRgba, width, height, 0, GlRgba, GlUnsignedByte, pixels);

        io.Fonts.SetTexID(new IntPtr((long)_fontTexture));
        io.Fonts.ClearTexData();
    }

    public unsafe void RenderUVE(ImDrawDataPtr drawData, int framebufferWidth, int framebufferHeight) {
        if (framebufferWidth <= 0 || framebufferHeight <= 0) {
            return;
        }

        GlProc.glEnable(GlBlend);
        GlProc.glBlendFunc(GlSrcAlpha, GlOneMinusSrcAlpha);
        GlProc.glDisable(GlCullFace);
        GlProc.glDisable(GlDepthTest);
        GlProc.glEnable(GlScissorTest);
        GlProc.glViewport(0, 0, framebufferWidth, framebufferHeight);

        float left = drawData.DisplayPos.X;
        float right = drawData.DisplayPos.X + drawData.DisplaySize.X;
        float top = drawData.DisplayPos.Y;
        float bottom = drawData.DisplayPos.Y + drawData.DisplaySize.Y;
        float[] orthoProjection = {
            2.0f / (right - left), 0f, 0f, 0f,
            0f, 2.0f / (top - bottom), 0f, 0f,
            0f, 0f, -1f, 0f,
            (right + left) / (left - right), (top + bottom) / (bottom - top), 0f, 1f,
        };

        GlProc.UseProgram(_program);
        GlProc.Uniform1i(_uniformTexture, 0);
        GlProc.UniformMatrix4fv(_uniformProjection, 1, 0, orthoProjection);
        GlProc.ActiveTexture(GlTexture0);

        GlProc.BindVertexArray(_vao);
        GlProc.BindBuffer(GlArrayBuffer, _vbo);
        GlProc.BindBuffer(GlElementArrayBuffer, _ebo);
        GlProc.EnableVertexAttribArray((uint)_attribPosition);
        GlProc.EnableVertexAttribArray((uint)_attribUv);
        GlProc.EnableVertexAttribArray((uint)_attribColor);
        int vertexStride = sizeof(ImDrawVert);
        GlProc.VertexAttribPointer((uint)_attribPosition, 2, GlFloat, 0, vertexStride, (IntPtr)0);
        GlProc.VertexAttribPointer((uint)_attribUv, 2, GlFloat, 0, vertexStride, (IntPtr)8);
        GlProc.VertexAttribPointer((uint)_attribColor, 4, GlUnsignedByte, 1, vertexStride, (IntPtr)16);

        System.Numerics.Vector2 clipOffset = drawData.DisplayPos;
        for (int listIndex = 0; listIndex < drawData.CmdListsCount; ++listIndex) {
            ImDrawListPtr commandList = drawData.CmdLists[listIndex];

            GlProc.BufferData(GlArrayBuffer, (IntPtr)(commandList.VtxBuffer.Size * sizeof(ImDrawVert)),
                commandList.VtxBuffer.Data, GlStreamDraw);
            GlProc.BufferData(GlElementArrayBuffer, (IntPtr)(commandList.IdxBuffer.Size * sizeof(ushort)),
                commandList.IdxBuffer.Data, GlStreamDraw);

            for (int commandIndex = 0; commandIndex < commandList.CmdBuffer.Size; ++commandIndex) {
                ImDrawCmdPtr command = commandList.CmdBuffer[commandIndex];
                float clipMinX = command.ClipRect.X - clipOffset.X;
                float clipMinY = command.ClipRect.Y - clipOffset.Y;
                float clipMaxX = command.ClipRect.Z - clipOffset.X;
                float clipMaxY = command.ClipRect.W - clipOffset.Y;
                if (clipMaxX <= clipMinX || clipMaxY <= clipMinY) {
                    continue;
                }
                GlProc.glScissor((int)clipMinX, (int)(framebufferHeight - clipMaxY),
                    (int)(clipMaxX - clipMinX), (int)(clipMaxY - clipMinY));
                GlProc.glBindTexture(GlTexture2D, unchecked((uint)command.TextureId.ToInt64()));
                GlProc.glDrawElements(GlTriangles, (int)command.ElemCount, GlUnsignedShort,
                    (IntPtr)((int)command.IdxOffset * sizeof(ushort)));
            }
        }

        GlProc.glDisable(GlScissorTest);
        GlProc.glDisable(GlBlend);
    }
}
