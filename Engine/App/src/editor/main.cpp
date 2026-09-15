// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// GL/glew.h (pulled in via GlApi.h) must be included before anything that might transitively pull
// <GLFW/glfw3.h>, or GLFW's own bundled GL header conflicts with GLEW's - see GlApi.h's own
// comment. Nothing else in this file currently drags in GLFW, but this ordering is cheap
// insurance and matches every other translation unit in Engine/Editor/Viewport that mixes the two.
#include "univex/render/GlApi.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "ViewportRenderPass.h"
#include "integration/EditorMeshLayer.h"
#include "integration/EntityManagerEntitySource.h"
#include "univex/camera/OrbitCamera.h"
#include "univex/render/ShaderProgram.h"

#include "uve/core/engine_core_uve.h"
#include "uve/debug/logging_macros_uve.h"
#include "uve/editor/editor_bridge_stdio_uve.h"
#include "uve/ui/ui_draw_batch_uve.h"
#include "uve/ui/ui_font_atlas_uve.h"
#include "uve/editor/editor_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/scene/components/camera_component_uve.h"
#include "uve/scene/components/world_transform_component_uve.h"
#include "uve/scene/i_entity_manager_uve.h"
#include "uve/scene/i_scene_graph_uve.h"

namespace {

// Fullscreen-triangle compositing pass: layers EditorMeshLayerUVE's real mesh/material render on
// top of ViewportRenderPass's grid/gizmo image, using the mesh layer's own depth buffer (cleared
// to the far value, 1.0) as the per-pixel test for "was real geometry drawn here" - avoids needing
// to reconcile the two renderers' independent camera/projection depth conventions, since only the
// mesh layer's own depth is ever read. No vertex buffer needed (same gl_VertexID trick already
// used by Renderer3DUVE's own internal tonemap/fullscreen-quad shader).
constexpr std::string_view kCompositeVertexShaderUVE = R"(#version 330 core
void main() {
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr std::string_view kCompositeFragmentShaderUVE = R"(#version 330 core
uniform sampler2D uGridColor;
uniform sampler2D uMeshColor;
uniform sampler2D uMeshDepth;
out vec4 FragColor;
void main() {
    ivec2 coord = ivec2(gl_FragCoord.xy);
    float meshDepth = texelFetch(uMeshDepth, coord, 0).r;
    vec4 meshColor = texelFetch(uMeshColor, coord, 0);
    vec4 gridColor = texelFetch(uGridColor, coord, 0);
    FragColor = meshDepth < 1.0 ? meshColor : gridColor;
}
)";

// Chooses "the" scene camera to render through while the Game workspace tab is active - a player
// preview, per UpdateSelectionGizmoUVE/ApplyOverlayStateUVE's own established "what a player would
// see" convention for that state. CameraComponentUVE (Component/include/.../camera_component_uve.h)
// has no priority/"main camera" tag of any kind yet, so this is deliberately the simplest honest
// rule - the first entity found (stable ECS storage order) carrying both a real world transform and
// a camera - documented here rather than silently assumed; a future increment can add a real
// "main camera" flag once more than one scene camera is a real authoring scenario.
[[nodiscard]] std::optional<UVE::Scene::EntityUVE> FindGameCameraEntityUVE(
    UVE::Scene::IEntityManagerUVE& entityManager) {
    std::optional<UVE::Scene::EntityUVE> found;
    entityManager.ForEachUVE<UVE::Scene::WorldTransformComponentUVE, UVE::Scene::CameraComponentUVE>(
        [&found](const UVE::Scene::EntityUVE entity, const UVE::Scene::WorldTransformComponentUVE&,
                 const UVE::Scene::CameraComponentUVE&) {
            if (!found.has_value()) {
                found = entity;
            }
        });
    return found;
}

// Bridges Engine/Editor/Viewport's real GL renderer (grid + orbit camera + transform/orientation
// gizmos + one proxy cube per live scene entity) into EditorUVE's generic, viewport-agnostic
// "Viewport" panel hook (EditorUVE::SetViewportPanelRendererUVE) - see that method's own doc
// comment in editor_uve.h for why EditorCore itself stays free of any Viewport-module/GL/ImGui
// types. Renders off-screen into an MSAA framebuffer, resolves into a plain 2D texture
// ImGui::Image() can sample, and recreates both whenever the panel's reported size changes -
// the same multisample-then-resolve shape tools/headless_capture.cpp already uses (that file's
// own comment explains why: the solid gizmo/cube geometry has nothing but MSAA to soften its
// silhouettes) and the same resize-on-demand shape app/main.cpp's interactive demo already uses
// for its own window-sized framebuffer, just driven by the ImGui panel's reported size instead of
// a GLFW framebuffer-resize callback.
class ViewportPanelBackendUVE final {
public:
    ViewportPanelBackendUVE(UVE::Editor::EditorUVE& editor, UVE::Core::EngineCoreUVE& engine)
        : editor_(editor), engine_(engine), entityManager_(engine.GetServicesUVE().GetEntityManagerUVE()),
          entitySource_(entityManager_), meshLayer_(engine.GetServicesUVE()) {}

