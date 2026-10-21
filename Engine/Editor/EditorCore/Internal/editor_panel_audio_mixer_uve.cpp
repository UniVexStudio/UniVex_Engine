// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

/// The audio authoring dock: the mixer tree with live meters on the left, the selected source's
/// attenuation curve on the right.
///
/// Everything this file decides - what a row says, how a level reads, where the curve's points are -
/// lives in editor_audio_mixer_uve.h and is unit tested. This file only draws it, which is also why
/// it takes no locks of its own: the audio system's own accessors are the contract.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

#include <imgui.h>

#include "editor_chrome_layout_uve.h"
#include "uve/audio/i_audio_system_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/editor/editor_audio_mixer_uve.h"
#include "uve/editor/editor_uve.h"
#include "uve/scene/scene_component_metadata_uve.h"

namespace UVE::Editor {

namespace {

/// Width of the attenuation editor column. Fixed so the mixer, which has one row per bus and a
/// slider in each, keeps the space it needs when the dock is narrow.
constexpr float kAudioSourceColumnWidthUVE = 340.0F;
constexpr float kAudioMeterBarHeightUVE = 6.0F;
constexpr float kAudioMixerIndentPixelsUVE = 14.0F;

/// Mute/solo buttons are one character wide by design: two more columns of text would push the
/// volume slider below the width where it can be dragged with any precision.
[[nodiscard]] bool DrawAudioMixerToggleUVE(const char* const label, const bool active, const ImVec4& activeColor,
                                          const char* const tooltip) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.06F, 0.06F, 0.06F, 1.0F});
    }
    const bool pressed = ImGui::Button(label, ImVec2{ImGui::GetFrameHeight(), 0.0F});
    if (active) {
        ImGui::PopStyleColor(2);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return pressed;
}

/// One bus's meter: a filled bar plus the reading, coloured when the bus is clipping. Drawn rather
/// than a widget because ImGui has no meter, and a progress-bar substitute cannot show the
/// difference between "quiet" and "silent" at the low end of a decibel scale.
void DrawAudioMeterUVE(const EditorAudioMixerRowUVE& row) {
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float textHeight = ImGui::GetTextLineHeight();
    const ImU32 track = ImGui::GetColorU32(ImGuiCol_FrameBg);
    const ImU32 fill = ImGui::GetColorU32(row.isClipping ? ImVec4{0.92F, 0.30F, 0.26F, 1.0F}
                                                        : row.isSilencedWithSources
                                                              ? ImVec4{0.55F, 0.55F, 0.58F, 1.0F}
                                                              : ImVec4{0.35F, 0.78F, 0.45F, 1.0F});
    const float barTop = origin.y + textHeight + 1.0F;
    drawList->AddRectFilled(ImVec2{origin.x, barTop}, ImVec2{origin.x + width, barTop + kAudioMeterBarHeightUVE},
                            track, 2.0F);
    const float filled = std::clamp(row.levelNormalized, 0.0F, 1.0F) * width;
    if (filled > 0.5F) {
        drawList->AddRectFilled(ImVec2{origin.x, barTop}, ImVec2{origin.x + filled, barTop + kAudioMeterBarHeightUVE},
                                fill, 2.0F);
    }
    ImGui::TextUnformatted(row.levelText.c_str());
    // Reserve the bar's own height so the next row starts below it rather than on top of it.
    ImGui::Dummy(ImVec2{width, kAudioMeterBarHeightUVE});
}

