// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_core_types.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <queue>
#include <set>
#include <unordered_map>
#include <utility>

#include "blueprint_nodes.h"

namespace UVE::Blueprint::Core {

// ---------------------------------------------------------------------------
// BlueprintIdGeneratorUVE
// ---------------------------------------------------------------------------

BlueprintIdGeneratorUVE::BlueprintIdGeneratorUVE(const std::uint32_t nextId) noexcept
    : m_nextId(nextId == 0U ? 1U : nextId) {}

void BlueprintIdGeneratorUVE::SeedFromAssetUVE(const Asset::BlueprintAssetUVE& asset) noexcept {
    for (const Asset::BlueprintNodeUVE& node : asset.nodes) {
        ReserveUVE(node.id);
    }
}

void BlueprintIdGeneratorUVE::ReserveUVE(const std::uint32_t existingId) noexcept {
    if (existingId >= m_nextId) {
        m_nextId = existingId + 1U;
    }
}

std::uint32_t BlueprintIdGeneratorUVE::AllocateUVE() noexcept {
    if (m_nextId == 0U) {
        m_nextId = 1U;
    }
    return m_nextId++;
}

// ---------------------------------------------------------------------------
// TypeSystem
// ---------------------------------------------------------------------------

BlueprintTypeDescriptorUVE GetBlueprintPinTypeDescriptorUVE(const Asset::BlueprintPinTypeUVE type) noexcept {
    using T = Asset::BlueprintPinTypeUVE;
    using C = BlueprintTypeCategoryUVE;
    switch (type) {
        case T::Exec: return {T::Exec, C::Execution, "Exec", "exec", "", false, T::Exec};
        case T::Bool: return {T::Bool, C::Scalar, "Boolean", "bool", "false", false, T::Bool};
        case T::Int: return {T::Int, C::Scalar, "Integer", "int", "0", false, T::Int};
        case T::Float: return {T::Float, C::Scalar, "Float", "float", "0.0", false, T::Float};
        case T::Vector: return {T::Vector, C::Math, "Vector3", "vec3", "(0,0,0)", false, T::Vector};
        case T::String: return {T::String, C::Text, "String", "str", "", false, T::String};
        case T::Object: return {T::Object, C::Reference, "Object", "Object3D", "", false, T::Object};
        case T::ObjectArray: return {T::ObjectArray, C::Collection, "Object Array", "list[Object3D]", "[]", true, T::Object};
        case T::IntArray: return {T::IntArray, C::Collection, "Integer Array", "list[int]", "[]", true, T::Int};
        case T::FloatArray: return {T::FloatArray, C::Collection, "Float Array", "list[float]", "[]", true, T::Float};
        case T::StringArray: return {T::StringArray, C::Collection, "String Array", "list[str]", "[]", true, T::String};
    }
    return {T::Float, C::Scalar, "Float", "float", "0.0", false, T::Float};
}

std::string_view BlueprintPinTypeDisplayNameUVE(const Asset::BlueprintPinTypeUVE type) noexcept {
    return GetBlueprintPinTypeDescriptorUVE(type).displayName;
}

std::string_view BlueprintPinTypeDefaultLiteralUVE(const Asset::BlueprintPinTypeUVE type) noexcept {
    return GetBlueprintPinTypeDescriptorUVE(type).defaultLiteral;
}

bool CanConnectBlueprintPinTypesUVE(const Asset::BlueprintPinTypeUVE from,
                                    const Asset::BlueprintPinTypeUVE to) noexcept {
    if (from == to) {
        return true;
    }
    if (from == Asset::BlueprintPinTypeUVE::Exec || to == Asset::BlueprintPinTypeUVE::Exec) {
        return false;
    }
    return from == Asset::BlueprintPinTypeUVE::Int && to == Asset::BlueprintPinTypeUVE::Float;
}

// ---------------------------------------------------------------------------
// Metadata
// ---------------------------------------------------------------------------

bool BlueprintMetadataUVE::HasTagUVE(const std::string_view key) const noexcept {
    return tags.find(key) != tags.end();
}

std::string BlueprintMetadataUVE::GetTagUVE(const std::string_view key, const std::string_view fallback) const {
    const auto it = tags.find(key);
    return it != tags.end() ? it->second : std::string(fallback);
}

void BlueprintMetadataUVE::SetTagUVE(std::string key, std::string value) {
    tags[std::move(key)] = std::move(value);
}

// ---------------------------------------------------------------------------
// BlueprintCoreNodeUVE
// ---------------------------------------------------------------------------

const BlueprintCorePinUVE* BlueprintCoreNodeUVE::FindInputPinUVE(const BlueprintPinIdUVE index) const noexcept {
    for (const BlueprintCorePinUVE& pin : inputs) {
        if (pin.index == index) {
            return &pin;
        }
    }
    return nullptr;
}

const BlueprintCorePinUVE* BlueprintCoreNodeUVE::FindOutputPinUVE(const BlueprintPinIdUVE index) const noexcept {
    for (const BlueprintCorePinUVE& pin : outputs) {
        if (pin.index == index) {
            return &pin;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// BlueprintDiagnosticSinkUVE
// ---------------------------------------------------------------------------

void BlueprintDiagnosticSinkUVE::EmitUVE(const BlueprintCoreDiagnosticSeverityUVE severity,
                                         const BlueprintNodeIdUVE nodeId, std::string code, std::string message,
                                         const std::optional<BlueprintPinIdUVE> pinIndex) {
    m_entries.push_back(BlueprintCoreDiagnosticUVE{severity, nodeId, pinIndex, std::move(code), std::move(message)});
}

void BlueprintDiagnosticSinkUVE::ClearUVE() noexcept { m_entries.clear(); }

bool BlueprintDiagnosticSinkUVE::HasErrorsUVE() const noexcept { return ErrorCountUVE() > 0U; }

std::size_t BlueprintDiagnosticSinkUVE::ErrorCountUVE() const noexcept {
    return static_cast<std::size_t>(std::count_if(m_entries.begin(), m_entries.end(), [](const BlueprintCoreDiagnosticUVE& d) {
        return d.severity == BlueprintCoreDiagnosticSeverityUVE::Error;
    }));
}

std::size_t BlueprintDiagnosticSinkUVE::WarningCountUVE() const noexcept {
    return static_cast<std::size_t>(std::count_if(m_entries.begin(), m_entries.end(), [](const BlueprintCoreDiagnosticUVE& d) {
        return d.severity == BlueprintCoreDiagnosticSeverityUVE::Warning;
    }));
}

// ---------------------------------------------------------------------------
// BlueprintCoreGraphUVE
// ---------------------------------------------------------------------------

BlueprintCoreGraphUVE::BlueprintCoreGraphUVE(std::string name, std::string parentKind)
    : m_name(std::move(name)), m_parentKind(std::move(parentKind)) {}

void BlueprintCoreGraphUVE::SetNameUVE(std::string name) {
    m_name = std::move(name);
    NotifyUVE(BlueprintGraphEventUVE{BlueprintGraphChangeKindUVE::MetadataChanged, 0U, 0U, "name"});
}

void BlueprintCoreGraphUVE::SetParentKindUVE(std::string parentKind) {
    m_parentKind = std::move(parentKind);
    NotifyUVE(BlueprintGraphEventUVE{BlueprintGraphChangeKindUVE::MetadataChanged, 0U, 0U, "parentKind"});
}

void BlueprintCoreGraphUVE::SetMetadataUVE(BlueprintMetadataUVE metadata) {
    m_metadata = std::move(metadata);
    NotifyUVE(BlueprintGraphEventUVE{BlueprintGraphChangeKindUVE::MetadataChanged, 0U, 0U, "metadata"});
}

void BlueprintCoreGraphUVE::AddListenerUVE(IBlueprintGraphListenerUVE* const listener) {
    if (listener == nullptr) {
        return;
    }
    if (std::find(m_listeners.begin(), m_listeners.end(), listener) == m_listeners.end()) {
        m_listeners.push_back(listener);
    }
}

void BlueprintCoreGraphUVE::RemoveListenerUVE(IBlueprintGraphListenerUVE* const listener) {
    m_listeners.erase(std::remove(m_listeners.begin(), m_listeners.end(), listener), m_listeners.end());
}

void BlueprintCoreGraphUVE::NotifyUVE(const BlueprintGraphEventUVE& event) {
    for (IBlueprintGraphListenerUVE* const listener : m_listeners) {
        if (listener != nullptr) {
            listener->OnBlueprintGraphChangedUVE(event);
        }
    }
}

BlueprintNodeIdUVE BlueprintCoreGraphUVE::AddNodeUVE(BlueprintCoreNodeUVE node) {
    if (node.id == kInvalidBlueprintNodeIdUVE || FindNodeUVE(node.id) != nullptr) {
        node.id = m_nodeIds.AllocateUVE();
    } else {
        m_nodeIds.ReserveUVE(node.id);
    }
    for (std::size_t i = 0U; i < node.inputs.size(); ++i) {
        node.inputs[i].index = static_cast<BlueprintPinIdUVE>(i);
        node.inputs[i].direction = BlueprintPinDirectionUVE::Input;
    }
    for (std::size_t i = 0U; i < node.outputs.size(); ++i) {
        node.outputs[i].index = static_cast<BlueprintPinIdUVE>(i);
        node.outputs[i].direction = BlueprintPinDirectionUVE::Output;
    }
    const BlueprintNodeIdUVE addedId = node.id;
    std::string typeKey = node.typeKey;
    m_nodes.push_back(std::move(node));
    NotifyUVE(BlueprintGraphEventUVE{BlueprintGraphChangeKindUVE::NodeAdded, addedId, 0U, std::move(typeKey)});
    return addedId;
}

bool BlueprintCoreGraphUVE::RemoveNodeUVE(const BlueprintNodeIdUVE nodeId) {
    const auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
                                 [nodeId](const BlueprintCoreNodeUVE& n) { return n.id == nodeId; });
    if (it == m_nodes.end()) {
        return false;
    }

    std::vector<BlueprintConnectionIdUVE> removedConns;
    m_connections.erase(
        std::remove_if(m_connections.begin(), m_connections.end(),
                       [nodeId, &removedConns](const BlueprintCoreConnectionUVE& c) {
                           if (c.fromNode == nodeId || c.toNode == nodeId) {
                               removedConns.push_back(c.id);
                               return true;
                           }
                           return false;
                       }),
        m_connections.end());

    m_nodes.erase(it);
    for (const BlueprintConnectionIdUVE cid : removedConns) {
        NotifyUVE(BlueprintGraphEventUVE{BlueprintGraphChangeKindUVE::ConnectionRemoved, nodeId, cid, "node_removed"});
    }
    NotifyUVE(BlueprintGraphEventUVE{BlueprintGraphChangeKindUVE::NodeRemoved, nodeId, 0U, {}});
    return true;
}

bool BlueprintCoreGraphUVE::MoveNodeUVE(const BlueprintNodeIdUVE nodeId, const float x, const float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    BlueprintCoreNodeUVE* const node = FindNodeUVE(nodeId);
    if (node == nullptr) {
        return false;
    }
    node->x = x;
    node->y = y;
    NotifyUVE(BlueprintGraphEventUVE{BlueprintGraphChangeKindUVE::NodeMoved, nodeId, 0U, {}});
    return true;
}

BlueprintCoreNodeUVE* BlueprintCoreGraphUVE::FindNodeUVE(const BlueprintNodeIdUVE nodeId) noexcept {
    for (BlueprintCoreNodeUVE& n : m_nodes) {
        if (n.id == nodeId) {
            return &n;
        }
    }
    return nullptr;
}

const BlueprintCoreNodeUVE* BlueprintCoreGraphUVE::FindNodeUVE(const BlueprintNodeIdUVE nodeId) const noexcept {
    for (const BlueprintCoreNodeUVE& n : m_nodes) {
        if (n.id == nodeId) {
            return &n;
        }
    }
    return nullptr;
}

BlueprintConnectionResultUVE BlueprintCoreGraphUVE::ConnectUVE(const BlueprintNodeIdUVE fromNode,
                                                               const BlueprintPinIdUVE fromPin,
                                                               const BlueprintNodeIdUVE toNode,
                                                               const BlueprintPinIdUVE toPin) {
    if (fromNode == toNode) {
        return {false, kInvalidBlueprintConnectionIdUVE, "Cannot connect a node to itself"};
    }
    const BlueprintCoreNodeUVE* const src = FindNodeUVE(fromNode);
    const BlueprintCoreNodeUVE* const dst = FindNodeUVE(toNode);
    if (src == nullptr || dst == nullptr) {
        return {false, kInvalidBlueprintConnectionIdUVE, "Endpoint node does not exist"};
    }
    const BlueprintCorePinUVE* const outPin = src->FindOutputPinUVE(fromPin);
    const BlueprintCorePinUVE* const inPin = dst->FindInputPinUVE(toPin);
    if (outPin == nullptr || inPin == nullptr) {
        return {false, kInvalidBlueprintConnectionIdUVE, "Pin index out of range"};
    }
    if (!CanConnectBlueprintPinTypesUVE(outPin->type, inPin->type)) {
        return {false, kInvalidBlueprintConnectionIdUVE, "Incompatible pin types"};
    }
    for (const BlueprintCoreConnectionUVE& existing : m_connections) {
        if (existing.fromNode == fromNode && existing.fromPin == fromPin && existing.toNode == toNode &&
            existing.toPin == toPin) {
            return {false, existing.id, "Connection already exists"};
        }
    }

    // Single-wire constraints: non-exec input pins take one driver; exec output pins take one target.
    if (inPin->type != Asset::BlueprintPinTypeUVE::Exec) {
        m_connections.erase(
            std::remove_if(m_connections.begin(), m_connections.end(),
                           [toNode, toPin](const BlueprintCoreConnectionUVE& c) {
                               return c.toNode == toNode && c.toPin == toPin;
                           }),
            m_connections.end());
    } else {
        m_connections.erase(
            std::remove_if(m_connections.begin(), m_connections.end(),
                           [fromNode, fromPin](const BlueprintCoreConnectionUVE& c) {
                               return c.fromNode == fromNode && c.fromPin == fromPin;
                           }),
            m_connections.end());
    }

    const BlueprintConnectionIdUVE cid = m_connectionIds.AllocateUVE();
    m_connections.push_back(BlueprintCoreConnectionUVE{cid, fromNode, fromPin, toNode, toPin});
    NotifyUVE(BlueprintGraphEventUVE{BlueprintGraphChangeKindUVE::ConnectionAdded, fromNode, cid, {}});
    return {true, cid, {}};
}

bool BlueprintCoreGraphUVE::DisconnectUVE(const BlueprintConnectionIdUVE connectionId) {
    const auto it = std::find_if(m_connections.begin(), m_connections.end(),
                                 [connectionId](const BlueprintCoreConnectionUVE& c) { return c.id == connectionId; });
    if (it == m_connections.end()) {
        return false;
    }
    const BlueprintNodeIdUVE fromNode = it->fromNode;
    m_connections.erase(it);
    NotifyUVE(BlueprintGraphEventUVE{BlueprintGraphChangeKindUVE::ConnectionRemoved, fromNode, connectionId, {}});
    return true;
}

bool BlueprintCoreGraphUVE::DisconnectPinsUVE(const BlueprintNodeIdUVE fromNode, const BlueprintPinIdUVE fromPin,
                                              const BlueprintNodeIdUVE toNode, const BlueprintPinIdUVE toPin) {
    const auto it = std::find_if(m_connections.begin(), m_connections.end(), [&](const BlueprintCoreConnectionUVE& c) {
        return c.fromNode == fromNode && c.fromPin == fromPin && c.toNode == toNode && c.toPin == toPin;
    });
    if (it == m_connections.end()) {
        return false;
    }
    return DisconnectUVE(it->id);
}

std::vector<BlueprintCoreConnectionUVE> BlueprintCoreGraphUVE::GetIncomingConnectionsUVE(
    const BlueprintNodeIdUVE nodeId) const {
    std::vector<BlueprintCoreConnectionUVE> out;
    for (const BlueprintCoreConnectionUVE& c : m_connections) {
        if (c.toNode == nodeId) {
            out.push_back(c);
        }
    }
    return out;
}

std::vector<BlueprintCoreConnectionUVE> BlueprintCoreGraphUVE::GetOutgoingConnectionsUVE(
    const BlueprintNodeIdUVE nodeId) const {
    std::vector<BlueprintCoreConnectionUVE> out;
    for (const BlueprintCoreConnectionUVE& c : m_connections) {
        if (c.fromNode == nodeId) {
            out.push_back(c);
        }
    }
    return out;
}

void BlueprintCoreGraphUVE::SetVariablesUVE(std::vector<Asset::BlueprintVariableUVE> variables) {
    m_variables = std::move(variables);
    NotifyUVE(BlueprintGraphEventUVE{BlueprintGraphChangeKindUVE::VariableChanged, 0U, 0U, {}});
}

void BlueprintCoreGraphUVE::SetCommentsUVE(std::vector<Asset::BlueprintCommentUVE> comments) {
    m_comments = std::move(comments);
    NotifyUVE(BlueprintGraphEventUVE{BlueprintGraphChangeKindUVE::MetadataChanged, 0U, 0U, "comments"});
}

std::vector<BlueprintNodeIdUVE> BlueprintCoreGraphUVE::TopologicalSortNodesUVE() const {
    std::unordered_map<BlueprintNodeIdUVE, std::size_t> indegree;
    std::unordered_map<BlueprintNodeIdUVE, std::vector<BlueprintNodeIdUVE>> adj;
    for (const BlueprintCoreNodeUVE& n : m_nodes) {
        indegree[n.id] = 0U;
    }
    for (const BlueprintCoreConnectionUVE& c : m_connections) {
        if (indegree.contains(c.fromNode) && indegree.contains(c.toNode)) {
            adj[c.fromNode].push_back(c.toNode);
            ++indegree[c.toNode];
        }
    }

    std::queue<BlueprintNodeIdUVE> ready;
    for (const BlueprintCoreNodeUVE& n : m_nodes) {
        if (indegree[n.id] == 0U) {
            ready.push(n.id);
        }
    }

    std::vector<BlueprintNodeIdUVE> order;
    order.reserve(m_nodes.size());
    while (!ready.empty()) {
        const BlueprintNodeIdUVE cur = ready.front();
        ready.pop();
        order.push_back(cur);
        if (const auto it = adj.find(cur); it != adj.end()) {
            for (const BlueprintNodeIdUVE nxt : it->second) {
                if (--indegree[nxt] == 0U) {
                    ready.push(nxt);
                }
            }
        }
    }

    if (order.size() != m_nodes.size()) {
        return {};
    }
    return order;
}

bool BlueprintCoreGraphUVE::HasCycleUVE() const {
    return !m_nodes.empty() && TopologicalSortNodesUVE().empty();
}

void BlueprintCoreGraphUVE::ValidateWithUVE(const IBlueprintNodeValidatorUVE& validator,
                                            BlueprintDiagnosticSinkUVE& sink) const {
    for (const BlueprintCoreNodeUVE& node : m_nodes) {
        validator.ValidateNodeUVE(*this, node, sink);
    }
}

BlueprintCoreGraphUVE BlueprintCoreGraphFromAssetUVE(const Asset::BlueprintAssetUVE& asset, std::string name) {
    BlueprintCoreGraphUVE graph(std::move(name), asset.parentKind);
    graph.SetVariablesUVE(asset.variables);
    graph.SetCommentsUVE(asset.comments);

    for (const Asset::BlueprintNodeUVE& src : asset.nodes) {
        BlueprintCoreNodeUVE node;
        node.id = src.id;
        node.typeKey = src.type;
        node.variableName = src.variable;
        node.x = src.x;
        node.y = src.y;

        const Asset::BlueprintNodeTypeUVE* const spec = Asset::FindBlueprintNodeTypeUVE(src.type);
        node.title = spec != nullptr ? spec->title : FormatBlueprintNodeTitleUVE(src.type);
        if (spec != nullptr) {
            node.metadata.category = spec->category;
            node.metadata.tooltip = spec->tooltip;
        }

        const Asset::BlueprintNodePinsUVE pins = Asset::ResolveBlueprintNodePinsUVE(asset, src);
        for (std::size_t i = 0U; i < pins.inputs.size(); ++i) {
            BlueprintCorePinUVE pin;
            pin.index = static_cast<BlueprintPinIdUVE>(i);
            pin.name = pins.inputs[i].name;
            pin.type = pins.inputs[i].type;
            pin.direction = BlueprintPinDirectionUVE::Input;
            pin.defaultValue = i < src.inputValues.size() ? src.inputValues[i] : pins.inputs[i].defaultText;
            node.inputs.push_back(std::move(pin));
        }
        // Preserve extra serialized input values for unknown node types so round-tripping stays lossless.
        if (pins.inputs.empty() && !src.inputValues.empty()) {
            for (std::size_t i = 0U; i < src.inputValues.size(); ++i) {
                BlueprintCorePinUVE pin;
                pin.index = static_cast<BlueprintPinIdUVE>(i);
                pin.name = "In" + std::to_string(i);
                pin.type = Asset::BlueprintPinTypeUVE::String;
                pin.direction = BlueprintPinDirectionUVE::Input;
                pin.defaultValue = src.inputValues[i];
                node.inputs.push_back(std::move(pin));
            }
        }
        for (std::size_t i = 0U; i < pins.outputs.size(); ++i) {
            BlueprintCorePinUVE pin;
            pin.index = static_cast<BlueprintPinIdUVE>(i);
            pin.name = pins.outputs[i].name;
            pin.type = pins.outputs[i].type;
            pin.direction = BlueprintPinDirectionUVE::Output;
            pin.defaultValue = pins.outputs[i].defaultText;
            node.outputs.push_back(std::move(pin));
        }

        graph.AddNodeUVE(std::move(node));
    }

    for (const Asset::BlueprintWireUVE& wire : asset.wires) {
        (void)graph.ConnectUVE(wire.fromNode, wire.fromPin, wire.toNode, wire.toPin);
    }

    return graph;
}

Asset::BlueprintAssetUVE BlueprintCoreGraphToAssetUVE(const BlueprintCoreGraphUVE& graph) {
    Asset::BlueprintAssetUVE asset;
    asset.parentKind = graph.ParentKindUVE();
    asset.variables = graph.VariablesUVE();
    asset.comments = graph.CommentsUVE();

    asset.nodes.reserve(graph.NodesUVE().size());
    for (const BlueprintCoreNodeUVE& src : graph.NodesUVE()) {
        Asset::BlueprintNodeUVE node;
        node.id = src.id;
        node.type = src.typeKey;
        node.variable = src.variableName;
        node.x = src.x;
        node.y = src.y;
        node.inputValues.reserve(src.inputs.size());
        for (const BlueprintCorePinUVE& pin : src.inputs) {
            node.inputValues.push_back(pin.defaultValue);
        }
        asset.nodes.push_back(std::move(node));
    }

    asset.wires.reserve(graph.ConnectionsUVE().size());
    for (const BlueprintCoreConnectionUVE& conn : graph.ConnectionsUVE()) {
        asset.wires.push_back(Asset::BlueprintWireUVE{conn.fromNode, conn.fromPin, conn.toNode, conn.toPin});
    }

    return asset;
}

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------

bool BlueprintReflectionRegistryUVE::RegisterClassUVE(BlueprintClassReflectionUVE descriptor) {
    if (descriptor.className.empty()) {
        return false;
    }
    const std::string key = descriptor.className;
    m_classes[key] = std::move(descriptor);
    return true;
}

bool BlueprintReflectionRegistryUVE::UnregisterClassUVE(const std::string_view className) {
    const auto it = m_classes.find(className);
    if (it == m_classes.end()) {
        return false;
    }
    m_classes.erase(it);
    return true;
}

const BlueprintClassReflectionUVE* BlueprintReflectionRegistryUVE::FindClassUVE(
    const std::string_view className) const noexcept {
    const auto it = m_classes.find(className);
    return it != m_classes.end() ? &it->second : nullptr;
}

bool BlueprintReflectionRegistryUVE::IsSubclassOfUVE(const std::string_view childClass,
                                                     const std::string_view baseClass) const noexcept {
    if (childClass.empty() || baseClass.empty()) {
        return false;
    }
    if (childClass == baseClass) {
        return true;
    }
    std::set<std::string_view> visited;
    std::string_view current = childClass;
    while (!current.empty() && visited.insert(current).second) {
        const BlueprintClassReflectionUVE* const desc = FindClassUVE(current);
        if (desc == nullptr) {
            return false;
        }
        if (desc->parentClassName == baseClass) {
            return true;
        }
        current = desc->parentClassName;
    }
    return false;
}

std::vector<std::string> BlueprintReflectionRegistryUVE::GetRegisteredClassNamesUVE() const {
    std::vector<std::string> names;
    names.reserve(m_classes.size());
    for (const auto& entry : m_classes) {
        names.push_back(entry.first);
    }
    return names;
}

void BlueprintReflectionRegistryUVE::ClearUVE() noexcept { m_classes.clear(); }

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

std::string SanitizeBlueprintIdentifierUVE(const std::string_view text) {
    std::string out;
    out.reserve(text.size() + 1U);
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), '_');
    }
    return out;
}

