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
// BlueprintIdGenerator
// ---------------------------------------------------------------------------

BlueprintIdGenerator::BlueprintIdGenerator(const std::uint32_t nextId) noexcept
    : m_nextId(nextId == 0U ? 1U : nextId) {}

void BlueprintIdGenerator::SeedFromAsset(const Asset::BlueprintAsset& asset) noexcept {
    for (const Asset::BlueprintNode& node : asset.nodes) {
        Reserve(node.id);
    }
}

void BlueprintIdGenerator::Reserve(const std::uint32_t existingId) noexcept {
    if (existingId >= m_nextId) {
        m_nextId = existingId + 1U;
    }
}

std::uint32_t BlueprintIdGenerator::Allocate() noexcept {
    if (m_nextId == 0U) {
        m_nextId = 1U;
    }
    return m_nextId++;
}

// ---------------------------------------------------------------------------
// TypeSystem
// ---------------------------------------------------------------------------

BlueprintTypeDescriptor GetBlueprintPinTypeDescriptor(const Asset::BlueprintPinType type) noexcept {
    using T = Asset::BlueprintPinType;
    using C = BlueprintTypeCategory;
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

std::string_view BlueprintPinTypeDisplayName(const Asset::BlueprintPinType type) noexcept {
    return GetBlueprintPinTypeDescriptor(type).displayName;
}

std::string_view BlueprintPinTypeDefaultLiteral(const Asset::BlueprintPinType type) noexcept {
    return GetBlueprintPinTypeDescriptor(type).defaultLiteral;
}

bool CanConnectBlueprintPinTypes(const Asset::BlueprintPinType from,
                                    const Asset::BlueprintPinType to) noexcept {
    if (from == to) {
        return true;
    }
    if (from == Asset::BlueprintPinType::Exec || to == Asset::BlueprintPinType::Exec) {
        return false;
    }
    return from == Asset::BlueprintPinType::Int && to == Asset::BlueprintPinType::Float;
}

// ---------------------------------------------------------------------------
// Metadata
// ---------------------------------------------------------------------------

bool BlueprintMetadata::HasTag(const std::string_view key) const noexcept {
    return tags.find(key) != tags.end();
}

std::string BlueprintMetadata::GetTag(const std::string_view key, const std::string_view fallback) const {
    const auto it = tags.find(key);
    return it != tags.end() ? it->second : std::string(fallback);
}

void BlueprintMetadata::SetTag(std::string key, std::string value) {
    tags[std::move(key)] = std::move(value);
}

// ---------------------------------------------------------------------------
// BlueprintCoreNode
// ---------------------------------------------------------------------------

const BlueprintCorePin* BlueprintCoreNode::FindInputPin(const BlueprintPinId index) const noexcept {
    for (const BlueprintCorePin& pin : inputs) {
        if (pin.index == index) {
            return &pin;
        }
    }
    return nullptr;
}

const BlueprintCorePin* BlueprintCoreNode::FindOutputPin(const BlueprintPinId index) const noexcept {
    for (const BlueprintCorePin& pin : outputs) {
        if (pin.index == index) {
            return &pin;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// BlueprintDiagnosticSink
// ---------------------------------------------------------------------------

void BlueprintDiagnosticSink::Emit(const BlueprintCoreDiagnosticSeverity severity,
                                         const BlueprintNodeId nodeId, std::string code, std::string message,
                                         const std::optional<BlueprintPinId> pinIndex) {
    m_entries.push_back(BlueprintCoreDiagnostic{severity, nodeId, pinIndex, std::move(code), std::move(message)});
}

void BlueprintDiagnosticSink::Clear() noexcept { m_entries.clear(); }

bool BlueprintDiagnosticSink::HasErrors() const noexcept { return ErrorCount() > 0U; }

std::size_t BlueprintDiagnosticSink::ErrorCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(m_entries.begin(), m_entries.end(), [](const BlueprintCoreDiagnostic& d) {
        return d.severity == BlueprintCoreDiagnosticSeverity::Error;
    }));
}

std::size_t BlueprintDiagnosticSink::WarningCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(m_entries.begin(), m_entries.end(), [](const BlueprintCoreDiagnostic& d) {
        return d.severity == BlueprintCoreDiagnosticSeverity::Warning;
    }));
}

