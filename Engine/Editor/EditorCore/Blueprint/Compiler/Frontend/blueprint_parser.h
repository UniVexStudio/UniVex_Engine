// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>
#include <vector>

#include "blueprint_ast.h"

namespace UVE::Blueprint {

struct ParseResult final {
    File file;
    /// Every problem found, in source order. Parsing goes on at the next line after an error, so
    /// one mistake does not hide the ones after it.
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool IsSuccess() const noexcept { return diagnostics.empty(); }
};

/// Parses Blueprint notation text. The tree holds everything that parsed, even when there are
/// diagnostics. Tabs count as four spaces. Line breaks inside (), [] are ignored.
[[nodiscard]] ParseResult ParseBlueprintNotation(std::string_view source);

/// The unit suffixes a number may carry: s, ms, m, cm, km, deg, rad.
[[nodiscard]] bool IsBlueprintNotationUnit(std::string_view word) noexcept;

} // namespace UVE::Blueprint
