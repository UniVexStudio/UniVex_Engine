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
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "editor_chrome_layout_uve.h"
#include "editor_fonts_uve.h"
#include "editor_entity_label_uve.h"
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
    ImGui::TextDisabled("%s", EntityLabelUVE(m_selectedEntity).c_str());
    std::array<char, 128> inspectorFilterBuffer{};
    m_inspectorFilter.copy(inspectorFilterBuffer.data(), inspectorFilterBuffer.size() - 1U);
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputTextWithHint("##inspector-filter", "Search properties...",
                                inspectorFilterBuffer.data(), inspectorFilterBuffer.size())) {
        m_inspectorFilter = inspectorFilterBuffer.data();
    }
    if (m_inspectorFilter.empty()) {
        m_inspectorDrawerRegistry.DrawEligibleUVE(m_selectedEntity);
    } else {
        m_inspectorDrawerRegistry.DrawEligibleMatchingUVE(m_selectedEntity, m_inspectorFilter);
    }
    DrawSceneComponentAddPanelUVE();
    if (!m_services->GetEntityManagerUVE().HasComponentUVE<Scene::TransformComponentUVE>(m_selectedEntity)) {
        ImGui::TextUnformatted("No local Transform component.");
    }
    ImGui::EndDisabled();
}

void EditorUVE::RegisterBuiltInInspectorDrawersUVE() {
    static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
        "name",
        [this](const Scene::EntityUVE entity) { return IsDocumentEntityUVE(entity); },
        [this](const Scene::EntityUVE entity) { DrawNameInspectorDrawerUVE(entity); },
    }));
    static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
        "hierarchy",
        [this](const Scene::EntityUVE entity) { return IsDocumentEntityUVE(entity); },
        [this](const Scene::EntityUVE entity) { DrawHierarchyInspectorDrawerUVE(entity); },
    }));
    static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
        "transform",
        [this](const Scene::EntityUVE entity) {
            return IsDocumentEntityUVE(entity) &&
                   m_services->GetEntityManagerUVE().HasComponentUVE<Scene::TransformComponentUVE>(entity);
        },
        [this](const Scene::EntityUVE entity) { DrawTransformInspectorDrawerUVE(entity); },
    }));
    static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
        "primitive-mesh",
        [this](const Scene::EntityUVE entity) {
            return IsDocumentEntityUVE(entity) &&
                   m_services->GetEntityManagerUVE().HasComponentUVE<Scene::TransformComponentUVE>(entity) &&
                   m_services->GetEntityManagerUVE().HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity);
        },
        [this](const Scene::EntityUVE entity) { DrawPrimitiveMeshInspectorDrawerUVE(entity); },
    }));
    const auto registerComponentDrawer = [this](const char* const id, const EditorSceneComponentKindUVE kind) {
        static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
            id,
            [this, kind](const Scene::EntityUVE entity) {
                if (!IsDocumentEntityUVE(entity)) {
                    return false;
                }
                const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
                switch (kind) {
                    case EditorSceneComponentKindUVE::Camera:
                        return entityManager.HasComponentUVE<Scene::CameraComponentUVE>(entity);
                    case EditorSceneComponentKindUVE::Mesh:
                        return entityManager.HasComponentUVE<Scene::MeshComponentUVE>(entity);
                    case EditorSceneComponentKindUVE::Light:
                        return entityManager.HasComponentUVE<Scene::LightComponentUVE>(entity);
                    case EditorSceneComponentKindUVE::Collider:
                        return entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(entity);
                    case EditorSceneComponentKindUVE::RigidBody:
                        return entityManager.HasComponentUVE<Scene::RigidBodyComponentUVE>(entity);
                    case EditorSceneComponentKindUVE::AudioSource:
                        return entityManager.HasComponentUVE<Scene::AudioSourceComponentUVE>(entity);
                    case EditorSceneComponentKindUVE::ParticleEmitter:
                        return entityManager.HasComponentUVE<Scene::ParticleEmitterComponentUVE>(entity);
                    case EditorSceneComponentKindUVE::Script:
                        return entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity);
                    case EditorSceneComponentKindUVE::AnimationPlayer:
                        return entityManager.HasComponentUVE<Scene::AnimationPlayerComponentUVE>(entity);
                    case EditorSceneComponentKindUVE::WorldEnvironment:
                        return entityManager.HasComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(entity);
                    case EditorSceneComponentKindUVE::CharacterController:
                        return entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(entity);
                    case EditorSceneComponentKindUVE::Canvas:
                        return entityManager.HasComponentUVE<Scene::CanvasComponentUVE>(entity);
                    case EditorSceneComponentKindUVE::UIText:
                        return entityManager.HasComponentUVE<Scene::UITextComponentUVE>(entity);
                    case EditorSceneComponentKindUVE::UIImage:
                        return entityManager.HasComponentUVE<Scene::UIImageComponentUVE>(entity);
                    case EditorSceneComponentKindUVE::UIButton:
                        return entityManager.HasComponentUVE<Scene::UIButtonComponentUVE>(entity);
                }
                return false;
            },
            [this, kind](const Scene::EntityUVE entity) { DrawSceneComponentInspectorDrawerUVE(entity, kind); },
        }));
    };
    registerComponentDrawer("camera", EditorSceneComponentKindUVE::Camera);
    registerComponentDrawer("mesh", EditorSceneComponentKindUVE::Mesh);
    registerComponentDrawer("light", EditorSceneComponentKindUVE::Light);
    registerComponentDrawer("collider", EditorSceneComponentKindUVE::Collider);
    registerComponentDrawer("rigid-body", EditorSceneComponentKindUVE::RigidBody);
    registerComponentDrawer("audio-source", EditorSceneComponentKindUVE::AudioSource);
    registerComponentDrawer("particle-emitter", EditorSceneComponentKindUVE::ParticleEmitter);
    registerComponentDrawer("script", EditorSceneComponentKindUVE::Script);
    registerComponentDrawer("animation-player", EditorSceneComponentKindUVE::AnimationPlayer);
    static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
        "world-environment",
        [this](const Scene::EntityUVE entity) {
            return IsDocumentEntityUVE(entity) &&
                   m_services->GetEntityManagerUVE().HasComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(entity);
        },
        [this](const Scene::EntityUVE entity) { DrawWorldEnvironmentInspectorDrawerUVE(entity); },
    }));
    static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
        "character-controller",
        [this](const Scene::EntityUVE entity) {
            return IsDocumentEntityUVE(entity) &&
                   m_services->GetEntityManagerUVE().HasComponentUVE<Scene::CharacterControllerComponentUVE>(entity);
        },
        [this](const Scene::EntityUVE entity) { DrawCharacterControllerInspectorDrawerUVE(entity); },
    }));
    static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
        "canvas",
        [this](const Scene::EntityUVE entity) {
            return IsDocumentEntityUVE(entity) &&
                   m_services->GetEntityManagerUVE().HasComponentUVE<Scene::CanvasComponentUVE>(entity);
        },
        [this](const Scene::EntityUVE entity) { DrawCanvasInspectorDrawerUVE(entity); },
    }));
    static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
        "ui-text",
        [this](const Scene::EntityUVE entity) {
            return IsDocumentEntityUVE(entity) &&
                   m_services->GetEntityManagerUVE().HasComponentUVE<Scene::UITextComponentUVE>(entity);
        },
        [this](const Scene::EntityUVE entity) { DrawUITextInspectorDrawerUVE(entity); },
    }));
    static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
        "ui-image",
        [this](const Scene::EntityUVE entity) {
            return IsDocumentEntityUVE(entity) &&
                   m_services->GetEntityManagerUVE().HasComponentUVE<Scene::UIImageComponentUVE>(entity);
        },
        [this](const Scene::EntityUVE entity) { DrawUIImageInspectorDrawerUVE(entity); },
    }));
    static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
        "ui-button",
        [this](const Scene::EntityUVE entity) {
            return IsDocumentEntityUVE(entity) &&
                   m_services->GetEntityManagerUVE().HasComponentUVE<Scene::UIButtonComponentUVE>(entity);
        },
        [this](const Scene::EntityUVE entity) { DrawUIButtonInspectorDrawerUVE(entity); },
    }));
    static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
        "prefab-instance",
        [this](const Scene::EntityUVE entity) {
            return IsDocumentEntityUVE(entity) &&
                   m_services->GetEntityManagerUVE().HasComponentUVE<Scene::PrefabInstanceComponentUVE>(entity);
        },
        [this](const Scene::EntityUVE entity) { DrawPrefabInspectorDrawerUVE(entity); },
    }));
}

