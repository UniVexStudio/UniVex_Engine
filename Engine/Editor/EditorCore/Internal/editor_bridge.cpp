// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_bridge.h"

#include "prefab_instance_component.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "asset_type_registry.h"
#include "mesh_component.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] std::string SourceKindLabel(const Asset::AssetImportSourceKind kind) {
    return std::string(Asset::AssetImportSourceKindLabel(kind));
}

[[nodiscard]] const std::vector<EditorBridgeCapability>& Capabilities() noexcept {
    static const std::vector<EditorBridgeCapability> capabilities{
        EditorBridgeCapability::ReadSnapshot,
        EditorBridgeCapability::SelectEntity,
        EditorBridgeCapability::ClearSelection,
        EditorBridgeCapability::SetSelectedEntityName,
        EditorBridgeCapability::CreateDocumentEntity,
        EditorBridgeCapability::Undo,
        EditorBridgeCapability::Redo,
        EditorBridgeCapability::ReadHierarchy,
        EditorBridgeCapability::ReadInspector,
        EditorBridgeCapability::ReadAssetTable,
        EditorBridgeCapability::ToggleEntitySelection,
        EditorBridgeCapability::SetHierarchyFilter,
        EditorBridgeCapability::SetAssetTableDirectory,
        EditorBridgeCapability::SetAssetTableFilter,
        EditorBridgeCapability::SetAssetTableFocus,
        EditorBridgeCapability::RefreshAssetTable,
        EditorBridgeCapability::SelectAssetTableEntry,
        EditorBridgeCapability::QueueAssetTableImport,
        EditorBridgeCapability::ReadViewportSurface,
        EditorBridgeCapability::ReadDeveloperConsole,
        EditorBridgeCapability::SubmitDeveloperConsoleCommand,
        EditorBridgeCapability::ClearDeveloperConsole,
        EditorBridgeCapability::SetDeveloperConsoleSeverityFilter,
        EditorBridgeCapability::SetDeveloperConsoleCompletionPrefix,
        EditorBridgeCapability::MoveDeveloperConsoleHistory,
        EditorBridgeCapability::SelectDataTablePreview,
    };
    return capabilities;
}

[[nodiscard]] bool IsMutationRequest(const EditorBridgeRequestKind kind) noexcept {
    return kind != EditorBridgeRequestKind::ReadSnapshot &&
           kind != EditorBridgeRequestKind::ReadViewportSurface &&
           kind != EditorBridgeRequestKind::ReadDeveloperConsole;
}

[[nodiscard]] bool IsBoundedRequestText(const std::string& value) noexcept {
    return value.size() <= kEditorBridgeMaximumPresentationTextBytes;
}

[[nodiscard]] bool IsBoundedContentPath(const std::string& value) noexcept {
    return value.size() <= kEditorBridgeMaximumContentPathBytes;
}

[[nodiscard]] std::string FormatDataTableValue(const Asset::DataTableValue& value) {
    return std::visit([](const auto& current) {
        using ValueType = std::decay_t<decltype(current)>;
        if constexpr (std::is_same_v<ValueType, bool>) {
            return std::string{current ? "true" : "false"};
        } else if constexpr (std::is_same_v<ValueType, std::int64_t>) {
            return std::to_string(current);
        } else if constexpr (std::is_same_v<ValueType, double>) {
            if (!std::isfinite(current)) {
                return std::string{"<non-finite>"};
            }
            char buffer[64]{};
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), current,
                                              std::chars_format::general, 15);
            return result.ec == std::errc{} ? std::string{buffer, result.ptr} : std::string{"<number>"};
        } else {
            return current;
        }
    }, value);
}

} // namespace

EditorBridge::EditorBridge(Editor& editor, const Asset::DataTableRegistry* dataTableRegistry)
    : m_editor(&editor), m_dataTableRegistry(dataTableRegistry) {
}

const std::vector<EditorBridgeCapability>& EditorBridge::GetCapabilities() noexcept {
    return Capabilities();
}

bool EditorBridge::SetPreviewTable(const std::string_view name) {
    if (m_dataTableRegistry == nullptr || name.size() > kEditorBridgeMaximumPresentationTextBytes) {
        return false;
    }
    if (!name.empty() && !m_dataTableRegistry->Contains(name)) {
        return false;
    }
    const std::optional<std::string> nextName = name.empty()
        ? std::nullopt
        : std::optional<std::string>{std::string{name}};
    if (m_dataTablePreviewName == nextName) {
        return true;
    }
    m_dataTablePreviewName = nextName;
    if (m_lastObservedState.has_value()) {
        SynchronizeRevision();
    }
    return true;
}

void EditorBridge::SetDataTableCatalogSnapshot(Asset::DataTableCatalogSnapshot snapshot) {
    if (m_dataTableRegistry != nullptr || m_dataTableCatalogSnapshot == snapshot) {
        return;
    }
    m_dataTableCatalogSnapshot = std::move(snapshot);
    if (m_lastObservedState.has_value()) {
        SynchronizeRevision();
    }
}

void EditorBridge::SetDataTablePreviewSnapshot(Asset::DataTableSnapshot snapshot) {
    if (m_dataTableRegistry != nullptr || m_dataTablePreviewSnapshot == snapshot) {
        return;
    }
    m_dataTablePreviewSnapshot = std::move(snapshot);
    if (m_lastObservedState.has_value()) {
        SynchronizeRevision();
    }
}


EditorBridgeSnapshot EditorBridge::GetSnapshot() {
    SynchronizeRevision();
    return BuildSnapshot();
}