    ~ViewportPanelBackendUVE() {
        DestroyFramebuffersUVE();
        if (compositeVao_ != 0U) {
            glDeleteVertexArrays(1, &compositeVao_);
        }
        if (uiFontAtlasTexture_ != 0U) {
            glDeleteTextures(1, &uiFontAtlasTexture_);
        }
    }

    ViewportPanelBackendUVE(const ViewportPanelBackendUVE&) = delete;
    ViewportPanelBackendUVE& operator=(const ViewportPanelBackendUVE&) = delete;

    [[nodiscard]] std::uint64_t RenderUVE(const UVE::Math::Vector2UVE& availableSize,
                                          UVE::Math::Vector2UVE& outUsedSize,
                                          const UVE::Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
        if (!EnsureGlewInitializedUVE() || !EnsureRenderPassUVE()) {
            return 0U;
        }

        const int width = std::max(1, static_cast<int>(availableSize.x));
        const int height = std::max(1, static_cast<int>(availableSize.y));
        if (!EnsureFramebuffersUVE(width, height)) {
            return 0U;
        }

        ApplyOverlayStateUVE(overlayState);
        UpdateSelectionGizmoUVE();
        UpdateCameraFromMouseUVE(height);

        GLint previousFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        glEnable(GL_MULTISAMPLE);
        renderPass_->RenderFrame(camera_, width, height);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));

        // Real MeshComponentUVE-carrying scene entities, rendered via the same lit/shaded pipeline
        // EngineCoreUVE itself uses at runtime (Renderer3DUVE::RenderFrameToTargetUVE), layered on
        // top of the grid/gizmo image above - see EditorMeshLayerUVE's own header comment. While the
        // Game workspace tab is active, render through the scene's own camera instead of the
        // editor's free-look OrbitCamera - see FindGameCameraEntityUVE's own comment for the "first
        // camera found" convention; EditorMeshLayerUVE itself falls back to the OrbitCamera-synced
        // view if the scene has no usable camera, so a Play session with no authored camera still
        // shows something instead of a blank panel.
        const std::optional<UVE::Scene::EntityUVE> gameCameraOverride =
            gameWorkspaceActive_ ? FindGameCameraEntityUVE(entityManager_) : std::nullopt;
        const univex::integration::EditorMeshLayerResultUVE meshResult = meshLayer_.RenderUVE(
            camera_, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), gameCameraOverride);
        outUsedSize = UVE::Math::Vector2UVE{static_cast<float>(width), static_cast<float>(height)};

        // Player-facing HUD content only shows during the Game workspace tab's "what a player
        // would see" preview (matching the grid/transform-gizmo hiding above) - drawn via ImGui's
        // own foreground overlay rather than baked into meshResult's texture; see this method's own
        // DrawUIOverlayUVE() comment for why.
        if (gameWorkspaceActive_) {
            DrawUIOverlayUVE();
        }

        if (meshResult.colorTextureId == 0U || !EnsureCompositeResourcesUVE(width, height)) {
            return static_cast<std::uint64_t>(resolveColorTexture_);
        }
        CompositeMeshOverGridUVE(meshResult, width, height, static_cast<GLuint>(previousFbo));
        return static_cast<std::uint64_t>(compositeColorTexture_);
    }

