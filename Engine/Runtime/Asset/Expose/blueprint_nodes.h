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
struct BlueprintPinSpecUVE final {
    std::string name;
    BlueprintPinTypeUVE type = BlueprintPinTypeUVE::Exec;
    std::string defaultText;
};

/// How a node turns into UVScript.
enum class BlueprintNodeRoleUVE : std::uint8_t {
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
};

/// One kind of node the engine can run: everything a blueprint can do is one of these.
struct BlueprintNodeTypeUVE final {
    std::string key;
    std::string title;
    /// "Events", "Flow Control", "Math|Float" ... (`|` nests a sub-category).
    std::string category;
    std::string keywords;
    BlueprintNodeRoleUVE role = BlueprintNodeRoleUVE::Action;
    std::vector<BlueprintPinSpecUVE> inputs;
    std::vector<BlueprintPinSpecUVE> outputs;
    /// Event: the UVScript event name.
    std::string event;
    /// Action: the statement.
    std::string code;
    /// Pure: one expression per output.
    std::vector<std::string> outputCode;
    std::string tooltip;
};

/// Every node type, in palette order.
[[nodiscard]] const std::vector<BlueprintNodeTypeUVE>& GetBlueprintNodeTypesUVE();
[[nodiscard]] const BlueprintNodeTypeUVE* FindBlueprintNodeTypeUVE(std::string_view key);

/// The pins a node on a graph has: its type's, with a variable's own type on Get / Set Variable.
struct BlueprintNodePinsUVE final {
    std::vector<BlueprintPinSpecUVE> inputs;
    std::vector<BlueprintPinSpecUVE> outputs;
};
[[nodiscard]] BlueprintNodePinsUVE ResolveBlueprintNodePinsUVE(const BlueprintAssetUVE& blueprint, const BlueprintNodeUVE& node);

/// The UVScript name of a pin type ("float", "vec3", "Object3D" ...).
[[nodiscard]] std::string_view BlueprintPinTypeScriptNameUVE(BlueprintPinTypeUVE type) noexcept;

/// A blueprint as UVScript source, with the node behind each line (for diagnostics) and the
/// problems found while translating (an unknown node, a loop of exec wires ...).
struct BlueprintTranslationUVE final {
    std::string source;
    /// lineNodes[line - 1]: the node that wrote that line (0 for the header and fields).
    std::vector<std::uint32_t> lineNodes;
    std::vector<std::pair<std::uint32_t, std::string>> problems;
    /// The node behind a 1-based source line, or 0.
    [[nodiscard]] std::uint32_t NodeAtLineUVE(std::uint32_t line) const noexcept {
        return line >= 1U && line <= lineNodes.size() ? lineNodes[line - 1U] : 0U;
    }
};

/// Translates `blueprint` for an object named `objectName` of kind `objectKind` (its UVScript
/// header: `entity <objectName> : <objectKind>`).
[[nodiscard]] BlueprintTranslationUVE TranslateBlueprintToUVScriptUVE(const BlueprintAssetUVE& blueprint, std::string_view objectName,
                                                                     std::string_view objectKind);

} // namespace UVE::Asset
