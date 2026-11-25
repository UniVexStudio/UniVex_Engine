// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "gameplay_tags.h"

#include <utility>
#include <vector>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsValidTag(const std::string_view tag) noexcept {
    return !tag.empty() && tag.size() <= kMaximumGameplayTagBytes;
}

} // namespace

bool IsGameplayTagComponentValid(const GameplayTagComponent& value) noexcept {
    if (value.tags.size() > kMaximumGameplayTags) {
        return false;
    }
    for (const std::string& tag : value.tags) {
        if (!IsValidTag(tag)) {
            return false;
        }
    }
    for (std::size_t first = 0U; first < value.tags.size(); ++first) {
        for (std::size_t second = first + 1U; second < value.tags.size(); ++second) {
            if (value.tags[first] == value.tags[second]) {
                return false;
            }
        }
    }
    return true;
}

bool HasGameplayTag(const GameplayTagComponent& tags, const std::string_view tag) noexcept {
    for (const std::string& held : tags.tags) {
        if (held == tag) {
            return true;
        }
    }
    return false;
}

bool HasAllGameplayTags(const GameplayTagComponent& tags,
                            const std::initializer_list<std::string_view> required) noexcept {
    for (const std::string_view want : required) {
        if (!HasGameplayTag(tags, want)) {
            return false;
        }
    }
    return true;
}

bool HasAnyGameplayTag(const GameplayTagComponent& tags,
                           const std::initializer_list<std::string_view> any) noexcept {
    for (const std::string_view want : any) {
        if (HasGameplayTag(tags, want)) {
            return true;
        }
    }
    return false;
}

bool AddGameplayTag(GameplayTagComponent& tags, std::string tag) {
    if (!IsValidTag(tag) || tags.tags.size() >= kMaximumGameplayTags ||
        HasGameplayTag(tags, tag)) {
        return false;
    }
    tags.tags.push_back(std::move(tag));
    return true;
}

bool RemoveGameplayTag(GameplayTagComponent& tags, const std::string_view tag) {
    const auto removed = std::erase_if(tags.tags, [tag](const std::string& held) { return held == tag; });
    return removed > 0;
}

} // namespace UVE::Scene
