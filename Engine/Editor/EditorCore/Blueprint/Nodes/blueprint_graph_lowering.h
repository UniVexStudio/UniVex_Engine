// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "blueprint_ast.h"
#include "blueprint_compiler.h"
#include "blueprint_nodes.h"

namespace UVE::Asset {

/// A graph lowered straight to the syntax tree the compiler takes: no text in between. Every statement and
/// expression that came from a node carries that node's id as its `at.line` (and column 0); the header, the
/// fields and everything made for the graph as a whole carry 0. Diagnostics and breakpoints therefore name nodes.
struct BlueprintLowering final {
    Blueprint::File file;
    /// (node id, what is wrong): an unknown node, exec wires that lead back into themselves, an event wired into.
    std::vector<std::pair<std::uint32_t, std::string>> problems;
};

[[nodiscard]] BlueprintLowering LowerBlueprintGraph(const BlueprintAsset& blueprint, std::string_view objectName,
                                                    std::string_view objectKind);

/// The graph compiled against what `host` offers.
struct BlueprintCompilation final {
    Blueprint::CompileResult result;
    std::vector<std::pair<std::uint32_t, std::string>> problems;

    [[nodiscard]] bool IsSuccess() const noexcept { return result.IsSuccess() && problems.empty(); }
};

[[nodiscard]] BlueprintCompilation CompileBlueprintGraph(const BlueprintAsset& blueprint, std::string_view objectName,
                                                         std::string_view objectKind,
                                                         const Blueprint::BlueprintHost& host);

} // namespace UVE::Asset
