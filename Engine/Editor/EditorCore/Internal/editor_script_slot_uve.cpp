// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Scripting slot: the Inspector row that says which script an object runs, and the commands
// behind its actions - New UVScript, Open, Quick Load, Load and Clear.
//
// An object's script is one `.uvs` file, the asset its Script component names. New UVScript writes
// that file with a header naming the object and its kind and opens it in the text editor; Quick
// Load and Load point the object at a script that already exists.

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <typeindex>
#include <utility>
#include <vector>

#include <imgui.h>

#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/object/scene_folder_uve.h"
#include "uve/scene/objects/scene_object_type_uve.h"

namespace UVE::Editor {
namespace {

constexpr std::string_view kScriptFolderUVE = "scripts";
constexpr std::string_view kUVScriptExtensionUVE = ".uvs";
constexpr std::size_t kMaximumScriptStemBytesUVE = 64U;

/// A file stem from an object name: letters, digits, '-' and '_' kept, anything else an underscore,
/// runs of underscores collapsed. "Main Menu (old)" becomes "Main_Menu_old".
[[nodiscard]] std::string ScriptFileStemUVE(const std::string& name) {
    std::string stem;
    for (const char character : name) {
        const bool keep = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                          (character >= '0' && character <= '9') || character == '-' || character == '_';
        if (keep) {
            stem.push_back(character);
        } else if (!stem.empty() && stem.back() != '_') {
            stem.push_back('_');
        }
    }
    while (!stem.empty() && stem.back() == '_') {
        stem.pop_back();
    }
    if (stem.size() > kMaximumScriptStemBytesUVE) {
        stem.resize(kMaximumScriptStemBytesUVE);
    }
    return stem.empty() ? std::string{"script"} : stem;
}

/// The Script component's declared path property, through which every slot edit goes so it is one
/// undo entry exactly like any other property edit.
struct ScriptPathPropertyUVE final {
    const Core::TypeMetadataEntryUVE* entry = nullptr;
    const Core::TypeMetadataPropertyUVE* property = nullptr;
};

[[nodiscard]] ScriptPathPropertyUVE FindScriptPathPropertyUVE() {
    const Core::TypeMetadataEntryUVE* const entry =
        Scene::FindSceneComponentMetadataUVE(std::type_index(typeid(Scene::ScriptComponentUVE)));
    if (entry == nullptr) {
        return {};
    }
    const auto property = std::find_if(entry->properties.cbegin(), entry->properties.cend(),
                                       [](const Core::TypeMetadataPropertyUVE& candidate) {
                                           return candidate.name == "scriptAssetPath";
                                       });
    return property == entry->properties.cend() ? ScriptPathPropertyUVE{} : ScriptPathPropertyUVE{entry, &*property};
}

[[nodiscard]] bool IsUVScriptPathUVE(const std::string_view path) noexcept { return path.ends_with(kUVScriptExtensionUVE); }

/// A free script path for `stem`: not a file that exists, and not a path another object already
/// names (it may not have been saved yet). Bounded, so a folder full of collisions gives up
/// (empty) instead of spinning.
template <typename ExistsFn>
[[nodiscard]] std::string FindFreeScriptPathUVE(const std::string& stem, const std::vector<std::string>& taken,
                                                const std::string_view extension, const ExistsFn& exists) {
    constexpr int kMaximumAttemptsUVE = 1000;
    for (int attempt = 1; attempt <= kMaximumAttemptsUVE; ++attempt) {
        const std::string candidate = std::string{kScriptFolderUVE} + "/" + stem +
                                      (attempt == 1 ? std::string{} : "_" + std::to_string(attempt)) +
                                      std::string{extension};
        if (std::find(taken.cbegin(), taken.cend(), candidate) == taken.cend() && !exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

/// A UVScript identifier from free text: letters, digits and '_', never starting with a digit.
[[nodiscard]] std::string ScriptIdentifierUVE(const std::string_view text, const std::string_view fallback) {
    std::string identifier;
    for (const char character : text) {
        const bool keep = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                          (character >= '0' && character <= '9') || character == '_';
        if (keep) {
            identifier.push_back(character);
        }
    }
    if (identifier.empty() || (identifier.front() >= '0' && identifier.front() <= '9')) {
        identifier.insert(0, fallback);
    }
    return identifier;
}

/// A small "UVS" tag, so the slot reads as a typed reference at a glance. Returns its width.
float DrawLanguageBadgeUVE(ImDrawList& drawList, const ImVec2 position, const float height) {
    constexpr const char* label = "UVS";
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 max{position.x + textSize.x + 10.0F, position.y + height};
    drawList.AddRectFilled(position, max, IM_COL32(46, 122, 96, 255), 3.0F);
    drawList.AddText(ImVec2{position.x + 5.0F, position.y + ((height - textSize.y) * 0.5F)},
                     IM_COL32(228, 238, 250, 255), label);
    return max.x - position.x;
}

} // namespace

std::string EditorUVE::DescribeScriptAssetProblemUVE(const std::string& path) const {
    if (path.empty()) {
        return "Enter the path of a script, such as scripts/player.uvs.";
    }
    if (!Scene::IsScriptAssetPathValidUVE(path)) {
        return "Use a project-relative path: no drive, no leading '/', no '..'.";
    }
    if (!IsUVScriptPathUVE(path)) {
        return "A script is a UVScript file ending in .uvs.";
    }
    if (!ReadProjectTextFileUVE(path).has_value()) {
        return "There is no file at this path.";
    }
    // A script with errors is still assignable: the text editor shows them.
    return {};
}

std::vector<std::string> EditorUVE::GetKnownScriptAssetPathsUVE() const {
    std::vector<std::string> paths;
    m_services->GetEntityManagerUVE().ForEachUVE<Scene::ScriptComponentUVE>(
        [this, &paths](const Scene::EntityUVE entity, const Scene::ScriptComponentUVE& script) {
            if (IsDocumentEntityUVE(entity) && !script.scriptAssetPath.empty()) {
                paths.push_back(script.scriptAssetPath);
            }
        });
    // The scripts folder, resolved by the same rule scripts are written by.
    std::filesystem::path folder = m_services->GetFileSystemUVE().ResolveRealPathUVE(kScriptFolderUVE);
    if (folder.empty()) {
        folder = std::filesystem::path{kScriptFolderUVE};
    }
    std::error_code error;
    if (std::filesystem::is_directory(folder, error)) {
        for (std::filesystem::recursive_directory_iterator iterator{folder, error}, end; !error && iterator != end;
             iterator.increment(error)) {
            if (iterator->is_regular_file(error) && iterator->path().extension() == kUVScriptExtensionUVE) {
                const std::filesystem::path relative = std::filesystem::relative(iterator->path(), folder, error);
                if (!error) {
                    paths.push_back((std::filesystem::path{kScriptFolderUVE} / relative).generic_string());
                }
            }
        }
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

bool EditorUVE::AssignScriptToSelectedEntityUVE(const std::string& path) {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE() ||
        !m_services->GetEntityManagerUVE().HasComponentUVE<Scene::ScriptComponentUVE>(m_selectedEntity) ||
        (!path.empty() && !DescribeScriptAssetProblemUVE(path).empty())) {
        return false;
    }
    const ScriptPathPropertyUVE target = FindScriptPathPropertyUVE();
    return target.entry != nullptr && SetSelectedComponentPropertyUVE(*target.entry, *target.property, &path);
}

bool EditorUVE::AssignScriptToEntityUVE(const Scene::EntityUVE entity, const std::string& path) {
    if (!IsAuthoringCommandAllowedUVE() || !IsHierarchyObjectUVE(entity) || IsEntityLockedUVE(entity)) {
        return false;
    }
    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (entityManager.HasComponentUVE<Scene::FolderComponentUVE>(entity)) {
        return false;
    }
    if (path.empty()) {
        if (!entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity) ||
            entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath.empty()) {
            return false;
        }
        return SetScriptPathForEntityUVE(entity, path);
    }
    if (!CanCreateUVScriptForEntityUVE(entity) || !DescribeScriptAssetProblemUVE(path).empty()) {
        return false;
    }
    return SetScriptPathForEntityUVE(entity, path);
}

bool EditorUVE::OpenHierarchyScriptAttachDialogUVE(const Scene::EntityUVE target) {
    if (!CanCreateUVScriptForEntityUVE(target)) {
        return false;
    }
    m_hierarchyScriptAttachTarget = target;
    m_hierarchyScriptAttachPath.clear();
    m_hierarchyScriptAttachFilter.clear();
    m_hierarchyScriptAttachCandidates = GetKnownScriptAssetPathsUVE();
    m_hierarchyScriptAttachProblem = "Choose or enter an existing .uvs script path.";
    m_hierarchyScriptAttachDialogOpenRequested = true;
    return true;
}

void EditorUVE::SetHierarchyScriptAttachPathUVE(std::string path) {
    m_hierarchyScriptAttachPath = std::move(path);
    m_hierarchyScriptAttachProblem = DescribeScriptAssetProblemUVE(m_hierarchyScriptAttachPath);
}

bool EditorUVE::ConfirmHierarchyScriptAttachUVE() {
    if (!CanCreateUVScriptForEntityUVE(m_hierarchyScriptAttachTarget)) {
        m_hierarchyScriptAttachProblem = "Choose an available object without an attached script.";
        return false;
    }
    m_hierarchyScriptAttachProblem = DescribeScriptAssetProblemUVE(m_hierarchyScriptAttachPath);
    if (!m_hierarchyScriptAttachProblem.empty() ||
        !AssignScriptToEntityUVE(m_hierarchyScriptAttachTarget, m_hierarchyScriptAttachPath)) {
        if (m_hierarchyScriptAttachProblem.empty()) {
            m_hierarchyScriptAttachProblem = "The script could not be attached.";
        }
        return false;
    }
    CancelHierarchyScriptAttachUVE();
    return true;
}

void EditorUVE::CancelHierarchyScriptAttachUVE() noexcept {
    m_hierarchyScriptAttachTarget = Scene::kInvalidEntityUVE;
    m_hierarchyScriptAttachPath.clear();
    m_hierarchyScriptAttachFilter.clear();
    m_hierarchyScriptAttachProblem.clear();
    m_hierarchyScriptAttachCandidates.clear();
    m_hierarchyScriptAttachDialogOpenRequested = false;
    m_hierarchyScriptAttachDialogWasOpened = false;
}

bool EditorUVE::CreateUVScriptForSelectedEntityUVE() {
    if (!IsAuthoringCommandAllowedUVE() || !HasSingleDocumentSelectionUVE()) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(m_selectedEntity) ||
        !entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(m_selectedEntity).scriptAssetPath.empty()) {
        return false;
    }
    return CreateUVScriptForEntityUVE(m_selectedEntity);
}

bool EditorUVE::CanCreateUVScriptForEntityUVE(const Scene::EntityUVE entity) const {
    if (!IsAuthoringCommandAllowedUVE() || !IsHierarchyObjectUVE(entity) || IsEntityLockedUVE(entity)) {
        return false;
    }
    const Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (entityManager.HasComponentUVE<Scene::FolderComponentUVE>(entity)) {
        return false;
    }
    return !entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity) ||
           entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath.empty();
}

std::vector<Scene::EntityUVE> EditorUVE::GetUVScriptTargetCandidatesUVE() const {
    std::vector<Scene::EntityUVE> candidates;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    entityManager.ForEachUVE<Scene::HierarchyComponentUVE>(
        [this, &candidates](const Scene::EntityUVE entity, const Scene::HierarchyComponentUVE&) {
            if (IsHierarchyObjectUVE(entity)) {
                candidates.push_back(entity);
            }
        });
    std::sort(candidates.begin(), candidates.end(), [this](const Scene::EntityUVE lhs, const Scene::EntityUVE rhs) {
        const std::string leftName = GetEntityDisplayLabelUVE(lhs);
        const std::string rightName = GetEntityDisplayLabelUVE(rhs);
        return leftName == rightName ? lhs.index < rhs.index : leftName < rightName;
    });
    return candidates;
}

std::string EditorUVE::GetUniqueUVScriptPathUVE(const std::string_view fileName) const {
    if (fileName.empty() ||
        std::all_of(fileName.begin(), fileName.end(), [](const char character) {
            return std::isspace(static_cast<unsigned char>(character)) != 0;
        })) {
        return {};
    }
    const std::string stem = ScriptFileStemUVE(std::string{fileName});
    return FindFreeScriptPathUVE(stem, GetKnownScriptAssetPathsUVE(), kUVScriptExtensionUVE,
                                 [this](const std::string& candidate) {
                                     return ReadProjectTextFileUVE(candidate).has_value();
                                 });
}

bool EditorUVE::OpenUVScriptCreationDialogUVE(const Scene::EntityUVE defaultTarget) {
    if (!CanCreateUVScriptForEntityUVE(defaultTarget)) {
        return false;
    }
    m_uvScriptCreationTarget = defaultTarget;
    m_uvScriptCreationFileName = GetEntityDisplayLabelUVE(defaultTarget);
    m_uvScriptCreationFileNameEdited = false;
    m_uvScriptCreationPath = GetUniqueUVScriptPathUVE(m_uvScriptCreationFileName);
    m_uvScriptCreationProblem = m_uvScriptCreationPath.empty()
                                    ? "Enter a different name; no free default filename is available."
                                    : std::string{};
    m_uvScriptCreationDialogOpenRequested = true;
    return true;
}

bool EditorUVE::ChooseUVScriptCreationTargetUVE(const Scene::EntityUVE target) {
    if (!CanCreateUVScriptForEntityUVE(target)) {
        return false;
    }
    m_uvScriptCreationTarget = target;
    if (!m_uvScriptCreationFileNameEdited) {
        m_uvScriptCreationFileName = GetEntityDisplayLabelUVE(target);
    }
    m_uvScriptCreationPath = GetUniqueUVScriptPathUVE(m_uvScriptCreationFileName);
    m_uvScriptCreationProblem.clear();
    return true;
}

void EditorUVE::SetUVScriptCreationFileNameUVE(std::string fileName) {
    m_uvScriptCreationFileName = std::move(fileName);
    m_uvScriptCreationFileNameEdited = true;
    m_uvScriptCreationPath = GetUniqueUVScriptPathUVE(m_uvScriptCreationFileName);
    m_uvScriptCreationProblem.clear();
}

bool EditorUVE::ConfirmUVScriptCreationDialogUVE() {
    if (!CanCreateUVScriptForEntityUVE(m_uvScriptCreationTarget) || m_uvScriptCreationPath.empty()) {
        m_uvScriptCreationProblem = "Choose an available object and enter a valid script name.";
        return false;
    }
    if (!CreateUVScriptForEntityUVE(m_uvScriptCreationTarget, m_uvScriptCreationFileName)) {
        m_uvScriptCreationProblem = "The script could not be created or attached.";
        return false;
    }
    CancelUVScriptCreationDialogUVE();
    return true;
}

void EditorUVE::CancelUVScriptCreationDialogUVE() noexcept {
    m_uvScriptCreationTarget = Scene::kInvalidEntityUVE;
    m_uvScriptCreationFileName.clear();
    m_uvScriptCreationPath.clear();
    m_uvScriptCreationProblem.clear();
    m_uvScriptCreationFileNameEdited = false;
    m_uvScriptCreationDialogOpenRequested = false;
    m_uvScriptCreationDialogWasOpened = false;
}

bool EditorUVE::CreateUVScriptForEntityUVE(const Scene::EntityUVE entity, const std::string_view fileName) {
    if (!IsAuthoringCommandAllowedUVE() || !IsDocumentEntityUVE(entity) || IsEntityLockedUVE(entity)) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    const bool hasScriptComponent = entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity);
    if (hasScriptComponent) {
        if (!entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath.empty()) {
            return false;
        }
    } else if (!IsHierarchyObjectUVE(entity) || entityManager.HasComponentUVE<Scene::FolderComponentUVE>(entity)) {
        return false;
    }
    const ScriptPathPropertyUVE target = FindScriptPathPropertyUVE();
    if (target.entry == nullptr || target.property == nullptr || !target.entry->HasFactoryUVE() ||
        !target.property->IsAuthoringWritableUVE()) {
        return false;
    }
    const std::string label = GetEntityDisplayLabelUVE(entity);
    const std::string path = GetUniqueUVScriptPathUVE(fileName.empty() ? std::string_view{label} : fileName);
    if (path.empty() || !Scene::IsScriptAssetPathValidUVE(path)) {
        return false;
    }

