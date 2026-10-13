// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/gameplay/gameplay_tags_uve.h"

#include <utility>
#include <vector>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsValidTagUVE(const std::string_view tag) noexcept {
    return !tag.empty() && tag.size() <= kMaximumGameplayTagBytesUVE;
}

} // namespace

bool IsGameplayTagComponentValidUVE(const GameplayTagComponentUVE& value) noexcept {
    if (value.tags.size() > kMaximumGameplayTagsUVE) {
        return false;
    }
    for (const std::string& tag : value.tags) {
        if (!IsValidTagUVE(tag)) {
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

bool HasGameplayTagUVE(const GameplayTagComponentUVE& tags, const std::string_view tag) noexcept {
    for (const std::string& held : tags.tags) {
        if (held == tag) {
            return true;
        }
    }
    return false;
}

bool HasAllGameplayTagsUVE(const GameplayTagComponentUVE& tags,
                            const std::initializer_list<std::string_view> required) noexcept {
    for (const std::string_view want : required) {
        if (!HasGameplayTagUVE(tags, want)) {
            return false;
        }
    }
    return true;
}

bool HasAnyGameplayTagUVE(const GameplayTagComponentUVE& tags,
                           const std::initializer_list<std::string_view> any) noexcept {
    for (const std::string_view want : any) {
        if (HasGameplayTagUVE(tags, want)) {
            return true;
        }
    }
    return false;
}

bool AddGameplayTagUVE(GameplayTagComponentUVE& tags, std::string tag) {
    if (!IsValidTagUVE(tag) || tags.tags.size() >= kMaximumGameplayTagsUVE ||
        HasGameplayTagUVE(tags, tag)) {
        return false;
    }
    tags.tags.push_back(std::move(tag));
    return true;
}

bool RemoveGameplayTagUVE(GameplayTagComponentUVE& tags, const std::string_view tag) {
    const auto removed = std::erase_if(tags.tags, [tag](const std::string& held) { return held == tag; });
    return removed > 0;
}

} // namespace UVE::Scene
