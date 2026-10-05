// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/auto_translate_component_uve.h"

namespace UVE::Scene {

bool IsAutoTranslateComponentValidUVE(const AutoTranslateComponentUVE&) noexcept {
    return true;
}

LocalizeModeUVE ResolveLocalizeModeUVE(const LocalizeModeUVE mode,
                                                 const LocalizeModeUVE parentMode) noexcept {
    if (mode == LocalizeModeUVE::Inherit) {
        return parentMode == LocalizeModeUVE::Inherit ? LocalizeModeUVE::Localized : parentMode;
    }
    return mode;
}

} // namespace UVE::Scene
