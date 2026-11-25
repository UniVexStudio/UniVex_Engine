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

#include <GLFW/glfw3.h>

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

Scene::Entity Editor::AddPreviewSunToScene() {
    // The scene's sun, made from (or updated to) the preview sun: direction, colour, energy, and
    // real-time shadows on.
    if (!IsAuthoringCommandAllowed()) {
        return Scene::kInvalidEntity;
    }
    Scene::Entity sunEntity = FindTopLevelObject(Scene::Objects::SceneObjectKind::DirectionalLight3D);
    if (sunEntity == Scene::kInvalidEntity) {
        sunEntity = CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::DirectionalLight3D);
    }
    if (sunEntity == Scene::kInvalidEntity) {
        return sunEntity;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    const std::optional<Math::Quaternion> rotation = Scene::TryMakePreviewSunRotation(
        m_viewportOverlayState.previewSunAzimuth, m_viewportOverlayState.previewSunAltitude);
    if (rotation.has_value() && entityManager.HasComponent<Scene::TransformComponent>(sunEntity)) {
        auto transform = entityManager.GetComponent<Scene::TransformComponent>(sunEntity);
        transform.localRotation = *rotation;
        m_services->GetSceneGraph().SetLocalTransform(entityManager, sunEntity, transform);
    }
    if (entityManager.HasComponent<Scene::LightEmitterComponent>(sunEntity)) {
        auto& emitter = entityManager.GetComponent<Scene::LightEmitterComponent>(sunEntity);
        emitter.color = Math::ColorFromDisplay(Math::Vector3{m_viewportOverlayState.previewSunColor[0],
                                                                    m_viewportOverlayState.previewSunColor[1],
                                                                    m_viewportOverlayState.previewSunColor[2]});
        emitter.energy = m_viewportOverlayState.previewSunEnergy;
        emitter.shadowEnabled = true;
    }
    m_sceneDirty = true;
    SelectEntity(sunEntity);
    m_contentStatusMessage = "Added DirectionalLight3D from Preview Sun (with real-time shadows).";
    return sunEntity;
}

Scene::Entity Editor::AddPreviewEnvironmentToScene() {
    // The scene's WorldEnvironment3D, made from (or updated to) the preview sky and ambient.
    if (!IsAuthoringCommandAllowed()) {
        return Scene::kInvalidEntity;
    }
    Scene::Entity envEntity = FindTopLevelObject(Scene::Objects::SceneObjectKind::WorldEnvironment3D);
    if (envEntity == Scene::kInvalidEntity) {
        envEntity = CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::WorldEnvironment3D);
    }
    if (envEntity == Scene::kInvalidEntity) {
        return envEntity;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (entityManager.HasComponent<Scene::WorldEnvironment3DComponent>(envEntity)) {
        auto& env = entityManager.GetComponent<Scene::WorldEnvironment3DComponent>(envEntity);
        env.ambientSource = Scene::WorldEnvironmentAmbientSource::Sky;
        env.ambientColor = Math::Vector3{1.0F, 1.0F, 1.0F};
        env.skyColor = Math::Vector3{m_viewportOverlayState.previewSkyColor[0], m_viewportOverlayState.previewSkyColor[1],
                                        m_viewportOverlayState.previewSkyColor[2]};
        env.horizonColor = Math::Vector3{m_viewportOverlayState.previewHorizonColor[0],
                                            m_viewportOverlayState.previewHorizonColor[1],
                                            m_viewportOverlayState.previewHorizonColor[2]};
        env.groundColor = Math::Vector3{m_viewportOverlayState.previewGroundColor[0],
                                           m_viewportOverlayState.previewGroundColor[1],
                                           m_viewportOverlayState.previewGroundColor[2]};
        env.ambientEnergy = m_viewportOverlayState.previewAmbientEnergy;
        env.postProcessingEnabled = true;
        env.bloomEnabled = true;
        env.bloomThreshold = 1.15F;
        env.bloomIntensity = 0.25F;
        env.bloomSoftKnee = 0.5F;
        env.bloomMipCount = 4U;
        env.ssaoEnabled = true;
        env.ssaoRadius = 0.5F;
        env.ssaoIntensity = 0.85F;
        env.fogEnabled = false;
    }
    m_sceneDirty = true;
    SelectEntity(envEntity);
    m_contentStatusMessage = "Added WorldEnvironment3D from Preview Environment.";
    return envEntity;
}

void Editor::CapturePacket(const std::vector<std::uint8_t>& datagram,
                                 const Network::PacketDirection direction,
                                 const std::uint64_t timestampMs,
                                 const Network::InspectedChannel channel) {
    m_packetCapture.Push(datagram, direction, timestampMs, channel);
}

void Editor::ClearCapturedPackets() {
    m_packetCapture.Clear();
}

const Network::PacketCapture& Editor::CapturedPackets() const noexcept {
    return m_packetCapture;
}

Core::ProfilerCapture& Editor::Profiler() noexcept {
    return m_profilerCapture;
}

const Core::DiagnosticCaptureBundle& Editor::ProfilerBundle() const noexcept {
    return m_profilerBundle;
}

static std::uint64_t ProfilerNowNs() {
    const std::chrono::nanoseconds now =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
    return static_cast<std::uint64_t>(now.count());
}

void Editor::BeginProfilerCapture() {
    if (m_profilerCapture.IsActive()) {
        return;
    }
    ++m_profilerSession;
    const std::string session = "editor-" + std::to_string(m_profilerSession);
    const Core::DiagnosticCaptureResult begun = m_profilerCapture.Begin(session, ProfilerNowNs());
    m_profilerStatus = begun.IsAccepted() ? "Capturing " + session + "." : begun.message;
}

void Editor::EndProfilerCapture() {
    if (!m_profilerCapture.IsActive()) {
        return;
    }
    Core::DiagnosticCaptureStopResult ended = m_profilerCapture.End(ProfilerNowNs());
    if (ended.IsAccepted()) {
        m_profilerBundle = std::move(ended.bundle);
        m_profilerHasBundle = true;
        m_profilerStatus = "Ended " + m_profilerBundle.sessionId + ".";
    } else {
        m_profilerStatus = ended.message;
    }
}

} // namespace UVE::Editor