void EditorUVE::DrawNameInspectorDrawerUVE(const Scene::EntityUVE entity) {
    if (!IsDocumentEntityUVE(entity)) {
        return;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    DrawNativeIconLabelUVE(0U, "Name");
    std::array<char, kMaximumEntityNameBytesUVE + 1U> nameBuffer{};
    if (entityManager.HasComponentUVE<Scene::NameComponentUVE>(entity)) {
        const std::string& currentName = entityManager.GetComponentUVE<Scene::NameComponentUVE>(entity).name;
        currentName.copy(nameBuffer.data(), std::min(currentName.size(), nameBuffer.size() - 1U));
    }
    if (ImGui::InputText("##name", nameBuffer.data(), nameBuffer.size())) {
        static_cast<void>(SetSelectedEntityNameUVE(nameBuffer.data()));
    }
}

void EditorUVE::DrawHierarchyInspectorDrawerUVE(const Scene::EntityUVE entity) {
    if (!IsDocumentEntityUVE(entity) || entity != m_selectedEntity) {
        return;
    }

    ImGui::Separator();
    DrawNativeIconLabelUVE(0U, "Hierarchy");
    Scene::EntityUVE currentParent = Scene::kInvalidEntityUVE;
    if (!TryGetDocumentParentUVE(entity, currentParent)) {
        ImGui::TextDisabled("Parent unavailable due to invalid hierarchy state.");
        return;
    }

    if (currentParent == Scene::kInvalidEntityUVE) {
        ImGui::TextDisabled("Parent: Root");
    } else {
        ImGui::Text("Parent: %s", GetHierarchyCandidateLabelUVE(currentParent).c_str());
    }

    const std::vector<Scene::EntityUVE> ancestry = GetDocumentAncestryUVE(entity);
    if (!ancestry.empty()) {
        ImGui::TextDisabled("Ancestry (read-only)");
        for (const Scene::EntityUVE ancestor : ancestry) {
            ImGui::BulletText("%s", GetHierarchyCandidateLabelUVE(ancestor).c_str());
        }
    }

    const bool canReparent = IsLifecycleCommandAllowedUVE();
    ImGui::BeginDisabled(!canReparent);
    int reparentModeIndex =
        m_reparentTransformMode == EditorReparentTransformModeUVE::KeepWorld ? 1 : 0;
    constexpr const char* kReparentModes[] = {"Keep Local", "Keep World"};
    ImGui::TextUnformatted("Reparent Transform");
    if (ImGui::Combo("##reparent-transform", &reparentModeIndex, kReparentModes,
                     static_cast<int>(std::size(kReparentModes)))) {
        const EditorReparentTransformModeUVE requestedMode = reparentModeIndex == 1
                                                                  ? EditorReparentTransformModeUVE::KeepWorld
                                                                  : EditorReparentTransformModeUVE::KeepLocal;
        static_cast<void>(SetReparentTransformModeUVE(requestedMode));
    }

    const std::string parentPreview = currentParent == Scene::kInvalidEntityUVE
                                          ? "Root"
                                          : GetHierarchyCandidateLabelUVE(currentParent);
    ImGui::TextUnformatted("New Parent");
    if (ImGui::BeginCombo("##new-parent", parentPreview.c_str())) {
        for (const Scene::EntityUVE candidate : GetEligibleReparentParentsUVE(entity)) {
            const bool isCurrentParent = candidate == currentParent;
            ImGui::BeginDisabled(isCurrentParent);
            const std::string candidateLabel = GetHierarchyCandidateLabelUVE(candidate) + "##reparent-" +
                                               std::to_string(candidate.index) + ":" +
                                               std::to_string(candidate.generation);
            if (ImGui::Selectable(candidateLabel.c_str(), false) && !isCurrentParent) {
                static_cast<void>(ReparentSelectedEntityUVE(candidate));
            }
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }

    ImGui::BeginDisabled(currentParent == Scene::kInvalidEntityUVE);
    if (ImGui::Button("Make Root")) {
        static_cast<void>(ReparentSelectedEntityUVE(Scene::kInvalidEntityUVE));
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
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
    ImGui::Separator();
    DrawProceduralIconLabelUVE(8.0F, "Transform", DrawMoveIconUVE);
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

    // Cowork's mockup uses a monospace font for numeric fields (`--font-mono`); PushFont() here
    // only around these three widgets, not the whole panel, since everything else (labels,
    // section headers) stays on the main UI font. Each group's label sits on its own line above
    // its row of boxes (matching Unity/Unreal's own Inspector convention) rather than ImGui's
    // default trailing label, which used to clip off the panel's right edge.
    if (g_monoFontUVE != nullptr) {
        ImGui::PushFont(g_monoFontUVE);
    }
    ImGui::TextUnformatted("Position");
    const bool positionChanged = ImGui::InputFloat3("##local-position", position);
    ImGui::TextUnformatted("Rotation");
    const bool rotationChanged = ImGui::InputFloat3("##local-rotation", rotationDegrees);
    ImGui::TextUnformatted("Scale");
    const bool scaleChanged = ImGui::InputFloat3("##local-scale", scale);
    if (g_monoFontUVE != nullptr) {
        ImGui::PopFont();
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

void EditorUVE::DrawPrimitiveMeshInspectorDrawerUVE(const Scene::EntityUVE entity) {
    if (!IsDocumentEntityUVE(entity)) {
        return;
    }

    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity)) {
        return;
    }

    ImGui::Separator();
    DrawProceduralIconLabelUVE(8.0F, "Primitive", DrawNodeMeshIconUVE);
    const Scene::PrimitiveMeshComponentUVE current =
        entityManager.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity);
    int kindIndex = static_cast<int>(current.kind);
    // Combo labels sourced from the primitive node definitions' own default names — index order
    // matches PrimitiveMeshKindUVE (Cube, UVSphere, Plane). The string literals backing
    // defaultName are null-terminated, so data() is safe for ImGui's const char* array API.
    constexpr const char* kPrimitiveKinds[] = {Scene::BoxMesh3DNodeDefinitionUVE::defaultName.data(),
                                                Scene::SphereMesh3DNodeDefinitionUVE::defaultName.data(),
                                                Scene::PlaneMesh3DNodeDefinitionUVE::defaultName.data()};
    ImGui::TextUnformatted("Primitive Kind");
    const bool kindChanged = ImGui::Combo("##primitive-kind", &kindIndex, kPrimitiveKinds,
                                          static_cast<int>(std::size(kPrimitiveKinds)));
    float baseColor[3]{current.baseColor.x, current.baseColor.y, current.baseColor.z};
    ImGui::TextUnformatted("Base Color");
    const bool colorChanged =
        ImGui::ColorEdit3("##base-color", baseColor, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB);
    if (kindChanged || colorChanged) {
        Scene::PrimitiveMeshComponentUVE updated = current;
        updated.kind = static_cast<Scene::PrimitiveMeshKindUVE>(kindIndex);
        updated.baseColor = Math::Vector3UVE{baseColor[0], baseColor[1], baseColor[2]};
        static_cast<void>(SetSelectedPrimitiveMeshUVE(updated));
    }
}

void EditorUVE::DrawWorldEnvironmentInspectorDrawerUVE(const Scene::EntityUVE entity) {
    if (!IsDocumentEntityUVE(entity) || entity != m_selectedEntity) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(entity)) {
        return;
    }

    const Scene::WorldEnvironment3DNodeComponentUVE current =
        entityManager.GetComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(entity);
    Scene::WorldEnvironment3DNodeComponentUVE edited = current;
    bool changed = false;

    ImGui::Separator();
    DrawNativeIconLabelUVE(m_uiAssets.GetGeneralIconTextureIdUVE("environment"), "World Environment");
    ImGui::TextDisabled("Scene environment settings are authored on this node and persisted in the scene.");

    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::array<char, 257> skyAssetPathBuffer{};
        current.skyAssetPath.copy(skyAssetPathBuffer.data(), skyAssetPathBuffer.size() - 1U);
        ImGui::TextUnformatted("Environment Resource");
        if (ImGui::InputTextWithHint("##environment-resource", "Optional sky asset path...",
                                    skyAssetPathBuffer.data(), skyAssetPathBuffer.size())) {
            edited.skyAssetPath = skyAssetPathBuffer.data();
            changed = true;
        }
        ImGui::TextDisabled("The environment resource path is authored here; runtime sky sampling is not active yet.");
    }

    if (ImGui::CollapsingHeader("Background", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled(edited.skyAssetPath.empty() ? "Clear color background" : "Sky resource background");
        ImGui::TextDisabled("Background rendering remains scene-driven; no hidden editor light is created.");
    }

    if (ImGui::CollapsingHeader("Sky", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Asset: %s", edited.skyAssetPath.empty() ? "None" : edited.skyAssetPath.c_str());
        ImGui::TextDisabled("Sky asset assignment is persisted; runtime sky sampling is not active yet.");
    }

    const std::uintptr_t sunIconTextureId = m_uiAssets.GetGeneralIconTextureIdUVE("sun");
    if (sunIconTextureId != 0U) {
        ImGui::Image(static_cast<ImTextureID>(sunIconTextureId), ImVec2{16.0F, 16.0F});
        ImGui::SameLine(0.0F, 5.0F);
    }
    if (ImGui::CollapsingHeader("Ambient Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        float ambientColor[3]{edited.ambientColor.x, edited.ambientColor.y, edited.ambientColor.z};
        ImGui::TextUnformatted("Ambient Color");
        const bool colorChanged =
            ImGui::ColorEdit3("##ambient-color", ambientColor, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB);
        float ambientEnergy = edited.ambientEnergy;
        ImGui::TextUnformatted("Ambient Energy");
        const bool energyChanged = ImGui::DragFloat("##ambient-energy", &ambientEnergy, 0.05F, 0.0F, 32.0F, "%.3f");
        if (colorChanged || energyChanged) {
            edited.ambientColor = Math::Vector3UVE{ambientColor[0], ambientColor[1], ambientColor[2]};
            edited.ambientEnergy = ambientEnergy;
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Reflected Light")) {
        ImGui::TextDisabled("Reflection probes are authored separately and are not synthesized here.");
    }

    if (ImGui::CollapsingHeader("Tonemap", ImGuiTreeNodeFlags_DefaultOpen)) {
        float exposure = edited.exposure;
        ImGui::TextUnformatted("Exposure");
        const bool exposureChanged = ImGui::DragFloat("##exposure", &exposure, 0.05F, 0.001F, 32.0F, "%.3f");
        ImGui::BeginDisabled();
        ImGui::TextUnformatted("Post Processing (reserved)");
        ImGui::Checkbox("##post-processing", &edited.postProcessingEnabled);
        ImGui::EndDisabled();
        if (exposureChanged) {
            edited.exposure = exposure;
            changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Fog")) {
        ImGui::TextUnformatted("Enabled");
        const bool fogEnabledChanged = ImGui::Checkbox("##fog-enabled", &edited.fogEnabled);
        float fogColor[3]{edited.fogColor.x, edited.fogColor.y, edited.fogColor.z};
        ImGui::TextUnformatted("Fog Color");
        const bool fogColorChanged =
            ImGui::ColorEdit3("##fog-color", fogColor, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB);
        float fogDensity = edited.fogDensity;
        ImGui::TextUnformatted("Density");
        const bool fogDensityChanged = ImGui::DragFloat("##fog-density", &fogDensity, 0.001F, 0.0F, 10.0F, "%.4f");
        if (fogEnabledChanged || fogColorChanged || fogDensityChanged) {
            edited.fogColor = Math::Vector3UVE{fogColor[0], fogColor[1], fogColor[2]};
            edited.fogDensity = fogDensity;
            changed = true;
        }
    }

    if (changed && !SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::WorldEnvironment, edited)) {
        ImGui::TextDisabled("Input was rejected by the authored-value validator.");
    }
}

void EditorUVE::DrawCharacterControllerInspectorDrawerUVE(const Scene::EntityUVE entity) {
    if (!IsDocumentEntityUVE(entity) || entity != m_selectedEntity) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(entity)) {
        return;
    }

    const Scene::CharacterControllerComponentUVE current =
        entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(entity);
    Scene::CharacterControllerComponentUVE edited = current;
    bool changed = false;

    ImGui::Separator();
    DrawProceduralIconLabelUVE(8.0F, "Character Controller",
                               [this](ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32) {
                                   DrawHierarchyNodeIconUVE(drawList, center, radius, HierarchyNodeIconKindUVE::Physics,
                                                           m_uiAssets.GetGeneralIconTextureIdUVE("sun"),
                                                           m_uiAssets.GetGeneralIconTextureIdUVE("environment"));
                               });
    ImGui::TextDisabled(
        "Driven every fixed step by EngineCoreUVE's real gravity/jump/ground-state stepping - requires a "
        "Collider (and, if present, a kinematic Rigid Body).");

    float moveSpeed = edited.moveSpeed;
    ImGui::TextUnformatted("Move Speed");
    if (ImGui::DragFloat("##move-speed", &moveSpeed, 0.05F, 0.0F, 100.0F, "%.3f")) {
        edited.moveSpeed = moveSpeed;
        changed = true;
    }
    float jumpHeight = edited.jumpHeight;
    ImGui::TextUnformatted("Jump Height");
    if (ImGui::DragFloat("##jump-height", &jumpHeight, 0.02F, 0.0F, 50.0F, "%.3f")) {
        edited.jumpHeight = jumpHeight;
        changed = true;
    }
    float gravityScale = edited.gravityScale;
    ImGui::TextUnformatted("Gravity Scale");
    if (ImGui::DragFloat("##gravity-scale", &gravityScale, 0.02F, 0.0F, 10.0F, "%.3f")) {
        edited.gravityScale = gravityScale;
        changed = true;
    }
    ImGui::BeginDisabled();
    float verticalVelocity = edited.verticalVelocity;
    ImGui::TextUnformatted("Vertical Velocity (runtime)");
    ImGui::DragFloat("##vertical-velocity", &verticalVelocity, 0.0F);
    bool isGrounded = edited.isGrounded;
    ImGui::TextUnformatted("Grounded (runtime)");
    ImGui::Checkbox("##grounded", &isGrounded);
    ImGui::EndDisabled();

    if (changed && !SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::CharacterController, edited)) {
        ImGui::TextDisabled("Input was rejected by the authored-value validator.");
    }
    if (ImGui::Button("Remove Character Controller")) {
        static_cast<void>(RemoveSelectedSceneComponentUVE(EditorSceneComponentKindUVE::CharacterController));
    }
}

void EditorUVE::DrawCanvasInspectorDrawerUVE(const Scene::EntityUVE entity) {
    if (!IsDocumentEntityUVE(entity) || entity != m_selectedEntity) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::CanvasComponentUVE>(entity)) {
        return;
    }

    const Scene::CanvasComponentUVE current = entityManager.GetComponentUVE<Scene::CanvasComponentUVE>(entity);
    Scene::CanvasComponentUVE edited = current;
    bool changed = false;

    ImGui::Separator();
    DrawProceduralIconLabelUVE(8.0F, "Canvas", DrawNodeEmptyIconUVE);
    ImGui::TextDisabled(
        "A screen-space UI root - UIText/UIImage/UIButton entities render in window pixel "
        "coordinates regardless of Canvas nesting; world-space canvases are not supported yet.");

    bool visible = edited.visible;
    ImGui::TextUnformatted("Visible");
    if (ImGui::Checkbox("##visible", &visible)) {
        edited.visible = visible;
        changed = true;
    }
    int sortOrder = edited.sortOrder;
    ImGui::TextUnformatted("Sort Order");
    if (ImGui::DragInt("##sort-order", &sortOrder, 1.0F, -1000, 1000)) {
        edited.sortOrder = sortOrder;
        changed = true;
    }

    if (changed && !SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Canvas, edited)) {
        ImGui::TextDisabled("Input was rejected by the authored-value validator.");
    }
    if (ImGui::Button("Remove Canvas")) {
        static_cast<void>(RemoveSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Canvas));
    }
}