    // The header names the attached object and its kind; only the file's name can be overridden.
    const Scene::Objects::SceneObjectDescriptorUVE* const descriptor =
        Scene::Objects::FindSceneObjectDescriptorUVE(Scene::ResolveSceneObjectKindUVE(entityManager, entity));
    const std::string kind = ScriptIdentifierUVE(descriptor != nullptr ? descriptor->displayName : "Object", "Object");
    const std::string text = "entity " + ScriptIdentifierUVE(label, "Object_") + " : " + kind +
                             "\n\non ready:\n    print(\"{name} is ready\")\n\non tick(dt):\n    pass\n";
    // The file first: if it cannot be written, nothing about the object has changed yet.
    if (!WriteProjectTextFileUVE(path, text) || !SetScriptPathForEntityUVE(entity, path)) {
        return false;
    }
    return OpenUVScriptForEntityUVE(entity);
}

bool EditorUVE::SetScriptPathForEntityUVE(const Scene::EntityUVE entity, const std::string& path) {
    static_cast<void>(CommitComponentPropertyPreviewUVE());
    if (!IsAuthoringCommandAllowedUVE() || !IsDocumentEntityUVE(entity) || IsEntityLockedUVE(entity)) {
        return false;
    }
    const ScriptPathPropertyUVE target = FindScriptPathPropertyUVE();
    if (target.entry == nullptr || target.property == nullptr || !target.entry->HasFactoryUVE() ||
        !target.property->IsAuthoringWritableUVE()) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.HasComponentUVE(entity, target.entry->typeIndex)) {
        Scene::ScriptComponentUVE script{};
        script.scriptAssetPath = path;
        const EditorSceneComponentValueUVE value{script};
        if (!IsSceneComponentValueValidUVE(EditorSceneComponentKindUVE::Script, value)) {
            return false;
        }
        const EditorSelectionSnapshotUVE selection = CaptureSelectionSnapshotUVE();
        const bool dirtyBefore = m_sceneDirty;
        const std::optional<EditorSceneComponentValueUVE> after{value};
        if (!ApplySceneComponentStateUVE(entity, EditorSceneComponentKindUVE::Script, after)) {
            return false;
        }
        InvalidateHierarchyFilterCacheUVE();
        m_sceneDirty = true;
        RecordHistoryUVE(SceneComponentHistoryEntryUVE{entity, EditorSceneComponentKindUVE::Script, std::nullopt,
                                                       after, selection, selection, dirtyBefore, true});
        return true;
    }
    const EditorSelectionSnapshotUVE selection = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    void* const instance = entityManager.GetComponentPointerUVE(entity, target.entry->typeIndex);
    Core::TypeInstanceUVE before = Core::TypeInstanceUVE::CloneUVE(*target.entry, instance);
    if (!before.IsValidUVE()) {
        return false;
    }
    target.property->setValue(instance, &path);
    if (target.entry->isInstanceValid != nullptr && !target.entry->isInstanceValid(instance)) {
        target.entry->assignInstance(instance, before.GetUVE());
        return false;
    }
    if (target.property->areEqual != nullptr && target.property->areEqual(before.GetUVE(), instance)) {
        return false;
    }
    Core::TypeInstanceUVE after = Core::TypeInstanceUVE::CloneUVE(*target.entry, instance);
    if (!after.IsValidUVE()) {
        target.entry->assignInstance(instance, before.GetUVE());
        return false;
    }

