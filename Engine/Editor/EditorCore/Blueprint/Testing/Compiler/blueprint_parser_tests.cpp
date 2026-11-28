// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>

#include <gtest/gtest.h>

#include "blueprint_parser.h"

namespace UVE::Blueprint::Tests {
namespace {

constexpr const char* kPlayer = R"(entity Player : Character3D

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
    if grounded and input.pressed("jump"):
        velocity.y = sqrt(2.0 * gravity * jump_height)
        jumps += 1
    elif not grounded:
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

TEST(BlueprintParserTest, ParsesAWholeEntityScript) {
    const ParseResult result = ParseBlueprintNotation(kPlayer);
    for (const Diagnostic& d : result.diagnostics) {
        ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
    }
    const File& file = result.file;
    ASSERT_TRUE(file.header.has_value());
    EXPECT_EQ(file.header->name, "Player");
    EXPECT_EQ(file.header->baseKind, "Character3D");

    ASSERT_EQ(file.fields.size(), 4U);
    EXPECT_EQ(file.fields[0].kind, FieldKind::Export);
    EXPECT_EQ(file.fields[0].type->name, "float");
    EXPECT_EQ(file.fields[1].initializer->unit, "m");
    EXPECT_EQ(file.fields[2].kind, FieldKind::Var);
    EXPECT_TRUE(file.fields[2].initializer->isInteger);
    EXPECT_EQ(file.fields[3].kind, FieldKind::Const);
    EXPECT_EQ(file.fields[3].initializer->unit, "deg");

    ASSERT_EQ(file.handlers.size(), 3U);
    EXPECT_EQ(file.handlers[0].event, "ready");
    EXPECT_EQ(file.handlers[1].params[0].name, "dt");
    EXPECT_EQ(file.handlers[2].params[0].type->name, "Object3D");

    const Block& tick = file.handlers[1].body;
    ASSERT_EQ(tick.size(), 3U);
    EXPECT_EQ(tick[0]->kind, StmtKind::Let);
    EXPECT_EQ(tick[1]->kind, StmtKind::Assign);
    EXPECT_EQ(tick[1]->target->kind, ExprKind::Member);
    EXPECT_EQ(tick[2]->kind, StmtKind::If);
    EXPECT_EQ(tick[2]->branches.size(), 2U);
    EXPECT_EQ(tick[2]->elseBody.size(), 1U);
    EXPECT_EQ(tick[2]->branches[0].body[1]->op, "+=");

    const Block& entered = file.handlers[2].body;
    EXPECT_EQ(entered[0]->kind, StmtKind::Wait);
    EXPECT_EQ(entered[0]->value->unit, "s");
    EXPECT_DOUBLE_EQ(entered[0]->value->number, 0.5);
    EXPECT_EQ(entered[1]->value->kind, ExprKind::Call);

    ASSERT_EQ(file.functions.size(), 1U);
    const Function& heal = file.functions[0];
    EXPECT_EQ(heal.returnType->name, "bool");
    EXPECT_EQ(heal.body[0]->kind, StmtKind::For);
    EXPECT_EQ(heal.body[0]->value->text, "..");
    EXPECT_EQ(heal.body[1]->kind, StmtKind::While);
    EXPECT_EQ(heal.body[2]->kind, StmtKind::Return);
}

TEST(BlueprintParserTest, BindsOperatorsByPrecedence) {
    const ParseResult result = ParseBlueprintNotation("fn f():\n    return 1 + 2 * 3 == 7 and not a or b\n");
    ASSERT_TRUE(result.IsSuccess());
    const Expr& root = *result.file.functions[0].body[0]->value;
    ASSERT_EQ(root.text, "or");
    const Expr& conjunction = *root.operands[0];
    ASSERT_EQ(conjunction.text, "and");
    const Expr& equals = *conjunction.operands[0];
    ASSERT_EQ(equals.text, "==");
    const Expr& sum = *equals.operands[0];
    ASSERT_EQ(sum.text, "+");
    EXPECT_EQ(sum.operands[1]->text, "*");
    EXPECT_EQ(conjunction.operands[1]->text, "not");
}

TEST(BlueprintParserTest, SplitsStringInterpolation) {
    const ParseResult result = ParseBlueprintNotation("on ready:\n    print(\"hp {hp} / {max_hp} {{ok}}\")\n");
    ASSERT_TRUE(result.IsSuccess());
    const Expr& text = *result.file.handlers[0].body[0]->value->operands[1];
    ASSERT_EQ(text.kind, ExprKind::String);
    EXPECT_EQ(text.segments, (std::vector<std::string>{"hp ", " / ", " {ok}"}));
    ASSERT_EQ(text.operands.size(), 2U);
    EXPECT_EQ(text.operands[1]->text, "max_hp");
}

TEST(BlueprintParserTest, ReportsErrorsWithLinesAndKeepsGoing) {
    const ParseResult result = ParseBlueprintNotation(
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

TEST(BlueprintParserTest, RejectsBadIndentationAndStrayCode) {
    EXPECT_FALSE(ParseBlueprintNotation("on ready:\n        a = 1\n    b = 2\n").IsSuccess());
    EXPECT_FALSE(ParseBlueprintNotation("x = 1\n").IsSuccess());
    EXPECT_FALSE(ParseBlueprintNotation("on ready:\n    print(\"open\n").IsSuccess());
    EXPECT_FALSE(ParseBlueprintNotation("on ready:\npass\n").IsSuccess());
}

TEST(BlueprintParserTest, JoinsLinesInsideBracketsAndReadsUnits) {
    const ParseResult result = ParseBlueprintNotation("on ready:\n    move(1,\n         2)\n    wait 150 ms\n");
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.file.handlers[0].body[0]->value->operands.size(), 3U);
    EXPECT_EQ(result.file.handlers[0].body[1]->value->unit, "ms");
    EXPECT_TRUE(IsBlueprintNotationUnit("rad"));
    EXPECT_FALSE(IsBlueprintNotationUnit("hours"));
}

TEST(BlueprintParserTest, RejectsDeeplyNestedExpressionsWithoutStackOverflow) {
    std::string source = "on ready:\n    let x = ";
    source.append(200U, '(');
    source += "1";
    source.append(200U, ')');
    source += "\n";
    const ParseResult result = ParseBlueprintNotation(source);
    EXPECT_FALSE(result.IsSuccess());
    ASSERT_FALSE(result.diagnostics.empty());
}

} // namespace
} // namespace UVE::Blueprint::Tests
