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

using BlueprintNodeIdUVE = std::uint32_t;
using BlueprintPinIdUVE = std::uint32_t;
using BlueprintConnectionIdUVE = std::uint32_t;
using BlueprintGraphIdUVE = std::uint64_t;

inline constexpr BlueprintNodeIdUVE kInvalidBlueprintNodeIdUVE = 0U;
inline constexpr BlueprintConnectionIdUVE kInvalidBlueprintConnectionIdUVE = 0U;

/// Monotonic, collision-free identifier generator for Blueprint graph elements.
class BlueprintIdGeneratorUVE final {
public:
    explicit BlueprintIdGeneratorUVE(std::uint32_t nextId = 1U) noexcept;

    /// Seeds the generator above every existing node ID in `asset`.
    void SeedFromAssetUVE(const Asset::BlueprintAssetUVE& asset) noexcept;

    /// Reserves `existingId` so future allocations will never collide with it.
    void ReserveUVE(std::uint32_t existingId) noexcept;

    [[nodiscard]] std::uint32_t AllocateUVE() noexcept;
    [[nodiscard]] std::uint32_t PeekNextUVE() const noexcept { return m_nextId; }

private:
    std::uint32_t m_nextId = 1U;
};

// ---------------------------------------------------------------------------
// TypeSystem
// ---------------------------------------------------------------------------

enum class BlueprintTypeCategoryUVE : std::uint8_t {
    Execution = 0,
    Scalar,
    Math,
    Text,
    Reference,
    Collection,
};

struct BlueprintTypeDescriptorUVE final {
    Asset::BlueprintPinTypeUVE type = Asset::BlueprintPinTypeUVE::Float;
    BlueprintTypeCategoryUVE category = BlueprintTypeCategoryUVE::Scalar;
    std::string_view displayName;
    std::string_view scriptTypeName;
    std::string_view defaultLiteral;
    bool isArray = false;
    Asset::BlueprintPinTypeUVE elementType = Asset::BlueprintPinTypeUVE::Float;
};

[[nodiscard]] BlueprintTypeDescriptorUVE GetBlueprintPinTypeDescriptorUVE(Asset::BlueprintPinTypeUVE type) noexcept;
[[nodiscard]] std::string_view BlueprintPinTypeDisplayNameUVE(Asset::BlueprintPinTypeUVE type) noexcept;
[[nodiscard]] std::string_view BlueprintPinTypeDefaultLiteralUVE(Asset::BlueprintPinTypeUVE type) noexcept;

/// True when an output pin of type `from` can wire into an input pin of type `to`.
/// Allows exact matches plus lossless numeric promotion (Int -> Float).
[[nodiscard]] bool CanConnectBlueprintPinTypesUVE(Asset::BlueprintPinTypeUVE from,
                                                  Asset::BlueprintPinTypeUVE to) noexcept;

// ---------------------------------------------------------------------------
// Metadata
// ---------------------------------------------------------------------------

struct BlueprintMetadataUVE final {
    std::string displayName;
    std::string category;
    std::string description;
    std::string author;
    std::string tooltip;
    bool deprecated = false;
    std::string deprecationMessage;
    std::map<std::string, std::string, std::less<>> tags;

    [[nodiscard]] bool HasTagUVE(std::string_view key) const noexcept;
    [[nodiscard]] std::string GetTagUVE(std::string_view key, std::string_view fallback = {}) const;
    void SetTagUVE(std::string key, std::string value);

    [[nodiscard]] bool operator==(const BlueprintMetadataUVE&) const = default;
};

// ---------------------------------------------------------------------------
// Pin, Node, Connection
// ---------------------------------------------------------------------------

enum class BlueprintPinDirectionUVE : std::uint8_t {
    Input = 0,
    Output,
};

struct BlueprintCorePinUVE final {
    BlueprintPinIdUVE index = 0U;
    std::string name;
    Asset::BlueprintPinTypeUVE type = Asset::BlueprintPinTypeUVE::Exec;
    BlueprintPinDirectionUVE direction = BlueprintPinDirectionUVE::Input;
    std::string defaultValue;

