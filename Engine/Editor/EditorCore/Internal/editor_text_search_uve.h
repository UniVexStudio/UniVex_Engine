// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <cctype>
#include <string_view>

namespace UVE::Editor {

/// Case-insensitive substring test, used by every filter box in the editor - the hierarchy
/// filter, the content browser's search, the script-canvas node search.
///
/// Shared rather than copied because those filters must behave identically: a user who learns
/// that typing "cam" finds Camera in the outliner expects the same in the asset grid, and two
/// copies of this that drift apart would make one search subtly pickier than another with nothing
/// to say which was right.
///
/// An empty query matches everything. That is the behaviour every caller depends on - it is what
/// makes an empty filter box show the full list rather than nothing.
[[nodiscard]] inline bool ContainsCaseInsensitiveUVE(const std::string_view text,
                                                     const std::string_view query) noexcept {
    if (query.empty()) {
        return true;
    }

    const auto equalsCaseInsensitive = [](const char lhs, const char rhs) noexcept {
        return std::tolower(static_cast<unsigned char>(lhs)) == std::tolower(static_cast<unsigned char>(rhs));
    };
    return std::search(text.begin(), text.end(), query.begin(), query.end(), equalsCaseInsensitive) != text.end();
}

} // namespace UVE::Editor
