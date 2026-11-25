// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_instance.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "blueprint_compiler.h"
#include "blueprint_host.h"

namespace UVE::Blueprint::Tests {
namespace {

class DebuggerHost final : public UVScriptHost {
public:
    std::optional<HostProperty> DescribeProperty(std::string_view) const override {
        return std::nullopt;
    }
    std::optional<HostFunction> DescribeFunction(std::string_view) const override {
        return std::nullopt;
    }
    std::optional<std::vector<Type>> DescribeEvent(const std::string_view event) const override {
        if (event == "ready") return std::vector<Type>{};
        return std::nullopt;
    }
    Value GetProperty(std::string_view) override { return Value{}; }
    void SetProperty(std::string_view, const Value&) override {}
    Value CallFunction(std::string_view, std::span<const Value>) override { return Value{}; }
    void CallMethod(ObjectRef, std::string_view, std::span<const Value>) override {}
    void Print(const std::string_view text) override {
        printed.emplace_back(text);
        if (onPrint) onPrint();
    }
    std::function<void()> onPrint;

    std::vector<std::string> printed;
};

constexpr const char* kFunctionSource =
    "var count = 3\n"
    "fn inner(x: int) -> int:\n"
    "    return x + 1\n"
    "\n"
    "fn outer(x: int) -> int:\n"
    "    let y = inner(x)\n"
    "    return y + 1\n";

std::shared_ptr<const Program> Compile(const std::string& source, const DebuggerHost& host) {
    const CompileResult result = CompileUVScriptSource(source, host);
    for (const Diagnostic& diagnostic : result.diagnostics) {
        ADD_FAILURE() << diagnostic.at.line << ": " << diagnostic.message;
    }
    return result.program;
}

TEST(UVScriptDebuggerTest, BreakpointPausesBeforeCallAndContinueReturnsResult) {
    DebuggerHost host;
    const auto program = Compile(kFunctionSource, host);
    ASSERT_NE(program, nullptr);
    ScriptInstance instance(program, host, Execution::Interpreted);
    ASSERT_TRUE(instance.EnableDebugger());
    ASSERT_TRUE(instance.SetDebuggerBreakpoints(std::array<std::uint32_t, 3>{0U, 6U, 6U}));

    const std::array<Value, 1> input{std::int64_t{41}};
    EXPECT_EQ(instance.Call("outer", input), std::nullopt);
    ASSERT_TRUE(instance.IsDebugPaused());
    const auto snapshot = instance.GetDebugSnapshot();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->line, 6U);
    EXPECT_EQ(snapshot->functionName, "outer");
    ASSERT_FALSE(snapshot->locals.empty());
    EXPECT_EQ(std::get<std::int64_t>(snapshot->locals[0]), 41);
    ASSERT_EQ(snapshot->fields.size(), 1U);
    EXPECT_EQ(snapshot->fields[0].first, "count");
    EXPECT_EQ(std::get<std::int64_t>(snapshot->fields[0].second), 3);

    EXPECT_EQ(instance.Call("outer", input), std::nullopt);
    EXPECT_FALSE(instance.RaiseEvent("ready"));
    instance.Advance(10.0);
    EXPECT_TRUE(instance.IsDebugPaused());
    ASSERT_TRUE(instance.SetField("count", std::int64_t{9}));
    EXPECT_EQ(std::get<std::int64_t>(snapshot->fields[0].second), 3);

    const ScriptDebugResumeResult result = instance.ContinueDebug();
    EXPECT_EQ(result.state, ScriptDebugState::Completed);
    ASSERT_TRUE(result.value.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*result.value), 43);
    EXPECT_FALSE(instance.IsDebugPaused());
    EXPECT_FALSE(instance.GetDebugSnapshot().has_value());
    EXPECT_EQ(instance.ContinueDebug().state, ScriptDebugState::NotPaused);
}

