// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace UVE::Scene {

inline constexpr std::size_t kMaximumEntityNameBytesUVE = 128U;

/// Persistent, human-readable authored metadata for a scene entity. Names are intentionally not
/// required to be globally unique: the editor gives newly created entities deterministic defaults,
/// while callers remain free to assign duplicate names when that better describes a scene. The
/// component is optional so legacy documents and runtime-created entities remain valid without it.
/// Thread-safety: value type; safe to copy and move freely with no shared state.
struct NameComponentUVE final {
    std::string name;
};

/// True when `name` is at most `kMaximumEntityNameBytesUVE` bytes and contains no embedded NULs.
[[nodiscard]] bool IsEntityNameStringValidUVE(std::string_view name) noexcept;

[[nodiscard]] bool IsNameComponentValidUVE(const NameComponentUVE& component) noexcept;

/// True when `component` is non-null and holds a non-empty, valid entity name.
[[nodiscard]] bool HasNonEmptyEntityNameUVE(const NameComponentUVE* component) noexcept;

/// Strips embedded NUL bytes, truncates to `kMaximumEntityNameBytesUVE`, and applies `fallbackName` when empty.
[[nodiscard]] std::string SanitizeEntityNameStringUVE(
    std::string_view rawName, std::string_view fallbackName = {});

/// Returns a sanitized `NameComponentUVE` guaranteed to pass `IsNameComponentValidUVE`.
[[nodiscard]] NameComponentUVE SanitizeNameComponentUVE(
    const NameComponentUVE& component, std::string_view fallbackName = {});

/// Returns `component->name` when present and non-empty, or `fallbackName` otherwise.
[[nodiscard]] std::string_view ResolveEntityDisplayNameOrFallbackUVE(
    const NameComponentUVE* component, std::string_view fallbackName) noexcept;

} // namespace UVE::Scene
