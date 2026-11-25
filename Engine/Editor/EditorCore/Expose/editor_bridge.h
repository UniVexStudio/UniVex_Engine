// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "data_table_registry.h"
#include "data_table.h"
#include "editor.h"
#include "developer_console.h"

namespace UVE::Editor {

inline constexpr std::uint32_t kEditorBridgeProtocolVersion = 1U;

/// Copy-only identity suitable for protocol DTOs. It intentionally mirrors a generational entity
/// handle without exposing entity-manager memory or behavior across a future managed boundary.
struct EditorBridgeEntityRef final {
    std::uint32_t index = Scene::kInvalidEntity.index;
    std::uint32_t generation = Scene::kInvalidEntity.generation;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return Scene::Entity{index, generation} != Scene::kInvalidEntity;
    }
    [[nodiscard]] constexpr bool operator==(const EditorBridgeEntityRef&) const noexcept = default;
};

/// Capabilities are ordered stable protocol facts. A future C# host must only surface controls that
/// are advertised here and must still handle request rejection from an authoritative C++ backend.
enum class EditorBridgeCapability : std::uint8_t {
    ReadSnapshot,
    SelectEntity,
    ClearSelection,
    SetSelectedEntityName,
    CreateDocumentEntity,
    Undo,
    Redo,
    ReadHierarchy,
    ReadInspector,
    ReadAssetTable,
    ToggleEntitySelection,
    SetHierarchyFilter,
    SetAssetTableDirectory,
    SetAssetTableFilter,
    SetAssetTableFocus,
    RefreshAssetTable,
    SelectAssetTableEntry,
    QueueAssetTableImport,
    ReadViewportSurface,
    ReadDeveloperConsole,
    SubmitDeveloperConsoleCommand,
    ClearDeveloperConsole,
    SetDeveloperConsoleSeverityFilter,
    SetDeveloperConsoleCompletionPrefix,
    MoveDeveloperConsoleHistory,
    SelectDataTablePreview,
};

/// The deliberately small v1 request vocabulary. No generic command string is accepted because
/// every mutation must be routed through a named, testable Editor command path.
enum class EditorBridgeRequestKind : std::uint8_t {
    ReadSnapshot,
    SelectEntity,
    ClearSelection,
    SetSelectedEntityName,
    CreateDocumentEntity,
    Undo,
    Redo,
    ToggleEntitySelection,
    SetHierarchyFilter,
    SetAssetTableDirectory,
    SetAssetTableFilter,
    SetAssetTableFocus,
    RefreshAssetTable,
    SelectAssetTableEntry,
    QueueAssetTableImport,
    ReadViewportSurface,
    ReadDeveloperConsole,
    SubmitDeveloperConsoleCommand,
    ClearDeveloperConsole,
    SetDeveloperConsoleSeverityFilter,
    SetDeveloperConsoleCompletionPrefix,
    MoveDeveloperConsoleHistory,
    SelectDataTablePreview,
};

/// Explicitly describes whether this bridge session has a native-owned viewport surface. No raw
/// window, GL context, texture, or input-forwarding handle crosses the bridge boundary.
enum class EditorBridgeViewportSurfaceState : std::uint8_t {
    Unavailable,
    NativeOwned,
    Detached,
};

struct EditorBridgeViewportSurfaceSnapshot final {
    EditorBridgeViewportSurfaceState state = EditorBridgeViewportSurfaceState::Unavailable;
    std::uint64_t generation = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    bool nativeRendererOwnsSurface = true;
    bool managedAttachAllowed = false;
    std::string reason;

    [[nodiscard]] bool operator==(const EditorBridgeViewportSurfaceSnapshot&) const = default;
};

/// One copied entity fact usable by a UI. The label is intentionally presentation data, while all
/// component and hierarchy ownership remains in the native editor and ECS.
struct EditorBridgeEntitySnapshot final {
    EditorBridgeEntityRef entity;
    std::string displayLabel;

    [[nodiscard]] bool operator==(const EditorBridgeEntitySnapshot&) const = default;
};

