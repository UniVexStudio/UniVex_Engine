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
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

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
    // The scene root is a fixed, short Inspector: the common Node section and nothing else. It is
    // renamed from the Scene panel, it has no parent, it has no transform, and there is nothing
    // to add to it, so the search box, the Add Component panel and the id line would all be chrome
    // around six rows.
    if (IsSceneRootEntityUVE(m_selectedEntity)) {
        ImGui::Separator();
        m_inspectorDrawerRegistry.DrawEligibleUVE(m_selectedEntity);
        ImGui::EndDisabled();
        return;
    }
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
        // The root is renamed from the Scene panel; its Inspector is only the Node section.
        [this](const Scene::EntityUVE entity) { return IsDocumentEntityUVE(entity) && !IsSceneRootEntityUVE(entity); },
        [this](const Scene::EntityUVE entity) { DrawNameInspectorDrawerUVE(entity); },
    }));
    static_cast<void>(m_inspectorDrawerRegistry.RegisterDrawerUVE(InspectorDrawerEntryUVE{
        "hierarchy",
        // The root has no parent and cannot be given one.
        [this](const Scene::EntityUVE entity) { return IsDocumentEntityUVE(entity) && !IsSceneRootEntityUVE(entity); },
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
    // Everything between "transform" above and "prefab-instance" below used to be registered by
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
    // ~17 components unconditionally on every entity, gate by what's already attached: once an
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
        entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(m_selectedEntity) ||
        entityManager.HasComponentUVE<Scene::PhysicsInterpolationComponentUVE>(m_selectedEntity);

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
        const bool hasPhysicsInterpolation =
            entityManager.HasComponentUVE<Scene::PhysicsInterpolationComponentUVE>(m_selectedEntity);
        if (shouldOfferRowUVE(hasPhysicsInterpolation, ComponentCategoryUVE::ThreeD)) {
            addIfMissing("Physics Interpolation", EditorSceneComponentKindUVE::PhysicsInterpolation,
                         Scene::PhysicsInterpolationComponentUVE{}, hasPhysicsInterpolation);
        }
        // Editor Description, like Script, is a cross-cutting concern that applies to any entity
        // regardless of which family it has committed to, so it is never category-gated.
        addIfMissing("Editor Description", EditorSceneComponentKindUVE::EditorDescription,
                     Scene::EditorDescriptionComponentUVE{},
                     entityManager.HasComponentUVE<Scene::EditorDescriptionComponentUVE>(m_selectedEntity));
        // The rest of the common Node section, ungated for the same reason: when an entity runs,
        // which thread it runs on, whether its text is translated and what data is attached to it
        // apply to every kind of node, not to a family of them.
        addIfMissing("Process", EditorSceneComponentKindUVE::Process, Scene::ProcessComponentUVE{},
                     entityManager.HasComponentUVE<Scene::ProcessComponentUVE>(m_selectedEntity));
        addIfMissing("Thread Group", EditorSceneComponentKindUVE::ThreadGroup,
                     Scene::ThreadGroupComponentUVE{},
                     entityManager.HasComponentUVE<Scene::ThreadGroupComponentUVE>(m_selectedEntity));
        addIfMissing("Auto Translate", EditorSceneComponentKindUVE::AutoTranslate,
                     Scene::AutoTranslateComponentUVE{},
                     entityManager.HasComponentUVE<Scene::AutoTranslateComponentUVE>(m_selectedEntity));
        addIfMissing("Metadata", EditorSceneComponentKindUVE::NodeMetadata,
                     Scene::NodeMetadataComponentUVE{},
                     entityManager.HasComponentUVE<Scene::NodeMetadataComponentUVE>(m_selectedEntity));

        ImGui::EndTable();
    }
}

} // namespace UVE::Editor
