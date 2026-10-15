// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/uvscript/uvscript_instance_uve.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/uvscript/uvscript_compiler_uve.h"
#include "uve/uvscript/uvscript_host_uve.h"

namespace UVE::UVScript::Tests {
namespace {

class DebuggerHostUVE final : public UVScriptHostUVE {
public:
    std::optional<HostPropertyUVE> DescribePropertyUVE(std::string_view) const override {
        return std::nullopt;
    }
    std::optional<HostFunctionUVE> DescribeFunctionUVE(std::string_view) const override {
        return std::nullopt;
    }
    std::optional<std::vector<TypeUVE>> DescribeEventUVE(const std::string_view event) const override {
        if (event == "ready") return std::vector<TypeUVE>{};
        return std::nullopt;
    }
    ValueUVE GetPropertyUVE(std::string_view) override { return ValueUVE{}; }
    void SetPropertyUVE(std::string_view, const ValueUVE&) override {}
    ValueUVE CallFunctionUVE(std::string_view, std::span<const ValueUVE>) override { return ValueUVE{}; }
    void CallMethodUVE(ObjectRefUVE, std::string_view, std::span<const ValueUVE>) override {}
    void PrintUVE(const std::string_view text) override {
        printed.emplace_back(text);
        if (onPrint) onPrint();
    }
    std::function<void()> onPrint;

    std::vector<std::string> printed;
};

constexpr const char* kFunctionSourceUVE =
    "var count = 3\n"
    "fn inner(x: int) -> int:\n"
    "    return x + 1\n"
    "\n"
    "fn outer(x: int) -> int:\n"
    "    let y = inner(x)\n"
    "    return y + 1\n";

std::shared_ptr<const ProgramUVE> CompileUVE(const std::string& source, const DebuggerHostUVE& host) {
    const CompileResultUVE result = CompileUVScriptSourceUVE(source, host);
    for (const DiagnosticUVE& diagnostic : result.diagnostics) {
        ADD_FAILURE() << diagnostic.at.line << ": " << diagnostic.message;
    }
    return result.program;
}

TEST(UVScriptDebuggerUVETest, BreakpointPausesBeforeCallAndContinueReturnsResult) {
    DebuggerHostUVE host;
    const auto program = CompileUVE(kFunctionSourceUVE, host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host, ExecutionUVE::Interpreted);
    ASSERT_TRUE(instance.EnableDebuggerUVE());
    ASSERT_TRUE(instance.SetDebuggerBreakpointsUVE(std::array<std::uint32_t, 3>{0U, 6U, 6U}));

    const std::array<ValueUVE, 1> input{std::int64_t{41}};
    EXPECT_EQ(instance.CallUVE("outer", input), std::nullopt);
    ASSERT_TRUE(instance.IsDebugPausedUVE());
    const auto snapshot = instance.GetDebugSnapshotUVE();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->line, 6U);
    EXPECT_EQ(snapshot->functionName, "outer");
    ASSERT_FALSE(snapshot->locals.empty());
    EXPECT_EQ(std::get<std::int64_t>(snapshot->locals[0]), 41);
    ASSERT_EQ(snapshot->fields.size(), 1U);
    EXPECT_EQ(snapshot->fields[0].first, "count");
    EXPECT_EQ(std::get<std::int64_t>(snapshot->fields[0].second), 3);

    EXPECT_EQ(instance.CallUVE("outer", input), std::nullopt);
    EXPECT_FALSE(instance.RaiseEventUVE("ready"));
    instance.AdvanceUVE(10.0);
    EXPECT_TRUE(instance.IsDebugPausedUVE());
    ASSERT_TRUE(instance.SetFieldUVE("count", std::int64_t{9}));
    EXPECT_EQ(std::get<std::int64_t>(snapshot->fields[0].second), 3);

    const ScriptDebugResumeResultUVE result = instance.ContinueDebugUVE();
    EXPECT_EQ(result.state, ScriptDebugStateUVE::Completed);
    ASSERT_TRUE(result.value.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*result.value), 43);
    EXPECT_FALSE(instance.IsDebugPausedUVE());
    EXPECT_FALSE(instance.GetDebugSnapshotUVE().has_value());
    EXPECT_EQ(instance.ContinueDebugUVE().state, ScriptDebugStateUVE::NotPaused);
}