TEST(UVScriptDebuggerTest, StepIntoEntersCalleeAndStepOverReturnsToCaller) {
    DebuggerHost host;
    const auto program = Compile(kFunctionSource, host);
    ASSERT_NE(program, nullptr);
    ScriptInstance instance(program, host, Execution::Interpreted);
    ASSERT_TRUE(instance.SetDebuggerBreakpoints(std::array<std::uint32_t, 1>{6U}));
    const std::array<Value, 1> input{std::int64_t{41}};
    EXPECT_EQ(instance.Call("outer", input), std::nullopt);
    ASSERT_TRUE(instance.IsDebugPaused());

    EXPECT_EQ(instance.StepIntoDebug().state, ScriptDebugState::Paused);
    auto snapshot = instance.GetDebugSnapshot();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->line, 3U);
    EXPECT_EQ(snapshot->functionName, "inner");
    EXPECT_EQ(snapshot->callDepth, 2U);

    EXPECT_EQ(instance.StepOverDebug().state, ScriptDebugState::Paused);
    snapshot = instance.GetDebugSnapshot();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->line, 7U);
    EXPECT_EQ(snapshot->functionName, "outer");
    EXPECT_EQ(snapshot->callDepth, 1U);
}

TEST(UVScriptDebuggerTest, StepOverDoesNotPauseInsideCallee) {
    DebuggerHost host;
    const auto program = Compile(kFunctionSource, host);
    ASSERT_NE(program, nullptr);
    ScriptInstance instance(program, host, Execution::Interpreted);
    ASSERT_TRUE(instance.SetDebuggerBreakpoints(std::array<std::uint32_t, 1>{6U}));
    const std::array<Value, 1> input{std::int64_t{41}};
    EXPECT_EQ(instance.Call("outer", input), std::nullopt);

    EXPECT_EQ(instance.StepOverDebug().state, ScriptDebugState::Paused);
    const auto snapshot = instance.GetDebugSnapshot();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->line, 7U);
    EXPECT_EQ(snapshot->functionName, "outer");
    EXPECT_EQ(instance.ContinueDebug().state, ScriptDebugState::Completed);
}

TEST(UVScriptDebuggerTest, EventBreakpointAndWaitResumeThroughDebugger) {
    DebuggerHost host;
    const auto program = Compile(
        "on ready:\n"
        "    print(\"before\")\n"
        "    wait 0.5 s\n"
        "    print(\"after\")\n", host);
    ASSERT_NE(program, nullptr);
    ScriptInstance instance(program, host, Execution::Interpreted);
    ASSERT_TRUE(instance.SetDebuggerBreakpoints(std::array<std::uint32_t, 2>{2U, 4U}));
    EXPECT_TRUE(instance.RaiseEvent("ready"));
    ASSERT_TRUE(instance.IsDebugPaused());
    EXPECT_TRUE(host.printed.empty());
    EXPECT_EQ(instance.ContinueDebug().state, ScriptDebugState::Waiting);
    EXPECT_EQ(host.printed, (std::vector<std::string>{"before"}));
    EXPECT_EQ(instance.GetWaitingCount(), 1U);
    instance.Advance(0.5);
    ASSERT_TRUE(instance.IsDebugPaused());
    const auto snapshot = instance.GetDebugSnapshot();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->line, 4U);
    EXPECT_EQ(instance.ContinueDebug().state, ScriptDebugState::Completed);
    EXPECT_EQ(host.printed, (std::vector<std::string>{"before", "after"}));
    EXPECT_EQ(instance.GetWaitingCount(), 0U);
}