EditorBridgeResponse EditorBridge::Dispatch(const EditorBridgeRequest& request) {
    SynchronizeRevision();
    if (request.protocolVersion != kEditorBridgeProtocolVersion) {
        return MakeResponse(request, false, "bridge.protocol.unsupported",
                               "The request protocol version is not supported by this editor backend.");
    }
    if (request.kind == EditorBridgeRequestKind::ReadSnapshot) {
        return MakeResponse(request, true, "bridge.snapshot.read", "Bridge-visible editor state was copied.");
    }
    if (request.kind == EditorBridgeRequestKind::ReadDeveloperConsole) {
        return MakeResponse(request, true, "bridge.developer_console.snapshot.read",
                               "The bounded developer-console snapshot was copied.");
    }
    if (m_editor->GetState() != EditorState::Running) {
        return MakeResponse(request, false, "bridge.editor.not_running",
                               "The editor is not in a running state and cannot accept bridge commands.");
    }
    if (IsMutationRequest(request.kind) && request.expectedRevision != m_revision) {
        return MakeResponse(request, false, "bridge.snapshot.stale",
                               "The request was based on an older bridge-visible editor snapshot.");
    }

    bool applied = false;
    std::string code = "bridge.command.rejected";
    std::string message = "The editor command was rejected without applying a mutation.";
    std::optional<EditorBridgeEntityRef> createdEntity;
    std::optional<std::uint64_t> responseContentImportJobId;
    switch (request.kind) {
        case EditorBridgeRequestKind::SelectEntity: {
            if (!request.entity.has_value() || !request.entity->IsValid() ||
                !m_editor->IsDocumentEntity(ToEntity(*request.entity))) {
                return MakeResponse(request, false, "bridge.entity.invalid",
                                       "The requested entity is not a live document entity.");
            }
            m_editor->SelectEntity(ToEntity(*request.entity));
            applied = true;
            code = "bridge.command.applied";
            message = "The document entity became the active selection.";
            break;
        }
        case EditorBridgeRequestKind::ClearSelection:
            m_editor->ClearSelection();
            applied = true;
            code = "bridge.command.applied";
            message = "The editor selection was cleared.";
            break;
        case EditorBridgeRequestKind::SetSelectedEntityName:
            if (!request.entityName.has_value() || !IsBoundedRequestText(*request.entityName)) {
                return MakeResponse(request, false, "bridge.request.invalid",
                                       "SetSelectedEntityName requires a bounded name payload.");
            }
            applied = m_editor->SetSelectedEntityName(*request.entityName);
            if (applied) {
                code = "bridge.command.applied";
                message = "The selected document entity name was updated through the native command path.";
            }
            break;
        case EditorBridgeRequestKind::CreateDocumentEntity: {
            if (!request.entityKind.has_value() || !IsSupportedEntityKind(*request.entityKind)) {
                return MakeResponse(request, false, "bridge.request.invalid",
                                       "CreateDocumentEntity requires one supported document entity kind.");
            }
            const Scene::Entity entity = m_editor->CreateDocumentEntity(*request.entityKind);
            if (entity != Scene::kInvalidEntity) {
                applied = true;
                createdEntity = ToBridgeEntity(entity);
                code = "bridge.command.applied";
                message = "A document entity was created through the native command path.";
            }
            break;
        }
        case EditorBridgeRequestKind::Undo:
            applied = m_editor->Undo();
            if (applied) {
                code = "bridge.command.applied";
                message = "The native editor undo command completed.";
            }
            break;
        case EditorBridgeRequestKind::Redo:
            applied = m_editor->Redo();
            if (applied) {
                code = "bridge.command.applied";
                message = "The native editor redo command completed.";
            }
            break;
        case EditorBridgeRequestKind::ToggleEntitySelection: {
            if (!request.entity.has_value() || !request.entity->IsValid() ||
                !m_editor->IsDocumentEntity(ToEntity(*request.entity))) {
                return MakeResponse(request, false, "bridge.entity.invalid",
                                       "The requested entity is not a live document entity.");
            }
            m_editor->ToggleEntitySelection(ToEntity(*request.entity));
            applied = true;
            code = "bridge.command.applied";
            message = "The document entity selection was toggled through the native selection path.";
            break;
        }
        case EditorBridgeRequestKind::SetHierarchyFilter:
            if (!request.hierarchyFilter.has_value() || !IsBoundedRequestText(*request.hierarchyFilter)) {
                return MakeResponse(request, false, "bridge.request.invalid",
                                       "SetHierarchyFilter requires a bounded filter payload.");
            }
            if (m_editor->m_hierarchyFilter != *request.hierarchyFilter) {
                m_editor->m_hierarchyFilter = *request.hierarchyFilter;
                m_editor->InvalidateHierarchyFilterCache();
                applied = true;
                code = "bridge.command.applied";
                message = "The native hierarchy filter was updated.";
            }
            break;
        case EditorBridgeRequestKind::SetAssetTableDirectory: {
            if (!request.contentDirectory.has_value() || !IsBoundedContentPath(*request.contentDirectory)) {
                return MakeResponse(request, false, "bridge.request.invalid",
                                       "SetAssetTableDirectory requires a bounded relative directory payload.");
            }
            const std::filesystem::path requestedDirectory = std::filesystem::path{*request.contentDirectory}.lexically_normal();
            if (requestedDirectory.is_absolute() || requestedDirectory.has_root_name() ||
                std::find(requestedDirectory.begin(), requestedDirectory.end(), std::filesystem::path{".."}) !=
                    requestedDirectory.end()) {
                return MakeResponse(request, false, "bridge.request.invalid",
                                       "The requested asset table directory must be a normalized relative path.");
            }
            const Asset::ProjectFileSnapshot snapshot = m_editor->m_services->GetProjectFileIndex().GetSnapshot();
            if (!m_editor->IsAssetTableDirectoryInSnapshot(snapshot, requestedDirectory)) {
                return MakeResponse(request, false, "bridge.content.directory.invalid",
                                       "The requested asset table directory is absent from the current native snapshot.");
            }
            if (m_editor->m_assetTableDirectory != requestedDirectory) {
                m_editor->m_assetTableDirectory = requestedDirectory;
                applied = true;
                code = "bridge.command.applied";
                message = "The native asset table directory was updated.";
            }
            break;
        }
        case EditorBridgeRequestKind::SetAssetTableFilter:
            if (!request.contentFilter.has_value() || !IsBoundedRequestText(*request.contentFilter)) {
                return MakeResponse(request, false, "bridge.request.invalid",
                                       "SetAssetTableFilter requires a bounded filter payload.");
            }
            if (m_editor->m_assetFilter != *request.contentFilter) {
                m_editor->m_assetFilter = *request.contentFilter;
                applied = true;
                code = "bridge.command.applied";
                message = "The native asset table filter was updated.";
            }
            break;
        case EditorBridgeRequestKind::SetAssetTableFocus: {
            if (!request.contentFocus.has_value() || !IsBoundedRequestText(*request.contentFocus)) {
                return MakeResponse(request, false, "bridge.request.invalid",
                                       "SetAssetTableFocus requires one known bounded focus identifier.");
            }
            const std::optional<Editor::AssetTableTypeFocus> focus =
                ParseAssetTableFocus(*request.contentFocus);
            if (!focus.has_value()) {
                return MakeResponse(request, false, "bridge.request.invalid",
                                       "The requested asset table focus is not supported.");
            }
            if (m_editor->m_assetTableTypeFocus != *focus) {
                m_editor->m_assetTableTypeFocus = *focus;
                applied = true;
                code = "bridge.command.applied";
                message = "The native asset table type focus was updated.";
            }
            break;
        }
        case EditorBridgeRequestKind::RefreshAssetTable: {
            Asset::IProjectFileIndex& projectFileIndex = m_editor->m_services->GetProjectFileIndex();
            Asset::IProjectChangeWatcher& projectChangeWatcher = m_editor->m_services->GetProjectChangeWatcher();
            const Asset::ProjectChangeSnapshot changesBeforeRefresh = projectChangeWatcher.GetSnapshot();
            m_editor->m_projectFileLastRefreshSucceeded =
                projectFileIndex.Refresh(m_editor->m_services->GetAssetDatabase());
            m_editor->m_projectFileSnapshotInitialized = true;
            if (!m_editor->m_projectFileLastRefreshSucceeded) {
                SynchronizeRevision();
                return MakeResponse(request, false, "bridge.content.refresh.failed",
                                       "The native project content index retained its last successful snapshot after refresh failure.");
            }
            projectChangeWatcher.AcknowledgeThrough(changesBeforeRefresh.latestSequence);
            if (changesBeforeRefresh.rescanRequired) {
                projectChangeWatcher.AcknowledgeRescan();
            }
            const Asset::ProjectFileSnapshot refreshedSnapshot = projectFileIndex.GetSnapshot();
            m_editor->ReconcileAssetTableDirectory(refreshedSnapshot);
            applied = true;
            code = "bridge.command.applied";
            message = "The native cached project content snapshot was refreshed explicitly.";
            break;
        }
        case EditorBridgeRequestKind::SelectAssetTableEntry: {
            if (!request.contentEntryPath.has_value() || !IsBoundedContentPath(*request.contentEntryPath)) {
                return MakeResponse(request, false, "bridge.request.invalid",
                                       "SelectAssetTableEntry requires a bounded relative entry path.");
            }
            const std::filesystem::path requestedPath = std::filesystem::path{*request.contentEntryPath}.lexically_normal();
            if (requestedPath.empty() || requestedPath.is_absolute() || requestedPath.has_root_name() ||
                std::find(requestedPath.begin(), requestedPath.end(), std::filesystem::path{".."}) != requestedPath.end()) {
                return MakeResponse(request, false, "bridge.request.invalid",
                                       "The requested asset table entry must be a normalized relative path.");
            }
            const Asset::ProjectFileSnapshot snapshot = m_editor->m_services->GetProjectFileIndex().GetSnapshot();
            const auto selected = std::find_if(snapshot.entries.cbegin(), snapshot.entries.cend(),
                                               [&requestedPath](const Asset::ProjectFileEntry& entry) {
                                                   return entry.relativePath == requestedPath;
                                               });
            if (selected == snapshot.entries.cend()) {
                return MakeResponse(request, false, "bridge.content.entry.invalid",
                                       "The requested cached project content entry is absent from the native snapshot.");
            }
            if (!m_editor->m_selectedProjectFile.has_value() ||
                m_editor->m_selectedProjectFile->relativePath != selected->relativePath ||
                m_editor->m_selectedProjectFile->kind != selected->kind) {
                m_editor->m_selectedProjectFile = *selected;
                if (selected->registeredAssetGuid.has_value()) {
                    m_editor->m_selectedAsset = Asset::AssetRecord{
                        *selected->registeredAssetGuid, snapshot.contentRoot / selected->relativePath};
                } else {
                    m_editor->m_selectedAsset.reset();
                }
                applied = true;
                code = "bridge.command.applied";
                message = "The cached project content entry became the native content-browser selection.";
            }
            break;
        }
        case EditorBridgeRequestKind::QueueAssetTableImport: {
            if (!request.contentEntryPath.has_value() || !request.contentImportDestinationPath.has_value() ||
                !IsBoundedContentPath(*request.contentEntryPath) ||
                !IsBoundedContentPath(*request.contentImportDestinationPath)) {
                return MakeResponse(request, false, "bridge.content.import.request.invalid",
                                       "QueueAssetTableImport requires bounded source and destination paths.");
            }
            if (!m_editor->m_selectedProjectFile.has_value() ||
                m_editor->m_selectedProjectFile->relativePath.generic_string() != *request.contentEntryPath) {
                return MakeResponse(request, false, "bridge.content.import.selection_mismatch",
                                       "The import request must target the currently selected Asset Table entry.");
            }
            const Asset::ProjectFileEntry& selected = *m_editor->m_selectedProjectFile;
            if (selected.kind == Asset::ProjectFileEntryKind::Directory) {
                return MakeResponse(request, false, "bridge.content.import.directory",
                                       "Directories cannot be queued for import.");
            }
            const auto isNormalizedRelativePath = [](const std::string& value) {
                const std::filesystem::path path = std::filesystem::path{value}.lexically_normal();
                return !path.empty() && !path.is_absolute() && !path.has_root_name() &&
                       std::find(path.begin(), path.end(), std::filesystem::path{".."}) == path.end() &&
                       path.generic_string() == value;
            };
            if (!isNormalizedRelativePath(*request.contentEntryPath) ||
                !isNormalizedRelativePath(*request.contentImportDestinationPath) ||
                *request.contentEntryPath == *request.contentImportDestinationPath) {
                return MakeResponse(request, false, "bridge.content.import.path.invalid",
                                       "Import source and destination must be distinct normalized relative paths.");
            }
            const Asset::AssetImportSourceClassification classification =
                m_editor->m_services->GetAssetImporter().ClassifySource(selected.relativePath);
            if (!classification.importerRegistered) {
                return MakeResponse(request, false, "bridge.content.import.unsupported",
                                       classification.diagnostic);
            }
            const Asset::ProjectFileSnapshot projectSnapshot =
                m_editor->m_services->GetProjectFileIndex().GetSnapshot();
            const std::filesystem::path sourcePath = projectSnapshot.contentRoot / selected.relativePath;
            const std::filesystem::path destinationPath =
                projectSnapshot.contentRoot / std::filesystem::path{*request.contentImportDestinationPath};
            Asset::AssetImportRequest importRequest;
            importRequest.sourcePath = sourcePath;
            importRequest.destinationPath = destinationPath;
            importRequest.settings = std::make_shared<const Asset::AssetImportSettings>();
            const std::optional<Asset::AssetImportJobId> jobId =
                m_editor->m_services->GetAssetImportQueue().Enqueue(std::move(importRequest));
            if (!jobId.has_value()) {
                return MakeResponse(request, false, "bridge.content.import.rejected",
                                       "The native import queue rejected the bounded request.");
            }
            applied = true;
            code = "bridge.content.import.queued";
            message = "The selected Asset Table entry was queued for native import.";
            responseContentImportJobId = jobId->value;
            break;
        }
        case EditorBridgeRequestKind::ReadViewportSurface:
            code = "bridge.viewport_surface.unavailable";
            message = "This headless bridge session has no attachable managed viewport surface; native C++ retains window and OpenGL ownership.";
            break;
        case EditorBridgeRequestKind::SubmitDeveloperConsoleCommand:
            if (!request.developerConsoleCommand.has_value() ||
                request.developerConsoleCommand->size() > DeveloperConsole::kMaximumValueBytes) {
                return MakeResponse(request, false, "bridge.developer_console.request.invalid",
                                       "SubmitDeveloperConsoleCommand requires a bounded command payload.");
            }
            applied = m_developerConsole.Execute(*request.developerConsoleCommand);
            code = applied ? "bridge.command.applied" : "bridge.developer_console.command.rejected";
            message = applied ? "The registered native console command executed."
                              : "The native console rejected or could not execute the console command.";
            break;
        case EditorBridgeRequestKind::ClearDeveloperConsole:
            applied = m_developerConsole.Clear();
            code = applied ? "bridge.command.applied" : "bridge.developer_console.clear.noop";
            message = applied ? "The native developer-console output was cleared."
                              : "The native developer-console output was already empty.";
            break;
        case EditorBridgeRequestKind::SetDeveloperConsoleSeverityFilter:
            if (!request.developerConsoleSeverityFilter.has_value()) {
                return MakeResponse(request, false, "bridge.developer_console.request.invalid",
                                       "SetDeveloperConsoleSeverityFilter requires a severity-filter payload.");
            }
            if (!m_developerConsole.IsAvailable()) {
                return MakeResponse(request, false, "bridge.developer_console.unavailable",
                                       "Developer-console discovery is disabled by the native shipping build policy.");
            }
            applied = m_developerConsole.SetSeverityFilter(*request.developerConsoleSeverityFilter);
            code = applied ? "bridge.command.applied" : "bridge.developer_console.filter.noop";
            message = applied ? "The native developer-console severity filter was updated."
                              : "The native developer-console severity filter was already set.";
            break;
        case EditorBridgeRequestKind::SetDeveloperConsoleCompletionPrefix:
            if (!request.developerConsoleCompletionPrefix.has_value() ||
                request.developerConsoleCompletionPrefix->size() > DeveloperConsole::kMaximumValueBytes) {
                return MakeResponse(request, false, "bridge.developer_console.request.invalid",
                                       "SetDeveloperConsoleCompletionPrefix requires a bounded prefix payload.");
            }
            if (!m_developerConsole.IsAvailable()) {
                return MakeResponse(request, false, "bridge.developer_console.unavailable",
                                       "Developer-console discovery is disabled by the native shipping build policy.");
            }
            applied = m_developerConsole.SetCompletionPrefix(*request.developerConsoleCompletionPrefix);
            code = applied ? "bridge.command.applied" : "bridge.developer_console.completion.noop";
            message = applied ? "The native developer-console completion prefix was updated."
                              : "The native developer-console completion prefix was already set.";
            break;
        case EditorBridgeRequestKind::SelectDataTablePreview:
            if (!request.dataTableName.has_value() ||
                request.dataTableName->size() > kEditorBridgeMaximumPresentationTextBytes) {
                return MakeResponse(request, false, "bridge.data_table.request.invalid",
                                       "SelectDataTablePreview requires a bounded table name payload.");
            }
            if (m_dataTableRegistry == nullptr) {
                return MakeResponse(request, false, "bridge.data_table.registry.unavailable",
                                       "Data-table preview selection requires a native registry-backed bridge session.");
            }
            applied = SetPreviewTable(*request.dataTableName);
            code = applied ? "bridge.command.applied" : "bridge.data_table.preview.invalid";
            message = applied
                ? "The selected native data-table preview was updated."
                : "The requested native data-table name is not available in the session registry.";
            break;
        case EditorBridgeRequestKind::MoveDeveloperConsoleHistory:
            if (!request.developerConsoleHistoryDelta.has_value() || *request.developerConsoleHistoryDelta == 0) {
                return MakeResponse(request, false, "bridge.developer_console.request.invalid",
                                       "MoveDeveloperConsoleHistory requires a non-zero history delta.");
            }
            if (!m_developerConsole.IsAvailable()) {
                return MakeResponse(request, false, "bridge.developer_console.unavailable",
                                       "Developer-console history is disabled by the native shipping build policy.");
            }
            applied = m_developerConsole.MoveHistory(*request.developerConsoleHistoryDelta);
            code = applied ? "bridge.command.applied" : "bridge.developer_console.history.noop";
            message = applied ? "The native developer-console history cursor moved."
                              : "The native developer-console history cursor did not move.";
            break;
        case EditorBridgeRequestKind::ReadDeveloperConsole:
            code = "bridge.developer_console.snapshot.read";
            message = "The bounded developer-console snapshot was copied.";
            break;
        case EditorBridgeRequestKind::ReadSnapshot:
            break;
    }

    SynchronizeRevision();
    EditorBridgeResponse response = MakeResponse(request, applied, std::move(code), std::move(message));
    response.createdEntity = createdEntity;
    response.contentImportJobId = responseContentImportJobId;
    return response;
}

