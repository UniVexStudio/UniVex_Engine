// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - OpenGL 3.3+ reference render backend. No window-system dependency: the host creates the GL context,
// makes it current on the calling thread, and passes a function loader (e.g. SDL_GL_GetProcAddress, glfwGetProcAddress,
// wglGetProcAddress). Use this file and GpuBatch.h as the worked example for a backend over your own graphics API:
// everything API-specific is in GlBackend.cpp (about 300 lines).
#pragma once
#include "RenderBackend.h"
#include <functional>
#include <string>
#include <string_view>

namespace prism {

// The GLSL header for a context whose glGetString(GL_VERSION) is `glVersion`: "#version 300 es" with default
// precisions on OpenGL ES, "#version 330 core" otherwise. Shader sources written for both leave out their own
// #version line and are compiled after this one.
const char* glslHeaderFor(const char* glVersion);
// `source` with its leading #version line (if any) replaced by glslHeaderFor(glVersion), so a shader written for
// desktop GLSL 3.30 also compiles on OpenGL ES 3.0.
std::string glslForContext(std::string_view source, const char* glVersion);

class GlBackend : public RenderBackend {
public:
  using Loader = void* (*)(const char* name);
  // Returns null (and fills *error) if the context is too old or shaders fail to compile.
  // `present` is optional: called after each frame has been blitted to the default framebuffer (e.g. SDL_GL_SwapWindow).
  // Pass blitToScreen=false for off-screen use (render-to-texture, tests): frames are only available through readback().
  static std::shared_ptr<GlBackend> create(Loader loader, std::function<void()> present = {}, bool blitToScreen = true, std::string* error = nullptr);
  ~GlBackend() override;
  const char* name() const override { return "opengl"; }
  void resize(int width, int height) override;
  void submit(const DrawList& list) override;
  PixelView readback() override;
  /// Sets the physical-pixel-per-logical-unit scale factor (e.g., Android/Retina DPI scale).
  void setPixelScale(float scale);
  float pixelScale() const;
  unsigned colorTexture() const;           // GL texture holding the last frame (RGBA8, bottom-up), for compositing in a host renderer
  size_t lastDrawCalls() const;
private:
  GlBackend();
  struct Impl;
  std::unique_ptr<Impl> m;
};

} // namespace prism