TEST(UVScriptDebuggerUVETest, StepIntoEntersCalleeAndStepOverReturnsToCaller) {
    DebuggerHostUVE host;
    const auto program = CompileUVE(kFunctionSourceUVE, host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host, ExecutionUVE::Interpreted);
    ASSERT_TRUE(instance.SetDebuggerBreakpointsUVE(std::array<std::uint32_t, 1>{6U}));
    const std::array<ValueUVE, 1> input{std::int64_t{41}};
    EXPECT_EQ(instance.CallUVE("outer", input), std::nullopt);
    ASSERT_TRUE(instance.IsDebugPausedUVE());

    EXPECT_EQ(instance.StepIntoDebugUVE().state, ScriptDebugStateUVE::Paused);
    auto snapshot = instance.GetDebugSnapshotUVE();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->line, 3U);
    EXPECT_EQ(snapshot->functionName, "inner");
    EXPECT_EQ(snapshot->callDepth, 2U);

    EXPECT_EQ(instance.StepOverDebugUVE().state, ScriptDebugStateUVE::Paused);
    snapshot = instance.GetDebugSnapshotUVE();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->line, 7U);
    EXPECT_EQ(snapshot->functionName, "outer");
    EXPECT_EQ(snapshot->callDepth, 1U);
}

TEST(UVScriptDebuggerUVETest, StepOverDoesNotPauseInsideCallee) {
    DebuggerHostUVE host;
    const auto program = CompileUVE(kFunctionSourceUVE, host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host, ExecutionUVE::Interpreted);
    ASSERT_TRUE(instance.SetDebuggerBreakpointsUVE(std::array<std::uint32_t, 1>{6U}));
    const std::array<ValueUVE, 1> input{std::int64_t{41}};
    EXPECT_EQ(instance.CallUVE("outer", input), std::nullopt);

    EXPECT_EQ(instance.StepOverDebugUVE().state, ScriptDebugStateUVE::Paused);
    const auto snapshot = instance.GetDebugSnapshotUVE();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->line, 7U);
    EXPECT_EQ(snapshot->functionName, "outer");
    EXPECT_EQ(instance.ContinueDebugUVE().state, ScriptDebugStateUVE::Completed);
}

TEST(UVScriptDebuggerUVETest, EventBreakpointAndWaitResumeThroughDebugger) {
    DebuggerHostUVE host;
    const auto program = CompileUVE(
        "on ready:\n"
        "    print(\"before\")\n"
        "    wait 0.5 s\n"
        "    print(\"after\")\n", host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host, ExecutionUVE::Interpreted);
    ASSERT_TRUE(instance.SetDebuggerBreakpointsUVE(std::array<std::uint32_t, 2>{2U, 4U}));
    EXPECT_TRUE(instance.RaiseEventUVE("ready"));
    ASSERT_TRUE(instance.IsDebugPausedUVE());
    EXPECT_TRUE(host.printed.empty());
    EXPECT_EQ(instance.ContinueDebugUVE().state, ScriptDebugStateUVE::Waiting);
    EXPECT_EQ(host.printed, (std::vector<std::string>{"before"}));
    EXPECT_EQ(instance.GetWaitingCountUVE(), 1U);
    instance.AdvanceUVE(0.5);
    ASSERT_TRUE(instance.IsDebugPausedUVE());
    const auto snapshot = instance.GetDebugSnapshotUVE();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->line, 4U);
    EXPECT_EQ(instance.ContinueDebugUVE().state, ScriptDebugStateUVE::Completed);
    EXPECT_EQ(host.printed, (std::vector<std::string>{"before", "after"}));
    EXPECT_EQ(instance.GetWaitingCountUVE(), 0U);
}

