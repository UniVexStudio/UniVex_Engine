// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

// GL/glext.h supplies the standard Khronos PFNGL*PROC function-pointer typedefs and GL_* enum
// constants this loader needs; it declares types only, never links anything, so including it
// here doesn't pull in GLEW/GLAD or any other loader library.
//
// PUBLIC CONTRACT, deliberately: this is the engine's one shared GL proc-table loader. The
// codebase's rule remains that no public header includes a GL header UNLESS the GL type surface
// is the contract itself - which is exactly the case here. There are two sanctioned consumers:
// GlRenderDeviceUVE (the primary owner) and the editor's MeshThumbnailRendererUVE, which draws a
// tiny self-contained preview directly with GL instead of standing up a full render device and
// used to keep a second hand-rolled copy of this loader (AUDIT 5.6). Any future consumer must be
// equally small and self-contained; anything larger belongs behind IRenderDeviceUVE instead.
#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif

namespace UVE::Render::Detail {

/// The GL function pointers GlRenderDeviceUVE needs beyond what the system's legacy OpenGL 1.1
/// <GL/gl.h> already declares directly and requires no loading for (glViewport, glClear,
/// glClearColor, glDrawArrays, glGetError, glGetIntegerv, glEnable/glDisable, ...). This is a
/// genuinely minimal, hand-rolled loader — exactly the ~20 buffer/VAO/shader/program functions
/// this increment's triangle needs — rather than a GLEW/GLAD dependency, matching this codebase's
/// "only build what the current consumer needs" discipline (see docs/CODING_STANDARDS.md,
/// Matrix4x4UVE's minimal API for the established precedent).
struct GlFunctionsUVE {
    PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
    PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
    PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
    PFNGLBUFFERDATAPROC glBufferData = nullptr;
    PFNGLBUFFERSUBDATAPROC glBufferSubData = nullptr;
    PFNGLBINDBUFFERBASEPROC glBindBufferBase = nullptr;

    PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
    PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;
    PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
    PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
    PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;

    PFNGLCREATESHADERPROC glCreateShader = nullptr;
    PFNGLDELETESHADERPROC glDeleteShader = nullptr;
    PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
    PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
    PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
    PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;

    PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
    PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
    PFNGLATTACHSHADERPROC glAttachShader = nullptr;
    PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
    PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
    PFNGLUSEPROGRAMPROC glUseProgram = nullptr;

    PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = nullptr;
    PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = nullptr;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer = nullptr;

    // Renderbuffer block: not used by GlRenderDeviceUVE itself; MeshThumbnailRendererUVE (the
    // contract's other sanctioned consumer) needs a depth renderbuffer for its scratch target.
    PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers = nullptr;
    PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers = nullptr;
    PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer = nullptr;
    PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage = nullptr;

    PFNGLACTIVETEXTUREPROC glActiveTexture = nullptr;

    // Uniform-related (Increment 21: ShaderManagerUVE's reflection + ICommandBufferUVE's
    // SetUniform*UVE calls) and program-binary-cache (Increment 21's on-disk shader cache).
    PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
    PFNGLUNIFORM1FPROC glUniform1f = nullptr;
    PFNGLUNIFORM1IPROC glUniform1i = nullptr;
    PFNGLUNIFORM3FVPROC glUniform3fv = nullptr;
    PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;
    PFNGLGETACTIVEUNIFORMPROC glGetActiveUniform = nullptr;
    PFNGLGETPROGRAMBINARYPROC glGetProgramBinary = nullptr;
    PFNGLPROGRAMBINARYPROC glProgramBinary = nullptr;

    // GL_KHR_debug (core since desktop GL 4.3; a common but not universally guaranteed GLES
    // extension - this engine's Android baseline is a fixed GLES 3.0 context, so this is expected
    // to stay null there). Deliberately excluded from IsCompleteUVE() below, matching
    // glGetProgramBinary/glProgramBinary's precedent immediately above: an optional capability a
    // caller checks for null before using, not a hard requirement for a usable render device. See
    // gl_error_check_uve.h for the manual glGetError()-polling fallback this engine relies on when
    // this pointer isn't available (Phase 2e GL error checking).
    PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback = nullptr;

    /// True iff every function pointer above loaded successfully (non-null).
    [[nodiscard]] bool IsCompleteUVE() const noexcept;
};

/// Loads every GlFunctionsUVE member by calling `getProcAddress(name)` once per function and
/// reinterpret_cast-ing the result to the matching PFNGL*PROC type — the universal pattern every
/// GL loader (hand-rolled or generated) uses, since a GL driver only exposes post-1.1 entry
/// points through this kind of dynamic lookup. `getProcAddress` is injected rather than calling
/// glfwGetProcAddress directly so this loader itself never includes GLFW; both callers
/// (GlRenderDeviceUVE and the editor's MeshThumbnailRendererUVE, each constructed only after a
/// GL context has been made current) pass a small wrapper around glfwGetProcAddress.
[[nodiscard]] GlFunctionsUVE LoadGlFunctionsUVE(void* (*getProcAddress)(const char*));

} // namespace UVE::Render::Detail
