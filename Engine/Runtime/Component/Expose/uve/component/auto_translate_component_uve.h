// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

namespace UVE::Scene {

/// Whether an entity's authored text is looked up in the active locale's string table before it is
/// displayed.
///
/// WHY THIS IS A CHOICE AT ALL. Not every string in a scene is prose. Debug labels, placeholder
/// text, a player's own name, a number formatted into a label, an internal identifier shown in a
/// tool - translating those either does nothing or actively corrupts them. Translating everything
/// by default and adding exceptions gets this wrong in the direction that is hardest to notice: a
/// missing translation is visible, a wrongly-translated identifier is not.
enum class LocalizeModeUVE : std::uint8_t {
    /// Take the parent's answer, or Localized at the top of the hierarchy. The default, so a whole
    /// menu is switched by its root.
    Inherit = 0,
    /// Look the text up. Text with no entry in the active locale falls back, ultimately to the
    /// authored string, so an untranslated build still reads correctly.
    Localized,
    /// Display the authored text exactly as written.
    Literal,
};

/// An entity's translation preference.
struct AutoTranslateComponentUVE final {
    LocalizeModeUVE mode = LocalizeModeUVE::Inherit;

    /// Derived: the mode resolved against ancestors. Written only by SceneGraphUVE::UpdateUVE, and
    /// read by whatever draws the entity's text.
    LocalizeModeUVE resolvedModeInHierarchy = LocalizeModeUVE::Localized;
};

[[nodiscard]] bool IsAutoTranslateComponentValidUVE(const AutoTranslateComponentUVE& component) noexcept;

/// Resolves `mode` against the parent's already-resolved answer. Inherit passes through; anything
/// else replaces it, because a single label inside a translated menu must be able to opt out.
[[nodiscard]] LocalizeModeUVE ResolveLocalizeModeUVE(LocalizeModeUVE mode,
                                                               LocalizeModeUVE parentMode) noexcept;

} // namespace UVE::Scene