    // This row-targeted write keeps the exact pre-edit selection snapshot on both sides of undo and redo.
    m_sceneDirty = true;
    RecordHistoryUVE(ComponentPropertyHistoryEntryUVE{entity, target.entry, std::move(before), std::move(after),
                                                      selection, selection, dirtyBefore, true});
    return true;
}

void EditorUVE::DrawScriptSlotPropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                          const Core::TypeMetadataPropertyUVE& property, const void* const instance) {
    if (property.typeId != Scene::kPropertyTypeStringUVE || property.getValue == nullptr) {
        return;
    }
    const bool writable = property.IsAuthoringWritableUVE() && IsAuthoringCommandAllowedUVE();
    std::string path;
    property.getValue(instance, &path);
    const Scene::EntityUVE entity = m_selectedEntity;
    bool openActions = false;

    if (!ImGui::BeginTable("##script-slot", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        return;
    }
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 0.42F);
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.58F);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    static_cast<void>(DrawMetadataPropertyLabelUVE(entry, property, instance, writable));
    ImGui::TableSetColumnIndex(1);

    // The slot: a framed control reading "UVS  <empty>" or "UVS  main". Clicking it floats a small
    // action menu just under it; double-clicking a filled slot goes straight to the script.
    ImGui::BeginDisabled(!writable);
    const float height = ImGui::GetFrameHeight();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    if (ImGui::Button("##slot", ImVec2{width, height})) {
        openActions = true;
    }
    const bool doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", path.empty() ? "No script. Click to create or load one." : path.c_str());
    }
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const float badgeHeight = height - 6.0F;
    const float badgeWidth =
        DrawLanguageBadgeUVE(drawList, ImVec2{origin.x + 4.0F, origin.y + 3.0F}, badgeHeight);
    const float textX = origin.x + badgeWidth + 10.0F;
    const float textY = origin.y + ((height - ImGui::GetTextLineHeight()) * 0.5F);
    if (path.empty()) {
        drawList.AddText(ImVec2{textX, textY}, ImGui::GetColorU32(ImGuiCol_TextDisabled), "empty");
    } else {
        const std::string name = std::filesystem::path{path}.stem().string();
        drawList.AddText(ImVec2{textX, textY}, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
    }
    ImGui::EndDisabled();
    if (doubleClicked && !path.empty()) {
        static_cast<void>(OpenScriptGraphForEntityUVE(entity));
        openActions = false;
    }
    ImGui::EndTable();

    // Opened here, outside the table: a table scopes the ids inside it, and the popup must be
    // opened and drawn under the same id.
    if (openActions && writable) {
        ImGui::OpenPopup("##script-actions");
    }
    ImGui::SetNextWindowPos(ImVec2{origin.x, origin.y + height + 2.0F}, ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("##script-actions")) {
        return;
    }
    // A small floating menu, one action per row, like the Add Metadata form but compact.
    constexpr float kMenuWidth = 170.0F;
    bool closeMenu = false;
    const auto action = [kMenuWidth](const char* const label, const char* const tooltip) {
        const bool pressed = ImGui::Button(label, ImVec2{kMenuWidth, 0.0F});
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
        return pressed;
    };
    if (path.empty()) {
        if (action("New UVScript", "Create a text script (.uvs) for this object and open it.")) {
            closeMenu = CreateUVScriptForSelectedEntityUVE();
        }
    } else if (action("Open", "Open this object's script in Scripting.")) {
        closeMenu = OpenScriptGraphForEntityUVE(entity);
    }
    if (action("Quick Load", "Pick one of this project's scripts.")) {
        m_scriptQuickLoadFilter.clear();
        m_scriptQuickLoadCandidates = GetKnownScriptAssetPathsUVE();
        ImGui::OpenPopup("##script-quick-load");
    }
    if (action("Load", "Load a script by its path.")) {
        m_scriptLoadPath = path;
        m_scriptLoadCheckedPath.reset();
        ImGui::OpenPopup("Load Script##script-load");
    }
    if (!path.empty()) {
        ImGui::Separator();
        if (action("Clear", "Detach the script from this object. Undo brings it back.")) {
            closeMenu = AssignScriptToSelectedEntityUVE({});
        }
    }

    // Quick Load: every script the project knows, filtered as you type; one click assigns.
    ImGui::SetNextWindowSizeConstraints(ImVec2{260.0F, 0.0F}, ImVec2{FLT_MAX, 320.0F});
    if (ImGui::BeginPopup("##script-quick-load")) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        std::array<char, 128> filter{};
        m_scriptQuickLoadFilter.copy(filter.data(), filter.size() - 1U);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##filter", "Search scripts", filter.data(), filter.size())) {
            m_scriptQuickLoadFilter = filter.data();
        }
        std::size_t shown = 0U;
        for (const std::string& candidate : m_scriptQuickLoadCandidates) {
            const bool matches = m_scriptQuickLoadFilter.empty() ||
                                 std::search(candidate.cbegin(), candidate.cend(), m_scriptQuickLoadFilter.cbegin(),
                                             m_scriptQuickLoadFilter.cend(), [](const char left, const char right) {
                                                 return std::tolower(static_cast<unsigned char>(left)) ==
                                                        std::tolower(static_cast<unsigned char>(right));
                                             }) != candidate.cend();
            if (!matches) {
                continue;
            }
            ++shown;
            if (ImGui::Selectable(candidate.c_str(), candidate == path)) {
                closeMenu = AssignScriptToSelectedEntityUVE(candidate) || candidate == path;
                ImGui::CloseCurrentPopup();
            }
        }
        if (shown == 0U) {
            ImGui::TextDisabled(m_scriptQuickLoadFilter.empty() ? "No scripts in this project yet."
                                                                : "No script matches.");
        }
        ImGui::EndPopup();
    }

    // Load: a path typed or pasted in full, checked as it is typed so the reason a path is refused
    // is visible before Load is pressed. Enter loads, Escape cancels.
    ImGui::SetNextWindowSize(ImVec2{420.0F, 0.0F}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Load Script##script-load", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        std::array<char, Scene::kMaximumScriptAssetPathBytesUVE + 1U> buffer{};
        m_scriptLoadPath.copy(buffer.data(), buffer.size() - 1U);
        ImGui::SetNextItemWidth(400.0F);
        const bool submitted = ImGui::InputTextWithHint("##path", "scripts/player.uvs", buffer.data(),
                                                        buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        m_scriptLoadPath = buffer.data();
        if (m_scriptLoadCheckedPath != m_scriptLoadPath) {
            m_scriptLoadProblem = DescribeScriptAssetProblemUVE(m_scriptLoadPath);
            m_scriptLoadCheckedPath = m_scriptLoadPath;
        }
        const std::string& problem = m_scriptLoadProblem;
        if (problem.empty()) {
            ImGui::TextDisabled("Ready to load.");
        } else {
            ImGui::TextColored(ImVec4{0.95F, 0.62F, 0.35F, 1.0F}, "%s", problem.c_str());
        }
        ImGui::Separator();
        const bool cancel = ImGui::Button("Cancel", ImVec2{120.0F, 0.0F}) || ImGui::IsKeyPressed(ImGuiKey_Escape);
        ImGui::SameLine();
        ImGui::BeginDisabled(!problem.empty());
        const bool load = ImGui::Button("Load", ImVec2{120.0F, 0.0F}) || (submitted && problem.empty());
        ImGui::EndDisabled();
        if (load && AssignScriptToSelectedEntityUVE(m_scriptLoadPath)) {
            closeMenu = true;
            ImGui::CloseCurrentPopup();
        } else if (cancel) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (closeMenu) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace UVE::Editor
