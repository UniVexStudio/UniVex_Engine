// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/rhi_opengl/gl_functions_uve.h"

namespace UVE::Render::Detail {

namespace {



template <typename TFunctionPointer>
[[nodiscard]] TFunctionPointer LoadOneUVE(void* (*getProcAddress)(const char*), const char* name) {
    return reinterpret_cast<TFunctionPointer>(getProcAddress(name));
}

} // namespace

bool GlFunctionsUVE::IsCompleteUVE() const noexcept {
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

GlFunctionsUVE LoadGlFunctionsUVE(void* (*getProcAddress)(const char*)) {
    GlFunctionsUVE functions;

    functions.glCopyBufferSubData =
        LoadOneUVE<PFNGLCOPYBUFFERSUBDATAPROC>(getProcAddress, "glCopyBufferSubData");
    functions.glBlitFramebuffer = LoadOneUVE<PFNGLBLITFRAMEBUFFERPROC>(getProcAddress, "glBlitFramebuffer");
    functions.glGenerateMipmap = LoadOneUVE<PFNGLGENERATEMIPMAPPROC>(getProcAddress, "glGenerateMipmap");
    functions.glFenceSync = LoadOneUVE<PFNGLFENCESYNCPROC>(getProcAddress, "glFenceSync");
    functions.glClientWaitSync = LoadOneUVE<PFNGLCLIENTWAITSYNCPROC>(getProcAddress, "glClientWaitSync");
    functions.glDeleteSync = LoadOneUVE<PFNGLDELETESYNCPROC>(getProcAddress, "glDeleteSync");
    functions.glGenQueries = LoadOneUVE<PFNGLGENQUERIESPROC>(getProcAddress, "glGenQueries");
    functions.glDeleteQueries = LoadOneUVE<PFNGLDELETEQUERIESPROC>(getProcAddress, "glDeleteQueries");
    functions.glBeginQuery = LoadOneUVE<PFNGLBEGINQUERYPROC>(getProcAddress, "glBeginQuery");
    functions.glEndQuery = LoadOneUVE<PFNGLENDQUERYPROC>(getProcAddress, "glEndQuery");
    functions.glGetQueryiv = LoadOneUVE<PFNGLGETQUERYIVPROC>(getProcAddress, "glGetQueryiv");
    functions.glGetQueryObjectuiv =
        LoadOneUVE<PFNGLGETQUERYOBJECTUIVPROC>(getProcAddress, "glGetQueryObjectuiv");
#if !defined(__ANDROID__)
    functions.glQueryCounter = LoadOneUVE<PFNGLQUERYCOUNTERPROC>(getProcAddress, "glQueryCounter");
    functions.glGetQueryObjectui64v =
        LoadOneUVE<PFNGLGETQUERYOBJECTUI64VPROC>(getProcAddress, "glGetQueryObjectui64v");
#endif
    functions.glBlendEquation = LoadOneUVE<PFNGLBLENDEQUATIONPROC>(getProcAddress, "glBlendEquation");
    functions.glStencilFuncSeparate =
        LoadOneUVE<PFNGLSTENCILFUNCSEPARATEPROC>(getProcAddress, "glStencilFuncSeparate");
    functions.glStencilOpSeparate =
        LoadOneUVE<PFNGLSTENCILOPSEPARATEPROC>(getProcAddress, "glStencilOpSeparate");
    functions.glStencilMaskSeparate =
        LoadOneUVE<PFNGLSTENCILMASKSEPARATEPROC>(getProcAddress, "glStencilMaskSeparate");
    functions.glGenBuffers = LoadOneUVE<PFNGLGENBUFFERSPROC>(getProcAddress, "glGenBuffers");
    functions.glDeleteBuffers = LoadOneUVE<PFNGLDELETEBUFFERSPROC>(getProcAddress, "glDeleteBuffers");
    functions.glBindBuffer = LoadOneUVE<PFNGLBINDBUFFERPROC>(getProcAddress, "glBindBuffer");
    functions.glBufferData = LoadOneUVE<PFNGLBUFFERDATAPROC>(getProcAddress, "glBufferData");
    functions.glBufferSubData = LoadOneUVE<PFNGLBUFFERSUBDATAPROC>(getProcAddress, "glBufferSubData");
#if !defined(__ANDROID__)
    functions.glGetBufferSubData = LoadOneUVE<PFNGLGETBUFFERSUBDATAPROC>(getProcAddress, "glGetBufferSubData");
#endif
    functions.glBindBufferBase = LoadOneUVE<PFNGLBINDBUFFERBASEPROC>(getProcAddress, "glBindBufferBase");

    // M5a: optional compute entry points — may stay null on pre-4.3 contexts (see GlFunctionsUVE).
    functions.glDispatchCompute = LoadOneUVE<PFNGLDISPATCHCOMPUTEPROC>(getProcAddress, "glDispatchCompute");
    functions.glMemoryBarrier = LoadOneUVE<PFNGLMEMORYBARRIERPROC>(getProcAddress, "glMemoryBarrier");
    // M5b: optional image-unit binding (GL 4.2+) — null means no storage-image binds.
    functions.glBindImageTexture = LoadOneUVE<PFNGLBINDIMAGETEXTUREPROC>(getProcAddress, "glBindImageTexture");
    functions.glDrawElementsIndirect =
        LoadOneUVE<PFNGLDRAWELEMENTSINDIRECTPROC>(getProcAddress, "glDrawElementsIndirect");

    functions.glGenVertexArrays = LoadOneUVE<PFNGLGENVERTEXARRAYSPROC>(getProcAddress, "glGenVertexArrays");
    functions.glDeleteVertexArrays = LoadOneUVE<PFNGLDELETEVERTEXARRAYSPROC>(getProcAddress, "glDeleteVertexArrays");
    functions.glBindVertexArray = LoadOneUVE<PFNGLBINDVERTEXARRAYPROC>(getProcAddress, "glBindVertexArray");
    functions.glVertexAttribPointer =
        LoadOneUVE<PFNGLVERTEXATTRIBPOINTERPROC>(getProcAddress, "glVertexAttribPointer");
    functions.glEnableVertexAttribArray =
        LoadOneUVE<PFNGLENABLEVERTEXATTRIBARRAYPROC>(getProcAddress, "glEnableVertexAttribArray");

    functions.glCreateShader = LoadOneUVE<PFNGLCREATESHADERPROC>(getProcAddress, "glCreateShader");
    functions.glDeleteShader = LoadOneUVE<PFNGLDELETESHADERPROC>(getProcAddress, "glDeleteShader");
    functions.glShaderSource = LoadOneUVE<PFNGLSHADERSOURCEPROC>(getProcAddress, "glShaderSource");
    functions.glCompileShader = LoadOneUVE<PFNGLCOMPILESHADERPROC>(getProcAddress, "glCompileShader");
    functions.glGetShaderiv = LoadOneUVE<PFNGLGETSHADERIVPROC>(getProcAddress, "glGetShaderiv");
    functions.glGetShaderInfoLog = LoadOneUVE<PFNGLGETSHADERINFOLOGPROC>(getProcAddress, "glGetShaderInfoLog");

    functions.glCreateProgram = LoadOneUVE<PFNGLCREATEPROGRAMPROC>(getProcAddress, "glCreateProgram");
    functions.glDeleteProgram = LoadOneUVE<PFNGLDELETEPROGRAMPROC>(getProcAddress, "glDeleteProgram");
    functions.glAttachShader = LoadOneUVE<PFNGLATTACHSHADERPROC>(getProcAddress, "glAttachShader");
    functions.glLinkProgram = LoadOneUVE<PFNGLLINKPROGRAMPROC>(getProcAddress, "glLinkProgram");
    functions.glGetProgramiv = LoadOneUVE<PFNGLGETPROGRAMIVPROC>(getProcAddress, "glGetProgramiv");
    functions.glGetProgramInfoLog = LoadOneUVE<PFNGLGETPROGRAMINFOLOGPROC>(getProcAddress, "glGetProgramInfoLog");
    functions.glUseProgram = LoadOneUVE<PFNGLUSEPROGRAMPROC>(getProcAddress, "glUseProgram");

    functions.glGenFramebuffers = LoadOneUVE<PFNGLGENFRAMEBUFFERSPROC>(getProcAddress, "glGenFramebuffers");
    functions.glDeleteFramebuffers = LoadOneUVE<PFNGLDELETEFRAMEBUFFERSPROC>(getProcAddress, "glDeleteFramebuffers");
    functions.glBindFramebuffer = LoadOneUVE<PFNGLBINDFRAMEBUFFERPROC>(getProcAddress, "glBindFramebuffer");
    functions.glFramebufferTexture2D =
        LoadOneUVE<PFNGLFRAMEBUFFERTEXTURE2DPROC>(getProcAddress, "glFramebufferTexture2D");
    functions.glCheckFramebufferStatus =
        LoadOneUVE<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(getProcAddress, "glCheckFramebufferStatus");
    functions.glFramebufferRenderbuffer =
        LoadOneUVE<PFNGLFRAMEBUFFERRENDERBUFFERPROC>(getProcAddress, "glFramebufferRenderbuffer");

    functions.glGenRenderbuffers = LoadOneUVE<PFNGLGENRENDERBUFFERSPROC>(getProcAddress, "glGenRenderbuffers");
    functions.glDeleteRenderbuffers =
        LoadOneUVE<PFNGLDELETERENDERBUFFERSPROC>(getProcAddress, "glDeleteRenderbuffers");
    functions.glBindRenderbuffer = LoadOneUVE<PFNGLBINDRENDERBUFFERPROC>(getProcAddress, "glBindRenderbuffer");
    functions.glRenderbufferStorage =
        LoadOneUVE<PFNGLRENDERBUFFERSTORAGEPROC>(getProcAddress, "glRenderbufferStorage");

    functions.glActiveTexture = LoadOneUVE<PFNGLACTIVETEXTUREPROC>(getProcAddress, "glActiveTexture");
    functions.glCompressedTexImage2D =
        LoadOneUVE<PFNGLCOMPRESSEDTEXIMAGE2DPROC>(getProcAddress, "glCompressedTexImage2D");
    // Tier 2.3 array/cubemap entries (kept out of IsCompleteUVE: optional capability).
    functions.glTexImage3D = LoadOneUVE<PFNGLTEXIMAGE3DPROC>(getProcAddress, "glTexImage3D");
    functions.glCompressedTexImage3D =
        LoadOneUVE<PFNGLCOMPRESSEDTEXIMAGE3DPROC>(getProcAddress, "glCompressedTexImage3D");
    functions.glFramebufferTextureLayer = LoadOneUVE<PFNGLFRAMEBUFFERTEXTURELAYERPROC>(
        getProcAddress, "glFramebufferTextureLayer");
    // Tier 2.4/2.5 MRT entries (kept out of IsCompleteUVE: optional capability).
    functions.glDrawBuffers =
        LoadOneUVE<PFNGLDRAWBUFFERSPROC>(getProcAddress, "glDrawBuffers");
    functions.glVertexAttribIPointer =
        LoadOneUVE<PFNGLVERTEXATTRIBIPOINTERPROC>(getProcAddress, "glVertexAttribIPointer");
    functions.glClearBufferfv =
        LoadOneUVE<PFNGLCLEARBUFFERFVPROC>(getProcAddress, "glClearBufferfv");
    functions.glInvalidateFramebuffer =
        LoadOneUVE<PFNGLINVALIDATEFRAMEBUFFERPROC>(getProcAddress, "glInvalidateFramebuffer");

    functions.glGetUniformLocation = LoadOneUVE<PFNGLGETUNIFORMLOCATIONPROC>(getProcAddress, "glGetUniformLocation");
    functions.glUniform1f = LoadOneUVE<PFNGLUNIFORM1FPROC>(getProcAddress, "glUniform1f");
    functions.glUniform1i = LoadOneUVE<PFNGLUNIFORM1IPROC>(getProcAddress, "glUniform1i");
    functions.glUniform2fv = LoadOneUVE<PFNGLUNIFORM2FVPROC>(getProcAddress, "glUniform2fv");
    functions.glUniform3fv = LoadOneUVE<PFNGLUNIFORM3FVPROC>(getProcAddress, "glUniform3fv");
    functions.glUniform4fv = LoadOneUVE<PFNGLUNIFORM4FVPROC>(getProcAddress, "glUniform4fv");
    functions.glUniformMatrix4fv = LoadOneUVE<PFNGLUNIFORMMATRIX4FVPROC>(getProcAddress, "glUniformMatrix4fv");
    functions.glGetActiveUniform = LoadOneUVE<PFNGLGETACTIVEUNIFORMPROC>(getProcAddress, "glGetActiveUniform");
    functions.glGetProgramBinary = LoadOneUVE<PFNGLGETPROGRAMBINARYPROC>(getProcAddress, "glGetProgramBinary");
    functions.glProgramBinary = LoadOneUVE<PFNGLPROGRAMBINARYPROC>(getProcAddress, "glProgramBinary");

    functions.glGenSamplers = LoadOneUVE<PFNGLGENSAMPLERSPROC>(getProcAddress, "glGenSamplers");
    functions.glDeleteSamplers = LoadOneUVE<PFNGLDELETESAMPLERSPROC>(getProcAddress, "glDeleteSamplers");
    functions.glBindSampler = LoadOneUVE<PFNGLBINDSAMPLERPROC>(getProcAddress, "glBindSampler");
    functions.glSamplerParameteri =
        LoadOneUVE<PFNGLSAMPLERPARAMETERIPROC>(getProcAddress, "glSamplerParameteri");
    functions.glSamplerParameterf =
        LoadOneUVE<PFNGLSAMPLERPARAMETERFPROC>(getProcAddress, "glSamplerParameterf");
    functions.glGetStringi = LoadOneUVE<PFNGLGETSTRINGIPROC>(getProcAddress, "glGetStringi");

    functions.glDebugMessageCallback =
        LoadOneUVE<PFNGLDEBUGMESSAGECALLBACKPROC>(getProcAddress, "glDebugMessageCallback");

    return functions;
}

} // namespace UVE::Render::Detail