void EditorUVE::DrawUITextInspectorDrawerUVE(const Scene::EntityUVE entity) {
    if (!IsDocumentEntityUVE(entity) || entity != m_selectedEntity) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::UITextComponentUVE>(entity)) {
        return;
    }

    const Scene::UITextComponentUVE current = entityManager.GetComponentUVE<Scene::UITextComponentUVE>(entity);
    Scene::UITextComponentUVE edited = current;
    bool changed = false;

    // UIText/UIImage/UIButton can all be attached to the same entity at once and share several
    // field names (Position/Size/Alpha) - PushID scopes this drawer's widget IDs so they can never
    // collide with a sibling UI drawer's identically-named fields in the same Inspector frame.
    ImGui::PushID("ui-text-inspector");
    ImGui::Separator();
    DrawProceduralIconLabelUVE(8.0F, "UI Text", DrawNodeEmptyIconUVE);

    std::array<char, Scene::kMaximumUITextBytesUVE + 1U> textBuffer{};
    current.text.copy(textBuffer.data(), std::min(current.text.size(), textBuffer.size() - 1U));
    ImGui::TextUnformatted("Text");
    if (ImGui::InputText("##text", textBuffer.data(), textBuffer.size())) {
        edited.text = textBuffer.data();
        changed = true;
    }
    float position[2]{edited.positionPixels.x, edited.positionPixels.y};
    ImGui::TextUnformatted("Position");
    if (ImGui::DragFloat2("##position", position, 1.0F)) {
        edited.positionPixels = Math::Vector2UVE{position[0], position[1]};
        changed = true;
    }
    float fontSize = edited.fontSize;
    ImGui::TextUnformatted("Font Size");
    if (ImGui::DragFloat("##font-size", &fontSize, 0.5F, Scene::kMinimumUIFontSizeUVE, Scene::kMaximumUIFontSizeUVE, "%.1f")) {
        edited.fontSize = fontSize;
        changed = true;
    }
    float color[3]{edited.color.x, edited.color.y, edited.color.z};
    ImGui::TextUnformatted("Color");
    if (ImGui::ColorEdit3("##color", color, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB)) {
        edited.color = Math::Vector3UVE{color[0], color[1], color[2]};
        changed = true;
    }
    float alpha = edited.alpha;
    ImGui::TextUnformatted("Alpha");
    if (ImGui::DragFloat("##alpha", &alpha, 0.01F, 0.0F, 1.0F, "%.3f")) {
        edited.alpha = alpha;
        changed = true;
    }

    if (changed && !SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::UIText, edited)) {
        ImGui::TextDisabled("Input was rejected by the authored-value validator.");
    }
    if (ImGui::Button("Remove UI Text")) {
        static_cast<void>(RemoveSelectedSceneComponentUVE(EditorSceneComponentKindUVE::UIText));
    }
    ImGui::PopID();
}

