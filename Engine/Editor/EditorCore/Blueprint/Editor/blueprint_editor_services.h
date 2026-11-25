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

struct BlueprintUndoEntry final {
    std::string label;
    Asset::BlueprintAsset snapshot;
};

class BlueprintUndoStack final {
public:
    explicit BlueprintUndoStack(std::size_t maxHistory = 64U) noexcept;

    void Reset(const Asset::BlueprintAsset& initialState, std::string label = "Initial");
    void PushSnapshot(const Asset::BlueprintAsset& newState, std::string label);

    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;
    [[nodiscard]] std::optional<Asset::BlueprintAsset> Undo();
    [[nodiscard]] std::optional<Asset::BlueprintAsset> Redo();
    [[nodiscard]] std::string_view CurrentLabel() const noexcept;
    [[nodiscard]] std::size_t HistoryCount() const noexcept { return m_history.size(); }

private:
    std::size_t m_maxHistory = 64U;
    std::vector<BlueprintUndoEntry> m_history;
    std::size_t m_cursor = 0U;
};

// ---------------------------------------------------------------------------
// Clipboard (Copy / Paste Selection with ID Remapping)
// ---------------------------------------------------------------------------

struct BlueprintPasteResult final {
    bool success = false;
    std::vector<std::uint32_t> createdNodeIds;
    std::size_t createdWireCount = 0U;
    std::string error;
};

/// Serializes the selected node IDs (and wires connecting selected nodes) into a JSON clipboard payload.
[[nodiscard]] std::string CopyBlueprintSelection(const Asset::BlueprintAsset& blueprint,
                                                    std::span<const std::uint32_t> selectedNodeIds);

/// Pastes a JSON clipboard payload into `target`, allocating fresh non-colliding IDs and offsetting positions.
[[nodiscard]] BlueprintPasteResult PasteBlueprintSelection(Asset::BlueprintAsset& target,
                                                                 std::string_view clipboardJson,
                                                                 float offsetX = 40.0F, float offsetY = 40.0F);

// ---------------------------------------------------------------------------
// Graph Search
// ---------------------------------------------------------------------------

enum class BlueprintSearchHitKind : std::uint8_t {
    NodeTitle = 0,
    NodeKey,
    NodeInputValue,
    Variable,
    Comment,
};

struct BlueprintGraphSearchHit final {
    BlueprintSearchHitKind kind = BlueprintSearchHitKind::NodeTitle;
    std::uint32_t nodeId = 0U;
    std::string summary;
};

[[nodiscard]] std::vector<BlueprintGraphSearchHit> SearchBlueprintGraph(
    const Asset::BlueprintAsset& blueprint, std::string_view query);

// ---------------------------------------------------------------------------
// Context Menu / Node Palette
// ---------------------------------------------------------------------------

struct BlueprintPaletteEntry final {
    std::string key;
    std::string title;
    std::string category;
    std::string tooltip;
    Asset::BlueprintNodeRole role = Asset::BlueprintNodeRole::Action;
};

/// Builds a filtered palette list for graph context menus, optionally requiring pin compatibility.
[[nodiscard]] std::vector<BlueprintPaletteEntry> BuildBlueprintPalette(
    Asset::BlueprintGraphKind graphKind, std::string_view filterText = {},
    std::optional<Asset::BlueprintPinType> compatibleInputPinType = std::nullopt,
    const std::set<std::string>* availableComponents = nullptr);

// ---------------------------------------------------------------------------
// Keyboard Shortcuts
// ---------------------------------------------------------------------------

enum class BlueprintShortcutAction : std::uint8_t {
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

struct BlueprintShortcutBinding final {
    BlueprintShortcutAction action = BlueprintShortcutAction::Compile;
    std::string_view keyName;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    std::string_view description;
};

[[nodiscard]] std::span<const BlueprintShortcutBinding> GetDefaultBlueprintShortcuts() noexcept;
[[nodiscard]] std::optional<BlueprintShortcutAction> MatchBlueprintShortcut(std::string_view keyName, bool ctrl,
                                                                                  bool shift, bool alt) noexcept;

// ---------------------------------------------------------------------------
// Execution Visualization & Accessibility
// ---------------------------------------------------------------------------

struct BlueprintWireFlowPulse final {
    std::uint32_t fromNode = 0U;
    std::uint32_t fromPin = 0U;
    std::uint32_t toNode = 0U;
    std::uint32_t toPin = 0U;
    float remainingSeconds = 0.0F;
};

class BlueprintExecutionVisualization final {
public:
    void TriggerWirePulse(const Asset::BlueprintWire& wire, float durationSeconds = 0.75F);
    void Tick(float deltaSeconds) noexcept;
    void Clear() noexcept { m_pulses.clear(); }
    [[nodiscard]] const std::vector<BlueprintWireFlowPulse>& ActivePulses() const noexcept { return m_pulses; }

private:
    std::vector<BlueprintWireFlowPulse> m_pulses;
};

/// Builds a concise, screen-reader-friendly description of a node, its connections, and optional error state.
[[nodiscard]] std::string DescribeBlueprintNodeForAccessibility(const Asset::BlueprintAsset& blueprint,
                                                                   std::uint32_t nodeId,
                                                                   std::string_view errorText = {});

} // namespace UVE::Blueprint::Editor
