// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_asset_types.h"

#include <set>

namespace UVE::Blueprint::Assets {
namespace {

[[nodiscard]] bool ValidMemberName(const std::string& name) noexcept {
    return !name.empty() && name.size() <= Asset::kMaximumBlueprintNameBytes && !name.contains('\0');
}

[[nodiscard]] bool ValidPins(const std::vector<Asset::BlueprintPinSpec>& pins) noexcept {
    if (pins.size() > 64U) {
        return false;
    }
    for (const Asset::BlueprintPinSpec& pin : pins) {
        if (pin.name.size() > Asset::kMaximumBlueprintNameBytes || pin.name.contains('\0')) {
            return false;
        }
        if (pin.type > Asset::BlueprintPinType::StringArray) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string_view BlueprintAssetKindName(const BlueprintAssetKind kind) noexcept {
    switch (kind) {
        case BlueprintAssetKind::BlueprintClass: return "BlueprintClass";
        case BlueprintAssetKind::WidgetBlueprint: return "WidgetBlueprint";
        case BlueprintAssetKind::FunctionLibrary: return "FunctionLibrary";
        case BlueprintAssetKind::MacroLibrary: return "MacroLibrary";
        case BlueprintAssetKind::Interface: return "Interface";
    }
    return "BlueprintClass";
}

bool IsFunctionLibraryAssetValid(const BlueprintFunctionLibraryAsset& library) noexcept {
    if (!ValidMemberName(library.libraryName) || library.functions.size() > 512U) {
        return false;
    }
    std::set<std::string> names;
    for (const BlueprintFunctionDefinition& fn : library.functions) {
        if (!ValidMemberName(fn.name) || !names.insert(fn.name).second || !ValidPins(fn.inputs) ||
            !ValidPins(fn.outputs) || !Asset::IsBlueprintAssetValid(fn.bodyGraph)) {
            return false;
        }
    }
    return true;
}

bool IsMacroLibraryAssetValid(const BlueprintMacroLibraryAsset& library) noexcept {
    if (!ValidMemberName(library.libraryName) || !ValidMemberName(library.targetParentKind) ||
        library.macros.size() > 512U) {
        return false;
    }
    std::set<std::string> names;
    for (const BlueprintMacroDefinition& macro : library.macros) {
        if (!ValidMemberName(macro.name) || !names.insert(macro.name).second || !ValidPins(macro.inputs) ||
            !ValidPins(macro.outputs) || !Asset::IsBlueprintAssetValid(macro.subgraph)) {
            return false;
        }
    }
    return true;
}

bool IsInterfaceAssetValid(const BlueprintInterfaceAsset& iface) noexcept {
    if (!ValidMemberName(iface.interfaceName) || iface.methods.size() > 512U) {
        return false;
    }
    std::set<std::string> names;
    for (const BlueprintInterfaceMethod& method : iface.methods) {
        if (!ValidMemberName(method.name) || !names.insert(method.name).second || !ValidPins(method.inputs) ||
            !ValidPins(method.outputs)) {
            return false;
        }
    }
    return true;
}

} // namespace UVE::Blueprint::Assets