EditorBridge::ObservedState EditorBridge::CaptureObservedState() {
    ObservedState observed{};
    observed.editorState = m_editor->GetState();
    observed.playModeState = m_editor->GetPlayModeState();
    observed.sceneDirty = m_editor->IsSceneDirty();
    observed.canUndo = m_editor->CanUndo();
    observed.canRedo = m_editor->CanRedo();
    observed.activeScenePath = m_editor->GetActiveScenePath();
    for (const Scene::Entity entity : m_editor->GetSelectedEntities()) {
        if (observed.selectedEntities.size() >= kEditorBridgeMaximumPanelEntries) {
            observed.selectedEntitiesTruncated = true;
            break;
        }
        observed.selectedEntities.push_back(
            EditorBridgeEntitySnapshot{ToBridgeEntity(entity), BoundPresentationText(m_editor->GetEntityDisplayLabel(entity))});
    }
    const Scene::Entity active = m_editor->GetSelectedEntity();
    if (m_editor->IsDocumentEntity(active)) {
        observed.activeEntity = ToBridgeEntity(active);
    }
    observed.hierarchy = CaptureHierarchy();
    observed.inspector = CaptureInspector();
    observed.assetTable = CaptureAssetTable();
    observed.viewportSurface = EditorBridgeViewportSurfaceSnapshot{
        EditorBridgeViewportSurfaceState::Unavailable, 0U, 0U, 0U, true, false,
        "No managed viewport surface transport is available in this headless bridge session."};
    observed.developerConsole = CaptureDeveloperConsole();
    observed.dataTableCatalog = CaptureDataTableCatalog();
    observed.dataTablePreview = CaptureDataTablePreview();
    return observed;
}

