// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/config/setting_descriptor_uve.h"

namespace UVE::Editor {

class EditorUVE;

/// One editor setting: its description, and how to read it from and apply it to a live editor.
/// Loading, saving and the preferences window all go through these, so a setting is wired up in
/// exactly one place (editor_settings_uve.cpp).
struct EditorSettingBindingUVE final {
    Config::SettingDescriptorUVE descriptor;
    /// The value the editor is using now.
    Config::SettingValueUVE (*get)(const EditorUVE& editor) = nullptr;
    /// Applies `value`, which is already legal for `descriptor`. False if the editor refused it.
    bool (*set)(EditorUVE& editor, const Config::SettingValueUVE& value) = nullptr;
};

} // namespace UVE::Editor