TEST(UVScriptDebuggerTest, DisableAbandonsPauseAndClearsBreakpoints) {
    DebuggerHost host;
    const auto program = Compile("on ready:\n    print(\"done\")\n", host);
    ASSERT_NE(program, nullptr);
    ScriptInstance instance(program, host, Execution::Interpreted);
    ASSERT_TRUE(instance.SetDebuggerBreakpoints(std::array<std::uint32_t, 1>{2U}));
    ASSERT_TRUE(instance.RaiseEvent("ready"));
    ASSERT_TRUE(instance.IsDebugPaused());
    instance.DisableDebugger();
    EXPECT_FALSE(instance.IsDebuggerEnabled());
    EXPECT_FALSE(instance.IsDebugPaused());
    EXPECT_FALSE(instance.GetDebugSnapshot().has_value());
    EXPECT_EQ(instance.ContinueDebug().state, ScriptDebugState::NotPaused);
    EXPECT_TRUE(host.printed.empty());
    ASSERT_TRUE(instance.EnableDebugger());
    EXPECT_TRUE(instance.RaiseEvent("ready"));
    EXPECT_EQ(host.printed, (std::vector<std::string>{"done"}));
}

TEST(UVScriptDebuggerTest, LoopBreakpointHitsEveryIterationAndCanBeClearedWhilePaused) {
    DebuggerHost host;
    const auto program = Compile(
        "fn run() -> int:\n"
        "    let i = 0\n"
        "    while i < 3:\n"
        "        i += 1\n"
        "    return i\n", host);
    ASSERT_NE(program, nullptr);
    ScriptInstance instance(program, host, Execution::Interpreted);
    ASSERT_TRUE(instance.SetDebuggerBreakpoints(std::array<std::uint32_t, 1>{4U}));
    EXPECT_EQ(instance.Call("run"), std::nullopt);
    for (std::int64_t i = 0; i < 3; ++i) {
        ASSERT_TRUE(instance.IsDebugPaused());
        const auto snapshot = instance.GetDebugSnapshot();
        ASSERT_TRUE(snapshot.has_value());
        ASSERT_FALSE(snapshot->locals.empty());
        EXPECT_EQ(std::get<std::int64_t>(snapshot->locals[0]), i);
        if (i == 2) {
            instance.ClearDebuggerBreakpoints();
            const auto result = instance.ContinueDebug();
            EXPECT_EQ(result.state, ScriptDebugState::Completed);
            ASSERT_TRUE(result.value.has_value());
            EXPECT_EQ(std::get<std::int64_t>(*result.value), 3);
        } else {
            EXPECT_EQ(instance.ContinueDebug().state, ScriptDebugState::Paused);
        }
    }
}

TEST(UVScriptDebuggerTest, RuntimeFailureClearsPauseAndNextCallCanSucceed) {
    DebuggerHost host;
    const auto program = Compile("fn divide(x: int) -> float:\n    return 10 / x\n", host);
    ASSERT_NE(program, nullptr);
    ScriptInstance instance(program, host, Execution::Interpreted);
    ASSERT_TRUE(instance.SetDebuggerBreakpoints(std::array<std::uint32_t, 1>{2U}));
    EXPECT_EQ(instance.Call("divide", std::array<Value, 1>{std::int64_t{0}}), std::nullopt);
    EXPECT_EQ(instance.ContinueDebug().state, ScriptDebugState::Failed);
    EXPECT_FALSE(instance.IsDebugPaused());
    EXPECT_FALSE(instance.GetLastError().empty());
    EXPECT_EQ(instance.Call("divide", std::array<Value, 1>{std::int64_t{2}}), std::nullopt);
    EXPECT_EQ(instance.ContinueDebug().state, ScriptDebugState::Completed);
    EXPECT_TRUE(instance.GetLastError().empty());
}

