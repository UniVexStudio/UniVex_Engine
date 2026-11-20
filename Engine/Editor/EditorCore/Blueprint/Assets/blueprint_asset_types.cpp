// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_asset_types.h"

#include <set>

namespace UVE::Blueprint::Assets {
namespace {

[[nodiscard]] bool ValidMemberNameUVE(const std::string& name) noexcept {
    return !name.empty() && name.size() <= Asset::kMaximumBlueprintNameBytesUVE && !name.contains('\0');
}

[[nodiscard]] bool ValidPinsUVE(const std::vector<Asset::BlueprintPinSpecUVE>& pins) noexcept {
    if (pins.size() > 64U) {
        return false;
    }
    for (const Asset::BlueprintPinSpecUVE& pin : pins) {
        if (pin.name.size() > Asset::kMaximumBlueprintNameBytesUVE || pin.name.contains('\0')) {
            return false;
        }
        if (pin.type > Asset::BlueprintPinTypeUVE::StringArray) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string_view BlueprintAssetKindNameUVE(const BlueprintAssetKindUVE kind) noexcept {
    switch (kind) {
        case BlueprintAssetKindUVE::BlueprintClass: return "BlueprintClass";
        case BlueprintAssetKindUVE::WidgetBlueprint: return "WidgetBlueprint";
        case BlueprintAssetKindUVE::FunctionLibrary: return "FunctionLibrary";
        case BlueprintAssetKindUVE::MacroLibrary: return "MacroLibrary";
        case BlueprintAssetKindUVE::Interface: return "Interface";
    }
    return "BlueprintClass";
}

bool IsFunctionLibraryAssetValidUVE(const BlueprintFunctionLibraryAssetUVE& library) noexcept {
    if (!ValidMemberNameUVE(library.libraryName) || library.functions.size() > 512U) {
        return false;
    }
    std::set<std::string> names;
    for (const BlueprintFunctionDefinitionUVE& fn : library.functions) {
        if (!ValidMemberNameUVE(fn.name) || !names.insert(fn.name).second || !ValidPinsUVE(fn.inputs) ||
            !ValidPinsUVE(fn.outputs) || !Asset::IsBlueprintAssetValidUVE(fn.bodyGraph)) {
            return false;
        }
    }
    return true;
}

bool IsMacroLibraryAssetValidUVE(const BlueprintMacroLibraryAssetUVE& library) noexcept {
    if (!ValidMemberNameUVE(library.libraryName) || !ValidMemberNameUVE(library.targetParentKind) ||
        library.macros.size() > 512U) {
        return false;
    }
    std::set<std::string> names;
    for (const BlueprintMacroDefinitionUVE& macro : library.macros) {
        if (!ValidMemberNameUVE(macro.name) || !names.insert(macro.name).second || !ValidPinsUVE(macro.inputs) ||
            !ValidPinsUVE(macro.outputs) || !Asset::IsBlueprintAssetValidUVE(macro.subgraph)) {
            return false;
        }
    }
    return true;
}

bool IsInterfaceAssetValidUVE(const BlueprintInterfaceAssetUVE& iface) noexcept {
    if (!ValidMemberNameUVE(iface.interfaceName) || iface.methods.size() > 512U) {
        return false;
    }
    std::set<std::string> names;
    for (const BlueprintInterfaceMethodUVE& method : iface.methods) {
        if (!ValidMemberNameUVE(method.name) || !names.insert(method.name).second || !ValidPinsUVE(method.inputs) ||
            !ValidPinsUVE(method.outputs)) {
            return false;
        }
    }
    return true;
}

} // namespace UVE::Blueprint::Assets
