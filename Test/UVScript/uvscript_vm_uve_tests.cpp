// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <map>
#include <numbers>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/uvscript/uvscript_compiler_uve.h"
#include "uve/uvscript/uvscript_instance_uve.h"

namespace UVE::UVScript::Tests {
namespace {

/// A CharacterBody3D-like node: a velocity, a read-only floor flag, input, and three events.
class FakeHostUVE final : public UVScriptHostUVE {
public:
    std::optional<HostPropertyUVE> DescribePropertyUVE(const std::string_view name) const override {
        if (name == "velocity") return HostPropertyUVE{TypeUVE::Vec3UVE(), true};
        if (name == "is_on_floor") return HostPropertyUVE{TypeUVE::BoolUVE(), false};
        if (name == "health") return HostPropertyUVE{TypeUVE::IntUVE(), true};
        return std::nullopt;
    }
    std::optional<HostFunctionUVE> DescribeFunctionUVE(const std::string_view name) const override {
        if (name == "input.axis") return HostFunctionUVE{{TypeUVE::StrUVE(), TypeUVE::StrUVE()}, TypeUVE::FloatUVE()};
        return std::nullopt;
    }
    std::optional<std::vector<TypeUVE>> DescribeEventUVE(const std::string_view event) const override {
        if (event == "ready") return std::vector<TypeUVE>{};
        if (event == "tick") return std::vector<TypeUVE>{TypeUVE::FloatUVE()};
        if (event == "body_entered") return std::vector<TypeUVE>{TypeUVE::NodeUVE("Node3D")};
        return std::nullopt;
    }
    ValueUVE GetPropertyUVE(const std::string_view name) override {
        if (name == "velocity") return velocity;
        if (name == "is_on_floor") return onFloor;
        return health;
    }
    void SetPropertyUVE(const std::string_view name, const ValueUVE& value) override {
        if (name == "velocity") velocity = std::get<Vec3ValueUVE>(value);
        else health = std::get<std::int64_t>(value);
    }
    ValueUVE CallFunctionUVE(const std::string_view name, std::span<const ValueUVE> args) override {
        calls.push_back(std::string{name} + "(" + std::get<std::string>(args[0]) + "," + std::get<std::string>(args[1]) + ")");
        return axis;
    }
    void PrintUVE(const std::string_view text) override { printed.emplace_back(text); }

    Vec3ValueUVE velocity{};
    bool onFloor = true;
    std::int64_t health = 10;
    double axis = 1.0;
    std::vector<std::string> printed;
    std::vector<std::string> calls;
};

[[nodiscard]] std::shared_ptr<const ProgramUVE> CompileOrFailUVE(const std::string& source, const FakeHostUVE& host) {
    const CompileResultUVE result = CompileUVScriptSourceUVE(source, host);
    for (const DiagnosticUVE& d : result.diagnostics) {
        ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
    }
    return result.program;
}

[[nodiscard]] std::vector<std::string> ErrorsOfUVE(const std::string& source) {
    FakeHostUVE host;
    std::vector<std::string> messages;
    for (const DiagnosticUVE& d : CompileUVScriptSourceUVE(source, host).diagnostics) {
        messages.push_back(std::to_string(d.at.line) + ": " + d.message);
    }
    return messages;
}

TEST(UVScriptVmUVETest, RunsAPlayerController) {
    FakeHostUVE host;
    const auto program = CompileOrFailUVE(R"(entity Player : CharacterBody3D
export speed: float = 6.0
export jump = 2.0 m
var jumps = 0

on ready:
    print("{jumps} jumps, speed {speed}")

on tick(dt):
    velocity.x = input.axis("left", "right") * speed
    if is_on_floor:
        velocity.y = sqrt(2.0 * 9.8 * jump)
        jumps += 1
)", host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready"));
    ASSERT_EQ(host.printed, (std::vector<std::string>{"0 jumps, speed 6.0"}));

    const std::array<ValueUVE, 1> dt{0.016};
    ASSERT_TRUE(instance.RaiseEventUVE("tick", dt)) << instance.GetLastErrorUVE();
    EXPECT_DOUBLE_EQ(host.velocity.x, 6.0);
    EXPECT_NEAR(host.velocity.y, std::sqrt(2.0 * 9.8 * 2.0), 1e-9);
    EXPECT_EQ(std::get<std::int64_t>(*instance.GetFieldUVE("jumps")), 1);
    EXPECT_EQ(host.calls, (std::vector<std::string>{"input.axis(left,right)"}));
    EXPECT_FALSE(instance.RaiseEventUVE("body_entered")); // wrong argument count
}

TEST(UVScriptVmUVETest, EvaluatesArithmeticUnitsAndControlFlow) {
    FakeHostUVE host;
    const auto program = CompileOrFailUVE(R"(
const TURN = 90 deg
const SHORT = 250 ms

fn sum_to(n: int) -> int:
    let total = 0
    for i in 0..n + 1:
        if i % 2 == 0:
            continue
        total += i
    return total

fn fib(n: int) -> int:
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

fn first_over(limit: int) -> int:
    let i = 0
    while true:
        i += 1
        if i * i > limit:
            break
    return i

fn mix() -> float:
    return 7 / 2 + 1 * 2.5 - -1
)", host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host);
    EXPECT_NEAR(std::get<double>(*instance.GetFieldUVE("TURN")), std::numbers::pi / 2.0, 1e-12);
    EXPECT_DOUBLE_EQ(std::get<double>(*instance.GetFieldUVE("SHORT")), 0.25);
    const std::array<ValueUVE, 1> ten{std::int64_t{10}};
    EXPECT_EQ(std::get<std::int64_t>(*instance.CallUVE("sum_to", ten)), 25); // 1+3+5+7+9
    EXPECT_EQ(std::get<std::int64_t>(*instance.CallUVE("fib", ten)), 55);
    const std::array<ValueUVE, 1> fifty{std::int64_t{50}};
    EXPECT_EQ(std::get<std::int64_t>(*instance.CallUVE("first_over", fifty)), 8);
    EXPECT_DOUBLE_EQ(std::get<double>(*instance.CallUVE("mix")), 3.5 + 2.5 + 1.0);
}