    [[nodiscard]] bool operator==(const BlueprintCorePinUVE&) const = default;
};

struct BlueprintCoreNodeUVE final {
    BlueprintNodeIdUVE id = kInvalidBlueprintNodeIdUVE;
    std::string typeKey;
    std::string title;
    std::string variableName;
    float x = 0.0F;
    float y = 0.0F;
    std::vector<BlueprintCorePinUVE> inputs;
    std::vector<BlueprintCorePinUVE> outputs;
    BlueprintMetadataUVE metadata;

    [[nodiscard]] const BlueprintCorePinUVE* FindInputPinUVE(BlueprintPinIdUVE index) const noexcept;
    [[nodiscard]] const BlueprintCorePinUVE* FindOutputPinUVE(BlueprintPinIdUVE index) const noexcept;
};

struct BlueprintCoreConnectionUVE final {
    BlueprintConnectionIdUVE id = kInvalidBlueprintConnectionIdUVE;
    BlueprintNodeIdUVE fromNode = kInvalidBlueprintNodeIdUVE;
    BlueprintPinIdUVE fromPin = 0U;
    BlueprintNodeIdUVE toNode = kInvalidBlueprintNodeIdUVE;
    BlueprintPinIdUVE toPin = 0U;

    [[nodiscard]] bool operator==(const BlueprintCoreConnectionUVE&) const = default;
};

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

enum class BlueprintCoreDiagnosticSeverityUVE : std::uint8_t {
    Info = 0,
    Warning,
    Error,
};

struct BlueprintCoreDiagnosticUVE final {
    BlueprintCoreDiagnosticSeverityUVE severity = BlueprintCoreDiagnosticSeverityUVE::Error;
    BlueprintNodeIdUVE nodeId = kInvalidBlueprintNodeIdUVE;
    std::optional<BlueprintPinIdUVE> pinIndex;
    std::string code;
    std::string message;
};

class BlueprintDiagnosticSinkUVE final {
public:
    void EmitUVE(BlueprintCoreDiagnosticSeverityUVE severity, BlueprintNodeIdUVE nodeId, std::string code,
                 std::string message, std::optional<BlueprintPinIdUVE> pinIndex = std::nullopt);
    void ClearUVE() noexcept;
    [[nodiscard]] bool HasErrorsUVE() const noexcept;
    [[nodiscard]] std::size_t ErrorCountUVE() const noexcept;
    [[nodiscard]] std::size_t WarningCountUVE() const noexcept;
    [[nodiscard]] const std::vector<BlueprintCoreDiagnosticUVE>& EntriesUVE() const noexcept { return m_entries; }

private:
    std::vector<BlueprintCoreDiagnosticUVE> m_entries;
};

// ---------------------------------------------------------------------------
// Events & Interfaces
// ---------------------------------------------------------------------------

enum class BlueprintGraphChangeKindUVE : std::uint8_t {
    NodeAdded = 0,
    NodeRemoved,
    NodeMoved,
    ConnectionAdded,
    ConnectionRemoved,
    VariableChanged,
    MetadataChanged,
};

struct BlueprintGraphEventUVE final {
    BlueprintGraphChangeKindUVE kind = BlueprintGraphChangeKindUVE::NodeAdded;
    BlueprintNodeIdUVE nodeId = kInvalidBlueprintNodeIdUVE;
    BlueprintConnectionIdUVE connectionId = kInvalidBlueprintConnectionIdUVE;
    std::string detail;
};

class IBlueprintGraphListenerUVE {
public:
    virtual ~IBlueprintGraphListenerUVE() = default;
    virtual void OnBlueprintGraphChangedUVE(const BlueprintGraphEventUVE& event) = 0;
};

class BlueprintCoreGraphUVE;

