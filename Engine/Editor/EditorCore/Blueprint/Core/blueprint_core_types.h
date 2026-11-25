// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "blueprint_asset.h"

namespace UVE::Blueprint::Core {

// ---------------------------------------------------------------------------
// Identifiers
// ---------------------------------------------------------------------------

using BlueprintNodeId = std::uint32_t;
using BlueprintPinId = std::uint32_t;
using BlueprintConnectionId = std::uint32_t;
using BlueprintGraphId = std::uint64_t;

inline constexpr BlueprintNodeId kInvalidBlueprintNodeId = 0U;
inline constexpr BlueprintConnectionId kInvalidBlueprintConnectionId = 0U;

/// Monotonic, collision-free identifier generator for Blueprint graph elements.
class BlueprintIdGenerator final {
public:
    explicit BlueprintIdGenerator(std::uint32_t nextId = 1U) noexcept;

    /// Seeds the generator above every existing node ID in `asset`.
    void SeedFromAsset(const Asset::BlueprintAsset& asset) noexcept;

    /// Reserves `existingId` so future allocations will never collide with it.
    void Reserve(std::uint32_t existingId) noexcept;

    [[nodiscard]] std::uint32_t Allocate() noexcept;
    [[nodiscard]] std::uint32_t PeekNext() const noexcept { return m_nextId; }

private:
    std::uint32_t m_nextId = 1U;
};

// ---------------------------------------------------------------------------
// TypeSystem
// ---------------------------------------------------------------------------

enum class BlueprintTypeCategory : std::uint8_t {
    Execution = 0,
    Scalar,
    Math,
    Text,
    Reference,
    Collection,
};

struct BlueprintTypeDescriptor final {
    Asset::BlueprintPinType type = Asset::BlueprintPinType::Float;
    BlueprintTypeCategory category = BlueprintTypeCategory::Scalar;
    std::string_view displayName;
    std::string_view scriptTypeName;
    std::string_view defaultLiteral;
    bool isArray = false;
    Asset::BlueprintPinType elementType = Asset::BlueprintPinType::Float;
};

[[nodiscard]] BlueprintTypeDescriptor GetBlueprintPinTypeDescriptor(Asset::BlueprintPinType type) noexcept;
[[nodiscard]] std::string_view BlueprintPinTypeDisplayName(Asset::BlueprintPinType type) noexcept;
[[nodiscard]] std::string_view BlueprintPinTypeDefaultLiteral(Asset::BlueprintPinType type) noexcept;

/// True when an output pin of type `from` can wire into an input pin of type `to`.
/// Allows exact matches plus lossless numeric promotion (Int -> Float).
[[nodiscard]] bool CanConnectBlueprintPinTypes(Asset::BlueprintPinType from,
                                                  Asset::BlueprintPinType to) noexcept;

// ---------------------------------------------------------------------------
// Metadata
// ---------------------------------------------------------------------------

struct BlueprintMetadata final {
    std::string displayName;
    std::string category;
    std::string description;
    std::string author;
    std::string tooltip;
    bool deprecated = false;
    std::string deprecationMessage;
    std::map<std::string, std::string, std::less<>> tags;

    [[nodiscard]] bool HasTag(std::string_view key) const noexcept;
    [[nodiscard]] std::string GetTag(std::string_view key, std::string_view fallback = {}) const;
    void SetTag(std::string key, std::string value);

    [[nodiscard]] bool operator==(const BlueprintMetadata&) const = default;
};

// ---------------------------------------------------------------------------
// Pin, Node, Connection
// ---------------------------------------------------------------------------

enum class BlueprintPinDirection : std::uint8_t {
    Input = 0,
    Output,
};

struct BlueprintCorePin final {
    BlueprintPinId index = 0U;
    std::string name;
    Asset::BlueprintPinType type = Asset::BlueprintPinType::Exec;
    BlueprintPinDirection direction = BlueprintPinDirection::Input;
    std::string defaultValue;

    [[nodiscard]] bool operator==(const BlueprintCorePin&) const = default;
};

struct BlueprintCoreNode final {
    BlueprintNodeId id = kInvalidBlueprintNodeId;
    std::string typeKey;
    std::string title;
    std::string variableName;
    float x = 0.0F;
    float y = 0.0F;
    std::vector<BlueprintCorePin> inputs;
    std::vector<BlueprintCorePin> outputs;
    BlueprintMetadata metadata;

