// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "gl_functions.h"

namespace UVE::Render::Detail {

namespace {



template <typename TFunctionPointer>
[[nodiscard]] TFunctionPointer LoadOne(void* (*getProcAddress)(const char*), const char* name) {
    return reinterpret_cast<TFunctionPointer>(getProcAddress(name));
}

} // namespace

bool GlFunctions::IsComplete() const noexcept {
    return glBlendEquation != nullptr && glStencilFuncSeparate != nullptr && glStencilOpSeparate != nullptr &&
           glStencilMaskSeparate != nullptr && glGenBuffers != nullptr && glDeleteBuffers != nullptr && glBindBuffer != nullptr &&
           glBufferData != nullptr && glBufferSubData != nullptr &&
#if !defined(__ANDROID__)
           glGetBufferSubData != nullptr &&
#endif
           glBindBufferBase != nullptr &&
           glGenVertexArrays != nullptr && glDeleteVertexArrays != nullptr && glBindVertexArray != nullptr &&
           glVertexAttribPointer != nullptr && glEnableVertexAttribArray != nullptr && glCreateShader != nullptr &&
           glDeleteShader != nullptr && glShaderSource != nullptr && glCompileShader != nullptr &&
           glGetShaderiv != nullptr && glGetShaderInfoLog != nullptr && glCreateProgram != nullptr &&
           glDeleteProgram != nullptr && glAttachShader != nullptr && glLinkProgram != nullptr &&
           glGetProgramiv != nullptr && glGetProgramInfoLog != nullptr && glUseProgram != nullptr &&
           glGenFramebuffers != nullptr && glDeleteFramebuffers != nullptr && glBindFramebuffer != nullptr &&
           glFramebufferTexture2D != nullptr && glCheckFramebufferStatus != nullptr &&
           glFramebufferRenderbuffer != nullptr && glGenRenderbuffers != nullptr &&
           glDeleteRenderbuffers != nullptr && glBindRenderbuffer != nullptr && glRenderbufferStorage != nullptr &&
           glActiveTexture != nullptr &&
           glGetUniformLocation != nullptr && glUniform1f != nullptr && glUniform1i != nullptr &&
           glUniform2fv != nullptr && glUniform3fv != nullptr && glUniform4fv != nullptr &&
           glUniformMatrix4fv != nullptr && glGetActiveUniform != nullptr;
}

GlFunctions LoadGlFunctions(void* (*getProcAddress)(const char*)) {
    GlFunctions functions;

    functions.glCopyBufferSubData =
        LoadOne<PFNGLCOPYBUFFERSUBDATAPROC>(getProcAddress, "glCopyBufferSubData");
    functions.glBlitFramebuffer = LoadOne<PFNGLBLITFRAMEBUFFERPROC>(getProcAddress, "glBlitFramebuffer");
    functions.glGenerateMipmap = LoadOne<PFNGLGENERATEMIPMAPPROC>(getProcAddress, "glGenerateMipmap");
    functions.glFenceSync = LoadOne<PFNGLFENCESYNCPROC>(getProcAddress, "glFenceSync");
    functions.glClientWaitSync = LoadOne<PFNGLCLIENTWAITSYNCPROC>(getProcAddress, "glClientWaitSync");
    functions.glDeleteSync = LoadOne<PFNGLDELETESYNCPROC>(getProcAddress, "glDeleteSync");
    functions.glGenQueries = LoadOne<PFNGLGENQUERIESPROC>(getProcAddress, "glGenQueries");
    functions.glDeleteQueries = LoadOne<PFNGLDELETEQUERIESPROC>(getProcAddress, "glDeleteQueries");
    functions.glBeginQuery = LoadOne<PFNGLBEGINQUERYPROC>(getProcAddress, "glBeginQuery");
    functions.glEndQuery = LoadOne<PFNGLENDQUERYPROC>(getProcAddress, "glEndQuery");
    functions.glGetQueryiv = LoadOne<PFNGLGETQUERYIVPROC>(getProcAddress, "glGetQueryiv");
    functions.glGetQueryObjectuiv =
        LoadOne<PFNGLGETQUERYOBJECTUIVPROC>(getProcAddress, "glGetQueryObjectuiv");
#if !defined(__ANDROID__)
    functions.glQueryCounter = LoadOne<PFNGLQUERYCOUNTERPROC>(getProcAddress, "glQueryCounter");
    functions.glGetQueryObjectui64v =
        LoadOne<PFNGLGETQUERYOBJECTUI64VPROC>(getProcAddress, "glGetQueryObjectui64v");
#endif
    functions.glBlendEquation = LoadOne<PFNGLBLENDEQUATIONPROC>(getProcAddress, "glBlendEquation");
    functions.glStencilFuncSeparate =
        LoadOne<PFNGLSTENCILFUNCSEPARATEPROC>(getProcAddress, "glStencilFuncSeparate");
    functions.glStencilOpSeparate =
        LoadOne<PFNGLSTENCILOPSEPARATEPROC>(getProcAddress, "glStencilOpSeparate");
    functions.glStencilMaskSeparate =
        LoadOne<PFNGLSTENCILMASKSEPARATEPROC>(getProcAddress, "glStencilMaskSeparate");
    functions.glGenBuffers = LoadOne<PFNGLGENBUFFERSPROC>(getProcAddress, "glGenBuffers");
    functions.glDeleteBuffers = LoadOne<PFNGLDELETEBUFFERSPROC>(getProcAddress, "glDeleteBuffers");
    functions.glBindBuffer = LoadOne<PFNGLBINDBUFFERPROC>(getProcAddress, "glBindBuffer");
    functions.glBufferData = LoadOne<PFNGLBUFFERDATAPROC>(getProcAddress, "glBufferData");
    functions.glBufferSubData = LoadOne<PFNGLBUFFERSUBDATAPROC>(getProcAddress, "glBufferSubData");
#if !defined(__ANDROID__)
    functions.glGetBufferSubData = LoadOne<PFNGLGETBUFFERSUBDATAPROC>(getProcAddress, "glGetBufferSubData");
#endif
    functions.glBindBufferBase = LoadOne<PFNGLBINDBUFFERBASEPROC>(getProcAddress, "glBindBufferBase");

    // M5a: optional compute entry points — may stay null on pre-4.3 contexts (see GlFunctions).
    functions.glDispatchCompute = LoadOne<PFNGLDISPATCHCOMPUTEPROC>(getProcAddress, "glDispatchCompute");
    functions.glMemoryBarrier = LoadOne<PFNGLMEMORYBARRIERPROC>(getProcAddress, "glMemoryBarrier");
    // M5b: optional image-unit binding (GL 4.2+) — null means no storage-image binds.
    functions.glBindImageTexture = LoadOne<PFNGLBINDIMAGETEXTUREPROC>(getProcAddress, "glBindImageTexture");
    functions.glDrawElementsIndirect =
        LoadOne<PFNGLDRAWELEMENTSINDIRECTPROC>(getProcAddress, "glDrawElementsIndirect");
    functions.glDrawArraysInstanced =
        LoadOne<PFNGLDRAWARRAYSINSTANCEDPROC>(getProcAddress, "glDrawArraysInstanced");
    functions.glDrawElementsInstanced =
        LoadOne<PFNGLDRAWELEMENTSINSTANCEDPROC>(getProcAddress, "glDrawElementsInstanced");

    functions.glGenVertexArrays = LoadOne<PFNGLGENVERTEXARRAYSPROC>(getProcAddress, "glGenVertexArrays");
    functions.glDeleteVertexArrays = LoadOne<PFNGLDELETEVERTEXARRAYSPROC>(getProcAddress, "glDeleteVertexArrays");
    functions.glBindVertexArray = LoadOne<PFNGLBINDVERTEXARRAYPROC>(getProcAddress, "glBindVertexArray");
    functions.glVertexAttribPointer =
        LoadOne<PFNGLVERTEXATTRIBPOINTERPROC>(getProcAddress, "glVertexAttribPointer");
    functions.glEnableVertexAttribArray =
        LoadOne<PFNGLENABLEVERTEXATTRIBARRAYPROC>(getProcAddress, "glEnableVertexAttribArray");

    functions.glCreateShader = LoadOne<PFNGLCREATESHADERPROC>(getProcAddress, "glCreateShader");
    functions.glDeleteShader = LoadOne<PFNGLDELETESHADERPROC>(getProcAddress, "glDeleteShader");
    functions.glShaderSource = LoadOne<PFNGLSHADERSOURCEPROC>(getProcAddress, "glShaderSource");
    functions.glCompileShader = LoadOne<PFNGLCOMPILESHADERPROC>(getProcAddress, "glCompileShader");
    functions.glGetShaderiv = LoadOne<PFNGLGETSHADERIVPROC>(getProcAddress, "glGetShaderiv");
    functions.glGetShaderInfoLog = LoadOne<PFNGLGETSHADERINFOLOGPROC>(getProcAddress, "glGetShaderInfoLog");

    functions.glCreateProgram = LoadOne<PFNGLCREATEPROGRAMPROC>(getProcAddress, "glCreateProgram");
    functions.glDeleteProgram = LoadOne<PFNGLDELETEPROGRAMPROC>(getProcAddress, "glDeleteProgram");
    functions.glAttachShader = LoadOne<PFNGLATTACHSHADERPROC>(getProcAddress, "glAttachShader");
    functions.glLinkProgram = LoadOne<PFNGLLINKPROGRAMPROC>(getProcAddress, "glLinkProgram");
    functions.glGetProgramiv = LoadOne<PFNGLGETPROGRAMIVPROC>(getProcAddress, "glGetProgramiv");
    functions.glGetProgramInfoLog = LoadOne<PFNGLGETPROGRAMINFOLOGPROC>(getProcAddress, "glGetProgramInfoLog");
    functions.glUseProgram = LoadOne<PFNGLUSEPROGRAMPROC>(getProcAddress, "glUseProgram");

    functions.glGenFramebuffers = LoadOne<PFNGLGENFRAMEBUFFERSPROC>(getProcAddress, "glGenFramebuffers");
    functions.glDeleteFramebuffers = LoadOne<PFNGLDELETEFRAMEBUFFERSPROC>(getProcAddress, "glDeleteFramebuffers");
    functions.glBindFramebuffer = LoadOne<PFNGLBINDFRAMEBUFFERPROC>(getProcAddress, "glBindFramebuffer");
    functions.glFramebufferTexture2D =
        LoadOne<PFNGLFRAMEBUFFERTEXTURE2DPROC>(getProcAddress, "glFramebufferTexture2D");
    functions.glCheckFramebufferStatus =
        LoadOne<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(getProcAddress, "glCheckFramebufferStatus");
    functions.glFramebufferRenderbuffer =
        LoadOne<PFNGLFRAMEBUFFERRENDERBUFFERPROC>(getProcAddress, "glFramebufferRenderbuffer");

    functions.glGenRenderbuffers = LoadOne<PFNGLGENRENDERBUFFERSPROC>(getProcAddress, "glGenRenderbuffers");
    functions.glDeleteRenderbuffers =
        LoadOne<PFNGLDELETERENDERBUFFERSPROC>(getProcAddress, "glDeleteRenderbuffers");
    functions.glBindRenderbuffer = LoadOne<PFNGLBINDRENDERBUFFERPROC>(getProcAddress, "glBindRenderbuffer");
    functions.glRenderbufferStorage =
        LoadOne<PFNGLRENDERBUFFERSTORAGEPROC>(getProcAddress, "glRenderbufferStorage");

    functions.glActiveTexture = LoadOne<PFNGLACTIVETEXTUREPROC>(getProcAddress, "glActiveTexture");
    functions.glCompressedTexImage2D =
        LoadOne<PFNGLCOMPRESSEDTEXIMAGE2DPROC>(getProcAddress, "glCompressedTexImage2D");
    // Tier 2.3 array/cubemap entries (kept out of IsComplete: optional capability).
    functions.glTexImage3D = LoadOne<PFNGLTEXIMAGE3DPROC>(getProcAddress, "glTexImage3D");
    functions.glCompressedTexImage3D =
        LoadOne<PFNGLCOMPRESSEDTEXIMAGE3DPROC>(getProcAddress, "glCompressedTexImage3D");
    functions.glFramebufferTextureLayer = LoadOne<PFNGLFRAMEBUFFERTEXTURELAYERPROC>(
        getProcAddress, "glFramebufferTextureLayer");
    // Tier 2.4/2.5 MRT entries (kept out of IsComplete: optional capability).
    functions.glDrawBuffers =
        LoadOne<PFNGLDRAWBUFFERSPROC>(getProcAddress, "glDrawBuffers");
    functions.glVertexAttribIPointer =
        LoadOne<PFNGLVERTEXATTRIBIPOINTERPROC>(getProcAddress, "glVertexAttribIPointer");
    functions.glClearBufferfv =
        LoadOne<PFNGLCLEARBUFFERFVPROC>(getProcAddress, "glClearBufferfv");
    functions.glInvalidateFramebuffer =
        LoadOne<PFNGLINVALIDATEFRAMEBUFFERPROC>(getProcAddress, "glInvalidateFramebuffer");

    functions.glGetUniformLocation = LoadOne<PFNGLGETUNIFORMLOCATIONPROC>(getProcAddress, "glGetUniformLocation");
    functions.glUniform1f = LoadOne<PFNGLUNIFORM1FPROC>(getProcAddress, "glUniform1f");
    functions.glUniform1i = LoadOne<PFNGLUNIFORM1IPROC>(getProcAddress, "glUniform1i");
    functions.glUniform2fv = LoadOne<PFNGLUNIFORM2FVPROC>(getProcAddress, "glUniform2fv");
    functions.glUniform3fv = LoadOne<PFNGLUNIFORM3FVPROC>(getProcAddress, "glUniform3fv");
    functions.glUniform4fv = LoadOne<PFNGLUNIFORM4FVPROC>(getProcAddress, "glUniform4fv");
    functions.glUniformMatrix4fv = LoadOne<PFNGLUNIFORMMATRIX4FVPROC>(getProcAddress, "glUniformMatrix4fv");
    functions.glGetActiveUniform = LoadOne<PFNGLGETACTIVEUNIFORMPROC>(getProcAddress, "glGetActiveUniform");
    functions.glGetProgramBinary = LoadOne<PFNGLGETPROGRAMBINARYPROC>(getProcAddress, "glGetProgramBinary");
    functions.glProgramBinary = LoadOne<PFNGLPROGRAMBINARYPROC>(getProcAddress, "glProgramBinary");
    functions.glProgramParameteri =
        LoadOne<PFNGLPROGRAMPARAMETERIPROC>(getProcAddress, "glProgramParameteri");

    functions.glGenSamplers = LoadOne<PFNGLGENSAMPLERSPROC>(getProcAddress, "glGenSamplers");
    functions.glDeleteSamplers = LoadOne<PFNGLDELETESAMPLERSPROC>(getProcAddress, "glDeleteSamplers");
    functions.glBindSampler = LoadOne<PFNGLBINDSAMPLERPROC>(getProcAddress, "glBindSampler");
    functions.glSamplerParameteri =
        LoadOne<PFNGLSAMPLERPARAMETERIPROC>(getProcAddress, "glSamplerParameteri");
    functions.glSamplerParameterf =
        LoadOne<PFNGLSAMPLERPARAMETERFPROC>(getProcAddress, "glSamplerParameterf");
    functions.glGetStringi = LoadOne<PFNGLGETSTRINGIPROC>(getProcAddress, "glGetStringi");

    functions.glDebugMessageCallback =
        LoadOne<PFNGLDEBUGMESSAGECALLBACKPROC>(getProcAddress, "glDebugMessageCallback");

    return functions;
}

} // namespace UVE::Render::Detail
