// Engine/Tools/headless_framedump.c
// ---------------------------------------------------------------------------------------------
// LD_PRELOAD frame dumper for the headless GL toolchain (see headless_gl_toolchain.sh).
//
// With GLFW's Null platform + OSMesa there is no display to screenshot: the window is a block of
// memory and glfwSwapBuffers() is a no-op. This shim wraps glfwSwapBuffers() in ANY GLFW program
// of this repository (uve_editor, univex_editor, univex_viewport_demo, the game runtime) and
// writes the default framebuffer to a binary PPM on the frames you ask for, then optionally asks
// the window to close so the program exits through its normal shutdown path. No engine code is
// touched or linked - everything is resolved through glfwGetProcAddress at the first swap.
//
// Build (done by headless_gl_toolchain.sh):
//   cc -shared -fPIC -O2 -o "$HOME/toolchain/lib/libuve_framedump.so" headless_framedump.c -ldl
//
// Use:
//   UVE_FRAMEDUMP=/tmp/editor.ppm UVE_FRAMEDUMP_FRAMES=8 UVE_FRAMEDUMP_CLOSE=8
//       LD_PRELOAD="$HOME/toolchain/lib/libuve_framedump.so" build/Engine/App/uve_editor
//
//   UVE_FRAMEDUMP         output path (.ppm). Several frames -> "<stem>_<frame>.ppm".
//   UVE_FRAMEDUMP_FRAMES  1-based swap indices to dump: "8", "4,8,30" or "every:10". Default "3"
//                         (ImGui docking layouts need a couple of frames to settle).
//   UVE_FRAMEDUMP_CLOSE   after this swap call glfwSetWindowShouldClose(window, 1). Unset = never.
//   UVE_FRAMEDUMP_WINDOWS "main" (default) counts and dumps only the first window that swaps - the
//                         application window. ImGui multi-viewport mode swaps one extra GLFW window
//                         per floating panel each frame; "all" dumps those too as "<stem>_w<N>.ppm"
//                         (N = order of first appearance) on the same frames.
//   UVE_FRAMEDUMP_QUIET   silence the per-dump stderr line.
//
// PPM -> PNG: python3 -c "from PIL import Image; Image.open('/tmp/editor.ppm').save('/tmp/editor.png')"
// ---------------------------------------------------------------------------------------------
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct GLFWwindow GLFWwindow;
typedef void (*GLFWglproc)(void);
typedef void (*PFN_glfwSwapBuffers)(GLFWwindow*);
typedef GLFWglproc (*PFN_glfwGetProcAddress)(const char*);
typedef void (*PFN_glfwGetFramebufferSize)(GLFWwindow*, int*, int*);
typedef void (*PFN_glfwSetWindowShouldClose)(GLFWwindow*, int);

typedef void (*PFN_glGetIntegerv)(unsigned int, int*);
typedef void (*PFN_glBindFramebuffer)(unsigned int, unsigned int);
typedef void (*PFN_glReadBuffer)(unsigned int);
typedef void (*PFN_glPixelStorei)(unsigned int, int);
typedef void (*PFN_glReadPixels)(int, int, int, int, unsigned int, unsigned int, void*);
typedef void (*PFN_glFinish)(void);
typedef unsigned int (*PFN_glGetError)(void);

#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_RGB 0x1907
#define GL_UNSIGNED_BYTE 0x1401
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define GL_DOUBLEBUFFER 0x0C32

static struct {
    int initialised;
    PFN_glfwSwapBuffers realSwap;
    PFN_glfwGetFramebufferSize getFramebufferSize;
    PFN_glfwSetWindowShouldClose setShouldClose;
    PFN_glGetIntegerv getIntegerv;
    PFN_glBindFramebuffer bindFramebuffer;
    PFN_glReadBuffer readBuffer;
    PFN_glPixelStorei pixelStorei;
    PFN_glReadPixels readPixels;
    PFN_glFinish finish;
    PFN_glGetError getError;
    const char* outPath;
    const char* frameSpec;
    long closeAfter;
    long every;
    int quiet;
    int allWindows;
    long swapCount;          /* main-window swaps only = frame number */
    GLFWwindow* windows[64]; /* index 0 is the main window */
    int windowCount;
} g;

