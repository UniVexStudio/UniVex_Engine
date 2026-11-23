// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/GlBackend.h"
#include "prism/GpuBatch.h"
#include <GL/glcorearb.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace prism {

namespace {
constexpr const char* kDesktopGlslHeader = "#version 330 core\n";
constexpr const char* kEsGlslHeader = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
// GLES contexts report a version string that starts with this.
constexpr const char* kEsVersionPrefix = "OpenGL ES";
// GLES reads pixels back only as RGBA bytes; ARGB in a little-endian word is BGRA bytes.
constexpr int kRedShift = 16;
constexpr uint32_t kRedMask = 0x00FF0000U, kBlueMask = 0x000000FFU;
} // namespace

const char* glslHeaderFor(const char* glVersion) {
  const bool es = glVersion && std::strncmp(glVersion, kEsVersionPrefix, std::strlen(kEsVersionPrefix)) == 0;
  return es ? kEsGlslHeader : kDesktopGlslHeader;
}

std::string glslForContext(std::string_view source, const char* glVersion) {
  constexpr std::string_view kVersionDirective = "#version";
  const size_t start = source.find_first_not_of(" \t\r\n");
  if (start != std::string_view::npos && source.substr(start).starts_with(kVersionDirective)) {
    const size_t lineEnd = source.find('\n', start);
    source = lineEnd == std::string_view::npos ? std::string_view{} : source.substr(lineEnd + 1);
  }
  std::string result(glslHeaderFor(glVersion));
  result.append(source);
  return result;
}

#define PRISM_GL_FUNCS(X)                                                                                                         \
  X(PFNGLVIEWPORTPROC, glViewport) X(PFNGLCLEARCOLORPROC, glClearColor) X(PFNGLCLEARPROC, glClear)                             \
  X(PFNGLENABLEPROC, glEnable) X(PFNGLDISABLEPROC, glDisable) X(PFNGLSCISSORPROC, glScissor)                                   \
  X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) X(PFNGLREADPIXELSPROC, glReadPixels) X(PFNGLPIXELSTOREIPROC, glPixelStorei) \
  X(PFNGLTEXPARAMETERIPROC, glTexParameteri) X(PFNGLTEXIMAGE2DPROC, glTexImage2D) X(PFNGLGENTEXTURESPROC, glGenTextures)       \
  X(PFNGLBINDTEXTUREPROC, glBindTexture) X(PFNGLDELETETEXTURESPROC, glDeleteTextures) X(PFNGLACTIVETEXTUREPROC, glActiveTexture) \
  X(PFNGLDRAWELEMENTSPROC, glDrawElements) X(PFNGLCREATESHADERPROC, glCreateShader) X(PFNGLSHADERSOURCEPROC, glShaderSource) \
  X(PFNGLCOMPILESHADERPROC, glCompileShader) X(PFNGLGETSHADERIVPROC, glGetShaderiv) X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
  X(PFNGLCREATEPROGRAMPROC, glCreateProgram) X(PFNGLATTACHSHADERPROC, glAttachShader) X(PFNGLLINKPROGRAMPROC, glLinkProgram)     \
  X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) X(PFNGLUSEPROGRAMPROC, glUseProgram) \
  X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) X(PFNGLUNIFORM1IPROC, glUniform1i) X(PFNGLUNIFORM1FPROC, glUniform1f)  \
  X(PFNGLUNIFORM2FPROC, glUniform2f) X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
  X(PFNGLGENBUFFERSPROC, glGenBuffers) X(PFNGLBINDBUFFERPROC, glBindBuffer) X(PFNGLBUFFERDATAPROC, glBufferData)               \
  X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)       \
  X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                                \
  X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)       \
  X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer) X(PFNGLDELETESHADERPROC, glDeleteShader)                                      \
  X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                              \
  X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays) X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                    \
  X(PFNGLGETSTRINGPROC, glGetString)

