// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Inspector panel and every built-in component drawer: name, hierarchy, transform, primitive
// mesh, world environment, character controller, canvas, UI text/image/button, scene components
// and prefabs - plus the Add-Component panel and the drawer registration that wires them up.
//
// Seventeen methods and a thousand lines, the largest single group in editor_uve.cpp and the one
// most often edited: adding a component type means adding a drawer, and that work had to be done
// inside a seven-thousand-line file shared with the viewport, the menus and the content browser.
// This is the group that most justifies the split.
//
// The methods remain EditorUVE members, declared where they always were. They read and write
// editor state directly - selection, the tool session, the drawer registry - and turning that
// into parameters to make them free functions would be a redesign of the editor's state
// ownership disguised as a file move.
//
// Moved verbatim. Not one line of the seventeen functions differs from what was in
// editor_uve.cpp; anything here worth improving was already here and belongs in its own commit
// where it reads as a change rather than hiding inside a move.

#include "uve/editor/editor_uve.h"
#include "uve/objects/3d/camera_3d_uve.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "editor_axis_input_uve.h"
#include "editor_chrome_layout_uve.h"
#include "editor_fonts_uve.h"
#include "editor_entity_label_uve.h"
#include "editor_object_icons_uve.h"

#include "uve/asset/asset_import_queue_uve.h"
#include "uve/component/animation_sequencer_component_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/canvas_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/prefab_instance_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/world_environment_3d_uve.h"
#include "uve/scene/i_scene_graph_uve.h"

namespace UVE::Editor {

namespace {

// File-local to the inspector, exactly as they were file-local to editor_uve.cpp. Verified before
// moving that nothing outside this panel calls them - unlike the chrome layout and the icon
// helpers, which had callers elsewhere and had to become shared headers instead.

[[nodiscard]] const char* ImportJobStateLabelUVE(const Asset::AssetImportJobStateUVE state) noexcept {
    switch (state) {
        case Asset::AssetImportJobStateUVE::Queued:
            return "Queued";
        case Asset::AssetImportJobStateUVE::Running:
            return "Running";
        case Asset::AssetImportJobStateUVE::Succeeded:
            return "Succeeded";
        case Asset::AssetImportJobStateUVE::Failed:
            return "Failed";
    }
    return "Unknown";
}

[[nodiscard]] const char* ImportDiagnosticSeverityLabelUVE(
    const Asset::AssetImportDiagnosticSeverityUVE severity) noexcept {
    switch (severity) {
        case Asset::AssetImportDiagnosticSeverityUVE::Warning:
            return "Warning";
        case Asset::AssetImportDiagnosticSeverityUVE::Error:
            return "Error";
    }
    return "Unknown";
}


/// A class-chain heading: the ancestor the sections below it come from. A quiet label with a rule
/// to the edge, so it groups without competing with the section headers.
void DrawInspectorChainHeaderUVE(const std::string& label) {
    ImGui::Dummy(ImVec2(0.0F, 4.0F));
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55F, 0.66F, 0.80F, 1.0F));
    ImGui::TextUnformatted(label.c_str());
    ImGui::PopStyleColor();
    const float textWidth = ImGui::CalcTextSize(label.c_str()).x;
    const float ruleY = start.y + ImGui::GetTextLineHeight() * 0.5F;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(start.x + textWidth + 6.0F, ruleY), ImVec2(start.x + width, ruleY),
                                        IM_COL32(88, 104, 128, 160), 1.0F);
}


/// A counter-clockwise circular arrow glyph for resetting a property to its default value.
void DrawResetGlyphUVE(ImDrawList& drawList, const ImVec2 center, const float size, const ImU32 color) {
    constexpr float kPi = std::numbers::pi_v<float>;
    const float radius = size * 0.35F;
    const float thickness = std::max(1.5F, size * 0.13F);
    constexpr int kSegments = 16;
    constexpr float kStartAngle = 0.40F * kPi;
    constexpr float kEndAngle = 1.85F * kPi;
    for (int i = 0; i < kSegments; ++i) {
        const float a0 = kStartAngle + (static_cast<float>(i) * (kEndAngle - kStartAngle) / static_cast<float>(kSegments));
        const float a1 = kStartAngle + (static_cast<float>(i + 1) * (kEndAngle - kStartAngle) / static_cast<float>(kSegments));
        const ImVec2 p0{center.x + (radius * std::cos(a0)), center.y + (radius * std::sin(a0))};
        const ImVec2 p1{center.x + (radius * std::cos(a1)), center.y + (radius * std::sin(a1))};
        drawList.AddLine(p0, p1, color, thickness);
    }
    const ImVec2 tip{center.x + (radius * std::cos(kStartAngle)), center.y + (radius * std::sin(kStartAngle))};
    const float arrowLen = size * 0.30F;
    drawList.AddLine(tip, ImVec2{tip.x - (arrowLen * 0.85F), tip.y - (arrowLen * 0.15F)}, color, thickness);
    drawList.AddLine(tip, ImVec2{tip.x + (arrowLen * 0.15F), tip.y - (arrowLen * 0.85F)}, color, thickness);
}

