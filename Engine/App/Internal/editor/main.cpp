// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// GL/glew.h (pulled in via GlApi.h) must be included before anything that might transitively pull
// <GLFW/glfw3.h>, or GLFW's own bundled GL header conflicts with GLEW's - see GlApi.h's own
// comment. Nothing else in this file currently drags in GLFW, but this ordering is cheap
// insurance and matches every other translation unit in Engine/Editor/Viewport that mixes the two.
#include "univex/render/GlApi.h"

#include <algorithm>
#include <charconv>
#include <cmath>
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
#include "integration/EntityPicker.h"
#include "univex/camera/ViewportMetrics.h"
#include "univex/gizmo/GizmoDrag.h"
#include "univex/gizmo/GizmoPicking.h"
#include "integration/MathConversions.h"
#include "univex/camera/OrbitCamera.h"
#include "univex/render/ShaderProgram.h"

#include "uve/core/engine_core_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/editor/editor_bridge_stdio_uve.h"
#include "uve/ui/ui_draw_batch_uve.h"
#include "uve/ui/ui_font_atlas_uve.h"
#include "uve/editor/editor_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/scene/i_scene_graph_uve.h"

namespace {

// How far the pointer may travel on the nav gizmo (accumulated since the button went down, in
// ImGui points) and still count as a click rather than a drag - mirrors app/main.cpp's own
// kNavClickSlopPixels for the standalone demo.
constexpr float kNavClickSlopPixelsUVE = 4.0F;

// Fullscreen-triangle pass that injects EditorMeshLayerUVE's real mesh/material render into the
// viewport's own framebuffer, colour AND depth, so that the editor's grid and gizmos share one
// depth buffer with the engine's scene geometry instead of being reconciled against it afterwards.
// No vertex buffer needed (same gl_VertexID trick already used by Renderer3DUVE's own internal
// tonemap/fullscreen-quad shader).
constexpr std::string_view kMeshBlitVertexShaderUVE = R"(#version 330 core
void main() {
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Coverage comes from alpha, not depth: Renderer3DUVE's tone-mapping pass reports which pixels it
// actually drew by writing alpha 1 there and 0 elsewhere (see fullscreen_quad.glsl), because the
// depth attachment a caller hands RenderFrameToTargetUVE is cleared by that pass and never
// written, and the scene's own clear colour is indistinguishable from dark geometry.
//
// Covered pixels are pushed to the near plane rather than carried at their real depth, so scene
// geometry occludes the grid drawn after it. That is correct for opaque geometry above the ground
// plane, which is every case the editor can currently author; geometry below y=0 will hide grid
// lines that should cross in front of it. Fixing that properly needs the renderer to export real
// per-pixel depth, which in turn needs a depth-compare mode the RHI does not expose yet.
constexpr std::string_view kMeshBlitFragmentShaderUVE = R"(#version 330 core
uniform sampler2D uMeshColor;
out vec4 FragColor;
void main() {
    vec4 meshColor = texelFetch(uMeshColor, ivec2(gl_FragCoord.xy), 0);
    if (meshColor.a < 0.5) {
        discard; // the renderer drew nothing here; leave the backdrop and its depth alone
    }
    FragColor = vec4(meshColor.rgb, 1.0);
    gl_FragDepth = 0.0;
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
          meshLayer_(engine.GetServicesUVE()) {}

    ~ViewportPanelBackendUVE() {
        DestroyFramebuffersUVE();
        if (meshBlitVao_ != 0U) {
            glDeleteVertexArrays(1, &meshBlitVao_);
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
        const bool navGizmoOwnsGesture = UpdateNavGizmoInteractionUVE(width, height);
        // A handle drag outranks both: grabbing an arrow must not also orbit the camera or, on
        // release, register as a click that selects whatever is behind the gizmo.
        const bool gizmoOwnsGesture = !navGizmoOwnsGesture && UpdateGizmoDragUVE(width, height);
        const bool pointerTaken = navGizmoOwnsGesture || gizmoOwnsGesture;
        UpdateSelectionFromMouseUVE(width, height, pointerTaken);
        UpdateEntityContextToolbarFromMouseUVE(width, height, pointerTaken);
        UpdateCameraFromMouseUVE(height, pointerTaken);
        UpdateViewportBookmarkHotkeysUVE();
        // Advances the eased snap-to-axis animation SnapToDirection() starts (a manual orbit/pan
        // cancels it instead - see OrbitCamera.cpp) - without this the camera would flag itself
        // "animating" and then never actually move, since nothing else ticks it forward. Mirrors
        // app/main.cpp's own per-frame state.camera.Update(deltaSeconds) call in the standalone demo.
        camera_.Update(ImGui::GetIO().DeltaTime);

        // Real scene entities, rendered via the same lit/shaded pipeline EngineCoreUVE itself uses
        // at runtime (Renderer3DUVE::RenderFrameToTargetUVE) - see EditorMeshLayerUVE's own header
        // comment. This runs first because its colour and depth are injected into the viewport's
        // framebuffer below, between the backdrop and the grid, so the grid depth-tests against
        // real geometry. While the Game workspace tab is active, render through the scene's own
        // camera instead of the editor's free-look OrbitCamera - see FindGameCameraEntityUVE's own
        // comment for the "first camera found" convention; EditorMeshLayerUVE itself falls back to
        // the OrbitCamera-synced view if the scene has no usable camera, so a Play session with no
        // authored camera still shows something instead of a blank panel.
        const std::optional<UVE::Scene::EntityUVE> gameCameraOverride =
            gameWorkspaceActive_ ? FindGameCameraEntityUVE(entityManager_) : std::nullopt;
        const univex::integration::EditorMeshLayerResultUVE meshResult = meshLayer_.RenderUVE(
            camera_, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), gameCameraOverride);
        outUsedSize = UVE::Math::Vector2UVE{static_cast<float>(width), static_cast<float>(height)};

        // One framebuffer, one depth buffer, drawn back to front: backdrop, then the engine's
        // scene geometry, then the grid (which depth-tests against it), then the gizmos last of
        // all so nothing can paint over them.
        GLint previousFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        glEnable(GL_MULTISAMPLE);
        renderPass_->ClearUVE(width, height);
        renderPass_->RenderBackgroundUVE();
        if (meshResult.colorTextureId != 0U && EnsureMeshBlitResourcesUVE()) {
            BlitMeshLayerUVE(meshResult);
        }
        renderPass_->RenderGridUVE(camera_, width, height);
        renderPass_->RenderOverlayUVE(camera_, width, height);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));

        // Player-facing HUD content only shows during the Game workspace tab's "what a player
        // would see" preview (matching the grid/transform-gizmo hiding above) - drawn via ImGui's
        // own foreground overlay rather than baked into meshResult's texture; see this method's own
        // DrawUIOverlayUVE() comment for why.
        if (gameWorkspaceActive_) {
            DrawUIOverlayUVE();
        }

        return static_cast<std::uint64_t>(resolveColorTexture_);
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
    }

