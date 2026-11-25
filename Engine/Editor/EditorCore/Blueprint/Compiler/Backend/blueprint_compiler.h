// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "blueprint_ast.h"
#include "blueprint_host.h"
#include "blueprint_value.h"

namespace UVE::Blueprint {

/// A compiled script: bytecode for every handler and function, and the field layout. Immutable and
/// shareable - every object running the same script uses one program.
struct Program;

/// A field as the Inspector sees it.
struct FieldInfo final {
    std::string name;
    Type type;
    FieldKind kind = FieldKind::Var;
};

struct CompileResult final {
    std::shared_ptr<const Program> program;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool IsSuccess() const noexcept { return program != nullptr && diagnostics.empty(); }
};

/// Type-checks `file` against what `host` offers and compiles it. Every value's type is known here:
/// names are resolved, operators checked, units converted (deg to radians, ms to seconds, cm and
/// km to metres). No program is produced when there is any diagnostic.
[[nodiscard]] CompileResult CompileUVScript(const File& file, const UVScriptHost& host);

/// Parses and compiles in one step; parse errors come first.
[[nodiscard]] CompileResult CompileUVScriptSource(std::string_view source, const UVScriptHost& host);

[[nodiscard]] std::vector<FieldInfo> GetProgramFields(const Program& program);

/// Identifies the compiled code exactly: the same source compiled against a host that describes
/// the same things gives the same fingerprint. Native code is looked up by it.
[[nodiscard]] std::uint64_t GetProgramFingerprint(const Program& program);

} // namespace UVE::Blueprint