class IBlueprintNodeValidatorUVE {
public:
    virtual ~IBlueprintNodeValidatorUVE() = default;
    virtual void ValidateNodeUVE(const BlueprintCoreGraphUVE& graph, const BlueprintCoreNodeUVE& node,
                                 BlueprintDiagnosticSinkUVE& sink) const = 0;
};

// ---------------------------------------------------------------------------
// Graph
// ---------------------------------------------------------------------------

struct BlueprintConnectionResultUVE final {
    bool connected = false;
    BlueprintConnectionIdUVE connectionId = kInvalidBlueprintConnectionIdUVE;
    std::string reason;
};

class BlueprintCoreGraphUVE final {
public:
    explicit BlueprintCoreGraphUVE(std::string name = "EventGraph", std::string parentKind = "Object3D");

    [[nodiscard]] const std::string& NameUVE() const noexcept { return m_name; }
    void SetNameUVE(std::string name);

    [[nodiscard]] const std::string& ParentKindUVE() const noexcept { return m_parentKind; }
    void SetParentKindUVE(std::string parentKind);

    [[nodiscard]] const BlueprintMetadataUVE& MetadataUVE() const noexcept { return m_metadata; }
    void SetMetadataUVE(BlueprintMetadataUVE metadata);

    void AddListenerUVE(IBlueprintGraphListenerUVE* listener);
    void RemoveListenerUVE(IBlueprintGraphListenerUVE* listener);

    BlueprintNodeIdUVE AddNodeUVE(BlueprintCoreNodeUVE node);
    bool RemoveNodeUVE(BlueprintNodeIdUVE nodeId);
    bool MoveNodeUVE(BlueprintNodeIdUVE nodeId, float x, float y);

    [[nodiscard]] BlueprintCoreNodeUVE* FindNodeUVE(BlueprintNodeIdUVE nodeId) noexcept;
    [[nodiscard]] const BlueprintCoreNodeUVE* FindNodeUVE(BlueprintNodeIdUVE nodeId) const noexcept;
    [[nodiscard]] const std::vector<BlueprintCoreNodeUVE>& NodesUVE() const noexcept { return m_nodes; }

    BlueprintConnectionResultUVE ConnectUVE(BlueprintNodeIdUVE fromNode, BlueprintPinIdUVE fromPin,
                                            BlueprintNodeIdUVE toNode, BlueprintPinIdUVE toPin);
    bool DisconnectUVE(BlueprintConnectionIdUVE connectionId);
    bool DisconnectPinsUVE(BlueprintNodeIdUVE fromNode, BlueprintPinIdUVE fromPin,
                           BlueprintNodeIdUVE toNode, BlueprintPinIdUVE toPin);
    [[nodiscard]] const std::vector<BlueprintCoreConnectionUVE>& ConnectionsUVE() const noexcept {
        return m_connections;
    }

    [[nodiscard]] std::vector<BlueprintCoreConnectionUVE> GetIncomingConnectionsUVE(BlueprintNodeIdUVE nodeId) const;
    [[nodiscard]] std::vector<BlueprintCoreConnectionUVE> GetOutgoingConnectionsUVE(BlueprintNodeIdUVE nodeId) const;

    void SetVariablesUVE(std::vector<Asset::BlueprintVariableUVE> variables);
    [[nodiscard]] const std::vector<Asset::BlueprintVariableUVE>& VariablesUVE() const noexcept {
        return m_variables;
    }

    void SetCommentsUVE(std::vector<Asset::BlueprintCommentUVE> comments);
    [[nodiscard]] const std::vector<Asset::BlueprintCommentUVE>& CommentsUVE() const noexcept { return m_comments; }

    /// True if any directed cycle exists in the graph's connections.
    [[nodiscard]] bool HasCycleUVE() const;

    /// Topological ordering of all node IDs when acyclic; empty if a cycle is present.
    [[nodiscard]] std::vector<BlueprintNodeIdUVE> TopologicalSortNodesUVE() const;