private:
    [[nodiscard]] bool EnsureGlewInitializedUVE() {
        if (glewInitialized_) {
            return true;
        }
        glewExperimental = GL_TRUE;
        if (const GLenum status = glewInit(); status != GLEW_OK) {
            UVE_ERROR("uve_editor_app: glewInit failed for the viewport panel: {}",
                      reinterpret_cast<const char*>(glewGetErrorString(status)));
            return false;
        }
        glGetError(); // discard GLEW's benign core-profile extension-probe error
        glewInitialized_ = true;
        return true;
    }

    [[nodiscard]] bool EnsureRenderPassUVE() {
        if (renderPass_.has_value()) {
            return true;
        }
        std::string error;
        renderPass_ = univex::app::ViewportRenderPass::Create(error);
        if (!renderPass_.has_value()) {
            UVE_ERROR("uve_editor_app: viewport render pass init failed: {}", error);
            return false;
        }
        renderPass_->SetEntitySource(&entitySource_);
        return true;
    }

    [[nodiscard]] bool EnsureFramebuffersUVE(const int width, const int height) {
        if (width == framebufferWidth_ && height == framebufferHeight_ && resolveFbo_ != 0U) {
            return true;
        }
        DestroyFramebuffersUVE();
        framebufferWidth_ = width;
        framebufferHeight_ = height;

        GLint maxSamples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        const GLsizei samples = std::min(maxSamples, 8);

        glGenFramebuffers(1, &msaaFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        glGenRenderbuffers(1, &msaaColorRb_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaColorRb_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColorRb_);
        glGenRenderbuffers(1, &msaaDepthRb_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaDepthRb_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaaDepthRb_);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            UVE_ERROR("uve_editor_app: viewport MSAA framebuffer incomplete");
            return false;
        }

        glGenFramebuffers(1, &resolveFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo_);
        glGenTextures(1, &resolveColorTexture_);
        glBindTexture(GL_TEXTURE_2D, resolveColorTexture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveColorTexture_, 0);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (!complete) {
            UVE_ERROR("uve_editor_app: viewport resolve framebuffer incomplete");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return complete;
    }

    void DestroyFramebuffersUVE() {
        if (resolveColorTexture_ != 0U) {
            glDeleteTextures(1, &resolveColorTexture_);
        }
        if (resolveFbo_ != 0U) {
            glDeleteFramebuffers(1, &resolveFbo_);
        }
        if (msaaColorRb_ != 0U) {
            glDeleteRenderbuffers(1, &msaaColorRb_);
        }
        if (msaaDepthRb_ != 0U) {
            glDeleteRenderbuffers(1, &msaaDepthRb_);
        }
        if (msaaFbo_ != 0U) {
            glDeleteFramebuffers(1, &msaaFbo_);
        }
        resolveColorTexture_ = resolveFbo_ = msaaColorRb_ = msaaDepthRb_ = msaaFbo_ = 0U;
        framebufferWidth_ = framebufferHeight_ = 0;
        if (compositeColorTexture_ != 0U) {
            glDeleteTextures(1, &compositeColorTexture_);
        }
        if (compositeFbo_ != 0U) {
            glDeleteFramebuffers(1, &compositeFbo_);
        }
        compositeColorTexture_ = compositeFbo_ = 0U;
        compositeWidth_ = compositeHeight_ = 0;
    }

    // Lazily builds the compositing shader/VAO once (not size-dependent) and (re)creates the
    // composite FBO+color-texture pair whenever the panel's reported size changes - same shape as
    // EnsureFramebuffersUVE above, kept separate since this pair's lifetime is independent of the
    // MSAA/resolve pair (this one only needs recreating, never touched by the grid render pass).
    [[nodiscard]] bool EnsureCompositeResourcesUVE(const int width, const int height) {
        if (!compositeProgram_.has_value()) {
            std::string error;
            compositeProgram_ = univex::render::ShaderProgram::Build(kCompositeVertexShaderUVE,
                                                                      kCompositeFragmentShaderUVE, error);
            if (!compositeProgram_.has_value()) {
                UVE_ERROR("uve_editor_app: viewport composite shader build failed: {}", error);
                return false;
            }
            glGenVertexArrays(1, &compositeVao_);
        }
        if (width == compositeWidth_ && height == compositeHeight_ && compositeFbo_ != 0U) {
            return true;
        }
        if (compositeColorTexture_ != 0U) {
            glDeleteTextures(1, &compositeColorTexture_);
        }
        if (compositeFbo_ != 0U) {
            glDeleteFramebuffers(1, &compositeFbo_);
        }
        compositeWidth_ = width;
        compositeHeight_ = height;

        glGenFramebuffers(1, &compositeFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, compositeFbo_);
        glGenTextures(1, &compositeColorTexture_);
        glBindTexture(GL_TEXTURE_2D, compositeColorTexture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, compositeColorTexture_, 0);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (!complete) {
            UVE_ERROR("uve_editor_app: viewport composite framebuffer incomplete");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return complete;
    }

    // Draws the fullscreen depth-tested composite pass: `resolveColorTexture_` (grid/gizmos) under
    // `meshResult`'s color, selected per-pixel by `meshResult`'s own depth (see the shader source
    // above for why only the mesh layer's depth is ever read).
    void CompositeMeshOverGridUVE(const univex::integration::EditorMeshLayerResultUVE& meshResult, const int width,
                                  const int height, const GLuint restoreFbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, compositeFbo_);
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        compositeProgram_->Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, resolveColorTexture_);
        glUniform1i(compositeProgram_->UniformLocation("uGridColor"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, meshResult.colorTextureId);
        glUniform1i(compositeProgram_->UniformLocation("uMeshColor"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, meshResult.depthTextureId);
        glUniform1i(compositeProgram_->UniformLocation("uMeshDepth"), 2);
        glBindVertexArray(compositeVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
        glBindFramebuffer(GL_FRAMEBUFFER, restoreFbo);
    }

    // Uploads UI::UIFontAtlasUVE's baked RGBA8 bitmap once (it never changes after construction),
    // for AddImage()'s glyph quads below.
    void EnsureUIFontAtlasTextureUVE(const UVE::UI::UIFontAtlasUVE& fontAtlas) {
        if (uiFontAtlasTexture_ != 0U || !fontAtlas.IsValidUVE()) {
            return;
        }
        glGenTextures(1, &uiFontAtlasTexture_);
        glBindTexture(GL_TEXTURE_2D, uiFontAtlasTexture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, UVE::UI::UIFontAtlasUVE::kAtlasWidthUVE,
                     UVE::UI::UIFontAtlasUVE::kAtlasHeightUVE, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     fontAtlas.GetBitmapUVE().data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Draws UIRuntimeUVE's current draw batch directly via ImGui's own foreground overlay draw
    // list, in the same real-window pixel coordinates UIQuadUVE is authored in (matching
    // IInputSystemUVE::GetMousePositionUVE()'s own convention - confirmed directly: hovering the
    // real cursor at a button's authored positionPixels correctly sets isHovered, proving that
    // space really is the whole application window, not this one panel's local render-target
    // space). This is why UI content is NOT baked into meshLayer_'s own offscreen texture (see
    // EditorMeshLayerUVE::RenderUVE()'s own call site comment) - a per-pixel depth-based compositor
    // could never distinguish "UI was drawn here" from "nothing was drawn here" without real
    // OpenGL depth WRITES, which require depth TESTING to also be enabled - exactly what a
    // screen-space overlay must never have (it always draws on top, regardless of the 3D scene).
    // Image quads referencing a real (non-zero) texture asset guid fall back to a flat tint here
    // (this preview path does not resolve/upload arbitrary imported textures) - an honest, stated
    // limitation, not a silent gap.
    void DrawUIOverlayUVE() {
        const UVE::UI::UIDrawBatchUVE& batch = engine_.GetUIRuntimeUVE().GetDrawBatchUVE();
        if (batch.quads.empty()) {
            return;
        }
        EnsureUIFontAtlasTextureUVE(engine_.GetUIRuntimeUVE().GetFontAtlasUVE());
        ImDrawList* const drawList = ImGui::GetForegroundDrawList();
        for (const UVE::UI::UIQuadUVE& quad : batch.quads) {
            const ImVec2 pMin{quad.positionPixels.x, quad.positionPixels.y};
            const ImVec2 pMax{quad.positionPixels.x + quad.sizePixels.x, quad.positionPixels.y + quad.sizePixels.y};
            const ImU32 tint = ImGui::ColorConvertFloat4ToU32(
                ImVec4{quad.color.x, quad.color.y, quad.color.z, quad.alpha});
            if (quad.kind == UVE::UI::UIDrawItemKindUVE::Glyph && uiFontAtlasTexture_ != 0U) {
                drawList->AddImage(static_cast<ImTextureID>(static_cast<std::uintptr_t>(uiFontAtlasTexture_)), pMin,
                                   pMax, ImVec2{quad.u0, quad.v0}, ImVec2{quad.u1, quad.v1}, tint);
            } else {
                drawList->AddRectFilled(pMin, pMax, tint);
            }
        }
    }

    // Applies EditorUVE's own generic overlay-toolbar state (see ViewportOverlayStateUVE's doc
    // comment on why it's plain enums/bools rather than any Viewport-module type) to the real
    // ViewportRenderPass each frame. Snap is stored and reflected in the bubble's highlight but
    // has no behavioral effect yet: there is no drag-to-move gizmo interaction implemented in this
    // slice for it to snap - the gizmo is currently a visual overlay only, not yet draggable.
    void ApplyOverlayStateUVE(const UVE::Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
        auto& settings = renderPass_->Settings();
        settings.projection = overlayState.orthographic ? univex::viewport::ProjectionMode::Orthographic
                                                        : univex::viewport::ProjectionMode::Perspective;
        // The Game workspace tab previews what a player would see - no editor-only grid overlay.
        settings.viewGrid = overlayState.gridVisible && !overlayState.gameWorkspaceActive;
        gameWorkspaceActive_ = overlayState.gameWorkspaceActive;
        using UVE::Editor::EditorUVE;
        switch (overlayState.gizmoMode) {
            case EditorUVE::ViewportGizmoModeUVE::Move:
                renderPass_->SetGizmoMode(univex::gizmo::GizmoMode::Move);
                break;
            case EditorUVE::ViewportGizmoModeUVE::Rotate:
                renderPass_->SetGizmoMode(univex::gizmo::GizmoMode::Rotate);
                break;
            case EditorUVE::ViewportGizmoModeUVE::Scale:
                renderPass_->SetGizmoMode(univex::gizmo::GizmoMode::Scale);
                break;
            case EditorUVE::ViewportGizmoModeUVE::Universal:
                renderPass_->SetGizmoMode(univex::gizmo::GizmoMode::Universal);
                break;
        }
    }

    // Only shows the transform gizmo (and its center pivot cube) while a real entity is selected
    // in EditorUVE, like a Node3D-style engine - the reference standalone demo always draws it at
    // the camera's own orbit target since it has no independent "selected object" concept, which
    // read as a stray gizmo floating with nothing selected once wired into a real editor.
    // Repositions the gizmo to the selected entity's actual world transform via
    // SetGizmoPivotOverride() (see that method's own comment on why camera.Target() alone isn't
    // enough - orbiting the camera must not drag a selected object's gizmo along with it).
    void UpdateSelectionGizmoUVE() {
        const UVE::Scene::EntityUVE selected = editor_.GetSelectedEntityUVE();
        const bool hasSelection = selected != UVE::Scene::kInvalidEntityUVE &&
                                  entityManager_.HasComponentUVE<UVE::Scene::WorldTransformComponentUVE>(selected);
        renderPass_->Settings().viewTransformGizmo = hasSelection && !gameWorkspaceActive_;
        if (hasSelection) {
            const auto& worldTransform =
                entityManager_.GetComponentUVE<UVE::Scene::WorldTransformComponentUVE>(selected);
            renderPass_->SetGizmoPivotOverride(
                univex::math::Vec3{worldTransform.worldPosition.x, worldTransform.worldPosition.y,
                                   worldTransform.worldPosition.z});
        } else {
            renderPass_->SetGizmoPivotOverride(std::nullopt);
        }
    }

    // Mirrors app/main.cpp's own GLFW mouse-button/scroll wiring (left-drag orbits, middle/right
    // drag pans, wheel dollies) but sourced from ImGui's IO instead of GLFW callbacks, since input
    // flows through ImGui while the viewport is docked. Called from inside EditorUVE's own
    // ImGui::Begin("Viewport")/End() block (via the render callback this class provides to
    // SetViewportPanelRendererUVE), so ImGui::IsWindowHovered() here correctly reports hover over
    // the Viewport panel specifically. Known simplification versus the GLFW demo: a drag that
    // began inside the panel stops orbiting the moment the cursor leaves it, rather than
    // continuing to track a global drag - acceptable for this integration slice.
    void UpdateCameraFromMouseUVE(const int framebufferHeight) {
        if (!ImGui::IsWindowHovered()) {
            return;
        }
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0F)) {
            camera_.Orbit(io.MouseDelta.x, io.MouseDelta.y);
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0F) ||
                   ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0F)) {
            camera_.Pan(io.MouseDelta.x, io.MouseDelta.y, framebufferHeight);
        }
        if (io.MouseWheel != 0.0F) {
            camera_.Dolly(io.MouseWheel);
        }
    }

    UVE::Editor::EditorUVE& editor_;
    UVE::Core::EngineCoreUVE& engine_;
    UVE::Scene::IEntityManagerUVE& entityManager_;
    univex::integration::EntityManagerEntitySource entitySource_;
    univex::integration::EditorMeshLayerUVE meshLayer_;
    std::optional<univex::app::ViewportRenderPass> renderPass_;
    // Set each frame by ApplyOverlayStateUVE(), read by UpdateSelectionGizmoUVE() so it can force
    // the transform gizmo off while the Game workspace tab is active (see ApplyOverlayStateUVE's
    // own comment - it already forces the grid off directly, but the gizmo's visibility is decided
    // later in the same frame by selection state, so it needs this stored flag instead).
    bool gameWorkspaceActive_ = false;
    univex::camera::OrbitCamera camera_;
    bool glewInitialized_ = false;
    GLuint msaaFbo_ = 0U;
    GLuint msaaColorRb_ = 0U;
    GLuint msaaDepthRb_ = 0U;
    GLuint resolveFbo_ = 0U;
    GLuint resolveColorTexture_ = 0U;
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
    std::optional<univex::render::ShaderProgram> compositeProgram_;
    GLuint compositeVao_ = 0U;
    GLuint compositeFbo_ = 0U;
    GLuint compositeColorTexture_ = 0U;
    int compositeWidth_ = 0;
    int compositeHeight_ = 0;
    // Uploaded lazily on first use by DrawUIOverlayUVE() - see that method's own comment for why
    // the editor keeps its own copy of this texture rather than reading Renderer3DUVE's internal
    // one (created only inside its "UIOverlay" render-graph pass, which this panel deliberately
    // never runs - see EditorMeshLayerUVE::RenderUVE()'s own call site comment).
    GLuint uiFontAtlasTexture_ = 0U;
};

