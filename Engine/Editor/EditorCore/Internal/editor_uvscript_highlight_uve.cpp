// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_uvscript_highlight_uve.h"

#include <algorithm>
#include <array>

namespace UVE::Editor {
namespace {

[[nodiscard]] constexpr bool IsWordStartUVE(const char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
[[nodiscard]] constexpr bool IsDigitUVE(const char c) noexcept { return c >= '0' && c <= '9'; }
[[nodiscard]] constexpr bool IsWordUVE(const char c) noexcept { return IsWordStartUVE(c) || IsDigitUVE(c); }

template <std::size_t N>
[[nodiscard]] bool ContainsUVE(const std::array<std::string_view, N>& words, const std::string_view word) noexcept {
    return std::ranges::find(words, word) != words.end();
}

constexpr std::array<std::string_view, 7> kDeclarationsUVE{"entity", "on", "fn", "export", "var", "const", "let"};
constexpr std::array<std::string_view, 14> kKeywordsUVE{"if",   "elif", "else",     "while", "for", "in",  "return",
                                                         "break", "pass", "continue", "wait",  "and", "or", "not"};
constexpr std::array<std::string_view, 3> kLiteralsUVE{"true", "false", "none"};
constexpr std::array<std::string_view, 7> kTypesUVE{"int", "float", "bool", "str", "vec3", "list", "map"};

[[nodiscard]] UVScriptTokenKindUVE ClassifyWordUVE(const std::string_view word) noexcept {
    if (ContainsUVE(kDeclarationsUVE, word)) {
        return UVScriptTokenKindUVE::Declaration;
    }
    if (ContainsUVE(kKeywordsUVE, word)) {
        return UVScriptTokenKindUVE::Keyword;
    }
    if (ContainsUVE(kLiteralsUVE, word)) {
        return UVScriptTokenKindUVE::Literal;
    }
    if (ContainsUVE(kTypesUVE, word) || (word.front() >= 'A' && word.front() <= 'Z')) {
        return UVScriptTokenKindUVE::Type;
    }
    return UVScriptTokenKindUVE::Plain;
}

} // namespace

std::vector<UVScriptTokenSpanUVE> HighlightUVScriptLineUVE(const std::string_view line) {
    std::vector<UVScriptTokenSpanUVE> spans;
    bool nextIsDefinition = false;
    std::size_t at = 0U;
    while (at < line.size()) {
        const char c = line[at];
        const std::size_t start = at;
        if (c == '#') {
            spans.push_back({start, line.size() - start, UVScriptTokenKindUVE::Comment});
            break;
        }
        if (c == '"') {
            ++at;
            while (at < line.size() && line[at] != '"') {
                at += line[at] == '\\' && at + 1U < line.size() ? 2U : 1U;
            }
            at = std::min(line.size(), at + 1U);
            spans.push_back({start, at - start, UVScriptTokenKindUVE::String});
            continue;
        }
        if (IsDigitUVE(c)) {
            // Digits, one decimal point, then a unit suffix (deg, ms...) glued on.
            while (at < line.size() && (IsDigitUVE(line[at]) || line[at] == '.' || line[at] == '_')) {
                ++at;
            }
            while (at < line.size() && IsWordStartUVE(line[at])) {
                ++at;
            }
            spans.push_back({start, at - start, UVScriptTokenKindUVE::Number});
            continue;
        }
        if (IsWordStartUVE(c)) {
            while (at < line.size() && IsWordUVE(line[at])) {
                ++at;
            }
            const std::string_view word = line.substr(start, at - start);
            UVScriptTokenKindUVE kind = nextIsDefinition ? UVScriptTokenKindUVE::Definition : ClassifyWordUVE(word);
            nextIsDefinition = word == "on" || word == "fn";
            if (kind != UVScriptTokenKindUVE::Plain) {
                spans.push_back({start, at - start, kind});
            }
            continue;
        }
        ++at;
    }
    return spans;
}

std::string ComputeUVScriptNewLineIndentUVE(const std::string_view previousLine) {
    const std::size_t indentEnd = previousLine.find_first_not_of(" \t");
    std::string indent{previousLine.substr(0U, indentEnd == std::string_view::npos ? previousLine.size() : indentEnd)};
    // A trailing comment does not hide the ':' before it.
    std::string_view code = previousLine;
    bool inString = false;
    for (std::size_t index = 0U; index < previousLine.size(); ++index) {
        if (previousLine[index] == '"') {
            inString = !inString;
        } else if (previousLine[index] == '#' && !inString) {
            code = previousLine.substr(0U, index);
            break;
        }
    }
    const std::size_t last = code.find_last_not_of(" \t\r");
    if (last != std::string_view::npos && code[last] == ':') {
        indent += "    ";
    }
    return indent;
}

} // namespace UVE::Editor
