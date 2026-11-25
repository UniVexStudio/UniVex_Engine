// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "blueprint_asset.h"
#include "blueprint_nodes.h"

namespace UVE::Blueprint::Assets {

/// High-level kind of Blueprint asset stored in a UniVex project.
enum class BlueprintAssetKind : std::uint8_t {
    BlueprintClass = 0,
    WidgetBlueprint,
    FunctionLibrary,
    MacroLibrary,
    Interface,
};

[[nodiscard]] std::string_view BlueprintAssetKindName(BlueprintAssetKind kind) noexcept;

/// A reusable Blueprint function hosted inside a FunctionLibrary asset.
struct BlueprintFunctionDefinition final {
    std::string name;
    std::string category = "Functions";
    std::string tooltip;
    bool isPure = false;
    bool isStatic = true;
    std::vector<Asset::BlueprintPinSpec> inputs;
    std::vector<Asset::BlueprintPinSpec> outputs;
    Asset::BlueprintAsset bodyGraph;
};

/// A collection of stateless Blueprint functions callable from any Blueprint graph.
struct BlueprintFunctionLibraryAsset final {
    std::string libraryName;
    std::string description;
    std::vector<BlueprintFunctionDefinition> functions;
};

/// A reusable Blueprint macro expanding inline at compile/translation time.
struct BlueprintMacroDefinition final {
    std::string name;
    std::string category = "Macros";
    std::string tooltip;
    std::vector<Asset::BlueprintPinSpec> inputs;
    std::vector<Asset::BlueprintPinSpec> outputs;
    Asset::BlueprintAsset subgraph;
};

/// A collection of reusable Blueprint macros scoped to a target parent kind.
struct BlueprintMacroLibraryAsset final {
    std::string libraryName;
    std::string targetParentKind = "Object3D";
    std::string description;
    std::vector<BlueprintMacroDefinition> macros;
};

/// One method contract declared by a Blueprint Interface asset.
struct BlueprintInterfaceMethod final {
    std::string name;
    std::string tooltip;
    bool isConst = false;
    std::vector<Asset::BlueprintPinSpec> inputs;
    std::vector<Asset::BlueprintPinSpec> outputs;
};

/// A Blueprint Interface declaring methods that multiple Blueprint classes may implement.
struct BlueprintInterfaceAsset final {
    std::string interfaceName;
    std::string description;
    std::vector<BlueprintInterfaceMethod> methods;
};

/// Fast structural validity checks matching IsBlueprintAssetValid.
[[nodiscard]] bool IsFunctionLibraryAssetValid(const BlueprintFunctionLibraryAsset& library) noexcept;
[[nodiscard]] bool IsMacroLibraryAssetValid(const BlueprintMacroLibraryAsset& library) noexcept;
[[nodiscard]] bool IsInterfaceAssetValid(const BlueprintInterfaceAsset& iface) noexcept;

} // namespace UVE::Blueprint::Assets
