// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_editor_services.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "blueprint_core_types.h"

namespace UVE::Blueprint::Editor {
namespace {

using Json = nlohmann::json;

constexpr std::string_view kClipboardSchema = "uve-blueprint-clipboard-v1";

[[nodiscard]] std::string LowerCopy(const std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

[[nodiscard]] bool ContainsI(const std::string_view hay, const std::string& needleLower) {
    if (needleLower.empty()) {
        return true;
    }
    return LowerCopy(hay).find(needleLower) != std::string::npos;
}

[[nodiscard]] bool EqualsIgnoreCase(const std::string_view a, const std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0U; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

constexpr std::array<BlueprintShortcutBinding, 16> kDefaultShortcuts{{
    {BlueprintShortcutAction::Compile, "F7", false, false, false, "Compile Blueprint"},
    {BlueprintShortcutAction::Save, "S", true, false, false, "Save Blueprint"},
    {BlueprintShortcutAction::Find, "F", true, false, false, "Find in Graph"},
    {BlueprintShortcutAction::Undo, "Z", true, false, false, "Undo"},
    {BlueprintShortcutAction::Redo, "Y", true, false, false, "Redo"},
    {BlueprintShortcutAction::Copy, "C", true, false, false, "Copy Selection"},
    {BlueprintShortcutAction::Paste, "V", true, false, false, "Paste Selection"},
    {BlueprintShortcutAction::Cut, "X", true, false, false, "Cut Selection"},
    {BlueprintShortcutAction::Duplicate, "D", true, false, false, "Duplicate Selection"},
    {BlueprintShortcutAction::DeleteSelection, "Delete", false, false, false, "Delete Selection"},
    {BlueprintShortcutAction::AddComment, "C", false, false, false, "Add Comment Box"},
    {BlueprintShortcutAction::FrameAll, "Home", false, false, false, "Frame Entire Graph"},
    {BlueprintShortcutAction::ToggleBreakpoint, "F9", false, false, false, "Toggle Breakpoint"},
    {BlueprintShortcutAction::DebugContinue, "F5", false, false, false, "Continue Execution"},
    {BlueprintShortcutAction::DebugStepOver, "F10", false, false, false, "Step Over"},
    {BlueprintShortcutAction::DebugStepInto, "F11", false, false, false, "Step Into"},
}};

} // namespace

// ---------------------------------------------------------------------------
// BlueprintUndoStack
// ---------------------------------------------------------------------------

BlueprintUndoStack::BlueprintUndoStack(const std::size_t maxHistory) noexcept
    : m_maxHistory(maxHistory < 2U ? 2U : maxHistory) {}

void BlueprintUndoStack::Reset(const Asset::BlueprintAsset& initialState, std::string label) {
    m_history.clear();
    m_history.push_back(BlueprintUndoEntry{std::move(label), initialState});
    m_cursor = 0U;
}

void BlueprintUndoStack::PushSnapshot(const Asset::BlueprintAsset& newState, std::string label) {
    if (!m_history.empty() && m_history[m_cursor].snapshot == newState) {
        return;
    }
    if (!m_history.empty() && m_cursor + 1U < m_history.size()) {
        m_history.erase(m_history.begin() + static_cast<std::ptrdiff_t>(m_cursor + 1U), m_history.end());
    }
    m_history.push_back(BlueprintUndoEntry{std::move(label), newState});
    if (m_history.size() > m_maxHistory) {
        m_history.erase(m_history.begin());
    }
    m_cursor = m_history.size() - 1U;
}

bool BlueprintUndoStack::CanUndo() const noexcept { return !m_history.empty() && m_cursor > 0U; }

bool BlueprintUndoStack::CanRedo() const noexcept {
    return !m_history.empty() && m_cursor + 1U < m_history.size();
}

std::optional<Asset::BlueprintAsset> BlueprintUndoStack::Undo() {
    if (!CanUndo()) {
        return std::nullopt;
    }
    --m_cursor;
    return m_history[m_cursor].snapshot;
}

std::optional<Asset::BlueprintAsset> BlueprintUndoStack::Redo() {
    if (!CanRedo()) {
        return std::nullopt;
    }
    ++m_cursor;
    return m_history[m_cursor].snapshot;
}

std::string_view BlueprintUndoStack::CurrentLabel() const noexcept {
    if (m_history.empty()) {
        return {};
    }
    return m_history[m_cursor].label;
}

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------

std::string CopyBlueprintSelection(const Asset::BlueprintAsset& blueprint,
                                      const std::span<const std::uint32_t> selectedNodeIds) {
    std::set<std::uint32_t> selected(selectedNodeIds.begin(), selectedNodeIds.end());
    Json nodes = Json::array();
    for (const Asset::BlueprintNode& n : blueprint.nodes) {
        if (!selected.contains(n.id)) {
            continue;
        }
        Json entry{{"id", n.id}, {"type", n.type}, {"x", n.x}, {"y", n.y}, {"inputs", n.inputValues}};
        if (!n.variable.empty()) {
            entry["variable"] = n.variable;
        }
        nodes.push_back(std::move(entry));
    }
    Json wires = Json::array();
    for (const Asset::BlueprintWire& w : blueprint.wires) {
        if (selected.contains(w.fromNode) && selected.contains(w.toNode)) {
            wires.push_back({w.fromNode, w.fromPin, w.toNode, w.toPin});
        }
    }
    const Json payload{{"schema", kClipboardSchema},
                          {"nodes", std::move(nodes)},
                          {"wires", std::move(wires)}};
    return payload.dump();
}

BlueprintPasteResult PasteBlueprintSelection(Asset::BlueprintAsset& target,
                                                   const std::string_view clipboardJson,
                                                   const float offsetX, const float offsetY) {
    BlueprintPasteResult result;
    Json payload;
    try {
        payload = Json::parse(clipboardJson);
    } catch (const std::exception& e) {
        result.error = std::string("Invalid clipboard JSON: ") + e.what();
        return result;
    }
    if (!payload.is_object() || payload.value("schema", std::string{}) != kClipboardSchema) {
        result.error = "Clipboard does not contain a Blueprint selection.";
        return result;
    }

    Core::BlueprintIdGenerator idGen;
    idGen.SeedFromAsset(target);

    std::unordered_map<std::uint32_t, std::uint32_t> idRemap;
    for (const Json& nJson : payload.value("nodes", Json::array())) {
        const std::uint32_t oldId = nJson.value("id", 0U);
        const std::string type = nJson.value("type", std::string{});
        if (oldId == 0U || type.empty()) {
            continue;
        }
        const std::uint32_t newId = idGen.Allocate();
        idRemap[oldId] = newId;

        Asset::BlueprintNode node;
        node.id = newId;
        node.type = type;
        node.variable = nJson.value("variable", std::string{});
        node.x = nJson.value("x", 0.0F) + offsetX;
        node.y = nJson.value("y", 0.0F) + offsetY;
        node.inputValues = nJson.value("inputs", std::vector<std::string>{});
        target.nodes.push_back(std::move(node));
        result.createdNodeIds.push_back(newId);
    }

    for (const Json& wJson : payload.value("wires", Json::array())) {
        if (!wJson.is_array() || wJson.size() != 4U) {
            continue;
        }
        const auto fromIt = idRemap.find(wJson[0].get<std::uint32_t>());
        const auto toIt = idRemap.find(wJson[2].get<std::uint32_t>());
        if (fromIt == idRemap.end() || toIt == idRemap.end()) {
            continue;
        }
        target.wires.push_back(Asset::BlueprintWire{fromIt->second, wJson[1].get<std::uint32_t>(), toIt->second,
                                                       wJson[3].get<std::uint32_t>()});
        ++result.createdWireCount;
    }

    result.success = !result.createdNodeIds.empty();
    if (!result.success) {
        result.error = "Clipboard contained no valid nodes to paste.";
    }
    return result;
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

std::vector<BlueprintGraphSearchHit> SearchBlueprintGraph(const Asset::BlueprintAsset& blueprint,
                                                                const std::string_view query) {
    std::vector<BlueprintGraphSearchHit> hits;
    const std::string needle = LowerCopy(query);
    if (needle.empty()) {
        return hits;
    }

    for (const Asset::BlueprintVariable& v : blueprint.variables) {
        if (ContainsI(v.name, needle) || ContainsI(v.defaultText, needle)) {
            hits.push_back(BlueprintGraphSearchHit{BlueprintSearchHitKind::Variable, 0U, "Variable: " + v.name});
        }
    }

    for (const Asset::BlueprintNode& n : blueprint.nodes) {
        const Asset::BlueprintNodeType* const spec = Asset::FindBlueprintNodeType(n.type);
        const std::string title = spec != nullptr ? spec->title : n.type;
        if (ContainsI(title, needle)) {
            hits.push_back(BlueprintGraphSearchHit{BlueprintSearchHitKind::NodeTitle, n.id, title});
        } else if (ContainsI(n.type, needle) || ContainsI(n.variable, needle)) {
            hits.push_back(BlueprintGraphSearchHit{BlueprintSearchHitKind::NodeKey, n.id,
                                                      title + " (" + n.type + ")"});
        }
        for (const std::string& val : n.inputValues) {
            if (!val.empty() && ContainsI(val, needle)) {
                hits.push_back(BlueprintGraphSearchHit{BlueprintSearchHitKind::NodeInputValue, n.id,
                                                          title + " = " + val});
                break;
            }
        }
    }

    for (const Asset::BlueprintComment& c : blueprint.comments) {
        if (ContainsI(c.title, needle)) {
            hits.push_back(BlueprintGraphSearchHit{BlueprintSearchHitKind::Comment, 0U, "Comment: " + c.title});
        }
    }

    return hits;
}

// ---------------------------------------------------------------------------
// Context Menu / Node Palette
// ---------------------------------------------------------------------------

std::vector<BlueprintPaletteEntry> BuildBlueprintPalette(
    const Asset::BlueprintGraphKind graphKind, const std::string_view filterText,
    const std::optional<Asset::BlueprintPinType> compatibleInputPinType,
    const std::set<std::string>* const availableComponents) {
    const std::string needle = LowerCopy(filterText);
    std::vector<BlueprintPaletteEntry> entries;

    for (const Asset::BlueprintNodeType& spec : Asset::GetBlueprintNodeTypes()) {
        if (!Asset::IsBlueprintNodeForGraph(spec, graphKind)) {
            continue;
        }
        if (availableComponents != nullptr && !spec.needs.empty() && !availableComponents->contains(spec.needs)) {
            continue;
        }
        if (compatibleInputPinType.has_value()) {
            const bool hasCompatibleInput = std::any_of(
                spec.inputs.begin(), spec.inputs.end(), [&](const Asset::BlueprintPinSpec& pin) {
                    return Core::CanConnectBlueprintPinTypes(*compatibleInputPinType, pin.type);
                });
            if (!hasCompatibleInput) {
                continue;
            }
        }
        if (!needle.empty()) {
            const bool matches = ContainsI(spec.title, needle) || ContainsI(spec.key, needle) ||
                                 ContainsI(spec.category, needle) || ContainsI(spec.keywords, needle);
            if (!matches) {
                continue;
            }
        }
        entries.push_back(
            BlueprintPaletteEntry{spec.key, spec.title, spec.category, spec.tooltip, spec.role});
    }

    return entries;
}

// ---------------------------------------------------------------------------
// Shortcuts
// ---------------------------------------------------------------------------

std::span<const BlueprintShortcutBinding> GetDefaultBlueprintShortcuts() noexcept { return kDefaultShortcuts; }

std::optional<BlueprintShortcutAction> MatchBlueprintShortcut(const std::string_view keyName, const bool ctrl,
                                                                    const bool shift, const bool alt) noexcept {
    for (const BlueprintShortcutBinding& b : kDefaultShortcuts) {
        if (b.ctrl == ctrl && b.shift == shift && b.alt == alt && EqualsIgnoreCase(b.keyName, keyName)) {
            return b.action;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Visualization & Accessibility
// ---------------------------------------------------------------------------

void BlueprintExecutionVisualization::TriggerWirePulse(const Asset::BlueprintWire& wire,
                                                             const float durationSeconds) {
    if (durationSeconds <= 0.0F) {
        return;
    }
    for (BlueprintWireFlowPulse& pulse : m_pulses) {
        if (pulse.fromNode == wire.fromNode && pulse.fromPin == wire.fromPin && pulse.toNode == wire.toNode &&
            pulse.toPin == wire.toPin) {
            pulse.remainingSeconds = std::max(pulse.remainingSeconds, durationSeconds);
            return;
        }
    }
    m_pulses.push_back(
        BlueprintWireFlowPulse{wire.fromNode, wire.fromPin, wire.toNode, wire.toPin, durationSeconds});
}

void BlueprintExecutionVisualization::Tick(const float deltaSeconds) noexcept {
    if (deltaSeconds <= 0.0F) {
        return;
    }
    for (BlueprintWireFlowPulse& pulse : m_pulses) {
        pulse.remainingSeconds -= deltaSeconds;
    }
    m_pulses.erase(std::remove_if(m_pulses.begin(), m_pulses.end(),
                                  [](const BlueprintWireFlowPulse& p) { return p.remainingSeconds <= 0.0F; }),
                   m_pulses.end());
}

std::string DescribeBlueprintNodeForAccessibility(const Asset::BlueprintAsset& blueprint,
                                                     const std::uint32_t nodeId, const std::string_view errorText) {
    const auto it = std::find_if(blueprint.nodes.begin(), blueprint.nodes.end(),
                                 [nodeId](const Asset::BlueprintNode& n) { return n.id == nodeId; });
    if (it == blueprint.nodes.end()) {
        return "Blueprint node #" + std::to_string(nodeId) + " (not found)";
    }
    const Asset::BlueprintNodeType* const spec = Asset::FindBlueprintNodeType(it->type);
    const Asset::BlueprintNodePins pins = Asset::ResolveBlueprintNodePins(blueprint, *it);

    std::size_t incoming = 0U;
    std::size_t outgoing = 0U;
    for (const Asset::BlueprintWire& w : blueprint.wires) {
        if (w.toNode == nodeId) ++incoming;
        if (w.fromNode == nodeId) ++outgoing;
    }

    std::ostringstream oss;
    oss << (spec != nullptr ? spec->title : it->type) << " (ID " << nodeId << ")";
    if (!it->variable.empty()) {
        oss << " variable '" << it->variable << "'";
    }
    oss << ": " << pins.inputs.size() << " inputs (" << incoming << " wired), " << pins.outputs.size()
        << " outputs (" << outgoing << " wired)";
    if (!errorText.empty()) {
        oss << "; error: " << errorText;
    }
    return oss.str();
}

} // namespace UVE::Blueprint::Editor