// ---------------------------------------------------------------------------
// BlueprintCoreGraph
// ---------------------------------------------------------------------------

BlueprintCoreGraph::BlueprintCoreGraph(std::string name, std::string parentKind)
    : m_name(std::move(name)), m_parentKind(std::move(parentKind)) {}

void BlueprintCoreGraph::SetName(std::string name) {
    m_name = std::move(name);
    Notify(BlueprintGraphEvent{BlueprintGraphChangeKind::MetadataChanged, 0U, 0U, "name"});
}

void BlueprintCoreGraph::SetParentKind(std::string parentKind) {
    m_parentKind = std::move(parentKind);
    Notify(BlueprintGraphEvent{BlueprintGraphChangeKind::MetadataChanged, 0U, 0U, "parentKind"});
}

void BlueprintCoreGraph::SetMetadata(BlueprintMetadata metadata) {
    m_metadata = std::move(metadata);
    Notify(BlueprintGraphEvent{BlueprintGraphChangeKind::MetadataChanged, 0U, 0U, "metadata"});
}

void BlueprintCoreGraph::AddListener(IBlueprintGraphListener* const listener) {
    if (listener == nullptr) {
        return;
    }
    if (std::find(m_listeners.begin(), m_listeners.end(), listener) == m_listeners.end()) {
        m_listeners.push_back(listener);
    }
}

void BlueprintCoreGraph::RemoveListener(IBlueprintGraphListener* const listener) {
    m_listeners.erase(std::remove(m_listeners.begin(), m_listeners.end(), listener), m_listeners.end());
}

void BlueprintCoreGraph::Notify(const BlueprintGraphEvent& event) {
    for (IBlueprintGraphListener* const listener : m_listeners) {
        if (listener != nullptr) {
            listener->OnBlueprintGraphChanged(event);
        }
    }
}

BlueprintNodeId BlueprintCoreGraph::AddNode(BlueprintCoreNode node) {
    if (node.id == kInvalidBlueprintNodeId || FindNode(node.id) != nullptr) {
        node.id = m_nodeIds.Allocate();
    } else {
        m_nodeIds.Reserve(node.id);
    }
    for (std::size_t i = 0U; i < node.inputs.size(); ++i) {
        node.inputs[i].index = static_cast<BlueprintPinId>(i);
        node.inputs[i].direction = BlueprintPinDirection::Input;
    }
    for (std::size_t i = 0U; i < node.outputs.size(); ++i) {
        node.outputs[i].index = static_cast<BlueprintPinId>(i);
        node.outputs[i].direction = BlueprintPinDirection::Output;
    }
    const BlueprintNodeId addedId = node.id;
    std::string typeKey = node.typeKey;
    m_nodes.push_back(std::move(node));
    Notify(BlueprintGraphEvent{BlueprintGraphChangeKind::NodeAdded, addedId, 0U, std::move(typeKey)});
    return addedId;
}

bool BlueprintCoreGraph::RemoveNode(const BlueprintNodeId nodeId) {
    const auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
                                 [nodeId](const BlueprintCoreNode& n) { return n.id == nodeId; });
    if (it == m_nodes.end()) {
        return false;
    }

    std::vector<BlueprintConnectionId> removedConns;
    m_connections.erase(
        std::remove_if(m_connections.begin(), m_connections.end(),
                       [nodeId, &removedConns](const BlueprintCoreConnection& c) {
                           if (c.fromNode == nodeId || c.toNode == nodeId) {
                               removedConns.push_back(c.id);
                               return true;
                           }
                           return false;
                       }),
        m_connections.end());

    m_nodes.erase(it);
    for (const BlueprintConnectionId cid : removedConns) {
        Notify(BlueprintGraphEvent{BlueprintGraphChangeKind::ConnectionRemoved, nodeId, cid, "node_removed"});
    }
    Notify(BlueprintGraphEvent{BlueprintGraphChangeKind::NodeRemoved, nodeId, 0U, {}});
    return true;
}