/// Vertical markers on the last drawn plot, at the source's authored min and max distances. Drawn
/// over the plot rather than as separate widgets because the interesting reading is where the curve
/// bends, not where a number is.
void DrawAudioCurveMarkersUVE(const EditorAudioAttenuationMarkersUVE& markers, const char* const minLabel,
                              const char* const maxLabel) {
    const ImVec2 plotMin = ImGui::GetItemRectMin();
    const ImVec2 plotMax = ImGui::GetItemRectMax();
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const ImU32 minColor = ImGui::GetColorU32(ImVec4{0.45F, 0.72F, 0.95F, 1.0F});
    const ImU32 maxColor = ImGui::GetColorU32(ImVec4{0.95F, 0.72F, 0.35F, 1.0F});
    const float width = plotMax.x - plotMin.x;
    const auto drawMarker = [&](const float fraction, const ImU32 color, const char* const label) {
        const float x = plotMin.x + std::clamp(fraction, 0.0F, 1.0F) * width;
        drawList->AddLine(ImVec2{x, plotMin.y}, ImVec2{x, plotMax.y}, color, 1.0F);
        drawList->AddText(ImVec2{x + 2.0F, plotMin.y + 1.0F}, color, label);
    };
    drawMarker(markers.minNormalized, minColor, minLabel);
    drawMarker(markers.maxNormalized, maxColor, maxLabel);
}

} // namespace

void EditorUVE::DrawAudioMixerDockUVE() {
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    const EditorChromeLayoutUVE layout = ComputeEditorChromeLayoutUVE(*mainViewport, m_bottomDockVisible,
                                                                      m_bottomDockHeight, m_scenePanelWidth,
                                                                      m_inspectorPanelWidth);
    ImGui::SetNextWindowPos(layout.contentBrowserPos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(layout.contentBrowserSize, ImGuiCond_FirstUseEver);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin(kPanelLabelAudioMixerUVE, nullptr, flags)) {
        ImGui::End();
        return;
    }

    Audio::IAudioSystemUVE& audioSystem = m_services->GetAudioSystemUVE();
    const Audio::AudioMixerDiagnosticsUVE diagnostics = audioSystem.GetMixerDiagnosticsUVE();
    const EditorAudioMixerSnapshotUVE snapshot = BuildEditorAudioMixerSnapshotUVE(
        diagnostics, [&audioSystem](const std::string_view name) { return audioSystem.GetMixerGroupMeterUVE(name); });

    // Header: the one line that answers "why is this silent" before a single bus is read.
    if (snapshot.anySoloed) {
        ImGui::TextColored(ImVec4{0.95F, 0.72F, 0.35F, 1.0F}, "Solo active - buses outside every soloed chain are silent.");
    } else {
        ImGui::TextDisabled("%zu sources across %zu buses", snapshot.routedSourceCount, snapshot.rows.size());
    }
    if (snapshot.truncated) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4{0.95F, 0.45F, 0.40F, 1.0F}, "Bus table is full: some buses are not shown.");
    }
    ImGui::Separator();

    // Half the dock at most, so a narrow layout gives both columns room instead of collapsing the
    // one on the left. A child width of 0 means "fill", which is why the floor matters: a negative
    // remainder would otherwise be read as an inset from the right edge.
    const float sourceColumnWidth =
        std::min(kAudioSourceColumnWidthUVE, ImGui::GetContentRegionAvail().x * 0.5F);
    const float busColumnWidth = std::max(160.0F, ImGui::GetContentRegionAvail().x - sourceColumnWidth);
    ImGui::BeginChild("##audio-mixer-buses", ImVec2{busColumnWidth, 0.0F}, false);
    DrawAudioMixerBusTableUVE(audioSystem, snapshot);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##audio-mixer-source", ImVec2{0.0F, 0.0F}, false);
    DrawAudioAttenuationEditorUVE();
    ImGui::EndChild();
    ImGui::End();
}