/// A horseshoe magnet glyph for proportional/uniform scaling toggle.
void DrawMagnetGlyphUVE(ImDrawList& drawList, const ImVec2 center, const float size, const bool active) {
    constexpr float kPi = std::numbers::pi_v<float>;
    const float halfWidth = size * 0.38F;
    const float halfHeight = size * 0.42F;
    const float thickness = std::max(1.8F, size * 0.20F);
    const float topY = center.y - halfHeight;
    const float leftX = center.x - halfWidth + (thickness * 0.5F);
    const float rightX = center.x + halfWidth - (thickness * 0.5F);
    const float bendY = center.y + (halfHeight * 0.12F);

    const ImU32 bodyColor = active ? IM_COL32(235, 75, 75, 255) : IM_COL32(130, 130, 140, 200);
    const ImU32 capColor = active ? IM_COL32(110, 195, 255, 255) : IM_COL32(170, 170, 180, 180);

    const float capHeight = size * 0.26F;
    drawList.AddLine(ImVec2{leftX, bendY}, ImVec2{leftX, topY + capHeight}, bodyColor, thickness);
    drawList.AddLine(ImVec2{rightX, bendY}, ImVec2{rightX, topY + capHeight}, bodyColor, thickness);

    const float arcRadius = (rightX - leftX) * 0.5F;
    const ImVec2 arcCenter{(leftX + rightX) * 0.5F, bendY};
    constexpr int kSegments = 10;
    for (int i = 0; i < kSegments; ++i) {
        const float a0 = static_cast<float>(i) * kPi / static_cast<float>(kSegments);
        const float a1 = static_cast<float>(i + 1) * kPi / static_cast<float>(kSegments);
        const ImVec2 p0{arcCenter.x - (arcRadius * std::cos(a0)), arcCenter.y + (arcRadius * std::sin(a0))};
        const ImVec2 p1{arcCenter.x - (arcRadius * std::cos(a1)), arcCenter.y + (arcRadius * std::sin(a1))};
        drawList.AddLine(p0, p1, bodyColor, thickness);
    }

    drawList.AddLine(ImVec2{leftX, topY + capHeight}, ImVec2{leftX, topY}, capColor, thickness);
    drawList.AddLine(ImVec2{rightX, topY + capHeight}, ImVec2{rightX, topY}, capColor, thickness);

    if (active) {
        const float waveY = topY - 1.5F;
        drawList.AddLine(ImVec2{leftX + (thickness * 0.3F), waveY},
                         ImVec2{rightX - (thickness * 0.3F), waveY},
                         IM_COL32(255, 215, 75, 220), 1.2F);
    }
}

/// A compact reset icon button for inspector rows.
bool DrawResetButtonUVE(const char* const id, const char* const tooltip) {
    const float size = ImGui::GetFrameHeight();
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(38, 42, 50, 180));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(65, 72, 90, 240));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(95, 105, 130, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0.0F, 0.0F});
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::Button(id, ImVec2{size, size});
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const ImVec2 center{pos.x + (size * 0.5F), pos.y + (size * 0.5F)};
    const ImU32 color = hovered ? IM_COL32(255, 215, 80, 255) : IM_COL32(185, 190, 205, 220);
    DrawResetGlyphUVE(drawList, center, size * 0.65F, color);
    if (hovered && tooltip != nullptr) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return pressed;
}

/// A compact magnet toggle button for proportional/uniform scaling.
bool DrawMagnetButtonUVE(const char* const id, const bool active) {
    const float size = ImGui::GetFrameHeight();
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(40, 80, 125, 230));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(60, 110, 165, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(80, 130, 190, 255));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(38, 42, 50, 180));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(60, 65, 78, 220));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(80, 85, 100, 255));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0.0F, 0.0F});
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::Button(id, ImVec2{size, size});
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const ImVec2 center{pos.x + (size * 0.5F), pos.y + (size * 0.5F)};
    DrawMagnetGlyphUVE(drawList, center, size * 0.65F, active);
    if (hovered) {
        ImGui::SetTooltip("%s", active ? "Uniform Scale Locked (Magnet ON)\nScaling any axis scales all axes together."
                                       : "Uniform Scale Unlocked (Magnet OFF)\nEach axis scales independently.");
    }
    return pressed;
}

} // namespace

