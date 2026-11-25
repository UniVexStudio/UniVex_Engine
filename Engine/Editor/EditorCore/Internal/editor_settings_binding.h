// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "setting_descriptor.h"

namespace UVE::Editor {

class Editor;

/// One editor setting: its description, and how to read it from and apply it to a live editor.
/// Loading, saving and the preferences window all go through these, so a setting is wired up in
/// exactly one place (editor_settings_uve.cpp).
struct EditorSettingBinding final {
    Config::SettingDescriptor descriptor;
    /// The value the editor is using now.
    Config::SettingValue (*get)(const Editor& editor) = nullptr;
    /// Applies `value`, which is already legal for `descriptor`. False if the editor refused it.
    bool (*set)(Editor& editor, const Config::SettingValue& value) = nullptr;
};

} // namespace UVE::Editor