struct GlBackend::Impl {
#define X(T, N) T N = nullptr;
  PRISM_GL_FUNCS(X)
#undef X
  PFNGLBUFFERSUBDATAPROC glBufferSubData = nullptr;
  PFNGLTEXSUBIMAGE2DPROC glTexSubImage2D = nullptr;
  struct ImageUploadCache {
    const uint32_t* argb = nullptr;
    int width = 0;
    int height = 0;
    uint64_t hash = 0;
  };
  std::function<void()> present;
  bool blit = true;
  int w = 0, h = 0;
  float pixelScale = 1.0f;
  GLuint fbo = 0, colorTex = 0, prog = 0, vao = 0, vbo = 0, ebo = 0, atlasTex = 0;
  GLint uSize = -1, uH = -1, uTex = -1;
  GLsizeiptr vboCap = 0, eboCap = 0;
  std::vector<GLuint> imageTex;
  std::vector<ImageUploadCache> imageCache;
  GlyphAtlas atlas;
  uint64_t atlasVersion = 0, atlasGen = 0;
  GpuFrame frame;
  std::vector<uint32_t> readbuf, flipped;
  size_t drawCalls = 0;
  bool es = false;
  const char* glslHeader = kDesktopGlslHeader;

  static uint64_t hashPixels(const uint32_t* px, int width, int height) {
    uint64_t h = 1469598103934665603ULL;
    if (!px || width <= 0 || height <= 0) return h;
    const size_t n = size_t(width) * size_t(height);
    for (size_t k = 0; k < n; ++k) {
      h = (h ^ px[k]) * 1099511628211ULL;
    }
    return h;
  }

  bool load(Loader ld, std::string* err) {
#define X(T, N) if (!(N = reinterpret_cast<T>(ld(#N)))) { if (err) *err = "missing GL function " #N; return false; }
    PRISM_GL_FUNCS(X)
#undef X
    glBufferSubData = reinterpret_cast<PFNGLBUFFERSUBDATAPROC>(ld("glBufferSubData"));
    glTexSubImage2D = reinterpret_cast<PFNGLTEXSUBIMAGE2DPROC>(ld("glTexSubImage2D"));
    return true;
  }
  GLuint compile(GLenum type, const char* src, std::string* err) {
    GLuint s = glCreateShader(type);
    const char* parts[] = {glslHeader, src};
    glShaderSource(s, 2, parts, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof log, nullptr, log); if (err) *err = std::string("shader: ") + log; glDeleteShader(s); return 0; }
    return s;
  }
  void makeTarget() {
    if (colorTex) glDeleteTextures(1, &colorTex);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
  }
  GLuint tex2d(GLint filter) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
  }
};

GlBackend::GlBackend() : m(new Impl) {}

GlBackend::~GlBackend() {
  if (!m || !m->glDeleteTextures) return;
  if (m->colorTex) m->glDeleteTextures(1, &m->colorTex);
  if (m->atlasTex) m->glDeleteTextures(1, &m->atlasTex);
  for (GLuint t : m->imageTex) m->glDeleteTextures(1, &t);
  if (m->fbo) m->glDeleteFramebuffers(1, &m->fbo);
  if (m->vbo) m->glDeleteBuffers(1, &m->vbo);
  if (m->ebo) m->glDeleteBuffers(1, &m->ebo);
  if (m->vao) m->glDeleteVertexArrays(1, &m->vao);
  if (m->prog) m->glDeleteProgram(m->prog);
}

