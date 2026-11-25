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

#include "blueprint_codegen.h"
#include "blueprint_compiler.h"
#include "blueprint_host_description.h"
#include "blueprint_instance.h"

namespace UVE::Blueprint::Tests {
namespace {

/// A Character3D-like object: a velocity, a read-only floor flag, input, and three events.
class FakeHost final : public UVScriptHost {
public:
    std::optional<HostProperty> DescribeProperty(const std::string_view name) const override {
        if (name == "velocity") return HostProperty{Type::Vec3(), true};
        // Mirrors BlueprintObjectHost: "grounded" is the name, "is_on_floor" still reads.
        if (name == "grounded" || name == "is_on_floor") return HostProperty{Type::Bool(), false};
        if (name == "health") return HostProperty{Type::Int(), true};
        return std::nullopt;
    }
    std::optional<HostFunction> DescribeFunction(const std::string_view name) const override {
        if (name == "input.axis") return HostFunction{{Type::Str(), Type::Str()}, Type::Float()};
        if (name == "node") return HostFunction{{Type::Str()}, Type::Object("Object3D")};
        return std::nullopt;
    }
    std::optional<std::vector<Type>> DescribeEvent(const std::string_view event) const override {
        if (event == "ready") return std::vector<Type>{};
        if (event == "tick") return std::vector<Type>{Type::Float()};
        if (event == "body_entered") return std::vector<Type>{Type::Object("Object3D")};
        return std::nullopt;
    }
    Value GetProperty(const std::string_view name) override {
        if (name == "velocity") return velocity;
        if (name == "grounded" || name == "is_on_floor") return onFloor;
        return health;
    }
    void SetProperty(const std::string_view name, const Value& value) override {
        if (name == "velocity") velocity = std::get<Vec3Value>(value);
        else health = std::get<std::int64_t>(value);
    }
    Value CallFunction(const std::string_view name, std::span<const Value> args) override {
        if (name == "node") {
            return std::get<std::string>(args[0]) == "nobody" ? Value{ObjectRef{}}
                                                              : Value{ObjectRef{7U}};
        }
        calls.push_back(std::string{name} + "(" + std::get<std::string>(args[0]) + "," + std::get<std::string>(args[1]) + ")");
        return axis;
    }
    void CallMethod(const ObjectRef target, const std::string_view method,
                       const std::span<const Value> args) override {
        std::string call = std::to_string(target.id) + "." + std::string{method} + "(";
        for (std::size_t i = 0U; i < args.size(); ++i) {
            call += (i == 0U ? "" : ",") + FormatValue(args[i]);
        }
        calls.push_back(call + ")");
    }
    void Print(const std::string_view text) override { printed.emplace_back(text); }