TEST(UVScriptDebuggerUVETest, DisableAbandonsPauseAndClearsBreakpoints) {
    DebuggerHostUVE host;
    const auto program = CompileUVE("on ready:\n    print(\"done\")\n", host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host, ExecutionUVE::Interpreted);
    ASSERT_TRUE(instance.SetDebuggerBreakpointsUVE(std::array<std::uint32_t, 1>{2U}));
    ASSERT_TRUE(instance.RaiseEventUVE("ready"));
    ASSERT_TRUE(instance.IsDebugPausedUVE());
    instance.DisableDebuggerUVE();
    EXPECT_FALSE(instance.IsDebuggerEnabledUVE());
    EXPECT_FALSE(instance.IsDebugPausedUVE());
    EXPECT_FALSE(instance.GetDebugSnapshotUVE().has_value());
    EXPECT_EQ(instance.ContinueDebugUVE().state, ScriptDebugStateUVE::NotPaused);
    EXPECT_TRUE(host.printed.empty());
    ASSERT_TRUE(instance.EnableDebuggerUVE());
    EXPECT_TRUE(instance.RaiseEventUVE("ready"));
    EXPECT_EQ(host.printed, (std::vector<std::string>{"done"}));
}

TEST(UVScriptDebuggerUVETest, LoopBreakpointHitsEveryIterationAndCanBeClearedWhilePaused) {
    DebuggerHostUVE host;
    const auto program = CompileUVE(
        "fn run() -> int:\n"
        "    let i = 0\n"
        "    while i < 3:\n"
        "        i += 1\n"
        "    return i\n", host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host, ExecutionUVE::Interpreted);
    ASSERT_TRUE(instance.SetDebuggerBreakpointsUVE(std::array<std::uint32_t, 1>{4U}));
    EXPECT_EQ(instance.CallUVE("run"), std::nullopt);
    for (std::int64_t i = 0; i < 3; ++i) {
        ASSERT_TRUE(instance.IsDebugPausedUVE());
        const auto snapshot = instance.GetDebugSnapshotUVE();
        ASSERT_TRUE(snapshot.has_value());
        ASSERT_FALSE(snapshot->locals.empty());
        EXPECT_EQ(std::get<std::int64_t>(snapshot->locals[0]), i);
        if (i == 2) {
            instance.ClearDebuggerBreakpointsUVE();
            const auto result = instance.ContinueDebugUVE();
            EXPECT_EQ(result.state, ScriptDebugStateUVE::Completed);
            ASSERT_TRUE(result.value.has_value());
            EXPECT_EQ(std::get<std::int64_t>(*result.value), 3);
        } else {
            EXPECT_EQ(instance.ContinueDebugUVE().state, ScriptDebugStateUVE::Paused);
        }
    }
}

TEST(UVScriptDebuggerUVETest, RuntimeFailureClearsPauseAndNextCallCanSucceed) {
    DebuggerHostUVE host;
    const auto program = CompileUVE("fn divide(x: int) -> float:\n    return 10 / x\n", host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host, ExecutionUVE::Interpreted);
    ASSERT_TRUE(instance.SetDebuggerBreakpointsUVE(std::array<std::uint32_t, 1>{2U}));
    EXPECT_EQ(instance.CallUVE("divide", std::array<ValueUVE, 1>{std::int64_t{0}}), std::nullopt);
    EXPECT_EQ(instance.ContinueDebugUVE().state, ScriptDebugStateUVE::Failed);
    EXPECT_FALSE(instance.IsDebugPausedUVE());
    EXPECT_FALSE(instance.GetLastErrorUVE().empty());
    EXPECT_EQ(instance.CallUVE("divide", std::array<ValueUVE, 1>{std::int64_t{2}}), std::nullopt);
    EXPECT_EQ(instance.ContinueDebugUVE().state, ScriptDebugStateUVE::Completed);
    EXPECT_TRUE(instance.GetLastErrorUVE().empty());
}