void EditorUVE::DrawInspectorPanelUVE() {
    // A colour edit whose object is no longer the one selected - picked elsewhere while its picker
    // was open - is finished as it stands rather than left waiting for a picker nobody can see.
    if (m_componentPropertyPreview.has_value() &&
        (m_componentPropertyPreview->entity != m_selectedEntity || !HasSingleDocumentSelectionUVE())) {
        static_cast<void>(CommitComponentPropertyPreviewUVE());
    }
    if (!m_inspectorPanelVisible) {
        return;
    }
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    const EditorChromeLayoutUVE layout =
        ComputeEditorChromeLayoutUVE(*mainViewport, m_bottomDockVisible, m_bottomDockHeight, m_scenePanelWidth,
                                     m_inspectorPanelWidth, m_scenePanelVisible,
                                     m_inspectorPanelVisible);
    // Always: the rect comes from the chrome layout every frame. Dragging happens on the seam in
    // DrawEditorChromeHandlesUVE(), which writes m_inspectorPanelWidth - not on the window's edges.
    ImGui::SetNextWindowViewport(mainViewport->ID);
    ImGui::SetNextWindowPos(layout.inspectorPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(layout.inspectorSize, ImGuiCond_Always);
    // No title row and no close button: the Inspector / Import / Events tabs ARE the panel's top
    // edge, so the name is not said twice above them. Passing nullptr instead of
    // &m_inspectorPanelVisible is what removes the X - ImGui only draws a close button when it is
    // given a visibility flag to write back to. Hiding it is Window > Inspector, beside the other
    // panel toggles, which is where a hidden panel is brought back from.
    // The layout owns this rect; ImGui's own edge grip must not (see kLayoutOwnedPanelFlagsUVE).
    constexpr ImGuiWindowFlags flags = kLayoutOwnedPanelFlagsUVE;
    // Match the Outliner panel's neutral charcoal surface. Inputs and nested fields keep the
    // darker FrameBg/ChildBg layers from the shared theme, so depth and editability remain clear.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.106F, 0.118F, 0.129F, 1.0F});
    ImGui::Begin(kPanelLabelInspectorUVE, nullptr, flags);

    // Real tabs rather than three selectable labels. The active tab can also be changed from
    // elsewhere (the Content Browser opens the Inspector), so a change made outside the strip is
    // pushed into it once, on the frame it happened.
    const bool externallyChanged = m_activeRightPanelTab != m_drawnRightPanelTab;
    if (ImGui::BeginTabBar("##right-panel-tabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        const auto drawTab = [this, externallyChanged](const char* const label, const EditorRightPanelTabUVE tab) {
            const ImGuiTabItemFlags tabFlags =
                externallyChanged && m_activeRightPanelTab == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem(label, nullptr, tabFlags)) {
                if (!externallyChanged) {
                    m_activeRightPanelTab = tab;
                }
                ImGui::EndTabItem();
            }
        };
        drawTab("Inspector", EditorRightPanelTabUVE::Inspector);
        drawTab("Import", EditorRightPanelTabUVE::Import);
        drawTab("Events", EditorRightPanelTabUVE::Events);
        ImGui::EndTabBar();
    }
    m_drawnRightPanelTab = m_activeRightPanelTab;

    switch (m_activeRightPanelTab) {
        case EditorRightPanelTabUVE::Inspector:
            DrawInspectorContentUVE();
            break;
        case EditorRightPanelTabUVE::Import:
            DrawImportQueueMonitorUVE();
            break;
        case EditorRightPanelTabUVE::Events:
            ImGui::TextUnformatted("Events");
            ImGui::TextDisabled("Event bindings remain unavailable until the scripting runtime is added.");
            break;
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void EditorUVE::DrawImportQueueMonitorUVE() {
    ImGui::TextUnformatted("Import Queue");
    ImGui::TextDisabled("Read-only monitor. Enqueue and retry are programmatic-only in v1.");

    const std::vector<Asset::AssetImportJobUVE> jobs = m_services->GetAssetImportQueueUVE().GetJobsUVE();
    if (jobs.empty()) {
        ImGui::TextDisabled("No import jobs have been queued.");
        return;
    }

    ImGui::Text("%zu job(s)", jobs.size());
    ImGui::BeginChild("##import-queue-monitor", ImVec2{0.0F, 0.0F}, true);
    for (const Asset::AssetImportJobUVE& job : jobs) {
        const std::string header = "Job #" + std::to_string(job.id.value) + " — " +
                                   ImportJobStateLabelUVE(job.state) + "##import-job-" +
                                   std::to_string(job.id.value);
        if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            const std::string sourcePath = job.request.sourcePath.generic_string();
            const std::string destinationPath = job.request.destinationPath.generic_string();
            ImGui::TextWrapped("Source: %s", sourcePath.c_str());
            ImGui::TextWrapped("Destination: %s", destinationPath.c_str());
            ImGui::Text("Attempt: %u", job.attemptCount);
            ImGui::Text("Result: %s", job.cacheHit ? "Cache hit" : "Importer path");
            if (job.resultGuid.has_value()) {
                ImGui::Text("GUID: %016llX", static_cast<unsigned long long>(job.resultGuid->value));
            }
            for (const Asset::AssetImportDiagnosticUVE& diagnostic : job.diagnostics) {
                ImGui::Separator();
                ImGui::Text("%s (attempt %u)", ImportDiagnosticSeverityLabelUVE(diagnostic.severity),
                            diagnostic.attempt);
                ImGui::TextWrapped("%s", diagnostic.message.c_str());
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
}

void EditorUVE::DrawContentAssetCardInspectorUVE(const Asset::ProjectFileEntryUVE& entry) {
    ImGui::BeginChild("##inspector-asset-card", ImVec2{0.0F, 0.0F}, true);

    const auto snapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path fullPath = snapshot.contentRoot / entry.relativePath;
    const std::string filename = entry.relativePath.filename().string();
    const ContentBrowserItemTypeUVE type = ClassifyContentBrowserEntryUVE(entry);
    const char* const typeLabel = GetContentBrowserItemTypeLabelUVE(type);

    // Header with Icon & Name
    std::uintptr_t iconId = 0U;
    if (type == ContentBrowserItemTypeUVE::Texture) {
        iconId = GetTextureThumbnailUVE(entry.relativePath);
    } else if (type == ContentBrowserItemTypeUVE::Mesh || type == ContentBrowserItemTypeUVE::Model) {
        iconId = GetMeshThumbnailUVE(entry.relativePath);
    }
    if (iconId == 0U) {
        if (type == ContentBrowserItemTypeUVE::Entity || type == ContentBrowserItemTypeUVE::Prefab) {
            iconId = m_uiAssets.GetObjectIconTextureIdUVE(Scene::Objects::SceneObjectKindUVE::Object3D);
        } else {
            iconId = m_uiAssets.GetContentTypeIconTextureIdUVE(typeLabel);
        }
    }

    if (iconId != 0U) {
        ImGui::Image(static_cast<ImTextureID>(iconId), ImVec2{48.0F, 48.0F});
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4{0.95F, 0.95F, 0.98F, 1.0F}, "%s", filename.c_str());
    ImGui::TextColored(ImVec4{0.4F, 0.75F, 0.95F, 1.0F}, "[ %s ]", typeLabel);
    ImGui::EndGroup();

    ImGui::Separator();
    ImGui::Spacing();

    // Details: Path, Size, GUID
    ImGui::TextDisabled("FILE INFORMATION");
    ImGui::TextWrapped("Path: %s", entry.relativePath.generic_string().c_str());

    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(fullPath, ec);
    if (!ec) {
        if (fileSize < 1024U) {
            ImGui::Text("Size: %llu B", static_cast<unsigned long long>(fileSize));
        } else if (fileSize < 1024U * 1024U) {
            ImGui::Text("Size: %.1f KB", static_cast<double>(fileSize) / 1024.0);
        } else {
            ImGui::Text("Size: %.2f MB", static_cast<double>(fileSize) / (1024.0 * 1024.0));
        }
    }

    if (entry.registeredAssetGuid.has_value()) {
        ImGui::Text("Asset GUID: %016llX", static_cast<unsigned long long>(entry.registeredAssetGuid->value));
    } else {
        ImGui::TextDisabled("Asset GUID: (Unregistered)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Unreal shows an import's outcome on the asset itself; here the closest thing to the asset is
    // the source file's card, and without this the only record of a failed conversion was the log.
    if (IsModelSourcePathUVE(entry.relativePath)) {
        ImGui::TextDisabled("MODEL IMPORT");
        const std::filesystem::path converted = GetImportedModelPathUVE(entry.relativePath);
        std::error_code convertedError;
        const bool convertedExists = std::filesystem::is_regular_file(converted, convertedError);
        if (const std::string* const failure = FindModelImportFailureUVE(entry.relativePath); failure != nullptr) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.95F, 0.45F, 0.40F, 1.0F});
            ImGui::TextWrapped("Failed: %s", failure->c_str());
            ImGui::PopStyleColor();
            ImGui::TextWrapped("Reimport retries it; the log line names the same reason.");
        } else if (convertedExists) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.48F, 0.82F, 0.52F, 1.0F});
            ImGui::TextUnformatted("Converted");
            ImGui::PopStyleColor();
            ImGui::TextWrapped("%s", converted.generic_string().c_str());
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.95F, 0.78F, 0.40F, 1.0F});
            ImGui::TextUnformatted("Waiting for the import queue");
            ImGui::PopStyleColor();
        }
        if (const EditorModelSourceInfoUVE* const info = FindModelSourceInfoUVE(entry.relativePath);
            info != nullptr && !info->summary.empty()) {
            ImGui::TextWrapped("%s", info->summary.c_str());
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // Action buttons
    ImGui::TextDisabled("ACTIONS");
    if (type == ContentBrowserItemTypeUVE::Entity || type == ContentBrowserItemTypeUVE::Prefab) {
        if (ImGui::Button("Open in Entity Editor", ImVec2{-1.0F, 28.0F})) {
            OpenEntityEditorUVE(fullPath);
        }
    } else if (type == ContentBrowserItemTypeUVE::Mesh || type == ContentBrowserItemTypeUVE::Model ||
               IsModelSourcePathUVE(entry.relativePath)) {
        if (ImGui::Button("Reimport Model", ImVec2{-1.0F, 28.0F})) {
            QueueModelAutoImportsUVE(m_services->GetProjectFileIndexUVE().GetSnapshotUVE());
            m_contentStatusMessage = "Queued model reimport for " + filename;
        }
    } else if (type == ContentBrowserItemTypeUVE::Scene) {
        if (ImGui::Button("Open Scene", ImVec2{-1.0F, 28.0F})) {
            static_cast<void>(OpenSceneAssetUVE(fullPath));
        }
    }

    ImGui::EndChild();
}

