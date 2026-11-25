// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "univex/render/gl_api.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "editor.h"
#include "univex/render/shader_program.h"
#include "vector2.h"
#include "viewport_scene_interaction.h"
#include "viewport_surface.h"

namespace UVE::Core {
class EngineCore;
class EngineServices;
}

namespace UVE::UI {
class UIFontAtlas;
}

namespace UVE::App::Viewports {

/// Main Level Editor 3D viewport (`level_viewport`).
///
/// Composes `ViewportSurface` and `ViewportSceneInteraction` and exclusively owns all
/// Play-in-Editor engine bindings (UI pointer origin, UI viewport size, mouse cursor capture,
/// focus provider, Quit Game / Open Level handlers), Play-mode spectator / player-follow camera
/// control, pre-Play camera pose restoration, and the runtime Game HUD overlay pass.
class LevelViewport final {
public:
    LevelViewport(Editor::Editor& editor,
                     Core::EngineCore& engine,
                     Core::EngineServices& services);
    ~LevelViewport();

    LevelViewport(const LevelViewport&) = delete;
    LevelViewport& operator=(const LevelViewport&) = delete;
    LevelViewport(LevelViewport&&) = delete;
    LevelViewport& operator=(LevelViewport&&) = delete;

    [[nodiscard]] const Core::EngineServices* Services() const noexcept { return m_surface.Services(); }

    [[nodiscard]] std::uint64_t Render(const Math::Vector2& availableSize,
                                          Math::Vector2& outUsedSize,
                                          const Editor::Editor::ViewportOverlayState& overlayState);

private:
    struct GameRequest final {
        bool quit = false;
        std::string level;
    };

    void SyncPlayInEditorHooks(const Math::Vector2& availableSize,
                                  const Editor::EditorViewportInput& input,
                                  float renderScale);
    void SyncPlayCameraPose();
    void UpdatePlayCapturedCamera(const Editor::EditorViewportInput& input);

    void EnsureUIFontAtlasTexture(const UI::UIFontAtlas& fontAtlas);
    [[nodiscard]] bool EnsureUIOverlayResources();
    void DrawUIOverlay(int width, int height, float renderScale);

    Editor::Editor& m_editor;
    Core::EngineCore& m_engine;
    ViewportSurface m_surface;
    ViewportSceneInteraction m_interaction;

    bool m_pieCallbacksRegistered = false;
    std::optional<GameRequest> m_gameRequest;
    std::optional<ViewportCameraPose> m_poseBeforePlay;

    GLuint m_uiFontAtlasTexture = 0U;
    std::optional<univex::render::ShaderProgram> m_uiOverlayProgram;
    GLuint m_uiOverlayVao = 0U;
    GLuint m_uiOverlayVbo = 0U;
    std::vector<float> m_uiOverlayVertices;
};

} // namespace UVE::App::Viewports
