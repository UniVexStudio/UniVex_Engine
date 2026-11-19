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

using VpInputUVE = Editor::EditorViewportInputUVE;
using VpKeyUVE = VpInputUVE::KeyUVE;

// Game HUD overlay over the resolved Play-in-Editor image (view pixels, top-left origin).
constexpr std::string_view kUIOverlayVertexShaderUVE = R"(#version 330 core
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

constexpr std::string_view kUIOverlayFragmentShaderUVE = R"(#version 330 core
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

LevelViewportUVE::LevelViewportUVE(Editor::EditorUVE& editor,
                                   Core::EngineCoreUVE& engine,
                                   Core::EngineServicesUVE& services)
    : m_editor(editor),
      m_engine(engine),
      m_surface(services),
      m_interaction(editor, services.GetEntityManagerUVE()) {}

LevelViewportUVE::~LevelViewportUVE() {
    if (m_pieCallbacksRegistered) {
        m_engine.SetUIPointerOriginUVE(std::nullopt);
        m_engine.SetUIViewportSizeUVE(std::nullopt);
        m_engine.SetMouseCursorHandlerUVE({});
        m_engine.SetFocusProviderUVE({});
        m_engine.SetQuitGameHandlerUVE({});
        m_engine.SetOpenLevelHandlerUVE({});
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

std::uint64_t LevelViewportUVE::RenderUVE(const Math::Vector2UVE& availableSize,
                                          Math::Vector2UVE& outUsedSize,
                                          const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
    const int width = std::max(1, static_cast<int>(availableSize.x));
    const int height = std::max(1, static_cast<int>(availableSize.y));
    if (!m_surface.PrepareFrameUVE(width, height)) {
        return 0U;
    }

    SyncPlayInEditorHooksUVE(availableSize, overlayState.input);
    SyncPlayCameraPoseUVE();

    const bool gameWorkspaceActive = overlayState.gameWorkspaceActive;
    m_interaction.ApplyOverlayStateUVE(m_surface, overlayState, gameWorkspaceActive);

    std::optional<Scene::EntityUVE> lookThroughCamera;
    if (gameWorkspaceActive) {
        lookThroughCamera = Scene::FindCurrentCameraEntityUVE(m_surface.EntityManagerUVE());
        static_cast<void>(m_interaction.UpdateCameraPreviewFlightUVE(false, overlayState.input));
    } else {
        lookThroughCamera = m_interaction.UpdateCameraPreviewFlightUVE(true, overlayState.input);
    }

    const bool isGamePlaying =
        gameWorkspaceActive && (m_editor.GetPlayModeStateUVE() == Editor::EditorPlayModeStateUVE::Playing);
    const bool isGameMouseCaptured = isGamePlaying && m_editor.IsGameMouseCapturedUVE();

    m_interaction.UpdateInteractionUVE(overlayState, width, height, isGameMouseCaptured);
    if (isGameMouseCaptured && !m_interaction.IsPreviewingCameraUVE()) {
        UpdatePlayCapturedCameraUVE(overlayState.input);
    }
    m_interaction.AdvanceCameraAndPublishFocusUVE(overlayState.input);

    m_surface.ApplyPreviewLightingUVE(overlayState, !gameWorkspaceActive);

    const univex::integration::EditorMeshLayerResultUVE meshResult =
        m_surface.RenderSceneMeshLayerUVE(m_interaction.CameraUVE(), width, height, lookThroughCamera);

    const bool wantOverlay = !gameWorkspaceActive && !m_interaction.IsPreviewingCameraUVE();
    m_interaction.UpdateObjectGizmoOverlayUVE(m_surface, overlayState, width, height, lookThroughCamera, wantOverlay);
    m_interaction.ExportViewForHostUVE(overlayState, width, height, wantOverlay);
    outUsedSize = Math::Vector2UVE{static_cast<float>(width), static_cast<float>(height)};

    const GLint previousFbo = m_surface.BeginPassUVE(width, height);
    const bool neutralMainFallback = !m_surface.HasActiveWorldEnvironmentUVE();
    m_surface.RenderPassUVE().RenderBackgroundUVE(neutralMainFallback);
    m_surface.BlitMeshLayerUVE(meshResult, m_interaction.CameraUVE(), width, height);

    overlayState.selectionMask.clear();
    overlayState.selectionMaskWidth = overlayState.selectionMaskHeight = 0;
    m_interaction.ExportGridForHostUVE(m_surface, overlayState, false);
    if (overlayState.hostDrawsGizmos && !lookThroughCamera.has_value()) {
        m_interaction.ExportSceneDepthForHostUVE(m_surface, overlayState, width, height);
    } else {
        overlayState.sceneDepth.clear();
        overlayState.sceneDepthWidth = overlayState.sceneDepthHeight = 0;
    }

    if (!m_interaction.IsPreviewingCameraUVE()) {
        m_surface.RenderPassUVE().RenderGridUVE(m_interaction.CameraUVE(), width, height);
    }
    if (wantOverlay) {
        m_interaction.RenderSelectionOutlineOrMaskUVE(m_surface, overlayState, width, height);
    }
    m_surface.RenderPassUVE().RenderOverlayUVE(m_interaction.CameraUVE(), width, height);

    const std::uint64_t resolvedTexture = m_surface.EndPassAndResolveUVE(width, height, previousFbo);
    if (gameWorkspaceActive) {
        DrawUIOverlayUVE(width, height);
    }
    return resolvedTexture;
}

void LevelViewportUVE::SyncPlayInEditorHooksUVE(const Math::Vector2UVE& availableSize,
                                                const Editor::EditorViewportInputUVE& input) {
    m_engine.SetUIPointerOriginUVE(input.imageOrigin);
    m_engine.SetUIViewportSizeUVE(Math::Vector2UVE{std::floor(availableSize.x), std::floor(availableSize.y)});
    if (!m_engine.HasMouseCursorHandlerUVE()) {
        m_engine.SetMouseCursorHandlerUVE([this](const bool show) { m_editor.SetGameMouseCapturedUVE(!show); });
        m_engine.SetFocusProviderUVE([this] { return m_editor.IsAnyEditorWindowFocusedUVE(); });
        m_engine.SetQuitGameHandlerUVE([this] { m_gameRequest = GameRequestUVE{true, {}}; });
        m_engine.SetOpenLevelHandlerUVE([this](const std::string& path) {
            m_gameRequest = GameRequestUVE{false, path};
            return true;
        });
        m_pieCallbacksRegistered = true;
    }
    if (m_gameRequest.has_value()) {
        const GameRequestUVE request = std::move(*m_gameRequest);
        m_gameRequest.reset();
        if (m_editor.GetPlayModeStateUVE() != Editor::EditorPlayModeStateUVE::Edit) {
            static_cast<void>(m_editor.StopPlayModeUVE());
            if (!request.quit) {
                const std::filesystem::path level =
                    m_engine.GetServicesUVE().GetFileSystemUVE().ResolveRealPathUVE(request.level);
                if (!level.empty() && m_editor.OpenSceneAssetUVE(level)) {
                    static_cast<void>(m_editor.EnterPlayModeUVE());
                }
            }
        }
    }
}

void LevelViewportUVE::SyncPlayCameraPoseUVE() {
    const bool inPlay = m_editor.GetPlayModeStateUVE() != Editor::EditorPlayModeStateUVE::Edit;
    if (inPlay && !m_poseBeforePlay.has_value()) {
        m_poseBeforePlay = m_interaction.CapturePoseUVE();
    } else if (!inPlay && m_poseBeforePlay.has_value()) {
        m_interaction.RestorePoseUVE(*m_poseBeforePlay);
        m_poseBeforePlay.reset();
    }
}

void LevelViewportUVE::UpdatePlayCapturedCameraUVE(const Editor::EditorViewportInputUVE& input) {
    auto& camera = m_interaction.CameraUVE();
    const auto& prefs = m_editor.GetExtendedPreferencesUVE();
    const float orbitSens = static_cast<float>(std::clamp(prefs.orbitSensitivity, 0.05, 10.0));

    if (std::abs(input.mouseDelta.x) > 0.0F || std::abs(input.mouseDelta.y) > 0.0F) {
        camera.Orbit(input.mouseDelta.x * orbitSens, input.mouseDelta.y * orbitSens);
        m_editor.NotifyViewportOrbitedUVE();
    }

    Scene::IEntityManagerUVE& entityManager = m_surface.EntityManagerUVE();
    const Scene::EntityUVE player = Scene::ResolvePossessedPlayerUVE(entityManager);
    if (player != Scene::kInvalidEntityUVE &&
        entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(player)) {
        const auto& wt = entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(player);
        if (Math::IsFiniteUVE(wt.worldPosition)) {
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
    if (input.IsKeyDownUVE(VpKeyUVE::W)) {
        moveDir = moveDir + forward;
    }
    if (input.IsKeyDownUVE(VpKeyUVE::S)) {
        moveDir = moveDir - forward;
    }
    if (input.IsKeyDownUVE(VpKeyUVE::D)) {
        moveDir = moveDir + right;
    }
    if (input.IsKeyDownUVE(VpKeyUVE::A)) {
        moveDir = moveDir - right;
    }
    if (input.IsKeyDownUVE(VpKeyUVE::Space)) {
        moveDir = moveDir + up;
    }
    if (input.IsKeyDownUVE(VpKeyUVE::LeftCtrl) || input.IsKeyDownUVE(VpKeyUVE::C)) {
        moveDir = moveDir - up;
    }

    const float moveLen = univex::math::Length(moveDir);
    if (moveLen > 0.001F) {
        const float flySpeed =
            (input.IsKeyDownUVE(VpKeyUVE::LeftShift) ? 22.0F : 8.0F) * std::max(0.001F, input.deltaTime);
        moveDir = moveDir * (flySpeed / moveLen);
        camera.SetTarget(camera.Target() + moveDir);
    }
}

void LevelViewportUVE::EnsureUIFontAtlasTextureUVE(const UI::UIFontAtlasUVE& fontAtlas) {
    if (m_uiFontAtlasTexture != 0U || !fontAtlas.IsValidUVE()) {
        return;
    }
    glGenTextures(1, &m_uiFontAtlasTexture);
    glBindTexture(GL_TEXTURE_2D, m_uiFontAtlasTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, UI::UIFontAtlasUVE::kAtlasWidthUVE, UI::UIFontAtlasUVE::kAtlasHeightUVE,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, fontAtlas.GetBitmapUVE().data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool LevelViewportUVE::EnsureUIOverlayResourcesUVE() {
    if (m_uiOverlayProgram.has_value()) {
        return true;
    }
    std::string error;
    m_uiOverlayProgram =
        univex::render::ShaderProgram::Build(kUIOverlayVertexShaderUVE, kUIOverlayFragmentShaderUVE, error);
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

void LevelViewportUVE::DrawUIOverlayUVE(const int width, const int height) {
    const UI::UIDrawBatchUVE& batch = m_engine.GetUIRuntimeUVE().GetDrawBatchUVE();
    if (batch.quads.empty() || width <= 0 || height <= 0 || !EnsureUIOverlayResourcesUVE()) {
        return;
    }
    EnsureUIFontAtlasTextureUVE(m_engine.GetUIRuntimeUVE().GetFontAtlasUVE());
    m_uiOverlayVertices.clear();
    m_uiOverlayVertices.reserve(batch.quads.size() * 6U * 9U);
    for (const UI::UIQuadUVE& quad : batch.quads) {
        const float x0 = quad.rect.position.x;
        const float y0 = quad.rect.position.y;
        const float x1 = x0 + quad.rect.size.x;
        const float y1 = y0 + quad.rect.size.y;
        const float textured =
            quad.kind == UI::UIDrawItemKindUVE::Glyph && m_uiFontAtlasTexture != 0U ? 1.0F : 0.0F;
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
    glBindFramebuffer(GL_FRAMEBUFFER, m_surface.ResolveFboUVE());
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    m_uiOverlayProgram->Use();
    glUniform2f(m_uiOverlayProgram->UniformLocation("uViewSize"), static_cast<float>(width),
                static_cast<float>(height));
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