namespace {

/// One width for every control in the Inspector, held for the whole content pass.
///
/// ImGui's default item width is a fraction of the window, and each row is free to override it, so
/// a dropdown, a slider and a number field on neighbouring rows ended at different X positions and
/// read as three unrelated panels rather than one list. "Full width" is the one rule that needs no
/// per-row decision: the control starts where the row's label ends and runs to the panel's edge, so
/// every row looks like every other row no matter how long its label is.
///
/// Scoped rather than pushed and popped by hand because the content has several early returns -
/// an empty selection, a project file, a multi-selection - and a missed pop would leave every later
/// panel drawing at this width.
struct ScopedInspectorItemWidthUVE final {
    ScopedInspectorItemWidthUVE() {
        ImGui::PushItemWidth(-FLT_MIN);
    }
    ~ScopedInspectorItemWidthUVE() {
        ImGui::PopItemWidth();
    }
    ScopedInspectorItemWidthUVE(const ScopedInspectorItemWidthUVE&) = delete;
    ScopedInspectorItemWidthUVE& operator=(const ScopedInspectorItemWidthUVE&) = delete;
};

} // namespace

void EditorUVE::DrawInspectorContentUVE() {
    const ScopedInspectorItemWidthUVE uniformControlWidth;
    if (m_selectedEntities.empty()) {
        if (m_selectedProjectFile.has_value() && !m_selectedProjectFile->relativePath.empty()) {
            DrawContentAssetCardInspectorUVE(*m_selectedProjectFile);
            return;
        }
        ImGui::BeginChild("##inspector-empty-state", ImVec2{0.0F, 64.0F}, true);
        ImGui::TextColored(ImVec4{0.80F, 0.82F, 0.85F, 1.0F}, "NO ENTITY SELECTED");
        ImGui::TextDisabled("Select an entity in the Outliner or the Viewport to inspect it.");
        ImGui::EndChild();
        return;
    }
    if (!HasSingleDocumentSelectionUVE()) {
        ImGui::Text("%zu entities selected", m_selectedEntities.size());
        if (IsDocumentEntityUVE(m_selectedEntity)) {
            ImGui::Text("Active: %s", GetEntityDisplayLabelUVE(m_selectedEntity).c_str());
        }
        ImGui::Separator();
        for (const Scene::EntityUVE entity : m_selectedEntities) {
            if (IsDocumentEntityUVE(entity)) {
                ImGui::BulletText("%s%s", GetEntityDisplayLabelUVE(entity).c_str(),
                                  entity == m_selectedEntity ? " (Active)" : "");
            }
        }
        ImGui::TextDisabled("Single-entity editing is unavailable for multi-selection.");
        return;
    }

    if (Scene::IsDocumentCameraEntityUVE(m_services->GetEntityManagerUVE(), m_selectedEntity)) {
        const std::uintptr_t camIcon = m_uiAssets.GetComponentIconTextureIdUVE("camera");
        const bool previewOpen = DrawInspectorFoldUVE("Camera Preview###camera-preview-section",
                                                      "section:camera-preview", true, true, 0, camIcon);
        if (previewOpen) {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const float previewWidth = std::max(160.0F, available.x);
            const Math::Vector2UVE previewSize{previewWidth, previewWidth * (9.0F / 16.0F)};
            Math::Vector2UVE usedSize{};
            const std::uint64_t textureId = m_viewportPanelRenderer
                                                 ? m_viewportPanelRenderer(
                                                       ViewportContextUVE::InspectorCameraPreview, previewSize,
                                                       usedSize, m_viewportOverlayState)
                                                 : 0U;
            if (textureId != 0U && usedSize.x > 0.0F && usedSize.y > 0.0F) {
                ImGui::Image(static_cast<ImTextureID>(textureId), ImVec2{usedSize.x, usedSize.y},
                             ImVec2{0.0F, 1.0F}, ImVec2{1.0F, 0.0F});
            } else {
                ImGui::TextDisabled("Camera preview is not ready.");
            }
        }
        ImGui::Separator();
    }

    ImGui::BeginDisabled(!IsAuthoringCommandAllowedUVE());
    ImGui::Text("%s", GetEntityDisplayLabelUVE(m_selectedEntity).c_str());
    // Every Inspector is its object's recipe and nothing else, parent by parent: the object's own
    // section, each abstract base, Object3D's Transform and Visibility, then the common Object
    // section. Objects are renamed and reparented from the Scene panel and get their parts from
    // their recipe, so there is no name field, hierarchy block, search box, Add Component or Remove
    // here.

    // A section that asked for a component to be added or removed last frame gets it here, before
    // this frame's sections are drawn from the entity's storage.
    ApplyPendingComponentChangeUVE();
    RepairInspectorRecipeUVE(m_selectedEntity);
    ImGui::Separator();
    m_inspectorDrawerRegistry.DrawEligibleUVE(m_selectedEntity);
    ImGui::EndDisabled();
}