EditorBridgeDeveloperConsoleSnapshot EditorBridge::CaptureDeveloperConsole() const {
    return EditorBridgeDeveloperConsoleSnapshot{m_developerConsole.GetSnapshot()};
}

EditorBridgeDataTableCatalogSnapshot EditorBridge::CaptureDataTableCatalog() const {
    const Asset::DataTableCatalogSnapshot source = m_dataTableRegistry != nullptr
        ? m_dataTableRegistry->GetCatalogSnapshot()
        : m_dataTableCatalogSnapshot;
    EditorBridgeDataTableCatalogSnapshot snapshot{};
    snapshot.generation = source.generation;
    snapshot.entriesTruncated = source.entriesTruncated;
    snapshot.entries.reserve(source.entries.size());
    for (const Asset::DataTableCatalogEntry& entry : source.entries) {
        if (snapshot.entries.size() >= kEditorBridgeMaximumPanelEntries) {
            snapshot.entriesTruncated = true;
            break;
        }
        snapshot.entries.push_back(EditorBridgeDataTableCatalogEntry{
            BoundPresentationText(entry.name), entry.generation, entry.columnCount, entry.rowCount, entry.valid});
    }
    return snapshot;
}

EditorBridgeDataTablePreviewSnapshot EditorBridge::CaptureDataTablePreview() const {
    Asset::DataTableSnapshot source = m_dataTablePreviewSnapshot;
    if (m_dataTableRegistry != nullptr) {
        source = Asset::DataTableSnapshot{};
        if (m_dataTablePreviewName.has_value()) {
            static_cast<void>(m_dataTableRegistry->TryGetSnapshot(*m_dataTablePreviewName, source));
        }
    }

    EditorBridgeDataTablePreviewSnapshot preview{};
    preview.generation = source.generation;
    preview.name = BoundPresentationText(source.name);
    preview.available = !preview.name.empty();
    preview.totalColumnCount = source.columns.size();
    preview.totalRowCount = source.rows.size();
    preview.reason = preview.available
        ? "Native table preview is available as copied read-only facts."
        : m_dataTableRegistry != nullptr && m_dataTablePreviewName.has_value()
            ? "The selected native data-table preview is no longer available."
            : "No native data-table preview is selected in this bridge session.";

    for (const Asset::DataTableColumn& column : source.columns) {
        if (preview.columns.size() >= kEditorBridgeMaximumPanelEntries) {
            preview.columnsTruncated = true;
            break;
        }
        preview.columns.push_back(EditorBridgeDataTablePreviewColumn{
            BoundPresentationText(column.name), column.type});
    }
    for (const Asset::DataTableRow& row : source.rows) {
        if (preview.rows.size() >= kEditorBridgeMaximumPanelEntries) {
            preview.rowsTruncated = true;
            break;
        }
        EditorBridgeDataTablePreviewRow copiedRow{};
        copiedRow.identifier = BoundPresentationText(row.identifier);
        const std::size_t visibleValueCount = std::min(row.values.size(), preview.columns.size());
        for (std::size_t index = 0U; index < visibleValueCount; ++index) {
            std::string displayValue = FormatDataTableValue(row.values[index]);
            if (displayValue.size() > kEditorBridgeMaximumPresentationTextBytes) {
                displayValue.resize(kEditorBridgeMaximumPresentationTextBytes);
                preview.valuesTruncated = true;
            }
            copiedRow.values.push_back(std::move(displayValue));
        }
        if (row.values.size() != visibleValueCount) {
            preview.valuesTruncated = true;
        }
        preview.rows.push_back(std::move(copiedRow));
    }
    return preview;
}