/// Read-only asset references copied from the selected native MeshComponent. GUIDs are values,
/// not asset handles; the bridge never transfers resource ownership or loading authority.
struct EditorBridgeAssetBindingSnapshot final {
    std::optional<std::uint64_t> meshGuid;
    std::optional<std::uint64_t> materialGuid;

    [[nodiscard]] bool operator==(const EditorBridgeAssetBindingSnapshot&) const = default;
};

/// The bridge deliberately bounds presentation records before serializing them. A client must show
/// the explicit truncation fact and never infer that an omitted entry was deleted from the editor.
inline constexpr std::size_t kEditorBridgeMaximumPanelEntries = 256U;
inline constexpr std::size_t kEditorBridgeMaximumPresentationTextBytes = 256U;
/// Relative content paths double as native-validated request identities, so they use a separate
/// conservative bound rather than the shorter display-text bound. 128 such rows remain well below
/// the framed protocol's 1 MiB maximum after JSON encoding.
inline constexpr std::size_t kEditorBridgeMaximumContentPathBytes = 4096U;

/// One depth-first copied Scene Outliner row. Parentage stays value-only and C++ remains the sole
/// owner of graph traversal, document membership, selection, and type-tag priority.
struct EditorBridgeHierarchyEntry final {
    EditorBridgeEntityRef entity;
    std::optional<EditorBridgeEntityRef> parent;
    std::string displayLabel;
    std::string typeTag;
    std::size_t depth = 0U;
    std::size_t childCount = 0U;
    bool selected = false;
    bool active = false;

    [[nodiscard]] bool operator==(const EditorBridgeHierarchyEntry&) const = default;
};

struct EditorBridgeHierarchySnapshot final {
    std::string filter;
    bool filterActive = false;
    bool truncated = false;
    std::vector<EditorBridgeHierarchyEntry> entries;

    [[nodiscard]] bool operator==(const EditorBridgeHierarchySnapshot&) const = default;
};

enum class EditorBridgeInspectorMode : std::uint8_t {
    NoSelection,
    MultiSelection,
    SingleSelection,
};

/// Read-only inspector context. Drawer identifiers are copied stable native registry facts, not
/// reflection metadata or a managed authorization to edit arbitrary components.
enum class EditorBridgePrefabRevisionStatus : std::uint8_t {
    Unavailable,
    Current,
    Stale,
    MergeRequired,
};

struct EditorBridgePrefabSnapshot final {
    std::optional<std::uint64_t> sourcePrefabGuid;
    std::uint64_t sourceRevision = 0U;
    std::uint64_t instanceRevision = 0U;
    std::size_t overrideCount = 0U;
    EditorBridgePrefabRevisionStatus status = EditorBridgePrefabRevisionStatus::Unavailable;
    bool canRefresh = false;

    [[nodiscard]] bool operator==(const EditorBridgePrefabSnapshot&) const noexcept = default;
};

struct EditorBridgeInspectorSnapshot final {
    EditorBridgeInspectorMode mode = EditorBridgeInspectorMode::NoSelection;
    bool selectedEntitiesTruncated = false;
    std::vector<EditorBridgeEntitySnapshot> selectedEntities;
    std::optional<EditorBridgeEntitySnapshot> activeEntity;
    std::optional<EditorBridgeEntitySnapshot> parent;
    std::vector<EditorBridgeEntitySnapshot> ancestry;
    std::vector<std::string> eligibleDrawerIds;
    std::vector<std::string> attachedComponentIds;
    std::optional<EditorBridgeAssetBindingSnapshot> assetBinding;
    std::optional<EditorBridgePrefabSnapshot> prefab;
    bool canEditSelectedName = false;

    [[nodiscard]] bool operator==(const EditorBridgeInspectorSnapshot&) const = default;
};

/// One cached project-content row. This contains no absolute path traversal authority and no file
/// handle; `relativePath` is relative to the native index's configured content root.
struct EditorBridgeAssetTableEntry final {
    std::string relativePath;
    bool isDirectory = false;
    std::string typeLabel;
    std::optional<std::uint64_t> registeredAssetGuid;

