// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_bridge_stdio.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace UVE::Editor {
namespace {

using Json = nlohmann::json;

enum class FrameReadResult : std::uint8_t {
    Body,
    EndOfFile,
    TruncatedHeader,
    TruncatedBody,
    ZeroLength,
    Oversized,
};

[[nodiscard]] Json ToJson(const EditorBridgeEntityRef entity) {
    return Json{{"index", entity.index}, {"generation", entity.generation}};
}

[[nodiscard]] Json ToJson(const EditorBridgeEntitySnapshot& entity) {
    return Json{{"entity", ToJson(entity.entity)}, {"displayLabel", entity.displayLabel}};
}

[[nodiscard]] Json ToJson(const EditorBridgeHierarchyEntry& entry) {
    return Json{{"entity", ToJson(entry.entity)},
                   {"parent", entry.parent.has_value() ? ToJson(*entry.parent) : Json(nullptr)},
                   {"displayLabel", entry.displayLabel},
                   {"typeTag", entry.typeTag},
                   {"depth", entry.depth},
                   {"childCount", entry.childCount},
                   {"selected", entry.selected},
                   {"active", entry.active}};
}

[[nodiscard]] Json ToJson(const EditorBridgeHierarchySnapshot& snapshot) {
    Json entries = Json::array();
    for (const EditorBridgeHierarchyEntry& entry : snapshot.entries) {
        entries.push_back(ToJson(entry));
    }
    return Json{{"filter", snapshot.filter},
                   {"filterActive", snapshot.filterActive},
                   {"truncated", snapshot.truncated},
                   {"entries", std::move(entries)}};
}

[[nodiscard]] Json ToJson(const EditorBridgeInspectorSnapshot& snapshot) {
    Json selectedEntities = Json::array();
    for (const EditorBridgeEntitySnapshot& entity : snapshot.selectedEntities) {
        selectedEntities.push_back(ToJson(entity));
    }
    Json ancestry = Json::array();
    for (const EditorBridgeEntitySnapshot& entity : snapshot.ancestry) {
        ancestry.push_back(ToJson(entity));
    }
    Json drawerIds = Json::array();
    for (const std::string& identifier : snapshot.eligibleDrawerIds) {
        drawerIds.push_back(identifier);
    }
    Json attachedComponentIds = Json::array();
    for (const std::string& identifier : snapshot.attachedComponentIds) {
        attachedComponentIds.push_back(identifier);
    }
    Json assetBinding = Json(nullptr);
    Json prefab = Json(nullptr);
    if (snapshot.prefab.has_value()) {
        prefab = Json{{"sourcePrefabGuid", snapshot.prefab->sourcePrefabGuid.has_value()
                                                    ? Json(*snapshot.prefab->sourcePrefabGuid)
                                                    : Json(nullptr)},
                         {"sourceRevision", snapshot.prefab->sourceRevision},
                         {"instanceRevision", snapshot.prefab->instanceRevision},
                         {"overrideCount", snapshot.prefab->overrideCount},
                         {"status", static_cast<std::uint8_t>(snapshot.prefab->status)},
                         {"canRefresh", snapshot.prefab->canRefresh}};
    }
    if (snapshot.assetBinding.has_value()) {
        assetBinding = Json{{"meshGuid", snapshot.assetBinding->meshGuid.has_value()
                                                   ? Json(*snapshot.assetBinding->meshGuid)
                                                   : Json(nullptr)},
                               {"materialGuid", snapshot.assetBinding->materialGuid.has_value()
                                                       ? Json(*snapshot.assetBinding->materialGuid)
                                                       : Json(nullptr)}};
    }
    return Json{{"mode", static_cast<std::uint8_t>(snapshot.mode)},
                   {"selectedEntitiesTruncated", snapshot.selectedEntitiesTruncated},
                   {"selectedEntities", std::move(selectedEntities)},
                   {"activeEntity", snapshot.activeEntity.has_value() ? ToJson(*snapshot.activeEntity)
                                                                         : Json(nullptr)},
                   {"parent", snapshot.parent.has_value() ? ToJson(*snapshot.parent) : Json(nullptr)},
                   {"ancestry", std::move(ancestry)},
                   {"eligibleDrawerIds", std::move(drawerIds)},
                   {"attachedComponentIds", std::move(attachedComponentIds)},
                   {"assetBinding", std::move(assetBinding)},
                   {"prefab", std::move(prefab)},
                   {"canEditSelectedName", snapshot.canEditSelectedName}};
}

[[nodiscard]] Json ToJson(const EditorBridgeAssetTableEntry& entry) {
    return Json{{"relativePath", entry.relativePath},
                   {"isDirectory", entry.isDirectory},
                   {"typeLabel", entry.typeLabel},
                   {"registeredAssetGuid", entry.registeredAssetGuid.has_value()
                                               ? Json(*entry.registeredAssetGuid)
                                               : Json(nullptr)}};
}

[[nodiscard]] Json ToJson(const EditorBridgeContentImportActionSnapshot& action) {
    return Json{{"hasSelection", action.hasSelection},
                   {"canImport", action.canImport},
                   {"canReimport", action.canReimport},
                   {"importerRegistered", action.importerRegistered},
                   {"requiresFormatSpecificParser", action.requiresFormatSpecificParser},
                   {"sourceKind", action.sourceKind},
                   {"diagnostic", action.diagnostic}};
}

[[nodiscard]] Json ToJson(const EditorBridgeAssetTableSnapshot& snapshot) {
    Json entries = Json::array();
    for (const EditorBridgeAssetTableEntry& entry : snapshot.entries) {
        entries.push_back(ToJson(entry));
    }
    Json breadcrumbs = Json::array();
    for (const std::string& breadcrumb : snapshot.breadcrumbs) {
        breadcrumbs.push_back(breadcrumb);
    }
    return Json{{"contentRoot", snapshot.contentRoot},
                   {"currentDirectory", snapshot.currentDirectory},
                   {"filter", snapshot.filter},
                   {"typeFocus", snapshot.typeFocus},
                   {"breadcrumbs", std::move(breadcrumbs)},
                   {"refreshGeneration", snapshot.refreshGeneration},
                   {"visibleEntryCount", snapshot.visibleEntryCount},
                   {"directEntryCount", snapshot.directEntryCount},
                   {"contentRootExists", snapshot.contentRootExists},
                   {"initialized", snapshot.initialized},
                   {"lastRefreshSucceeded", snapshot.lastRefreshSucceeded},
                   {"truncated", snapshot.truncated},
                   {"entries", std::move(entries)},
                   {"selectedEntry", snapshot.selectedEntry.has_value() ? ToJson(*snapshot.selectedEntry)
                                                                           : Json(nullptr)},
                   {"importAction", ToJson(snapshot.importAction)}};
}

[[nodiscard]] Json ToJson(const DeveloperConsoleEntry& entry) {
    return Json{{"severity", static_cast<std::uint8_t>(entry.severity)}, {"text", entry.text}};
}

[[nodiscard]] Json ToJson(const DeveloperConsoleCVar& cvar) {
    return Json{{"name", cvar.name}, {"value", cvar.value}, {"readOnly", cvar.readOnly}};
}

[[nodiscard]] Json ToJson(const DeveloperConsoleCompletion& completion) {
    return Json{{"identifier", completion.identifier}, {"help", completion.help}};
}

[[nodiscard]] Json ToJson(const EditorBridgeDeveloperConsoleSnapshot& snapshot) {
    Json output = Json::array();
    for (const DeveloperConsoleEntry& entry : snapshot.console.output) {
        output.push_back(ToJson(entry));
    }
    Json history = Json::array();
    for (const std::string& command : snapshot.console.history) {
        history.push_back(command);
    }
    Json cvars = Json::array();
    for (const DeveloperConsoleCVar& cvar : snapshot.console.cvars) {
        cvars.push_back(ToJson(cvar));
    }
    Json completions = Json::array();
    for (const DeveloperConsoleCompletion& completion : snapshot.console.completions) {
        completions.push_back(ToJson(completion));
    }
    return Json{{"generation", snapshot.console.generation},
                   {"available", snapshot.console.available},
                   {"developmentOnly", snapshot.console.developmentOnly},
                   {"access", static_cast<std::uint8_t>(snapshot.console.access)},
                   {"severityFilter", static_cast<std::uint8_t>(snapshot.console.severityFilter)},
                   {"historyCursor", snapshot.console.historyCursor},
                   {"historyEntry", snapshot.console.historyEntry},
                   {"outputTruncated", snapshot.console.outputTruncated},
                   {"historyTruncated", snapshot.console.historyTruncated},
                   {"cvarsTruncated", snapshot.console.cvarsTruncated},
                   {"completionTruncated", snapshot.console.completionTruncated},
                   {"output", std::move(output)}, {"history", std::move(history)},
                   {"cvars", std::move(cvars)}, {"completions", std::move(completions)}};
}

[[nodiscard]] Json ToJson(const EditorBridgeDataTableCatalogEntry& entry) {
    return Json{{"name", entry.name},
                   {"generation", entry.generation},
                   {"columnCount", entry.columnCount},
                   {"rowCount", entry.rowCount},
                   {"valid", entry.valid}};
}

[[nodiscard]] Json ToJson(const EditorBridgeDataTableCatalogSnapshot& snapshot) {
    Json entries = Json::array();
    for (const EditorBridgeDataTableCatalogEntry& entry : snapshot.entries) {
        entries.push_back(ToJson(entry));
    }
    return Json{{"generation", snapshot.generation},
                   {"entriesTruncated", snapshot.entriesTruncated},
                   {"entries", std::move(entries)}};
}

[[nodiscard]] Json ToJson(const EditorBridgeDataTablePreviewColumn& column) {
    return Json{{"name", column.name}, {"type", static_cast<std::uint8_t>(column.type)}};
}

[[nodiscard]] Json ToJson(const EditorBridgeDataTablePreviewRow& row) {
    Json values = Json::array();
    for (const std::string& value : row.values) {
        values.push_back(value);
    }
    return Json{{"identifier", row.identifier}, {"values", std::move(values)}};
}

[[nodiscard]] Json ToJson(const EditorBridgeDataTablePreviewSnapshot& snapshot) {
    Json columns = Json::array();
    for (const EditorBridgeDataTablePreviewColumn& column : snapshot.columns) {
        columns.push_back(ToJson(column));
    }
    Json rows = Json::array();
    for (const EditorBridgeDataTablePreviewRow& row : snapshot.rows) {
        rows.push_back(ToJson(row));
    }
    return Json{{"available", snapshot.available},
                   {"generation", snapshot.generation},
                   {"name", snapshot.name},
                   {"totalColumnCount", snapshot.totalColumnCount},
                   {"totalRowCount", snapshot.totalRowCount},
                   {"columnsTruncated", snapshot.columnsTruncated},
                   {"rowsTruncated", snapshot.rowsTruncated},
                   {"valuesTruncated", snapshot.valuesTruncated},
                   {"reason", snapshot.reason},
                   {"columns", std::move(columns)},
                   {"rows", std::move(rows)}};
}

[[nodiscard]] Json ToJson(const EditorBridgeViewportSurfaceSnapshot& surface) {
    return Json{{"state", static_cast<std::uint8_t>(surface.state)},
                   {"generation", surface.generation},
                   {"width", surface.width},
                   {"height", surface.height},
                   {"nativeRendererOwnsSurface", surface.nativeRendererOwnsSurface},
                   {"managedAttachAllowed", surface.managedAttachAllowed},
                   {"reason", surface.reason}};
}

[[nodiscard]] Json ToJson(const EditorBridgeSnapshot& snapshot) {
    Json selectedEntities = Json::array();
    for (const EditorBridgeEntitySnapshot& entity : snapshot.selectedEntities) {
        selectedEntities.push_back(ToJson(entity));
    }
    Json capabilities = Json::array();
    for (const EditorBridgeCapability capability : snapshot.capabilities) {
        capabilities.push_back(static_cast<std::uint8_t>(capability));
    }

    return Json{{"protocolVersion", snapshot.protocolVersion},
                   {"revision", snapshot.revision},
                   {"editorState", static_cast<std::uint8_t>(snapshot.editorState)},
                   {"playModeState", static_cast<std::uint8_t>(snapshot.playModeState)},
                   {"sceneDirty", snapshot.sceneDirty},
                   {"canUndo", snapshot.canUndo},
                   {"canRedo", snapshot.canRedo},
                   {"activeScenePath", snapshot.activeScenePath.generic_string()},
                   {"selectedEntities", std::move(selectedEntities)},
                   {"selectedEntitiesTruncated", snapshot.selectedEntitiesTruncated},
                   {"activeEntity", snapshot.activeEntity.has_value() ? ToJson(*snapshot.activeEntity)
                                                                        : Json(nullptr)},
                   {"hierarchy", ToJson(snapshot.hierarchy)},
                   {"inspector", ToJson(snapshot.inspector)},
                   {"assetTable", ToJson(snapshot.assetTable)},
                   {"viewportSurface", ToJson(snapshot.viewportSurface)},
                   {"developerConsole", ToJson(snapshot.developerConsole)},
                   {"dataTableCatalog", ToJson(snapshot.dataTableCatalog)},
                   {"dataTablePreview", ToJson(snapshot.dataTablePreview)},
                   {"capabilities", std::move(capabilities)}};
}

[[nodiscard]] Json ToJson(const EditorBridgeResponse& response) {
    return Json{{"protocolVersion", response.protocolVersion},
                   {"requestId", response.requestId},
                   {"applied", response.applied},
                   {"code", response.code},
                   {"message", response.message},
                   {"snapshot", ToJson(response.snapshot)},
                   {"createdEntity", response.createdEntity.has_value() ? ToJson(*response.createdEntity)
                                                                         : Json(nullptr)},
                   {"contentImportJobId", response.contentImportJobId.has_value()
                                                ? Json(*response.contentImportJobId)
                                                : Json(nullptr)}};
}

[[nodiscard]] FrameReadResult ReadFrame(std::istream& input, std::string& body) {
    std::array<char, 4U> header{};
    input.read(header.data(), static_cast<std::streamsize>(header.size()));
    const std::streamsize headerBytes = input.gcount();
    if (headerBytes == 0 && input.eof()) {
        return FrameReadResult::EndOfFile;
    }
    if (headerBytes != static_cast<std::streamsize>(header.size())) {
        return FrameReadResult::TruncatedHeader;
    }

    const std::uint32_t length =
        (static_cast<std::uint32_t>(static_cast<unsigned char>(header[0])) << 24U) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(header[1])) << 16U) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(header[2])) << 8U) |
        static_cast<std::uint32_t>(static_cast<unsigned char>(header[3]));
    if (length == 0U) {
        return FrameReadResult::ZeroLength;
    }
    if (length > EditorBridgeStdioServer::kMaximumFrameBytes) {
        return FrameReadResult::Oversized;
    }

    body.assign(length, '\0');
    input.read(body.data(), static_cast<std::streamsize>(body.size()));
    if (input.gcount() != static_cast<std::streamsize>(body.size())) {
        body.clear();
        return FrameReadResult::TruncatedBody;
    }
    return FrameReadResult::Body;
}