void EditorUVE::DrawAudioMixerBusTableUVE(Audio::IAudioSystemUVE& audioSystem,
                                          const EditorAudioMixerSnapshotUVE& snapshot) {
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                           ImGuiTableFlags_NoBordersInBody;
    if (!ImGui::BeginTable("##audio-buses", 5, tableFlags)) {
        return;
    }
    ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_WidthStretch, 0.30F);
    ImGui::TableSetupColumn("M", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
    ImGui::TableSetupColumn("S", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
    ImGui::TableSetupColumn("Volume", ImGuiTableColumnFlags_WidthStretch, 0.34F);
    ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthStretch, 0.36F);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    const ImVec4 muteColor{0.92F, 0.55F, 0.30F, 1.0F};
    const ImVec4 soloColor{0.95F, 0.82F, 0.32F, 1.0F};
    for (const EditorAudioMixerRowUVE& row : snapshot.rows) {
        ImGui::TableNextRow();
        ImGui::PushID(row.name.c_str());

        ImGui::TableSetColumnIndex(0);
        const float indent = static_cast<float>(row.depth) * kAudioMixerIndentPixelsUVE;
        if (indent > 0.0F) {
            ImGui::Dummy(ImVec2{indent, 0.0F});
            ImGui::SameLine(0.0F, 0.0F);
        }
        // A bus that carries sources but produces silence is the one row worth colouring: it is
        // exactly the case a level meter alone cannot explain.
        if (row.isSilencedWithSources) {
            ImGui::TextColored(ImVec4{0.95F, 0.72F, 0.35F, 1.0F}, "%s", row.name.c_str());
        } else {
            ImGui::TextUnformatted(row.name.c_str());
        }
        if (!row.isVolumePersistent && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Volume is session-only: this bus has no project setting.");
        }

        ImGui::TableSetColumnIndex(1);
        if (DrawAudioMixerToggleUVE("M", row.muted, muteColor, "Mute this bus and everything under it.")) {
            static_cast<void>(audioSystem.SetMixerGroupMutedUVE(row.name, !row.muted));
        }

        ImGui::TableSetColumnIndex(2);
        if (DrawAudioMixerToggleUVE("S", row.soloed, soloColor,
                                    "Solo: while anything is soloed, only its chain stays audible.")) {
            static_cast<void>(audioSystem.SetMixerGroupSoloedUVE(row.name, !row.soloed));
        }

        ImGui::TableSetColumnIndex(3);
        float volume = row.volume;
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::SliderFloat("##volume", &volume, 0.0F, 1.0F, "%.2f")) {
            // Master/Music/SFX/Voice are project settings, and the engine already applies a settings
            // change to the live mixer - so a persisted bus is written once, through the one path
            // that keeps the running mix and the saved project in agreement.
            const std::string_view settingId = EditorAudioBusVolumeSettingIdUVE(row.name);
            if (!settingId.empty()) {
                static_cast<void>(m_services->GetProjectSettingsUVE().SetValueUVE(
                    settingId, static_cast<double>(std::clamp(volume, 0.0F, 1.0F))));
            } else {
                static_cast<void>(audioSystem.SetMixerGroupVolumeUVE(row.name, volume));
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip(row.isVolumePersistent ? "Saved to the project's audio settings."
                                                     : "Session-only: this bus has no project setting.");
        }

        ImGui::TableSetColumnIndex(4);
        DrawAudioMeterUVE(row);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void EditorUVE::DrawAudioAttenuationEditorUVE() {
    ImGui::TextUnformatted("Selected source");
    ImGui::Separator();

    const Scene::EntityUVE entity = GetSelectedEntityUVE();
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (entity == Scene::kInvalidEntityUVE || !entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<Scene::AudioSourceComponentUVE>(entity)) {
        ImGui::TextDisabled("Select an entity with an Audio Source to edit its falloff.");
        return;
    }

    const Scene::AudioSourceComponentUVE& source = entityManager.GetComponentUVE<Scene::AudioSourceComponentUVE>(entity);
    if (source.audioAssetPath.empty()) {
        ImGui::TextDisabled("This source has no clip assigned.");
    } else {
        ImGui::TextWrapped("%s", source.audioAssetPath.c_str());
    }

    const Core::TypeMetadataEntryUVE* const entry =
        Scene::FindSceneComponentMetadataUVE(std::type_index(typeid(Scene::AudioSourceComponentUVE)));
    if (entry == nullptr) {
        ImGui::TextDisabled("Audio Source metadata is unavailable.");
        return;
    }
    const auto findProperty = [entry](const char* const name) -> const Core::TypeMetadataPropertyUVE* {
        for (const Core::TypeMetadataPropertyUVE& property : entry->properties) {
            if (property.name == name) {
                return &property;
            }
        }
        return nullptr;
    };
    // Every field below is written through the same metadata-driven edit the inspector uses, so a
    // curve edit is one undo step and obeys the component's own validation rather than a second
    // write path that could disagree with it.
    const auto editFloat = [&](const char* const label, const char* const propertyName, const float step,
                               const float minimum, const float maximum) {
        const Core::TypeMetadataPropertyUVE* const property = findProperty(propertyName);
        if (property == nullptr) {
            return;
        }
        void* const instance = entityManager.GetComponentPointerUVE(entity, entry->typeIndex);
        if (instance == nullptr) {
            return;
        }
        float value = 0.0F;
        property->getValue(instance, &value);
        ImGui::SetNextItemWidth(-120.0F);
        const bool changed = ImGui::DragFloat(label, &value, step, minimum, maximum, "%.2f");
        // Called every frame, not only on change: this is also what ends a drag and records the
        // whole gesture as one undo step.
        static_cast<void>(ApplyContinuousPropertyEditUVE(*entry, *property, changed, &value));
    };
    const auto editBool = [&](const char* const label, const char* const propertyName) {
        const Core::TypeMetadataPropertyUVE* const property = findProperty(propertyName);
        if (property == nullptr) {
            return;
        }
        void* const instance = entityManager.GetComponentPointerUVE(entity, entry->typeIndex);
        if (instance == nullptr) {
            return;
        }
        bool value = false;
        property->getValue(instance, &value);
        if (ImGui::Checkbox(label, &value)) {
            static_cast<void>(SetSelectedComponentPropertyUVE(*entry, *property, &value));
        }
    };

    // Editing is authoring: during play the curve is shown but the fields are read-only, so a
    // checkbox cannot appear to change a component the simulation owns.
    const bool canAuthor = IsAuthoringCommandAllowedUVE() && HasSingleDocumentSelectionUVE();
    ImGui::BeginDisabled(!canAuthor);
    editBool("Spatial", "spatial");
    editFloat("Volume", "volume", 0.01F, 0.0F, 1.0F);
    if (source.spatial) {
        editFloat("Min distance", "minDistance", 0.1F, 0.0F, 10000.0F);
        editFloat("Max distance", "maxDistance", 0.1F, 0.0F, 10000.0F);
        editBool("Occlusion", "occlusionEnabled");
        if (source.occlusionEnabled) {
            editFloat("Occluded gain", "occlusionGain", 0.01F, 0.0F, 1.0F);
            editFloat("Occluded low-pass", "occlusionLowPassHz", 10.0F, 20.0F, 20000.0F);
        }
    }
    ImGui::EndDisabled();
    if (!canAuthor) {
        ImGui::TextDisabled("Stop playback to edit these fields.");
    }

    ImGui::Separator();
    if (!source.spatial) {
        ImGui::TextDisabled("A non-spatial source is not attenuated: its gain is the volume above.");
        return;
    }

    // The curve is sampled through the engine's own attenuation function, so what is drawn is what
    // is heard - including the flat shoulder inside minDistance and the tail past maxDistance.
    const std::vector<float> decibels = BuildEditorAudioAttenuationDecibelCurveUVE(source);
    if (!decibels.empty()) {
        ImGui::PlotLines("##falloff", decibels.data(), static_cast<int>(decibels.size()), 0,
                         "Falloff (dB, full scale 0)", Audio::kAudioMeterFloorDbUVE, 0.0F, ImVec2{-1.0F, 90.0F});
        std::array<char, 32> minLabel{};
        std::array<char, 32> maxLabel{};
        static_cast<void>(std::snprintf(minLabel.data(), minLabel.size(), "min %.1fm",
                                        static_cast<double>(source.minDistance)));
        static_cast<void>(std::snprintf(maxLabel.data(), maxLabel.size(), "max %.1fm",
                                        static_cast<double>(source.maxDistance)));
        DrawAudioCurveMarkersUVE(ComputeEditorAudioAttenuationMarkersUVE(source), minLabel.data(), maxLabel.data());
        ImGui::TextDisabled("Range 0 - %.1f m", static_cast<double>(ComputeEditorAudioAttenuationExtentUVE(source)));
    }
}

} // namespace UVE::Editor