    [[nodiscard]] bool operator==(const EditorBridgeAssetTableEntry&) const = default;
};

/// Value-only import/reimport facts for the selected Asset Table entry. This is capability
/// presentation, not an execution token: native importer registration, filesystem access, and
/// AssetDatabase ownership remain on the C++ side.
struct EditorBridgeContentImportActionSnapshot final {
    bool hasSelection = false;
    bool canImport = false;
    bool canReimport = false;
    bool importerRegistered = false;
    bool requiresFormatSpecificParser = false;
    std::string sourceKind;
    std::string diagnostic;

    [[nodiscard]] bool operator==(const EditorBridgeContentImportActionSnapshot&) const = default;
};

/// Copied current-folder view over the native ProjectFileIndex cache. UI filtering, navigation,
/// refresh, and selection all remain C++ editor session state and never trigger managed filesystem I/O.
struct EditorBridgeAssetTableSnapshot final {
    std::string contentRoot;
    std::string currentDirectory;
    std::string filter;
    std::string typeFocus;
    std::vector<std::string> breadcrumbs;
    std::uint64_t refreshGeneration = 0U;
    std::size_t visibleEntryCount = 0U;
    std::size_t directEntryCount = 0U;
    bool contentRootExists = false;
    bool initialized = false;
    bool lastRefreshSucceeded = true;
    bool truncated = false;
    std::vector<EditorBridgeAssetTableEntry> entries;
    std::optional<EditorBridgeAssetTableEntry> selectedEntry;
    EditorBridgeContentImportActionSnapshot importAction;

    [[nodiscard]] bool operator==(const EditorBridgeAssetTableSnapshot&) const = default;
};

/// Immutable bridge-visible state. A revision is incremented whenever any field observable through
/// this snapshot changes, whether the editor's own UI or the bridge initiated that change.
struct EditorBridgeDeveloperConsoleSnapshot final {
    DeveloperConsoleSnapshot console;
    [[nodiscard]] bool operator==(const EditorBridgeDeveloperConsoleSnapshot&) const = default;
};

struct EditorBridgeDataTableCatalogEntry final {
    std::string name;
    std::uint64_t generation = 0U;
    std::size_t columnCount = 0U;
    std::size_t rowCount = 0U;
    bool valid = false;
    [[nodiscard]] bool operator==(const EditorBridgeDataTableCatalogEntry&) const = default;
};

struct EditorBridgeDataTableCatalogSnapshot final {
    std::uint64_t generation = 0U;
    bool entriesTruncated = false;
    std::vector<EditorBridgeDataTableCatalogEntry> entries;
    [[nodiscard]] bool operator==(const EditorBridgeDataTableCatalogSnapshot&) const = default;
};

struct EditorBridgeDataTablePreviewColumn final {
    std::string name;
    Asset::DataTableColumnType type = Asset::DataTableColumnType::String;
    [[nodiscard]] bool operator==(const EditorBridgeDataTablePreviewColumn&) const = default;
};

struct EditorBridgeDataTablePreviewRow final {
    std::string identifier;
    std::vector<std::string> values;
    [[nodiscard]] bool operator==(const EditorBridgeDataTablePreviewRow&) const = default;
};

struct EditorBridgeDataTablePreviewSnapshot final {
    bool available = false;
    std::uint64_t generation = 0U;
    std::string name;
    std::size_t totalColumnCount = 0U;
    std::size_t totalRowCount = 0U;
    bool columnsTruncated = false;
    bool rowsTruncated = false;
    bool valuesTruncated = false;
    std::string reason;
    std::vector<EditorBridgeDataTablePreviewColumn> columns;
    std::vector<EditorBridgeDataTablePreviewRow> rows;
    [[nodiscard]] bool operator==(const EditorBridgeDataTablePreviewSnapshot&) const = default;
};