std::string FormatBlueprintNodeTitleUVE(const std::string_view rawKey) {
    std::string out;
    out.reserve(rawKey.size());
    bool capitalizeNext = true;
    for (const char c : rawKey) {
        if (c == '_' || c == '-' || c == '.') {
            if (!out.empty() && out.back() != ' ') {
                out.push_back(' ');
            }
            capitalizeNext = true;
            continue;
        }
        if (capitalizeNext) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            capitalizeNext = false;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

BlueprintGraphBoundsUVE ComputeBlueprintGraphBoundsUVE(const BlueprintCoreGraphUVE& graph) noexcept {
    BlueprintGraphBoundsUVE bounds;
    if (graph.NodesUVE().empty()) {
        return bounds;
    }
    bounds.valid = true;
    bounds.minX = std::numeric_limits<float>::max();
    bounds.minY = std::numeric_limits<float>::max();
    bounds.maxX = std::numeric_limits<float>::lowest();
    bounds.maxY = std::numeric_limits<float>::lowest();
    for (const BlueprintCoreNodeUVE& node : graph.NodesUVE()) {
        bounds.minX = std::min(bounds.minX, node.x);
        bounds.minY = std::min(bounds.minY, node.y);
        bounds.maxX = std::max(bounds.maxX, node.x);
        bounds.maxY = std::max(bounds.maxY, node.y);
    }
    return bounds;
}

void AutoLayoutBlueprintGraphUVE(BlueprintCoreGraphUVE& graph, const float horizontalSpacing,
                                 const float verticalSpacing) {
    std::vector<BlueprintNodeIdUVE> order = graph.TopologicalSortNodesUVE();
    if (order.empty()) {
        order.reserve(graph.NodesUVE().size());
        for (const BlueprintCoreNodeUVE& n : graph.NodesUVE()) {
            order.push_back(n.id);
        }
    }

    std::unordered_map<BlueprintNodeIdUVE, std::size_t> layer;
    for (const BlueprintNodeIdUVE id : order) {
        std::size_t nodeLayer = 0U;
        for (const BlueprintCoreConnectionUVE& conn : graph.GetIncomingConnectionsUVE(id)) {
            if (const auto it = layer.find(conn.fromNode); it != layer.end()) {
                nodeLayer = std::max(nodeLayer, it->second + 1U);
            }
        }
        layer[id] = nodeLayer;
    }

    std::unordered_map<std::size_t, std::size_t> rowPerLayer;
    for (const BlueprintNodeIdUVE id : order) {
        const std::size_t col = layer[id];
        const std::size_t row = rowPerLayer[col]++;
        (void)graph.MoveNodeUVE(id, static_cast<float>(col) * horizontalSpacing,
                                static_cast<float>(row) * verticalSpacing);
    }
}

} // namespace UVE::Blueprint::Core