    // Lazily builds the mesh-blit shader and its empty VAO once. Nothing here is size-dependent:
    // the pass draws straight into the viewport's own MSAA framebuffer, so there is no second
    // colour target to keep in step with the panel's size.
    [[nodiscard]] bool EnsureMeshBlitResourcesUVE() {
        if (meshBlitProgram_.has_value()) {
            return true;
        }
        std::string error;
        meshBlitProgram_ = univex::render::ShaderProgram::Build(kMeshBlitVertexShaderUVE,
                                                                 kMeshBlitFragmentShaderUVE, error);
        if (!meshBlitProgram_.has_value()) {
            UVE_ERROR("uve_editor_app: viewport mesh blit shader build failed: {}", error);
            return false;
        }
        glGenVertexArrays(1, &meshBlitVao_);
        return true;
    }

    // Writes EditorMeshLayerUVE's colour and depth into the currently bound framebuffer, so the
    // grid and gizmos that follow share one depth buffer with the engine's scene geometry. Depth
    // testing stays off: the mesh layer's own depth already resolved visibility between meshes,
    // and at this point in the frame only the backdrop is underneath.
    void BlitMeshLayerUVE(const univex::integration::EditorMeshLayerResultUVE& meshResult) {
        // GL_ALWAYS rather than disabling the test: OpenGL skips depth-buffer writes entirely
        // while GL_DEPTH_TEST is disabled, whatever the write mask says, and the depth this pass
        // writes is the whole reason it exists.
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_ALWAYS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        meshBlitProgram_->Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, meshResult.colorTextureId);
        glUniform1i(meshBlitProgram_->UniformLocation("uMeshColor"), 0);
        glBindVertexArray(meshBlitVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
        // The grid depth-tests against what this pass just wrote, so put the comparison back.
        glDepthFunc(GL_LESS);
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
    // ViewportRenderPass each frame. Snap is no longer decorative: the bubble now writes through
    // to EditorUVE's real snapping settings, which is what quantises a handle drag (see
    // UpdateGizmoDragUVE).
    void ApplyOverlayStateUVE(const UVE::Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
        auto& settings = renderPass_->Settings();
        settings.projection = overlayState.orthographic ? univex::viewport::ProjectionMode::Orthographic
                                                        : univex::viewport::ProjectionMode::Perspective;
        // The Game workspace tab previews what a player would see - no editor-only grid overlay.
        settings.viewGrid = overlayState.gridVisible && !overlayState.gameWorkspaceActive;
        gameWorkspaceActive_ = overlayState.gameWorkspaceActive;
        pointerOverOverlay_ = overlayState.pointerOverOverlay;
        // Axis colours drive the gizmo AND the grid's own axis lines, so they go through
        // SetAxisPaletteUVE rather than being written into either one directly - see that method.
        // Skipped until the host has seeded the real defaults, so an unset state cannot paint
        // every axis black (see ViewportOverlayStateUVE::axisColorsValid).
        if (overlayState.axisColorsValid) {
            static_cast<void>(renderPass_->SetAxisPaletteUVE(univex::viewport::AxisPaletteUVE{
                univex::viewport::AxisRgbUVE{overlayState.axisColorX.r, overlayState.axisColorX.g,
                                             overlayState.axisColorX.b},
                univex::viewport::AxisRgbUVE{overlayState.axisColorY.r, overlayState.axisColorY.g,
                                             overlayState.axisColorY.b},
                univex::viewport::AxisRgbUVE{overlayState.axisColorZ.r, overlayState.axisColorZ.g,
                                             overlayState.axisColorZ.b}}));
        } else {
            SeedEditorAxisColorsFromDefaultsUVE();
        }
        std::vector<univex::gizmo::BoneOverlayUVE> bones;
        bones.reserve(overlayState.bones.size());
        const auto toVec3 = [](const std::array<float, 3>& value) { return univex::math::Vec3{value[0], value[1], value[2]}; };
        for (const auto& bone : overlayState.bones) {
            bones.push_back(univex::gizmo::BoneOverlayUVE{toVec3(bone.head), toVec3(bone.tail), toVec3(bone.side),
                                                          bone.hasLink, toVec3(bone.linkFrom), bone.skeletonSelected,
                                                          bone.boneSelected});
        }
        renderPass_->SetBonesUVE(std::move(bones));
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

    // Hands EditorCore the viewport's own default axis hues. It cannot name them itself -
    // AxisPalette.h belongs to the Viewport module EditorCore deliberately does not depend on -
    // so this side of the boundary supplies them, and the menu bar's picker edits from there.
    //
    // Driven by the `axisColorsValid == false` branch above rather than called once at startup, so
    // one line covers both cases that need it: the first frame, and "Reset to defaults", which
    // clears the flag precisely so these defaults come back from the one place that owns them.
    void SeedEditorAxisColorsFromDefaultsUVE() {
        const univex::viewport::AxisPaletteUVE defaults;
        using EditorAxisColorUVE = UVE::Editor::EditorUVE::ViewportAxisColorUVE;
        const auto toEditor = [](const univex::viewport::AxisRgbUVE& color) {
            return EditorAxisColorUVE{color.r, color.g, color.b};
        };
        static_cast<void>(editor_.SetViewportAxisColorsUVE(
            toEditor(defaults.x), toEditor(defaults.y), toEditor(defaults.z)));
    }

    // Only shows the transform gizmo (and its pivot dot) while a real entity is selected
    // in EditorUVE, like a Node3D-style engine - the reference standalone demo always draws it at
    // the camera's own orbit target since it has no independent "selected object" concept, which
    // read as a stray gizmo floating with nothing selected once wired into a real editor.
    // Repositions the gizmo to the selected entity's actual world transform via
    // SetGizmoPivotOverride() (see that method's own comment on why camera.Target() alone isn't
    // enough - orbiting the camera must not drag a selected object's gizmo along with it).
    void UpdateSelectionGizmoUVE() {
        // The pivot is the centroid of everything selected, not the active entity's own position.
        // With one node the two are identical; with several, using the active entity put the gizmo
        // on whichever node happened to be clicked last, sitting off to one side of the group it
        // claims to represent.
        univex::math::Vec3 centroid{0.0F, 0.0F, 0.0F};
        int contributing = 0;
        for (const UVE::Scene::EntityUVE entity : editor_.GetSelectedEntitiesUVE()) {
            if (entity == UVE::Scene::kInvalidEntityUVE ||
                !entityManager_.HasComponentUVE<UVE::Scene::WorldTransformComponentUVE>(entity)) {
                continue;
            }
            const auto& worldTransform =
                entityManager_.GetComponentUVE<UVE::Scene::WorldTransformComponentUVE>(entity);
            centroid += univex::integration::FromUveVector3UVE(worldTransform.worldPosition);
            ++contributing;
        }

        const bool hasSelection = contributing > 0;
        renderPass_->Settings().viewTransformGizmo = hasSelection && !gameWorkspaceActive_;
        if (hasSelection) {
            gizmoPivot_ = centroid * (1.0F / static_cast<float>(contributing));
            renderPass_->SetGizmoPivotOverride(gizmoPivot_);
        } else {
            gizmoPivot_.reset();
            renderPass_->SetGizmoPivotOverride(std::nullopt);
        }
    }

    // Drags a transform handle.
    //
    // The gizmo has been drawable but not touchable: geometry was produced and nothing ever asked
    // what the pointer was on. This closes that loop - pick a handle, project the cursor ray onto
    // it each frame, and feed the result through EditorUVE's gesture API so the whole drag is one
    // undo step.
    //
    // Every number here is the one the renderer drew with, taken from the same helpers
    // (ScaleForPixelRadius / WorldPerPixelAtPointUVE at the gizmo's own pivot), so the region that
    // responds is the region that is visible.
    //
    // Returns true while a drag owns the pointer, which suppresses both orbit and click-to-select.
    [[nodiscard]] bool UpdateGizmoDragUVE(const int width, const int height) {
        if (dragHandle_ != univex::gizmo::GizmoHandleUVE::None) {
            return ContinueGizmoDragUVE(width, height);
        }
        return TryBeginGizmoDragUVE(width, height);
    }

    /// The gizmo's placement this frame: the pivot it is drawn at, the world size of one gizmo
    /// unit, and how many gizmo units a pixel spans. Empty when no gizmo is on screen.
    struct GizmoPlacementUVE final {
        univex::math::Vec3 pivot{};
        float scale = 1.0F;
        float unitsPerPixel = 1.0F;
        univex::math::Vec3 viewDirection{};
    };

    [[nodiscard]] std::optional<GizmoPlacementUVE> CurrentGizmoPlacementUVE(const int height) const {
        if (!gizmoPivot_.has_value() || !renderPass_->Settings().viewTransformGizmo || height <= 0) {
            return std::nullopt;
        }
        GizmoPlacementUVE placement;
        placement.pivot = *gizmoPivot_;
        placement.scale = univex::render::GizmoRenderer::ScaleForPixelRadius(
            camera_, height, renderPass_->Style().gizmoPixelRadius, placement.pivot);
        const float worldPerPixel =
            univex::camera::WorldPerPixelAtPointUVE(camera_, height, placement.pivot);
        placement.unitsPerPixel = (placement.scale > 0.0F) ? worldPerPixel / placement.scale : 1.0F;
        placement.viewDirection = univex::math::Normalize(camera_.Target() - camera_.Eye());
        return placement;
    }

    /// The cursor ray in the viewport's own math kit, for the gizmo code that lives in the
    /// engine-agnostic core.
    void CursorRayUVE(const int width, const int height, const float pixelX, const float pixelY,
                      univex::math::Vec3& outOrigin, univex::math::Vec3& outDirection) const {
        const UVE::Math::RayUVE ray =
            univex::integration::BuildCursorRayUVE(camera_, width, height, pixelX, pixelY);
        outOrigin = univex::integration::FromUveVector3UVE(ray.origin);
        outDirection = univex::integration::FromUveVector3UVE(ray.direction);
    }

    [[nodiscard]] bool TryBeginGizmoDragUVE(const int width, const int height) {
        if (pointerOverOverlay_ || !ImGui::IsWindowHovered() ||
            !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            return false;
        }
        const std::optional<GizmoPlacementUVE> placement = CurrentGizmoPlacementUVE(height);
        if (!placement.has_value()) {
            return false;
        }
        // Universal mode stacks a ring, an arrow and a scale cube on the same axis, and the pick
        // reports only which axis was hit - not which of the three tools. Rather than guess at
        // what the user grabbed, dragging is offered in the explicit Move/Rotate/Scale tools; the
        // universal widget stays a display of all three.
        const univex::gizmo::GizmoMode mode = renderPass_->Mode();
        if (mode != univex::gizmo::GizmoMode::Move && mode != univex::gizmo::GizmoMode::Rotate &&
            mode != univex::gizmo::GizmoMode::Scale) {
            return false;
        }

        const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
        const ImGuiIO& io = ImGui::GetIO();
        univex::math::Vec3 rayOrigin{};
        univex::math::Vec3 rayDirection{};
        CursorRayUVE(width, height, io.MousePos.x - imageOrigin.x, io.MousePos.y - imageOrigin.y,
                     rayOrigin, rayDirection);

        const univex::gizmo::GizmoPickResultUVE pick = univex::gizmo::PickGizmoHandleUVE(
            mode, renderPass_->Style(), rayOrigin, rayDirection, placement->pivot, placement->scale,
            placement->viewDirection, placement->unitsPerPixel);
        if (pick.handle == univex::gizmo::GizmoHandleUVE::None) {
            return false;
        }

        UVE::Editor::EditorToolSessionModeUVE sessionMode{};
        switch (mode) {
            case univex::gizmo::GizmoMode::Move:
                sessionMode = UVE::Editor::EditorToolSessionModeUVE::Translate;
                break;
            case univex::gizmo::GizmoMode::Rotate:
                sessionMode = UVE::Editor::EditorToolSessionModeUVE::Rotate;
                break;
            default:
                sessionMode = UVE::Editor::EditorToolSessionModeUVE::Scale;
                break;
        }

        // Capture where the drag started, in the handle's own terms. Every later frame reports its
        // value the same way and subtracts this one, so the amount fed to the gesture is always
        // measured from the press rather than accumulated frame to frame.
        if (!CaptureDragReferenceUVE(*placement, mode, pick.handle, rayOrigin, rayDirection,
                                     dragPressValue_)) {
            return false;
        }
        if (!editor_.BeginTransformGestureUVE(sessionMode)) {
            return false;
        }

        dragHandle_ = pick.handle;
        dragMode_ = mode;
        dragPivot_ = placement->pivot;
        return true;
    }

    /// One scalar and one point, enough to express every handle's press-time reference: the
    /// distance along an axis, the world point on a plane, or the angle around a ring.
    struct DragReferenceUVE final {
        float scalar = 0.0F;
        univex::math::Vec3 point{};
    };

    [[nodiscard]] bool CaptureDragReferenceUVE(const GizmoPlacementUVE& placement,
                                               const univex::gizmo::GizmoMode mode,
                                               const univex::gizmo::GizmoHandleUVE handle,
                                               const univex::math::Vec3& rayOrigin,
                                               const univex::math::Vec3& rayDirection,
                                               DragReferenceUVE& outReference) const {
        using univex::gizmo::GizmoHandleUVE;

        if (const std::optional<univex::math::Vec3> axis =
                univex::gizmo::AxisDirectionForHandleUVE(handle);
            axis.has_value()) {
            if (mode == univex::gizmo::GizmoMode::Rotate) {
                const std::optional<float> angle = univex::gizmo::ProjectRayOntoRingAngleUVE(
                    rayOrigin, rayDirection, placement.pivot, *axis);
                if (!angle.has_value()) {
                    return false;
                }
                outReference.scalar = *angle;
                return true;
            }
            const std::optional<float> along = univex::gizmo::ProjectRayOntoAxisUVE(
                rayOrigin, rayDirection, placement.pivot, *axis);
            if (!along.has_value()) {
                return false;
            }
            outReference.scalar = *along;
            return true;
        }

        if (const std::optional<univex::math::Vec3> normal = PlaneHandleNormalUVE(handle);
            normal.has_value()) {
            const std::optional<univex::math::Vec3> point = univex::gizmo::ProjectRayOntoPlaneUVE(
                rayOrigin, rayDirection, placement.pivot, *normal);
            if (!point.has_value()) {
                return false;
            }
            outReference.point = *point;
            return true;
        }

        if (handle == GizmoHandleUVE::Uniform && mode == univex::gizmo::GizmoMode::Scale) {
            // Uniform scale has no axis to run along, so it reads the cursor's distance from the
            // pivot in the view plane - drag away from the object to grow it, toward it to shrink.
            const std::optional<univex::math::Vec3> point = univex::gizmo::ProjectRayOntoPlaneUVE(
                rayOrigin, rayDirection, placement.pivot, placement.viewDirection);
            if (!point.has_value()) {
                return false;
            }
            outReference.scalar = univex::math::Length(*point - placement.pivot);
            return true;
        }

        // Anything else (the rotate gizmo's screen-space ring, a plane handle in Scale mode) has
        // no single-axis command to drive yet, so it stays pickable but not draggable rather than
        // being mapped onto an axis it does not mean.
        return false;
    }

    [[nodiscard]] static std::optional<univex::math::Vec3> PlaneHandleNormalUVE(
        const univex::gizmo::GizmoHandleUVE handle) {
        using univex::gizmo::GizmoHandleUVE;
        switch (handle) {
            case GizmoHandleUVE::PlaneXY: return univex::math::Vec3{0.0F, 0.0F, 1.0F};
            case GizmoHandleUVE::PlaneYZ: return univex::math::Vec3{1.0F, 0.0F, 0.0F};
            case GizmoHandleUVE::PlaneZX: return univex::math::Vec3{0.0F, 1.0F, 0.0F};
            default: return std::nullopt;
        }
    }

    [[nodiscard]] static UVE::Editor::EditorTransformAxisUVE EditorAxisForHandleUVE(
        const univex::gizmo::GizmoHandleUVE handle) {
        using univex::gizmo::GizmoHandleUVE;
        switch (handle) {
            case GizmoHandleUVE::AxisX: return UVE::Editor::EditorTransformAxisUVE::X;
            case GizmoHandleUVE::AxisY: return UVE::Editor::EditorTransformAxisUVE::Y;
            case GizmoHandleUVE::AxisZ: return UVE::Editor::EditorTransformAxisUVE::Z;
            default: return UVE::Editor::EditorTransformAxisUVE::None;
        }
    }

    [[nodiscard]] bool ContinueGizmoDragUVE(const int width, const int height) {
        // Esc abandons the drag and puts the object back, the universal convention.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            static_cast<void>(editor_.CancelTransformGestureUVE());
            EndGizmoDragUVE();
            return true;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            static_cast<void>(editor_.CommitTransformGestureUVE());
            EndGizmoDragUVE();
            return true;
        }

        const std::optional<GizmoPlacementUVE> placement = CurrentGizmoPlacementUVE(height);
        if (!placement.has_value()) {
            static_cast<void>(editor_.CancelTransformGestureUVE());
            EndGizmoDragUVE();
            return true;
        }

        const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
        const ImGuiIO& io = ImGui::GetIO();
        univex::math::Vec3 rayOrigin{};
        univex::math::Vec3 rayDirection{};
        CursorRayUVE(width, height, io.MousePos.x - imageOrigin.x, io.MousePos.y - imageOrigin.y,
                     rayOrigin, rayDirection);

        // The pivot is frozen at the press: it moves with the object during a translate, and
        // re-reading it each frame would make the gizmo chase itself.
        ApplyDragPreviewUVE(rayOrigin, rayDirection, placement->viewDirection);
        return true;
    }

    void ApplyDragPreviewUVE(const univex::math::Vec3& rayOrigin,
                             const univex::math::Vec3& rayDirection,
                             const univex::math::Vec3& viewDirection) {
        using univex::gizmo::GizmoHandleUVE;

        if (const std::optional<univex::math::Vec3> normal = PlaneHandleNormalUVE(dragHandle_);
            normal.has_value()) {
            const std::optional<univex::math::Vec3> point =
                univex::gizmo::ProjectRayOntoPlaneUVE(rayOrigin, rayDirection, dragPivot_, *normal);
            if (!point.has_value()) {
                return; // grazing the plane: hold the last good preview rather than jumping
            }
            const univex::math::Vec3 delta = *point - dragPressValue_.point;
            static_cast<void>(editor_.PreviewTranslateGestureUVE(
                univex::integration::ToUveVector3UVE(delta)));
            return;
        }

        if (dragHandle_ == GizmoHandleUVE::Uniform) {
            const std::optional<univex::math::Vec3> point = univex::gizmo::ProjectRayOntoPlaneUVE(
                rayOrigin, rayDirection, dragPivot_, viewDirection);
            if (!point.has_value()) {
                return;
            }
            const float radius = univex::math::Length(*point - dragPivot_);
            static_cast<void>(editor_.PreviewTransformGestureUVE(
                UVE::Editor::EditorTransformAxisUVE::None, radius - dragPressValue_.scalar));
            return;
        }

        const std::optional<univex::math::Vec3> axis =
            univex::gizmo::AxisDirectionForHandleUVE(dragHandle_);
        if (!axis.has_value()) {
            return;
        }
        const UVE::Editor::EditorTransformAxisUVE editorAxis = EditorAxisForHandleUVE(dragHandle_);

        if (dragMode_ == univex::gizmo::GizmoMode::Rotate) {
            const std::optional<float> angle =
                univex::gizmo::ProjectRayOntoRingAngleUVE(rayOrigin, rayDirection, dragPivot_, *axis);
            if (!angle.has_value()) {
                return;
            }
            // The ring's angle wraps at +-pi; a drag reads the short way round, which is the only
            // way a pointer can have travelled between two frames.
            static_cast<void>(editor_.PreviewTransformGestureUVE(
                editorAxis, univex::gizmo::ShortestAngleDeltaUVE(dragPressValue_.scalar, *angle)));
            return;
        }

        const std::optional<float> along =
            univex::gizmo::ProjectRayOntoAxisUVE(rayOrigin, rayDirection, dragPivot_, *axis);
        if (!along.has_value()) {
            return;
        }
        static_cast<void>(
            editor_.PreviewTransformGestureUVE(editorAxis, *along - dragPressValue_.scalar));
    }

    void EndGizmoDragUVE() {
        dragHandle_ = univex::gizmo::GizmoHandleUVE::None;
    }

    // Click-to-select. Until now selection came only from the Hierarchy panel, so the 3D view was
    // something to look at rather than something to work in.
    //
    // The whole difficulty is that the left button already means "orbit". A press is therefore not
    // committed to either meaning until release: travel more than the slop and it was a drag, so
    // the camera keeps it and nothing is selected; release within the slop and it was a click, so
    // it picks. That is the same press-relative accumulation UpdateNavGizmoInteractionUVE uses,
    // and the reason both read GetMouseDragDelta rather than the per-frame delta - a slow drag of
    // many tiny movements never exceeds a per-frame threshold.
    //
    // `suppressed` is true when the nav gizmo already owns this gesture, in which case the click
    // belongs to it and must not also fall through to selection.
    void UpdateSelectionFromMouseUVE(const int width, const int height, const bool suppressed) {
        if (suppressed) {
            selectionPressActive_ = false;
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (!pointerOverOverlay_ && ImGui::IsWindowHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
            selectionPressActive_ = true;
            selectionPressX_ = io.MousePos.x - imageOrigin.x;
            selectionPressY_ = io.MousePos.y - imageOrigin.y;
        }

        if (!selectionPressActive_ || !ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            return;
        }
        selectionPressActive_ = false;

        const ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0F);
        if ((std::fabs(dragDelta.x) + std::fabs(dragDelta.y)) > kNavClickSlopPixelsUVE) {
            return; // that was an orbit, not a click
        }

        const univex::integration::EntityPickResultUVE pick = univex::integration::PickEntityAtPixelUVE(
            entityManager_, camera_, width, height, selectionPressX_, selectionPressY_);

        // Ctrl extends the selection, matching every other multi-select surface in the editor;
        // a plain click replaces it, and a plain click on nothing clears it. Ctrl-clicking empty
        // space deliberately does nothing rather than clearing, so a mis-aimed extend does not
        // throw away a selection the user spent time building.
        const bool extend = io.KeyCtrl;
        if (!pick.hit) {
            if (!extend) {
                editor_.ClearSelectionUVE();
            }
            return;
        }
        if (extend) {
            editor_.ToggleEntitySelectionUVE(pick.entity);
        } else {
            editor_.SelectEntityUVE(pick.entity);
        }
    }

    // Right-click an entity -> select it and open the floating "Scripting" toolbar anchored at
    // its projected screen position (editor_.DrawEntityContextToolbarUVE draws it; this method
    // only decides whether to arm it). Same click-vs-drag shape as UpdateSelectionFromMouseUVE
    // above, but for the right button, which today only drives camera pan on an actual drag
    // (UpdateCameraFromMouseUVE's IsMouseDragging(Right) check below) - a clean right-click
    // currently falls through that check and does nothing, which is exactly the gap this fills.
    //
    // A right-drag beyond the slop is left alone: UpdateCameraFromMouseUVE already panned the
    // camera live during the drag, and an in-flight pan should not also fight whatever toolbar
    // state already existed before the drag started.
    void UpdateEntityContextToolbarFromMouseUVE(const int width, const int height, const bool suppressed) {
        if (suppressed) {
            contextToolbarPressActive_ = false;
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (!pointerOverOverlay_ && ImGui::IsWindowHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
            contextToolbarPressActive_ = true;
            contextToolbarPressX_ = io.MousePos.x - imageOrigin.x;
            contextToolbarPressY_ = io.MousePos.y - imageOrigin.y;
        }

        if (!contextToolbarPressActive_ || !ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            return;
        }
        contextToolbarPressActive_ = false;

        const ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 0.0F);
        if ((std::fabs(dragDelta.x) + std::fabs(dragDelta.y)) > kNavClickSlopPixelsUVE) {
            return; // that was a pan, not a click - leave any existing toolbar state alone
        }

        const univex::integration::EntityPickResultUVE pick = univex::integration::PickEntityAtPixelUVE(
            entityManager_, camera_, width, height, contextToolbarPressX_, contextToolbarPressY_);
        if (!pick.hit) {
            editor_.ClearEntityContextToolbarUVE();
            return;
        }

        editor_.SelectEntityUVE(pick.entity);

        if (!entityManager_.HasComponentUVE<UVE::Scene::WorldTransformComponentUVE>(pick.entity)) {
            editor_.ClearEntityContextToolbarUVE();
            return;
        }
        const auto& worldTransform =
            entityManager_.GetComponentUVE<UVE::Scene::WorldTransformComponentUVE>(pick.entity);
        float anchorPixelX = 0.0F;
        float anchorPixelY = 0.0F;
        if (univex::integration::ProjectWorldPointToPixelUVE(camera_, width, height, worldTransform.worldPosition,
                                                              anchorPixelX, anchorPixelY)) {
            editor_.SetEntityContextToolbarAnchorUVE(pick.entity, anchorPixelX, anchorPixelY);
        } else {
            editor_.ClearEntityContextToolbarUVE();
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
    //
    // `suppressOrbit` is true while UpdateNavGizmoInteractionUVE() below owns the current left-
    // button gesture (it started on the nav gizmo) - without it, this method's own
    // IsMouseDragging(Left) check would ALSO orbit the camera from the same drag, double-applying
    // the same mouse delta on top of the nav gizmo's own orbit-while-dragging behavior.
    void UpdateCameraFromMouseUVE(const int framebufferHeight, const bool suppressOrbit) {
        // Same overlay guard as selection and the handle drag: pressing a toolbar bubble must not
        // also start orbiting the scene behind it.
        if (pointerOverOverlay_ || !ImGui::IsWindowHovered()) {
            return;
        }
        const ImGuiIO& io = ImGui::GetIO();
        if (!suppressOrbit && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0F)) {
            camera_.Orbit(io.MouseDelta.x, io.MouseDelta.y);
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0F) ||
                   ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0F)) {
            camera_.Pan(io.MouseDelta.x, io.MouseDelta.y, framebufferHeight);
        }
        if (io.MouseWheel != 0.0F) {
            camera_.Dolly(io.MouseWheel);
        }
    }

