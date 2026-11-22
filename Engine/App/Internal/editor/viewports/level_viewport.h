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
class EngineCoreUVE;
class EngineServicesUVE;
}

namespace UVE::UI {
class UIFontAtlasUVE;
}

namespace UVE::App::Viewports {

/// Main Level Editor 3D viewport (`level_viewport`).
///
/// Composes `ViewportSurfaceUVE` and `ViewportSceneInteractionUVE` and exclusively owns all
/// Play-in-Editor engine bindings (UI pointer origin, UI viewport size, mouse cursor capture,
/// focus provider, Quit Game / Open Level handlers), Play-mode spectator / player-follow camera
/// control, pre-Play camera pose restoration, and the runtime Game HUD overlay pass.
class LevelViewportUVE final {
public:
    LevelViewportUVE(Editor::EditorUVE& editor,
                     Core::EngineCoreUVE& engine,
                     Core::EngineServicesUVE& services);
    ~LevelViewportUVE();

    LevelViewportUVE(const LevelViewportUVE&) = delete;
    LevelViewportUVE& operator=(const LevelViewportUVE&) = delete;
    LevelViewportUVE(LevelViewportUVE&&) = delete;
    LevelViewportUVE& operator=(LevelViewportUVE&&) = delete;

    [[nodiscard]] const Core::EngineServicesUVE* ServicesUVE() const noexcept { return m_surface.ServicesUVE(); }

    [[nodiscard]] std::uint64_t RenderUVE(const Math::Vector2UVE& availableSize,
                                          Math::Vector2UVE& outUsedSize,
                                          const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState);

private:
    struct GameRequestUVE final {
        bool quit = false;
        std::string level;
    };

    void SyncPlayInEditorHooksUVE(const Math::Vector2UVE& availableSize,
                                  const Editor::EditorViewportInputUVE& input,
                                  float renderScale);
    void SyncPlayCameraPoseUVE();
    void UpdatePlayCapturedCameraUVE(const Editor::EditorViewportInputUVE& input);

    void EnsureUIFontAtlasTextureUVE(const UI::UIFontAtlasUVE& fontAtlas);
    [[nodiscard]] bool EnsureUIOverlayResourcesUVE();
    void DrawUIOverlayUVE(int width, int height, float renderScale);

    Editor::EditorUVE& m_editor;
    Core::EngineCoreUVE& m_engine;
    ViewportSurfaceUVE m_surface;
    ViewportSceneInteractionUVE m_interaction;

    bool m_pieCallbacksRegistered = false;
    std::optional<GameRequestUVE> m_gameRequest;
    std::optional<ViewportCameraPoseUVE> m_poseBeforePlay;

    GLuint m_uiFontAtlasTexture = 0U;
    std::optional<univex::render::ShaderProgram> m_uiOverlayProgram;
    GLuint m_uiOverlayVao = 0U;
    GLuint m_uiOverlayVbo = 0U;
    std::vector<float> m_uiOverlayVertices;
};

} // namespace UVE::App::Viewports