    [[nodiscard]] const BlueprintCorePin* FindInputPin(BlueprintPinId index) const noexcept;
    [[nodiscard]] const BlueprintCorePin* FindOutputPin(BlueprintPinId index) const noexcept;
};

struct BlueprintCoreConnection final {
    BlueprintConnectionId id = kInvalidBlueprintConnectionId;
    BlueprintNodeId fromNode = kInvalidBlueprintNodeId;
    BlueprintPinId fromPin = 0U;
    BlueprintNodeId toNode = kInvalidBlueprintNodeId;
    BlueprintPinId toPin = 0U;

    [[nodiscard]] bool operator==(const BlueprintCoreConnection&) const = default;
};

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

enum class BlueprintCoreDiagnosticSeverity : std::uint8_t {
    Info = 0,
    Warning,
    Error,
};

struct BlueprintCoreDiagnostic final {
    BlueprintCoreDiagnosticSeverity severity = BlueprintCoreDiagnosticSeverity::Error;
    BlueprintNodeId nodeId = kInvalidBlueprintNodeId;
    std::optional<BlueprintPinId> pinIndex;
    std::string code;
    std::string message;
};

class BlueprintDiagnosticSink final {
public:
    void Emit(BlueprintCoreDiagnosticSeverity severity, BlueprintNodeId nodeId, std::string code,
                 std::string message, std::optional<BlueprintPinId> pinIndex = std::nullopt);
    void Clear() noexcept;
    [[nodiscard]] bool HasErrors() const noexcept;
    [[nodiscard]] std::size_t ErrorCount() const noexcept;
    [[nodiscard]] std::size_t WarningCount() const noexcept;
    [[nodiscard]] const std::vector<BlueprintCoreDiagnostic>& Entries() const noexcept { return m_entries; }

private:
    std::vector<BlueprintCoreDiagnostic> m_entries;
};

// ---------------------------------------------------------------------------
// Events & Interfaces
// ---------------------------------------------------------------------------

enum class BlueprintGraphChangeKind : std::uint8_t {
    NodeAdded = 0,
    NodeRemoved,
    NodeMoved,
    ConnectionAdded,
    ConnectionRemoved,
    VariableChanged,
    MetadataChanged,
};

struct BlueprintGraphEvent final {
    BlueprintGraphChangeKind kind = BlueprintGraphChangeKind::NodeAdded;
    BlueprintNodeId nodeId = kInvalidBlueprintNodeId;
    BlueprintConnectionId connectionId = kInvalidBlueprintConnectionId;
    std::string detail;
};

class IBlueprintGraphListener {
public:
    virtual ~IBlueprintGraphListener() = default;
    virtual void OnBlueprintGraphChanged(const BlueprintGraphEvent& event) = 0;
};

class BlueprintCoreGraph;

class IBlueprintNodeValidator {
public:
    virtual ~IBlueprintNodeValidator() = default;
    virtual void ValidateNode(const BlueprintCoreGraph& graph, const BlueprintCoreNode& node,
                                 BlueprintDiagnosticSink& sink) const = 0;
};

// ---------------------------------------------------------------------------
// Graph
// ---------------------------------------------------------------------------

struct BlueprintConnectionResult final {
    bool connected = false;
    BlueprintConnectionId connectionId = kInvalidBlueprintConnectionId;
    std::string reason;
};

class BlueprintCoreGraph final {
public:
    explicit BlueprintCoreGraph(std::string name = "EventGraph", std::string parentKind = "Object3D");

    [[nodiscard]] const std::string& Name() const noexcept { return m_name; }
    void SetName(std::string name);

    [[nodiscard]] const std::string& ParentKind() const noexcept { return m_parentKind; }
    void SetParentKind(std::string parentKind);

    [[nodiscard]] const BlueprintMetadata& Metadata() const noexcept { return m_metadata; }
    void SetMetadata(BlueprintMetadata metadata);

    void AddListener(IBlueprintGraphListener* listener);
    void RemoveListener(IBlueprintGraphListener* listener);

    BlueprintNodeId AddNode(BlueprintCoreNode node);
    bool RemoveNode(BlueprintNodeId nodeId);
    bool MoveNode(BlueprintNodeId nodeId, float x, float y);

    [[nodiscard]] BlueprintCoreNode* FindNode(BlueprintNodeId nodeId) noexcept;
    [[nodiscard]] const BlueprintCoreNode* FindNode(BlueprintNodeId nodeId) const noexcept;
    [[nodiscard]] const std::vector<BlueprintCoreNode>& Nodes() const noexcept { return m_nodes; }

    BlueprintConnectionResult Connect(BlueprintNodeId fromNode, BlueprintPinId fromPin,
                                            BlueprintNodeId toNode, BlueprintPinId toPin);
    bool Disconnect(BlueprintConnectionId connectionId);
    bool DisconnectPins(BlueprintNodeId fromNode, BlueprintPinId fromPin,
                           BlueprintNodeId toNode, BlueprintPinId toPin);
    [[nodiscard]] const std::vector<BlueprintCoreConnection>& Connections() const noexcept {
        return m_connections;
    }

