// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "uve/uvscript/uvscript_compiler_uve.h"

namespace UVE::UVScript {

/// One node running a program: its field values and any handlers paused on `wait`.
/// Not thread-safe; a node's script runs on the thread that updates the node.
class ScriptInstanceUVE final {
public:
    /// Evaluates the field initializers in source order. `host` must outlive the instance.
    ScriptInstanceUVE(std::shared_ptr<const ProgramUVE> program, UVScriptHostUVE& host);
    ~ScriptInstanceUVE();
    ScriptInstanceUVE(const ScriptInstanceUVE&) = delete;
    ScriptInstanceUVE& operator=(const ScriptInstanceUVE&) = delete;

    /// Runs `on <event>` with `args` until it returns or reaches a `wait`. Returns false when the
    /// script has no such handler or it stopped with an error (see GetLastErrorUVE).
    bool RaiseEventUVE(std::string_view event, std::span<const ValueUVE> args = {});

    /// Moves time forward: every handler whose `wait` has run out continues, in the order it began
    /// waiting. `tick` handlers are not raised here - the host raises them itself.
    void AdvanceUVE(double seconds);

    /// Calls `fn name(args)` and returns its result, or nothing on an error or unknown function.
    std::optional<ValueUVE> CallUVE(std::string_view name, std::span<const ValueUVE> args = {});

    [[nodiscard]] std::optional<ValueUVE> GetFieldUVE(std::string_view name) const;
    /// Sets an `export` or `var` field (the Inspector's edits). False for a const, an unknown field or
    /// a value of the wrong type.
    bool SetFieldUVE(std::string_view name, const ValueUVE& value);

    [[nodiscard]] std::size_t GetWaitingCountUVE() const noexcept;
    /// The last run-time error ("line 12: division by zero"), empty when there has been none.
    [[nodiscard]] const std::string& GetLastErrorUVE() const noexcept;

private:
    struct StateUVE;
    std::unique_ptr<StateUVE> m_state;
};

} // namespace UVE::UVScript
