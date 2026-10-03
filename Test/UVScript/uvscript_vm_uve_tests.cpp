// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <numbers>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/uvscript/uvscript_codegen_uve.h"
#include "uve/uvscript/uvscript_compiler_uve.h"
#include "uve/uvscript/uvscript_host_description_uve.h"
#include "uve/uvscript/uvscript_instance_uve.h"

namespace UVE::UVScript::Tests {
namespace {

/// A Character3D-like object: a velocity, a read-only floor flag, input, and three events.
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
        if (event == "body_entered") return std::vector<TypeUVE>{TypeUVE::ObjectUVE("Object3D")};
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
    const auto program = CompileOrFailUVE(R"(entity Player : Character3D
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
    EXPECT_EQ(ErrorsOfUVE("on jump:\n    pass\n"), (std::vector<std::string>{"1: this object has no event 'jump'"}));
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

TEST(UVScriptVmUVETest, ValueTextReadsBackWhatFormatWrites) {
    const std::array<std::pair<ValueUVE, TypeUVE>, 5> values{{
        {ValueUVE{true}, TypeUVE::BoolUVE()},
        {ValueUVE{std::int64_t{-12}}, TypeUVE::IntUVE()},
        {ValueUVE{6.0}, TypeUVE::FloatUVE()},
        {ValueUVE{std::string{"hero one"}}, TypeUVE::StrUVE()},
        {ValueUVE{Vec3ValueUVE{0.5, -1.0, 2.0}}, TypeUVE::Vec3UVE()},
    }};
    for (const auto& [value, type] : values) {
        EXPECT_EQ(ParseValueTextUVE(FormatValueUVE(value), type), value) << FormatValueUVE(value);
    }
    EXPECT_EQ(ParseValueTextUVE(" 1, 2 ,3 ", TypeUVE::Vec3UVE()), ValueUVE{(Vec3ValueUVE{1.0, 2.0, 3.0})});
    EXPECT_EQ(ParseValueTextUVE("4", TypeUVE::FloatUVE()), ValueUVE{4.0});
    EXPECT_FALSE(ParseValueTextUVE("4.5", TypeUVE::IntUVE()).has_value());
    EXPECT_FALSE(ParseValueTextUVE("yes", TypeUVE::BoolUVE()).has_value());
    EXPECT_FALSE(ParseValueTextUVE("1, 2", TypeUVE::Vec3UVE()).has_value());
    EXPECT_FALSE(ParseValueTextUVE("nan", TypeUVE::FloatUVE()).has_value());
    EXPECT_FALSE(ParseValueTextUVE("", TypeUVE::IntUVE()).has_value());
}

// ---- Native code: the same scripts compiled to C++ by uvsc at build time (Test/CMakeLists.txt).

[[nodiscard]] std::string ReadNativeScriptUVE(const std::string& name) {
    std::ifstream file(std::string{UVE_UVSCRIPT_NATIVE_DIR} + "/" + name, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

/// Everything a run leaves behind, so an interpreted and a native run can be compared whole.
struct TranscriptUVE final {
    std::vector<std::string> printed;
    std::vector<std::string> calls;
    Vec3ValueUVE velocity{};
    std::int64_t health = 0;
    std::vector<std::string> results;

    bool operator==(const TranscriptUVE&) const = default;
};

/// Compiles `name` against FakeHostUVE and hands `run` an instance in the given mode.
template <typename RunFn>
[[nodiscard]] TranscriptUVE RunScriptUVE(const std::string& name, const ExecutionUVE execution, bool& native,
                                         const RunFn& run) {
    FakeHostUVE host;
    host.onFloor = false;
    const auto program = CompileOrFailUVE(ReadNativeScriptUVE(name), host);
    TranscriptUVE transcript;
    if (program == nullptr) {
        return transcript;
    }
    ScriptInstanceUVE instance(program, host, execution);
    native = instance.IsNativeUVE();
    run(instance, host, transcript.results);
    transcript.printed = host.printed;
    transcript.calls = host.calls;
    transcript.velocity = host.velocity;
    transcript.health = host.health;
    return transcript;
}

/// Runs `name` both ways and checks the native run really was native and matched the interpreter.
template <typename RunFn>
void ExpectNativeMatchesInterpreterUVE(const std::string& name, const RunFn& run) {
    bool interpretedNative = true;
    bool native = false;
    const TranscriptUVE interpreted = RunScriptUVE(name, ExecutionUVE::Interpreted, interpretedNative, run);
    const TranscriptUVE compiled = RunScriptUVE(name, ExecutionUVE::Auto, native, run);
    EXPECT_FALSE(interpretedNative) << name;
    EXPECT_TRUE(native) << name << " has no native code linked in - is fake_object.uvhost in step with FakeHostUVE?";
    EXPECT_EQ(compiled.printed, interpreted.printed) << name;
    EXPECT_EQ(compiled.calls, interpreted.calls) << name;
    EXPECT_EQ(compiled.velocity, interpreted.velocity) << name;
    EXPECT_EQ(compiled.health, interpreted.health) << name;
    EXPECT_EQ(compiled.results, interpreted.results) << name;
}

[[nodiscard]] std::string ResultTextUVE(const std::optional<ValueUVE>& value, const ScriptInstanceUVE& instance) {
    return value.has_value() ? FormatValueUVE(*value) : "error: " + instance.GetLastErrorUVE();
}

TEST(UVScriptNativeUVETest, PlayerControllerMatchesTheInterpreter) {
    ExpectNativeMatchesInterpreterUVE("player.uvs", [](ScriptInstanceUVE& instance, FakeHostUVE& host,
                                                       std::vector<std::string>& results) {
        results.push_back(std::to_string(instance.RaiseEventUVE("ready")));
        const std::array<ValueUVE, 1> dt{0.016};
        for (int frame = 0; frame < 3; ++frame) {
            host.onFloor = frame == 1;
            results.push_back(std::to_string(instance.RaiseEventUVE("tick", dt)));
        }
        results.push_back(FormatValueUVE(*instance.GetFieldUVE("jumps")));
        results.push_back(FormatValueUVE(*instance.GetFieldUVE("jump")));
    });
}

TEST(UVScriptNativeUVETest, ArithmeticControlFlowAndBuiltinsMatchTheInterpreter) {
    ExpectNativeMatchesInterpreterUVE("math.uvs", [](ScriptInstanceUVE& instance, FakeHostUVE&,
                                                     std::vector<std::string>& results) {
        results.push_back(FormatValueUVE(*instance.GetFieldUVE("TURN")));
        results.push_back(FormatValueUVE(*instance.GetFieldUVE("SHORT")));
        results.push_back(FormatValueUVE(*instance.GetFieldUVE("label")));
        for (const std::int64_t n : {0, 1, 10, 20}) {
            const std::array<ValueUVE, 1> arg{n};
            results.push_back(ResultTextUVE(instance.CallUVE("sum_to", arg), instance));
            results.push_back(ResultTextUVE(instance.CallUVE("fib", arg), instance));
            results.push_back(ResultTextUVE(instance.CallUVE("first_over", arg), instance));
        }
        results.push_back(ResultTextUVE(instance.CallUVE("mix"), instance));
        // A value of the wrong type from outside is refused the same way by both.
        const std::array<ValueUVE, 1> wrong{2.5};
        results.push_back(ResultTextUVE(instance.CallUVE("fib", wrong), instance));
        for (const double x : {0.5, 2.0, 5.0, 12.0}) {
            const std::array<ValueUVE, 1> arg{x};
            results.push_back(ResultTextUVE(instance.CallUVE("shapes", arg), instance));
        }
    });
    // And the values are the right ones, not merely the same wrong ones.
    FakeHostUVE host;
    const auto program = CompileOrFailUVE(ReadNativeScriptUVE("math.uvs"), host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.IsNativeUVE());
    const std::array<ValueUVE, 1> ten{std::int64_t{10}};
    EXPECT_EQ(std::get<std::int64_t>(*instance.CallUVE("fib", ten)), 55);
    EXPECT_EQ(std::get<std::string>(*instance.GetFieldUVE("label")), "tab\there \"quoted\" ?? done");
    EXPECT_DOUBLE_EQ(std::get<double>(*instance.GetFieldUVE("TURN")), std::numbers::pi / 2.0);
}

TEST(UVScriptNativeUVETest, WaitResumesWhereItStopped) {
    ExpectNativeMatchesInterpreterUVE("waits.uvs", [](ScriptInstanceUVE& instance, FakeHostUVE&,
                                                      std::vector<std::string>& results) {
        results.push_back(std::to_string(instance.RaiseEventUVE("ready")));
        results.push_back(std::to_string(instance.RaiseEventUVE("ready"))); // two runs wait side by side
        for (const double step : {0.3, 0.2, 0.05, 0.05, 0.1}) {
            instance.AdvanceUVE(step);
            results.push_back(std::to_string(instance.GetWaitingCountUVE()));
        }
        results.push_back(FormatValueUVE(*instance.GetFieldUVE("count")));
    });
}

TEST(UVScriptNativeUVETest, RuntimeErrorsReadTheSame) {
    ExpectNativeMatchesInterpreterUVE("errors.uvs", [](ScriptInstanceUVE& instance, FakeHostUVE&,
                                                       std::vector<std::string>& results) {
        results.push_back(ResultTextUVE(instance.CallUVE("spin"), instance));
        const std::array<ValueUVE, 2> byZero{std::int64_t{1}, std::int64_t{0}};
        results.push_back(ResultTextUVE(instance.CallUVE("divide", byZero), instance));
        const std::array<ValueUVE, 1> start{std::int64_t{0}};
        results.push_back(ResultTextUVE(instance.CallUVE("deep", start), instance));
        results.push_back(std::to_string(instance.RaiseEventUVE("ready")));
    });
}

TEST(UVScriptNativeUVETest, GeneratedCodeNamesItsProgramAndRegistersIt) {
    FakeHostUVE host;
    const auto program = CompileOrFailUVE("var n = 1\n\nfn twice() -> int:\n    return n * 2\n", host);
    ASSERT_NE(program, nullptr);
    const std::string cpp = GenerateUVScriptNativeCppUVE(*program, "twice.uvs");
    char fingerprint[24];
    std::snprintf(fingerprint, sizeof(fingerprint), "0x%016llxULL",
                  static_cast<unsigned long long>(GetProgramFingerprintUVE(*program)));
    EXPECT_NE(cpp.find("Generated from twice.uvs"), std::string::npos);
    EXPECT_NE(cpp.find(std::string{"RegistrationUVE kRegistration{ProgramTableUVE{"} + fingerprint), std::string::npos);
    // The same source compiled twice is the same program; a different host description is not.
    EXPECT_EQ(GetProgramFingerprintUVE(*CompileOrFailUVE("var n = 1\n\nfn twice() -> int:\n    return n * 2\n", host)),
              GetProgramFingerprintUVE(*program));
    EXPECT_NE(GetProgramFingerprintUVE(*CompileOrFailUVE("var n = 2\n\nfn twice() -> int:\n    return n * 2\n", host)),
              GetProgramFingerprintUVE(*program));
}

TEST(UVScriptNativeUVETest, StraightLineFunctionsGetUnboxedCode) {
    FakeHostUVE host;
    const auto math = CompileOrFailUVE(ReadNativeScriptUVE("math.uvs"), host);
    ASSERT_NE(math, nullptr);
    const std::string cpp = GenerateUVScriptNativeCppUVE(*math, "math.uvs");
    // sum_to(n: int) -> int, mix() -> float and shapes(x: float) -> vec3 run on plain C++ values.
    EXPECT_NE(cpp.find("std::int64_t T0(ContextUVE& c, std::int64_t a0)"), std::string::npos);
    EXPECT_NE(cpp.find("double T3(ContextUVE& c)"), std::string::npos);
    EXPECT_NE(cpp.find("Vec3ValueUVE T4(ContextUVE& c, double a0)"), std::string::npos);
    // A handler that waits keeps the resumable form.
    const auto waits = CompileOrFailUVE(ReadNativeScriptUVE("waits.uvs"), host);
    ASSERT_NE(waits, nullptr);
    EXPECT_EQ(GenerateUVScriptNativeCppUVE(*waits, "waits.uvs").find(" T0(ContextUVE& c"), std::string::npos);
}

// Not a pass/fail check on speed (CI machines vary); it prints both times so a change can be seen.
TEST(UVScriptNativeUVETest, ReportsNativeAndInterpretedTimeForFib) {
    FakeHostUVE host;
    const auto program = CompileOrFailUVE(ReadNativeScriptUVE("math.uvs"), host);
    ASSERT_NE(program, nullptr);
    const std::array<ValueUVE, 1> arg{std::int64_t{20}};
    const auto time = [&](const ExecutionUVE execution) {
        ScriptInstanceUVE instance(program, host, execution);
        const auto start = std::chrono::steady_clock::now();
        const std::optional<ValueUVE> result = instance.CallUVE("fib", arg);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        EXPECT_EQ(std::get<std::int64_t>(*result), 6765);
        return std::chrono::duration<double, std::milli>(elapsed).count();
    };
    const double interpreted = time(ExecutionUVE::Interpreted);
    const double native = time(ExecutionUVE::Auto);
    std::printf("fib(20): interpreted %.3f ms, native %.3f ms (%.1fx)\n", interpreted, native,
                native > 0.0 ? interpreted / native : 0.0);
}

TEST(UVScriptNativeUVETest, HostDescriptionsParseAndReportTheirMistakes) {
    std::string error;
    const std::optional<DescribedHostUVE> host = DescribedHostUVE::ParseUVE(ReadNativeScriptUVE("fake_object.uvhost"), error);
    ASSERT_TRUE(host.has_value()) << error;
    EXPECT_EQ(host->DescribePropertyUVE("velocity")->type, TypeUVE::Vec3UVE());
    EXPECT_FALSE(host->DescribePropertyUVE("is_on_floor")->writable);
    EXPECT_EQ(host->DescribeFunctionUVE("input.axis")->params.size(), 2U);
    EXPECT_EQ(host->DescribeEventUVE("body_entered")->front(), TypeUVE::ObjectUVE("Object3D"));
    EXPECT_FALSE(host->DescribePropertyUVE("missing").has_value());
    // A script compiled against the description is the program the FakeHost gives: same fingerprint.
    FakeHostUVE fake;
    EXPECT_EQ(GetProgramFingerprintUVE(*CompileUVScriptSourceUVE(ReadNativeScriptUVE("player.uvs"), *host).program),
              GetProgramFingerprintUVE(*CompileOrFailUVE(ReadNativeScriptUVE("player.uvs"), fake)));

    EXPECT_FALSE(DescribedHostUVE::ParseUVE("property speed 3fast\n", error).has_value());
    EXPECT_EQ(error, "line 1: unknown type '3fast'");
    EXPECT_FALSE(DescribedHostUVE::ParseUVE("# ok\nfunction f(int)\n", error).has_value());
    EXPECT_EQ(error, "line 2: expected 'function <name>(<types>) -> <type>'");
    EXPECT_FALSE(DescribedHostUVE::ParseUVE("signal x\n", error).has_value());
    EXPECT_EQ(error, "line 1: expected 'property', 'function' or 'event'");
}

} // namespace
} // namespace UVE::UVScript::Tests