bool BlueprintCoreGraph::MoveNode(const BlueprintNodeId nodeId, const float x, const float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    BlueprintCoreNode* const node = FindNode(nodeId);
    if (node == nullptr) {
        return false;
    }
    node->x = x;
    node->y = y;
    Notify(BlueprintGraphEvent{BlueprintGraphChangeKind::NodeMoved, nodeId, 0U, {}});
    return true;
}

BlueprintCoreNode* BlueprintCoreGraph::FindNode(const BlueprintNodeId nodeId) noexcept {
    for (BlueprintCoreNode& n : m_nodes) {
        if (n.id == nodeId) {
            return &n;
        }
    }
    return nullptr;
}

const BlueprintCoreNode* BlueprintCoreGraph::FindNode(const BlueprintNodeId nodeId) const noexcept {
    for (const BlueprintCoreNode& n : m_nodes) {
        if (n.id == nodeId) {
            return &n;
        }
    }
    return nullptr;
}

BlueprintConnectionResult BlueprintCoreGraph::Connect(const BlueprintNodeId fromNode,
                                                               const BlueprintPinId fromPin,
                                                               const BlueprintNodeId toNode,
                                                               const BlueprintPinId toPin) {
    if (fromNode == toNode) {
        return {false, kInvalidBlueprintConnectionId, "Cannot connect a node to itself"};
    }
    const BlueprintCoreNode* const src = FindNode(fromNode);
    const BlueprintCoreNode* const dst = FindNode(toNode);
    if (src == nullptr || dst == nullptr) {
        return {false, kInvalidBlueprintConnectionId, "Endpoint node does not exist"};
    }
    const BlueprintCorePin* const outPin = src->FindOutputPin(fromPin);
    const BlueprintCorePin* const inPin = dst->FindInputPin(toPin);
    if (outPin == nullptr || inPin == nullptr) {
        return {false, kInvalidBlueprintConnectionId, "Pin index out of range"};
    }
    if (!CanConnectBlueprintPinTypes(outPin->type, inPin->type)) {
        return {false, kInvalidBlueprintConnectionId, "Incompatible pin types"};
    }
    for (const BlueprintCoreConnection& existing : m_connections) {
        if (existing.fromNode == fromNode && existing.fromPin == fromPin && existing.toNode == toNode &&
            existing.toPin == toPin) {
            return {false, existing.id, "Connection already exists"};
        }
    }

    // Single-wire constraints: non-exec input pins take one driver; exec output pins take one target.
    if (inPin->type != Asset::BlueprintPinType::Exec) {
        m_connections.erase(
            std::remove_if(m_connections.begin(), m_connections.end(),
                           [toNode, toPin](const BlueprintCoreConnection& c) {
                               return c.toNode == toNode && c.toPin == toPin;
                           }),
            m_connections.end());
    } else {
        m_connections.erase(
            std::remove_if(m_connections.begin(), m_connections.end(),
                           [fromNode, fromPin](const BlueprintCoreConnection& c) {
                               return c.fromNode == fromNode && c.fromPin == fromPin;
                           }),
            m_connections.end());
    }

    const BlueprintConnectionId cid = m_connectionIds.Allocate();
    m_connections.push_back(BlueprintCoreConnection{cid, fromNode, fromPin, toNode, toPin});
    Notify(BlueprintGraphEvent{BlueprintGraphChangeKind::ConnectionAdded, fromNode, cid, {}});
    return {true, cid, {}};
}

