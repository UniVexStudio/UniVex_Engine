// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "viewport_studio_camera.h"

#include <array>
#include <utility>
#include <vector>

#include "univex/gizmo/bone_shape.h"
#include "univex/gizmo/gizmo_geometry.h"
#include "viewport_render_pass.h"

namespace UVE::App::Viewports {

ViewportStudioCameraUVE::ViewportStudioCameraUVE() {
    m_camera.Settings().allowFullOrbit = true;
}

void ViewportStudioCameraUVE::ApplyStudioFramingUVE(const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
    m_camera.SetOrthographic(false);
    if (overlayState.studioFramingSerial != m_appliedStudioFramingSerial) {
        m_appliedStudioFramingSerial = overlayState.studioFramingSerial;
        // Front-facing (+Z) and slightly elevated to present characters and effects clearly.
        constexpr float kFrontYaw = 1.5707963F;
        constexpr float kStudioPitch = 0.10F;
        m_camera.CancelAnimation();
        m_camera.Focus(univex::math::Vec3{overlayState.studioTarget[0], overlayState.studioTarget[1],
                                          overlayState.studioTarget[2]},
                       overlayState.studioRadius);
        m_camera.SetYawPitch(kFrontYaw, kStudioPitch);
    }
}

void ViewportStudioCameraUVE::HandleWheelDollyUVE(const Editor::EditorViewportInputUVE& input) {
    if (input.hovered && input.wheel != 0.0F) {
        m_camera.Dolly(input.wheel);
    }
    m_camera.Update(input.deltaTime);
}

void ViewportStudioCameraUVE::UploadBoneOverlaysUVE(univex::app::ViewportRenderPass& renderPass,
                                                    const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
    const auto toVec3 = [](const std::array<float, 3>& value) {
        return univex::math::Vec3{value[0], value[1], value[2]};
    };
    std::vector<univex::gizmo::BoneOverlayUVE> bones;
    bones.reserve(overlayState.bones.size());
    for (const auto& bone : overlayState.bones) {
        bones.push_back(univex::gizmo::BoneOverlayUVE{toVec3(bone.head), toVec3(bone.tail), toVec3(bone.side),
                                                      bone.hasLink, toVec3(bone.linkFrom), bone.skeletonSelected,
                                                      bone.boneSelected, bone.hasColour, toVec3(bone.colour),
                                                      bone.jointOnly, bone.helper});
    }
    renderPass.SetBonesUVE(std::move(bones));
}

void ViewportStudioCameraUVE::UploadColliderLinesUVE(univex::app::ViewportRenderPass& renderPass,
                                                     const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
    constexpr float kColliderWireRedUVE = 0.20F;
    constexpr float kColliderWireGreenUVE = 0.95F;
    constexpr float kColliderWireBlueUVE = 0.45F;
    constexpr float kColliderWireWidthPixelsUVE = 1.5F;
    constexpr float kColliderHighlightWidthPixelsUVE = 2.5F;
    const univex::math::Vec3 wireColour{kColliderWireRedUVE, kColliderWireGreenUVE, kColliderWireBlueUVE};
    const univex::math::Vec3 highlightColour{0.70F, 1.00F, 0.82F};
    const auto toVec3 = [](const std::array<float, 3>& value) {
        return univex::math::Vec3{value[0], value[1], value[2]};
    };
    std::vector<univex::gizmo::GizmoLine> lines;
    lines.reserve(overlayState.colliderLines.size());
    for (const auto& line : overlayState.colliderLines) {
        lines.push_back(univex::gizmo::GizmoLine{toVec3(line.from), toVec3(line.to),
                                                 line.highlighted ? highlightColour : wireColour,
                                                 line.highlighted ? kColliderHighlightWidthPixelsUVE
                                                                  : kColliderWireWidthPixelsUVE});
    }
    renderPass.SetColliderWireframesUVE(std::move(lines));
}

void ViewportStudioCameraUVE::ExportCameraForHostUVE(const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState,
                                                     const int width,
                                                     const int height) const {
    auto& cam = overlayState.viewCamera;
    const univex::math::Vec3 eye = m_camera.Eye();
    const univex::math::Vec3 forward = univex::math::Normalize(m_camera.Target() - eye);
    const univex::math::Vec3 right = univex::math::Normalize(univex::math::Cross(forward, m_camera.Up()));
    const univex::math::Vec3 up = univex::math::Normalize(univex::math::Cross(right, forward));
    cam.valid = true;
    cam.eye = {eye.x, eye.y, eye.z};
    cam.forward = {forward.x, forward.y, forward.z};
    cam.right = {right.x, right.y, right.z};
    cam.up = {up.x, up.y, up.z};
    cam.fovYRadians = m_camera.Settings().fovYRadians;
    cam.nearPlane = m_camera.NearPlane();
    cam.orthographic = m_camera.IsOrthographic();
    cam.orthographicHalfHeight = m_camera.OrthographicHalfHeight();
    cam.width = static_cast<float>(width);
    cam.height = static_cast<float>(height);

    overlayState.gizmoPivotValid = false;
    overlayState.gizmoLines.clear();
    overlayState.gizmoTriangles.clear();
    overlayState.selectionMask.clear();
    overlayState.selectionMaskWidth = overlayState.selectionMaskHeight = 0;
    overlayState.sceneDepth.clear();
    overlayState.sceneDepthWidth = overlayState.sceneDepthHeight = 0;
    overlayState.hostGrid.visible = false;
}

} // namespace UVE::App::Viewports
