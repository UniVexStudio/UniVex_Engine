// univex/render/GlApi.h
// -----------------------------------------------------------------------
// Single include point for the OpenGL function loader, so swapping loaders
// is a one-file change instead of an edit in every translation unit.
//
// Defaults to GLEW because it is what this module was built and tested
// against. If UNIVEX already initialises GLAD (or its own loader), define
// UNIVEX_GL_LOADER_HEADER to that header at build time:
//
//     target_compile_definitions(univex_viewport_gl PUBLIC
//         UNIVEX_GL_LOADER_HEADER="glad/gl.h")
//
// Nothing else in this module includes a GL header directly.
// -----------------------------------------------------------------------
#pragma once

#if defined(__ANDROID__)
// OpenGL ES 3.2: the functions are the platform library's own, so no loader is needed. Shaders are
// written for desktop GLSL 3.30 and retargeted at compile time (prism::glslForContext).
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#define UNIVEX_GL_ES 1
#elif defined(UNIVEX_GL_LOADER_HEADER)
#include UNIVEX_GL_LOADER_HEADER
#else
#ifndef GLEW_NO_GLU
#define GLEW_NO_GLU
#endif
#ifndef GLEW_STATIC
#define GLEW_STATIC
#endif
#include <GL/glew.h>
#endif