std::shared_ptr<GlBackend> GlBackend::create(Loader loader, std::function<void()> present, bool blitToScreen, std::string* error) {
  std::shared_ptr<GlBackend> b(new GlBackend());
  Impl& g = *b->m;
  if (!g.load(loader, error)) return nullptr;
  g.present = std::move(present);
  g.blit = blitToScreen;
  const auto* version = reinterpret_cast<const char*>(g.glGetString(GL_VERSION));
  g.glslHeader = glslHeaderFor(version);
  g.es = g.glslHeader == kEsGlslHeader;
  GLuint vs = g.compile(GL_VERTEX_SHADER, kReferenceVertexGlsl, error);
  GLuint fs = vs ? g.compile(GL_FRAGMENT_SHADER, kReferenceFragmentGlsl, error) : 0;
  if (!vs || !fs) return nullptr;
  g.prog = g.glCreateProgram();
  g.glAttachShader(g.prog, vs);
  g.glAttachShader(g.prog, fs);
  g.glLinkProgram(g.prog);
  g.glDeleteShader(vs);
  g.glDeleteShader(fs);
  GLint ok = 0;
  g.glGetProgramiv(g.prog, GL_LINK_STATUS, &ok);
  if (!ok) { char log[1024]; g.glGetProgramInfoLog(g.prog, sizeof log, nullptr, log); if (error) *error = std::string("link: ") + log; return nullptr; }
  g.uSize = g.glGetUniformLocation(g.prog, "uSize");
  g.uH = g.glGetUniformLocation(g.prog, "uH");
  g.uTex = g.glGetUniformLocation(g.prog, "uTex");
  g.glGenVertexArrays(1, &g.vao);
  g.glBindVertexArray(g.vao);
  g.glGenBuffers(1, &g.vbo);
  g.glGenBuffers(1, &g.ebo);
  g.glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
  g.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
  const GLsizei stride = sizeof(GpuVertex);
  g.glEnableVertexAttribArray(0); g.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GpuVertex, x));
  g.glEnableVertexAttribArray(1); g.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GpuVertex, u));
  g.glEnableVertexAttribArray(2); g.glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)offsetof(GpuVertex, color));
  g.glEnableVertexAttribArray(3); g.glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GpuVertex, s0));
  g.glEnableVertexAttribArray(4); g.glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(GpuVertex, s1));
  g.atlasTex = g.tex2d(GL_LINEAR);
  g.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  g.glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, g.atlas.size, g.atlas.size, 0, GL_RED, GL_UNSIGNED_BYTE, g.atlas.pixels.data());
  return b;
}

void GlBackend::resize(int w, int h) {
  if (w <= 0 || h <= 0 || (w == m->w && h == m->h)) return;
  m->w = w; m->h = h;
  m->makeTarget();
}

void GlBackend::setPixelScale(float scale) {
  m->pixelScale = (std::isfinite(scale) && scale > 0.0f) ? scale : 1.0f;
}
float GlBackend::pixelScale() const { return m->pixelScale; }
unsigned GlBackend::colorTexture() const { return m->colorTex; }
size_t GlBackend::lastDrawCalls() const { return m->drawCalls; }