TEST(UVScriptDebuggerUVETest, MultipleWaitingHandlersAreNotLostWhenOnePauses) {
    DebuggerHostUVE host;
    const auto program = CompileUVE("on ready:\n    wait 0.5 s\n    print(\"done\")\n", host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host, ExecutionUVE::Interpreted);
    ASSERT_TRUE(instance.SetDebuggerBreakpointsUVE(std::array<std::uint32_t, 1>{3U}));
    ASSERT_TRUE(instance.RaiseEventUVE("ready"));
    ASSERT_TRUE(instance.RaiseEventUVE("ready"));
    ASSERT_EQ(instance.GetWaitingCountUVE(), 2U);
    instance.AdvanceUVE(0.5);
    ASSERT_TRUE(instance.IsDebugPausedUVE());
    EXPECT_EQ(instance.GetWaitingCountUVE(), 1U);
    instance.AdvanceUVE(1.0);
    EXPECT_TRUE(host.printed.empty());
    EXPECT_EQ(instance.ContinueDebugUVE().state, ScriptDebugStateUVE::Completed);
    instance.AdvanceUVE(0.0);
    ASSERT_TRUE(instance.IsDebugPausedUVE());
    EXPECT_EQ(instance.ContinueDebugUVE().state, ScriptDebugStateUVE::Completed);
    EXPECT_EQ(instance.GetWaitingCountUVE(), 0U);
    EXPECT_EQ(host.printed, (std::vector<std::string>{"done", "done"}));
}

TEST(UVScriptDebuggerUVETest, ExplicitCalleeBreakpointInterruptsStepOver) {
    DebuggerHostUVE host;
    const auto program = CompileUVE(kFunctionSourceUVE, host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host, ExecutionUVE::Interpreted);
    ASSERT_TRUE(instance.SetDebuggerBreakpointsUVE(std::array<std::uint32_t, 2>{3U, 6U}));
    EXPECT_EQ(instance.CallUVE("outer", std::array<ValueUVE, 1>{std::int64_t{41}}), std::nullopt);
    EXPECT_EQ(instance.StepOverDebugUVE().state, ScriptDebugStateUVE::Paused);
    const auto snapshot = instance.GetDebugSnapshotUVE();
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->functionName, "inner");
    EXPECT_EQ(snapshot->line, 3U);
    EXPECT_EQ(snapshot->callDepth, 2U);
    EXPECT_EQ(instance.ContinueDebugUVE().state, ScriptDebugStateUVE::Completed);
}

TEST(UVScriptDebuggerUVETest, DebuggerRejectsSameInstanceReentryFromHostCallback) {
    DebuggerHostUVE host;
    const auto program = CompileUVE("on ready:\n    print(\"done\")\n"
                                    "fn run() -> int:\n    return 7\n", host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host, ExecutionUVE::Interpreted);
    ASSERT_TRUE(instance.EnableDebuggerUVE());
    host.onPrint = [&instance] {
        EXPECT_EQ(instance.CallUVE("run"), std::nullopt);
        EXPECT_FALSE(instance.RaiseEventUVE("ready"));
        EXPECT_FALSE(instance.EnableDebuggerUVE());
        instance.AdvanceUVE(1.0);
    };
    EXPECT_TRUE(instance.RaiseEventUVE("ready"));
    EXPECT_EQ(host.printed, (std::vector<std::string>{"done"}));
    const auto result = instance.CallUVE("run");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*result), 7);
}

TEST(UVScriptDebuggerUVETest, NativeExecutionRejectsDebuggerAndExplicitInterpreterAcceptsIt) {
    DebuggerHostUVE host;
    std::ifstream file(std::string{UVE_UVSCRIPT_NATIVE_DIR} + "/math.uvs");
    ASSERT_TRUE(file.is_open());
    const std::string source{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    const auto program = CompileUVE(source, host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE native(program, host);
    ASSERT_TRUE(native.IsNativeUVE());
    EXPECT_FALSE(native.EnableDebuggerUVE());
    EXPECT_FALSE(native.SetDebuggerBreakpointsUVE(std::array<std::uint32_t, 1>{6U}));
    EXPECT_FALSE(native.IsDebuggerEnabledUVE());
    EXPECT_EQ(native.StepIntoDebugUVE().state, ScriptDebugStateUVE::NotPaused);
    ScriptInstanceUVE interpreted(program, host, ExecutionUVE::Interpreted);
    ASSERT_FALSE(interpreted.IsNativeUVE());
    EXPECT_TRUE(interpreted.EnableDebuggerUVE());
}

} // namespace
} // namespace UVE::UVScript::Tests
