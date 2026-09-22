// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/auto_translate_component_uve.h"

namespace UVE::Scene {

bool IsAutoTranslateComponentValidUVE(const AutoTranslateComponentUVE&) noexcept {
    return true;
}

AutoTranslateModeUVE ResolveAutoTranslateModeUVE(const AutoTranslateModeUVE mode,
                                                 const AutoTranslateModeUVE parentMode) noexcept {
    if (mode == AutoTranslateModeUVE::Inherit) {
        return parentMode == AutoTranslateModeUVE::Inherit ? AutoTranslateModeUVE::Always : parentMode;
    }
    return mode;
}

} // namespace UVE::Scene
