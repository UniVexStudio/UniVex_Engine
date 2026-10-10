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
#include "uve/editor/editor_settings_uve.h"
#include "uve/objects/3d/camera_3d_uve.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <filesystem>
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

constexpr const char* kPanelLabelInspectorUVE = "\xEE\xA8\x83 Inspector##right-panel";

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

} // namespace

void EditorUVE::DrawInspectorPanelUVE() {
    // A colour edit whose object is no longer the one selected - picked elsewhere while its picker
    // was open - is finished as it stands rather than left waiting for a picker nobody can see. A
    // drag that never left its selection previews on, including a steady multi-selection drag.
    if (m_componentPropertyPreview.has_value() &&
        (m_componentPropertyPreview->entity != m_selectedEntity ||
         !IsSelectionSnapshotCurrentUVE(m_componentPropertyPreview->selectionBefore))) {
        static_cast<void>(CommitComponentPropertyPreviewUVE());
    }
    if (!m_inspectorPanelVisible) {
        return;
    }
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    const EditorChromeLayoutUVE layout = ComputeEditorChromeLayoutUVE(*mainViewport, m_bottomDockVisible, m_bottomDockHeight);
    // Always, not FirstUseEver - see DrawHierarchyPanelUVE()'s comment on the same change.
    ImGui::SetNextWindowPos(layout.inspectorPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(layout.inspectorSize, ImGuiCond_Always);
    // No title row: the Inspector / Import / Events tabs are the panel's top edge, so the name is
    // not said twice. The panel is fixed in the layout, so there is nothing to drag it by anyway.
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
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

void EditorUVE::DrawInspectorContentUVE() {
    if (m_selectedEntities.empty()) {
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
        ImGui::TextDisabled("An edit below lands on every selected object that has the property, as one undo step.");
        ImGui::Separator();
        ImGui::BeginDisabled(!IsAuthoringCommandAllowedUVE());
        // The sections anchor on the active object; every row still writes the whole selection.
        RepairInspectorRecipeUVE(m_selectedEntity);
        m_inspectorDrawerRegistry.DrawEligibleUVE(m_selectedEntity);
        ImGui::EndDisabled();
        return;
    }

    if (Scene::IsDocumentCameraEntityUVE(m_services->GetEntityManagerUVE(), m_selectedEntity)) {
        const bool previewOpen = DrawInspectorFoldUVE("Camera Preview###camera-preview-section",
                                                      "section:camera-preview", true, true, 0);
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
        // Transform stays a single-object edit: the gizmo behind it moves one object. In a
        // multi-selection the section still shows (anchored on the active object) but cannot edit.
        [this](const Scene::EntityUVE entity) {
            ImGui::BeginDisabled(!HasSingleDocumentSelectionUVE());
            DrawTransformInspectorDrawerUVE(entity);
            ImGui::EndDisabled();
        },
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
    // This legacy-recipe repair may add filterable components; invalidate the outliner only when
    // the entity's component set actually changes.
    const std::size_t componentCountBefore = entityManager.GetComponentTypesUVE(entity).size();
    if (entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity) &&
        !entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity)) {
        entityManager.AddComponentUVE<Scene::VisibilityComponentUVE>(entity, Scene::VisibilityComponentUVE{});
    }
    Scene::EnsureCommonObjectSectionUVE(entityManager, entity);
    if (entityManager.GetComponentTypesUVE(entity).size() != componentCountBefore) {
        InvalidateHierarchyFilterCacheUVE();
    }
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
        // Prefab links stay per-object, like Transform: shown for the active object, read-only
        // while several are selected.
        [this](const Scene::EntityUVE entity) {
            ImGui::BeginDisabled(!HasSingleDocumentSelectionUVE());
            DrawPrefabInspectorDrawerUVE(entity);
            ImGui::EndDisabled();
        },
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
    const bool transformOpen = DrawInspectorFoldUVE("Transform##transform-section", "section:transform", true, true, 0);
    DrawInspectorSectionMenuUVE(nullptr, "Transform");
    if (!transformOpen) {
        return;
    }
    float position[3]{edited.localPosition.x, edited.localPosition.y, edited.localPosition.z};
    // Displayed/edited as Euler angles in the unit the author chose (degrees by default, the
    // convention every other engine's Inspector uses) even though the stored/serialized rotation
    // stays a quaternion - TryToEulerUVE()/TryMakeEulerUVE() are the display/edit-boundary
    // conversion, never touching TransformComponentUVE's own data shape, and these factors are the
    // unit boundary in the middle of it. One factor pair, so what is shown can never disagree with
    // what is stored.
    const AngleDisplayFactorsUVE angleFactors = AngleDisplayFactorsForUVE(m_inspectorAngleDisplay);
    Math::Vector3UVE eulerRadians{};
    const bool haveEuler = Math::TryToEulerUVE(edited.localRotation, eulerRadians);
    float rotationDisplayed[3]{haveEuler ? eulerRadians.x * angleFactors.unitsPerRadian : 0.0F,
                               haveEuler ? eulerRadians.y * angleFactors.unitsPerRadian : 0.0F,
                               haveEuler ? eulerRadians.z * angleFactors.unitsPerRadian : 0.0F};
    float scale[3]{edited.localScale.x, edited.localScale.y, edited.localScale.z};
    // A drag moves a comparable amount in either unit: half a degree per pixel is the shipped feel,
    // and 0.01 rad (~0.57 degrees) is its nearest clean step in radians.
    const float rotationDragSpeed =
        m_inspectorAngleDisplay == EditorAngleDisplayUVE::Degrees ? 0.5F : 0.01F;

    // Label beside value, the layout every other Inspector row uses, with one axis-tagged field per
    // component sharing the value column. Each field clips its own number, so a narrow panel shows
    // fewer digits rather than digits spilling over the box.
    bool positionChanged = false;
    bool rotationChanged = false;
    bool scaleChanged = false;
    bool gestureEnded = false;
    if (ImGui::BeginTable("##transform", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 0.26F);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.74F);
        // Each row is dragged (double-click or Ctrl+click types). A drag is a transform gesture:
        // it starts when a field is taken hold of and ends as one undo step when it is let go.
        const auto row = [this, &gestureEnded](const char* const label, const char* const id, float* const values,
                                               const float speed, const EditorToolSessionModeUVE mode) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            const bool changed =
                DrawAxisVectorInputUVE(id, values, 3, speed, 0.0F, 0.0F, m_inspectorFloatPrecision);
            if (ImGui::IsItemActivated()) {
                // Tabbing from one field to the next ends the first edit and starts another.
                if (GetToolSessionPhaseUVE() == EditorToolSessionPhaseUVE::Previewing) {
                    static_cast<void>(CommitTransformGestureUVE());
                }
                static_cast<void>(BeginTransformGestureUVE(mode));
            }
            gestureEnded = (ImGui::IsItemDeactivated() && !ImGui::IsItemActive()) || gestureEnded;
            // Right-click menu on the row's value group, the row's equivalent of the section
            // menu. The axis input leaves its group as the last item, so the popup anchors to
            // the whole row; each row keys its own popup from its widget id. A mode this switch
            // does not name gets no menu rather than another row's.
            std::optional<TransformClipboardPartUVE> part;
            switch (mode) {
            case EditorToolSessionModeUVE::Translate:
                part = TransformClipboardPartUVE::Position;
                break;
            case EditorToolSessionModeUVE::Rotate:
                part = TransformClipboardPartUVE::Rotation;
                break;
            case EditorToolSessionModeUVE::Scale:
                part = TransformClipboardPartUVE::Scale;
                break;
            }
            if (part.has_value()) {
                const std::string menuId = std::string(id) + "-menu";
                DrawInspectorTransformPartMenuUVE(*part, label, menuId.c_str());
            }
            return changed;
        };
        positionChanged = row("Position", "##local-position", position, 0.01F, EditorToolSessionModeUVE::Translate);
        rotationChanged =
            row("Rotation", "##local-rotation", rotationDisplayed, rotationDragSpeed, EditorToolSessionModeUVE::Rotate);
        scaleChanged = row("Scale", "##local-scale", scale, 0.01F, EditorToolSessionModeUVE::Scale);
        ImGui::EndTable();
    }
    if (positionChanged || rotationChanged || scaleChanged) {
        edited.localPosition = Math::Vector3UVE{position[0], position[1], position[2]};
        if (rotationChanged) {
            Math::QuaternionUVE newRotation{};
            const Math::Vector3UVE radians{rotationDisplayed[0] * angleFactors.radiansPerUnit,
                                           rotationDisplayed[1] * angleFactors.radiansPerUnit,
                                           rotationDisplayed[2] * angleFactors.radiansPerUnit};
            if (Math::TryMakeEulerUVE(radians, newRotation)) {
                edited.localRotation = newRotation;
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

bool EditorUVE::SetInspectorAngleDisplayUVE(const EditorAngleDisplayUVE mode) {
    switch (mode) {
    case EditorAngleDisplayUVE::Degrees:
    case EditorAngleDisplayUVE::Radians:
        break;
    default:
        return false; // not one of ours: a casted-out value, refused rather than stored
    }
    const EditorAngleDisplayUVE previous = m_inspectorAngleDisplay;
    if (previous == mode) {
        return false;
    }
    m_inspectorAngleDisplay = mode;
    NotifyEditorSettingChangedUVE(EditorSettingIdUVE::kInspectorAngleDisplayUVE,
                                  static_cast<std::int64_t>(previous), static_cast<std::int64_t>(mode));
    return true;
}

bool EditorUVE::SetInspectorFloatPrecisionUVE(const int precision) {
    if (precision < kInspectorFloatPrecisionMinUVE || precision > kInspectorFloatPrecisionMaxUVE) {
        return false; // outside the 0..6 the format table spells: refused rather than stored
    }
    const int previous = m_inspectorFloatPrecision;
    if (previous == precision) {
        return false;
    }
    m_inspectorFloatPrecision = precision;
    NotifyEditorSettingChangedUVE(EditorSettingIdUVE::kInspectorFloatPrecisionUVE,
                                  static_cast<std::int64_t>(previous), static_cast<std::int64_t>(precision));
    return true;
}

bool EditorUVE::IsInspectorFoldOpenUVE(const std::string& key, const bool defaultOpen) const {
    const auto it = m_inspectorFoldOpen.find(key);
    return it != m_inspectorFoldOpen.end() ? it->second : defaultOpen;
}

bool EditorUVE::DrawInspectorFoldUVE(const char* const label, const std::string& key, const bool defaultOpen,
                                     const bool asHeader, const int flags) {
    // The editor, not Dear ImGui's per-window storage, owns the state: that storage is lost on
    // exit, and the Inspector should reopen the way it was left.
    const bool open = IsInspectorFoldOpenUVE(key, defaultOpen);
    ImGui::SetNextItemOpen(open, ImGuiCond_Always);
    const auto treeFlags = static_cast<ImGuiTreeNodeFlags>(flags);
    const bool nowOpen = asHeader ? ImGui::CollapsingHeader(label, treeFlags) : ImGui::TreeNodeEx(label, treeFlags);
    if (nowOpen != open) {
        SetInspectorFoldOpenUVE(key, nowOpen);
    }
    return nowOpen;
}

} // namespace UVE::Editor
