// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/uvscript/uvscript_codegen_uve.h"
#include "uve/uvscript/uvscript_compiler_uve.h"
#include "uve/uvscript/uvscript_instance_uve.h"
#include "uve/uvscript/uvscript_parser_uve.h"

namespace UVE::UVScript::Tests {
namespace {

/// A host with no properties and three tuple-shaped functions: `triple` and `notuple` lie about
/// what they return, so the run-time unpacking checks have something to catch.
class CollectionHostUVE final : public UVScriptHostUVE {
public:
    std::optional<HostPropertyUVE> DescribePropertyUVE(const std::string_view) const override { return std::nullopt; }
    std::optional<HostFunctionUVE> DescribeFunctionUVE(const std::string_view name) const override {
        if (name == "triple" || name == "notuple") {
            return HostFunctionUVE{{}, TypeUVE::TupleUVE({TypeUVE::IntUVE(), TypeUVE::StrUVE()})};
        }
        return std::nullopt;
    }
    std::optional<std::vector<TypeUVE>> DescribeEventUVE(const std::string_view event) const override {
        if (event == "ready") return std::vector<TypeUVE>{};
        return std::nullopt;
    }
    ValueUVE GetPropertyUVE(const std::string_view) override { return {}; }
    void SetPropertyUVE(const std::string_view, const ValueUVE&) override {}
    ValueUVE CallFunctionUVE(const std::string_view name, const std::span<const ValueUVE>) override {
        if (name == "triple") {
            return MakeTupleValueUVE({ValueUVE{std::int64_t{1}}, ValueUVE{std::int64_t{2}}, ValueUVE{std::int64_t{3}}});
        }
        return MakeListValueUVE({ValueUVE{std::int64_t{1}}});
    }
    void CallMethodUVE(const ObjectRefUVE, const std::string_view, const std::span<const ValueUVE>) override {}
    void PrintUVE(const std::string_view text) override { printed.emplace_back(text); }