void EditorBridge::SynchronizeRevision() {
    ObservedState observed = CaptureObservedState();
    if (!m_lastObservedState.has_value()) {
        m_lastObservedState = std::move(observed);
        m_revision = 1U;
        return;
    }
    if (*m_lastObservedState != observed) {
        m_lastObservedState = std::move(observed);
        ++m_revision;
    }
}

EditorBridgeSnapshot EditorBridge::BuildSnapshot() const {
    const ObservedState& observed = *m_lastObservedState;
    EditorBridgeSnapshot snapshot{};
    snapshot.revision = m_revision;
    snapshot.editorState = observed.editorState;
    snapshot.playModeState = observed.playModeState;
    snapshot.sceneDirty = observed.sceneDirty;
    snapshot.canUndo = observed.canUndo;
    snapshot.canRedo = observed.canRedo;
    snapshot.activeScenePath = observed.activeScenePath;
    snapshot.selectedEntities = observed.selectedEntities;
    snapshot.selectedEntitiesTruncated = observed.selectedEntitiesTruncated;
    snapshot.activeEntity = observed.activeEntity;
    snapshot.hierarchy = observed.hierarchy;
    snapshot.inspector = observed.inspector;
    snapshot.assetTable = observed.assetTable;
    snapshot.viewportSurface = observed.viewportSurface;
    snapshot.developerConsole = observed.developerConsole;
    snapshot.dataTableCatalog = observed.dataTableCatalog;
    snapshot.dataTablePreview = observed.dataTablePreview;
    snapshot.capabilities = GetCapabilities();
    return snapshot;
}