void EditorUVE::DrawUIImageInspectorDrawerUVE(const Scene::EntityUVE entity) {
    if (!IsDocumentEntityUVE(entity) || entity != m_selectedEntity) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::UIImageComponentUVE>(entity)) {
        return;
    }

    const Scene::UIImageComponentUVE current = entityManager.GetComponentUVE<Scene::UIImageComponentUVE>(entity);
    Scene::UIImageComponentUVE edited = current;
    bool changed = false;

    // See DrawUITextInspectorDrawerUVE's own comment - PushID prevents this drawer's field IDs
    // (Position/Size/Alpha) from colliding with a sibling UI drawer's identically-named ones.
    ImGui::PushID("ui-image-inspector");
    ImGui::Separator();
    DrawProceduralIconLabelUVE(8.0F, "UI Image", DrawNodeMeshIconUVE);
    ImGui::TextDisabled("An unset (zero) Texture Asset GUID renders as a flat tint-colored quad.");

    std::uint64_t guidValue = edited.textureAssetGuid.value;
    ImGui::TextUnformatted("Texture Asset GUID");
    if (ImGui::InputScalar("##texture-asset-guid", ImGuiDataType_U64, &guidValue)) {
        edited.textureAssetGuid = Asset::AssetGuidUVE{guidValue};
        changed = true;
    }
    float position[2]{edited.positionPixels.x, edited.positionPixels.y};
    ImGui::TextUnformatted("Position");
    if (ImGui::DragFloat2("##position", position, 1.0F)) {
        edited.positionPixels = Math::Vector2UVE{position[0], position[1]};
        changed = true;
    }
    float size[2]{edited.sizePixels.x, edited.sizePixels.y};
    ImGui::TextUnformatted("Size");
    if (ImGui::DragFloat2("##size", size, 1.0F, Scene::kMinimumUIImageSizePixelsUVE,
                         Scene::kMaximumUIImageSizePixelsUVE)) {
        edited.sizePixels = Math::Vector2UVE{size[0], size[1]};
        changed = true;
    }
    float tintColor[3]{edited.tintColor.x, edited.tintColor.y, edited.tintColor.z};
    ImGui::TextUnformatted("Tint Color");
    if (ImGui::ColorEdit3("##tint-color", tintColor, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB)) {
        edited.tintColor = Math::Vector3UVE{tintColor[0], tintColor[1], tintColor[2]};
        changed = true;
    }
    float alpha = edited.alpha;
    ImGui::TextUnformatted("Alpha");
    if (ImGui::DragFloat("##alpha", &alpha, 0.01F, 0.0F, 1.0F, "%.3f")) {
        edited.alpha = alpha;
        changed = true;
    }

    if (changed && !SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::UIImage, edited)) {
        ImGui::TextDisabled("Input was rejected by the authored-value validator.");
    }
    if (ImGui::Button("Remove UI Image")) {
        static_cast<void>(RemoveSelectedSceneComponentUVE(EditorSceneComponentKindUVE::UIImage));
    }
    ImGui::PopID();
}