struct EditorBridgeSnapshot final {
    std::uint32_t protocolVersion = kEditorBridgeProtocolVersion;
    std::uint64_t revision = 0U;
    EditorState editorState = EditorState::Uninitialized;
    EditorPlayModeState playModeState = EditorPlayModeState::Edit;
    bool sceneDirty = false;
    bool canUndo = false;
    bool canRedo = false;
    std::filesystem::path activeScenePath;
    std::vector<EditorBridgeEntitySnapshot> selectedEntities;
    bool selectedEntitiesTruncated = false;
    std::optional<EditorBridgeEntityRef> activeEntity;
    EditorBridgeHierarchySnapshot hierarchy;
    EditorBridgeInspectorSnapshot inspector;
    EditorBridgeAssetTableSnapshot assetTable;
    EditorBridgeViewportSurfaceSnapshot viewportSurface;
    EditorBridgeDeveloperConsoleSnapshot developerConsole;
    EditorBridgeDataTableCatalogSnapshot dataTableCatalog;
    EditorBridgeDataTablePreviewSnapshot dataTablePreview;
    std::vector<EditorBridgeCapability> capabilities;
};

/// A value-only request. `expectedRevision` protects a future C# UI from applying a mutation based
/// on a stale copied snapshot after the editor's own UI or another bridge request changed visible state.
struct EditorBridgeRequest final {
    std::uint32_t protocolVersion = kEditorBridgeProtocolVersion;
    std::uint64_t requestId = 0U;
    std::uint64_t expectedRevision = 0U;
    EditorBridgeRequestKind kind = EditorBridgeRequestKind::ReadSnapshot;
    std::optional<EditorBridgeEntityRef> entity;
    std::optional<std::string> entityName;
    std::optional<EditorEntityKind> entityKind;
    std::optional<std::string> hierarchyFilter;
    std::optional<std::string> contentDirectory;
    std::optional<std::string> contentFilter;
    std::optional<std::string> contentFocus;
    std::optional<std::string> contentEntryPath;
    std::optional<std::string> contentImportDestinationPath;
    std::optional<std::string> dataTableName;
    std::optional<std::string> developerConsoleCommand;
    std::optional<DeveloperConsoleSeverityFilter> developerConsoleSeverityFilter;
    std::optional<std::string> developerConsoleCompletionPrefix;
    std::optional<std::int32_t> developerConsoleHistoryDelta;

    EditorBridgeRequest() = default;

    /// Retains source compatibility with the original named-command request shape. New panel
    /// payloads stay opt-in fields so existing callers cannot accidentally reinterpret a request.
    EditorBridgeRequest(std::uint32_t inProtocolVersion, std::uint64_t inRequestId,
                           std::uint64_t inExpectedRevision, EditorBridgeRequestKind inKind,
                           std::optional<EditorBridgeEntityRef> inEntity,
                           std::optional<std::string> inEntityName,
                           std::optional<EditorEntityKind> inEntityKind)
        : protocolVersion(inProtocolVersion),
          requestId(inRequestId),
          expectedRevision(inExpectedRevision),
          kind(inKind),
          entity(std::move(inEntity)),
          entityName(std::move(inEntityName)),
          entityKind(inEntityKind) {}
};

/// A deterministic response. `code` is a stable machine-readable protocol identifier and `message`
/// is safe human-facing context. The copied snapshot is always freshly synchronized before return.
struct EditorBridgeResponse final {
    std::uint32_t protocolVersion = kEditorBridgeProtocolVersion;
    std::uint64_t requestId = 0U;
    bool applied = false;
    std::string code;
    std::string message;
    EditorBridgeSnapshot snapshot;
    std::optional<EditorBridgeEntityRef> createdEntity;
    std::optional<std::uint64_t> contentImportJobId;
};

/// Main-thread adapter over Editor. It supports coexistence with the editor's own UI: every
/// public entry synchronizes the bridge-visible snapshot before inspecting expectedRevision, so a
/// C# client can observe native changes and cannot apply a stale mutation silently.
class EditorBridge final {
public:
    /// The optional registry is non-owning and must outlive this bridge. When supplied, it is the
    /// authoritative source for catalog facts and selected preview snapshots; the legacy injection
    /// seams remain available only for bridge sessions without a registry dependency.
    explicit EditorBridge(Editor& editor, const Asset::DataTableRegistry* dataTableRegistry = nullptr);