EditorBridgeResponse EditorBridge::MakeResponse(const EditorBridgeRequest& request, const bool applied,
                                                           std::string code, std::string message) const {
    return EditorBridgeResponse{kEditorBridgeProtocolVersion, request.requestId, applied,
                                   std::move(code), std::move(message), BuildSnapshot(), std::nullopt,
                                   std::nullopt};
}

Scene::Entity EditorBridge::ToEntity(const EditorBridgeEntityRef entity) noexcept {
    return Scene::Entity{entity.index, entity.generation};
}

EditorBridgeEntityRef EditorBridge::ToBridgeEntity(const Scene::Entity entity) noexcept {
    return EditorBridgeEntityRef{entity.index, entity.generation};
}

bool EditorBridge::IsSupportedEntityKind(const EditorEntityKind kind) const noexcept {
    switch (kind) {
        case EditorEntityKind::Empty:
        case EditorEntityKind::Camera:
        case EditorEntityKind::DirectionalLight:
        case EditorEntityKind::CollisionBox:
        case EditorEntityKind::Cube:
        case EditorEntityKind::UVSphere:
        case EditorEntityKind::Plane:
            return true;
    }
    return false;
}

std::string EditorBridge::BoundPresentationText(std::string value) {
    if (value.size() > kEditorBridgeMaximumPresentationTextBytes) {
        value.resize(kEditorBridgeMaximumPresentationTextBytes);
    }
    return value;
}

std::string EditorBridge::BoundContentPath(std::string value) {
    if (value.size() > kEditorBridgeMaximumContentPathBytes) {
        value.resize(kEditorBridgeMaximumContentPathBytes);
    }
    return value;
}

EditorBridgeHierarchySnapshot EditorBridge::CaptureHierarchy() {
    EditorBridgeHierarchySnapshot snapshot{};
    snapshot.filter = BoundPresentationText(m_editor->m_hierarchyFilter);
    snapshot.filterActive = m_editor->IsHierarchyFilterActive();
    m_editor->RebuildHierarchyFilterCache();

    Scene::IEntityManager& entityManager = m_editor->m_services->GetEntityManager();
    const auto visit = [this, &snapshot, &entityManager](const auto& self, const Scene::Entity entity,
                                                          const std::size_t depth) -> void {
        if (!m_editor->IsDocumentEntity(entity) || !m_editor->IsHierarchyEntityVisible(entity)) {
            return;
        }
        if (snapshot.entries.size() >= kEditorBridgeMaximumPanelEntries) {
            snapshot.truncated = true;
            return;
        }

        Scene::Entity parent = Scene::kInvalidEntity;
        const bool hasParent = m_editor->TryGetDocumentParent(entity, parent) && parent != Scene::kInvalidEntity;
        // The same tree the panel draws: a placed entity is one row, so an automated reader sees
        // the level as the author does rather than as a flatter tree that no window shows.
        const std::vector<Scene::Entity> children =
            m_editor->IsSealedHierarchyRow(entity)
                ? std::vector<Scene::Entity>{}
                : m_editor->m_services->GetSceneGraph().GetChildren(entityManager, entity);
        snapshot.entries.push_back(EditorBridgeHierarchyEntry{
            ToBridgeEntity(entity),
            hasParent ? std::optional<EditorBridgeEntityRef>{ToBridgeEntity(parent)} : std::nullopt,
            BoundPresentationText(m_editor->GetEntityDisplayLabel(entity)),
            BoundPresentationText(m_editor->GetOutlinerTypeTag(entity)),
            depth,
            children.size(),
            m_editor->IsEntitySelected(entity),
            entity == m_editor->m_selectedEntity,
        });
        for (const Scene::Entity child : children) {
            self(self, child, depth + 1U);
        }
    };
    for (const Scene::Entity root : m_editor->GetDocumentRoots()) {
        visit(visit, root, 0U);
    }
    return snapshot;
}

