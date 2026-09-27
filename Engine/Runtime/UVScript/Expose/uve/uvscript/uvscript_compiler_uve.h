// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "uve/uvscript/uvscript_ast_uve.h"
#include "uve/uvscript/uvscript_host_uve.h"
#include "uve/uvscript/uvscript_value_uve.h"

namespace UVE::UVScript {

/// A compiled script: bytecode for every handler and function, and the field layout. Immutable and
/// shareable - every node running the same script uses one program.
struct ProgramUVE;

/// A field as the Inspector sees it.
struct FieldInfoUVE final {
    std::string name;
    TypeUVE type;
    FieldKindUVE kind = FieldKindUVE::Var;
};

struct CompileResultUVE final {
    std::shared_ptr<const ProgramUVE> program;
    std::vector<DiagnosticUVE> diagnostics;

    [[nodiscard]] bool IsSuccessUVE() const noexcept { return program != nullptr && diagnostics.empty(); }
};

/// Type-checks `file` against what `host` offers and compiles it. Every value's type is known here:
/// names are resolved, operators checked, units converted (deg to radians, ms to seconds, cm and
/// km to metres). No program is produced when there is any diagnostic.
[[nodiscard]] CompileResultUVE CompileUVScriptUVE(const FileUVE& file, const UVScriptHostUVE& host);

/// Parses and compiles in one step; parse errors come first.
[[nodiscard]] CompileResultUVE CompileUVScriptSourceUVE(std::string_view source, const UVScriptHostUVE& host);

[[nodiscard]] std::vector<FieldInfoUVE> GetProgramFieldsUVE(const ProgramUVE& program);

/// Identifies the compiled code exactly: the same source compiled against a host that describes
/// the same things gives the same fingerprint. Native code is looked up by it.
[[nodiscard]] std::uint64_t GetProgramFingerprintUVE(const ProgramUVE& program);

} // namespace UVE::UVScript