[[nodiscard]] bool WriteFrame(std::ostream& output, const Json& payload) {
    const std::string body = payload.dump();
    if (body.empty() || body.size() > EditorBridgeStdioServer::kMaximumFrameBytes) {
        return false;
    }
    const std::uint32_t length = static_cast<std::uint32_t>(body.size());
    const std::array<char, 4U> header{
        static_cast<char>((length >> 24U) & 0xFFU), static_cast<char>((length >> 16U) & 0xFFU),
        static_cast<char>((length >> 8U) & 0xFFU), static_cast<char>(length & 0xFFU)};
    output.write(header.data(), static_cast<std::streamsize>(header.size()));
    output.write(body.data(), static_cast<std::streamsize>(body.size()));
    output.flush();
    return static_cast<bool>(output);
}

[[nodiscard]] Json MakeError(const Json& id, const std::string_view code, const std::string_view message) {
    return Json{{"jsonrpc", "2.0"},
                   {"id", id},
                   {"error", {{"code", -32600}, {"message", message}, {"data", {{"code", code}}}}}};
}

[[nodiscard]] Json MakeResult(const Json& id, Json result) {
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

[[nodiscard]] std::optional<EditorBridgeRequestKind> ParseRequestKind(const std::string_view value) {
    if (value == "readSnapshot") {
        return EditorBridgeRequestKind::ReadSnapshot;
    }
    if (value == "selectEntity") {
        return EditorBridgeRequestKind::SelectEntity;
    }
    if (value == "clearSelection") {
        return EditorBridgeRequestKind::ClearSelection;
    }
    if (value == "setSelectedEntityName") {
        return EditorBridgeRequestKind::SetSelectedEntityName;
    }
    if (value == "createDocumentEntity") {
        return EditorBridgeRequestKind::CreateDocumentEntity;
    }
    if (value == "undo") {
        return EditorBridgeRequestKind::Undo;
    }
    if (value == "redo") {
        return EditorBridgeRequestKind::Redo;
    }
    if (value == "toggleEntitySelection") {
        return EditorBridgeRequestKind::ToggleEntitySelection;
    }
    if (value == "setHierarchyFilter") {
        return EditorBridgeRequestKind::SetHierarchyFilter;
    }
    if (value == "setAssetTableDirectory") {
        return EditorBridgeRequestKind::SetAssetTableDirectory;
    }
    if (value == "setAssetTableFilter") {
        return EditorBridgeRequestKind::SetAssetTableFilter;
    }
    if (value == "setAssetTableFocus") {
        return EditorBridgeRequestKind::SetAssetTableFocus;
    }
    if (value == "refreshAssetTable") {
        return EditorBridgeRequestKind::RefreshAssetTable;
    }
    if (value == "selectAssetTableEntry") {
        return EditorBridgeRequestKind::SelectAssetTableEntry;
    }
    if (value == "queueAssetTableImport") {
        return EditorBridgeRequestKind::QueueAssetTableImport;
    }
    if (value == "readDeveloperConsole") {
        return EditorBridgeRequestKind::ReadDeveloperConsole;
    }
    if (value == "submitDeveloperConsoleCommand") {
        return EditorBridgeRequestKind::SubmitDeveloperConsoleCommand;
    }
    if (value == "clearDeveloperConsole") {
        return EditorBridgeRequestKind::ClearDeveloperConsole;
    }
    if (value == "setDeveloperConsoleSeverityFilter") {
        return EditorBridgeRequestKind::SetDeveloperConsoleSeverityFilter;
    }
    if (value == "setDeveloperConsoleCompletionPrefix") {
        return EditorBridgeRequestKind::SetDeveloperConsoleCompletionPrefix;
    }
    if (value == "moveDeveloperConsoleHistory") {
        return EditorBridgeRequestKind::MoveDeveloperConsoleHistory;
    }
    if (value == "selectDataTablePreview") {
        return EditorBridgeRequestKind::SelectDataTablePreview;
    }
    return std::nullopt;
}


[[nodiscard]] std::optional<EditorBridgeEntityRef> ParseEntity(const Json& json) {
    if (!json.is_object() || !json.contains("index") || !json.contains("generation")) {
        return std::nullopt;
    }
    return EditorBridgeEntityRef{json.at("index").get<std::uint32_t>(),
                                    json.at("generation").get<std::uint32_t>()};
}



[[nodiscard]] std::optional<EditorBridgeRequest> ParseBridgeRequest(const Json& params) {
    if (!params.is_object()) {
        return std::nullopt;
    }
    EditorBridgeRequest request{};
    request.protocolVersion = params.at("protocolVersion").get<std::uint32_t>();
    request.requestId = params.at("requestId").get<std::uint64_t>();
    request.expectedRevision = params.at("expectedRevision").get<std::uint64_t>();
    const std::optional<EditorBridgeRequestKind> kind = ParseRequestKind(params.at("kind").get<std::string>());
    if (!kind.has_value()) {
        return std::nullopt;
    }
    request.kind = *kind;
    if (params.contains("entity") && !params.at("entity").is_null()) {
        request.entity = ParseEntity(params.at("entity"));
        if (!request.entity.has_value()) {
            return std::nullopt;
        }
    }
    if (params.contains("entityName") && !params.at("entityName").is_null()) {
        request.entityName = params.at("entityName").get<std::string>();
    }
    if (params.contains("entityKind") && !params.at("entityKind").is_null()) {
        request.entityKind = ParseEditorEntityKind(params.at("entityKind").get<std::string>());
        if (!request.entityKind.has_value()) {
            return std::nullopt;
        }
    }
    if (params.contains("hierarchyFilter") && !params.at("hierarchyFilter").is_null()) {
        request.hierarchyFilter = params.at("hierarchyFilter").get<std::string>();
    }
    if (params.contains("contentDirectory") && !params.at("contentDirectory").is_null()) {
        request.contentDirectory = params.at("contentDirectory").get<std::string>();
    }
    if (params.contains("contentFilter") && !params.at("contentFilter").is_null()) {
        request.contentFilter = params.at("contentFilter").get<std::string>();
    }
    if (params.contains("contentFocus") && !params.at("contentFocus").is_null()) {
        request.contentFocus = params.at("contentFocus").get<std::string>();
    }
    if (params.contains("contentEntryPath") && !params.at("contentEntryPath").is_null()) {
        request.contentEntryPath = params.at("contentEntryPath").get<std::string>();
    }
    if (params.contains("contentImportDestinationPath") && !params.at("contentImportDestinationPath").is_null()) {
        request.contentImportDestinationPath = params.at("contentImportDestinationPath").get<std::string>();
    }
    if (params.contains("dataTableName") && !params.at("dataTableName").is_null()) {
        request.dataTableName = params.at("dataTableName").get<std::string>();
    }
    if (params.contains("developerConsoleCommand") && !params.at("developerConsoleCommand").is_null()) {
        request.developerConsoleCommand = params.at("developerConsoleCommand").get<std::string>();
    }
    if (params.contains("developerConsoleSeverityFilter") && !params.at("developerConsoleSeverityFilter").is_null()) {
        const std::uint8_t filter = params.at("developerConsoleSeverityFilter").get<std::uint8_t>();
        if (filter > static_cast<std::uint8_t>(DeveloperConsoleSeverityFilter::Error)) {
            return std::nullopt;
        }
        request.developerConsoleSeverityFilter = static_cast<DeveloperConsoleSeverityFilter>(filter);
    }
    if (params.contains("developerConsoleCompletionPrefix") && !params.at("developerConsoleCompletionPrefix").is_null()) {
        request.developerConsoleCompletionPrefix = params.at("developerConsoleCompletionPrefix").get<std::string>();
    }
    if (params.contains("developerConsoleHistoryDelta") && !params.at("developerConsoleHistoryDelta").is_null()) {
        request.developerConsoleHistoryDelta = params.at("developerConsoleHistoryDelta").get<std::int32_t>();
    }
    return request;
}

[[nodiscard]] bool IsJsonRpcRequest(const Json& request) {
    return request.is_object() && request.value("jsonrpc", "") == "2.0" && request.contains("method") &&
           request.at("method").is_string();
}

[[nodiscard]] Json DispatchJsonRequest(EditorBridge& bridge, const Json& request) {
    const Json id = request.value("id", Json(nullptr));
    if (!IsJsonRpcRequest(request)) {
        return MakeError(id, "bridge.request.invalid", "The request is not a JSON-RPC 2.0 object.");
    }

    const std::string method = request.at("method").get<std::string>();
    const Json params = request.value("params", Json::object());
    try {
        if (method == "bridge.hello") {
            const std::uint32_t requestedVersion = params.at("protocolVersion").get<std::uint32_t>();
            const EditorBridgeSnapshot snapshot = bridge.GetSnapshot();
            const bool compatible = requestedVersion == kEditorBridgeProtocolVersion;
            return MakeResult(id, Json{{"code", compatible ? "bridge.hello.compatible"
                                                                  : "bridge.protocol.unsupported"},
                                             {"compatible", compatible},
                                             {"protocolVersion", kEditorBridgeProtocolVersion},
                                             {"snapshot", ToJson(snapshot)}});
        }
        if (method == "bridge.getSnapshot") {
            return MakeResult(id, Json{{"code", "bridge.snapshot.read"},
                                             {"snapshot", ToJson(bridge.GetSnapshot())}});
        }
        if (method == "bridge.dispatch") {
            const std::optional<EditorBridgeRequest> parsed = ParseBridgeRequest(params);
            if (!parsed.has_value()) {
                return MakeError(id, "bridge.request.invalid", "The bridge dispatch payload is invalid.");
            }
            return MakeResult(id, ToJson(bridge.Dispatch(*parsed)));
        }
    } catch (const Json::exception&) {
        return MakeError(id, "bridge.request.invalid", "The request contains invalid bridge field values.");
    }
    return MakeError(id, "bridge.request.invalid", "The bridge method is not supported.");
}

[[nodiscard]] std::string_view FrameReadDiagnostic(const FrameReadResult result) {
    switch (result) {
        case FrameReadResult::TruncatedHeader:
            return "bridge.transport.frame.truncated_header";
        case FrameReadResult::TruncatedBody:
            return "bridge.transport.frame.truncated_body";
        case FrameReadResult::ZeroLength:
            return "bridge.transport.frame.zero_length";
        case FrameReadResult::Oversized:
            return "bridge.transport.frame.oversized";
        case FrameReadResult::Body:
        case FrameReadResult::EndOfFile:
            return "bridge.transport.frame.invalid";
    }
    return "bridge.transport.frame.invalid";
}

} // namespace

