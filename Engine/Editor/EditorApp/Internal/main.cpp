// Engine/Editor/EditorApp/src/main.cpp
// -----------------------------------------------------------------------
// The editor shell's first interactive slice: a real desktop window,
// created and owned by Engine/Runtime/Window::WindowManagerUVE (not raw
// GLFW, unlike Engine/Editor/Viewport's own standalone demo app), driving
// the same ViewportRenderPass that demo and the headless-capture tool use,
// showing a real Engine/Runtime/World::WorldUVE's entities through the
// same WorldUveEntitySource bridge Engine/Editor/Viewport's headless
// --engine-demo mode already proved out.
//
// This is a scaffold, not "the whole editor" - there is no outliner,
// inspector, or content browser yet (those are separate future increments
// per the roadmap; a UI toolkit for them hasn't been chosen). What this
// proves is that Window + Input + World + the viewport renderer already
// work together in one real, live, running process - camera navigation is
// driven by polling Input::IInputSystemUVE every frame (this codebase's
// documented input contract), not GLFW callbacks, which is the one
// meaningful behavioral difference from Viewport's own demo app.
//
// Controls (a reduced set of Viewport's own demo bindings - Home-reset and
// nav-gizmo click-to-snap are not implemented here, since KeyCodeUVE has no
// Home/numpad-specific entries and gizmo picking is a separate increment):
//   left drag     orbit            middle/right drag   pan
//   scroll        dolly            Q/W/E/R/T            gizmo mode
//   Z             cycle shading    G/N/H/B/M            toggle overlays
//   7/1/3 (+Ctrl) standard views   5                    toggle ortho
//   O             focus origin     F                    focus selection
//   Escape        quit
// -----------------------------------------------------------------------
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ViewportRenderPass.h"
#include "integration/WorldUveEntitySource.h"
#include "univex/camera/OrbitCamera.h"
#include "univex/render/GlApi.h"
#include "univex/viewport/ViewportSettings.h"

#include "uve/events/event_system_uve.h"
#include "uve/input/input_system_uve.h"
#include "uve/memory/heap_allocator_uve.h"
#include "uve/scene/components/transform_component_uve.h"
#include "uve/scene/scene_graph_uve.h"
#include "uve/window/window_desc_uve.h"
#include "uve/window/window_manager_uve.h"
#include "uve/world/world_uve.h"

namespace {

using univex::app::ViewportRenderPass;
using univex::camera::OrbitCamera;
using univex::gizmo::GizmoMode;
using univex::math::Vec3;
using univex::viewport::DisplayMode;
using univex::viewport::ProjectionMode;
using univex::viewport::StandardView;

void ResetView(OrbitCamera& camera) {
    camera.CancelAnimation();
    camera.SetTarget({0.F, 0.F, 0.F});
    camera.SetYawPitch(-0.7553F, 0.4561F);
    camera.SetDistance(11.26F);
    camera.SetOrthographic(false);
}

void GoToStandardView(ViewportRenderPass& pass, OrbitCamera& camera, StandardView view) {
    auto& settings = pass.Settings();
    settings.standardView = view;
    float x = 0.F;
    float y = 0.F;
    float z = 0.F;
    univex::viewport::StandardViewDirection(view, x, y, z);
    camera.SnapToDirection(Vec3{x, y, z});
    if (settings.autoOrthogonal) {
        camera.SetOrthographic(true);
    }
}

// Populates a handful of entities the same way Engine/Editor/Viewport's own
// headless_capture --engine-demo does, so the editor window shows the same
// proven real-entity integration instead of an empty world.
void PopulateDemoEntities(UVE::World::WorldUVE& world, UVE::Scene::SceneGraphUVE& sceneGraph) {
    auto& entityManager = world.GetEntityManagerUVE();
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
}

} // namespace