EditorBridgeInspectorSnapshot EditorBridge::CaptureInspector() const {
    EditorBridgeInspectorSnapshot snapshot{};
    for (const Scene::Entity entity : m_editor->GetSelectedEntities()) {
        if (snapshot.selectedEntities.size() >= kEditorBridgeMaximumPanelEntries) {
            snapshot.selectedEntitiesTruncated = true;
            break;
        }
        snapshot.selectedEntities.push_back(
            EditorBridgeEntitySnapshot{ToBridgeEntity(entity), BoundPresentationText(m_editor->GetEntityDisplayLabel(entity))});
    }
    if (snapshot.selectedEntities.empty()) {
        return snapshot;
    }

    const Scene::Entity active = m_editor->GetSelectedEntity();
    if (m_editor->IsDocumentEntity(active)) {
        snapshot.activeEntity = EditorBridgeEntitySnapshot{
            ToBridgeEntity(active), BoundPresentationText(m_editor->GetEntityDisplayLabel(active))};
    }
    if (!m_editor->HasSingleDocumentSelection() || !snapshot.activeEntity.has_value()) {
        snapshot.mode = EditorBridgeInspectorMode::MultiSelection;
        return snapshot;
    }

    snapshot.mode = EditorBridgeInspectorMode::SingleSelection;
    Scene::IEntityManager& entityManager = m_editor->m_services->GetEntityManager();
    if (entityManager.HasComponent<Scene::PrefabInstanceComponent>(active)) {
        const Scene::PrefabInstanceComponent& instance =
            entityManager.GetComponent<Scene::PrefabInstanceComponent>(active);
        EditorBridgePrefabSnapshot prefabSnapshot{};
        prefabSnapshot.sourcePrefabGuid = instance.sourcePrefabGuid != Asset::kInvalidAssetGuid
                                              ? std::optional<std::uint64_t>{instance.sourcePrefabGuid.value}
                                              : std::nullopt;
        prefabSnapshot.instanceRevision = instance.instanceRevision;
        prefabSnapshot.overrideCount = instance.overrides.size();
        const std::filesystem::path sourcePath =
            m_editor->m_services->GetAssetDatabase().Resolve(instance.sourcePrefabGuid);
        const std::optional<std::uint64_t> observedRevision =
            Scene::ComputePrefabSourceRevision(sourcePath);
        if (observedRevision.has_value()) {
            prefabSnapshot.sourceRevision = *observedRevision;
            prefabSnapshot.status = instance.overrides.empty() && *observedRevision == instance.instanceRevision
                                        ? EditorBridgePrefabRevisionStatus::Current
                                        : (instance.overrides.empty()
                                               ? EditorBridgePrefabRevisionStatus::Stale
                                               : EditorBridgePrefabRevisionStatus::MergeRequired);
            prefabSnapshot.canRefresh = instance.overrides.empty() &&
                                        prefabSnapshot.status == EditorBridgePrefabRevisionStatus::Stale &&
                                        m_editor->IsAuthoringCommandAllowed();
        }
        snapshot.prefab = prefabSnapshot;
    }
    if (entityManager.HasComponent<Scene::MeshComponent>(active)) {
        const Scene::MeshComponent& meshComponent =
            entityManager.GetComponent<Scene::MeshComponent>(active);
        snapshot.assetBinding = EditorBridgeAssetBindingSnapshot{
            meshComponent.meshGuid != Asset::kInvalidAssetGuid ? std::optional<std::uint64_t>{meshComponent.meshGuid.value}
                                                                  : std::nullopt,
            meshComponent.materialGuid != Asset::kInvalidAssetGuid
                ? std::optional<std::uint64_t>{meshComponent.materialGuid.value}
                : std::nullopt};
    }
    Scene::Entity parent = Scene::kInvalidEntity;
    if (m_editor->TryGetDocumentParent(active, parent) && parent != Scene::kInvalidEntity) {
        snapshot.parent = EditorBridgeEntitySnapshot{
            ToBridgeEntity(parent), BoundPresentationText(m_editor->GetHierarchyCandidateLabel(parent))};
    }
    for (const Scene::Entity ancestor : m_editor->GetDocumentAncestry(active)) {
        if (snapshot.ancestry.size() >= kEditorBridgeMaximumPanelEntries) {
            break;
        }
        snapshot.ancestry.push_back(EditorBridgeEntitySnapshot{
            ToBridgeEntity(ancestor), BoundPresentationText(m_editor->GetHierarchyCandidateLabel(ancestor))});
    }
    for (std::string identifier : m_editor->m_inspectorDrawerRegistry.GetEligibleDrawerIds(active)) {
        if (snapshot.eligibleDrawerIds.size() >= kEditorBridgeMaximumPanelEntries) {
            break;
        }
        const std::string boundedIdentifier = BoundPresentationText(identifier);
        snapshot.eligibleDrawerIds.push_back(boundedIdentifier);
        if (identifier != "name" && identifier != "hierarchy" && identifier != "transform" &&
            identifier != "primitive-mesh") {
            snapshot.attachedComponentIds.push_back(boundedIdentifier);
        }
    }
    snapshot.canEditSelectedName = m_editor->IsAuthoringCommandAllowed();
    return snapshot;
}

