// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "blueprint_codegen.h"
#include "blueprint_compiler.h"
#include "blueprint_instance.h"
#include "blueprint_parser.h"

namespace UVE::Blueprint::Tests {
namespace {

/// A host with no properties and three tuple-shaped functions: `triple` and `notuple` lie about
/// what they return, so the run-time unpacking checks have something to catch.
class CollectionHost final : public BlueprintHost {
public:
    std::optional<HostProperty> DescribeProperty(const std::string_view) const override { return std::nullopt; }
    std::optional<HostFunction> DescribeFunction(const std::string_view name) const override {
        if (name == "triple" || name == "notuple") {
            return HostFunction{{}, Type::Tuple({Type::Int(), Type::Str()})};
        }
        return std::nullopt;
    }
    std::optional<std::vector<Type>> DescribeEvent(const std::string_view event) const override {
        if (event == "ready") return std::vector<Type>{};
        return std::nullopt;
    }
    Value GetProperty(const std::string_view) override { return {}; }
    void SetProperty(const std::string_view, const Value&) override {}
    Value CallFunction(const std::string_view name, const std::span<const Value>) override {
        if (name == "triple") {
            return MakeTupleValue({Value{std::int64_t{1}}, Value{std::int64_t{2}}, Value{std::int64_t{3}}});
        }
        return MakeListValue({Value{std::int64_t{1}}});
    }
    void CallMethod(const ObjectRef, const std::string_view, const std::span<const Value>) override {}
    void Print(const std::string_view text) override { printed.emplace_back(text); }