static int WindowIndex(GLFWwindow* window) {
    for (int i = 0; i < g.windowCount; ++i) {
        if (g.windows[i] == window) return i;
    }
    if (g.windowCount < 64) {
        g.windows[g.windowCount] = window;
        return g.windowCount++;
    }
    return -1;
}

static void* ResolveOrDie(const char* name) {
    void* symbol = dlsym(RTLD_DEFAULT, name);
    if (symbol == NULL) {
        fprintf(stderr, "[framedump] cannot resolve %s - is this a GLFW program?\n", name);
        exit(97);
    }
    return symbol;
}

static GLFWglproc GlProc(PFN_glfwGetProcAddress getProc, const char* name) {
    GLFWglproc proc = getProc(name);
    if (proc == NULL) {
        fprintf(stderr, "[framedump] no GL entry point %s in the current context\n", name);
        exit(98);
    }
    return proc;
}

static void InitOnce(void) {
    if (g.initialised) return;
    g.initialised = 1;
    g.realSwap = (PFN_glfwSwapBuffers) dlsym(RTLD_NEXT, "glfwSwapBuffers");
    if (g.realSwap == NULL) {
        fprintf(stderr, "[framedump] RTLD_NEXT glfwSwapBuffers missing: %s\n", dlerror());
        exit(96);
    }
    PFN_glfwGetProcAddress getProc = (PFN_glfwGetProcAddress) ResolveOrDie("glfwGetProcAddress");
    g.getFramebufferSize = (PFN_glfwGetFramebufferSize) ResolveOrDie("glfwGetFramebufferSize");
    g.setShouldClose = (PFN_glfwSetWindowShouldClose) ResolveOrDie("glfwSetWindowShouldClose");
    g.getIntegerv = (PFN_glGetIntegerv) GlProc(getProc, "glGetIntegerv");
    g.bindFramebuffer = (PFN_glBindFramebuffer) GlProc(getProc, "glBindFramebuffer");
    g.readBuffer = (PFN_glReadBuffer) GlProc(getProc, "glReadBuffer");
    g.pixelStorei = (PFN_glPixelStorei) GlProc(getProc, "glPixelStorei");
    g.readPixels = (PFN_glReadPixels) GlProc(getProc, "glReadPixels");
    g.finish = (PFN_glFinish) GlProc(getProc, "glFinish");
    g.getError = (PFN_glGetError) GlProc(getProc, "glGetError");

    g.outPath = getenv("UVE_FRAMEDUMP");
    g.frameSpec = getenv("UVE_FRAMEDUMP_FRAMES");
    if (g.frameSpec == NULL || g.frameSpec[0] == '\0') g.frameSpec = "3";
    if (strncmp(g.frameSpec, "every:", 6) == 0) g.every = strtol(g.frameSpec + 6, NULL, 10);
    const char* closeSpec = getenv("UVE_FRAMEDUMP_CLOSE");
    g.closeAfter = (closeSpec != NULL && closeSpec[0] != '\0') ? strtol(closeSpec, NULL, 10) : 0;
    g.quiet = getenv("UVE_FRAMEDUMP_QUIET") != NULL;
    const char* windowsSpec = getenv("UVE_FRAMEDUMP_WINDOWS");
    g.allWindows = windowsSpec != NULL && strcmp(windowsSpec, "all") == 0;
}

