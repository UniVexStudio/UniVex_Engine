// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>
#include <vector>

#include "uve/uvscript/uvscript_ast_uve.h"

namespace UVE::UVScript {

struct ParseResultUVE final {
    FileUVE file;
    /// Every problem found, in source order. Parsing goes on at the next line after an error, so
    /// one mistake does not hide the ones after it.
    std::vector<DiagnosticUVE> diagnostics;

    [[nodiscard]] bool IsSuccessUVE() const noexcept { return diagnostics.empty(); }
};

/// Parses `.uvs` source text. The tree holds everything that parsed, even when there are
/// diagnostics. Tabs count as four spaces. Line breaks inside (), [] are ignored.
[[nodiscard]] ParseResultUVE ParseUVScriptUVE(std::string_view source);

/// The unit suffixes a number may carry: s, ms, m, cm, km, deg, rad.
[[nodiscard]] bool IsUVScriptUnitUVE(std::string_view word) noexcept;

} // namespace UVE::UVScript
