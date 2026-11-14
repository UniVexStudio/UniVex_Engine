// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// What is left of the previous menu bar and bottom dock once the PRISM level editor replaced them:
// the preview sun / sky to scene actions, the plugin tools window, the packet capture section, and
// the profiler capture sessions the PRISM Profiler page drives. The menus, the dock tab bar, the
// Output / Console / Profiler sections and the panel grips are the PRISM shell's now.

#include "editor.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <GLFW/glfw3.h>

#include "editor_chrome_layout.h"
#include "editor_color_field.h"
#include "editor_fonts.h"
#include "editor_object_icons.h"

#include "light_emitter_component.h"
#include "transform_component.h"
#include "profiler_view.h"
#include "editor_render_stats.h"
#include "i_entity_manager.h"
#include "color.h"
#include "quaternion.h"
#include "preview_lighting.h"
#include "world_environment_3d.h"
#include "i_scene_graph.h"
#include "scene_object_registry.h"

namespace UVE::Editor {

Scene::EntityUVE EditorUVE::AddPreviewSunToSceneUVE() {
    // The scene's sun, made from (or updated to) the preview sun: direction, colour, energy, and
    // real-time shadows on.
    if (!IsAuthoringCommandAllowedUVE()) {
        return Scene::kInvalidEntityUVE;
    }
    Scene::EntityUVE sunEntity = FindTopLevelObjectUVE(Scene::Objects::SceneObjectKindUVE::DirectionalLight3D);
    if (sunEntity == Scene::kInvalidEntityUVE) {
        sunEntity = CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::DirectionalLight3D);
    }
    if (sunEntity == Scene::kInvalidEntityUVE) {
        return sunEntity;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const std::optional<Math::QuaternionUVE> rotation = Scene::TryMakePreviewSunRotationUVE(
        m_viewportOverlayState.previewSunAzimuth, m_viewportOverlayState.previewSunAltitude);
    if (rotation.has_value() && entityManager.HasComponentUVE<Scene::TransformComponentUVE>(sunEntity)) {
        auto transform = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(sunEntity);
        transform.localRotation = *rotation;
        m_services->GetSceneGraphUVE().SetLocalTransformUVE(entityManager, sunEntity, transform);
    }
    if (entityManager.HasComponentUVE<Scene::LightEmitterComponentUVE>(sunEntity)) {
        auto& emitter = entityManager.GetComponentUVE<Scene::LightEmitterComponentUVE>(sunEntity);
        emitter.color = Math::ColorFromDisplayUVE(Math::Vector3UVE{m_viewportOverlayState.previewSunColor[0],
                                                                    m_viewportOverlayState.previewSunColor[1],
                                                                    m_viewportOverlayState.previewSunColor[2]});
        emitter.energy = m_viewportOverlayState.previewSunEnergy;
        emitter.shadowEnabled = true;
    }
    m_sceneDirty = true;
    SelectEntityUVE(sunEntity);
    m_contentStatusMessage = "Added DirectionalLight3D from Preview Sun (with real-time shadows).";
    return sunEntity;
}

Scene::EntityUVE EditorUVE::AddPreviewEnvironmentToSceneUVE() {
    // The scene's WorldEnvironment3D, made from (or updated to) the preview sky and ambient.
    if (!IsAuthoringCommandAllowedUVE()) {
        return Scene::kInvalidEntityUVE;
    }
    Scene::EntityUVE envEntity = FindTopLevelObjectUVE(Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D);
    if (envEntity == Scene::kInvalidEntityUVE) {
        envEntity = CreateDocumentSceneObjectUVE(Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D);
    }
    if (envEntity == Scene::kInvalidEntityUVE) {
        return envEntity;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (entityManager.HasComponentUVE<Scene::WorldEnvironment3DComponentUVE>(envEntity)) {
        auto& env = entityManager.GetComponentUVE<Scene::WorldEnvironment3DComponentUVE>(envEntity);
        env.ambientSource = Scene::WorldEnvironmentAmbientSourceUVE::Sky;
        env.ambientColor = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
        env.skyColor = Math::Vector3UVE{m_viewportOverlayState.previewSkyColor[0], m_viewportOverlayState.previewSkyColor[1],
                                        m_viewportOverlayState.previewSkyColor[2]};
        env.horizonColor = Math::Vector3UVE{m_viewportOverlayState.previewHorizonColor[0],
                                            m_viewportOverlayState.previewHorizonColor[1],
                                            m_viewportOverlayState.previewHorizonColor[2]};
        env.groundColor = Math::Vector3UVE{m_viewportOverlayState.previewGroundColor[0],
                                           m_viewportOverlayState.previewGroundColor[1],
                                           m_viewportOverlayState.previewGroundColor[2]};
        env.ambientEnergy = m_viewportOverlayState.previewAmbientEnergy;
        env.postProcessingEnabled = false;
    }
    m_sceneDirty = true;
    SelectEntityUVE(envEntity);
    m_contentStatusMessage = "Added WorldEnvironment3D from Preview Environment.";
    return envEntity;
}


void EditorUVE::CapturePacketUVE(const std::vector<std::uint8_t>& datagram,
                                 const Network::PacketDirectionUVE direction,
                                 const std::uint64_t timestampMs,
                                 const Network::InspectedChannelUVE channel) {
    m_packetCapture.PushUVE(datagram, direction, timestampMs, channel);
}

void EditorUVE::ClearCapturedPacketsUVE() {
    m_packetCapture.ClearUVE();
}

const Network::PacketCaptureUVE& EditorUVE::CapturedPacketsUVE() const noexcept {
    return m_packetCapture;
}

Core::ProfilerCaptureUVE& EditorUVE::ProfilerUVE() noexcept {
    return m_profilerCapture;
}

const Core::DiagnosticCaptureBundleUVE& EditorUVE::ProfilerBundleUVE() const noexcept {
    return m_profilerBundle;
}

static std::uint64_t ProfilerNowNsUVE() {
    const std::chrono::nanoseconds now =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
    return static_cast<std::uint64_t>(now.count());
}

void EditorUVE::BeginProfilerCaptureUVE() {
    if (m_profilerCapture.IsActiveUVE()) {
        return;
    }
    ++m_profilerSession;
    const std::string session = "editor-" + std::to_string(m_profilerSession);
    const Core::DiagnosticCaptureResultUVE begun = m_profilerCapture.BeginUVE(session, ProfilerNowNsUVE());
    m_profilerStatus = begun.IsAcceptedUVE() ? "Capturing " + session + "." : begun.message;
}

void EditorUVE::EndProfilerCaptureUVE() {
    if (!m_profilerCapture.IsActiveUVE()) {
        return;
    }
    Core::DiagnosticCaptureStopResultUVE ended = m_profilerCapture.EndUVE(ProfilerNowNsUVE());
    if (ended.IsAcceptedUVE()) {
        m_profilerBundle = std::move(ended.bundle);
        m_profilerHasBundle = true;
        m_profilerStatus = "Ended " + m_profilerBundle.sessionId + ".";
    } else {
        m_profilerStatus = ended.message;
    }
}

} // namespace UVE::Editor
