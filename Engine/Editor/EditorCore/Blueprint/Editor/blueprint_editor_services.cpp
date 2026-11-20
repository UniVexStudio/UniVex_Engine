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

using JsonUVE = nlohmann::json;

constexpr std::string_view kClipboardSchemaUVE = "uve-blueprint-clipboard-v1";

[[nodiscard]] std::string LowerCopyUVE(const std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

[[nodiscard]] bool ContainsIUVE(const std::string_view hay, const std::string& needleLower) {
    if (needleLower.empty()) {
        return true;
    }
    return LowerCopyUVE(hay).find(needleLower) != std::string::npos;
}

[[nodiscard]] bool EqualsIgnoreCaseUVE(const std::string_view a, const std::string_view b) noexcept {
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

constexpr std::array<BlueprintShortcutBindingUVE, 16> kDefaultShortcutsUVE{{
    {BlueprintShortcutActionUVE::Compile, "F7", false, false, false, "Compile Blueprint"},
    {BlueprintShortcutActionUVE::Save, "S", true, false, false, "Save Blueprint"},
    {BlueprintShortcutActionUVE::Find, "F", true, false, false, "Find in Graph"},
    {BlueprintShortcutActionUVE::Undo, "Z", true, false, false, "Undo"},
    {BlueprintShortcutActionUVE::Redo, "Y", true, false, false, "Redo"},
    {BlueprintShortcutActionUVE::Copy, "C", true, false, false, "Copy Selection"},
    {BlueprintShortcutActionUVE::Paste, "V", true, false, false, "Paste Selection"},
    {BlueprintShortcutActionUVE::Cut, "X", true, false, false, "Cut Selection"},
    {BlueprintShortcutActionUVE::Duplicate, "D", true, false, false, "Duplicate Selection"},
    {BlueprintShortcutActionUVE::DeleteSelection, "Delete", false, false, false, "Delete Selection"},
    {BlueprintShortcutActionUVE::AddComment, "C", false, false, false, "Add Comment Box"},
    {BlueprintShortcutActionUVE::FrameAll, "Home", false, false, false, "Frame Entire Graph"},
    {BlueprintShortcutActionUVE::ToggleBreakpoint, "F9", false, false, false, "Toggle Breakpoint"},
    {BlueprintShortcutActionUVE::DebugContinue, "F5", false, false, false, "Continue Execution"},
    {BlueprintShortcutActionUVE::DebugStepOver, "F10", false, false, false, "Step Over"},
    {BlueprintShortcutActionUVE::DebugStepInto, "F11", false, false, false, "Step Into"},
}};

} // namespace

// ---------------------------------------------------------------------------
// BlueprintUndoStackUVE
// ---------------------------------------------------------------------------

BlueprintUndoStackUVE::BlueprintUndoStackUVE(const std::size_t maxHistory) noexcept
    : m_maxHistory(maxHistory < 2U ? 2U : maxHistory) {}

void BlueprintUndoStackUVE::ResetUVE(const Asset::BlueprintAssetUVE& initialState, std::string label) {
    m_history.clear();
    m_history.push_back(BlueprintUndoEntryUVE{std::move(label), initialState});
    m_cursor = 0U;
}

void BlueprintUndoStackUVE::PushSnapshotUVE(const Asset::BlueprintAssetUVE& newState, std::string label) {
    if (!m_history.empty() && m_history[m_cursor].snapshot == newState) {
        return;
    }
    if (!m_history.empty() && m_cursor + 1U < m_history.size()) {
        m_history.erase(m_history.begin() + static_cast<std::ptrdiff_t>(m_cursor + 1U), m_history.end());
    }
    m_history.push_back(BlueprintUndoEntryUVE{std::move(label), newState});
    if (m_history.size() > m_maxHistory) {
        m_history.erase(m_history.begin());
    }
    m_cursor = m_history.size() - 1U;
}

bool BlueprintUndoStackUVE::CanUndoUVE() const noexcept { return !m_history.empty() && m_cursor > 0U; }

bool BlueprintUndoStackUVE::CanRedoUVE() const noexcept {
    return !m_history.empty() && m_cursor + 1U < m_history.size();
}

std::optional<Asset::BlueprintAssetUVE> BlueprintUndoStackUVE::UndoUVE() {
    if (!CanUndoUVE()) {
        return std::nullopt;
    }
    --m_cursor;
    return m_history[m_cursor].snapshot;
}

std::optional<Asset::BlueprintAssetUVE> BlueprintUndoStackUVE::RedoUVE() {
    if (!CanRedoUVE()) {
        return std::nullopt;
    }
    ++m_cursor;
    return m_history[m_cursor].snapshot;
}

std::string_view BlueprintUndoStackUVE::CurrentLabelUVE() const noexcept {
    if (m_history.empty()) {
        return {};
    }
    return m_history[m_cursor].label;
}

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------

std::string CopyBlueprintSelectionUVE(const Asset::BlueprintAssetUVE& blueprint,
                                      const std::span<const std::uint32_t> selectedNodeIds) {
    std::set<std::uint32_t> selected(selectedNodeIds.begin(), selectedNodeIds.end());
    JsonUVE nodes = JsonUVE::array();
    for (const Asset::BlueprintNodeUVE& n : blueprint.nodes) {
        if (!selected.contains(n.id)) {
            continue;
        }
        JsonUVE entry{{"id", n.id}, {"type", n.type}, {"x", n.x}, {"y", n.y}, {"inputs", n.inputValues}};
        if (!n.variable.empty()) {
            entry["variable"] = n.variable;
        }
        nodes.push_back(std::move(entry));
    }
    JsonUVE wires = JsonUVE::array();
    for (const Asset::BlueprintWireUVE& w : blueprint.wires) {
        if (selected.contains(w.fromNode) && selected.contains(w.toNode)) {
            wires.push_back({w.fromNode, w.fromPin, w.toNode, w.toPin});
        }
    }
    const JsonUVE payload{{"schema", kClipboardSchemaUVE},
                          {"nodes", std::move(nodes)},
                          {"wires", std::move(wires)}};
    return payload.dump();
}

BlueprintPasteResultUVE PasteBlueprintSelectionUVE(Asset::BlueprintAssetUVE& target,
                                                   const std::string_view clipboardJson,
                                                   const float offsetX, const float offsetY) {
    BlueprintPasteResultUVE result;
    JsonUVE payload;
    try {
        payload = JsonUVE::parse(clipboardJson);
    } catch (const std::exception& e) {
        result.error = std::string("Invalid clipboard JSON: ") + e.what();
        return result;
    }
    if (!payload.is_object() || payload.value("schema", std::string{}) != kClipboardSchemaUVE) {
        result.error = "Clipboard does not contain a Blueprint selection.";
        return result;
    }

    Core::BlueprintIdGeneratorUVE idGen;
    idGen.SeedFromAssetUVE(target);

    std::unordered_map<std::uint32_t, std::uint32_t> idRemap;
    for (const JsonUVE& nJson : payload.value("nodes", JsonUVE::array())) {
        const std::uint32_t oldId = nJson.value("id", 0U);
        const std::string type = nJson.value("type", std::string{});
        if (oldId == 0U || type.empty()) {
            continue;
        }
        const std::uint32_t newId = idGen.AllocateUVE();
        idRemap[oldId] = newId;

        Asset::BlueprintNodeUVE node;
        node.id = newId;
        node.type = type;
        node.variable = nJson.value("variable", std::string{});
        node.x = nJson.value("x", 0.0F) + offsetX;
        node.y = nJson.value("y", 0.0F) + offsetY;
        node.inputValues = nJson.value("inputs", std::vector<std::string>{});
        target.nodes.push_back(std::move(node));
        result.createdNodeIds.push_back(newId);
    }

    for (const JsonUVE& wJson : payload.value("wires", JsonUVE::array())) {
        if (!wJson.is_array() || wJson.size() != 4U) {
            continue;
        }
        const auto fromIt = idRemap.find(wJson[0].get<std::uint32_t>());
        const auto toIt = idRemap.find(wJson[2].get<std::uint32_t>());
        if (fromIt == idRemap.end() || toIt == idRemap.end()) {
            continue;
        }
        target.wires.push_back(Asset::BlueprintWireUVE{fromIt->second, wJson[1].get<std::uint32_t>(), toIt->second,
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

std::vector<BlueprintGraphSearchHitUVE> SearchBlueprintGraphUVE(const Asset::BlueprintAssetUVE& blueprint,
                                                                const std::string_view query) {
    std::vector<BlueprintGraphSearchHitUVE> hits;
    const std::string needle = LowerCopyUVE(query);
    if (needle.empty()) {
        return hits;
    }

    for (const Asset::BlueprintVariableUVE& v : blueprint.variables) {
        if (ContainsIUVE(v.name, needle) || ContainsIUVE(v.defaultText, needle)) {
            hits.push_back(BlueprintGraphSearchHitUVE{BlueprintSearchHitKindUVE::Variable, 0U, "Variable: " + v.name});
        }
    }

    for (const Asset::BlueprintNodeUVE& n : blueprint.nodes) {
        const Asset::BlueprintNodeTypeUVE* const spec = Asset::FindBlueprintNodeTypeUVE(n.type);
        const std::string title = spec != nullptr ? spec->title : n.type;
        if (ContainsIUVE(title, needle)) {
            hits.push_back(BlueprintGraphSearchHitUVE{BlueprintSearchHitKindUVE::NodeTitle, n.id, title});
        } else if (ContainsIUVE(n.type, needle) || ContainsIUVE(n.variable, needle)) {
            hits.push_back(BlueprintGraphSearchHitUVE{BlueprintSearchHitKindUVE::NodeKey, n.id,
                                                      title + " (" + n.type + ")"});
        }
        for (const std::string& val : n.inputValues) {
            if (!val.empty() && ContainsIUVE(val, needle)) {
                hits.push_back(BlueprintGraphSearchHitUVE{BlueprintSearchHitKindUVE::NodeInputValue, n.id,
                                                          title + " = " + val});
                break;
            }
        }
    }

    for (const Asset::BlueprintCommentUVE& c : blueprint.comments) {
        if (ContainsIUVE(c.title, needle)) {
            hits.push_back(BlueprintGraphSearchHitUVE{BlueprintSearchHitKindUVE::Comment, 0U, "Comment: " + c.title});
        }
    }

    return hits;
}

// ---------------------------------------------------------------------------
// Context Menu / Node Palette
// ---------------------------------------------------------------------------

std::vector<BlueprintPaletteEntryUVE> BuildBlueprintPaletteUVE(
    const Asset::BlueprintGraphKindUVE graphKind, const std::string_view filterText,
    const std::optional<Asset::BlueprintPinTypeUVE> compatibleInputPinType,
    const std::set<std::string>* const availableComponents) {
    const std::string needle = LowerCopyUVE(filterText);
    std::vector<BlueprintPaletteEntryUVE> entries;

    for (const Asset::BlueprintNodeTypeUVE& spec : Asset::GetBlueprintNodeTypesUVE()) {
        if (!Asset::IsBlueprintNodeForGraphUVE(spec, graphKind)) {
            continue;
        }
        if (availableComponents != nullptr && !spec.needs.empty() && !availableComponents->contains(spec.needs)) {
            continue;
        }
        if (compatibleInputPinType.has_value()) {
            const bool hasCompatibleInput = std::any_of(
                spec.inputs.begin(), spec.inputs.end(), [&](const Asset::BlueprintPinSpecUVE& pin) {
                    return Core::CanConnectBlueprintPinTypesUVE(*compatibleInputPinType, pin.type);
                });
            if (!hasCompatibleInput) {
                continue;
            }
        }
        if (!needle.empty()) {
            const bool matches = ContainsIUVE(spec.title, needle) || ContainsIUVE(spec.key, needle) ||
                                 ContainsIUVE(spec.category, needle) || ContainsIUVE(spec.keywords, needle);
            if (!matches) {
                continue;
            }
        }
        entries.push_back(
            BlueprintPaletteEntryUVE{spec.key, spec.title, spec.category, spec.tooltip, spec.role});
    }

    return entries;
}

// ---------------------------------------------------------------------------
// Shortcuts
// ---------------------------------------------------------------------------

std::span<const BlueprintShortcutBindingUVE> GetDefaultBlueprintShortcutsUVE() noexcept { return kDefaultShortcutsUVE; }

std::optional<BlueprintShortcutActionUVE> MatchBlueprintShortcutUVE(const std::string_view keyName, const bool ctrl,
                                                                    const bool shift, const bool alt) noexcept {
    for (const BlueprintShortcutBindingUVE& b : kDefaultShortcutsUVE) {
        if (b.ctrl == ctrl && b.shift == shift && b.alt == alt && EqualsIgnoreCaseUVE(b.keyName, keyName)) {
            return b.action;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Visualization & Accessibility
// ---------------------------------------------------------------------------

void BlueprintExecutionVisualizationUVE::TriggerWirePulseUVE(const Asset::BlueprintWireUVE& wire,
                                                             const float durationSeconds) {
    if (durationSeconds <= 0.0F) {
        return;
    }
    for (BlueprintWireFlowPulseUVE& pulse : m_pulses) {
        if (pulse.fromNode == wire.fromNode && pulse.fromPin == wire.fromPin && pulse.toNode == wire.toNode &&
            pulse.toPin == wire.toPin) {
            pulse.remainingSeconds = std::max(pulse.remainingSeconds, durationSeconds);
            return;
        }
    }
    m_pulses.push_back(
        BlueprintWireFlowPulseUVE{wire.fromNode, wire.fromPin, wire.toNode, wire.toPin, durationSeconds});
}

void BlueprintExecutionVisualizationUVE::TickUVE(const float deltaSeconds) noexcept {
    if (deltaSeconds <= 0.0F) {
        return;
    }
    for (BlueprintWireFlowPulseUVE& pulse : m_pulses) {
        pulse.remainingSeconds -= deltaSeconds;
    }
    m_pulses.erase(std::remove_if(m_pulses.begin(), m_pulses.end(),
                                  [](const BlueprintWireFlowPulseUVE& p) { return p.remainingSeconds <= 0.0F; }),
                   m_pulses.end());
}

std::string DescribeBlueprintNodeForAccessibilityUVE(const Asset::BlueprintAssetUVE& blueprint,
                                                     const std::uint32_t nodeId, const std::string_view errorText) {
    const auto it = std::find_if(blueprint.nodes.begin(), blueprint.nodes.end(),
                                 [nodeId](const Asset::BlueprintNodeUVE& n) { return n.id == nodeId; });
    if (it == blueprint.nodes.end()) {
        return "Blueprint node #" + std::to_string(nodeId) + " (not found)";
    }
    const Asset::BlueprintNodeTypeUVE* const spec = Asset::FindBlueprintNodeTypeUVE(it->type);
    const Asset::BlueprintNodePinsUVE pins = Asset::ResolveBlueprintNodePinsUVE(blueprint, *it);

    std::size_t incoming = 0U;
    std::size_t outgoing = 0U;
    for (const Asset::BlueprintWireUVE& w : blueprint.wires) {
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