EditorBridgeAssetTableSnapshot EditorBridge::CaptureAssetTable() {
    EditorBridgeAssetTableSnapshot snapshot{};
    Asset::IProjectFileIndex& projectFileIndex = m_editor->m_services->GetProjectFileIndex();
    const Asset::ProjectFileSnapshot nativeSnapshot = projectFileIndex.GetSnapshot();
    m_editor->ReconcileAssetTableDirectory(nativeSnapshot);

    snapshot.contentRoot = BoundPresentationText(nativeSnapshot.contentRoot.generic_string());
    snapshot.currentDirectory = BoundContentPath(m_editor->m_assetTableDirectory.generic_string());
    snapshot.filter = BoundPresentationText(m_editor->m_assetFilter);
    snapshot.typeFocus = BoundPresentationText(Editor::GetAssetTableFocusLabel(m_editor->m_assetTableTypeFocus));
    for (const std::filesystem::path& segment : m_editor->m_assetTableDirectory) {
        if (snapshot.breadcrumbs.size() >= kEditorBridgeMaximumPanelEntries) {
            break;
        }
        snapshot.breadcrumbs.push_back(BoundPresentationText(segment.generic_string()));
    }
    snapshot.refreshGeneration = nativeSnapshot.refreshGeneration;
    snapshot.contentRootExists = nativeSnapshot.contentRootExists;
    snapshot.initialized = m_editor->m_projectFileSnapshotInitialized;
    snapshot.lastRefreshSucceeded = m_editor->m_projectFileLastRefreshSucceeded;

    for (const Asset::ProjectFileEntry& entry : nativeSnapshot.entries) {
        if (entry.relativePath.parent_path() == m_editor->m_assetTableDirectory) {
            ++snapshot.directEntryCount;
        }
    }
    // The same items the panel lists: a folder's children, or with a search everything under it
    // whose name matches (a shelf the panel may show is not a bridge location).
    for (const std::size_t index :
         ListContentFolder(nativeSnapshot.entries, m_editor->m_assetTableDirectory, m_editor->m_assetFilter)) {
        const Asset::ProjectFileEntry& entry = nativeSnapshot.entries[index];
        if (!m_editor->DoesAssetTableEntryMatchFocus(entry)) {
            continue;
        }
        ++snapshot.visibleEntryCount;
        if (snapshot.entries.size() >= kEditorBridgeMaximumPanelEntries) {
            snapshot.truncated = true;
            continue;
        }
        snapshot.entries.push_back(ToContentEntry(entry));
    }

    if (m_editor->m_selectedProjectFile.has_value()) {
        const Asset::ProjectFileEntry& selected = *m_editor->m_selectedProjectFile;
        snapshot.selectedEntry = ToContentEntry(selected);
        snapshot.importAction.hasSelection = true;
        if (selected.kind == Asset::ProjectFileEntryKind::Directory) {
            snapshot.importAction.sourceKind = "directory";
            snapshot.importAction.diagnostic = "directories cannot be imported or reimported";
        } else {
            const Asset::AssetImportSourceClassification classification =
                m_editor->m_services->GetAssetImporter().ClassifySource(selected.relativePath);
            snapshot.importAction.importerRegistered = classification.importerRegistered;
            snapshot.importAction.requiresFormatSpecificParser = classification.requiresFormatSpecificParser;
            snapshot.importAction.sourceKind = SourceKindLabel(classification.kind);
            snapshot.importAction.canImport = !selected.registeredAssetGuid.has_value() &&
                                              classification.importerRegistered;
            snapshot.importAction.canReimport = selected.registeredAssetGuid.has_value() &&
                                                 classification.importerRegistered;
            snapshot.importAction.diagnostic = classification.diagnostic;
        }
    } else {
        snapshot.importAction.diagnostic = "no Asset Table entry is selected";
    }
    return snapshot;
}

EditorBridgeAssetTableEntry EditorBridge::ToContentEntry(const Asset::ProjectFileEntry& entry) const {
    return EditorBridgeAssetTableEntry{
        BoundContentPath(entry.relativePath.generic_string()),
        entry.kind == Asset::ProjectFileEntryKind::Directory,
        BoundPresentationText(Editor::GetAssetTableItemTypeLabel(m_editor->GetAssetTableEntryType(entry))),
        entry.registeredAssetGuid.has_value() ? std::optional<std::uint64_t>{entry.registeredAssetGuid->value} : std::nullopt,
    };
}

std::optional<Editor::AssetTableTypeFocus> EditorBridge::ParseAssetTableFocus(
    const std::string& focus) noexcept {
    using Focus = Editor::AssetTableTypeFocus;
    if (focus == "all") {
        return Focus::All;
    }
    if (focus == "folders") {
        return Focus::Folders;
    }
    if (focus == "scene") {
        return Focus::Scene;
    }
    if (focus == "prefab") {
        return Focus::Prefab;
    }
    if (focus == "bundle") {
        return Focus::Bundle;
    }
    if (focus == "mesh") {
        return Focus::Mesh;
    }
    if (focus == "texture") {
        return Focus::Texture;
    }
    if (focus == "shader") {
        return Focus::Shader;
    }
    if (focus == "material") {
        return Focus::Material;
    }
    if (focus == "save") {
        return Focus::Save;
    }
    if (focus == "registered") {
        return Focus::Registered;
    }
    if (focus == "otherFiles") {
        return Focus::OtherFiles;
    }
    return std::nullopt;
}

} // namespace UVE::Editor
