// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Strings {

/// Small string vocabulary over `string_view`: trim/split/join/case. All ASCII-only by design —
/// these are for identifiers, commands, paths and config keys, where Unicode case-folding would
/// be wrong anyway (see the localization service for human text). Views never allocate; the
/// owning functions (`JoinUVE`, `ToLowerUVE`, `ToUpperUVE`) return `std::string`.

/// True for the ASCII whitespace `TrimUVE` strips (space, tab, CR, LF, vertical tab, form feed).
[[nodiscard]] inline bool IsAsciiWhitespaceUVE(const char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

/// `text` with leading and trailing ASCII whitespace removed. A view into the input.
[[nodiscard]] inline std::string_view TrimUVE(const std::string_view text) noexcept {
    std::size_t begin = 0U;
    while (begin < text.size() && IsAsciiWhitespaceUVE(text[begin])) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && IsAsciiWhitespaceUVE(text[end - 1U])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

/// Splits `text` on `delimiter`, keeping empty parts (consecutive delimiters yield empty views
/// between them; a trailing delimiter yields a trailing empty view). Keeping empties is the
/// no-data-loss rule: `"a,,b"` is three fields, and the caller filters when it wants words.
/// Views into the input; never empty (no delimiter means one part: the whole text).
[[nodiscard]] inline std::vector<std::string_view> SplitUVE(const std::string_view text,
                                                            const char delimiter) {
    std::vector<std::string_view> parts;
    std::size_t begin = 0U;
    for (std::size_t i = 0U; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == delimiter) {
            parts.push_back(text.substr(begin, i - begin));
            begin = i + 1U;
        }
    }
    return parts;
}

/// Joins `parts` with `delimiter` between (none after the last). The span follows the
/// `std::span<const T>` read-path convention — any contiguous view of views qualifies.
[[nodiscard]] inline std::string JoinUVE(const std::span<const std::string_view> parts,
                                         const std::string_view delimiter) {
    std::string joined;
    bool first = true;
    for (const std::string_view part : parts) {
        if (!first) {
            joined.append(delimiter);
        }
        joined.append(part);
        first = false;
    }
    return joined;
}

/// ASCII-lowercased copy (`A`-`Z` only; every other byte passes through untouched).
[[nodiscard]] inline std::string ToLowerUVE(const std::string_view text) {
    std::string lowered{text};
    for (char& c : lowered) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return lowered;
}

/// ASCII-uppercased copy (`a`-`z` only; every other byte passes through untouched).
[[nodiscard]] inline std::string ToUpperUVE(const std::string_view text) {
    std::string uppered{text};
    for (char& c : uppered) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return uppered;
}

} // namespace UVE::Strings