static int WantsFrame(long frame) {
    if (g.outPath == NULL || g.outPath[0] == '\0') return 0;
    if (g.every > 0) return frame % g.every == 0;
    const char* cursor = g.frameSpec;
    while (*cursor != '\0') {
        char* end = NULL;
        const long value = strtol(cursor, &end, 10);
        if (end == cursor) break;
        if (value == frame) return 1;
        cursor = end;
        while (*cursor == ',' || *cursor == ' ') ++cursor;
    }
    return 0;
}

static int FrameSpecIsSingle(void) {
    return g.every <= 0 && strchr(g.frameSpec, ',') == NULL;
}

static void BuildPath(char* out, size_t capacity, long frame, int windowIndex) {
    char suffix[64] = "";
    if (!FrameSpecIsSingle()) snprintf(suffix, sizeof suffix, "_%04ld", frame);
    if (windowIndex > 0) snprintf(suffix + strlen(suffix), sizeof suffix - strlen(suffix), "_w%d", windowIndex);
    if (suffix[0] == '\0') {
        snprintf(out, capacity, "%s", g.outPath);
        return;
    }
    const char* dot = strrchr(g.outPath, '.');
    const char* slash = strrchr(g.outPath, '/');
    if (dot == NULL || (slash != NULL && dot < slash)) {
        snprintf(out, capacity, "%s%s.ppm", g.outPath, suffix);
    } else {
        snprintf(out, capacity, "%.*s%s%s", (int) (dot - g.outPath), g.outPath, suffix, dot);
    }
}

static void Dump(GLFWwindow* window, long frame, int windowIndex) {
    int width = 0;
    int height = 0;
    g.getFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0) return;

    int previousReadFbo = 0;
    g.getIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFbo);
    int doubleBuffered = 0;
    g.getIntegerv(GL_DOUBLEBUFFER, &doubleBuffered);

    const size_t rowBytes = (size_t) width * 3U;
    unsigned char* pixels = (unsigned char*) malloc(rowBytes * (size_t) height);
    if (pixels == NULL) return;

    g.finish();
    g.bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    // Before the swap the finished frame sits in the back buffer of a double-buffered context;
    // OSMesa contexts are single-buffered and only have a front.
    g.readBuffer(doubleBuffered ? GL_BACK : GL_FRONT);
    g.pixelStorei(GL_PACK_ALIGNMENT, 1);
    while (g.getError() != 0) {}
    g.readPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    const unsigned int error = g.getError();
    g.bindFramebuffer(GL_READ_FRAMEBUFFER, (unsigned int) previousReadFbo);

    char path[4096];
    BuildPath(path, sizeof path, frame, windowIndex);
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "[framedump] cannot open %s for writing\n", path);
        free(pixels);
        return;
    }
    fprintf(file, "P6\n%d %d\n255\n", width, height);
    for (int y = height - 1; y >= 0; --y) { // GL rows are bottom-up, PPM is top-down
        fwrite(pixels + (size_t) y * rowBytes, 1, rowBytes, file);
    }
    fclose(file);
    free(pixels);
    if (!g.quiet) {
        fprintf(stderr, "[framedump] frame %ld -> %s (%dx%d)%s\n", frame, path, width, height,
                error != 0 ? " [glReadPixels raised a GL error]" : "");
    }
}

void glfwSwapBuffers(GLFWwindow* window) {
    InitOnce();
    const int windowIndex = WindowIndex(window);
    if (windowIndex == 0) {
        const long frame = ++g.swapCount;
        if (WantsFrame(frame)) Dump(window, frame, 0);
        g.realSwap(window);
        if (g.closeAfter > 0 && frame >= g.closeAfter) g.setShouldClose(window, 1);
        return;
    }
    // Secondary (ImGui viewport) windows swap after the main one within the same frame, so they
    // belong to the frame the main window is about to start: report them as g.swapCount + 1.
    if (g.allWindows && windowIndex > 0 && WantsFrame(g.swapCount + 1)) Dump(window, g.swapCount + 1, windowIndex);
    g.realSwap(window);
}
