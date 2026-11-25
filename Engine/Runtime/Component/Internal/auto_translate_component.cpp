// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "auto_translate_component.h"

namespace UVE::Scene {

bool IsLocalizeModeValid(const LocalizeMode mode) noexcept {
    switch (mode) {
        case LocalizeMode::Inherit:
        case LocalizeMode::Localized:
        case LocalizeMode::Literal:
            return true;
    }
    return false;
}

bool IsAutoTranslateComponentValid(const AutoTranslateComponent& component) noexcept {
    return IsLocalizeModeValid(component.mode) &&
           IsLocalizeModeValid(component.resolvedModeInHierarchy);
}

LocalizeMode ResolveLocalizeMode(const LocalizeMode mode,
                                       const LocalizeMode parentMode) noexcept {
    const LocalizeMode safeParent =
        (!IsLocalizeModeValid(parentMode) || parentMode == LocalizeMode::Inherit)
            ? LocalizeMode::Localized
            : parentMode;
    if (!IsLocalizeModeValid(mode) || mode == LocalizeMode::Inherit) {
        return safeParent;
    }
    return mode;
}

LocalizeMode ResolveHierarchyLocalizeMode(
    AutoTranslateComponent* const component,
    const LocalizeMode parentResolvedMode) noexcept {
    if (component == nullptr) {
        return ResolveLocalizeMode(LocalizeMode::Inherit, parentResolvedMode);
    }
    const LocalizeMode resolved = ResolveLocalizeMode(component->mode, parentResolvedMode);
    component->resolvedModeInHierarchy = resolved;
    return resolved;
}

bool ShouldTranslateEntityText(const AutoTranslateComponent* const component) noexcept {
    if (component == nullptr) {
        return true;
    }
    const LocalizeMode effective =
        component->resolvedModeInHierarchy == LocalizeMode::Inherit
            ? ResolveLocalizeMode(component->mode, LocalizeMode::Localized)
            : component->resolvedModeInHierarchy;
    return effective == LocalizeMode::Localized;
}

AutoTranslateComponent SanitizeAutoTranslateComponent(
    const AutoTranslateComponent& component) noexcept {
    const LocalizeMode safeMode =
        IsLocalizeModeValid(component.mode) ? component.mode : LocalizeMode::Inherit;
    const LocalizeMode safeResolved =
        (IsLocalizeModeValid(component.resolvedModeInHierarchy) &&
         component.resolvedModeInHierarchy != LocalizeMode::Inherit)
            ? component.resolvedModeInHierarchy
            : ResolveLocalizeMode(safeMode, LocalizeMode::Localized);
    return AutoTranslateComponent{
        .mode = safeMode,
        .resolvedModeInHierarchy = safeResolved,
    };
}

} // namespace UVE::Scene
