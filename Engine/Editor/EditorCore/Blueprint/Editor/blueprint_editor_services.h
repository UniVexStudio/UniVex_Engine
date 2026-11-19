// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "blueprint_asset.h"
#include "blueprint_nodes.h"

namespace UVE::Blueprint::Editor {

// ---------------------------------------------------------------------------
// Undo / Redo Stack
// ---------------------------------------------------------------------------

struct BlueprintUndoEntryUVE final {
    std::string label;
    Asset::BlueprintAssetUVE snapshot;
};

class BlueprintUndoStackUVE final {
public:
    explicit BlueprintUndoStackUVE(std::size_t maxHistory = 64U) noexcept;

    void ResetUVE(const Asset::BlueprintAssetUVE& initialState, std::string label = "Initial");
    void PushSnapshotUVE(const Asset::BlueprintAssetUVE& newState, std::string label);

    [[nodiscard]] bool CanUndoUVE() const noexcept;
    [[nodiscard]] bool CanRedoUVE() const noexcept;
    [[nodiscard]] std::optional<Asset::BlueprintAssetUVE> UndoUVE();
    [[nodiscard]] std::optional<Asset::BlueprintAssetUVE> RedoUVE();
    [[nodiscard]] std::string_view CurrentLabelUVE() const noexcept;
    [[nodiscard]] std::size_t HistoryCountUVE() const noexcept { return m_history.size(); }

private:
    std::size_t m_maxHistory = 64U;
    std::vector<BlueprintUndoEntryUVE> m_history;
    std::size_t m_cursor = 0U;
};

// ---------------------------------------------------------------------------
// Clipboard (Copy / Paste Selection with ID Remapping)
// ---------------------------------------------------------------------------

struct BlueprintPasteResultUVE final {
    bool success = false;
    std::vector<std::uint32_t> createdNodeIds;
    std::size_t createdWireCount = 0U;
    std::string error;
};

/// Serializes the selected node IDs (and wires connecting selected nodes) into a JSON clipboard payload.
[[nodiscard]] std::string CopyBlueprintSelectionUVE(const Asset::BlueprintAssetUVE& blueprint,
                                                    std::span<const std::uint32_t> selectedNodeIds);

/// Pastes a JSON clipboard payload into `target`, allocating fresh non-colliding IDs and offsetting positions.
[[nodiscard]] BlueprintPasteResultUVE PasteBlueprintSelectionUVE(Asset::BlueprintAssetUVE& target,
                                                                 std::string_view clipboardJson,
                                                                 float offsetX = 40.0F, float offsetY = 40.0F);

// ---------------------------------------------------------------------------
// Graph Search
// ---------------------------------------------------------------------------

enum class BlueprintSearchHitKindUVE : std::uint8_t {
    NodeTitle = 0,
    NodeKey,
    NodeInputValue,
    Variable,
    Comment,
};

struct BlueprintGraphSearchHitUVE final {
    BlueprintSearchHitKindUVE kind = BlueprintSearchHitKindUVE::NodeTitle;
    std::uint32_t nodeId = 0U;
    std::string summary;
};

[[nodiscard]] std::vector<BlueprintGraphSearchHitUVE> SearchBlueprintGraphUVE(
    const Asset::BlueprintAssetUVE& blueprint, std::string_view query);

// ---------------------------------------------------------------------------
// Context Menu / Node Palette
// ---------------------------------------------------------------------------

struct BlueprintPaletteEntryUVE final {
    std::string key;
    std::string title;
    std::string category;
    std::string tooltip;
    Asset::BlueprintNodeRoleUVE role = Asset::BlueprintNodeRoleUVE::Action;
};

/// Builds a filtered palette list for graph context menus, optionally requiring pin compatibility.
[[nodiscard]] std::vector<BlueprintPaletteEntryUVE> BuildBlueprintPaletteUVE(
    Asset::BlueprintGraphKindUVE graphKind, std::string_view filterText = {},
    std::optional<Asset::BlueprintPinTypeUVE> compatibleInputPinType = std::nullopt,
    const std::set<std::string>* availableComponents = nullptr);

// ---------------------------------------------------------------------------
// Keyboard Shortcuts
// ---------------------------------------------------------------------------

enum class BlueprintShortcutActionUVE : std::uint8_t {
    Compile = 0,
    Save,
    Find,
    Undo,
    Redo,
    Copy,
    Paste,
    Cut,
    Duplicate,
    DeleteSelection,
    AddComment,
    FrameAll,
    ToggleBreakpoint,
    DebugContinue,
    DebugStepOver,
    DebugStepInto,
};

struct BlueprintShortcutBindingUVE final {
    BlueprintShortcutActionUVE action = BlueprintShortcutActionUVE::Compile;
    std::string_view keyName;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    std::string_view description;
};

[[nodiscard]] std::span<const BlueprintShortcutBindingUVE> GetDefaultBlueprintShortcutsUVE() noexcept;
[[nodiscard]] std::optional<BlueprintShortcutActionUVE> MatchBlueprintShortcutUVE(std::string_view keyName, bool ctrl,
                                                                                  bool shift, bool alt) noexcept;

// ---------------------------------------------------------------------------
// Execution Visualization & Accessibility
// ---------------------------------------------------------------------------

struct BlueprintWireFlowPulseUVE final {
    std::uint32_t fromNode = 0U;
    std::uint32_t fromPin = 0U;
    std::uint32_t toNode = 0U;
    std::uint32_t toPin = 0U;
    float remainingSeconds = 0.0F;
};

class BlueprintExecutionVisualizationUVE final {
public:
    void TriggerWirePulseUVE(const Asset::BlueprintWireUVE& wire, float durationSeconds = 0.75F);
    void TickUVE(float deltaSeconds) noexcept;
    void ClearUVE() noexcept { m_pulses.clear(); }
    [[nodiscard]] const std::vector<BlueprintWireFlowPulseUVE>& ActivePulsesUVE() const noexcept { return m_pulses; }

private:
    std::vector<BlueprintWireFlowPulseUVE> m_pulses;
};

/// Builds a concise, screen-reader-friendly description of a node, its connections, and optional error state.
[[nodiscard]] std::string DescribeBlueprintNodeForAccessibilityUVE(const Asset::BlueprintAssetUVE& blueprint,
                                                                   std::uint32_t nodeId,
                                                                   std::string_view errorText = {});

} // namespace UVE::Blueprint::Editor
