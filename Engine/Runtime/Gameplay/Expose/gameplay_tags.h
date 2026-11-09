// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Scene {

// ---- Gameplay tags -----------------------------------------------------------------
//
// A small string set per entity - "undead", "boss", "flammable" - for everything that gates on
// kind rather than numbers: immunities, team checks, interaction filters, spawner picks. Queries
// are pure and linear; sixteen short strings never made a profiler blink.

inline constexpr std::size_t kMaximumGameplayTagsUVE = 16U;
inline constexpr std::size_t kMaximumGameplayTagBytesUVE = 48U;

struct GameplayTagComponentUVE final {
    std::vector<std::string> tags;

    [[nodiscard]] bool operator==(const GameplayTagComponentUVE&) const = default;
};

/// Bounded count with non-empty, bounded, unique tags.
[[nodiscard]] bool IsGameplayTagComponentValidUVE(const GameplayTagComponentUVE& value) noexcept;

[[nodiscard]] bool HasGameplayTagUVE(const GameplayTagComponentUVE& tags, std::string_view tag) noexcept;

/// True when every `required` tag is present; vacuously true for an empty list.
[[nodiscard]] bool HasAllGameplayTagsUVE(const GameplayTagComponentUVE& tags,
                                         std::initializer_list<std::string_view> required) noexcept;

/// True when at least one of `any` is present; false for an empty list.
[[nodiscard]] bool HasAnyGameplayTagUVE(const GameplayTagComponentUVE& tags,
                                        std::initializer_list<std::string_view> any) noexcept;

/// Adds `tag`. False for a bad tag (empty, overlong, duplicate) or a full set.
[[nodiscard]] bool AddGameplayTagUVE(GameplayTagComponentUVE& tags, std::string tag);

/// Removes `tag`. False when it was never there.
[[nodiscard]] bool RemoveGameplayTagUVE(GameplayTagComponentUVE& tags, std::string_view tag);

} // namespace UVE::Scene