TEST(UVScriptVmUVETest, WaitPausesAHandlerForExactlyItsTime) {
    FakeHostUVE host;
    const auto program = CompileOrFailUVE(R"(
on ready:
    print("a")
    wait 0.5 s
    print("b")
    wait 100 ms
    print("c")
)", host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready"));
    EXPECT_EQ(host.printed, (std::vector<std::string>{"a"}));
    EXPECT_EQ(instance.GetWaitingCountUVE(), 1U);
    instance.AdvanceUVE(0.3);
    EXPECT_EQ(host.printed.size(), 1U);
    instance.AdvanceUVE(0.2);
    EXPECT_EQ(host.printed, (std::vector<std::string>{"a", "b"}));
    instance.AdvanceUVE(0.1);
    EXPECT_EQ(host.printed, (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_EQ(instance.GetWaitingCountUVE(), 0U);
}

TEST(UVScriptVmUVETest, ReportsTypeErrorsBeforeRunning) {
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let a = 1\n    a = \"text\"\n"),
              (std::vector<std::string>{"3: this assignment needs int but this is str"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    print(nope)\n"), (std::vector<std::string>{"2: nothing is called 'nope' here"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    is_on_floor = true\n"),
              (std::vector<std::string>{"2: 'is_on_floor' can be read but not changed"}));
    EXPECT_EQ(ErrorsOfUVE("const A = 1\non ready:\n    A = 2\n"),
              (std::vector<std::string>{"3: 'A' is a const and cannot change"}));
    EXPECT_EQ(ErrorsOfUVE("on jump:\n    pass\n"), (std::vector<std::string>{"1: this node has no event 'jump'"}));
    EXPECT_EQ(ErrorsOfUVE("fn f() -> int:\n    pass\n"),
              (std::vector<std::string>{"1: 'f' can reach its end without returning a int"}));
    EXPECT_EQ(ErrorsOfUVE("fn f():\n    wait 1 s\n"), (std::vector<std::string>{"2: 'wait' only works inside an 'on' block"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    break\n"), (std::vector<std::string>{"2: 'break' only works inside a loop"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    if 1:\n        pass\n"),
              (std::vector<std::string>{"2: a condition must be true or false, not int"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    1 + 2\n"),
              (std::vector<std::string>{"2: this line works out a value and then drops it - did you mean to assign it?"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let v = velocity + 1\n"),
              (std::vector<std::string>{"2: '+' does not work on vec3 and int"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let x = 1\n    x /= 2\n"),
              (std::vector<std::string>{"3: '/=' would turn this int into float"}));
}

TEST(UVScriptVmUVETest, StopsRunawayLoopsAndRuntimeErrors) {
    FakeHostUVE host;
    const auto program = CompileOrFailUVE(R"(
fn spin():
    while true:
        pass

fn divide(a: int, b: int) -> int:
    return a % b

on ready:
    health = 3
)", host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host);
    EXPECT_FALSE(instance.CallUVE("spin").has_value());
    EXPECT_NE(instance.GetLastErrorUVE().find("ran too long"), std::string::npos);
    const std::array<ValueUVE, 2> byZero{std::int64_t{1}, std::int64_t{0}};
    EXPECT_FALSE(instance.CallUVE("divide", byZero).has_value());
    EXPECT_EQ(instance.GetLastErrorUVE(), "line 7: '%' by zero");
    ASSERT_TRUE(instance.RaiseEventUVE("ready"));
    EXPECT_EQ(host.health, 3);
}

TEST(UVScriptVmUVETest, FieldsAreSetFromTheInspectorWithTheirOwnType) {
    FakeHostUVE host;
    const auto program = CompileOrFailUVE("export speed = 4.0\nconst LIMIT = 3\n", host);
    ASSERT_NE(program, nullptr);
    const std::vector<FieldInfoUVE> fields = GetProgramFieldsUVE(*program);
    ASSERT_EQ(fields.size(), 2U);
    EXPECT_EQ(fields[0].type, TypeUVE::FloatUVE());
    EXPECT_EQ(fields[1].kind, FieldKindUVE::Const);
    ScriptInstanceUVE instance(program, host);
    EXPECT_TRUE(instance.SetFieldUVE("speed", 9.5));
    EXPECT_DOUBLE_EQ(std::get<double>(*instance.GetFieldUVE("speed")), 9.5);
    EXPECT_FALSE(instance.SetFieldUVE("speed", std::string{"fast"}));
    EXPECT_FALSE(instance.SetFieldUVE("LIMIT", std::int64_t{4}));
    EXPECT_FALSE(instance.SetFieldUVE("missing", 1.0));
}

} // namespace
} // namespace UVE::UVScript::Tests
