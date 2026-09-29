// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Retarget {

/// A bone key taken apart: its side and its words (see MakeBoneKeyUVE).
struct BoneKeyPartsUVE final {
    char side = 'C';
    std::vector<std::string> words;

    /// The key is exactly this one word ("hand", not "hand ik").
    [[nodiscard]] bool Is(const std::string_view word) const { return words.size() == 1U && words[0] == word; }
    [[nodiscard]] bool Has(const std::string_view word) const {
        return std::ranges::find(words, word) != words.end();
    }
    [[nodiscard]] bool IsFinger() const {
        return !words.empty() && (words[0] == "thumb" || words[0] == "index" || words[0] == "middle" ||
                                  words[0] == "ring" || words[0] == "pinky");
    }
    /// The first number in the key ("spine 3" -> 3), 0 when there is none.
    [[nodiscard]] int Number() const {
        for (const std::string& word : words) {
            if (!word.empty() && word.size() <= 6U && std::isdigit(static_cast<unsigned char>(word[0])) != 0) {
                return std::stoi(word);
            }
        }
        return 0;
    }
};

[[nodiscard]] inline BoneKeyPartsUVE SplitBoneKeyUVE(const std::string_view key) {
    BoneKeyPartsUVE parts;
    const auto bar = key.find('|');
    std::string_view words = key;
    if (bar != std::string_view::npos) {
        parts.side = bar > 0U ? key[0] : 'C';
        words = key.substr(bar + 1U);
    }
    std::size_t start = 0U;
    while (start < words.size()) {
        const std::size_t end = std::min(words.find(' ', start), words.size());
        if (end > start) {
            parts.words.emplace_back(words.substr(start, end - start));
        }
        start = end + 1U;
    }
    return parts;
}

} // namespace UVE::Retarget