TEST(UVScriptDebuggerTest, MultipleWaitingHandlersAreNotLostWhenOnePauses) {
    DebuggerHost host;
    const auto program = Compile("on ready:\n    wait 0.5 s\n    print(\"done\")\n", host);
    ASSERT_NE(program, nullptr);
    ScriptInstance instance(program, host, Execution::Interpreted);
    ASSERT_TRUE(instance.SetDebuggerBreakpoints(std::array<std::uint32_t, 1>{3U}));
    ASSERT_TRUE(instance.RaiseEvent("ready"));
    ASSERT_TRUE(instance.RaiseEvent("ready"));
    ASSERT_EQ(instance.GetWaitingCount(), 2U);
    instance.Advance(0.5);
    ASSERT_TRUE(instance.IsDebugPaused());
    EXPECT_EQ(instance.GetWaitingCount(), 1U);
    instance.Advance(1.0);
    EXPECT_TRUE(host.printed.empty());
    EXPECT_EQ(instance.ContinueDebug().state, ScriptDebugState::Completed);
    instance.Advance(0.0);
    ASSERT_TRUE(instance.IsDebugPaused());
    EXPECT_EQ(instance.ContinueDebug().state, ScriptDebugState::Completed);
    EXPECT_EQ(instance.GetWaitingCount(), 0U);
    EXPECT_EQ(host.printed, (std::vector<std::string>{"done", "done"}));
}

TEST(UVScriptDebuggerTest, ExplicitCalleeBreakpointInterruptsStepOver) {
    DebuggerHost host;
    const auto program = Compile(kFunctionSource, host);
    ASSERT_NE(program, nullptr);
    ScriptInstance instance(program, host, Execution::Interpreted);
    ASSERT_TRUE(instance.SetDebuggerBreakpoints(std::array<std::uint32_t, 2>{3U, 6U}));
    EXPECT_EQ(instance.Call("outer", std::array<Value, 1>{std::int64_t{41}}), std::nullopt);
    EXPECT_EQ(instance.StepOverDebug().state, ScriptDebugState::Paused);
    const auto snapshot = instance.GetDebugSnapshot();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->functionName, "inner");
    EXPECT_EQ(snapshot->line, 3U);
    EXPECT_EQ(snapshot->callDepth, 2U);
    EXPECT_EQ(instance.ContinueDebug().state, ScriptDebugState::Completed);
}

TEST(UVScriptDebuggerTest, DebuggerRejectsSameInstanceReentryFromHostCallback) {
    DebuggerHost host;
    const auto program = Compile("on ready:\n    print(\"done\")\n"
                                    "fn run() -> int:\n    return 7\n", host);
    ASSERT_NE(program, nullptr);
    ScriptInstance instance(program, host, Execution::Interpreted);
    ASSERT_TRUE(instance.EnableDebugger());
    host.onPrint = [&instance] {
        EXPECT_EQ(instance.Call("run"), std::nullopt);
        EXPECT_FALSE(instance.RaiseEvent("ready"));
        EXPECT_FALSE(instance.EnableDebugger());
        instance.Advance(1.0);
    };
    EXPECT_TRUE(instance.RaiseEvent("ready"));
    EXPECT_EQ(host.printed, (std::vector<std::string>{"done"}));
    const auto result = instance.Call("run");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*result), 7);
}

TEST(UVScriptDebuggerTest, NativeExecutionRejectsDebuggerAndExplicitInterpreterAcceptsIt) {
    DebuggerHost host;
    std::ifstream file(std::string{UVE_UVSCRIPT_NATIVE_DIR} + "/math.uvs");
    ASSERT_TRUE(file.is_open());
    const std::string source{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    const auto program = Compile(source, host);
    ASSERT_NE(program, nullptr);
    ScriptInstance native(program, host);
    ASSERT_TRUE(native.IsNative());
    EXPECT_FALSE(native.EnableDebugger());
    EXPECT_FALSE(native.SetDebuggerBreakpoints(std::array<std::uint32_t, 1>{6U}));
    EXPECT_FALSE(native.IsDebuggerEnabled());
    EXPECT_EQ(native.StepIntoDebug().state, ScriptDebugState::NotPaused);
    ScriptInstance interpreted(program, host, Execution::Interpreted);
    ASSERT_FALSE(interpreted.IsNative());
    EXPECT_TRUE(interpreted.EnableDebugger());
}

} // namespace
} // namespace UVE::Blueprint::Tests