struct EditorLaunchOptionsUVE final {
    std::filesystem::path scenePath = "editor_scene.uvescene";
    std::optional<int> frameLimit;
    std::optional<std::uint32_t> glMajor;
    std::optional<std::uint32_t> glMinor;
    bool headless = false;
    bool bridgeStdio = false;
};

[[nodiscard]] bool ParseGlVersionUVE(const std::string_view value, std::uint32_t& major,
                                     std::uint32_t& minor) {
    const std::size_t separator = value.find('.');
    if (separator == std::string_view::npos || separator == 0U || separator + 1U >= value.size()) {
        return false;
    }

    const std::string_view majorText = value.substr(0U, separator);
    const std::string_view minorText = value.substr(separator + 1U);
    const auto [majorEnd, majorError] =
        std::from_chars(majorText.data(), majorText.data() + majorText.size(), major);
    const auto [minorEnd, minorError] =
        std::from_chars(minorText.data(), minorText.data() + minorText.size(), minor);
    return majorError == std::errc{} && minorError == std::errc{} &&
           majorEnd == majorText.data() + majorText.size() && minorEnd == minorText.data() + minorText.size();
}

[[nodiscard]] EditorLaunchOptionsUVE ParseOptionsUVE(const int argc, char** argv) {
    EditorLaunchOptionsUVE options{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--headless") {
            options.headless = true;
            continue;
        }
        if (argument == "--bridge-stdio") {
            options.bridgeStdio = true;
            options.headless = true;
            continue;
        }
        if (argument == "--scene" && index + 1 < argc) {
            options.scenePath = argv[++index];
            continue;
        }
        if (argument == "--frames" && index + 1 < argc) {
            int frameLimit = 0;
            const std::string_view value{argv[++index]};
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), frameLimit);
            if (error == std::errc{} && end == value.data() + value.size() && frameLimit >= 0) {
                options.frameLimit = frameLimit;
            }
            continue;
        }
        if (argument == "--gl-version" && index + 1 < argc) {
            std::uint32_t major = 0;
            std::uint32_t minor = 0;
            if (ParseGlVersionUVE(argv[++index], major, minor)) {
                options.glMajor = major;
                options.glMinor = minor;
            }
        }
    }
    if (options.headless && !options.frameLimit.has_value()) {
        options.frameLimit = 1;
    }
    return options;
}

} // namespace

