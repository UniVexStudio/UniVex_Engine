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

void EditorUVE::DrawPluginWindowUVE() {
    if (!m_pluginWindowVisible) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2{340.0F, 0.0F}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2{ImGui::GetMainViewport()->WorkPos.x + 260.0F,
                                   ImGui::GetMainViewport()->WorkPos.y + 104.0F},
                            ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Plugin Tools", &m_pluginWindowVisible, ImGuiWindowFlags_AlwaysAutoResize)) {
        DrawNativeIconLabelUVE(m_uiAssets.GetGeneralIconTextureIdUVE("plugin"), "Editor tools");
        ImGui::Separator();
        ImGui::TextDisabled("No editor plugins are currently installed.");
    }
    ImGui::End();
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

void EditorUVE::DrawCaptureBodyUVE() {
    // The viewer owns plain-int combo state; the filter struct is rebuilt from it each
    // frame so the ring's matching logic stays the single source of truth.
    static constexpr const char* kDirections[] = {"Both", "Out", "In"};
    static constexpr const char* kChannels[] = {"Any", "Wire", "Replication", "RPC", "Prediction", "Shot"};
    m_captureFilter.text = std::string(m_captureText.data());
    m_captureFilter.direction =
        static_cast<Network::PacketCaptureDirectionUVE>(m_captureDirection);
    if (m_captureChannel == 0) {
        m_captureFilter.channel.reset();
    } else {
        m_captureFilter.channel =
            static_cast<Network::InspectedChannelUVE>(m_captureChannel - 1);
    }
    m_captureFilter.malformedOnly = m_captureMalformedOnly;

    if (ImGui::Button("Clear")) {
        ClearCapturedPacketsUVE();
        m_captureSelected = kNoCaptureSelectionUVE;
    }
    ImGui::SameLine();
    const std::string stats = std::to_string(m_packetCapture.SizeUVE()) + "/" +
                              std::to_string(m_packetCapture.CapacityUVE()) + " packets (" +
                              std::to_string(m_packetCapture.DropsUVE()) + " dropped, " +
                              std::to_string(m_packetCapture.MalformedUVE()) + " malformed)";
    ImGui::TextDisabled("%s", stats.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0F);
    if (ImGui::BeginCombo("##uve_capture_dir", kDirections[m_captureDirection])) {
        for (int i = 0; i < 3; ++i) {
            const bool selected = m_captureDirection == i;
            if (ImGui::Selectable(kDirections[i], selected)) {
                m_captureDirection = i;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0F);
    if (ImGui::BeginCombo("##uve_capture_ch", kChannels[m_captureChannel])) {
        for (int i = 0; i < 6; ++i) {
            const bool selected = m_captureChannel == i;
            if (ImGui::Selectable(kChannels[i], selected)) {
                m_captureChannel = i;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Malformed", &m_captureMalformedOnly);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputText("##uve_capture_text", m_captureText.data(), m_captureText.size());

    const std::vector<std::size_t> rows = m_packetCapture.FilteredUVE(m_captureFilter);
    if (m_captureSelected != kNoCaptureSelectionUVE &&
        m_packetCapture.TryAtUVE(m_captureSelected) == nullptr) {
        m_captureSelected = kNoCaptureSelectionUVE;
    }
    if (ImGui::BeginChild("##uve_capture_list", ImVec2(0.0F, 220.0F), true)) {
        if (ImGui::BeginTable("##uve_capture_table", 1,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            for (const std::size_t index : rows) {
                const Network::InspectedPacketUVE* const packet =
                    m_packetCapture.TryAtUVE(index);
                if (packet == nullptr) {
                    continue;
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const std::string line = Network::DescribePacketUVE(*packet);
                const bool selected = m_captureSelected == index;
                if (packet->malformed) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.45F, 0.45F, 1.0F));
                }
                if (ImGui::Selectable(line.c_str(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    m_captureSelected = index;
                }
                if (packet->malformed) {
                    ImGui::PopStyleColor();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    if (ImGui::BeginChild("##uve_capture_detail", ImVec2(0.0F, 0.0F), true)) {
        const Network::InspectedPacketUVE* const packet =
            m_captureSelected == kNoCaptureSelectionUVE
                ? nullptr
                : m_packetCapture.TryAtUVE(m_captureSelected);
        if (packet == nullptr) {
            ImGui::TextDisabled("Select a packet.");
        } else {
            ImGui::TextUnformatted(Network::DescribePacketUVE(*packet).c_str());
            const std::string meta = "t=" + std::to_string(packet->timestampMs) + " ms, " +
                                     std::to_string(packet->raw.size()) + " bytes" +
                                     (packet->malformed ? ", MALFORMED" : "");
            ImGui::TextDisabled("%s", meta.c_str());
            if (ImGui::BeginChild("##uve_capture_hex", ImVec2(0.0F, 0.0F), false,
                                  ImGuiWindowFlags_HorizontalScrollbar)) {
                ImGui::TextUnformatted(Network::HexdumpPacketUVE(packet->raw).c_str());
            }
            ImGui::EndChild();
        }
    }
    ImGui::EndChild();

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