bool BlueprintCoreGraph::Disconnect(const BlueprintConnectionId connectionId) {
    const auto it = std::find_if(m_connections.begin(), m_connections.end(),
                                 [connectionId](const BlueprintCoreConnection& c) { return c.id == connectionId; });
    if (it == m_connections.end()) {
        return false;
    }
    const BlueprintNodeId fromNode = it->fromNode;
    m_connections.erase(it);
    Notify(BlueprintGraphEvent{BlueprintGraphChangeKind::ConnectionRemoved, fromNode, connectionId, {}});
    return true;
}

bool BlueprintCoreGraph::DisconnectPins(const BlueprintNodeId fromNode, const BlueprintPinId fromPin,
                                              const BlueprintNodeId toNode, const BlueprintPinId toPin) {
    const auto it = std::find_if(m_connections.begin(), m_connections.end(), [&](const BlueprintCoreConnection& c) {
        return c.fromNode == fromNode && c.fromPin == fromPin && c.toNode == toNode && c.toPin == toPin;
    });
    if (it == m_connections.end()) {
        return false;
    }
    return Disconnect(it->id);
}

std::vector<BlueprintCoreConnection> BlueprintCoreGraph::GetIncomingConnections(
    const BlueprintNodeId nodeId) const {
    std::vector<BlueprintCoreConnection> out;
    for (const BlueprintCoreConnection& c : m_connections) {
        if (c.toNode == nodeId) {
            out.push_back(c);
        }
    }
    return out;
}

std::vector<BlueprintCoreConnection> BlueprintCoreGraph::GetOutgoingConnections(
    const BlueprintNodeId nodeId) const {
    std::vector<BlueprintCoreConnection> out;
    for (const BlueprintCoreConnection& c : m_connections) {
        if (c.fromNode == nodeId) {
            out.push_back(c);
        }
    }
    return out;
}

void BlueprintCoreGraph::SetVariables(std::vector<Asset::BlueprintVariable> variables) {
    m_variables = std::move(variables);
    Notify(BlueprintGraphEvent{BlueprintGraphChangeKind::VariableChanged, 0U, 0U, {}});
}

void BlueprintCoreGraph::SetComments(std::vector<Asset::BlueprintComment> comments) {
    m_comments = std::move(comments);
    Notify(BlueprintGraphEvent{BlueprintGraphChangeKind::MetadataChanged, 0U, 0U, "comments"});
}