    // Unreal's Ctrl+digit (store) / digit (restore) viewport bookmarks, plus F-to-focus - the
    // live consumer of Marker3D in the real editor. Same hovered-panel routing
    // UpdateCameraFromMouseUVE() uses, and deliberately inert while a text field owns the
    // keyboard so typing a digit into an input never hijacks the camera. All bookmark state and
    // marker composition live in EditorUVE (tested there); this method only translates between
    // those plain orbit poses and this OrbitCamera - it owns no camera logic of its own.
    void UpdateViewportBookmarkHotkeysUVE() {
        const ImGuiIO& io = ImGui::GetIO();
        if (!ImGui::IsWindowHovered() || io.WantTextInput) {
            return;
        }
        static constexpr ImGuiKey kDigitKeysUVE[] = {
            ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5,
            ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9, ImGuiKey_0,
        };
        for (std::size_t order = 0U; order < 10U; ++order) {
            const std::size_t slot = (order + 1U) % 10U; // 1..9,0 like a real keyboard row
            if (!ImGui::IsKeyPressed(kDigitKeysUVE[order], false)) {
                continue;
            }
            if (io.KeyCtrl) {
                const univex::math::Vec3 target = camera_.Target();
                const bool stored = editor_.SetViewportBookmarkUVE(
                    slot, UVE::Editor::EditorViewportBookmarkUVE{
                               UVE::Math::Vector3UVE{target.x, target.y, target.z},
                               camera_.Yaw(), camera_.Pitch(), camera_.Distance()});
                // A live OrbitCamera only ever produces well-formed poses, so failure is
                // impossible here by construction; the bool is consumed, not ignored.
                (void)stored;

                continue;
            }
            const std::optional<UVE::Editor::EditorViewportBookmarkUVE> bookmark =
                editor_.GetViewportBookmarkUVE(slot);
            if (!bookmark.has_value()) {
                continue; // an empty slot restores nothing - Unreal does the same
            }
            camera_.CancelAnimation();
            camera_.SetTarget(univex::integration::FromUveVector3UVE(bookmark->target));
            camera_.SetYawPitch(bookmark->yawRadians, bookmark->pitchRadians);
            camera_.SetDistance(bookmark->distance);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeyCtrl) {
            const UVE::Scene::EntityUVE selected = editor_.GetSelectedEntityUVE();
            if (selected == UVE::Scene::kInvalidEntityUVE) {
                return;
            }
            // A selected Marker3D flies the camera INTO the marker's named viewpoint (the thing
            // Godot's inert Marker3D cannot do); anything else gets a plain re-target focus.
            if (const std::optional<UVE::Editor::EditorViewportBookmarkUVE> markerView =
                    editor_.ComposeMarker3DFocusBookmarkUVE(selected)) {
                camera_.CancelAnimation();
                camera_.SetTarget(univex::integration::FromUveVector3UVE(markerView->target));
                camera_.SetYawPitch(markerView->yawRadians, markerView->pitchRadians);
                camera_.SetDistance(markerView->distance);
            } else if (const std::optional<UVE::Math::Vector3UVE> focusTarget =
                           editor_.ResolveEntityFocusTargetUVE(selected)) {
                camera_.CancelAnimation();
                camera_.SetTarget(univex::integration::FromUveVector3UVE(*focusTarget));
            }
        }
    }