void EditorUVE::DrawUIButtonInspectorDrawerUVE(const Scene::EntityUVE entity) {
    if (!IsDocumentEntityUVE(entity) || entity != m_selectedEntity) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::UIButtonComponentUVE>(entity)) {
        return;
    }

    const Scene::UIButtonComponentUVE current = entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(entity);
    Scene::UIButtonComponentUVE edited = current;
    bool changed = false;

    // See DrawUITextInspectorDrawerUVE's own comment - PushID prevents this drawer's field IDs
    // (Position/Size) from colliding with a sibling UI drawer's identically-named ones.
    ImGui::PushID("ui-button-inspector");
    ImGui::Separator();
    DrawProceduralIconLabelUVE(8.0F, "UI Button", DrawNodeMeshIconUVE);
    ImGui::TextDisabled("Hit-tested every tick by UIRuntimeUVE against the real mouse position/button state.");

    float position[2]{edited.positionPixels.x, edited.positionPixels.y};
    ImGui::TextUnformatted("Position");
    if (ImGui::DragFloat2("##position", position, 1.0F)) {
        edited.positionPixels = Math::Vector2UVE{position[0], position[1]};
        changed = true;
    }
    float size[2]{edited.sizePixels.x, edited.sizePixels.y};
    ImGui::TextUnformatted("Size");
    if (ImGui::DragFloat2("##size", size, 1.0F, Scene::kMinimumUIButtonSizePixelsUVE,
                         Scene::kMaximumUIButtonSizePixelsUVE)) {
        edited.sizePixels = Math::Vector2UVE{size[0], size[1]};
        changed = true;
    }
    float normalColor[3]{edited.normalColor.x, edited.normalColor.y, edited.normalColor.z};
    ImGui::TextUnformatted("Normal Color");
    if (ImGui::ColorEdit3("##normal-color", normalColor, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB)) {
        edited.normalColor = Math::Vector3UVE{normalColor[0], normalColor[1], normalColor[2]};
        changed = true;
    }
    float hoverColor[3]{edited.hoverColor.x, edited.hoverColor.y, edited.hoverColor.z};
    ImGui::TextUnformatted("Hover Color");
    if (ImGui::ColorEdit3("##hover-color", hoverColor, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB)) {
        edited.hoverColor = Math::Vector3UVE{hoverColor[0], hoverColor[1], hoverColor[2]};
        changed = true;
    }
    float pressedColor[3]{edited.pressedColor.x, edited.pressedColor.y, edited.pressedColor.z};
    ImGui::TextUnformatted("Pressed Color");
    if (ImGui::ColorEdit3("##pressed-color", pressedColor, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB)) {
        edited.pressedColor = Math::Vector3UVE{pressedColor[0], pressedColor[1], pressedColor[2]};
        changed = true;
    }
    ImGui::BeginDisabled();
    bool isHovered = edited.isHovered;
    ImGui::TextUnformatted("Hovered (runtime)");
    ImGui::Checkbox("##hovered", &isHovered);
    bool wasClicked = edited.wasClickedThisFrame;
    ImGui::TextUnformatted("Clicked This Frame (runtime)");
    ImGui::Checkbox("##clicked-this-frame", &wasClicked);
    ImGui::EndDisabled();

    if (changed && !SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::UIButton, edited)) {
        ImGui::TextDisabled("Input was rejected by the authored-value validator.");
    }
    if (ImGui::Button("Remove UI Button")) {
        static_cast<void>(RemoveSelectedSceneComponentUVE(EditorSceneComponentKindUVE::UIButton));
    }
    ImGui::PopID();
}