void EditorUVE::RegisterTransformInspectorDrawerUVE() {
    static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
        "transform",
        [this](const Scene::EntityUVE entity) {
            return IsDocumentEntityUVE(entity) &&
                   m_services->GetEntityManagerUVE().HasComponentUVE<Scene::TransformComponentUVE>(entity);
        },
        [this](const Scene::EntityUVE entity) { DrawTransformInspectorDrawerUVE(entity); },
    }));
    static_cast<void>(m_inspectorDrawerRegistry.SetDrawerGroupUVE("transform", "Object3D"));
}

void EditorUVE::RepairInspectorRecipeUVE(const Scene::EntityUVE entity) {
    // An object saved before its recipe included Visibility and the Object section is given them here,
    // where they are first needed. Every default is Inherit or empty, so this changes nothing
    // about how the scene runs.
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!IsDocumentEntityUVE(entity)) {
        return;
    }
    if (entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity) &&
        !entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity)) {
        entityManager.AddComponentUVE<Scene::VisibilityComponentUVE>(entity, Scene::VisibilityComponentUVE{});
    }
    Scene::EnsureCommonObjectSectionUVE(entityManager, entity);
}

void EditorUVE::RegisterBuiltInInspectorDrawersUVE() {
    // Everything before "prefab-instance" below, the Transform section included, used to be registered by
    // hand here: a lambda with a fifteen-case switch over EditorSceneComponentKindUVE to decide
    // eligibility, fifteen calls into it, and a separate registration for each drawer that had
    // grown real fields. All of it is now generated from what each component declares about
    // itself, in registration order by declared section - see
    // editor_panel_inspector_metadata_uve.cpp. Nine of those drawers had never been finished and
    // only showed a title and a Remove button; they are complete field editors now without any of
    // them being written.
    m_inspectorDrawerRegistry.SetGroupHeaderDrawerUVE([](const std::string& label) { DrawInspectorChainHeaderUVE(label); });
    RegisterMetadataInspectorDrawersUVE();
    static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
        "prefab-instance",
        [this](const Scene::EntityUVE entity) {
            return IsDocumentEntityUVE(entity) &&
                   m_services->GetEntityManagerUVE().HasComponentUVE<Scene::PrefabInstanceComponentUVE>(entity);
        },
        [this](const Scene::EntityUVE entity) { DrawPrefabInspectorDrawerUVE(entity); },
    }));
}

