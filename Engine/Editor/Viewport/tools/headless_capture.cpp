// tools/headless_capture.cpp
// -----------------------------------------------------------------------
// Renders the viewport off-screen into an FBO at an arbitrary size and
// writes the result out as a binary PPM. Used to prove the renderer
// actually draws — and to check the auto-adjusting grid and the resize
// path at sizes the display may not support — without a visible window.
//
// It renders through the same ViewportRenderPass the interactive app
// uses, so what lands in the PPM is what the app draws.
//
// Usage:
//   headless_capture --out frame.ppm [--width 1280] [--height 800]
//                    [--dist 14] [--yaw -0.62] [--pitch 0.42] [--no-cube]
// -----------------------------------------------------------------------
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "ViewportRenderPass.h"
#include "univex/camera/OrbitCamera.h"
#include "univex/gizmo/GizmoGeometry.h"
#include "univex/viewport/ViewportSettings.h"
#include "univex/render/GlApi.h"

#include "integration/WorldUveEntitySource.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/heap_allocator_uve.h"
#include "uve/scene/components/transform_component_uve.h"
#include "uve/scene/scene_graph_uve.h"
#include "uve/world/world_uve.h"

#include <GLFW/glfw3.h>

namespace {

struct Options {
    int width = 1280;
    int height = 800;
    float distance = 11.26f;
    float yaw = -0.7553f;
    float pitch = 0.4561f;
    bool drawCube = true;
    bool useDefaultCamera = true; // unless --yaw/--pitch/--dist override it
    bool orthographic = false;
    bool drawGrid = true;
    bool drawNavGizmo = true;
    bool drawTransformGizmo = true;
    std::string gizmoMode = "universal";
    std::string displayMode = "normal";
    int samples = 8;   // MSAA sample count, clamped to GL_MAX_SAMPLES
    float navSize = 0.f; // >0 overrides the orientation gizmo's pixel size
    std::string outputPath = "frame.ppm";
    // Proves the univex_viewport_engine_bridge integration: builds a real
    // UVE::World::WorldUVE with a handful of entities and draws them (via
    // WorldUveEntitySource) instead of the single hardcoded demo cube.
    bool engineDemo = false;
};

univex::gizmo::GizmoMode ParseGizmoMode(const std::string& name) {
    using univex::gizmo::GizmoMode;
    if (name == "select") return GizmoMode::Select;
    if (name == "move") return GizmoMode::Move;
    if (name == "rotate") return GizmoMode::Rotate;
    if (name == "scale") return GizmoMode::Scale;
    return GizmoMode::Universal;
}

univex::viewport::DisplayMode ParseDisplayMode(const std::string& name) {
    using univex::viewport::DisplayMode;
    if (name == "wireframe") return DisplayMode::Wireframe;
    if (name == "unshaded") return DisplayMode::Unshaded;
    return DisplayMode::Normal;
}

Options ParseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&](float fallback) {
            return (i + 1 < argc) ? std::strtof(argv[++i], nullptr) : fallback;
        };
        if (arg == "--width") options.width = static_cast<int>(next(1280.f));
        else if (arg == "--height") options.height = static_cast<int>(next(800.f));
        else if (arg == "--dist") { options.distance = next(11.26f); options.useDefaultCamera = false; }
        else if (arg == "--yaw") { options.yaw = next(-0.7553f); options.useDefaultCamera = false; }
        else if (arg == "--pitch") { options.pitch = next(0.4561f); options.useDefaultCamera = false; }
        else if (arg == "--no-cube") options.drawCube = false;
        else if (arg == "--no-grid") options.drawGrid = false;
        else if (arg == "--no-nav") options.drawNavGizmo = false;
        else if (arg == "--no-gizmo") options.drawTransformGizmo = false;
        else if (arg == "--ortho") options.orthographic = true;
        else if (arg == "--gizmo" && i + 1 < argc) options.gizmoMode = argv[++i];
        else if (arg == "--display" && i + 1 < argc) options.displayMode = argv[++i];
        else if (arg == "--samples") options.samples = static_cast<int>(next(8.f));
        else if (arg == "--nav-size") options.navSize = next(0.f);
        else if (arg == "--out" && i + 1 < argc) options.outputPath = argv[++i];
        else if (arg == "--engine-demo") options.engineDemo = true;
    }
    return options;
}