    [[nodiscard]] std::vector<BlueprintCoreConnection> GetIncomingConnections(BlueprintNodeId nodeId) const;
    [[nodiscard]] std::vector<BlueprintCoreConnection> GetOutgoingConnections(BlueprintNodeId nodeId) const;

    void SetVariables(std::vector<Asset::BlueprintVariable> variables);
    [[nodiscard]] const std::vector<Asset::BlueprintVariable>& Variables() const noexcept {
        return m_variables;
    }

    void SetComments(std::vector<Asset::BlueprintComment> comments);
    [[nodiscard]] const std::vector<Asset::BlueprintComment>& Comments() const noexcept { return m_comments; }

    /// True if any directed cycle exists in the graph's connections.
    [[nodiscard]] bool HasCycle() const;

    /// Topological ordering of all node IDs when acyclic; empty if a cycle is present.
    [[nodiscard]] std::vector<BlueprintNodeId> TopologicalSortNodes() const;

    /// Runs `validator` over every node in the graph.
    void ValidateWith(const IBlueprintNodeValidator& validator, BlueprintDiagnosticSink& sink) const;

private:
    void Notify(const BlueprintGraphEvent& event);

    std::string m_name;
    std::string m_parentKind;
    BlueprintMetadata m_metadata;
    BlueprintIdGenerator m_nodeIds;
    BlueprintIdGenerator m_connectionIds;
    std::vector<BlueprintCoreNode> m_nodes;
    std::vector<BlueprintCoreConnection> m_connections;
    std::vector<Asset::BlueprintVariable> m_variables;
    std::vector<Asset::BlueprintComment> m_comments;
    std::vector<IBlueprintGraphListener*> m_listeners;
};

/// Lossless conversion between the persisted BlueprintAsset and the editable Core graph model.
[[nodiscard]] BlueprintCoreGraph BlueprintCoreGraphFromAsset(const Asset::BlueprintAsset& asset,
                                                                   std::string name = "EventGraph");
[[nodiscard]] Asset::BlueprintAsset BlueprintCoreGraphToAsset(const BlueprintCoreGraph& graph);

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------

struct BlueprintPropertyReflection final {
    std::string name;
    Asset::BlueprintPinType type = Asset::BlueprintPinType::Float;
    bool readOnly = false;
    bool instanceEditable = true;
    std::string defaultValue;
    std::string tooltip;
};

struct BlueprintFunctionReflection final {
    std::string name;
    std::string category;
    bool isPure = false;
    std::vector<BlueprintCorePin> inputs;
    std::vector<BlueprintCorePin> outputs;
    std::string tooltip;
};

struct BlueprintClassReflection final {
    std::string className;
    std::string parentClassName;
    std::vector<std::string> implementedInterfaces;
    std::vector<BlueprintPropertyReflection> properties;
    std::vector<BlueprintFunctionReflection> functions;
    BlueprintMetadata metadata;
};

class BlueprintReflectionRegistry final {
public:
    bool RegisterClass(BlueprintClassReflection descriptor);
    bool UnregisterClass(std::string_view className);
    [[nodiscard]] const BlueprintClassReflection* FindClass(std::string_view className) const noexcept;
    [[nodiscard]] bool IsSubclassOf(std::string_view childClass, std::string_view baseClass) const noexcept;
    [[nodiscard]] std::vector<std::string> GetRegisteredClassNames() const;
    void Clear() noexcept;

private:
    std::map<std::string, BlueprintClassReflection, std::less<>> m_classes;
};

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

struct BlueprintGraphBounds final {
    bool valid = false;
    float minX = 0.0F;
    float minY = 0.0F;
    float maxX = 0.0F;
    float maxY = 0.0F;

    [[nodiscard]] float Width() const noexcept { return valid ? (maxX - minX) : 0.0F; }
    [[nodiscard]] float Height() const noexcept { return valid ? (maxY - minY) : 0.0F; }
};

[[nodiscard]] std::string SanitizeBlueprintIdentifier(std::string_view text);
[[nodiscard]] std::string FormatBlueprintNodeTitle(std::string_view rawKey);
[[nodiscard]] BlueprintGraphBounds ComputeBlueprintGraphBounds(const BlueprintCoreGraph& graph) noexcept;
void AutoLayoutBlueprintGraph(BlueprintCoreGraph& graph, float horizontalSpacing = 280.0F,
                                 float verticalSpacing = 140.0F);

} // namespace UVE::Blueprint::Core