void EditorUVE::DrawSceneComponentInspectorDrawerUVE(const Scene::EntityUVE entity,
                                                        const EditorSceneComponentKindUVE kind) {
    if (!IsDocumentEntityUVE(entity) || entity != m_selectedEntity) {
        return;
    }

    const char* title = "Component";
    switch (kind) {
        case EditorSceneComponentKindUVE::Camera: title = "Camera"; break;
        case EditorSceneComponentKindUVE::Mesh: title = "Mesh"; break;
        case EditorSceneComponentKindUVE::Light: title = "Light"; break;
        case EditorSceneComponentKindUVE::Collider: title = "Collider"; break;
        case EditorSceneComponentKindUVE::RigidBody: title = "Rigid Body"; break;
        case EditorSceneComponentKindUVE::AudioSource: title = "Audio Source"; break;
        case EditorSceneComponentKindUVE::ParticleEmitter: title = "Particle Emitter"; break;
        case EditorSceneComponentKindUVE::Script: title = "Script"; break;
        case EditorSceneComponentKindUVE::AnimationPlayer: title = "Animation Player"; break;
        case EditorSceneComponentKindUVE::WorldEnvironment: title = "World Environment"; break;
        case EditorSceneComponentKindUVE::CharacterController: title = "Character Controller"; break;
        case EditorSceneComponentKindUVE::Canvas: title = "Canvas"; break;
        case EditorSceneComponentKindUVE::UIText: title = "UI Text"; break;
        case EditorSceneComponentKindUVE::UIImage: title = "UI Image"; break;
        case EditorSceneComponentKindUVE::UIButton: title = "UI Button"; break;
    }
    ImGui::Separator();
    DrawProceduralIconLabelUVE(8.0F, title, [this, kind](ImDrawList& drawList, const ImVec2 center,
                                                         const float radius, const ImU32) {
        DrawHierarchyNodeIconUVE(drawList, center, radius, ClassifySceneComponentKindIconUVE(kind),
                                 m_uiAssets.GetGeneralIconTextureIdUVE("sun"),
                                 m_uiAssets.GetGeneralIconTextureIdUVE("environment"));
    });
    ImGui::TextDisabled("Authored component state is validated and persisted by EditorUVE.");
    if (ImGui::Button((std::string("Remove ") + title).c_str())) {
        static_cast<void>(RemoveSelectedSceneComponentUVE(kind));
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

void EditorUVE::DrawSceneComponentAddPanelUVE() {
    if (!IsDocumentEntityUVE(m_selectedEntity)) {
        return;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!ImGui::CollapsingHeader("Add Component", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    // This engine has no stored per-entity "node type" - every entity is a bare ECS bag of
    // components, classified only by whichever components it currently has. Rather than list all
    // ~15 components unconditionally on every entity, gate by what's already attached: once an
    // entity has committed to being a UI node (Canvas/UIText/UIImage/UIButton) or a 3D node (any
    // of the rest below), the other family's not-yet-attached rows are hidden - a freshly created
    // Empty entity with neither yet shows everything until it picks a direction. Script is a
    // generic behavior hook that applies to either kind, so it is never gated. An already-attached
    // component's row always stays visible regardless of category, so the user can still see/
    // remove it.
    const bool hasAnyUIComponent = entityManager.HasComponentUVE<Scene::CanvasComponentUVE>(m_selectedEntity) ||
                                   entityManager.HasComponentUVE<Scene::UITextComponentUVE>(m_selectedEntity) ||
                                   entityManager.HasComponentUVE<Scene::UIImageComponentUVE>(m_selectedEntity) ||
                                   entityManager.HasComponentUVE<Scene::UIButtonComponentUVE>(m_selectedEntity);
    const bool hasAny3DComponent =
        entityManager.HasComponentUVE<Scene::CameraComponentUVE>(m_selectedEntity) ||
        entityManager.HasComponentUVE<Scene::MeshComponentUVE>(m_selectedEntity) ||
        entityManager.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(m_selectedEntity) ||
        entityManager.HasComponentUVE<Scene::LightComponentUVE>(m_selectedEntity) ||
        entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(m_selectedEntity) ||
        entityManager.HasComponentUVE<Scene::RigidBodyComponentUVE>(m_selectedEntity) ||
        entityManager.HasComponentUVE<Scene::AudioSourceComponentUVE>(m_selectedEntity) ||
        entityManager.HasComponentUVE<Scene::ParticleEmitterComponentUVE>(m_selectedEntity) ||
        entityManager.HasComponentUVE<Scene::AnimationPlayerComponentUVE>(m_selectedEntity) ||
        entityManager.HasComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(m_selectedEntity) ||
        entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(m_selectedEntity);

    enum class ComponentCategoryUVE { Neutral, UI, ThreeD };
    const auto shouldOfferRowUVE = [hasAnyUIComponent, hasAny3DComponent](const bool present,
                                                                          const ComponentCategoryUVE category) {
        if (present) {
            return true;
        }
        if (category == ComponentCategoryUVE::UI) {
            return !hasAny3DComponent;
        }
        if (category == ComponentCategoryUVE::ThreeD) {
            return !hasAnyUIComponent;
        }
        return true;
    };

    if (ImGui::BeginTable("##component-grid", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 70.0F);
        const auto addIfMissing = [this, &entityManager](const char* label, const EditorSceneComponentKindUVE kind,
                                                           const EditorSceneComponentValueUVE& value, const bool present) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::BeginDisabled(present);
            if (ImGui::SmallButton(label) && !present) {
                static_cast<void>(SetSelectedSceneComponentUVE(kind, value));
            }
            ImGui::EndDisabled();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled(present ? "Attached" : "Available");
        };

        const bool hasCamera = entityManager.HasComponentUVE<Scene::CameraComponentUVE>(m_selectedEntity);
        if (shouldOfferRowUVE(hasCamera, ComponentCategoryUVE::ThreeD)) {
            addIfMissing("Camera", EditorSceneComponentKindUVE::Camera, Scene::CameraComponentUVE{}, hasCamera);
        }
        const bool hasMesh = entityManager.HasComponentUVE<Scene::MeshComponentUVE>(m_selectedEntity);
        if (shouldOfferRowUVE(hasMesh, ComponentCategoryUVE::ThreeD)) {
            addIfMissing("Mesh", EditorSceneComponentKindUVE::Mesh, Scene::MeshComponentUVE{}, hasMesh);
        }
        const bool hasLight = entityManager.HasComponentUVE<Scene::LightComponentUVE>(m_selectedEntity);
        if (shouldOfferRowUVE(hasLight, ComponentCategoryUVE::ThreeD)) {
            addIfMissing("Light", EditorSceneComponentKindUVE::Light, Scene::LightComponentUVE{}, hasLight);
        }
        const bool hasCollider = entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(m_selectedEntity);
        if (shouldOfferRowUVE(hasCollider, ComponentCategoryUVE::ThreeD)) {
            addIfMissing("Collider", EditorSceneComponentKindUVE::Collider, Scene::ColliderComponentUVE{}, hasCollider);
        }
        const bool hasRigidBody = entityManager.HasComponentUVE<Scene::RigidBodyComponentUVE>(m_selectedEntity);
        if (shouldOfferRowUVE(hasRigidBody, ComponentCategoryUVE::ThreeD)) {
            addIfMissing("Rigid Body", EditorSceneComponentKindUVE::RigidBody, Scene::RigidBodyComponentUVE{},
                         hasRigidBody);
        }
        const bool hasAudioSource = entityManager.HasComponentUVE<Scene::AudioSourceComponentUVE>(m_selectedEntity);
        if (shouldOfferRowUVE(hasAudioSource, ComponentCategoryUVE::ThreeD)) {
            addIfMissing("Audio Source", EditorSceneComponentKindUVE::AudioSource, Scene::AudioSourceComponentUVE{},
                         hasAudioSource);
        }
        const bool hasParticleEmitter =
            entityManager.HasComponentUVE<Scene::ParticleEmitterComponentUVE>(m_selectedEntity);
        if (shouldOfferRowUVE(hasParticleEmitter, ComponentCategoryUVE::ThreeD)) {
            addIfMissing("Particle Emitter", EditorSceneComponentKindUVE::ParticleEmitter,
                         Scene::ParticleEmitterComponentUVE{}, hasParticleEmitter);
        }
        addIfMissing("Script", EditorSceneComponentKindUVE::Script, Scene::ScriptComponentUVE{},
                     entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(m_selectedEntity));
        const bool hasAnimationPlayer =
            entityManager.HasComponentUVE<Scene::AnimationPlayerComponentUVE>(m_selectedEntity);
        if (shouldOfferRowUVE(hasAnimationPlayer, ComponentCategoryUVE::ThreeD)) {
            addIfMissing("Animation Player", EditorSceneComponentKindUVE::AnimationPlayer,
                         Scene::AnimationPlayerComponentUVE{}, hasAnimationPlayer);
        }
        const bool hasWorldEnvironment =
            entityManager.HasComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(m_selectedEntity);
        if (shouldOfferRowUVE(hasWorldEnvironment, ComponentCategoryUVE::ThreeD)) {
            addIfMissing("World Environment", EditorSceneComponentKindUVE::WorldEnvironment,
                         Scene::WorldEnvironment3DNodeComponentUVE{}, hasWorldEnvironment);
        }

        // Character Controller needs its own row (not the shared addIfMissing lambda) because
        // attaching it also auto-attaches a Collider + kinematic Rigid Body if either is missing -
        // the same precondition CharacterControllerUVE::MoveUVE/MoveWithToIUVE already enforce, and
        // the same auto-attach behavior the Library's CharacterBody3D node already establishes.
        const bool hasCharacterController =
            entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(m_selectedEntity);
        if (shouldOfferRowUVE(hasCharacterController, ComponentCategoryUVE::ThreeD)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::BeginDisabled(hasCharacterController);
            if (ImGui::SmallButton("Character Controller") && !hasCharacterController) {
                if (!entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(m_selectedEntity)) {
                    entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(m_selectedEntity,
                                                                               Scene::ColliderComponentUVE{});
                }
                if (entityManager.HasComponentUVE<Scene::RigidBodyComponentUVE>(m_selectedEntity)) {
                    entityManager.GetComponentUVE<Scene::RigidBodyComponentUVE>(m_selectedEntity).isKinematic = true;
                } else {
                    Scene::RigidBodyComponentUVE body{};
                    body.isKinematic = true;
                    entityManager.AddComponentUVE<Scene::RigidBodyComponentUVE>(m_selectedEntity, body);
                }
                static_cast<void>(SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::CharacterController,
                                                                Scene::CharacterControllerComponentUVE{}));
            }
            ImGui::EndDisabled();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled(hasCharacterController ? "Attached" : "Available");
        }

        const bool hasCanvas = entityManager.HasComponentUVE<Scene::CanvasComponentUVE>(m_selectedEntity);
        if (shouldOfferRowUVE(hasCanvas, ComponentCategoryUVE::UI)) {
            addIfMissing("Canvas", EditorSceneComponentKindUVE::Canvas, Scene::CanvasComponentUVE{}, hasCanvas);
        }
        const bool hasUIText = entityManager.HasComponentUVE<Scene::UITextComponentUVE>(m_selectedEntity);
        if (shouldOfferRowUVE(hasUIText, ComponentCategoryUVE::UI)) {
            addIfMissing("UI Text", EditorSceneComponentKindUVE::UIText, Scene::UITextComponentUVE{}, hasUIText);
        }
        const bool hasUIImage = entityManager.HasComponentUVE<Scene::UIImageComponentUVE>(m_selectedEntity);
        if (shouldOfferRowUVE(hasUIImage, ComponentCategoryUVE::UI)) {
            addIfMissing("UI Image", EditorSceneComponentKindUVE::UIImage, Scene::UIImageComponentUVE{}, hasUIImage);
        }
        const bool hasUIButton = entityManager.HasComponentUVE<Scene::UIButtonComponentUVE>(m_selectedEntity);
        if (shouldOfferRowUVE(hasUIButton, ComponentCategoryUVE::UI)) {
            addIfMissing("UI Button", EditorSceneComponentKindUVE::UIButton, Scene::UIButtonComponentUVE{},
                         hasUIButton);
        }

        ImGui::EndTable();
    }
}

} // namespace UVE::Editor