bool WritePpm(const std::string& path, int width, int height, const std::vector<unsigned char>& rgba) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return false;
    std::fprintf(file, "P6\n%d %d\n255\n", width, height);

    // glReadPixels returns bottom-up; PPM is top-down.
    std::vector<unsigned char> row(static_cast<std::size_t>(width) * 3);
    for (int y = height - 1; y >= 0; --y) {
        const unsigned char* src = rgba.data() + static_cast<std::size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            row[static_cast<std::size_t>(x) * 3 + 0] = src[x * 4 + 0];
            row[static_cast<std::size_t>(x) * 3 + 1] = src[x * 4 + 1];
            row[static_cast<std::size_t>(x) * 3 + 2] = src[x * 4 + 2];
        }
        std::fwrite(row.data(), 1, row.size(), file);
    }
    std::fclose(file);
    return true;
}

const char* GlErrorName(GLenum error) {
    switch (error) {
        case GL_NO_ERROR: return "GL_NO_ERROR";
        case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
        default: return "GL_UNKNOWN_ERROR";
    }
}

} // namespace

int main(int argc, char** argv) {
    const Options options = ParseOptions(argc, argv);

    if (glfwInit() == GLFW_FALSE) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(64, 64, "univex headless", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "glfwCreateWindow failed (no GL context available)\n");
        glfwTerminate();
        return 2;
    }
    glfwMakeContextCurrent(window);

    glewExperimental = GL_TRUE;
    if (const GLenum status = glewInit(); status != GLEW_OK) {
        std::fprintf(stderr, "glewInit failed: %s\n", glewGetErrorString(status));
        return 3;
    }
    glGetError(); // discard GLEW's benign core-profile probe error

    std::printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));
    std::printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));

    // ---- off-screen target, so the capture size is not tied to any display --
    //
    // Multisampled, then resolved by a blit. Without this the capture has no
    // anti-aliasing at all: the line pass feathers its own edges analytically,
    // but the solid pass (arrow cones, scale cubes, rotation-ring annuli, the
    // nav balls) writes flat colour and has nothing to soften its silhouettes.
    // A screenshot taken without MSAA therefore looks markedly worse than what
    // the windowed demo, which has always requested samples, actually draws.
    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    const GLsizei samples = static_cast<GLsizei>(std::min(maxSamples, options.samples));

    GLuint msaaFbo = 0, msaaColorRb = 0, msaaDepthRb = 0;
    glGenFramebuffers(1, &msaaFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo);

    glGenRenderbuffers(1, &msaaColorRb);
    glBindRenderbuffer(GL_RENDERBUFFER, msaaColorRb);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, options.width, options.height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColorRb);

    glGenRenderbuffers(1, &msaaDepthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, msaaDepthRb);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8,
                                     options.width, options.height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaaDepthRb);

    if (const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER); status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "multisample framebuffer incomplete: 0x%x\n", status);
        return 4;
    }

    // Single-sample target the multisampled result resolves into, because
    // glReadPixels cannot read a multisampled buffer directly.
    GLuint fbo = 0, colorRb = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &colorRb);
    glBindRenderbuffer(GL_RENDERBUFFER, colorRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, options.width, options.height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorRb);
    if (const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER); status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "resolve framebuffer incomplete: 0x%x\n", status);
        return 4;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo);
    glEnable(GL_MULTISAMPLE);

    std::string error;
    auto pass = univex::app::ViewportRenderPass::Create(error);
    if (!pass.has_value()) {
        std::fprintf(stderr, "viewport init failed: %s\n", error.c_str());
        return 5;
    }
    {
        auto& settings = pass->Settings();
        settings.viewSceneGeometry = options.drawCube;
        settings.viewGrid = options.drawGrid;
        settings.viewGizmos = options.drawNavGizmo;
        settings.viewTransformGizmo = options.drawTransformGizmo;
        settings.display = ParseDisplayMode(options.displayMode);
        settings.projection = options.orthographic ? univex::viewport::ProjectionMode::Orthographic
                                                   : univex::viewport::ProjectionMode::Perspective;
    }
    pass->SetGizmoMode(ParseGizmoMode(options.gizmoMode));
    if (options.navSize > 0.f) pass->Style().navPixelSize = options.navSize;

    univex::camera::OrbitCamera camera;
    if (!options.useDefaultCamera) {
        camera.SetYawPitch(options.yaw, options.pitch);
        camera.SetDistance(options.distance);
    }
    camera.SetOrthographic(options.orthographic);

    // These must outlive pass->RenderFrame() below: WorldUVE's EntityManagerUVE
    // holds the allocator/event-system by reference, and entitySource wraps
    // world by reference too.
    UVE::Memory::HeapAllocatorUVE engineAllocator;
    UVE::Events::EventSystemUVE engineEventSystem;
    std::optional<UVE::World::WorldUVE> engineWorld;
    std::optional<univex::integration::WorldUveEntitySource> entitySource;
    if (options.engineDemo) {
        engineWorld.emplace(engineAllocator, engineEventSystem);
        UVE::Scene::SceneGraphUVE sceneGraph;
        auto& entityManager = engineWorld->GetEntityManagerUVE();
        // A small scattered handful of entities - just enough to prove the
        // bridge actually reads real WorldUVE entities, not the exact count
        // or arrangement of a real scene.
        const std::array<UVE::Math::Vector3UVE, 5> positions = {
            UVE::Math::Vector3UVE{0.0F, 0.0F, 0.0F},
            UVE::Math::Vector3UVE{4.0F, 0.0F, 2.0F},
            UVE::Math::Vector3UVE{-3.0F, 0.0F, -2.0F},
            UVE::Math::Vector3UVE{2.0F, 0.0F, -4.0F},
            UVE::Math::Vector3UVE{-4.0F, 0.0F, 3.0F},
        };
        for (const auto& position : positions) {
            const UVE::Scene::EntityUVE entity = entityManager.CreateEntityUVE();
            sceneGraph.AttachTransformUVE(entityManager, entity,
                                          UVE::Scene::TransformComponentUVE{position, {}, {1.0F, 1.0F, 1.0F}});
        }
        engineWorld->TickUVE(0.0F); // propagates local -> world transforms once
        entitySource.emplace(*engineWorld);
        pass->SetEntitySource(&*entitySource);
    }

    pass->RenderFrame(camera, options.width, options.height);

    // Resolve the multisampled colour into the readable single-sample buffer.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glBlitFramebuffer(0, 0, options.width, options.height,
                      0, 0, options.width, options.height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFinish();

    if (const GLenum glError = glGetError(); glError != GL_NO_ERROR) {
        std::fprintf(stderr, "GL error after render: %s\n", GlErrorName(glError));
        return 6;
    }

    std::vector<unsigned char> pixels(static_cast<std::size_t>(options.width) * options.height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, options.width, options.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    if (!WritePpm(options.outputPath, options.width, options.height, pixels)) {
        std::fprintf(stderr, "could not write %s\n", options.outputPath.c_str());
        return 7;
    }

    // ---- a few numbers so the capture is self-describing --------------------
    const float worldPerPixel = univex::render::WorldPerPixelAtPivot(camera, options.height);
    const float spacing = univex::render::ComputeDisplayGridSpacing(worldPerPixel, pass->Grid().Settings());
    const univex::render::GridLod lod = univex::render::ComputeGridLod(worldPerPixel, pass->Grid().Settings());

    std::size_t litPixels = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        // Anything brighter than the background gradient's lightest value.
        if (pixels[i] > 40 || pixels[i + 1] > 40 || pixels[i + 2] > 50) ++litPixels;
    }
    const double litFraction =
        static_cast<double>(litPixels) / (static_cast<double>(options.width) * options.height);

    std::printf("size        : %dx%d  (MSAA %dx)\n", options.width, options.height,
                static_cast<int>(samples));
    std::printf("camera      : dist=%.3f yaw=%.3f pitch=%.3f\n",
                static_cast<double>(camera.Distance()), static_cast<double>(camera.Yaw()),
                static_cast<double>(camera.Pitch()));
    std::printf("clip planes : near=%.5f far=%.1f\n",
                static_cast<double>(camera.NearPlane()), static_cast<double>(camera.FarPlane()));
    std::printf("world/pixel : %.6f\n", static_cast<double>(worldPerPixel));
    std::printf("lod level   : %.4f (fade %.4f, finest spacing %.4f)\n",
                static_cast<double>(lod.level), static_cast<double>(lod.fade),
                static_cast<double>(lod.finestSpacing));
    std::printf("grid spacing: %.4f world units\n", static_cast<double>(spacing));
    std::printf("lit pixels  : %.2f%%\n", litFraction * 100.0);
    std::printf("wrote       : %s\n", options.outputPath.c_str());

    pass.reset();
    glDeleteRenderbuffers(1, &msaaDepthRb);
    glDeleteRenderbuffers(1, &msaaColorRb);
    glDeleteFramebuffers(1, &msaaFbo);
    glDeleteRenderbuffers(1, &colorRb);
    glDeleteFramebuffers(1, &fbo);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
