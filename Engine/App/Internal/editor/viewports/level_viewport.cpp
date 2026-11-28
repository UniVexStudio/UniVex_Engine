// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "level_viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "camera_3d.h"
#include "engine_core.h"
#include "engine_services.h"
#include "i_entity_manager.h"
#include "logging_macros.h"
#include "player_3d.h"
#include "ui_draw_batch.h"
#include "ui_font_atlas.h"
#include "vector3.h"
#include "world_transform_component.h"

namespace UVE::App::Viewports {
namespace {

using VpInput = Editor::EditorViewportInput;
using VpKey = VpInput::Key;

// Game HUD overlay over the resolved Play-in-Editor image (view pixels, top-left origin).
constexpr std::string_view kUIOverlayVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aTextured;
uniform vec2 uViewSize;
out vec2 vUv;
out vec4 vColor;
out float vTextured;
void main() {
    vUv = aUv;
    vColor = aColor;
    vTextured = aTextured;
    gl_Position = vec4(aPos.x / uViewSize.x * 2.0 - 1.0, 1.0 - aPos.y / uViewSize.y * 2.0, 0.0, 1.0);
}
)";

constexpr std::string_view kUIOverlayFragmentShader = R"(#version 330 core
uniform sampler2D uAtlas;
in vec2 vUv;
in vec4 vColor;
in float vTextured;
out vec4 FragColor;
void main() {
    vec4 color = vColor;
    if (vTextured > 0.5) {
        color *= texture(uAtlas, vUv);
    }
    FragColor = color;
}
)";

} // namespace

LevelViewport::LevelViewport(Editor::Editor& editor,
                                   Core::EngineCore& engine,
                                   Core::EngineServices& services)
    : m_editor(editor),
      m_engine(engine),
      m_surface(services),
      m_interaction(editor, services.GetEntityManager()) {}

LevelViewport::~LevelViewport() {
    if (m_pieCallbacksRegistered) {
        m_engine.SetUIPointerOrigin(std::nullopt);
        m_engine.SetUIViewportSize(std::nullopt);
        m_engine.SetMouseCursorHandler({});
        m_engine.SetFocusProvider({});
        m_engine.SetQuitGameHandler({});
        m_engine.SetOpenLevelHandler({});
        m_pieCallbacksRegistered = false;
    }
    if (m_uiFontAtlasTexture != 0U) {
        glDeleteTextures(1, &m_uiFontAtlasTexture);
        m_uiFontAtlasTexture = 0U;
    }
    if (m_uiOverlayVao != 0U) {
        glDeleteVertexArrays(1, &m_uiOverlayVao);
        m_uiOverlayVao = 0U;
    }
    if (m_uiOverlayVbo != 0U) {
        glDeleteBuffers(1, &m_uiOverlayVbo);
        m_uiOverlayVbo = 0U;
    }
}