void EditorUVE::DrawTransformInspectorDrawerUVE(const Scene::EntityUVE entity) {
    if (!IsDocumentEntityUVE(entity)) {
        return;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity)) {
        return;
    }

    Scene::TransformComponentUVE edited = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
    // A collapsible section like every other one; its open state is remembered across sessions.
    const std::uintptr_t transIcon = m_uiAssets.GetComponentIconTextureIdUVE("transform");
    const bool transformOpen = DrawInspectorFoldUVE("Transform##transform-section", "section:transform", true, true, 0, transIcon);
    DrawInspectorSectionMenuUVE(nullptr, "Transform");
    if (!transformOpen) {
        return;
    }
    float position[3]{edited.localPosition.x, edited.localPosition.y, edited.localPosition.z};
    // Displayed/edited as Euler degrees (Position/Scale's own 3-box shape, and the convention
    // every other engine's Inspector uses) even though the stored/serialized rotation stays a
    // quaternion - TryToEulerUVE()/TryMakeEulerUVE() are the display/edit-boundary conversion,
    // never touching TransformComponentUVE's own data shape.
    constexpr float kRadiansToDegreesUVE = 180.0F / std::numbers::pi_v<float>;
    constexpr float kDegreesToRadiansUVE = std::numbers::pi_v<float> / 180.0F;
    Math::Vector3UVE eulerRadians{};
    const bool haveEuler = Math::TryToEulerUVE(edited.localRotation, eulerRadians);
    float rotationDegrees[3]{haveEuler ? eulerRadians.x * kRadiansToDegreesUVE : 0.0F,
                             haveEuler ? eulerRadians.y * kRadiansToDegreesUVE : 0.0F,
                             haveEuler ? eulerRadians.z * kRadiansToDegreesUVE : 0.0F};
    float scale[3]{edited.localScale.x, edited.localScale.y, edited.localScale.z};

    bool positionChanged = false;
    bool rotationChanged = false;
    bool scaleChanged = false;
    bool gestureEnded = false;

    const float resetBtnWidth = ImGui::GetFrameHeight();
    if (ImGui::BeginTable("##transform", 3, ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 72.0F);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, resetBtnWidth);

        // Position row: individual circular-arrow reset button resets position to (0, 0, 0)
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Position");
        ImGui::TableSetColumnIndex(1);
        const bool posInputChanged = DrawAxisVectorInputUVE("##local-position", position, 3, 0.01F);
        if (ImGui::IsItemActivated()) {
            if (GetToolSessionPhaseUVE() == EditorToolSessionPhaseUVE::Previewing) {
                static_cast<void>(CommitTransformGestureUVE());
            }
            static_cast<void>(BeginTransformGestureUVE(EditorToolSessionModeUVE::Translate));
        }
        gestureEnded = (ImGui::IsItemDeactivated() && !ImGui::IsItemActive()) || gestureEnded;
        ImGui::TableSetColumnIndex(2);
        bool posResetClicked = false;
        if (DrawResetButtonUVE("##reset-position", "Reset Position to (0, 0, 0)")) {
            position[0] = 0.0F;
            position[1] = 0.0F;
            position[2] = 0.0F;
            posResetClicked = true;
        }
        positionChanged = posInputChanged || posResetClicked;

        // Rotation row: individual circular-arrow reset button resets rotation to (0, 0, 0)
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Rotation");
        ImGui::TableSetColumnIndex(1);
        const bool rotInputChanged = DrawAxisVectorInputUVE("##local-rotation", rotationDegrees, 3, 0.5F);
        if (ImGui::IsItemActivated()) {
            if (GetToolSessionPhaseUVE() == EditorToolSessionPhaseUVE::Previewing) {
                static_cast<void>(CommitTransformGestureUVE());
            }
            static_cast<void>(BeginTransformGestureUVE(EditorToolSessionModeUVE::Rotate));
        }
        gestureEnded = (ImGui::IsItemDeactivated() && !ImGui::IsItemActive()) || gestureEnded;
        ImGui::TableSetColumnIndex(2);
        bool rotResetClicked = false;
        if (DrawResetButtonUVE("##reset-rotation", "Reset Rotation to (0, 0, 0)")) {
            rotationDegrees[0] = 0.0F;
            rotationDegrees[1] = 0.0F;
            rotationDegrees[2] = 0.0F;
            rotResetClicked = true;
        }
        rotationChanged = rotInputChanged || rotResetClicked;

        // Scale row: magnet button toggles uniform scaling; circular-arrow reset button resets scale to (1, 1, 1)
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Scale");
        ImGui::SameLine(0.0F, 3.0F);
        if (DrawMagnetButtonUVE("##scale-magnet", m_uniformScaleLocked)) {
            m_uniformScaleLocked = !m_uniformScaleLocked;
        }
        ImGui::TableSetColumnIndex(1);
        const float oldScale[3]{scale[0], scale[1], scale[2]};
        const bool scaleInputChanged = DrawAxisVectorInputUVE("##local-scale", scale, 3, 0.01F);
        if (ImGui::IsItemActivated()) {
            if (GetToolSessionPhaseUVE() == EditorToolSessionPhaseUVE::Previewing) {
                static_cast<void>(CommitTransformGestureUVE());
            }
            static_cast<void>(BeginTransformGestureUVE(EditorToolSessionModeUVE::Scale));
        }
        gestureEnded = (ImGui::IsItemDeactivated() && !ImGui::IsItemActive()) || gestureEnded;

        // When uniform scale / magnet is locked, adjust all axes in sync
        if (scaleInputChanged && m_uniformScaleLocked) {
            int changedAxis = -1;
            for (int i = 0; i < 3; ++i) {
                if (scale[i] != oldScale[i]) {
                    changedAxis = i;
                    break;
                }
            }
            if (changedAxis >= 0) {
                const float oldVal = oldScale[changedAxis];
                const float newVal = scale[changedAxis];
                if (std::abs(oldVal) > 1.0e-5F) {
                    const float ratio = newVal / oldVal;
                    for (int j = 0; j < 3; ++j) {
                        if (j != changedAxis) {
                            scale[j] = oldScale[j] * ratio;
                        }
                    }
                } else {
                    const float delta = newVal - oldVal;
                    for (int j = 0; j < 3; ++j) {
                        if (j != changedAxis) {
                            scale[j] = (oldScale[j] == 0.0F) ? newVal : (oldScale[j] + delta);
                        }
                    }
                }
            }
        }
        ImGui::TableSetColumnIndex(2);
        bool scaleResetClicked = false;
        if (DrawResetButtonUVE("##reset-scale", "Reset Scale to (1, 1, 1)")) {
            scale[0] = 1.0F;
            scale[1] = 1.0F;
            scale[2] = 1.0F;
            scaleResetClicked = true;
        }
        scaleChanged = scaleInputChanged || scaleResetClicked;

        ImGui::EndTable();
    }

    if (positionChanged || rotationChanged || scaleChanged) {
        edited.localPosition = Math::Vector3UVE{position[0], position[1], position[2]};
        if (rotationChanged) {
            Math::QuaternionUVE newRotation{};
            const Math::Vector3UVE radians{rotationDegrees[0] * kDegreesToRadiansUVE,
                                           rotationDegrees[1] * kDegreesToRadiansUVE,
                                           rotationDegrees[2] * kDegreesToRadiansUVE};
            if (Math::TryMakeEulerUVE(radians, newRotation)) {
                edited.localRotation = newRotation;
                edited.localEulerRadians = radians;
                edited.rotationEditMode = Scene::RotationEditModeUVE::Euler;
            }
        }
        edited.localScale = Math::Vector3UVE{scale[0], scale[1], scale[2]};
        // Outside a gesture (it could not start, or the value changed without the field being
        // held) the edit is recorded on its own, as before.
        if (GetToolSessionPhaseUVE() != EditorToolSessionPhaseUVE::Previewing ||
            !PreviewTransformGestureValueUVE(edited)) {
            static_cast<void>(SetSelectedLocalTransformUVE(edited));
        }
    }
    if (gestureEnded && GetToolSessionPhaseUVE() == EditorToolSessionPhaseUVE::Previewing) {
        static_cast<void>(CommitTransformGestureUVE());
    }
}