/// Starts the standalone UniVex Editor Foundation v1. `--scene <path>` selects the `.uvescene`
/// document. `--frames <n>` bounds a run for automation, while the normal windowed invocation runs
/// until the user closes the editor. `--gl-version <major.minor>` overrides the requested desktop
/// OpenGL version for an explicitly chosen platform capability (for example virtual-display CI).
/// `--headless` keeps the editor's non-visual lifecycle usable in CI and defaults to a single frame.
/// `--bridge-stdio` always implies headless mode and runs a framed JSON-RPC bridge server instead
/// of constructing native ImGui/GLFW presentation for this process.
///
/// The editor drives EngineCoreUVE's lifecycle by hand (Init()/Load()/TickFrameUVE()) rather than
/// through RunUVE() — the one desktop entry point that does not automatically get RunUVE()'s
/// built-in exception boundary (see EngineCoreUVE::RunUVE()'s doc comment) — so this function
/// wraps that entire hand-driven lifecycle in its own boundary below, following the same shape:
/// log via UVE_FATAL, still run Shutdown() if (and only if) the engine had reached
/// EngineStateUVE::Running by the time something threw, and return
/// EngineCoreUVE::kUnhandledExceptionExitCodeUVE instead of letting the exception unwind out of
/// main() into std::terminate().
int main(const int argc, char** argv) {
    const EditorLaunchOptionsUVE options = ParseOptionsUVE(argc, argv);

    UVE::Core::EngineConfigUVE config{};
    config.logFilePath = "uve_editor.log";
    config.enableConsoleLogging = !options.bridgeStdio;
    config.headlessUVE = options.headless;
    config.commandLineArgs = std::vector<std::string>(argv + 1, argv + argc);
    if (options.glMajor.has_value() && options.glMinor.has_value()) {
        config.windowGlVersionMajor = *options.glMajor;
        config.windowGlVersionMinor = *options.glMinor;
    }

    UVE::Core::EngineCoreUVE engine(config);
    try {
        engine.Init();
        if (!engine.Load()) {
            engine.Shutdown();
            return 1;
        }

        UVE::Editor::EditorUVE editor(engine.GetServicesUVE(), options.scenePath, 100U, &engine);
        editor.InitUVE();

        if (std::filesystem::exists(options.scenePath)) {
            static_cast<void>(editor.LoadSceneUVE());
        }

        if (options.bridgeStdio) {
            UVE::Asset::DataTableRegistryUVE dataTableRegistry;
            UVE::Editor::EditorBridgeUVE bridge(editor, &dataTableRegistry);
            UVE::Editor::EditorBridgeStdioServerUVE server(bridge);
            const int result = server.ServeUVE(std::cin, std::cout, std::cerr);
            editor.ShutdownUVE();
            engine.Shutdown();
            return result;
        }

        // EngineCoreUVE's own scene render stays a documented no-op (the editor doesn't own a
        // gameplay camera), so the Viewport panel's real content comes entirely from
        // ViewportPanelBackendUVE below - grid, orbit camera, gizmos, and one proxy cube per live
        // scene entity, composited via ImGui::Image() rather than EngineCoreUVE's own render
        // target. Only constructed in real windowed mode: it needs an actual current GL context,
        // which headless mode's NullRenderDeviceUVE never creates.
        std::optional<ViewportPanelBackendUVE> viewportBackend;
        if (!options.headless) {
            viewportBackend.emplace(editor, engine);
            editor.SetViewportPanelRendererUVE(
                [&backend = *viewportBackend](
                    const UVE::Math::Vector2UVE& availableSize, UVE::Math::Vector2UVE& outUsedSize,
                    const UVE::Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
                    return backend.RenderUVE(availableSize, outUsedSize, overlayState);
                });
        }
        engine.SetPostRenderCallbackUVE([&editor] { editor.RenderOverlayUVE(); });

        int framesRun = 0;
        while (!engine.GetServicesUVE().GetWindowManagerUVE().IsCloseRequestedUVE() &&
               (!options.frameLimit.has_value() || framesRun < *options.frameLimit)) {
            editor.TickUVE();
            engine.TickFrameUVE();
            ++framesRun;
        }

        engine.SetPostRenderCallbackUVE({});
        editor.SetViewportPanelRendererUVE({});
        // Destroyed here, before engine.Shutdown() tears down the window/GL context below - its
        // destructor deletes real GL objects (framebuffers/textures) that must still be valid.
        viewportBackend.reset();
        editor.ShutdownUVE();
        engine.Shutdown();
        return 0;
    } catch (const std::exception& exception) {
        UVE_FATAL("uve_editor_app: unhandled exception escaped the editor lifecycle - shutting down: {}",
                   exception.what());
    } catch (...) {
        UVE_FATAL("uve_editor_app: unhandled non-std::exception escaped the editor lifecycle - shutting down");
    }

    // Reached only via one of the catches above. Only safe to call Shutdown() if the engine had
    // actually reached Running - see EngineCoreUVE::RunUVE()'s doc comment for why an exception
    // during Init() itself must not force a Shutdown() call. `editor` (and any object declared
    // inside the try block above) is already out of scope here, having been destroyed normally
    // during stack unwinding.
    if (engine.GetStateUVE() == UVE::Core::EngineStateUVE::Running) {
        try {
            engine.Shutdown();
        } catch (const std::exception& exception) {
            UVE_FATAL("uve_editor_app: engine.Shutdown() itself threw while recovering from the exception "
                       "above: {}",
                       exception.what());
        } catch (...) {
            UVE_FATAL("uve_editor_app: engine.Shutdown() itself threw a non-std::exception while recovering "
                       "from the exception above");
        }
    }
    return UVE::Core::EngineCoreUVE::kUnhandledExceptionExitCodeUVE;
}