    Vec3Value velocity{};
    bool onFloor = true;
    std::int64_t health = 10;
    double axis = 1.0;
    std::vector<std::string> printed;
    std::vector<std::string> calls;
};

[[nodiscard]] std::shared_ptr<const Program> CompileOrFail(const std::string& source, const FakeHost& host) {
    const CompileResult result = CompileUVScriptSource(source, host);
    for (const Diagnostic& d : result.diagnostics) {
        ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
    }
    return result.program;
}

[[nodiscard]] std::vector<std::string> ErrorsOf(const std::string& source) {
    FakeHost host;
    std::vector<std::string> messages;
    for (const Diagnostic& d : CompileUVScriptSource(source, host).diagnostics) {
        messages.push_back(std::to_string(d.at.line) + ": " + d.message);
    }
    return messages;
}

TEST(UVScriptVmTest, RunsAPlayerController) {
    FakeHost host;
    const auto program = CompileOrFail(R"(entity Player : Character3D
export speed: float = 6.0
export jump = 2.0 m
var jumps = 0

on ready:
    print("{jumps} jumps, speed {speed}")

on tick(dt):
    velocity.x = input.axis("left", "right") * speed
    if grounded:
        velocity.y = sqrt(2.0 * 9.8 * jump)
        jumps += 1
)", host);
    ASSERT_NE(program, nullptr);
    ScriptInstance instance(program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready"));
    ASSERT_EQ(host.printed, (std::vector<std::string>{"0 jumps, speed 6.0"}));

    const std::array<Value, 1> dt{0.016};
    ASSERT_TRUE(instance.RaiseEvent("tick", dt)) << instance.GetLastError();
    EXPECT_DOUBLE_EQ(host.velocity.x, 6.0);
    EXPECT_NEAR(host.velocity.y, std::sqrt(2.0 * 9.8 * 2.0), 1e-9);
    EXPECT_EQ(std::get<std::int64_t>(*instance.GetField("jumps")), 1);
    EXPECT_EQ(host.calls, (std::vector<std::string>{"input.axis(left,right)"}));
    EXPECT_FALSE(instance.RaiseEvent("body_entered")); // wrong argument count
}

TEST(UVScriptVmTest, EvaluatesArithmeticUnitsAndControlFlow) {
    FakeHost host;
    const auto program = CompileOrFail(R"(
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
    ScriptInstance instance(program, host);
    EXPECT_NEAR(std::get<double>(*instance.GetField("TURN")), std::numbers::pi / 2.0, 1e-12);
    EXPECT_DOUBLE_EQ(std::get<double>(*instance.GetField("SHORT")), 0.25);
    const std::array<Value, 1> ten{std::int64_t{10}};
    EXPECT_EQ(std::get<std::int64_t>(*instance.Call("sum_to", ten)), 25); // 1+3+5+7+9
    EXPECT_EQ(std::get<std::int64_t>(*instance.Call("fib", ten)), 55);
    const std::array<Value, 1> fifty{std::int64_t{50}};
    EXPECT_EQ(std::get<std::int64_t>(*instance.Call("first_over", fifty)), 8);
    EXPECT_DOUBLE_EQ(std::get<double>(*instance.Call("mix")), 3.5 + 2.5 + 1.0);
}

TEST(UVScriptVmTest, WaitPausesAHandlerForExactlyItsTime) {
    FakeHost host;
    const auto program = CompileOrFail(R"(
on ready:
    print("a")
    wait 0.5 s
    print("b")
    wait 100 ms
    print("c")
)", host);
    ASSERT_NE(program, nullptr);
    ScriptInstance instance(program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready"));
    EXPECT_EQ(host.printed, (std::vector<std::string>{"a"}));
    EXPECT_EQ(instance.GetWaitingCount(), 1U);
    instance.Advance(0.3);
    EXPECT_EQ(host.printed.size(), 1U);
    instance.Advance(0.2);
    EXPECT_EQ(host.printed, (std::vector<std::string>{"a", "b"}));
    instance.Advance(0.1);
    EXPECT_EQ(host.printed, (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_EQ(instance.GetWaitingCount(), 0U);
}

TEST(UVScriptVmTest, ReportsTypeErrorsBeforeRunning) {
    EXPECT_EQ(ErrorsOf("on ready:\n    let a = 1\n    a = \"text\"\n"),
              (std::vector<std::string>{"3: this assignment needs int but this is str"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    print(nope)\n"), (std::vector<std::string>{"2: nothing is called 'nope' here"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    grounded = true\n"),
              (std::vector<std::string>{"2: 'grounded' can be read but not changed"}));
    // The name this property had before still reads, so a script written against it keeps working -
    // and it is still read-only, because an alias is a second spelling, not a second property.
    EXPECT_EQ(ErrorsOf("on ready:\n    is_on_floor = true\n"),
              (std::vector<std::string>{"2: 'is_on_floor' can be read but not changed"}));
    EXPECT_EQ(ErrorsOf("const A = 1\non ready:\n    A = 2\n"),
              (std::vector<std::string>{"3: 'A' is a const and cannot change"}));
    EXPECT_EQ(ErrorsOf("on jump:\n    pass\n"), (std::vector<std::string>{"1: this object has no event 'jump'"}));
    EXPECT_EQ(ErrorsOf("fn f() -> int:\n    pass\n"),
              (std::vector<std::string>{"1: 'f' can reach its end without returning a int"}));
    EXPECT_EQ(ErrorsOf("fn f():\n    wait 1 s\n"), (std::vector<std::string>{"2: 'wait' only works inside an 'on' block"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    break\n"), (std::vector<std::string>{"2: 'break' only works inside a loop"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    if 1:\n        pass\n"),
              (std::vector<std::string>{"2: a condition must be true or false, not int"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    1 + 2\n"),
              (std::vector<std::string>{"2: this line works out a value and then drops it - did you mean to assign it?"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let v = velocity + 1\n"),
              (std::vector<std::string>{"2: '+' does not work on vec3 and int"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let x = 1\n    x /= 2\n"),
              (std::vector<std::string>{"3: '/=' would turn this int into float"}));
}

TEST(UVScriptVmTest, StopsRunawayLoopsAndRuntimeErrors) {
    FakeHost host;
    const auto program = CompileOrFail(R"(
fn spin():
    while true:
        pass

fn divide(a: int, b: int) -> int:
    return a % b

on ready:
    health = 3
)", host);
    ASSERT_NE(program, nullptr);
    ScriptInstance instance(program, host);
    EXPECT_FALSE(instance.Call("spin").has_value());
    EXPECT_NE(instance.GetLastError().find("ran too long"), std::string::npos);
    const std::array<Value, 2> byZero{std::int64_t{1}, std::int64_t{0}};
    EXPECT_FALSE(instance.Call("divide", byZero).has_value());
    EXPECT_EQ(instance.GetLastError(), "line 7: '%' by zero");
    ASSERT_TRUE(instance.RaiseEvent("ready"));
    EXPECT_EQ(host.health, 3);
}

TEST(UVScriptVmTest, FieldsAreSetFromTheInspectorWithTheirOwnType) {
    FakeHost host;
    const auto program = CompileOrFail("export speed = 4.0\nconst LIMIT = 3\n", host);
    ASSERT_NE(program, nullptr);
    const std::vector<FieldInfo> fields = GetProgramFields(*program);
    ASSERT_EQ(fields.size(), 2U);
    EXPECT_EQ(fields[0].type, Type::Float());
    EXPECT_EQ(fields[1].kind, FieldKind::Const);
    ScriptInstance instance(program, host);
    EXPECT_TRUE(instance.SetField("speed", 9.5));
    EXPECT_DOUBLE_EQ(std::get<double>(*instance.GetField("speed")), 9.5);
    EXPECT_FALSE(instance.SetField("speed", std::string{"fast"}));
    EXPECT_FALSE(instance.SetField("LIMIT", std::int64_t{4}));
    EXPECT_FALSE(instance.SetField("missing", 1.0));
}

TEST(UVScriptVmTest, ValueTextReadsBackWhatFormatWrites) {
    const std::array<std::pair<Value, Type>, 5> values{{
        {Value{true}, Type::Bool()},
        {Value{std::int64_t{-12}}, Type::Int()},
        {Value{6.0}, Type::Float()},
        {Value{std::string{"hero one"}}, Type::Str()},
        {Value{Vec3Value{0.5, -1.0, 2.0}}, Type::Vec3()},
    }};
    for (const auto& [value, type] : values) {
        EXPECT_EQ(ParseValueText(FormatValue(value), type), value) << FormatValue(value);
    }
    EXPECT_EQ(ParseValueText(" 1, 2 ,3 ", Type::Vec3()), Value{(Vec3Value{1.0, 2.0, 3.0})});
    EXPECT_EQ(ParseValueText("4", Type::Float()), Value{4.0});
    EXPECT_FALSE(ParseValueText("4.5", Type::Int()).has_value());
    EXPECT_FALSE(ParseValueText("yes", Type::Bool()).has_value());
    EXPECT_FALSE(ParseValueText("1, 2", Type::Vec3()).has_value());
    EXPECT_FALSE(ParseValueText("nan", Type::Float()).has_value());
    EXPECT_FALSE(ParseValueText("", Type::Int()).has_value());
}

TEST(UVScriptVmTest, CallsMethodsOnOtherNodesFireAndForget) {
    FakeHost host;
    const auto program = CompileOrFail(R"(
on body_entered(other):
    other.take_damage(30)
    other.hide()
    if other == none:
        print("gone")
    else:
        print("here")
    let ghost = node("nobody")
    if ghost == none:
        print("no ghost")
    if node("friend").greet("hi") == none:
        print("fire and forget")
)", host);
    ASSERT_NE(program, nullptr);
    ScriptInstance instance(program, host);
    const std::array<Value, 1> args{Value{ObjectRef{3U}}};
    ASSERT_TRUE(instance.RaiseEvent("body_entered", args)) << instance.GetLastError();
    EXPECT_EQ(host.calls, (std::vector<std::string>{"3.take_damage(30)", "3.hide()", "7.greet(hi)"}));
    EXPECT_EQ(host.printed, (std::vector<std::string>{"here", "no ghost", "fire and forget"}));
}

TEST(UVScriptVmTest, ReportsBadMethodCallsBeforeRunning) {
    EXPECT_EQ(ErrorsOf("on tick(dt):\n    velocity.hide()\n"),
              (std::vector<std::string>{"2: there is no function 'velocity.hide'"}));
    EXPECT_EQ(ErrorsOf("on tick(dt):\n    dt.hide()\n"),
              (std::vector<std::string>{"2: there is no function 'dt.hide'"}));
    EXPECT_EQ(ErrorsOf("on tick(dt):\n    other.hide()\n"),
              (std::vector<std::string>{"2: there is no function 'other.hide'"}));
    EXPECT_EQ(ErrorsOf("on tick(dt):\n    print(node(\"a\") < node(\"b\"))\n"),
              (std::vector<std::string>{"2: '<' cannot compare Object3D with Object3D"}));
}

// ---- Native code: the same scripts compiled to C++ by uvsc at build time (Test/CMakeLists.txt).

[[nodiscard]] std::string ReadNativeScript(const std::string& name) {
    std::ifstream file(std::string{UVE_UVSCRIPT_NATIVE_DIR} + "/" + name, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

/// Everything a run leaves behind, so an interpreted and a native run can be compared whole.
struct Transcript final {
    std::vector<std::string> printed;
    std::vector<std::string> calls;
    Vec3Value velocity{};
    std::int64_t health = 0;
    std::vector<std::string> results;

    bool operator==(const Transcript&) const = default;
};

/// Compiles `name` against FakeHost and hands `run` an instance in the given mode.
template <typename RunFn>
[[nodiscard]] Transcript RunScript(const std::string& name, const Execution execution, bool& native,
                                         const RunFn& run) {
    FakeHost host;
    host.onFloor = false;
    const auto program = CompileOrFail(ReadNativeScript(name), host);
    Transcript transcript;
    if (program == nullptr) {
        return transcript;
    }
    ScriptInstance instance(program, host, execution);
    native = instance.IsNative();
    run(instance, host, transcript.results);
    transcript.printed = host.printed;
    transcript.calls = host.calls;
    transcript.velocity = host.velocity;
    transcript.health = host.health;
    return transcript;
}

/// Runs `name` both ways and checks the native run really was native and matched the interpreter.
template <typename RunFn>
void ExpectNativeMatchesInterpreter(const std::string& name, const RunFn& run) {
    bool interpretedNative = true;
    bool native = false;
    const Transcript interpreted = RunScript(name, Execution::Interpreted, interpretedNative, run);
    const Transcript compiled = RunScript(name, Execution::Auto, native, run);
    EXPECT_FALSE(interpretedNative) << name;
    EXPECT_TRUE(native) << name << " has no native code linked in - is fake_object.uvhost in step with FakeHost?";
    EXPECT_EQ(compiled.printed, interpreted.printed) << name;
    EXPECT_EQ(compiled.calls, interpreted.calls) << name;
    EXPECT_EQ(compiled.velocity, interpreted.velocity) << name;
    EXPECT_EQ(compiled.health, interpreted.health) << name;
    EXPECT_EQ(compiled.results, interpreted.results) << name;
}

[[nodiscard]] std::string ResultText(const std::optional<Value>& value, const ScriptInstance& instance) {
    return value.has_value() ? FormatValue(*value) : "error: " + instance.GetLastError();
}

TEST(UVScriptNativeTest, PlayerControllerMatchesTheInterpreter) {
    ExpectNativeMatchesInterpreter("player.uvs", [](ScriptInstance& instance, FakeHost& host,
                                                       std::vector<std::string>& results) {
        results.push_back(std::to_string(instance.RaiseEvent("ready")));
        const std::array<Value, 1> dt{0.016};
        for (int frame = 0; frame < 3; ++frame) {
            host.onFloor = frame == 1;
            results.push_back(std::to_string(instance.RaiseEvent("tick", dt)));
        }
        results.push_back(FormatValue(*instance.GetField("jumps")));
        results.push_back(FormatValue(*instance.GetField("jump")));
    });
}

TEST(UVScriptNativeTest, MethodCallsMatchTheInterpreter) {
    ExpectNativeMatchesInterpreter(
        "methods.uvs", [](ScriptInstance& instance, FakeHost&, std::vector<std::string>& results) {
            const std::array<Value, 1> nobody{Value{ObjectRef{}}};
            results.push_back(std::to_string(instance.RaiseEvent("body_entered", nobody)));
            const std::array<Value, 1> friend_{Value{ObjectRef{5U}}};
            results.push_back(std::to_string(instance.RaiseEvent("body_entered", friend_)));
        });
}

TEST(UVScriptNativeTest, CollectionsMatchTheInterpreter) {
    ExpectNativeMatchesInterpreter("collections.uvs", [](ScriptInstance& instance, FakeHost&,
                                                             std::vector<std::string>& results) {
        results.push_back(ResultText(instance.Call("describe"), instance));
        results.push_back(ResultText(instance.Call("total"), instance));
        const std::array<Value, 1> words{MakeListValue({Value{std::string{"a"}}, Value{std::string{"b"}},
                                                              Value{std::string{"a"}}})};
        results.push_back(ResultText(instance.Call("tally", words), instance));
        results.push_back(ResultText(instance.Call("first_pair"), instance));
        results.push_back(ResultText(instance.Call("unpacked"), instance));
        results.push_back(ResultText(instance.Call("key_list"), instance));
        results.push_back(ResultText(instance.Call("dropped"), instance));
        results.push_back(ResultText(instance.Call("joined"), instance));
        results.push_back(ResultText(instance.Call("has_torch"), instance));
        results.push_back(ResultText(instance.Call("count_words"), instance));
        results.push_back(ResultText(instance.Call("count_words"), instance));
        results.push_back(std::to_string(instance.RaiseEvent("ready")));
    });
    // And the values are the right ones, not merely the same wrong ones.
    FakeHost host;
    const auto program = CompileOrFail(ReadNativeScript("collections.uvs"), host);
    ASSERT_NE(program, nullptr);
    ScriptInstance instance(program, host);
    ASSERT_TRUE(instance.IsNative());
    EXPECT_EQ(FormatValue(*instance.Call("total")), "6");
    EXPECT_EQ(FormatValue(*instance.Call("unpacked")), "one=1");
    EXPECT_EQ(FormatValue(*instance.Call("joined")), "[1, 2, 3]");
    EXPECT_EQ(FormatValue(*instance.Call("describe")), "3 things: rope, map");
}

TEST(UVScriptNativeTest, ArithmeticControlFlowAndBuiltinsMatchTheInterpreter) {
    ExpectNativeMatchesInterpreter("math.uvs", [](ScriptInstance& instance, FakeHost&,
                                                     std::vector<std::string>& results) {
        results.push_back(FormatValue(*instance.GetField("TURN")));
        results.push_back(FormatValue(*instance.GetField("SHORT")));
        results.push_back(FormatValue(*instance.GetField("label")));
        for (const std::int64_t n : {0, 1, 10, 20}) {
            const std::array<Value, 1> arg{n};
            results.push_back(ResultText(instance.Call("sum_to", arg), instance));
            results.push_back(ResultText(instance.Call("fib", arg), instance));
            results.push_back(ResultText(instance.Call("first_over", arg), instance));
        }
        results.push_back(ResultText(instance.Call("mix"), instance));
        // A value of the wrong type from outside is refused the same way by both.
        const std::array<Value, 1> wrong{2.5};
        results.push_back(ResultText(instance.Call("fib", wrong), instance));
        for (const double x : {0.5, 2.0, 5.0, 12.0}) {
            const std::array<Value, 1> arg{x};
            results.push_back(ResultText(instance.Call("shapes", arg), instance));
        }
    });
    // And the values are the right ones, not merely the same wrong ones.
    FakeHost host;
    const auto program = CompileOrFail(ReadNativeScript("math.uvs"), host);
    ASSERT_NE(program, nullptr);
    ScriptInstance instance(program, host);
    ASSERT_TRUE(instance.IsNative());
    const std::array<Value, 1> ten{std::int64_t{10}};
    EXPECT_EQ(std::get<std::int64_t>(*instance.Call("fib", ten)), 55);
    EXPECT_EQ(std::get<std::string>(*instance.GetField("label")), "tab\there \"quoted\" ?? done");
    EXPECT_DOUBLE_EQ(std::get<double>(*instance.GetField("TURN")), std::numbers::pi / 2.0);
}

TEST(UVScriptNativeTest, WaitResumesWhereItStopped) {
    ExpectNativeMatchesInterpreter("waits.uvs", [](ScriptInstance& instance, FakeHost&,
                                                      std::vector<std::string>& results) {
        results.push_back(std::to_string(instance.RaiseEvent("ready")));
        results.push_back(std::to_string(instance.RaiseEvent("ready"))); // two runs wait side by side
        for (const double step : {0.3, 0.2, 0.05, 0.05, 0.1}) {
            instance.Advance(step);
            results.push_back(std::to_string(instance.GetWaitingCount()));
        }
        results.push_back(FormatValue(*instance.GetField("count")));
    });
}

TEST(UVScriptNativeTest, RuntimeErrorsReadTheSame) {
    ExpectNativeMatchesInterpreter("errors.uvs", [](ScriptInstance& instance, FakeHost&,
                                                       std::vector<std::string>& results) {
        results.push_back(ResultText(instance.Call("spin"), instance));
        const std::array<Value, 2> byZero{std::int64_t{1}, std::int64_t{0}};
        results.push_back(ResultText(instance.Call("divide", byZero), instance));
        const std::array<Value, 1> start{std::int64_t{0}};
        results.push_back(ResultText(instance.Call("deep", start), instance));
        results.push_back(std::to_string(instance.RaiseEvent("ready")));
    });
}

TEST(UVScriptNativeTest, GeneratedCodeNamesItsProgramAndRegistersIt) {
    FakeHost host;
    const auto program = CompileOrFail("var n = 1\n\nfn twice() -> int:\n    return n * 2\n", host);
    ASSERT_NE(program, nullptr);
    const std::string cpp = GenerateUVScriptNativeCpp(*program, "twice.uvs");
    char fingerprint[24];
    std::snprintf(fingerprint, sizeof(fingerprint), "0x%016llxULL",
                  static_cast<unsigned long long>(GetProgramFingerprint(*program)));
    EXPECT_NE(cpp.find("Generated from twice.uvs"), std::string::npos);
    EXPECT_NE(cpp.find(std::string{"Registration kRegistration{ProgramTable{"} + fingerprint), std::string::npos);
    // The same source compiled twice is the same program; a different host description is not.
    EXPECT_EQ(GetProgramFingerprint(*CompileOrFail("var n = 1\n\nfn twice() -> int:\n    return n * 2\n", host)),
              GetProgramFingerprint(*program));
    EXPECT_NE(GetProgramFingerprint(*CompileOrFail("var n = 2\n\nfn twice() -> int:\n    return n * 2\n", host)),
              GetProgramFingerprint(*program));
}

TEST(UVScriptNativeTest, StraightLineFunctionsGetUnboxedCode) {
    FakeHost host;
    const auto math = CompileOrFail(ReadNativeScript("math.uvs"), host);
    ASSERT_NE(math, nullptr);
    const std::string cpp = GenerateUVScriptNativeCpp(*math, "math.uvs");
    // sum_to(n: int) -> int, mix() -> float and shapes(x: float) -> vec3 run on plain C++ values.
    EXPECT_NE(cpp.find("std::int64_t T0(Context& c, std::int64_t a0)"), std::string::npos);
    EXPECT_NE(cpp.find("double T3(Context& c)"), std::string::npos);
    EXPECT_NE(cpp.find("Vec3Value T4(Context& c, double a0)"), std::string::npos);
    // A handler that waits keeps the resumable form.
    const auto waits = CompileOrFail(ReadNativeScript("waits.uvs"), host);
    ASSERT_NE(waits, nullptr);
    EXPECT_EQ(GenerateUVScriptNativeCpp(*waits, "waits.uvs").find(" T0(Context& c"), std::string::npos);
}

// Not a pass/fail check on speed (CI machines vary); it prints both times so a change can be seen.
TEST(UVScriptNativeTest, ReportsNativeAndInterpretedTimeForFib) {
    FakeHost host;
    const auto program = CompileOrFail(ReadNativeScript("math.uvs"), host);
    ASSERT_NE(program, nullptr);
    const std::array<Value, 1> arg{std::int64_t{20}};
    const auto time = [&](const Execution execution) {
        ScriptInstance instance(program, host, execution);
        const auto start = std::chrono::steady_clock::now();
        const std::optional<Value> result = instance.Call("fib", arg);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        EXPECT_EQ(std::get<std::int64_t>(*result), 6765);
        return std::chrono::duration<double, std::milli>(elapsed).count();
    };
    const double interpreted = time(Execution::Interpreted);
    const double native = time(Execution::Auto);
    std::printf("fib(20): interpreted %.3f ms, native %.3f ms (%.1fx)\n", interpreted, native,
                native > 0.0 ? interpreted / native : 0.0);
}

TEST(UVScriptNativeTest, HostDescriptionsParseAndReportTheirMistakes) {
    std::string error;
    const std::optional<DescribedHost> host = DescribedHost::Parse(ReadNativeScript("fake_object.uvhost"), error);
    ASSERT_TRUE(host.has_value()) << error;
    EXPECT_EQ(host->DescribeProperty("velocity")->type, Type::Vec3());
    EXPECT_FALSE(host->DescribeProperty("grounded")->writable);
    // A described host answers only to what its file declares - the retired-name alias lives in the
    // C++ hosts, which is where a Character3D script's property actually comes from.
    EXPECT_FALSE(host->DescribeProperty("is_on_floor").has_value());
    EXPECT_EQ(host->DescribeFunction("input.axis")->params.size(), 2U);
    EXPECT_EQ(host->DescribeEvent("body_entered")->front(), Type::Object("Object3D"));
    EXPECT_FALSE(host->DescribeProperty("missing").has_value());
    // A script compiled against the description is the program the FakeHost gives: same fingerprint.
    FakeHost fake;
    EXPECT_EQ(GetProgramFingerprint(*CompileUVScriptSource(ReadNativeScript("player.uvs"), *host).program),
              GetProgramFingerprint(*CompileOrFail(ReadNativeScript("player.uvs"), fake)));

    EXPECT_FALSE(DescribedHost::Parse("property speed 3fast\n", error).has_value());
    EXPECT_EQ(error, "line 1: unknown type '3fast'");
    EXPECT_FALSE(DescribedHost::Parse("# ok\nfunction f(int)\n", error).has_value());
    EXPECT_EQ(error, "line 2: expected 'function <name>(<types>) -> <type>'");
    EXPECT_FALSE(DescribedHost::Parse("signal x\n", error).has_value());
    EXPECT_EQ(error, "line 1: expected 'property', 'function' or 'event'");
}

} // namespace
} // namespace UVE::Blueprint::Tests