std::uint64_t LevelViewport::Render(const Math::Vector2& availableSize,
                                          Math::Vector2& outUsedSize,
                                          const Editor::Editor::ViewportOverlayState& overlayState) {
    const int width = std::max(1, static_cast<int>(availableSize.x));
    const int height = std::max(1, static_cast<int>(availableSize.y));
    m_surface.ApplyQualityPreset(overlayState.qualityPreset, overlayState);
    if (!m_surface.PrepareFrame(width, height)) {
        return 0U;
    }

    const float appliedScale = overlayState.resolvedRenderScale > 0.1F ? overlayState.resolvedRenderScale : 1.0F;
    SyncPlayInEditorHooks(availableSize, overlayState.input, appliedScale);
    SyncPlayCameraPose();

    const bool gameWorkspaceActive = overlayState.gameWorkspaceActive;
    m_interaction.ApplyOverlayState(m_surface, overlayState, gameWorkspaceActive);

    std::optional<Scene::Entity> lookThroughCamera;
    if (gameWorkspaceActive) {
        lookThroughCamera = Scene::FindCurrentCameraEntity(m_surface.EntityManager());
        static_cast<void>(m_interaction.UpdateCameraPreviewFlight(false, overlayState.input));
    } else {
        lookThroughCamera = m_interaction.UpdateCameraPreviewFlight(true, overlayState.input);
    }

    const bool isGamePlaying =
        gameWorkspaceActive && (m_editor.GetPlayModeState() == Editor::EditorPlayModeState::Playing);
    const bool isGameMouseCaptured = isGamePlaying && m_editor.IsGameMouseCaptured();

    if (isGameMouseCaptured) {
        if (!m_interaction.IsPreviewingCamera()) {
            UpdatePlayCapturedCamera(overlayState.input);
        }
    } else {
        m_interaction.UpdateInteraction(overlayState, width, height, false);
    }
    m_interaction.AdvanceCameraAndPublishFocus(overlayState.input);

    m_surface.ApplyPreviewLighting(overlayState, !gameWorkspaceActive);

    const univex::integration::EditorMeshLayerResult meshResult =
        m_surface.RenderSceneMeshLayer(m_interaction.Camera(), width, height, lookThroughCamera);

    const bool wantOverlay = !gameWorkspaceActive && !m_interaction.IsPreviewingCamera();
    m_interaction.UpdateObjectGizmoOverlay(m_surface, overlayState, width, height, lookThroughCamera, wantOverlay);
    m_interaction.ExportViewForHost(overlayState, width, height, wantOverlay);
    outUsedSize = Math::Vector2{static_cast<float>(width), static_cast<float>(height)};

    const GLint previousFbo = m_surface.BeginPass(width, height);
    const bool neutralMainFallback = !m_surface.HasActiveWorldEnvironment();
    m_surface.RenderPass().RenderBackground(neutralMainFallback);
    m_surface.BlitMeshLayer(meshResult, m_interaction.Camera(), width, height);

    overlayState.selectionMask.clear();
    overlayState.selectionMaskWidth = overlayState.selectionMaskHeight = 0;
    m_interaction.ExportGridForHost(m_surface, overlayState, false);
    if (overlayState.hostDrawsGizmos && !lookThroughCamera.has_value()) {
        m_interaction.ExportSceneDepthForHost(m_surface, overlayState, width, height);
    } else {
        overlayState.sceneDepth.clear();
        overlayState.sceneDepthWidth = overlayState.sceneDepthHeight = 0;
    }

    if (!m_interaction.IsPreviewingCamera()) {
        m_surface.RenderPass().RenderGrid(m_interaction.Camera(), width, height);
    }
    if (wantOverlay) {
        m_interaction.RenderSelectionOutlineOrMask(m_surface, overlayState, width, height);
    }
    m_surface.RenderPass().RenderOverlay(m_interaction.Camera(), width, height);

    const std::uint64_t resolvedTexture = m_surface.EndPassAndResolve(width, height, previousFbo);
    if (gameWorkspaceActive) {
        DrawUIOverlay(width, height, appliedScale);
    }
    return resolvedTexture;
}

void LevelViewport::SyncPlayInEditorHooks(const Math::Vector2& availableSize,
                                                const Editor::EditorViewportInput& input,
                                                const float renderScale) {
    const float invRenderScale = renderScale > 0.1F ? (1.0F / renderScale) : 1.0F;
    m_engine.SetUIPointerOrigin(
        Math::Vector2{input.imageOrigin.x * invRenderScale, input.imageOrigin.y * invRenderScale});
    m_engine.SetUIViewportSize(Math::Vector2{std::floor(availableSize.x * invRenderScale),
                                                   std::floor(availableSize.y * invRenderScale)});
    if (!m_engine.HasMouseCursorHandler()) {
        m_engine.SetMouseCursorHandler([this](const bool show) { m_editor.SetGameMouseCaptured(!show); });
        m_engine.SetFocusProvider([this] { return m_editor.IsAnyEditorWindowFocused(); });
        m_engine.SetQuitGameHandler([this] { m_gameRequest = GameRequest{true, {}}; });
        m_engine.SetOpenLevelHandler([this](const std::string& path) {
            m_gameRequest = GameRequest{false, path};
            return true;
        });
        m_pieCallbacksRegistered = true;
    }
    if (m_gameRequest.has_value()) {
        const GameRequest request = std::move(*m_gameRequest);
        m_gameRequest.reset();
        if (m_editor.GetPlayModeState() != Editor::EditorPlayModeState::Edit) {
            static_cast<void>(m_editor.StopPlayMode());
            if (!request.quit) {
                const std::filesystem::path level =
                    m_engine.GetServices().GetFileSystem().ResolveRealPath(request.level);
                if (!level.empty() && m_editor.OpenSceneAsset(level)) {
                    static_cast<void>(m_editor.EnterPlayMode());
                }
            }
        }
    }
}

