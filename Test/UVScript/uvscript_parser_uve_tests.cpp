// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>

#include <gtest/gtest.h>

#include "uve/uvscript/uvscript_parser_uve.h"

namespace UVE::UVScript::Tests {
namespace {

constexpr const char* kPlayerUVE = R"(entity Player : Character3D

# fields
export speed: float = 6.0
export jump_height = 1.2 m
var jumps = 0
const TURN = 90 deg

on ready:
    print("{name} is ready")

on tick(dt):
    let move = input.axis("left", "right")
    velocity.x = move * speed
    if is_on_floor and input.pressed("jump"):
        velocity.y = sqrt(2.0 * gravity * jump_height)
        jumps += 1
    elif not is_on_floor:
        pass
    else:
        jumps = 0

on body_entered(other: Object3D):
    wait 0.5 s
    other.hide()

fn heal(amount: int) -> bool:
    for i in 0..amount:
        health += 1
    while health > max_health:
        health -= 1
    return health >= max_health
)";

TEST(UVScriptParserUVETest, ParsesAWholeEntityScript) {
    const ParseResultUVE result = ParseUVScriptUVE(kPlayerUVE);
    for (const DiagnosticUVE& d : result.diagnostics) {
        ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
    }
    const FileUVE& file = result.file;
    ASSERT_TRUE(file.header.has_value());
    EXPECT_EQ(file.header->name, "Player");
    EXPECT_EQ(file.header->baseKind, "Character3D");

    ASSERT_EQ(file.fields.size(), 4U);
    EXPECT_EQ(file.fields[0].kind, FieldKindUVE::Export);
    EXPECT_EQ(file.fields[0].type->name, "float");
    EXPECT_EQ(file.fields[1].initializer->unit, "m");
    EXPECT_EQ(file.fields[2].kind, FieldKindUVE::Var);
    EXPECT_TRUE(file.fields[2].initializer->isInteger);
    EXPECT_EQ(file.fields[3].kind, FieldKindUVE::Const);
    EXPECT_EQ(file.fields[3].initializer->unit, "deg");

    ASSERT_EQ(file.handlers.size(), 3U);
    EXPECT_EQ(file.handlers[0].event, "ready");
    EXPECT_EQ(file.handlers[1].params[0].name, "dt");
    EXPECT_EQ(file.handlers[2].params[0].type->name, "Object3D");

    const BlockUVE& tick = file.handlers[1].body;
    ASSERT_EQ(tick.size(), 3U);
    EXPECT_EQ(tick[0]->kind, StmtKindUVE::Let);
    EXPECT_EQ(tick[1]->kind, StmtKindUVE::Assign);
    EXPECT_EQ(tick[1]->target->kind, ExprKindUVE::Member);
    EXPECT_EQ(tick[2]->kind, StmtKindUVE::If);
    EXPECT_EQ(tick[2]->branches.size(), 2U);
    EXPECT_EQ(tick[2]->elseBody.size(), 1U);
    EXPECT_EQ(tick[2]->branches[0].body[1]->op, "+=");

    const BlockUVE& entered = file.handlers[2].body;
    EXPECT_EQ(entered[0]->kind, StmtKindUVE::Wait);
    EXPECT_EQ(entered[0]->value->unit, "s");
    EXPECT_DOUBLE_EQ(entered[0]->value->number, 0.5);
    EXPECT_EQ(entered[1]->value->kind, ExprKindUVE::Call);

    ASSERT_EQ(file.functions.size(), 1U);
    const FunctionUVE& heal = file.functions[0];
    EXPECT_EQ(heal.returnType->name, "bool");
    EXPECT_EQ(heal.body[0]->kind, StmtKindUVE::For);
    EXPECT_EQ(heal.body[0]->value->text, "..");
    EXPECT_EQ(heal.body[1]->kind, StmtKindUVE::While);
    EXPECT_EQ(heal.body[2]->kind, StmtKindUVE::Return);
}

TEST(UVScriptParserUVETest, BindsOperatorsByPrecedence) {
    const ParseResultUVE result = ParseUVScriptUVE("fn f():\n    return 1 + 2 * 3 == 7 and not a or b\n");
    ASSERT_TRUE(result.IsSuccessUVE());
    const ExprUVE& root = *result.file.functions[0].body[0]->value;
    ASSERT_EQ(root.text, "or");
    const ExprUVE& conjunction = *root.operands[0];
    ASSERT_EQ(conjunction.text, "and");
    const ExprUVE& equals = *conjunction.operands[0];
    ASSERT_EQ(equals.text, "==");
    const ExprUVE& sum = *equals.operands[0];
    ASSERT_EQ(sum.text, "+");
    EXPECT_EQ(sum.operands[1]->text, "*");
    EXPECT_EQ(conjunction.operands[1]->text, "not");
}

TEST(UVScriptParserUVETest, SplitsStringInterpolation) {
    const ParseResultUVE result = ParseUVScriptUVE("on ready:\n    print(\"hp {hp} / {max_hp} {{ok}}\")\n");
    ASSERT_TRUE(result.IsSuccessUVE());
    const ExprUVE& text = *result.file.handlers[0].body[0]->value->operands[1];
    ASSERT_EQ(text.kind, ExprKindUVE::String);
    EXPECT_EQ(text.segments, (std::vector<std::string>{"hp ", " / ", " {ok}"}));
    ASSERT_EQ(text.operands.size(), 2U);
    EXPECT_EQ(text.operands[1]->text, "max_hp");
}

TEST(UVScriptParserUVETest, ReportsErrorsWithLinesAndKeepsGoing) {
    const ParseResultUVE result = ParseUVScriptUVE(
        "entity Door\n"            // 1: missing ": Kind"
        "on open:\n"
        "    let = 3\n"             // 3: missing name
        "    angle += 90 deg\n"
        "fn close(:\n"             // 5: bad parameter
        "    pass\n"
        "var speed\n");            // 7: no type and no value
    // Line 5 has two: the '(' is never closed, and ':' is not a parameter.
    ASSERT_EQ(result.diagnostics.size(), 5U);
    EXPECT_EQ(result.diagnostics[0].at.line, 1U);
    EXPECT_EQ(result.diagnostics[1].at.line, 3U);
    EXPECT_EQ(result.diagnostics[2].at.line, 5U);
    EXPECT_EQ(result.diagnostics[3].at.line, 5U);
    EXPECT_EQ(result.diagnostics[4].at.line, 7U);
    // What was fine still parsed.
    ASSERT_EQ(result.file.handlers.size(), 1U);
    ASSERT_EQ(result.file.handlers[0].body.size(), 1U);
    EXPECT_EQ(result.file.handlers[0].body[0]->value->unit, "deg");
}

TEST(UVScriptParserUVETest, RejectsBadIndentationAndStrayCode) {
    EXPECT_FALSE(ParseUVScriptUVE("on ready:\n        a = 1\n    b = 2\n").IsSuccessUVE());
    EXPECT_FALSE(ParseUVScriptUVE("x = 1\n").IsSuccessUVE());
    EXPECT_FALSE(ParseUVScriptUVE("on ready:\n    print(\"open\n").IsSuccessUVE());
    EXPECT_FALSE(ParseUVScriptUVE("on ready:\npass\n").IsSuccessUVE());
}

TEST(UVScriptParserUVETest, JoinsLinesInsideBracketsAndReadsUnits) {
    const ParseResultUVE result = ParseUVScriptUVE("on ready:\n    move(1,\n         2)\n    wait 150 ms\n");
    ASSERT_TRUE(result.IsSuccessUVE());
    EXPECT_EQ(result.file.handlers[0].body[0]->value->operands.size(), 3U);
    EXPECT_EQ(result.file.handlers[0].body[1]->value->unit, "ms");
    EXPECT_TRUE(IsUVScriptUnitUVE("rad"));
    EXPECT_FALSE(IsUVScriptUnitUVE("hours"));
}

} // namespace
} // namespace UVE::UVScript::Tests