EditorBridgeStdioServer::EditorBridgeStdioServer(EditorBridge& bridge) noexcept : m_bridge(&bridge) {}

int EditorBridgeStdioServer::Serve(std::istream& input, std::ostream& output, std::ostream& diagnostics) {
    while (true) {
        std::string body;
        const FrameReadResult frameResult = ReadFrame(input, body);
        if (frameResult == FrameReadResult::EndOfFile) {
            return 0;
        }
        if (frameResult != FrameReadResult::Body) {
            diagnostics << FrameReadDiagnostic(frameResult) << '\n';
            static_cast<void>(WriteFrame(output, MakeError(Json(nullptr), FrameReadDiagnostic(frameResult),
                                                                  "The bridge frame is malformed or outside protocol bounds.")));
            return 2;
        }

        try {
            const Json request = Json::parse(body);
            if (!WriteFrame(output, DispatchJsonRequest(*m_bridge, request))) {
                diagnostics << "bridge.transport.write.failed\n";
                return 3;
            }
        } catch (const Json::exception&) {
            diagnostics << "bridge.transport.json.invalid\n";
            if (!WriteFrame(output, MakeError(Json(nullptr), "bridge.transport.json.invalid",
                                                     "The bridge frame body is not valid UTF-8 JSON."))) {
                return 3;
            }
        }
    }
}

} // namespace UVE::Editor