    std::vector<std::string> printed;
};

[[nodiscard]] std::shared_ptr<const Program> CompileOrFail(const std::string& source, const CollectionHost& host) {
    const CompileResult result = CompileBlueprintNotation(source, host);
    for (const Diagnostic& d : result.diagnostics) {
        ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
    }
    return result.program;
}

[[nodiscard]] std::vector<std::string> ErrorsOf(const std::string& source) {
    CollectionHost host;
    std::vector<std::string> messages;
    for (const Diagnostic& d : CompileBlueprintNotation(source, host).diagnostics) {
        messages.push_back(std::to_string(d.at.line) + ": " + d.message);
    }
    return messages;
}

TEST(BlueprintCollectionsTest, ParsesCollectionLiterals) {
    const ParseResult result = ParseBlueprintNotation(R"(var xs = [1, 2, 3]
var trailing = [1, 2,]
var m = {"a": 1, "b": 2}
var t = (1, "a")
var grouped = (1)
var one = (1,)
var empty_list: list[int] = []
var empty_map: map[str, int] = {}
var empty_tuple = ()
var matrix: list[list[int]] = []
var multi = [
    1,
    2,
]
)");
    for (const Diagnostic& d : result.diagnostics) {
        ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
    }
    const File& file = result.file;
    ASSERT_EQ(file.fields.size(), 11U);
    EXPECT_EQ(file.fields[0].initializer->kind, ExprKind::List);
    EXPECT_EQ(file.fields[0].initializer->operands.size(), 3U);
    EXPECT_EQ(file.fields[1].initializer->kind, ExprKind::List);
    EXPECT_EQ(file.fields[1].initializer->operands.size(), 2U);
    EXPECT_EQ(file.fields[2].initializer->kind, ExprKind::Map);
    EXPECT_EQ(file.fields[2].initializer->operands.size(), 4U);
    EXPECT_EQ(file.fields[3].initializer->kind, ExprKind::Tuple);
    EXPECT_EQ(file.fields[3].initializer->operands.size(), 2U);
    // One value in brackets with no comma stays grouped, exactly as before.
    EXPECT_EQ(file.fields[4].initializer->kind, ExprKind::Number);
    EXPECT_EQ(file.fields[5].initializer->kind, ExprKind::Tuple);
    EXPECT_EQ(file.fields[5].initializer->operands.size(), 1U);
    EXPECT_EQ(file.fields[6].initializer->kind, ExprKind::List);
    EXPECT_TRUE(file.fields[6].initializer->operands.empty());
    ASSERT_TRUE(file.fields[6].type.has_value());
    EXPECT_EQ(file.fields[6].type->name, "list");
    ASSERT_EQ(file.fields[6].type->arguments.size(), 1U);
    EXPECT_EQ(file.fields[6].type->arguments[0].name, "int");
    EXPECT_EQ(file.fields[7].initializer->kind, ExprKind::Map);
    EXPECT_TRUE(file.fields[7].initializer->operands.empty());
    ASSERT_TRUE(file.fields[7].type.has_value());
    EXPECT_EQ(file.fields[7].type->name, "map");
    ASSERT_EQ(file.fields[7].type->arguments.size(), 2U);
    EXPECT_EQ(file.fields[8].initializer->kind, ExprKind::Tuple);
    EXPECT_TRUE(file.fields[8].initializer->operands.empty());
    ASSERT_TRUE(file.fields[9].type.has_value());
    EXPECT_EQ(file.fields[9].type->name, "list");
    ASSERT_EQ(file.fields[9].type->arguments.size(), 1U);
    EXPECT_EQ(file.fields[9].type->arguments[0].name, "list");
    EXPECT_EQ(file.fields[10].initializer->kind, ExprKind::List);
    EXPECT_EQ(file.fields[10].initializer->operands.size(), 2U);
}

TEST(BlueprintCollectionsTest, ParsesTupleUnpackingLets) {
    const ParseResult result = ParseBlueprintNotation("on ready:\n    let (a, b) = pair()\n    let x = 1\n");
    for (const Diagnostic& d : result.diagnostics) {
        ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
    }
    ASSERT_EQ(result.file.handlers.size(), 1U);
    const Block& body = result.file.handlers[0].body;
    ASSERT_EQ(body.size(), 2U);
    EXPECT_EQ(body[0]->kind, StmtKind::Let);
    EXPECT_EQ(body[0]->names, (std::vector<std::string>{"a", "b"}));
    EXPECT_TRUE(body[0]->name.empty());
    EXPECT_EQ(body[1]->kind, StmtKind::Let);
    EXPECT_EQ(body[1]->name, "x");
    EXPECT_TRUE(body[1]->names.empty());
    // Unpacked names take their types from the tuple; no annotation fits there.
    EXPECT_FALSE(ParseBlueprintNotation("on ready:\n    let (a, b): (int, str) = pair()\n").diagnostics.empty());
}

TEST(BlueprintCollectionsTest, ParsesElementAssignment) {
    const ParseResult result = ParseBlueprintNotation("on ready:\n    xs[0] = 1\n    m[\"k\"] += 2\n");
    for (const Diagnostic& d : result.diagnostics) {
        ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
    }
    ASSERT_EQ(result.file.handlers.size(), 1U);
    const Block& body = result.file.handlers[0].body;
    ASSERT_EQ(body.size(), 2U);
    EXPECT_EQ(body[0]->kind, StmtKind::Assign);
    EXPECT_EQ(body[0]->op, "=");
    EXPECT_EQ(body[0]->target->kind, ExprKind::Index);
    EXPECT_EQ(body[1]->kind, StmtKind::Assign);
    EXPECT_EQ(body[1]->op, "+=");
    EXPECT_EQ(body[1]->target->kind, ExprKind::Index);
}

TEST(BlueprintCollectionsTest, EmptyLiteralsNeedAType) {
    EXPECT_EQ(ErrorsOf("on ready:\n    let xs = []\n"),
              (std::vector<std::string>{"2: this list is empty, so its item type is unknown - write it where the "
                                        "list is stored, as in 'let xs: list[int] = []'"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let m = {}\n"),
              (std::vector<std::string>{"2: this map is empty, so its types are unknown - write them where the map "
                                        "is stored, as in 'let scores: map[str, int] = {}'"}));
}

TEST(BlueprintCollectionsTest, LiteralItemsMustShareAType) {
    EXPECT_EQ(ErrorsOf("on ready:\n    let xs = [1, \"a\"]\n"),
              (std::vector<std::string>{"2: this list holds int, not str"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let m = {\"a\": 1, \"b\": \"x\"}\n"),
              (std::vector<std::string>{"2: this map holds int, not str"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let m = {\"a\": 1, 2: 3}\n"),
              (std::vector<std::string>{"2: this map's keys are str, not int"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let m = {1.5: 1}\n"),
              (std::vector<std::string>{"2: map keys are int, str or bool, not float"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let xs = [none]\n"),
              (std::vector<std::string>{"2: this list holds none - give it values of one type"}));
    // Whole numbers join floats; the list holds floats.
    EXPECT_TRUE(ErrorsOf("on ready:\n    let xs = [1, 2.5]\n").empty());
}

TEST(BlueprintCollectionsTest, AnnotationsMustNameElementTypes) {
    EXPECT_EQ(ErrorsOf("on ready:\n    let xs: list = [1]\n"),
              (std::vector<std::string>{"2: write the item type, as in 'list[int]'"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let m: map[int] = {}\n"),
              (std::vector<std::string>{"2: write the key and value types, as in 'map[str, int]'"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let t: tuple = (1,)\n"),
              (std::vector<std::string>{"2: write the item types, as in 'tuple[int, str]'"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let m: map[vec3, int] = {}\n"),
              (std::vector<std::string>{"2: map keys are int, str or bool, not vec3"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let x: int[str] = 1\n"),
              (std::vector<std::string>{"2: only list[...], map[...] and tuple[...] take types inside brackets"}));
}

TEST(BlueprintCollectionsTest, CollectionsMustMatchExactly) {
    EXPECT_EQ(ErrorsOf("on ready:\n    let xs: list[str] = [1]\n"),
              (std::vector<std::string>{"2: 'xs' needs list[str] but this is list[int]"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let xs = [1]\n    print(xs == [\"a\"])\n"),
              (std::vector<std::string>{"3: '==' cannot compare list[int] with list[str]"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    print([1] == [1.0])\n"),
              (std::vector<std::string>{"2: '==' cannot compare list[int] with list[float]"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let z = [1] + [\"a\"]\n"),
              (std::vector<std::string>{"2: these lists hold different types (list[int] and list[str])"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let xs = [1]\n    let y = -xs\n"),
              (std::vector<std::string>{"3: '-' does not work on list[int]"}));
}

TEST(BlueprintCollectionsTest, IndexingIsChecked) {
    EXPECT_EQ(ErrorsOf("on ready:\n    let xs = [1]\n    print(xs[1.0])\n"),
              (std::vector<std::string>{"3: a list index is a whole number, not float"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let n = 1\n    print(n[0])\n"),
              (std::vector<std::string>{"3: only a list, map or tuple uses [...], not int"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let t = (1, \"a\")\n    let i = 0\n    print(t[i])\n"),
              (std::vector<std::string>{"4: a tuple index is written out, as in 'pair[0]' - each position has its "
                                        "own type"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let t = (1, \"a\")\n    print(t[5])\n"),
              (std::vector<std::string>{"3: this (int, str) has no index 5"}));
}

TEST(BlueprintCollectionsTest, ElementAssignmentIsChecked) {
    EXPECT_EQ(ErrorsOf("on ready:\n    let t = (1, 2)\n    t[0] = 3\n"),
              (std::vector<std::string>{"3: a tuple cannot change - build a new one instead"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let a = [[1]]\n    a[0][1] = 2\n"),
              (std::vector<std::string>{"3: an element of an element cannot be assigned yet - unpack it into a "
                                        "variable first"}));
    EXPECT_EQ(ErrorsOf("const xs = [1]\n\non ready:\n    xs[0] = 2\n"),
              (std::vector<std::string>{"4: 'xs' is a const and cannot change"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let n = 1\n    n[0] = 2\n"),
              (std::vector<std::string>{"3: only a list or map element can be assigned to"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let xs = [1]\n    xs[0] += 1.5\n"),
              (std::vector<std::string>{"3: '+=' would turn this int into float"}));
}

TEST(BlueprintCollectionsTest, UnpackingIsChecked) {
    EXPECT_EQ(ErrorsOf("on ready:\n    let (a, b, c) = (1, \"x\")\n"),
              (std::vector<std::string>{"2: this (int, str) unpacks into 2, not 3"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let (a, b) = [1, 2]\n"),
              (std::vector<std::string>{"2: only a tuple unpacks into names, not list[int]"}));
}

TEST(BlueprintCollectionsTest, ForWalksRangesListsAndMaps) {
    EXPECT_EQ(ErrorsOf("on ready:\n    for x in 1:\n        pass\n"),
              (std::vector<std::string>{"2: 'for' walks a range, a list or a map's keys - not int"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    for x in (1, 2):\n        pass\n"),
              (std::vector<std::string>{"2: a tuple unpacks with 'let (a, b) = ...' instead of a loop"}));
}

TEST(BlueprintCollectionsTest, BuiltinsAreChecked) {
    EXPECT_EQ(ErrorsOf("on ready:\n    print(push({\"a\": 1}, 2))\n"),
              (std::vector<std::string>{"2: 'push' adds to a list, not map[str, int]"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    print(push([1], \"a\"))\n"),
              (std::vector<std::string>{"2: 'push' needs int but this is str"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    print(keys([1]))\n"),
              (std::vector<std::string>{"2: 'keys' reads a map, not list[int]"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    print(remove((1, 2), 0))\n"),
              (std::vector<std::string>{"2: 'remove' cannot shrink a tuple - build a new one instead"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    print(length(1))\n"),
              (std::vector<std::string>{"2: 'length' measures a vec3, a str or a collection, not int"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    print(contains(1, 2))\n"),
              (std::vector<std::string>{"2: 'contains' looks in a str, list, map or tuple, not int"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    print(contains((1, \"a\"), true))\n"),
              (std::vector<std::string>{"2: this (int, str) never holds bool"}));
    // Numbers compare across int and float at run time, so a float may be looked up in ints.
    EXPECT_TRUE(ErrorsOf("on ready:\n    print(contains((1, 2), 1.0))\n").empty());
    EXPECT_EQ(ErrorsOf("on ready:\n    let xs = [1]\n    print(xs.frobnicate())\n"),
              (std::vector<std::string>{"3: list[int] has no 'frobnicate'"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let xs = [1]\n    print(xs.push())\n"),
              (std::vector<std::string>{"3: 'push' takes 1 value(s) after the list[int], not 0"}));
    EXPECT_EQ(ErrorsOf("on ready:\n    let xs = [1]\n    xs.push(1, 2)\n"),
              (std::vector<std::string>{"3: write 'xs.push(value)' - push adds one value"}));
}

TEST(BlueprintCollectionsTest, ReadsLiteralsAndNestedElements) {
    CollectionHost host;
    const auto program = CompileOrFail(R"(on ready:
    let xs = [10, 20, 30]
    let m = {"a": xs, "b": [1]}
    print(xs[0])
    print(xs[2])
    print(m["a"][1])
    print(m["b"][0])
    print([[1], [2, 3]][1][0])
)",
                                          host);
    ASSERT_NE(program, nullptr);
    BlueprintInstance instance(program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready")) << instance.GetLastError();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"10", "30", "20", "1", "2"}));
}

TEST(BlueprintCollectionsTest, LengthPushKeysContainsRemove) {
    CollectionHost host;
    const auto program = CompileOrFail(R"(on ready:
    let xs = [1, 2]
    print(length(xs))
    print(length("abcd"))
    let ys = push(xs, 3)
    print(ys)
    print(xs)
    let m = {"b": 2, "a": 1}
    print(keys(m))
    print(contains(xs, 2))
    print(contains(xs, 9))
    print(contains(m, "a"))
    print(contains("hello", "ell"))
    print(remove(ys, 0))
    print(remove(m, "b"))
)",
                                          host);
    ASSERT_NE(program, nullptr);
    BlueprintInstance instance(program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready")) << instance.GetLastError();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"2", "4", "[1, 2, 3]", "[1, 2]", "[b, a]", "true", "false",
                                                       "true", "true", "[2, 3]", "{a: 1}"}));
}

TEST(BlueprintCollectionsTest, MethodFormsAndSugarStatement) {
    CollectionHost host;
    const auto program = CompileOrFail(R"(on ready:
    let xs = [1]
    print(xs.length())
    print(xs.contains(1))
    print("abc".length())
    let ys = xs.push(2)
    print(ys)
    xs.push(3)
    print(xs)
    xs.remove(0)
    print(xs)
    let m = {"k": 1}
    print(m.keys())
    print(m.length())
    m.remove("k")
    print(m)
    print(length(m))
)",
                                          host);
    ASSERT_NE(program, nullptr);
    BlueprintInstance instance(program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready")) << instance.GetLastError();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"1", "true", "3", "[1, 2]", "[1, 3]", "[3]", "[k]", "1", "{}",
                                                       "0"}));
}

TEST(BlueprintCollectionsTest, AssignsElements) {
    CollectionHost host;
    const auto program = CompileOrFail(R"(on ready:
    let xs = [1, 2, 3]
    xs[0] = 9
    xs[2] += 7
    print(xs)
    let counts: map[str, int] = {}
    counts["a"] = 1
    counts["a"] += 4
    counts["b"] = 2
    print(counts)
)",
                                          host);
    ASSERT_NE(program, nullptr);
    BlueprintInstance instance(program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready")) << instance.GetLastError();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"[9, 2, 10]", "{a: 5, b: 2}"}));
}

TEST(BlueprintCollectionsTest, CopiesNeverAlias) {
    CollectionHost host;
    const auto program = CompileOrFail(R"(on ready:
    let a = [1, 2]
    let b = a
    b[0] = 9
    print(a)
    print(b)
    let m = {"x": 1}
    let n = m
    n["x"] = 2
    print(m["x"])
)",
                                          host);
    ASSERT_NE(program, nullptr);
    BlueprintInstance instance(program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready")) << instance.GetLastError();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"[1, 2]", "[9, 2]", "1"}));
}

TEST(BlueprintCollectionsTest, ForWalksListsAndMaps) {
    CollectionHost host;
    const auto program = CompileOrFail(R"(on ready:
    let sum = 0
    for n in [1, 2, 3]:
        sum += n
    print(sum)
    let m = {"a": 1, "b": 2}
    let out = ""
    for k in m:
        out = out + k
    print(out)
    for n in [1, 2, 3]:
        if n == 2:
            continue
        if n == 3:
            break
        print(n)
    let walked = 0
    for i in 0..4:
        walked += i
    print(walked)
)",
                                          host);
    ASSERT_NE(program, nullptr);
    BlueprintInstance instance(program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready")) << instance.GetLastError();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"6", "ab", "1", "6"}));
}

TEST(BlueprintCollectionsTest, ForSeesASnapshot) {
    CollectionHost host;
    const auto program = CompileOrFail(R"(on ready:
    let xs = [1, 2, 3]
    for n in xs:
        print(n)
        xs.push(9)
    print(length(xs))
)",
                                          host);
    ASSERT_NE(program, nullptr);
    BlueprintInstance instance(program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready")) << instance.GetLastError();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"1", "2", "3", "6"}));
}

TEST(BlueprintCollectionsTest, UnpacksTuplesFromFunctions) {
    CollectionHost host;
    const auto program = CompileOrFail(R"(fn pair() -> tuple[int, str]:
    return (7, "seven")

on ready:
    let (n, word) = pair()
    print(n)
    print(word)
    let t = pair()
    print(t[0])
    print(t[1])
    print(t)
)",
                                          host);
    ASSERT_NE(program, nullptr);
    BlueprintInstance instance(program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready")) << instance.GetLastError();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"7", "seven", "7", "seven", "(7, seven)"}));
}

TEST(BlueprintCollectionsTest, ConcatenatesAndCompares) {
    CollectionHost host;
    const auto program = CompileOrFail(R"(on ready:
    print([1, 2] + [3])
    print([1, 2] == [1, 2])
    print([1] == [1, 2])
    print(1 == 1.0)
    print({"a": 1, "b": 2} == {"b": 2, "a": 1})
    print((1, "a") == (1, "a"))
    print([[1]] == [[1]])
    print([1] != [2])
)",
                                          host);
    ASSERT_NE(program, nullptr);
    BlueprintInstance instance(program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready")) << instance.GetLastError();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"[1, 2, 3]", "true", "false", "true", "true", "true", "true",
                                                       "true"}));
}

TEST(BlueprintCollectionsTest, FormatsCollectionsInStrings) {
    CollectionHost host;
    const auto program = CompileOrFail(R"(on ready:
    let xs = [1, 2]
    print("xs={xs} len={length(xs)}")
    print(str({"a": [1]}))
    print((1,))
    print(())
)",
                                          host);
    ASSERT_NE(program, nullptr);
    BlueprintInstance instance(program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready")) << instance.GetLastError();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"xs=[1, 2] len=2", "{a: [1]}", "(1,)", "()"}));
}

TEST(BlueprintCollectionsTest, ReportsIndexErrorsAtRunTime) {
    CollectionHost host;
    const auto outOfRange = CompileOrFail(R"(on ready:
    let xs = [1, 2]
    let i = 5
    print(xs[i])
)",
                                             host);
    ASSERT_NE(outOfRange, nullptr);
    BlueprintInstance first(outOfRange, host);
    EXPECT_FALSE(first.RaiseEvent("ready"));
    EXPECT_EQ(first.GetLastError(), "line 4: index 5 is out of range for a list of 2");

    const auto missing = CompileOrFail(R"(on ready:
    let m = {"a": 1}
    let k = "b"
    print(m[k])
)",
                                          host);
    ASSERT_NE(missing, nullptr);
    BlueprintInstance second(missing, host);
    EXPECT_FALSE(second.RaiseEvent("ready"));
    EXPECT_EQ(second.GetLastError(), "line 4: the map has no key 'b'");
}

TEST(BlueprintCollectionsTest, ReportsUnpackErrorsAtRunTime) {
    CollectionHost host;
    const auto wrongSize = CompileOrFail("on ready:\n    let (a, b) = triple()\n    print(a)\n", host);
    ASSERT_NE(wrongSize, nullptr);
    BlueprintInstance first(wrongSize, host);
    EXPECT_FALSE(first.RaiseEvent("ready"));
    EXPECT_EQ(first.GetLastError(), "line 2: this tuple holds 3, not 2");

    const auto notTuple = CompileOrFail("on ready:\n    let (a, b) = notuple()\n    print(a)\n", host);
    ASSERT_NE(notTuple, nullptr);
    BlueprintInstance second(notTuple, host);
    EXPECT_FALSE(second.RaiseEvent("ready"));
    EXPECT_EQ(second.GetLastError(), "line 2: only a tuple unpacks into names, not a list");
}

TEST(BlueprintCollectionsTest, InferredTypesUnifyNumbers) {
    CollectionHost host;
    const auto program = CompileOrFail(R"(on ready:
    let xs = [1, 2.5]
    print(xs)
    let m = {"a": 1, "b": 2.5}
    print(m)
    print(xs[0])
)",
                                          host);
    ASSERT_NE(program, nullptr);
    BlueprintInstance instance(program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready")) << instance.GetLastError();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"[1.0, 2.5]", "{a: 1.0, b: 2.5}", "1.0"}));
}

TEST(BlueprintCollectionsTest, AnnotatedEmptiesStartEmpty) {
    CollectionHost host;
    const auto program = CompileOrFail(R"(var empty_field: list[int]

fn empty() -> list[int]:
    return []

fn tally(xs: list[str]) -> int:
    return length(xs)

on ready:
    let xs: list[int] = []
    print(length(xs))
    print(length(empty_field))
    print(length(empty()))
    print(tally([]))
    let m: map[str, int] = {}
    print(length(m))
    xs.push(1)
    print(xs)
)",
                                          host);
    ASSERT_NE(program, nullptr);
    BlueprintInstance instance(program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready")) << instance.GetLastError();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"0", "0", "0", "0", "0", "[1]"}));
}

TEST(BlueprintCollectionsTest, FingerprintsDistinguishElementTypes) {
    CollectionHost host;
    const auto ints = CompileOrFail("var xs: list[int] = []\n", host);
    const auto strs = CompileOrFail("var xs: list[str] = []\n", host);
    const auto intsAgain = CompileOrFail("var xs: list[int] = []\n", host);
    ASSERT_NE(ints, nullptr);
    ASSERT_NE(strs, nullptr);
    ASSERT_NE(intsAgain, nullptr);
    EXPECT_NE(GetProgramFingerprint(*ints), GetProgramFingerprint(*strs));
    EXPECT_EQ(GetProgramFingerprint(*ints), GetProgramFingerprint(*intsAgain));
}

TEST(BlueprintCollectionsTest, NativeCodegenCoversCollectionsAndStrLength) {
    CollectionHost host;
    const auto strLength = CompileOrFail("fn f() -> int:\n    return length(\"abc\")\n", host);
    ASSERT_NE(strLength, nullptr);
    const std::string typed = GenerateBlueprintNativeCpp(*strLength, "strlen.bpn");
    // A str's length stays in typed code, counting characters rather than a magnitude.
    EXPECT_NE(typed.find("std::int64_t T0(Context& c)"), std::string::npos);
    EXPECT_NE(typed.find(".size())"), std::string::npos);

    const auto lists = CompileOrFail("fn f() -> int:\n    let xs = [1]\n    return xs[0]\n", host);
    ASSERT_NE(lists, nullptr);
    const std::string boxed = GenerateBlueprintNativeCpp(*lists, "lists.bpn");
    EXPECT_NE(boxed.find("BuildList"), std::string::npos);
    EXPECT_NE(boxed.find("GetIndex"), std::string::npos);
}

} // namespace
} // namespace UVE::Blueprint::Tests
