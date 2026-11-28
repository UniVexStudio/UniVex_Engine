// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace UVE::Blueprint {

/// 1-based line and column of a token in the source text.
struct SourceLocation final {
    std::uint32_t line = 0U;
    std::uint32_t column = 0U;

    bool operator==(const SourceLocation&) const = default;
};

struct Diagnostic final {
    SourceLocation at;
    std::string message;
};

/// A written type: `float`, `list[int]`, `map[str, Object3D]`.
struct TypeRef final {
    std::string name;
    std::vector<TypeRef> arguments;
    SourceLocation at;
};

enum class ExprKind : std::uint8_t {
    Number,
    String,
    Bool,
    None,
    Name,
    Unary,
    Binary,
    Member,
    Call,
    Index,
    List,
    Map,
    Tuple,
};

/// One expression. Which fields mean something depends on `kind`:
/// - Number: `number`, `isInteger`, `unit` ("" or s, ms, m, cm, km, deg, rad).
/// - String: `segments` (the literal text between `{...}` parts, one more than `operands`) and
///   `operands` (the interpolated expressions).
/// - Bool: `boolean`. Name: `text`. Member: `operands[0]` `.` `text`.
/// - Unary/Binary: `text` is the operator (`-`, `not`, `+`, `and`, `..`, ...), `operands` in order.
/// - Call: `operands[0]` is the callee, the rest are arguments. Index: `operands[0][operands[1]]`.
/// - List/Tuple: `operands` are the items. Map: `operands` alternate key, value, key, value...
struct Expr final {
    ExprKind kind = ExprKind::None;
    SourceLocation at;
    std::string text;
    double number = 0.0;
    bool isInteger = false;
    bool boolean = false;
    std::string unit;
    std::vector<std::string> segments;
    std::vector<std::unique_ptr<Expr>> operands;
};

using ExprPtr = std::unique_ptr<Expr>;

enum class StmtKind : std::uint8_t {
    Let,
    Assign,
    Expr,
    If,
    While,
    For,
    Return,
    Break,
    Continue,
    Pass,
    Wait,
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;
using Block = std::vector<StmtPtr>;

struct ConditionalBlock final {
    ExprPtr condition;
    Block body;
};

/// One statement. By `kind`:
/// - Let: `name` (or `names` for `let (a, b) = ...`), `type` (optional), `value`.
/// - Assign: `target` `op` `value` (`op` is =, +=, -=, *= or /=). Expr: `value`.
/// - If: `branches` (the `if` and every `elif`, in order) and `elseBody`.
/// - While: `branches[0]`. For: `name` in `value`, `body`.
/// - Return: `value` (may be null). Wait: `value`.
struct Stmt final {
    StmtKind kind = StmtKind::Pass;
    SourceLocation at;
    std::string name;
    std::vector<std::string> names;
    std::optional<TypeRef> type;
    std::string op;
    ExprPtr target;
    ExprPtr value;
    std::vector<ConditionalBlock> branches;
    Block body;
    Block elseBody;
};

struct Param final {
    std::string name;
    std::optional<TypeRef> type;
    SourceLocation at;
};

enum class FieldKind : std::uint8_t {
    Export,
    Var,
    Const,
};

struct Field final {
    FieldKind kind = FieldKind::Var;
    std::string name;
    std::optional<TypeRef> type;
    ExprPtr initializer;
    SourceLocation at;
};

/// `on <event>(params):` - runs when the object raises `event`.
struct Handler final {
    std::string event;
    std::vector<Param> params;
    Block body;
    SourceLocation at;
};

struct Function final {
    std::string name;
    std::vector<Param> params;
    std::optional<TypeRef> returnType;
    Block body;
    SourceLocation at;
};

/// `entity Player : Character3D`
struct Header final {
    std::string name;
    std::string baseKind;
    SourceLocation at;
};

/// A whole Blueprint, in source order within each list.
struct File final {
    std::optional<Header> header;
    std::vector<Field> fields;
    std::vector<Handler> handlers;
    std::vector<Function> functions;
};

} // namespace UVE::Blueprint