void EditorUVE::DrawPrefabInspectorDrawerUVE(const Scene::EntityUVE entity) {
    if (!IsDocumentEntityUVE(entity) || entity != m_selectedEntity) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::PrefabInstanceComponentUVE>(entity)) {
        return;
    }
    const Scene::PrefabInstanceComponentUVE& instance =
        entityManager.GetComponentUVE<Scene::PrefabInstanceComponentUVE>(entity);
    const std::filesystem::path sourcePath =
        m_services->GetAssetDatabaseUVE().ResolveUVE(instance.sourcePrefabGuid);
    const std::optional<std::uint64_t> observedRevision =
        Scene::ComputePrefabSourceRevisionUVE(sourcePath);
    ImGui::Separator();
    DrawNativeIconLabelUVE(m_uiAssets.GetContentTypeIconTextureIdUVE("Prefab"), "Prefab Instance");
    ImGui::Text("Source GUID: %llu", static_cast<unsigned long long>(instance.sourcePrefabGuid.value));
    ImGui::Text("Instance revision: %llu", static_cast<unsigned long long>(instance.instanceRevision));
    ImGui::Text("Source revision: %s", observedRevision.has_value() ? "available" : "unavailable");
    ImGui::Text("Local overrides: %zu", instance.overrides.size());
    if (!instance.overrides.empty()) {
        ImGui::TextColored(ImVec4{1.0F, 0.72F, 0.25F, 1.0F}, "Merge required before refresh.");
        if (ImGui::Button("Discard Overrides & Refresh")) {
            static_cast<void>(DiscardSelectedPrefabOverridesAndRefreshUVE());
        }
    } else if (observedRevision.has_value() && *observedRevision != instance.instanceRevision) {
        if (ImGui::Button("Refresh Prefab")) {
            static_cast<void>(RefreshSelectedPrefabUVE());
        }
    } else {
        ImGui::TextDisabled("Prefab instance is current.");
    }
    if (!sourcePath.empty()) {
        ImGui::TextDisabled("%s", sourcePath.generic_string().c_str());
    }
}


