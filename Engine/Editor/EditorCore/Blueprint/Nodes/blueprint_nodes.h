// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "blueprint_asset.h"

namespace UVE::Asset {

/// A pin of a blueprint node: its label, its type, and the value an unwired input starts with.
struct BlueprintPinSpec final {
    std::string name;
    BlueprintPinType type = BlueprintPinType::Exec;
    std::string defaultText;
};

/// How a node turns into UVScript.
enum class BlueprintNodeRole : std::uint8_t {
    Event,        ///< starts a chain: `on <event>(args):`
    Action,       ///< one statement (`code`, `{n}` = input pin n's expression), then its exec output
    Pure,         ///< an expression per output (`outputCode`), no exec pins
    Branch,
    Sequence,
    ForLoop,
    While,
    DoOnce,
    FlipFlop,
    Delay,
    IsValid,
    GetVariable,  ///< the node's `variable`
    SetVariable,
    Gate,         ///< exec inputs Enter, Open, Close, Toggle: Enter passes while open
    MultiGate,    ///< each Enter goes out the next output in turn (round again when Loop)
    DoN,          ///< passes the first N times, until Reset
    RetriggerableDelay, ///< waits, starting over each time it is entered
    SwitchInt,    ///< one output per whole number, else Default
    SwitchString, ///< one output per named case, else Default
    ForEach,      ///< the Loop Body once per item of an array (Element, Index), then Completed
};

/// One kind of node the engine can run: everything a blueprint can do is one of these.
struct BlueprintNodeType final {
    std::string key;
    std::string title;
    /// "Events", "Flow Control", "Math|Float" ... (`|` nests a sub-category).
    std::string category;
    std::string keywords;
    BlueprintNodeRole role = BlueprintNodeRole::Action;
    std::vector<BlueprintPinSpec> inputs;
    std::vector<BlueprintPinSpec> outputs;
    /// Event: the UVScript event name.
    std::string event;
    /// Action: the statement.
    std::string code;
    /// Pure: one expression per output.
    std::vector<std::string> outputCode;
    std::string tooltip;
    /// The graphs it belongs in (BlueprintGraphKind bits): an object's graph, a widget's, or both.
    std::uint8_t graphs = 0x3U;
    /// A component the object needs for it ("CharacterController", "Light", ...); empty: any object.
    std::string needs;
};

/// Which graph a node is offered in.
enum class BlueprintGraphKind : std::uint8_t { Object = 0x1U, Widget = 0x2U };
[[nodiscard]] constexpr bool IsBlueprintNodeForGraph(const BlueprintNodeType& type, const BlueprintGraphKind graph) noexcept {
    return (type.graphs & static_cast<std::uint8_t>(graph)) != 0U;
}

/// Every node type, in palette order.
[[nodiscard]] const std::vector<BlueprintNodeType>& GetBlueprintNodeTypes();
[[nodiscard]] const BlueprintNodeType* FindBlueprintNodeType(std::string_view key);

/// The pins a node on a graph has: its type's, with a variable's own type on Get / Set Variable.
struct BlueprintNodePins final {
    std::vector<BlueprintPinSpec> inputs;
    std::vector<BlueprintPinSpec> outputs;
};
[[nodiscard]] BlueprintNodePins ResolveBlueprintNodePins(const BlueprintAsset& blueprint, const BlueprintNode& node);

/// The UVScript name of a pin type ("float", "vec3", "Object3D" ...).
[[nodiscard]] std::string_view BlueprintPinTypeScriptName(BlueprintPinType type) noexcept;

/// A blueprint as UVScript source, with the node behind each line (for diagnostics) and the
/// problems found while translating (an unknown node, a loop of exec wires ...).
struct BlueprintTranslation final {
    std::string source;
    /// lineNodes[line - 1]: the node that wrote that line (0 for the header and fields).
    std::vector<std::uint32_t> lineNodes;
    std::vector<std::pair<std::uint32_t, std::string>> problems;
    /// The node behind a 1-based source line, or 0.
    [[nodiscard]] std::uint32_t NodeAtLine(std::uint32_t line) const noexcept {
        return line >= 1U && line <= lineNodes.size() ? lineNodes[line - 1U] : 0U;
    }
};

/// Translates `blueprint` for an object named `objectName` of kind `objectKind` (its UVScript
/// header: `entity <objectName> : <objectKind>`).
[[nodiscard]] BlueprintTranslation TranslateBlueprintToUVScript(const BlueprintAsset& blueprint, std::string_view objectName,
                                                                     std::string_view objectKind);

} // namespace UVE::Asset