    std::vector<std::string> printed;
};

[[nodiscard]] std::shared_ptr<const ProgramUVE> CompileOrFailUVE(const std::string& source, const CollectionHostUVE& host) {
    const CompileResultUVE result = CompileUVScriptSourceUVE(source, host);
    for (const DiagnosticUVE& d : result.diagnostics) {
        ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
    }
    return result.program;
}

[[nodiscard]] std::vector<std::string> ErrorsOfUVE(const std::string& source) {
    CollectionHostUVE host;
    std::vector<std::string> messages;
    for (const DiagnosticUVE& d : CompileUVScriptSourceUVE(source, host).diagnostics) {
        messages.push_back(std::to_string(d.at.line) + ": " + d.message);
    }
    return messages;
}

TEST(UVScriptCollectionsUVETest, ParsesCollectionLiterals) {
    const ParseResultUVE result = ParseUVScriptUVE(R"(var xs = [1, 2, 3]
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
    for (const DiagnosticUVE& d : result.diagnostics) {
        ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
    }
    const FileUVE& file = result.file;
    ASSERT_EQ(file.fields.size(), 11U);
    EXPECT_EQ(file.fields[0].initializer->kind, ExprKindUVE::List);
    EXPECT_EQ(file.fields[0].initializer->operands.size(), 3U);
    EXPECT_EQ(file.fields[1].initializer->kind, ExprKindUVE::List);
    EXPECT_EQ(file.fields[1].initializer->operands.size(), 2U);
    EXPECT_EQ(file.fields[2].initializer->kind, ExprKindUVE::Map);
    EXPECT_EQ(file.fields[2].initializer->operands.size(), 4U);
    EXPECT_EQ(file.fields[3].initializer->kind, ExprKindUVE::Tuple);
    EXPECT_EQ(file.fields[3].initializer->operands.size(), 2U);
    // One value in brackets with no comma stays grouped, exactly as before.
    EXPECT_EQ(file.fields[4].initializer->kind, ExprKindUVE::Number);
    EXPECT_EQ(file.fields[5].initializer->kind, ExprKindUVE::Tuple);
    EXPECT_EQ(file.fields[5].initializer->operands.size(), 1U);
    EXPECT_EQ(file.fields[6].initializer->kind, ExprKindUVE::List);
    EXPECT_TRUE(file.fields[6].initializer->operands.empty());
    ASSERT_TRUE(file.fields[6].type.has_value());
    EXPECT_EQ(file.fields[6].type->name, "list");
    ASSERT_EQ(file.fields[6].type->arguments.size(), 1U);
    EXPECT_EQ(file.fields[6].type->arguments[0].name, "int");
    EXPECT_EQ(file.fields[7].initializer->kind, ExprKindUVE::Map);
    EXPECT_TRUE(file.fields[7].initializer->operands.empty());
    ASSERT_TRUE(file.fields[7].type.has_value());
    EXPECT_EQ(file.fields[7].type->name, "map");
    ASSERT_EQ(file.fields[7].type->arguments.size(), 2U);
    EXPECT_EQ(file.fields[8].initializer->kind, ExprKindUVE::Tuple);
    EXPECT_TRUE(file.fields[8].initializer->operands.empty());
    ASSERT_TRUE(file.fields[9].type.has_value());
    EXPECT_EQ(file.fields[9].type->name, "list");
    ASSERT_EQ(file.fields[9].type->arguments.size(), 1U);
    EXPECT_EQ(file.fields[9].type->arguments[0].name, "list");
    EXPECT_EQ(file.fields[10].initializer->kind, ExprKindUVE::List);
    EXPECT_EQ(file.fields[10].initializer->operands.size(), 2U);
}

TEST(UVScriptCollectionsUVETest, ParsesTupleUnpackingLets) {
    const ParseResultUVE result = ParseUVScriptUVE("on ready:\n    let (a, b) = pair()\n    let x = 1\n");
    for (const DiagnosticUVE& d : result.diagnostics) {
        ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
    }
    ASSERT_EQ(result.file.handlers.size(), 1U);
    const BlockUVE& body = result.file.handlers[0].body;
    ASSERT_EQ(body.size(), 2U);
    EXPECT_EQ(body[0]->kind, StmtKindUVE::Let);
    EXPECT_EQ(body[0]->names, (std::vector<std::string>{"a", "b"}));
    EXPECT_TRUE(body[0]->name.empty());
    EXPECT_EQ(body[1]->kind, StmtKindUVE::Let);
    EXPECT_EQ(body[1]->name, "x");
    EXPECT_TRUE(body[1]->names.empty());
    // Unpacked names take their types from the tuple; no annotation fits there.
    EXPECT_FALSE(ParseUVScriptUVE("on ready:\n    let (a, b): (int, str) = pair()\n").diagnostics.empty());
}

TEST(UVScriptCollectionsUVETest, ParsesElementAssignment) {
    const ParseResultUVE result = ParseUVScriptUVE("on ready:\n    xs[0] = 1\n    m[\"k\"] += 2\n");
    for (const DiagnosticUVE& d : result.diagnostics) {
        ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
    }
    ASSERT_EQ(result.file.handlers.size(), 1U);
    const BlockUVE& body = result.file.handlers[0].body;
    ASSERT_EQ(body.size(), 2U);
    EXPECT_EQ(body[0]->kind, StmtKindUVE::Assign);
    EXPECT_EQ(body[0]->op, "=");
    EXPECT_EQ(body[0]->target->kind, ExprKindUVE::Index);
    EXPECT_EQ(body[1]->kind, StmtKindUVE::Assign);
    EXPECT_EQ(body[1]->op, "+=");
    EXPECT_EQ(body[1]->target->kind, ExprKindUVE::Index);
}

TEST(UVScriptCollectionsUVETest, EmptyLiteralsNeedAType) {
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let xs = []\n"),
              (std::vector<std::string>{"2: this list is empty, so its item type is unknown - write it where the "
                                        "list is stored, as in 'let xs: list[int] = []'"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let m = {}\n"),
              (std::vector<std::string>{"2: this map is empty, so its types are unknown - write them where the map "
                                        "is stored, as in 'let scores: map[str, int] = {}'"}));
}

TEST(UVScriptCollectionsUVETest, LiteralItemsMustShareAType) {
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let xs = [1, \"a\"]\n"),
              (std::vector<std::string>{"2: this list holds int, not str"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let m = {\"a\": 1, \"b\": \"x\"}\n"),
              (std::vector<std::string>{"2: this map holds int, not str"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let m = {\"a\": 1, 2: 3}\n"),
              (std::vector<std::string>{"2: this map's keys are str, not int"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let m = {1.5: 1}\n"),
              (std::vector<std::string>{"2: map keys are int, str or bool, not float"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let xs = [none]\n"),
              (std::vector<std::string>{"2: this list holds none - give it values of one type"}));
    // Whole numbers join floats; the list holds floats.
    EXPECT_TRUE(ErrorsOfUVE("on ready:\n    let xs = [1, 2.5]\n").empty());
}

TEST(UVScriptCollectionsUVETest, AnnotationsMustNameElementTypes) {
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let xs: list = [1]\n"),
              (std::vector<std::string>{"2: write the item type, as in 'list[int]'"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let m: map[int] = {}\n"),
              (std::vector<std::string>{"2: write the key and value types, as in 'map[str, int]'"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let t: tuple = (1,)\n"),
              (std::vector<std::string>{"2: write the item types, as in 'tuple[int, str]'"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let m: map[vec3, int] = {}\n"),
              (std::vector<std::string>{"2: map keys are int, str or bool, not vec3"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let x: int[str] = 1\n"),
              (std::vector<std::string>{"2: only list[...], map[...] and tuple[...] take types inside brackets"}));
}

TEST(UVScriptCollectionsUVETest, CollectionsMustMatchExactly) {
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let xs: list[str] = [1]\n"),
              (std::vector<std::string>{"2: 'xs' needs list[str] but this is list[int]"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let xs = [1]\n    print(xs == [\"a\"])\n"),
              (std::vector<std::string>{"3: '==' cannot compare list[int] with list[str]"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    print([1] == [1.0])\n"),
              (std::vector<std::string>{"2: '==' cannot compare list[int] with list[float]"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let z = [1] + [\"a\"]\n"),
              (std::vector<std::string>{"2: these lists hold different types (list[int] and list[str])"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let xs = [1]\n    let y = -xs\n"),
              (std::vector<std::string>{"3: '-' does not work on list[int]"}));
}

TEST(UVScriptCollectionsUVETest, IndexingIsChecked) {
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let xs = [1]\n    print(xs[1.0])\n"),
              (std::vector<std::string>{"3: a list index is a whole number, not float"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let n = 1\n    print(n[0])\n"),
              (std::vector<std::string>{"3: only a list, map or tuple uses [...], not int"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let t = (1, \"a\")\n    let i = 0\n    print(t[i])\n"),
              (std::vector<std::string>{"4: a tuple index is written out, as in 'pair[0]' - each position has its "
                                        "own type"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let t = (1, \"a\")\n    print(t[5])\n"),
              (std::vector<std::string>{"3: this (int, str) has no index 5"}));
}

TEST(UVScriptCollectionsUVETest, ElementAssignmentIsChecked) {
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let t = (1, 2)\n    t[0] = 3\n"),
              (std::vector<std::string>{"3: a tuple cannot change - build a new one instead"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let a = [[1]]\n    a[0][1] = 2\n"),
              (std::vector<std::string>{"3: an element of an element cannot be assigned yet - unpack it into a "
                                        "variable first"}));
    EXPECT_EQ(ErrorsOfUVE("const xs = [1]\n\non ready:\n    xs[0] = 2\n"),
              (std::vector<std::string>{"4: 'xs' is a const and cannot change"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let n = 1\n    n[0] = 2\n"),
              (std::vector<std::string>{"3: only a list or map element can be assigned to"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let xs = [1]\n    xs[0] += 1.5\n"),
              (std::vector<std::string>{"3: '+=' would turn this int into float"}));
}

TEST(UVScriptCollectionsUVETest, UnpackingIsChecked) {
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let (a, b, c) = (1, \"x\")\n"),
              (std::vector<std::string>{"2: this (int, str) unpacks into 2, not 3"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let (a, b) = [1, 2]\n"),
              (std::vector<std::string>{"2: only a tuple unpacks into names, not list[int]"}));
}

TEST(UVScriptCollectionsUVETest, ForWalksRangesListsAndMaps) {
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    for x in 1:\n        pass\n"),
              (std::vector<std::string>{"2: 'for' walks a range, a list or a map's keys - not int"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    for x in (1, 2):\n        pass\n"),
              (std::vector<std::string>{"2: a tuple unpacks with 'let (a, b) = ...' instead of a loop"}));
}

TEST(UVScriptCollectionsUVETest, BuiltinsAreChecked) {
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    print(push({\"a\": 1}, 2))\n"),
              (std::vector<std::string>{"2: 'push' adds to a list, not map[str, int]"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    print(push([1], \"a\"))\n"),
              (std::vector<std::string>{"2: 'push' needs int but this is str"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    print(keys([1]))\n"),
              (std::vector<std::string>{"2: 'keys' reads a map, not list[int]"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    print(remove((1, 2), 0))\n"),
              (std::vector<std::string>{"2: 'remove' cannot shrink a tuple - build a new one instead"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    print(length(1))\n"),
              (std::vector<std::string>{"2: 'length' measures a vec3, a str or a collection, not int"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    print(contains(1, 2))\n"),
              (std::vector<std::string>{"2: 'contains' looks in a str, list, map or tuple, not int"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    print(contains((1, \"a\"), true))\n"),
              (std::vector<std::string>{"2: this (int, str) never holds bool"}));
    // Numbers compare across int and float at run time, so a float may be looked up in ints.
    EXPECT_TRUE(ErrorsOfUVE("on ready:\n    print(contains((1, 2), 1.0))\n").empty());
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let xs = [1]\n    print(xs.frobnicate())\n"),
              (std::vector<std::string>{"3: list[int] has no 'frobnicate'"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let xs = [1]\n    print(xs.push())\n"),
              (std::vector<std::string>{"3: 'push' takes 1 value(s) after the list[int], not 0"}));
    EXPECT_EQ(ErrorsOfUVE("on ready:\n    let xs = [1]\n    xs.push(1, 2)\n"),
              (std::vector<std::string>{"3: write 'xs.push(value)' - push adds one value"}));
}

TEST(UVScriptCollectionsUVETest, ReadsLiteralsAndNestedElements) {
    CollectionHostUVE host;
    const auto program = CompileOrFailUVE(R"(on ready:
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
    ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready")) << instance.GetLastErrorUVE();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"10", "30", "20", "1", "2"}));
}

TEST(UVScriptCollectionsUVETest, LengthPushKeysContainsRemove) {
    CollectionHostUVE host;
    const auto program = CompileOrFailUVE(R"(on ready:
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
    ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready")) << instance.GetLastErrorUVE();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"2", "4", "[1, 2, 3]", "[1, 2]", "[b, a]", "true", "false",
                                                       "true", "true", "[2, 3]", "{a: 1}"}));
}

TEST(UVScriptCollectionsUVETest, MethodFormsAndSugarStatement) {
    CollectionHostUVE host;
    const auto program = CompileOrFailUVE(R"(on ready:
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
    ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready")) << instance.GetLastErrorUVE();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"1", "true", "3", "[1, 2]", "[1, 3]", "[3]", "[k]", "1", "{}",
                                                       "0"}));
}

TEST(UVScriptCollectionsUVETest, AssignsElements) {
    CollectionHostUVE host;
    const auto program = CompileOrFailUVE(R"(on ready:
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
    ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready")) << instance.GetLastErrorUVE();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"[9, 2, 10]", "{a: 5, b: 2}"}));
}

TEST(UVScriptCollectionsUVETest, CopiesNeverAlias) {
    CollectionHostUVE host;
    const auto program = CompileOrFailUVE(R"(on ready:
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
    ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready")) << instance.GetLastErrorUVE();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"[1, 2]", "[9, 2]", "1"}));
}

TEST(UVScriptCollectionsUVETest, ForWalksListsAndMaps) {
    CollectionHostUVE host;
    const auto program = CompileOrFailUVE(R"(on ready:
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
    ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready")) << instance.GetLastErrorUVE();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"6", "ab", "1", "6"}));
}

TEST(UVScriptCollectionsUVETest, ForSeesASnapshot) {
    CollectionHostUVE host;
    const auto program = CompileOrFailUVE(R"(on ready:
    let xs = [1, 2, 3]
    for n in xs:
        print(n)
        xs.push(9)
    print(length(xs))
)",
                                          host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready")) << instance.GetLastErrorUVE();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"1", "2", "3", "6"}));
}

TEST(UVScriptCollectionsUVETest, UnpacksTuplesFromFunctions) {
    CollectionHostUVE host;
    const auto program = CompileOrFailUVE(R"(fn pair() -> tuple[int, str]:
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
    ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready")) << instance.GetLastErrorUVE();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"7", "seven", "7", "seven", "(7, seven)"}));
}

TEST(UVScriptCollectionsUVETest, ConcatenatesAndCompares) {
    CollectionHostUVE host;
    const auto program = CompileOrFailUVE(R"(on ready:
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
    ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready")) << instance.GetLastErrorUVE();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"[1, 2, 3]", "true", "false", "true", "true", "true", "true",
                                                       "true"}));
}

TEST(UVScriptCollectionsUVETest, FormatsCollectionsInStrings) {
    CollectionHostUVE host;
    const auto program = CompileOrFailUVE(R"(on ready:
    let xs = [1, 2]
    print("xs={xs} len={length(xs)}")
    print(str({"a": [1]}))
    print((1,))
    print(())
)",
                                          host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready")) << instance.GetLastErrorUVE();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"xs=[1, 2] len=2", "{a: [1]}", "(1,)", "()"}));
}

TEST(UVScriptCollectionsUVETest, ReportsIndexErrorsAtRunTime) {
    CollectionHostUVE host;
    const auto outOfRange = CompileOrFailUVE(R"(on ready:
    let xs = [1, 2]
    let i = 5
    print(xs[i])
)",
                                             host);
    ASSERT_NE(outOfRange, nullptr);
    ScriptInstanceUVE first(outOfRange, host);
    EXPECT_FALSE(first.RaiseEventUVE("ready"));
    EXPECT_EQ(first.GetLastErrorUVE(), "line 4: index 5 is out of range for a list of 2");

    const auto missing = CompileOrFailUVE(R"(on ready:
    let m = {"a": 1}
    let k = "b"
    print(m[k])
)",
                                          host);
    ASSERT_NE(missing, nullptr);
    ScriptInstanceUVE second(missing, host);
    EXPECT_FALSE(second.RaiseEventUVE("ready"));
    EXPECT_EQ(second.GetLastErrorUVE(), "line 4: the map has no key 'b'");
}

TEST(UVScriptCollectionsUVETest, ReportsUnpackErrorsAtRunTime) {
    CollectionHostUVE host;
    const auto wrongSize = CompileOrFailUVE("on ready:\n    let (a, b) = triple()\n    print(a)\n", host);
    ASSERT_NE(wrongSize, nullptr);
    ScriptInstanceUVE first(wrongSize, host);
    EXPECT_FALSE(first.RaiseEventUVE("ready"));
    EXPECT_EQ(first.GetLastErrorUVE(), "line 2: this tuple holds 3, not 2");

    const auto notTuple = CompileOrFailUVE("on ready:\n    let (a, b) = notuple()\n    print(a)\n", host);
    ASSERT_NE(notTuple, nullptr);
    ScriptInstanceUVE second(notTuple, host);
    EXPECT_FALSE(second.RaiseEventUVE("ready"));
    EXPECT_EQ(second.GetLastErrorUVE(), "line 2: only a tuple unpacks into names, not a list");
}

TEST(UVScriptCollectionsUVETest, InferredTypesUnifyNumbers) {
    CollectionHostUVE host;
    const auto program = CompileOrFailUVE(R"(on ready:
    let xs = [1, 2.5]
    print(xs)
    let m = {"a": 1, "b": 2.5}
    print(m)
    print(xs[0])
)",
                                          host);
    ASSERT_NE(program, nullptr);
    ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready")) << instance.GetLastErrorUVE();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"[1.0, 2.5]", "{a: 1.0, b: 2.5}", "1.0"}));
}

TEST(UVScriptCollectionsUVETest, AnnotatedEmptiesStartEmpty) {
    CollectionHostUVE host;
    const auto program = CompileOrFailUVE(R"(var empty_field: list[int]

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
    ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready")) << instance.GetLastErrorUVE();
    EXPECT_EQ(host.printed, (std::vector<std::string>{"0", "0", "0", "0", "0", "[1]"}));
}

TEST(UVScriptCollectionsUVETest, FingerprintsDistinguishElementTypes) {
    CollectionHostUVE host;
    const auto ints = CompileOrFailUVE("var xs: list[int] = []\n", host);
    const auto strs = CompileOrFailUVE("var xs: list[str] = []\n", host);
    const auto intsAgain = CompileOrFailUVE("var xs: list[int] = []\n", host);
    ASSERT_NE(ints, nullptr);
    ASSERT_NE(strs, nullptr);
    ASSERT_NE(intsAgain, nullptr);
    EXPECT_NE(GetProgramFingerprintUVE(*ints), GetProgramFingerprintUVE(*strs));
    EXPECT_EQ(GetProgramFingerprintUVE(*ints), GetProgramFingerprintUVE(*intsAgain));
}

TEST(UVScriptCollectionsUVETest, NativeCodegenCoversCollectionsAndStrLength) {
    CollectionHostUVE host;
    const auto strLength = CompileOrFailUVE("fn f() -> int:\n    return length(\"abc\")\n", host);
    ASSERT_NE(strLength, nullptr);
    const std::string typed = GenerateUVScriptNativeCppUVE(*strLength, "strlen.uvs");
    // A str's length stays in typed code, counting characters rather than a magnitude.
    EXPECT_NE(typed.find("std::int64_t T0(ContextUVE& c)"), std::string::npos);
    EXPECT_NE(typed.find(".size())"), std::string::npos);

    const auto lists = CompileOrFailUVE("fn f() -> int:\n    let xs = [1]\n    return xs[0]\n", host);
    ASSERT_NE(lists, nullptr);
    const std::string boxed = GenerateUVScriptNativeCppUVE(*lists, "lists.uvs");
    EXPECT_NE(boxed.find("BuildListUVE"), std::string::npos);
    EXPECT_NE(boxed.find("GetIndexUVE"), std::string::npos);
}

} // namespace
} // namespace UVE::UVScript::Tests