std::vector<BlueprintNodeId> BlueprintCoreGraph::TopologicalSortNodes() const {
    std::unordered_map<BlueprintNodeId, std::size_t> indegree;
    std::unordered_map<BlueprintNodeId, std::vector<BlueprintNodeId>> adj;
    for (const BlueprintCoreNode& n : m_nodes) {
        indegree[n.id] = 0U;
    }
    for (const BlueprintCoreConnection& c : m_connections) {
        if (indegree.contains(c.fromNode) && indegree.contains(c.toNode)) {
            adj[c.fromNode].push_back(c.toNode);
            ++indegree[c.toNode];
        }
    }

    std::queue<BlueprintNodeId> ready;
    for (const BlueprintCoreNode& n : m_nodes) {
        if (indegree[n.id] == 0U) {
            ready.push(n.id);
        }
    }

    std::vector<BlueprintNodeId> order;
    order.reserve(m_nodes.size());
    while (!ready.empty()) {
        const BlueprintNodeId cur = ready.front();
        ready.pop();
        order.push_back(cur);
        if (const auto it = adj.find(cur); it != adj.end()) {
            for (const BlueprintNodeId nxt : it->second) {
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

bool BlueprintCoreGraph::HasCycle() const {
    return !m_nodes.empty() && TopologicalSortNodes().empty();
}

void BlueprintCoreGraph::ValidateWith(const IBlueprintNodeValidator& validator,
                                            BlueprintDiagnosticSink& sink) const {
    for (const BlueprintCoreNode& node : m_nodes) {
        validator.ValidateNode(*this, node, sink);
    }
}

BlueprintCoreGraph BlueprintCoreGraphFromAsset(const Asset::BlueprintAsset& asset, std::string name) {
    BlueprintCoreGraph graph(std::move(name), asset.parentKind);
    graph.SetVariables(asset.variables);
    graph.SetComments(asset.comments);

    for (const Asset::BlueprintNode& src : asset.nodes) {
        BlueprintCoreNode node;
        node.id = src.id;
        node.typeKey = src.type;
        node.variableName = src.variable;
        node.x = src.x;
        node.y = src.y;

        const Asset::BlueprintNodeType* const spec = Asset::FindBlueprintNodeType(src.type);
        node.title = spec != nullptr ? spec->title : FormatBlueprintNodeTitle(src.type);
        if (spec != nullptr) {
            node.metadata.category = spec->category;
            node.metadata.tooltip = spec->tooltip;
        }

        const Asset::BlueprintNodePins pins = Asset::ResolveBlueprintNodePins(asset, src);
        for (std::size_t i = 0U; i < pins.inputs.size(); ++i) {
            BlueprintCorePin pin;
            pin.index = static_cast<BlueprintPinId>(i);
            pin.name = pins.inputs[i].name;
            pin.type = pins.inputs[i].type;
            pin.direction = BlueprintPinDirection::Input;
            pin.defaultValue = i < src.inputValues.size() ? src.inputValues[i] : pins.inputs[i].defaultText;
            node.inputs.push_back(std::move(pin));
        }
        // Preserve extra serialized input values for unknown node types so round-tripping stays lossless.
        if (pins.inputs.empty() && !src.inputValues.empty()) {
            for (std::size_t i = 0U; i < src.inputValues.size(); ++i) {
                BlueprintCorePin pin;
                pin.index = static_cast<BlueprintPinId>(i);
                pin.name = "In" + std::to_string(i);
                pin.type = Asset::BlueprintPinType::String;
                pin.direction = BlueprintPinDirection::Input;
                pin.defaultValue = src.inputValues[i];
                node.inputs.push_back(std::move(pin));
            }
        }
        for (std::size_t i = 0U; i < pins.outputs.size(); ++i) {
            BlueprintCorePin pin;
            pin.index = static_cast<BlueprintPinId>(i);
            pin.name = pins.outputs[i].name;
            pin.type = pins.outputs[i].type;
            pin.direction = BlueprintPinDirection::Output;
            pin.defaultValue = pins.outputs[i].defaultText;
            node.outputs.push_back(std::move(pin));
        }

        graph.AddNode(std::move(node));
    }

    for (const Asset::BlueprintWire& wire : asset.wires) {
        (void)graph.Connect(wire.fromNode, wire.fromPin, wire.toNode, wire.toPin);
    }

    return graph;
}

Asset::BlueprintAsset BlueprintCoreGraphToAsset(const BlueprintCoreGraph& graph) {
    Asset::BlueprintAsset asset;
    asset.parentKind = graph.ParentKind();
    asset.variables = graph.Variables();
    asset.comments = graph.Comments();

    asset.nodes.reserve(graph.Nodes().size());
    for (const BlueprintCoreNode& src : graph.Nodes()) {
        Asset::BlueprintNode node;
        node.id = src.id;
        node.type = src.typeKey;
        node.variable = src.variableName;
        node.x = src.x;
        node.y = src.y;
        node.inputValues.reserve(src.inputs.size());
        for (const BlueprintCorePin& pin : src.inputs) {
            node.inputValues.push_back(pin.defaultValue);
        }
        asset.nodes.push_back(std::move(node));
    }

    asset.wires.reserve(graph.Connections().size());
    for (const BlueprintCoreConnection& conn : graph.Connections()) {
        asset.wires.push_back(Asset::BlueprintWire{conn.fromNode, conn.fromPin, conn.toNode, conn.toPin});
    }

    return asset;
}

// ---------------------------------------------------------------------------
// Reflection
// ---------------------------------------------------------------------------

bool BlueprintReflectionRegistry::RegisterClass(BlueprintClassReflection descriptor) {
    if (descriptor.className.empty()) {
        return false;
    }
    const std::string key = descriptor.className;
    m_classes[key] = std::move(descriptor);
    return true;
}

bool BlueprintReflectionRegistry::UnregisterClass(const std::string_view className) {
    const auto it = m_classes.find(className);
    if (it == m_classes.end()) {
        return false;
    }
    m_classes.erase(it);
    return true;
}

const BlueprintClassReflection* BlueprintReflectionRegistry::FindClass(
    const std::string_view className) const noexcept {
    const auto it = m_classes.find(className);
    return it != m_classes.end() ? &it->second : nullptr;
}

bool BlueprintReflectionRegistry::IsSubclassOf(const std::string_view childClass,
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
        const BlueprintClassReflection* const desc = FindClass(current);
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

std::vector<std::string> BlueprintReflectionRegistry::GetRegisteredClassNames() const {
    std::vector<std::string> names;
    names.reserve(m_classes.size());
    for (const auto& entry : m_classes) {
        names.push_back(entry.first);
    }
    return names;
}

void BlueprintReflectionRegistry::Clear() noexcept { m_classes.clear(); }

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

std::string SanitizeBlueprintIdentifier(const std::string_view text) {
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

std::string FormatBlueprintNodeTitle(const std::string_view rawKey) {
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

BlueprintGraphBounds ComputeBlueprintGraphBounds(const BlueprintCoreGraph& graph) noexcept {
    BlueprintGraphBounds bounds;
    if (graph.Nodes().empty()) {
        return bounds;
    }
    bounds.valid = true;
    bounds.minX = std::numeric_limits<float>::max();
    bounds.minY = std::numeric_limits<float>::max();
    bounds.maxX = std::numeric_limits<float>::lowest();
    bounds.maxY = std::numeric_limits<float>::lowest();
    for (const BlueprintCoreNode& node : graph.Nodes()) {
        bounds.minX = std::min(bounds.minX, node.x);
        bounds.minY = std::min(bounds.minY, node.y);
        bounds.maxX = std::max(bounds.maxX, node.x);
        bounds.maxY = std::max(bounds.maxY, node.y);
    }
    return bounds;
}

void AutoLayoutBlueprintGraph(BlueprintCoreGraph& graph, const float horizontalSpacing,
                                 const float verticalSpacing) {
    std::vector<BlueprintNodeId> order = graph.TopologicalSortNodes();
    if (order.empty()) {
        order.reserve(graph.Nodes().size());
        for (const BlueprintCoreNode& n : graph.Nodes()) {
            order.push_back(n.id);
        }
    }

    std::unordered_map<BlueprintNodeId, std::size_t> layer;
    for (const BlueprintNodeId id : order) {
        std::size_t nodeLayer = 0U;
        for (const BlueprintCoreConnection& conn : graph.GetIncomingConnections(id)) {
            if (const auto it = layer.find(conn.fromNode); it != layer.end()) {
                nodeLayer = std::max(nodeLayer, it->second + 1U);
            }
        }
        layer[id] = nodeLayer;
    }

    std::unordered_map<std::size_t, std::size_t> rowPerLayer;
    for (const BlueprintNodeId id : order) {
        const std::size_t col = layer[id];
        const std::size_t row = rowPerLayer[col]++;
        (void)graph.MoveNode(id, static_cast<float>(col) * horizontalSpacing,
                                static_cast<float>(row) * verticalSpacing);
    }
}

} // namespace UVE::Blueprint::Core
