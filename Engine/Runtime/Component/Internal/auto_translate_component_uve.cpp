// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/auto_translate_component_uve.h"

namespace UVE::Scene {

bool IsLocalizeModeValidUVE(const LocalizeModeUVE mode) noexcept {
    switch (mode) {
        case LocalizeModeUVE::Inherit:
        case LocalizeModeUVE::Localized:
        case LocalizeModeUVE::Literal:
            return true;
    }
    return false;
}

bool IsAutoTranslateComponentValidUVE(const AutoTranslateComponentUVE& component) noexcept {
    return IsLocalizeModeValidUVE(component.mode) &&
           IsLocalizeModeValidUVE(component.resolvedModeInHierarchy);
}

LocalizeModeUVE ResolveLocalizeModeUVE(const LocalizeModeUVE mode,
                                       const LocalizeModeUVE parentMode) noexcept {
    const LocalizeModeUVE safeParent =
        (!IsLocalizeModeValidUVE(parentMode) || parentMode == LocalizeModeUVE::Inherit)
            ? LocalizeModeUVE::Localized
            : parentMode;
    if (!IsLocalizeModeValidUVE(mode) || mode == LocalizeModeUVE::Inherit) {
        return safeParent;
    }
    return mode;
}

LocalizeModeUVE ResolveHierarchyLocalizeModeUVE(
    AutoTranslateComponentUVE* const component,
    const LocalizeModeUVE parentResolvedMode) noexcept {
    if (component == nullptr) {
        return ResolveLocalizeModeUVE(LocalizeModeUVE::Inherit, parentResolvedMode);
    }
    const LocalizeModeUVE resolved = ResolveLocalizeModeUVE(component->mode, parentResolvedMode);
    component->resolvedModeInHierarchy = resolved;
    return resolved;
}

bool ShouldTranslateEntityTextUVE(const AutoTranslateComponentUVE* const component) noexcept {
    if (component == nullptr) {
        return true;
    }
    const LocalizeModeUVE effective =
        component->resolvedModeInHierarchy == LocalizeModeUVE::Inherit
            ? ResolveLocalizeModeUVE(component->mode, LocalizeModeUVE::Localized)
            : component->resolvedModeInHierarchy;
    return effective == LocalizeModeUVE::Localized;
}

AutoTranslateComponentUVE SanitizeAutoTranslateComponentUVE(
    const AutoTranslateComponentUVE& component) noexcept {
    const LocalizeModeUVE safeMode =
        IsLocalizeModeValidUVE(component.mode) ? component.mode : LocalizeModeUVE::Inherit;
    const LocalizeModeUVE safeResolved =
        (IsLocalizeModeValidUVE(component.resolvedModeInHierarchy) &&
         component.resolvedModeInHierarchy != LocalizeModeUVE::Inherit)
            ? component.resolvedModeInHierarchy
            : ResolveLocalizeModeUVE(safeMode, LocalizeModeUVE::Localized);
    return AutoTranslateComponentUVE{
        .mode = safeMode,
        .resolvedModeInHierarchy = safeResolved,
    };
}

} // namespace UVE::Scene
