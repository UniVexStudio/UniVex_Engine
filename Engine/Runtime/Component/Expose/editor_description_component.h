// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace UVE::Scene {

inline constexpr std::size_t kMaximumEditorDescriptionBytes = 4096U;

/// Free-text author notes attached to a scene entity: why this object exists, what a magic number
/// on it means, which bug it works around.
///
/// WHY IT IS A COMPONENT RATHER THAN A FIELD ON EVERY NODE. Most entities never carry one, and a
/// string on every transform would cost every scene memory for a feature a handful of objects use.
/// As an optional component it is paid for only where it is written.
///
/// Editor-facing and deliberately inert: nothing at runtime reads it, and nothing should start.
/// The moment a system branches on a description, authors lose the ability to write freely in it -
/// which is the only thing it is for.
///
/// Bounded at 4 KB. Not a technical limit but an honest one: a description longer than that is
/// documentation that belongs in the project's own files, where it can be searched and reviewed,
/// rather than buried in a scene object's metadata.
struct EditorDescriptionComponent final {
    std::string description;
};

/// True when `text` is within `kMaximumEditorDescriptionBytes` and contains no embedded NULs.
[[nodiscard]] bool IsEditorDescriptionTextValid(std::string_view text) noexcept;

[[nodiscard]] bool IsEditorDescriptionComponentValid(
    const EditorDescriptionComponent& component) noexcept;

/// True when `component.description` contains at least one non-whitespace character.
[[nodiscard]] bool HasNonEmptyEditorDescription(
    const EditorDescriptionComponent& component) noexcept;

/// Counts the number of lines in `component.description` (`0` when empty).
[[nodiscard]] std::size_t CountEditorDescriptionLines(
    const EditorDescriptionComponent& component) noexcept;

/// Strips embedded NUL bytes and truncates to `kMaximumEditorDescriptionBytes`.
[[nodiscard]] std::string SanitizeEditorDescriptionText(std::string_view text);

/// Returns a sanitized copy of `component` guaranteed to pass `IsEditorDescriptionComponentValid`.
[[nodiscard]] EditorDescriptionComponent SanitizeEditorDescriptionComponent(
    const EditorDescriptionComponent& component);

/// Builds a single-line summary preview of the first non-empty line, bounded to `maxPreviewBytes`.
[[nodiscard]] std::string BuildEditorDescriptionPreview(
    const EditorDescriptionComponent& component, std::size_t maxPreviewBytes = 80U);

} // namespace UVE::Scene