int main(int argc, char** argv) {
    // --max-frames N: exit automatically after N frames instead of running
    // until Escape/close - lets this run to completion under a headless
    // Xvfb display for automated validation, same purpose as
    // headless_capture's own CLI flags serve for that tool.
    int maxFrames = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--max-frames") == 0 && i + 1 < argc) {
            maxFrames = std::atoi(argv[++i]);
        }
    }

    UVE::Events::EventSystemUVE eventSystem;

    UVE::Window::WindowDescUVE windowDesc;
    windowDesc.title = "UniVex Editor";
    windowDesc.width = 1280;
    windowDesc.height = 800;
    // 3.3 Core matches what Engine/Editor/Viewport's own GL renderer targets
    // (see its app/main.cpp) - deliberately not the RHI/OpenGL backend's 4.6
    // shipped default, which this sandbox's Mesa llvmpipe cannot create a
    // context for (confirmed by direct testing, same finding the ported
    // WindowManagerUVE test suite already documents).
    windowDesc.glVersionMajor = 3;
    windowDesc.glVersionMinor = 3;

    UVE::Window::WindowManagerUVE windowManager(eventSystem, windowDesc);
    if (!windowManager.IsValidUVE()) {
        std::fprintf(stderr, "UniVex Editor: window/GL context creation failed\n");
        return 1;
    }

    glewExperimental = GL_TRUE;
    if (const GLenum status = glewInit(); status != GLEW_OK) {
        std::fprintf(stderr, "UniVex Editor: glewInit failed: %s\n", glewGetErrorString(status));
        return 2;
    }
    glGetError(); // GLEW's core-profile probe leaves a benign GL_INVALID_ENUM behind

    UVE::Input::InputSystemUVE inputSystem(eventSystem);
    windowManager.AttachInputSystemUVE(&inputSystem);

    std::string error;
    auto pass = ViewportRenderPass::Create(error);
    if (!pass.has_value()) {
        std::fprintf(stderr, "UniVex Editor: viewport init failed: %s\n", error.c_str());
        return 3;
    }

    OrbitCamera camera;
    ResetView(camera);

    UVE::Memory::HeapAllocatorUVE worldAllocator;
    UVE::World::WorldUVE world(worldAllocator, eventSystem);
    UVE::Scene::SceneGraphUVE sceneGraph;
    PopulateDemoEntities(world, sceneGraph);
    world.TickUVE(0.0F);
    const univex::integration::WorldUveEntitySource entitySource(world);
    pass->SetEntitySource(&entitySource);

    std::printf("UniVex Editor: GL_RENDERER: %s\nUniVex Editor: GL_VERSION : %s\n",
                glGetString(GL_RENDERER), glGetString(GL_VERSION));

    auto lastFrameTime = std::chrono::steady_clock::now();
    int frameCount = 0;

    while (!windowManager.IsCloseRequestedUVE()) {
        windowManager.PollEventsUVE();
        inputSystem.UpdateUVE();

        if (inputSystem.WasKeyPressedThisFrameUVE(UVE::Input::KeyCodeUVE::Escape)) {
            break;
        }

        auto& settings = pass->Settings();
        const UVE::Math::Vector2UVE mouseDelta = inputSystem.GetMouseDeltaUVE();
        const bool leftDown = inputSystem.IsMouseButtonDownUVE(UVE::Input::MouseButtonUVE::Left);
        const bool panDown = inputSystem.IsMouseButtonDownUVE(UVE::Input::MouseButtonUVE::Middle) ||
                              inputSystem.IsMouseButtonDownUVE(UVE::Input::MouseButtonUVE::Right);
        if (leftDown && (mouseDelta.x != 0.0F || mouseDelta.y != 0.0F)) {
            camera.Orbit(mouseDelta.x, mouseDelta.y);
            settings.standardView = StandardView::User;
        }
        if (panDown) {
            camera.Pan(mouseDelta.x, mouseDelta.y, static_cast<int>(windowManager.GetHeightUVE()));
        }
        const float scrollDelta = inputSystem.GetMouseScrollDeltaUVE();
        if (scrollDelta != 0.0F) {
            camera.Dolly(scrollDelta);
        }

        using UVE::Input::KeyCodeUVE;
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::Q)) pass->SetGizmoMode(GizmoMode::Select);
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::W)) pass->SetGizmoMode(GizmoMode::Move);
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::E)) pass->SetGizmoMode(GizmoMode::Rotate);
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::R)) pass->SetGizmoMode(GizmoMode::Scale);
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::T)) pass->SetGizmoMode(GizmoMode::Universal);
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::Z)) settings.CycleDisplayMode();
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::G)) settings.viewGrid = !settings.viewGrid;
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::N)) settings.viewGizmos = !settings.viewGizmos;
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::H)) settings.viewTransformGizmo = !settings.viewTransformGizmo;
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::B)) settings.viewEnvironment = !settings.viewEnvironment;
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::M)) settings.viewSceneGeometry = !settings.viewSceneGeometry;

        const bool ctrl = inputSystem.IsKeyDownUVE(KeyCodeUVE::LeftCtrl) ||
                           inputSystem.IsKeyDownUVE(KeyCodeUVE::RightCtrl);
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::Num7)) {
            GoToStandardView(*pass, camera, ctrl ? StandardView::Bottom : StandardView::Top);
        }
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::Num1)) {
            GoToStandardView(*pass, camera, ctrl ? StandardView::Rear : StandardView::Front);
        }
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::Num3)) {
            GoToStandardView(*pass, camera, ctrl ? StandardView::Left : StandardView::Right);
        }
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::Num5)) {
            camera.SetOrthographic(!camera.IsOrthographic());
            settings.projection =
                camera.IsOrthographic() ? ProjectionMode::Orthographic : ProjectionMode::Perspective;
        }
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::O)) {
            camera.Focus(Vec3{0.F, 0.F, 0.F}, 6.F);
        }
        if (inputSystem.WasKeyPressedThisFrameUVE(KeyCodeUVE::F)) {
            camera.Focus(Vec3{0.F, 1.F, 0.F}, 3.2F);
        }

        const auto now = std::chrono::steady_clock::now();
        const float deltaSeconds = std::chrono::duration<float>(now - lastFrameTime).count();
        lastFrameTime = now;
        camera.Update(deltaSeconds);

        const std::uint32_t width = windowManager.GetWidthUVE();
        const std::uint32_t height = windowManager.GetHeightUVE();
        if (width > 0 && height > 0) {
            pass->RenderFrame(camera, static_cast<int>(width), static_cast<int>(height));
        }
        windowManager.SwapBuffersUVE();

        ++frameCount;
        if (maxFrames > 0 && frameCount >= maxFrames) {
            break;
        }
    }

    return 0;
}