void LevelViewport::SyncPlayCameraPose() {
    const bool inPlay = m_editor.GetPlayModeState() != Editor::EditorPlayModeState::Edit;
    if (inPlay && !m_poseBeforePlay.has_value()) {
        m_poseBeforePlay = m_interaction.CapturePose();
    } else if (!inPlay && m_poseBeforePlay.has_value()) {
        m_interaction.RestorePose(*m_poseBeforePlay);
        m_poseBeforePlay.reset();
    }
}

void LevelViewport::UpdatePlayCapturedCamera(const Editor::EditorViewportInput& input) {
    auto& camera = m_interaction.Camera();
    const auto& prefs = m_editor.GetExtendedPreferences();
    const float orbitSens = static_cast<float>(std::clamp(prefs.orbitSensitivity, 0.05, 10.0));

    if (std::abs(input.mouseDelta.x) > 0.0F || std::abs(input.mouseDelta.y) > 0.0F) {
        camera.Orbit(input.mouseDelta.x * orbitSens, input.mouseDelta.y * orbitSens);
        m_editor.NotifyViewportOrbited();
    }

    // The play view follows the possessed player. It stops following - and holds where it was -
    // once the player has fallen out of the world, so a body that drops through the level never
    // drags the view down with it forever.
    constexpr float kPlayFollowLowestY = -200.0F;
    Scene::IEntityManager& entityManager = m_surface.EntityManager();
    const Scene::Entity player = Scene::ResolvePossessedPlayer(entityManager);
    if (player != Scene::kInvalidEntity &&
        entityManager.HasComponent<Scene::WorldTransformComponent>(player)) {
        const auto& wt = entityManager.GetComponent<Scene::WorldTransformComponent>(player);
        if (Math::IsFinite(wt.worldPosition) && wt.worldPosition.y > kPlayFollowLowestY) {
            camera.SetTarget(univex::math::Vec3{wt.worldPosition.x, wt.worldPosition.y + 1.2F, wt.worldPosition.z});
        }
        return;
    }

    univex::math::Vec3 forward = univex::math::Normalize(camera.Target() - camera.Eye());
    if (univex::math::Length(forward) < 0.001F) {
        forward = univex::math::Vec3{0.0F, 0.0F, -1.0F};
    }
    const univex::math::Vec3 up{0.0F, 1.0F, 0.0F};
    const univex::math::Vec3 right = univex::math::Normalize(univex::math::Cross(forward, up));

    univex::math::Vec3 moveDir{0.0F, 0.0F, 0.0F};
    if (input.IsKeyDown(VpKey::W)) {
        moveDir = moveDir + forward;
    }
    if (input.IsKeyDown(VpKey::S)) {
        moveDir = moveDir - forward;
    }
    if (input.IsKeyDown(VpKey::D)) {
        moveDir = moveDir + right;
    }
    if (input.IsKeyDown(VpKey::A)) {
        moveDir = moveDir - right;
    }
    if (input.IsKeyDown(VpKey::Space)) {
        moveDir = moveDir + up;
    }
    if (input.IsKeyDown(VpKey::LeftCtrl) || input.IsKeyDown(VpKey::C)) {
        moveDir = moveDir - up;
    }

    const float moveLen = univex::math::Length(moveDir);
    if (moveLen > 0.001F) {
        const float flySpeed =
            (input.IsKeyDown(VpKey::LeftShift) ? 22.0F : 8.0F) * std::max(0.001F, input.deltaTime);
        moveDir = moveDir * (flySpeed / moveLen);
        camera.SetTarget(camera.Target() + moveDir);
    }
}

