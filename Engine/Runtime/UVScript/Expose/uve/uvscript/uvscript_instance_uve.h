// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "uve/uvscript/uvscript_compiler_uve.h"

namespace UVE::UVScript {

/// How an instance runs its program.
enum class ExecutionUVE : std::uint8_t {
    /// Native when C++ generated from this exact program is linked in, interpreted otherwise.
    Auto,
    /// Always the bytecode interpreter (tests compare the two).
    Interpreted,
};

enum class ScriptDebugStateUVE : std::uint8_t { NotPaused, Paused, Completed, Waiting, Failed };

/// A copied view of the next interpreted instruction. Local values are identified by slot
/// because bytecode does not retain local source names; field values carry their field names.
struct ScriptDebugSnapshotUVE final {
    std::uint32_t line = 0U;
    std::string functionName;
    std::size_t instructionIndex = 0U;
    std::size_t callDepth = 0U;
    std::vector<ValueUVE> locals;
    std::vector<std::pair<std::string, ValueUVE>> fields;
};

struct ScriptDebugResumeResultUVE final {
    ScriptDebugStateUVE state = ScriptDebugStateUVE::NotPaused;
    std::optional<ValueUVE> value;
    [[nodiscard]] bool IsPausedUVE() const noexcept { return state == ScriptDebugStateUVE::Paused; }
};

/// One object running a program: its field values and any handlers paused on `wait`.
/// Not thread-safe; an object's script runs on the thread that updates the object.
class ScriptInstanceUVE final {
public:
    /// Evaluates the field initializers in source order. `host` must outlive the instance.
    ScriptInstanceUVE(std::shared_ptr<const ProgramUVE> program, UVScriptHostUVE& host,
                      ExecutionUVE execution = ExecutionUVE::Auto);
    ~ScriptInstanceUVE();
    ScriptInstanceUVE(const ScriptInstanceUVE&) = delete;
    ScriptInstanceUVE& operator=(const ScriptInstanceUVE&) = delete;

    /// Runs `on <event>` with `args` until it returns or reaches a `wait`. Returns false when the
    /// script has no such handler or it stopped with an error (see GetLastErrorUVE).
    /// A debugger pause is a successful dispatch; inspect IsDebugPausedUVE to distinguish it.
    bool RaiseEventUVE(std::string_view event, std::span<const ValueUVE> args = {});

    /// Moves time forward: every handler whose `wait` has run out continues, in the order it began
    /// waiting. `tick` handlers are not raised here - the host raises them itself.
    void AdvanceUVE(double seconds);

    /// Calls `fn name(args)` and returns its result, or nothing on an error, unknown function
    /// or debugger pause. After a pause, Continue/Step returns the final value on completion.
    std::optional<ValueUVE> CallUVE(std::string_view name, std::span<const ValueUVE> args = {});

    /// Whether `fn name` takes `argc` values (what CallUVE would run). The engine asks before
    /// routing a cross-node call at a script, so a missing method falls through to built-ins.
    [[nodiscard]] bool HasFunctionUVE(std::string_view name, std::size_t argc) const noexcept;

    [[nodiscard]] std::optional<ValueUVE> GetFieldUVE(std::string_view name) const;
    /// Sets an `export` or `var` field (the Inspector's edits). False for a const, an unknown field or
    /// a value of the wrong type.
    bool SetFieldUVE(std::string_view name, const ValueUVE& value);

    /// True when this instance runs generated C++ rather than the interpreter.
    [[nodiscard]] bool IsNativeUVE() const noexcept;
    [[nodiscard]] std::size_t GetWaitingCountUVE() const noexcept;
    /// The last run-time error ("line 12: division by zero"), empty when there has been none.
    [[nodiscard]] const std::string& GetLastErrorUVE() const noexcept;

    /// Interpreted-execution debugger. Enabling fails for a linked native program rather than
    /// pretending breakpoints apply there; construct with ExecutionUVE::Interpreted to debug.
    /// Breakpoint lines are source lines, duplicates/zero are ignored, and a pause occurs
    /// before the first not-yet-executed instruction on a selected line. While paused, ordinary
    /// Call/Raise/Advance calls are rejected by their existing fail-closed result shapes so the
    /// paused coroutine cannot be re-entered accidentally. With debugging enabled, host callbacks
    /// also cannot re-enter the same instance while it executes. Other instances remain independent.
    /// Steps follow source-line transitions (not individual instructions); StepOver skips deeper
    /// frames unless they hit an explicit breakpoint. A wait returns Waiting and rejoins Advance's
    /// queue; stepping ends at that yield. There is no cross-instance stepping or editor UI here.
    [[nodiscard]] bool EnableDebuggerUVE();
    /// Abandons any paused coroutine (its side effects so far remain) and clears breakpoints.
    void DisableDebuggerUVE() noexcept;
    [[nodiscard]] bool SetDebuggerBreakpointsUVE(std::span<const std::uint32_t> lines);
    void ClearDebuggerBreakpointsUVE() noexcept;
    [[nodiscard]] bool IsDebuggerEnabledUVE() const noexcept;
    [[nodiscard]] bool IsDebugPausedUVE() const noexcept;
    [[nodiscard]] std::optional<ScriptDebugSnapshotUVE> GetDebugSnapshotUVE() const;
    [[nodiscard]] ScriptDebugResumeResultUVE ContinueDebugUVE();
    [[nodiscard]] ScriptDebugResumeResultUVE StepIntoDebugUVE();
    [[nodiscard]] ScriptDebugResumeResultUVE StepOverDebugUVE();

private:
    struct StateUVE;
    std::unique_ptr<StateUVE> m_state;
};

} // namespace UVE::UVScript