    /// Runs `validator` over every node in the graph.
    void ValidateWithUVE(const IBlueprintNodeValidatorUVE& validator, BlueprintDiagnosticSinkUVE& sink) const;

private:
    void NotifyUVE(const BlueprintGraphEventUVE& event);

    std::string m_name;
    std::string m_parentKind;
    BlueprintMetadataUVE m_metadata;
    BlueprintIdGeneratorUVE m_nodeIds;
    BlueprintIdGeneratorUVE m_connectionIds;
    std::vector<BlueprintCoreNodeUVE> m_nodes;
    std::vector<BlueprintCoreConnectionUVE> m_connections;
    std::vector<Asset::BlueprintVariableUVE> m_variables;
    std::vector<Asset::BlueprintCommentUVE> m_comments;
    std::vector<IBlueprintGraphListenerUVE*> m_listeners;
};

/// Lossless conversion between the persisted BlueprintAssetUVE and the editable Core graph model.
[[nodiscard]] BlueprintCoreGraphUVE BlueprintCoreGraphFromAssetUVE(const Asset::BlueprintAssetUVE& asset,
                                                                   std::string name = "EventGraph");
[[nodiscard]] Asset::BlueprintAssetUVE BlueprintCoreGraphToAssetUVE(const BlueprintCoreGraphUVE& graph);

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------

struct BlueprintPropertyReflectionUVE final {
    std::string name;
    Asset::BlueprintPinTypeUVE type = Asset::BlueprintPinTypeUVE::Float;
    bool readOnly = false;
    bool instanceEditable = true;
    std::string defaultValue;
    std::string tooltip;
};

struct BlueprintFunctionReflectionUVE final {
    std::string name;
    std::string category;
    bool isPure = false;
    std::vector<BlueprintCorePinUVE> inputs;
    std::vector<BlueprintCorePinUVE> outputs;
    std::string tooltip;
};

struct BlueprintClassReflectionUVE final {
    std::string className;
    std::string parentClassName;
    std::vector<std::string> implementedInterfaces;
    std::vector<BlueprintPropertyReflectionUVE> properties;
    std::vector<BlueprintFunctionReflectionUVE> functions;
    BlueprintMetadataUVE metadata;
};

class BlueprintReflectionRegistryUVE final {
public:
    bool RegisterClassUVE(BlueprintClassReflectionUVE descriptor);
    bool UnregisterClassUVE(std::string_view className);
    [[nodiscard]] const BlueprintClassReflectionUVE* FindClassUVE(std::string_view className) const noexcept;
    [[nodiscard]] bool IsSubclassOfUVE(std::string_view childClass, std::string_view baseClass) const noexcept;
    [[nodiscard]] std::vector<std::string> GetRegisteredClassNamesUVE() const;
    void ClearUVE() noexcept;

private:
    std::map<std::string, BlueprintClassReflectionUVE, std::less<>> m_classes;
};

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

struct BlueprintGraphBoundsUVE final {
    bool valid = false;
    float minX = 0.0F;
    float minY = 0.0F;
    float maxX = 0.0F;
    float maxY = 0.0F;

    [[nodiscard]] float WidthUVE() const noexcept { return valid ? (maxX - minX) : 0.0F; }
    [[nodiscard]] float HeightUVE() const noexcept { return valid ? (maxY - minY) : 0.0F; }
};

[[nodiscard]] std::string SanitizeBlueprintIdentifierUVE(std::string_view text);
[[nodiscard]] std::string FormatBlueprintNodeTitleUVE(std::string_view rawKey);
[[nodiscard]] BlueprintGraphBoundsUVE ComputeBlueprintGraphBoundsUVE(const BlueprintCoreGraphUVE& graph) noexcept;
void AutoLayoutBlueprintGraphUVE(BlueprintCoreGraphUVE& graph, float horizontalSpacing = 280.0F,
                                 float verticalSpacing = 140.0F);

} // namespace UVE::Blueprint::Core