void LevelViewport::EnsureUIFontAtlasTexture(const UI::UIFontAtlas& fontAtlas) {
    if (m_uiFontAtlasTexture != 0U || !fontAtlas.IsValid()) {
        return;
    }
    glGenTextures(1, &m_uiFontAtlasTexture);
    glBindTexture(GL_TEXTURE_2D, m_uiFontAtlasTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, UI::UIFontAtlas::kAtlasWidth, UI::UIFontAtlas::kAtlasHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, fontAtlas.GetBitmap().data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool LevelViewport::EnsureUIOverlayResources() {
    if (m_uiOverlayProgram.has_value()) {
        return true;
    }
    std::string error;
    m_uiOverlayProgram =
        univex::render::ShaderProgram::Build(kUIOverlayVertexShader, kUIOverlayFragmentShader, error);
    if (!m_uiOverlayProgram.has_value()) {
        UVE_ERROR("uve_editor_app: game UI overlay shader build failed: {}", error);
        return false;
    }
    glGenVertexArrays(1, &m_uiOverlayVao);
    glGenBuffers(1, &m_uiOverlayVbo);
    glBindVertexArray(m_uiOverlayVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_uiOverlayVbo);
    constexpr GLsizei stride = 9 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(4 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(8 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void LevelViewport::DrawUIOverlay(const int width, const int height, const float renderScale) {
    const UI::UIDrawBatch& batch = m_engine.GetUIRuntime().GetDrawBatch();
    if (batch.quads.empty() || width <= 0 || height <= 0 || !EnsureUIOverlayResources()) {
        return;
    }
    EnsureUIFontAtlasTexture(m_engine.GetUIRuntime().GetFontAtlas());
    m_uiOverlayVertices.clear();
    m_uiOverlayVertices.reserve(batch.quads.size() * 6U * 9U);
    for (const UI::UIQuad& quad : batch.quads) {
        const float x0 = quad.rect.position.x;
        const float y0 = quad.rect.position.y;
        const float x1 = x0 + quad.rect.size.x;
        const float y1 = y0 + quad.rect.size.y;
        const float textured =
            quad.kind == UI::UIDrawItemKind::Glyph && m_uiFontAtlasTexture != 0U ? 1.0F : 0.0F;
        const auto vertex = [&](const float x, const float y, const float u, const float v) {
            m_uiOverlayVertices.insert(m_uiOverlayVertices.end(),
                                       {x, y, u, v, quad.color.x, quad.color.y, quad.color.z, quad.alpha, textured});
        };
        vertex(x0, y0, quad.u0, quad.v0);
        vertex(x1, y0, quad.u1, quad.v0);
        vertex(x1, y1, quad.u1, quad.v1);
        vertex(x0, y0, quad.u0, quad.v0);
        vertex(x1, y1, quad.u1, quad.v1);
        vertex(x0, y1, quad.u0, quad.v1);
    }

    GLint previousFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    GLint previousViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glBindFramebuffer(GL_FRAMEBUFFER, m_surface.OutputFbo());
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    m_uiOverlayProgram->Use();
    const float invRenderScale = renderScale > 0.1F ? (1.0F / renderScale) : 1.0F;
    glUniform2f(m_uiOverlayProgram->UniformLocation("uViewSize"), static_cast<float>(width) * invRenderScale,
                static_cast<float>(height) * invRenderScale);
    glUniform1i(m_uiOverlayProgram->UniformLocation("uAtlas"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_uiFontAtlasTexture);
    glBindVertexArray(m_uiOverlayVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_uiOverlayVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_uiOverlayVertices.size() * sizeof(float)),
                 m_uiOverlayVertices.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_uiOverlayVertices.size() / 9U));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
}

} // namespace UVE::App::Viewports
