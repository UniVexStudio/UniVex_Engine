#pragma once

namespace UVE::Editor {

struct EditorExtendedPreferencesUVE;

// Applies the UVE editor's neutral charcoal visual system to the active ImGui context.
// When preferences are supplied, custom styling (colors, contrast, density, rounding)
// is applied dynamically. Default nullptr maintains the standard neutral charcoal theme.
void ApplyEditorVisualThemeUVE(const EditorExtendedPreferencesUVE* preferences = nullptr) noexcept;

} // namespace UVE::Editor