void GlBackend::submit(const DrawList& list) {
  Impl& g = *m;
  const float s = (std::isfinite(g.pixelScale) && g.pixelScale > 0.0f) ? g.pixelScale : 1.0f;
  const int targetW = list.width > 0 ? std::max(1, int(std::lround(float(list.width) * s))) : 0;
  const int targetH = list.height > 0 ? std::max(1, int(std::lround(float(list.height) * s))) : 0;
  resize(targetW, targetH);
  if (!g.fbo) return;
  buildGpuFrame(list, g.atlas, g.frame, s);
  const GpuFrame& f = g.frame;

  g.glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
  g.glViewport(0, 0, g.w, g.h);
  g.glUseProgram(g.prog);
  g.glUniform2f(g.uSize, float(g.w), float(g.h));
  g.glUniform1f(g.uH, float(g.h));
  g.glUniform1i(g.uTex, 0);
  g.glActiveTexture(GL_TEXTURE0);

  g.glBindTexture(GL_TEXTURE_2D, g.atlasTex);
  if (g.atlasVersion != g.atlas.version) {
    g.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    g.glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, g.atlas.size, g.atlas.size, 0, GL_RED, GL_UNSIGNED_BYTE, g.atlas.pixels.data());
    g.atlasVersion = g.atlas.version;
  }
  while (g.imageTex.size() < f.images.size()) {
    g.imageTex.push_back(g.tex2d(GL_LINEAR));
    g.imageCache.push_back({});
  }
  for (size_t i = 0; i < f.images.size(); i++) {
    const auto& img = f.images[i];
    const uint64_t hash = Impl::hashPixels(img.argb, img.width, img.height);
    auto& cached = g.imageCache[i];
    if (cached.argb == img.argb && cached.width == img.width && cached.height == img.height && cached.hash == hash) {
      continue;
    }
    g.glBindTexture(GL_TEXTURE_2D, g.imageTex[i]);
    g.glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    // ARGB words are B,G,R,A in memory on little-endian hosts; uploaded as RGBA so the shader's .bgra swizzle fixes the order.
    if (g.glTexSubImage2D && cached.width == img.width && cached.height == img.height) {
      g.glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img.width, img.height, GL_RGBA, GL_UNSIGNED_BYTE, img.argb);
    } else {
      g.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.argb);
    }
    cached = {img.argb, img.width, img.height, hash};
  }

  g.glBindVertexArray(g.vao);
  g.glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
  const GLsizeiptr vbBytes = GLsizeiptr(f.vertices.size() * sizeof(GpuVertex));
  if (g.glBufferSubData && vbBytes > 0 && vbBytes <= g.vboCap) {
    g.glBufferSubData(GL_ARRAY_BUFFER, 0, vbBytes, f.vertices.data());
  } else {
    g.glBufferData(GL_ARRAY_BUFFER, vbBytes, f.vertices.data(), GL_STREAM_DRAW);
    g.vboCap = vbBytes;
  }
  g.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
  const GLsizeiptr ebBytes = GLsizeiptr(f.indices.size() * sizeof(uint32_t));
  if (g.glBufferSubData && ebBytes > 0 && ebBytes <= g.eboCap) {
    g.glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, ebBytes, f.indices.data());
  } else {
    g.glBufferData(GL_ELEMENT_ARRAY_BUFFER, ebBytes, f.indices.data(), GL_STREAM_DRAW);
    g.eboCap = ebBytes;
  }

  g.drawCalls = 0;
  for (const GpuBatch& b : f.batches) {
    if (b.kind == GpuBatch::Kind::Clear) {
      g.glDisable(GL_SCISSOR_TEST);
      g.glClearColor(b.clearColor.r / 255.f, b.clearColor.g / 255.f, b.clearColor.b / 255.f, b.clearColor.a / 255.f);
      g.glClear(GL_COLOR_BUFFER_BIT);
      continue;
    }
    if (b.indexCount == 0 || b.sw <= 0 || b.sh <= 0) continue;
    g.glEnable(GL_SCISSOR_TEST);
    g.glScissor(b.sx, g.h - (b.sy + b.sh), b.sw, b.sh);
    if (b.replace) g.glDisable(GL_BLEND);
    else { g.glEnable(GL_BLEND); g.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); }
    g.glBindTexture(GL_TEXTURE_2D, b.texture == GpuBatch::Texture::Image ? g.imageTex[size_t(b.image)] : g.atlasTex);
    g.glDrawElements(GL_TRIANGLES, GLsizei(b.indexCount), GL_UNSIGNED_INT, (void*)(uintptr_t(b.firstIndex) * sizeof(uint32_t)));
    g.drawCalls++;
  }
  g.glDisable(GL_SCISSOR_TEST);
  g.glDisable(GL_BLEND);

  if (g.blit) {
    g.glBindFramebuffer(GL_READ_FRAMEBUFFER, g.fbo);
    g.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    g.glBlitFramebuffer(0, 0, g.w, g.h, 0, 0, g.w, g.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    g.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (g.present) g.present();
  }
}

PixelView GlBackend::readback() {
  Impl& g = *m;
  if (!g.fbo) return {};
  g.readbuf.resize(size_t(g.w) * size_t(g.h));
  g.flipped.resize(g.readbuf.size());
  g.glBindFramebuffer(GL_FRAMEBUFFER, g.fbo);
  g.glPixelStorei(GL_PACK_ALIGNMENT, 4);
  if (g.es) {
    g.glReadPixels(0, 0, g.w, g.h, GL_RGBA, GL_UNSIGNED_BYTE, g.readbuf.data());
    for (uint32_t& p : g.readbuf)   // RGBA bytes -> ARGB word: swap the red and blue bytes
      p = (p & ~(kRedMask | kBlueMask)) | ((p & kBlueMask) << kRedShift) | ((p & kRedMask) >> kRedShift);
  } else {
    g.glReadPixels(0, 0, g.w, g.h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, g.readbuf.data());
  }
  for (int y = 0; y < g.h; y++)
    std::memcpy(&g.flipped[size_t(y) * size_t(g.w)], &g.readbuf[size_t(g.h - 1 - y) * size_t(g.w)], size_t(g.w) * 4);
  return {g.flipped.data(), g.w, g.h, g.w};
}

} // namespace prism
