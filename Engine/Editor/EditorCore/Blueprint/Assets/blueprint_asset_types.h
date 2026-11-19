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
enum class BlueprintAssetKindUVE : std::uint8_t {
    BlueprintClass = 0,
    WidgetBlueprint,
    FunctionLibrary,
    MacroLibrary,
    Interface,
};

[[nodiscard]] std::string_view BlueprintAssetKindNameUVE(BlueprintAssetKindUVE kind) noexcept;

/// A reusable Blueprint function hosted inside a FunctionLibrary asset.
struct BlueprintFunctionDefinitionUVE final {
    std::string name;
    std::string category = "Functions";
    std::string tooltip;
    bool isPure = false;
    bool isStatic = true;
    std::vector<Asset::BlueprintPinSpecUVE> inputs;
    std::vector<Asset::BlueprintPinSpecUVE> outputs;
    Asset::BlueprintAssetUVE bodyGraph;
};

/// A collection of stateless Blueprint functions callable from any Blueprint graph.
struct BlueprintFunctionLibraryAssetUVE final {
    std::string libraryName;
    std::string description;
    std::vector<BlueprintFunctionDefinitionUVE> functions;
};

/// A reusable Blueprint macro expanding inline at compile/translation time.
struct BlueprintMacroDefinitionUVE final {
    std::string name;
    std::string category = "Macros";
    std::string tooltip;
    std::vector<Asset::BlueprintPinSpecUVE> inputs;
    std::vector<Asset::BlueprintPinSpecUVE> outputs;
    Asset::BlueprintAssetUVE subgraph;
};

/// A collection of reusable Blueprint macros scoped to a target parent kind.
struct BlueprintMacroLibraryAssetUVE final {
    std::string libraryName;
    std::string targetParentKind = "Object3D";
    std::string description;
    std::vector<BlueprintMacroDefinitionUVE> macros;
};

/// One method contract declared by a Blueprint Interface asset.
struct BlueprintInterfaceMethodUVE final {
    std::string name;
    std::string tooltip;
    bool isConst = false;
    std::vector<Asset::BlueprintPinSpecUVE> inputs;
    std::vector<Asset::BlueprintPinSpecUVE> outputs;
};

/// A Blueprint Interface declaring methods that multiple Blueprint classes may implement.
struct BlueprintInterfaceAssetUVE final {
    std::string interfaceName;
    std::string description;
    std::vector<BlueprintInterfaceMethodUVE> methods;
};

/// Fast structural validity checks matching IsBlueprintAssetValidUVE.
[[nodiscard]] bool IsFunctionLibraryAssetValidUVE(const BlueprintFunctionLibraryAssetUVE& library) noexcept;
[[nodiscard]] bool IsMacroLibraryAssetValidUVE(const BlueprintMacroLibraryAssetUVE& library) noexcept;
[[nodiscard]] bool IsInterfaceAssetValidUVE(const BlueprintInterfaceAssetUVE& iface) noexcept;

} // namespace UVE::Blueprint::Assets