    // Returns true and fills the nav-local position if the cursor is over the orientation gizmo -
    // ports app/main.cpp's own CursorOverNavGizmo() (the standalone GLFW demo, where this already
    // works) into the ImGui-hosted real editor. `fbX`/`fbY` are pixel coordinates relative to the
    // Viewport panel's own rendered image top-left, matching NavViewportRectFor()'s own convention.
    [[nodiscard]] bool CursorOverNavGizmoUVE(int width, int height, float fbX, float fbY,
                                            float& outLocalX, float& outLocalY) const {
        if (!renderPass_.has_value() || !renderPass_->Settings().viewGizmos) {
            return false;
        }
        const univex::app::NavViewportRect rect =
            univex::app::ViewportRenderPass::NavViewportRectFor(renderPass_->Style(), width, height);
        if (rect.size <= 0) {
            return false;
        }
        // rect.y is a GL viewport origin (bottom-left); convert to top-left, matching fbY's own
        // top-left-origin convention (the same one IInputSystemUVE::GetMousePositionUVE() uses).
        const float top = static_cast<float>(height - rect.y - rect.size);
        const float left = static_cast<float>(rect.x);
        if (fbX < left || fbX > left + static_cast<float>(rect.size)) {
            return false;
        }
        if (fbY < top || fbY > top + static_cast<float>(rect.size)) {
            return false;
        }
        outLocalX = fbX - left;
        outLocalY = fbY - top;
        return true;
    }

