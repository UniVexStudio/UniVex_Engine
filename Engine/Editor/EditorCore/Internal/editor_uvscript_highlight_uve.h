// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Colouring for UVScript text in the script editor. GL-free, so the rules are unit-tested: the
// editor draws each span in the colour of its kind over the text box.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Editor {

enum class UVScriptTokenKindUVE : std::uint8_t {
    Plain,
    /// Control and operators: if, for, return, wait, and, not...
    Keyword,
    /// Words that declare something: entity, on, fn, export, var, const, let.
    Declaration,
    /// true, false, none.
    Literal,
    /// A type or node kind: int, vec3, CharacterBody3D (any capitalised word).
    Type,
    /// The name being declared right after `on` or `fn`.
    Definition,
    /// 12, 0.5, 90deg, 200ms.
    Number,
    String,
    Comment,
};

struct UVScriptTokenSpanUVE final {
    /// Byte offset into the line, and length.
    std::size_t start = 0U;
    std::size_t length = 0U;
    UVScriptTokenKindUVE kind = UVScriptTokenKindUVE::Plain;

    bool operator==(const UVScriptTokenSpanUVE&) const = default;
};

/// The coloured spans of one line (no newline), left to right. Plain text between them is not
/// listed. A string left open runs to the end of the line.
[[nodiscard]] std::vector<UVScriptTokenSpanUVE> HighlightUVScriptLineUVE(std::string_view line);

/// What to insert after Enter at the end of `previousLine`: its indentation, plus one level
/// (four spaces) when it opens a block with ':'.
[[nodiscard]] std::string ComputeUVScriptNewLineIndentUVE(std::string_view previousLine);

} // namespace UVE::Editor
