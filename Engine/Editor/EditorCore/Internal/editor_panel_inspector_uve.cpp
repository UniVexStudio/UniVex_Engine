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

#include <algorithm>
#include <array>
#include <cfloat>
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
#include "editor_icon_registry_uve.h"
#include "editor_node_icons_uve.h"

#include "uve/asset/asset_import_queue_uve.h"
#include "uve/component/animation_player_component_uve.h"
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
#include "uve/component/rigid_body_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/world_environment_3d_uve.h"
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


} // namespace

void EditorUVE::DrawInspectorPanelUVE() {
    // A colour edit whose node is no longer the one selected - picked elsewhere while its picker
    // was open - is finished as it stands rather than left waiting for a picker nobody can see.
    if (m_componentPropertyPreview.has_value() &&
        (m_componentPropertyPreview->entity != m_selectedEntity || !HasSingleDocumentSelectionUVE())) {
        static_cast<void>(CommitComponentPropertyPreviewUVE());
    }
    if (!m_inspectorPanelVisible) {
        return;
    }
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    const EditorChromeLayoutUVE layout = ComputeEditorChromeLayoutUVE(*mainViewport, m_bottomDockVisible);
    // Always, not FirstUseEver - see DrawHierarchyPanelUVE()'s comment on the same change.
    ImGui::SetNextWindowPos(layout.inspectorPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(layout.inspectorSize, ImGuiCond_Always);
    // NoTitleBar dropped (was the only flag actually blocking dragging - dockable/draggable
    // windows need a title bar as their default drag handle) and given a real title: an internal
    // Inspector/Import/Signals tab strip already exists below via Selectable(), so the window
    // title identifies the *panel* to dock/drag by, while that internal strip still switches the
    // panel's *content* - two different, non-conflicting notions of "tab".
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    ImGui::Begin(kPanelLabelInspectorUVE, nullptr, flags);

    const auto drawRightPanelTab = [this](const char* const label, const EditorRightPanelTabUVE tab) {
        const bool active = m_activeRightPanelTab == tab;
        if (ImGui::Selectable(label, active, ImGuiSelectableFlags_DontClosePopups, ImVec2{0.0F, 0.0F})) {
            m_activeRightPanelTab = tab;
        }
        ImGui::SameLine();
    };
    drawRightPanelTab("Inspector", EditorRightPanelTabUVE::Inspector);
    drawRightPanelTab("Import", EditorRightPanelTabUVE::Import);
    const bool signalsActive = m_activeRightPanelTab == EditorRightPanelTabUVE::Signals;
    if (ImGui::Selectable("Signals", signalsActive, ImGuiSelectableFlags_DontClosePopups, ImVec2{0.0F, 0.0F})) {
        m_activeRightPanelTab = EditorRightPanelTabUVE::Signals;
    }
    ImGui::Separator();

    switch (m_activeRightPanelTab) {
        case EditorRightPanelTabUVE::Inspector:
            DrawInspectorContentUVE();
            break;
        case EditorRightPanelTabUVE::Import:
            DrawImportQueueMonitorUVE();
            break;
        case EditorRightPanelTabUVE::Signals:
            ImGui::TextUnformatted("Signals");
            ImGui::TextDisabled("Signal bindings remain unavailable until the scripting runtime is added.");
            break;
    }
    ImGui::End();
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
        ImGui::TextDisabled("Select an entity in Scene or Viewport to inspect it.");
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

    ImGui::BeginDisabled(!IsAuthoringCommandAllowedUVE());
    ImGui::Text("%s", GetEntityDisplayLabelUVE(m_selectedEntity).c_str());
    // Every Inspector is its node's recipe and nothing else: the node's own section, its bases,
    // Node3D's Transform and Visibility, then the common Node section. Nodes are renamed and
    // reparented from the Scene panel and get their parts from their recipe, so there is no name
    // field, hierarchy block, search box, Add Component or Remove here.
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
}

void EditorUVE::RepairInspectorRecipeUVE(const Scene::EntityUVE entity) {
    // A node saved before its recipe included Visibility and the Node section is given them here,
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
    Scene::EnsureCommonNodeSectionUVE(entityManager, entity);
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
    const bool transformOpen = DrawInspectorFoldUVE("Transform##transform-section", "section:transform", true, true, 0);
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

    // Label beside value, the layout every other Inspector row uses, with one axis-tagged field per
    // component sharing the value column. Each field clips its own number, so a narrow panel shows
    // fewer digits rather than digits spilling over the box.
    bool positionChanged = false;
    bool rotationChanged = false;
    bool scaleChanged = false;
    if (ImGui::BeginTable("##transform", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 0.26F);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.74F);
        const auto row = [](const char* const label, const char* const id, float* const values) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            return DrawAxisVectorInputUVE(id, values, 3, 0.0F, 0.0F, 0.0F, true);
        };
        positionChanged = row("Position", "##local-position", position);
        rotationChanged = row("Rotation", "##local-rotation", rotationDegrees);
        scaleChanged = row("Scale", "##local-scale", scale);
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
            }
        }
        edited.localScale = Math::Vector3UVE{scale[0], scale[1], scale[2]};
        static_cast<void>(SetSelectedLocalTransformUVE(edited));
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
    DrawProceduralIconLabelUVE(8.0F, "Prefab Instance", DrawFolderIconUVE);
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