    // Handles both nav-gizmo gestures - drag-anywhere-on-it orbits exactly like dragging the scene,
    // click-a-ball snaps the camera to look down that axis - mirroring app/main.cpp's own
    // OnMouseButton()/OnCursorPos() handling for the standalone demo, just driven by ImGui's per-
    // frame IO instead of GLFW press/release/move callbacks. Returns true while this gesture owns
    // the left mouse button, so UpdateCameraFromMouseUVE() knows to suppress its own generic orbit.
    [[nodiscard]] bool UpdateNavGizmoInteractionUVE(const int width, const int height) {
        if (!ImGui::IsWindowHovered() && !navDragging_) {
            return false;
        }
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
        const float fbX = io.MousePos.x - imageOrigin.x;
        const float fbY = io.MousePos.y - imageOrigin.y;

        if (!navDragging_) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                float localX = 0.0F;
                float localY = 0.0F;
                if (CursorOverNavGizmoUVE(width, height, fbX, fbY, localX, localY)) {
                    navDragging_ = true;
                    navDragMoved_ = false;
                    navPressX_ = localX;
                    navPressY_ = localY;
                    camera_.CancelAnimation();
                }
            }
            return false;
        }

        // A gesture that started on the gizmo: still deciding click vs. drag, or already
        // committed to orbiting. GetMouseDragDelta accumulates from the original press position,
        // matching the demo's own "press-relative slop" check exactly (not per-frame delta, which
        // would never exceed the threshold for a series of tiny frame-to-frame movements).
        if (!navDragMoved_) {
            const ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0F);
            if ((std::fabs(dragDelta.x) + std::fabs(dragDelta.y)) > kNavClickSlopPixelsUVE) {
                navDragMoved_ = true;
            }
        }
        if (navDragMoved_) {
            camera_.Orbit(io.MouseDelta.x, io.MouseDelta.y);
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (!navDragMoved_) {
                const univex::gizmo::NavPickResult pick = univex::gizmo::PickNavGizmo(
                    renderPass_->Style(), univex::app::ViewportRenderPass::NavViewMatrix(camera_),
                    navPressX_, navPressY_, static_cast<float>(renderPass_->Style().navPixelSize));
                if (pick.hit) {
                    camera_.SnapToDirection(pick.direction);
                    auto& settings = renderPass_->Settings();
                    settings.standardView = univex::viewport::StandardView::User;
                    if (settings.autoOrthogonal) {
                        camera_.SetOrthographic(true);
                    }
                }
            }
            navDragging_ = false;
            navDragMoved_ = false;
        }
        return true;
    }

    UVE::Editor::EditorUVE& editor_;
    UVE::Core::EngineCoreUVE& engine_;
    UVE::Scene::IEntityManagerUVE& entityManager_;
    univex::integration::EditorMeshLayerUVE meshLayer_;
    std::optional<univex::app::ViewportRenderPass> renderPass_;
    // Set each frame by ApplyOverlayStateUVE(), read by UpdateSelectionGizmoUVE() so it can force
    // the transform gizmo off while the Game workspace tab is active (see ApplyOverlayStateUVE's
    // own comment - it already forces the grid off directly, but the gizmo's visibility is decided
    // later in the same frame by selection state, so it needs this stored flag instead).
    bool gameWorkspaceActive_ = false;
    // See ViewportOverlayStateUVE::pointerOverOverlay - the toolbar floats inside this same
    // window, so without this a click on one of its buttons also lands in the scene behind it.
    bool pointerOverOverlay_ = false;
    univex::camera::OrbitCamera camera_;
    // Nav-gizmo click-vs-drag state - see UpdateNavGizmoInteractionUVE()'s own comment.
    bool navDragging_ = false;
    bool navDragMoved_ = false;
    float navPressX_ = 0.0F;
    float navPressY_ = 0.0F;
    // Click-vs-drag state for click-to-select - see UpdateSelectionFromMouseUVE().
    bool selectionPressActive_ = false;
    float selectionPressX_ = 0.0F;
    float selectionPressY_ = 0.0F;
    // Click-vs-drag state for the entity context toolbar - see UpdateEntityContextToolbarFromMouseUVE().
    bool contextToolbarPressActive_ = false;
    float contextToolbarPressX_ = 0.0F;
    float contextToolbarPressY_ = 0.0F;
    // Transform-gizmo drag state - see UpdateGizmoDragUVE(). dragPivot_ and dragPressValue_ are
    // frozen at the press: the object moves during the drag, and re-reading them each frame would
    // make the gesture chase its own result.
    std::optional<univex::math::Vec3> gizmoPivot_;
    univex::gizmo::GizmoHandleUVE dragHandle_ = univex::gizmo::GizmoHandleUVE::None;
    univex::gizmo::GizmoMode dragMode_ = univex::gizmo::GizmoMode::Move;
    univex::math::Vec3 dragPivot_{};
    DragReferenceUVE dragPressValue_{};
    bool glewInitialized_ = false;
    GLuint msaaFbo_ = 0U;
    GLuint msaaColorRb_ = 0U;
    GLuint msaaDepthRb_ = 0U;
    GLuint resolveFbo_ = 0U;
    GLuint resolveColorTexture_ = 0U;
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
    std::optional<univex::render::ShaderProgram> meshBlitProgram_;
    GLuint meshBlitVao_ = 0U;
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
