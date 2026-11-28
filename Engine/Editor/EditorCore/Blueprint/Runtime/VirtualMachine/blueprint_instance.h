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

#include "blueprint_compiler.h"

namespace UVE::Blueprint {

/// How an instance runs its program.
enum class Execution : std::uint8_t {
    /// Native when C++ generated from this exact program is linked in, interpreted otherwise.
    Auto,
    /// Always the bytecode interpreter (tests compare the two).
    Interpreted,
};

enum class ScriptDebugState : std::uint8_t { NotPaused, Paused, Completed, Waiting, Failed };

/// A copied view of the next interpreted instruction. Local values are identified by slot
/// because bytecode does not retain local source names; field values carry their field names.
struct ScriptDebugSnapshot final {
    std::uint32_t line = 0U;
    std::string functionName;
    std::size_t instructionIndex = 0U;
    std::size_t callDepth = 0U;
    std::vector<Value> locals;
    std::vector<std::pair<std::string, Value>> fields;
};

struct ScriptDebugResumeResult final {
    ScriptDebugState state = ScriptDebugState::NotPaused;
    std::optional<Value> value;
    [[nodiscard]] bool IsPaused() const noexcept { return state == ScriptDebugState::Paused; }
};

/// One object running a program: its field values and any handlers paused on `wait`.
/// Not thread-safe; an object's script runs on the thread that updates the object.
class BlueprintInstance final {
public:
    /// Evaluates the field initializers in source order. `host` must outlive the instance.
    BlueprintInstance(std::shared_ptr<const Program> program, BlueprintHost& host,
                      Execution execution = Execution::Auto);
    ~BlueprintInstance();
    BlueprintInstance(const BlueprintInstance&) = delete;
    BlueprintInstance& operator=(const BlueprintInstance&) = delete;

    /// Runs `on <event>` with `args` until it returns or reaches a `wait`. Returns false when the
    /// script has no such handler or it stopped with an error (see GetLastError).
    /// A debugger pause is a successful dispatch; inspect IsDebugPaused to distinguish it.
    bool RaiseEvent(std::string_view event, std::span<const Value> args = {});

    /// Moves time forward: every handler whose `wait` has run out continues, in the order it began
    /// waiting. `tick` handlers are not raised here - the host raises them itself.
    void Advance(double seconds);

    /// Calls `fn name(args)` and returns its result, or nothing on an error, unknown function
    /// or debugger pause. After a pause, Continue/Step returns the final value on completion.
    std::optional<Value> Call(std::string_view name, std::span<const Value> args = {});

    /// Whether `fn name` takes `argc` values (what Call would run). The engine asks before
    /// routing a cross-node call at a script, so a missing method falls through to built-ins.
    [[nodiscard]] bool HasFunction(std::string_view name, std::size_t argc) const noexcept;

    [[nodiscard]] std::optional<Value> GetField(std::string_view name) const;
    /// Sets an `export` or `var` field (the Inspector's edits). False for a const, an unknown field or
    /// a value of the wrong type.
    bool SetField(std::string_view name, const Value& value);

    /// True when this instance runs generated C++ rather than the interpreter.
    [[nodiscard]] bool IsNative() const noexcept;
    [[nodiscard]] std::size_t GetWaitingCount() const noexcept;
    /// The last run-time error ("line 12: division by zero"), empty when there has been none.
    [[nodiscard]] const std::string& GetLastError() const noexcept;

    /// Interpreted-execution debugger. Enabling fails for a linked native program rather than
    /// pretending breakpoints apply there; construct with Execution::Interpreted to debug.
    /// Breakpoint lines are source lines, duplicates/zero are ignored, and a pause occurs
    /// before the first not-yet-executed instruction on a selected line. While paused, ordinary
    /// Call/Raise/Advance calls are rejected by their existing fail-closed result shapes so the
    /// paused coroutine cannot be re-entered accidentally. With debugging enabled, host callbacks
    /// also cannot re-enter the same instance while it executes. Other instances remain independent.
    /// Steps follow source-line transitions (not individual instructions); StepOver skips deeper
    /// frames unless they hit an explicit breakpoint. A wait returns Waiting and rejoins Advance's
    /// queue; stepping ends at that yield. There is no cross-instance stepping or editor UI here.
    [[nodiscard]] bool EnableDebugger();
    /// Abandons any paused coroutine (its side effects so far remain) and clears breakpoints.
    void DisableDebugger() noexcept;
    [[nodiscard]] bool SetDebuggerBreakpoints(std::span<const std::uint32_t> lines);
    void ClearDebuggerBreakpoints() noexcept;
    [[nodiscard]] bool IsDebuggerEnabled() const noexcept;
    [[nodiscard]] bool IsDebugPaused() const noexcept;
    [[nodiscard]] std::optional<ScriptDebugSnapshot> GetDebugSnapshot() const;
    [[nodiscard]] ScriptDebugResumeResult ContinueDebug();
    [[nodiscard]] ScriptDebugResumeResult StepIntoDebug();
    [[nodiscard]] ScriptDebugResumeResult StepOverDebug();

private:
    struct State;
    std::unique_ptr<State> m_state;
};

} // namespace UVE::Blueprint