void EditorUVE::SetInspectorFoldOpenUVE(const std::string& key, const bool open) {
    if (key.empty()) {
        return;
    }
    const auto it = m_inspectorFoldOpen.find(key);
    if (it != m_inspectorFoldOpen.end()) {
        it->second = open;
    } else if (m_inspectorFoldOpen.size() < kMaxRememberedInspectorFoldsUVE) {
        m_inspectorFoldOpen.emplace(key, open);
    }
}

bool EditorUVE::IsInspectorFoldOpenUVE(const std::string& key, const bool defaultOpen) const {
    const auto it = m_inspectorFoldOpen.find(key);
    return it != m_inspectorFoldOpen.end() ? it->second : defaultOpen;
}

bool EditorUVE::DrawInspectorFoldUVE(const char* const label, const std::string& key, const bool defaultOpen,
                                     const bool asHeader, const int flags, const std::uintptr_t iconTextureId) {
    // The editor, not Dear ImGui's per-window storage, owns the state: that storage is lost on
    // exit, and the Inspector should reopen the way it was left.
    const bool open = IsInspectorFoldOpenUVE(key, defaultOpen);
    ImGui::SetNextItemOpen(open, ImGuiCond_Always);
    const auto treeFlags = static_cast<ImGuiTreeNodeFlags>(flags);
    std::string displayLabel;
    if (iconTextureId != 0U) {
        displayLabel = "      " + std::string(label);
    }
    const char* const renderText = iconTextureId != 0U ? displayLabel.c_str() : label;
    const bool nowOpen = asHeader ? ImGui::CollapsingHeader(renderText, treeFlags) : ImGui::TreeNodeEx(renderText, treeFlags);
    if (iconTextureId != 0U) {
        const ImVec2 rectMin = ImGui::GetItemRectMin();
        const ImVec2 rectMax = ImGui::GetItemRectMax();
        const float iconSize = 16.0F;
        const float arrowOffset = ImGui::GetFontSize() + ImGui::GetStyle().ItemInnerSpacing.x + 4.0F;
        const float iconX = rectMin.x + arrowOffset;
        const float iconY = std::floor(rectMin.y + (rectMax.y - rectMin.y - iconSize) * 0.5F);
        ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(iconTextureId),
                                             ImVec2{iconX, iconY},
                                             ImVec2{iconX + iconSize, iconY + iconSize});
    }
    if (nowOpen != open) {
        SetInspectorFoldOpenUVE(key, nowOpen);
    }
    return nowOpen;
}

} // namespace UVE::Editor
