// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace UVE::UVScript {

/// 1-based line and column of a token in the source text.
struct SourceLocationUVE final {
    std::uint32_t line = 0U;
    std::uint32_t column = 0U;

    bool operator==(const SourceLocationUVE&) const = default;
};

struct DiagnosticUVE final {
    SourceLocationUVE at;
    std::string message;
};

/// A written type: `float`, `list[int]`, `map[str, Node3D]`.
struct TypeRefUVE final {
    std::string name;
    std::vector<TypeRefUVE> arguments;
    SourceLocationUVE at;
};

enum class ExprKindUVE : std::uint8_t {
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
};

/// One expression. Which fields mean something depends on `kind`:
/// - Number: `number`, `isInteger`, `unit` ("" or s, ms, m, cm, km, deg, rad).
/// - String: `segments` (the literal text between `{...}` parts, one more than `operands`) and
///   `operands` (the interpolated expressions).
/// - Bool: `boolean`. Name: `text`. Member: `operands[0]` `.` `text`.
/// - Unary/Binary: `text` is the operator (`-`, `not`, `+`, `and`, `..`, ...), `operands` in order.
/// - Call: `operands[0]` is the callee, the rest are arguments. Index: `operands[0][operands[1]]`.
struct ExprUVE final {
    ExprKindUVE kind = ExprKindUVE::None;
    SourceLocationUVE at;
    std::string text;
    double number = 0.0;
    bool isInteger = false;
    bool boolean = false;
    std::string unit;
    std::vector<std::string> segments;
    std::vector<std::unique_ptr<ExprUVE>> operands;
};

using ExprPtrUVE = std::unique_ptr<ExprUVE>;

enum class StmtKindUVE : std::uint8_t {
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

struct StmtUVE;
using StmtPtrUVE = std::unique_ptr<StmtUVE>;
using BlockUVE = std::vector<StmtPtrUVE>;

struct ConditionalBlockUVE final {
    ExprPtrUVE condition;
    BlockUVE body;
};

/// One statement. By `kind`:
/// - Let: `name`, `type` (optional), `value`.
/// - Assign: `target` `op` `value` (`op` is =, +=, -=, *= or /=). Expr: `value`.
/// - If: `branches` (the `if` and every `elif`, in order) and `elseBody`.
/// - While: `branches[0]`. For: `name` in `value`, `body`.
/// - Return: `value` (may be null). Wait: `value`.
struct StmtUVE final {
    StmtKindUVE kind = StmtKindUVE::Pass;
    SourceLocationUVE at;
    std::string name;
    std::optional<TypeRefUVE> type;
    std::string op;
    ExprPtrUVE target;
    ExprPtrUVE value;
    std::vector<ConditionalBlockUVE> branches;
    BlockUVE body;
    BlockUVE elseBody;
};

struct ParamUVE final {
    std::string name;
    std::optional<TypeRefUVE> type;
    SourceLocationUVE at;
};

enum class FieldKindUVE : std::uint8_t {
    Export,
    Var,
    Const,
};

struct FieldUVE final {
    FieldKindUVE kind = FieldKindUVE::Var;
    std::string name;
    std::optional<TypeRefUVE> type;
    ExprPtrUVE initializer;
    SourceLocationUVE at;
};

/// `on <event>(params):` - runs when the node raises `event`.
struct HandlerUVE final {
    std::string event;
    std::vector<ParamUVE> params;
    BlockUVE body;
    SourceLocationUVE at;
};

struct FunctionUVE final {
    std::string name;
    std::vector<ParamUVE> params;
    std::optional<TypeRefUVE> returnType;
    BlockUVE body;
    SourceLocationUVE at;
};

/// `entity Player : Character3D`
struct HeaderUVE final {
    std::string name;
    std::string baseKind;
    SourceLocationUVE at;
};

/// A whole `.uvs` file, in source order within each list.
struct FileUVE final {
    std::optional<HeaderUVE> header;
    std::vector<FieldUVE> fields;
    std::vector<HandlerUVE> handlers;
    std::vector<FunctionUVE> functions;
};

} // namespace UVE::UVScript