    [[nodiscard]] EditorBridgeSnapshot GetSnapshot();
    [[nodiscard]] EditorBridgeResponse Dispatch(const EditorBridgeRequest& request);
    /// Selects a registry-owned table for read-only preview. An empty name clears the selection.
    /// Unknown or overlong names are rejected without changing the current selection.
    [[nodiscard]] bool SetPreviewTable(std::string_view name);
    void SetDataTableCatalogSnapshot(Asset::DataTableCatalogSnapshot snapshot);
    void SetDataTablePreviewSnapshot(Asset::DataTableSnapshot snapshot);

    [[nodiscard]] static const std::vector<EditorBridgeCapability>& GetCapabilities() noexcept;

private:
    struct ObservedState final {
        EditorState editorState = EditorState::Uninitialized;
        EditorPlayModeState playModeState = EditorPlayModeState::Edit;
        bool sceneDirty = false;
        bool canUndo = false;
        bool canRedo = false;
        std::filesystem::path activeScenePath;
        std::vector<EditorBridgeEntitySnapshot> selectedEntities;
        bool selectedEntitiesTruncated = false;
        std::optional<EditorBridgeEntityRef> activeEntity;
        EditorBridgeHierarchySnapshot hierarchy;
        EditorBridgeInspectorSnapshot inspector;
        EditorBridgeAssetTableSnapshot assetTable;
        EditorBridgeViewportSurfaceSnapshot viewportSurface;
        EditorBridgeDeveloperConsoleSnapshot developerConsole;
        EditorBridgeDataTableCatalogSnapshot dataTableCatalog;
        EditorBridgeDataTablePreviewSnapshot dataTablePreview;

        [[nodiscard]] bool operator==(const ObservedState&) const = default;
    };

    [[nodiscard]] ObservedState CaptureObservedState();
    void SynchronizeRevision();
    [[nodiscard]] EditorBridgeSnapshot BuildSnapshot() const;
    [[nodiscard]] EditorBridgeResponse MakeResponse(const EditorBridgeRequest& request, bool applied,
                                                           std::string code, std::string message) const;
    [[nodiscard]] static Scene::Entity ToEntity(EditorBridgeEntityRef entity) noexcept;
    [[nodiscard]] static EditorBridgeEntityRef ToBridgeEntity(Scene::Entity entity) noexcept;
    [[nodiscard]] bool IsSupportedEntityKind(EditorEntityKind kind) const noexcept;
    [[nodiscard]] static std::string BoundPresentationText(std::string value);
    [[nodiscard]] static std::string BoundContentPath(std::string value);
    [[nodiscard]] EditorBridgeDeveloperConsoleSnapshot CaptureDeveloperConsole() const;
    [[nodiscard]] EditorBridgeDataTableCatalogSnapshot CaptureDataTableCatalog() const;
    [[nodiscard]] EditorBridgeDataTablePreviewSnapshot CaptureDataTablePreview() const;
    [[nodiscard]] EditorBridgeHierarchySnapshot CaptureHierarchy();
    [[nodiscard]] EditorBridgeInspectorSnapshot CaptureInspector() const;
    [[nodiscard]] EditorBridgeAssetTableSnapshot CaptureAssetTable();
    [[nodiscard]] EditorBridgeAssetTableEntry ToContentEntry(
        const Asset::ProjectFileEntry& entry) const;
    [[nodiscard]] static std::optional<Editor::AssetTableTypeFocus> ParseAssetTableFocus(
        const std::string& focus) noexcept;

    Editor* m_editor = nullptr;
    DeveloperConsole m_developerConsole;
    const Asset::DataTableRegistry* m_dataTableRegistry = nullptr;
    std::optional<std::string> m_dataTablePreviewName;
    Asset::DataTableCatalogSnapshot m_dataTableCatalogSnapshot;
    Asset::DataTableSnapshot m_dataTablePreviewSnapshot;
    std::optional<ObservedState> m_lastObservedState;
    std::uint64_t m_revision = 0U;
};

} // namespace UVE::Editor
