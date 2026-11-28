// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <string>
#include <system_error>
#include <utility>

namespace UVE::Blueprint {
namespace {

// ---------------------------------------------------------------- lexer

enum class TokenKind : std::uint8_t {
    Name,
    Number,
    String,
    Symbol,
    Newline,
    Indent,
    Dedent,
    End,
};

struct Token final {
    TokenKind kind = TokenKind::End;
    std::string text;
    SourceLocation at;
    bool isInteger = false;
};

constexpr std::array<std::string_view, 7> kUnits{"s", "ms", "m", "cm", "km", "deg", "rad"};

// Two-character symbols first, so "<=" is not read as "<" then "=".
constexpr std::array<std::string_view, 10> kTwoCharSymbols{"==", "!=", "<=", ">=", "+=", "-=",
                                                              "*=", "/=", "->", ".."};
constexpr std::string_view kOneCharSymbols = "()[]{}:,.+-*/%<>=";

class Lexer final {
public:
    Lexer(const std::string_view source, std::vector<Diagnostic>& diagnostics)
        : m_source(source), m_diagnostics(diagnostics) {}

    std::vector<Token> Run() {
        std::vector<std::size_t> indents{0U};
        bool lineStart = true;
        while (m_pos < m_source.size()) {
            if (lineStart && m_depth == 0) {
                // Measure this line's indentation; blank and comment-only lines do not count.
                std::size_t width = 0U;
                while (m_pos < m_source.size() && (Peek() == ' ' || Peek() == '\t')) {
                    width += Peek() == '\t' ? 4U : 1U;
                    Advance();
                }
                if (m_pos >= m_source.size() || Peek() == '\n' || Peek() == '\r' || Peek() == '#') {
                    SkipToLineEnd();
                    continue;
                }
                lineStart = false;
                if (width > indents.back()) {
                    indents.push_back(width);
                    Emit(TokenKind::Indent, "");
                } else {
                    while (width < indents.back()) {
                        indents.pop_back();
                        Emit(TokenKind::Dedent, "");
                    }
                    if (width != indents.back()) {
                        Error("this line's indentation does not match any block above it");
                    }
                }
            }
            const char c = Peek();
            if (c == ' ' || c == '\t' || c == '\r') {
                Advance();
            } else if (c == '#') {
                while (m_pos < m_source.size() && Peek() != '\n') {
                    Advance();
                }
            } else if (c == '\n') {
                if (m_depth > 0 && NextLineStartsADeclaration()) {
                    // An unclosed bracket would otherwise swallow the rest of the file. A line that
                    // starts a new statement means it was never closed: report it and move on.
                    m_diagnostics.push_back({m_openBracket, "this bracket is never closed"});
                    m_depth = 0;
                }
                if (m_depth == 0) {
                    Emit(TokenKind::Newline, "");
                    lineStart = true;
                }
                Advance();
            } else if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
                LexName();
            } else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
                LexNumber();
            } else if (c == '"') {
                LexString();
            } else {
                LexSymbol();
            }
        }
        if (!m_tokens.empty() && m_tokens.back().kind != TokenKind::Newline &&
            m_tokens.back().kind != TokenKind::Dedent) {
            Emit(TokenKind::Newline, "");
        }
        while (indents.size() > 1U) {
            indents.pop_back();
            Emit(TokenKind::Dedent, "");
        }
        Emit(TokenKind::End, "");
        return std::move(m_tokens);
    }

private:
    [[nodiscard]] char Peek(const std::size_t ahead = 0U) const noexcept {
        return m_pos + ahead < m_source.size() ? m_source[m_pos + ahead] : '\0';
    }

    void Advance() noexcept {
        if (m_source[m_pos] == '\n') {
            ++m_line;
            m_column = 1U;
        } else {
            ++m_column;
        }
        ++m_pos;
    }

    void SkipToLineEnd() noexcept {
        while (m_pos < m_source.size() && Peek() != '\n') {
            Advance();
        }
        if (m_pos < m_source.size()) {
            Advance();
        }
    }

    void Emit(const TokenKind kind, std::string text, const SourceLocation at = {}, const bool isInteger = false) {
        Token token{kind, std::move(text), at.line == 0U ? SourceLocation{m_line, m_column} : at, isInteger};
        m_tokens.push_back(std::move(token));
    }

    void Error(std::string message) { m_diagnostics.push_back({{m_line, m_column}, std::move(message)}); }

    void LexName() {
        const SourceLocation at{m_line, m_column};
        const std::size_t start = m_pos;
        while (std::isalnum(static_cast<unsigned char>(Peek())) != 0 || Peek() == '_') {
            Advance();
        }
        Emit(TokenKind::Name, std::string{m_source.substr(start, m_pos - start)}, at);
    }

    void LexNumber() {
        const SourceLocation at{m_line, m_column};
        const std::size_t start = m_pos;
        bool isInteger = true;
        while (std::isdigit(static_cast<unsigned char>(Peek())) != 0 || Peek() == '_') {
            Advance();
        }
        // "1..5" is a range, not a number with two points.
        if (Peek() == '.' && Peek(1U) != '.' && std::isdigit(static_cast<unsigned char>(Peek(1U))) != 0) {
            isInteger = false;
            Advance();
            while (std::isdigit(static_cast<unsigned char>(Peek())) != 0 || Peek() == '_') {
                Advance();
            }
        }
        std::string text;
        for (const char digit : m_source.substr(start, m_pos - start)) {
            if (digit != '_') {
                text += digit;
            }
        }
        Emit(TokenKind::Number, std::move(text), at, isInteger);
    }

    void LexString() {
        const SourceLocation at{m_line, m_column};
        Advance();
        std::string text;
        while (m_pos < m_source.size() && Peek() != '"' && Peek() != '\n') {
            if (Peek() == '\\' && m_pos + 1U < m_source.size()) {
                Advance();
                const char escaped = Peek();
                text += escaped == 'n' ? '\n' : escaped == 't' ? '\t' : escaped;
                Advance();
                continue;
            }
            text += Peek();
            Advance();
        }
        if (Peek() != '"') {
            m_diagnostics.push_back({at, "this string is never closed - add a \" before the end of the line"});
        } else {
            Advance();
        }
        Emit(TokenKind::String, std::move(text), at);
    }

    void LexSymbol() {
        const SourceLocation at{m_line, m_column};
        for (const std::string_view symbol : kTwoCharSymbols) {
            if (Peek() == symbol[0] && Peek(1U) == symbol[1]) {
                Advance();
                Advance();
                Emit(TokenKind::Symbol, std::string{symbol}, at);
                return;
            }
        }
        const char c = Peek();
        if (kOneCharSymbols.find(c) == std::string_view::npos) {
            Error(std::string{"'"} + c + "' is not part of Blueprint");
            Advance();
            return;
        }
        if (c == '(' || c == '[' || c == '{') {
            if (m_depth == 0) {
                m_openBracket = at;
            }
            ++m_depth;
        } else if ((c == ')' || c == ']' || c == '}') && m_depth > 0) {
            --m_depth;
        }
        Advance();
        Emit(TokenKind::Symbol, std::string(1U, c), at);
    }

    /// True when the line after the current '\n' begins with a word that starts a statement or a
    /// declaration - never the continuation of an expression.
    [[nodiscard]] bool NextLineStartsADeclaration() const noexcept {
        std::size_t i = m_pos + 1U;
        while (i < m_source.size() && (m_source[i] == ' ' || m_source[i] == '\t')) {
            ++i;
        }
        const std::size_t start = i;
        while (i < m_source.size() && (std::isalnum(static_cast<unsigned char>(m_source[i])) != 0 || m_source[i] == '_')) {
            ++i;
        }
        const std::string_view word = m_source.substr(start, i - start);
        constexpr std::array<std::string_view, 13> kStarters{"entity", "export", "var",   "const", "on",
                                                             "fn",     "let",    "if",    "while", "for",
                                                             "return", "wait",   "pass"};
        for (const std::string_view starter : kStarters) {
            if (starter == word) {
                return true;
            }
        }
        return false;
    }

    std::string_view m_source;
    std::vector<Diagnostic>& m_diagnostics;
    SourceLocation m_openBracket;
    std::vector<Token> m_tokens;
    std::size_t m_pos = 0U;
    std::uint32_t m_line = 1U;
    std::uint32_t m_column = 1U;
    int m_depth = 0;
};

// ---------------------------------------------------------------- parser

/// Thrown to abandon the current line; caught where parsing can resume.
struct SyntaxError final {};

constexpr std::size_t kMaximumBlueprintParseDepth = 128U;

class Parser final {
public:
    Parser(std::vector<Token> tokens, std::vector<Diagnostic>& diagnostics)
        : m_tokens(std::move(tokens)), m_diagnostics(diagnostics) {}

    File Run() {
        File file;
        SkipNewlines();
        if (IsName("entity")) {
            try {
                file.header = ParseHeader();
            } catch (const SyntaxError&) {
                Recover();
            }
        }
        while (!Is(TokenKind::End)) {
            SkipNewlines();
            if (Is(TokenKind::End)) {
                break;
            }
            try {
                ParseMember(file);
            } catch (const SyntaxError&) {
                Recover();
            }
        }
        return file;
    }

private:
    // ---- token helpers

    [[nodiscard]] const Token& Current() const noexcept { return m_tokens[m_index]; }
    [[nodiscard]] bool Is(const TokenKind kind) const noexcept { return Current().kind == kind; }
    [[nodiscard]] bool IsName(const std::string_view text) const noexcept {
        return Is(TokenKind::Name) && Current().text == text;
    }
    [[nodiscard]] bool IsSymbol(const std::string_view text) const noexcept {
        return Is(TokenKind::Symbol) && Current().text == text;
    }

    const Token& Take() noexcept {
        const Token& token = m_tokens[m_index];
        if (m_index + 1U < m_tokens.size()) {
            ++m_index;
        }
        return token;
    }

    bool Accept(const std::string_view symbolOrWord) {
        if ((Is(TokenKind::Symbol) || Is(TokenKind::Name)) && Current().text == symbolOrWord) {
            Take();
            return true;
        }
        return false;
    }

    [[noreturn]] void Fail(const std::string& message) {
        m_diagnostics.push_back({Current().at, message});
        throw SyntaxError{};
    }

    [[nodiscard]] std::string Describe() const {
        switch (Current().kind) {
            case TokenKind::Newline: return "the end of the line";
            case TokenKind::Indent: return "an indented line";
            case TokenKind::Dedent: return "the end of the block";
            case TokenKind::End: return "the end of the file";
            case TokenKind::String: return "a string";
            case TokenKind::Number: return "the number " + Current().text;
            default: return "'" + Current().text + "'";
        }
    }

    void Expect(const std::string_view symbolOrWord, const std::string_view what) {
        if (!Accept(symbolOrWord)) {
            Fail("expected " + std::string{what} + " but found " + Describe());
        }
    }

    std::string ExpectName(const std::string_view what) {
        if (!Is(TokenKind::Name) || IsKeyword(Current().text)) {
            Fail("expected " + std::string{what} + " but found " + Describe());
        }
        return Take().text;
    }

    void ExpectNewline() {
        if (!Is(TokenKind::Newline) && !Is(TokenKind::End) && !Is(TokenKind::Dedent)) {
            Fail("expected the end of the line but found " + Describe());
        }
        if (Is(TokenKind::Newline)) {
            Take();
        }
    }

    void SkipNewlines() noexcept {
        while (Is(TokenKind::Newline)) {
            Take();
        }
    }

    /// Drops the rest of the broken line, and any block that hangs off it.
    void Recover() noexcept {
        while (!Is(TokenKind::Newline) && !Is(TokenKind::End) && !Is(TokenKind::Dedent)) {
            Take();
        }
        if (Is(TokenKind::Newline)) {
            Take();
        }
        if (Is(TokenKind::Indent)) {
            int depth = 0;
            do {
                if (Is(TokenKind::Indent)) {
                    ++depth;
                } else if (Is(TokenKind::Dedent)) {
                    --depth;
                }
                Take();
            } while (depth > 0 && !Is(TokenKind::End));
        }
    }

    [[nodiscard]] static bool IsKeyword(const std::string_view word) noexcept {
        constexpr std::array<std::string_view, 24> kKeywords{
            "entity", "export", "var",   "const", "on",    "fn",     "let",   "if",
            "elif",   "else",   "while", "for",   "in",    "return", "break", "continue",
            "pass",   "wait",   "and",   "or",    "not",   "true",   "false", "none"};
        for (const std::string_view keyword : kKeywords) {
            if (keyword == word) {
                return true;
            }
        }
        return false;
    }

    // ---- declarations

    Header ParseHeader() {
        Header header;
        header.at = Take().at;
        header.name = ExpectName("the entity's name after 'entity'");
        Expect(":", "':' and the object kind it drives, as in 'entity Player : Character3D'");
        header.baseKind = ExpectName("an object kind such as Character3D");
        ExpectNewline();
        return header;
    }

    void ParseMember(File& file) {
        if (IsName("export") || IsName("var") || IsName("const")) {
            Field field;
            field.at = Current().at;
            const std::string word = Take().text;
            field.kind = word == "export" ? FieldKind::Export : word == "var" ? FieldKind::Var : FieldKind::Const;
            field.name = ExpectName("a field name");
            if (Accept(":")) {
                field.type = ParseType();
            }
            if (Accept("=")) {
                field.initializer = ParseExpr();
            } else if (field.kind == FieldKind::Const) {
                Fail("a const needs a value: 'const " + field.name + " = ...'");
            }
            if (!field.type.has_value() && !field.initializer) {
                Fail("give '" + field.name + "' a type or a starting value");
            }
            ExpectNewline();
            file.fields.push_back(std::move(field));
        } else if (IsName("on")) {
            Handler handler;
            handler.at = Take().at;
            handler.event = ExpectName("an event name, as in 'on ready:'");
            if (Accept("(")) {
                handler.params = ParseParams();
            }
            Expect(":", "':' after the event");
            handler.body = ParseBlock();
            file.handlers.push_back(std::move(handler));
        } else if (IsName("fn")) {
            Function function;
            function.at = Take().at;
            function.name = ExpectName("a function name");
            Expect("(", "'(' after the function name");
            function.params = ParseParams();
            if (Accept("->")) {
                function.returnType = ParseType();
            }
            Expect(":", "':' after the function's parameters");
            function.body = ParseBlock();
            file.functions.push_back(std::move(function));
        } else if (IsName("entity")) {
            Fail("'entity' belongs on the first line of the file, once");
        } else {
            Fail("expected 'export', 'var', 'const', 'on' or 'fn' here but found " + Describe() +
                 " - code runs inside 'on' blocks and functions");
        }
    }

    /// Reads parameters up to and including the ')'.
    std::vector<Param> ParseParams() {
        std::vector<Param> params;
        if (Accept(")")) {
            return params;
        }
        do {
            Param param;
            param.at = Current().at;
            param.name = ExpectName("a parameter name");
            if (Accept(":")) {
                param.type = ParseType();
            }
            params.push_back(std::move(param));
        } while (Accept(","));
        Expect(")", "')' after the parameters");
        return params;
    }

    TypeRef ParseType() {
        TypeRef type;
        type.at = Current().at;
        type.name = ExpectName("a type such as int, float or Object3D");
        if (Accept("[")) {
            do {
                type.arguments.push_back(ParseType());
            } while (Accept(","));
            Expect("]", "']' after the type's arguments");
        }
        return type;
    }

    // ---- statements

    Block ParseBlock() {
        if (m_recursionDepth >= kMaximumBlueprintParseDepth) {
            Fail("block is nested too deeply (maximum depth is 128)");
        }
        struct DepthScope final {
            std::size_t& depth;
            explicit DepthScope(std::size_t& d) noexcept : depth(d) { ++depth; }
            ~DepthScope() noexcept { --depth; }
        } scope{m_recursionDepth};
        if (!Is(TokenKind::Newline)) {
            Fail("start the block on a new, indented line");
        }
        Take();
        if (!Is(TokenKind::Indent)) {
            Fail("expected an indented block here - write 'pass' for an empty one");
        }
        Take();
        Block block;
        while (!Is(TokenKind::Dedent) && !Is(TokenKind::End)) {
            try {
                block.push_back(ParseStatement());
            } catch (const SyntaxError&) {
                Recover();
            }
        }
        if (Is(TokenKind::Dedent)) {
            Take();
        }
        return block;
    }

    StmtPtr ParseStatement() {
        auto stmt = std::make_unique<Stmt>();
        stmt->at = Current().at;
        if (Accept("let")) {
            stmt->kind = StmtKind::Let;
            // `let (a, b) = pair()` unpacks a tuple; the names take their types from it, so no
            // annotation is allowed there.
            if (Accept("(")) {
                do {
                    stmt->names.push_back(ExpectName("a name to unpack into"));
                } while (Accept(","));
                Expect(")", "')' after the names");
            } else {
                stmt->name = ExpectName("a name after 'let'");
                if (Accept(":")) {
                    stmt->type = ParseType();
                }
            }
            Expect("=", "'=' and a value - 'let' always starts with one");
            stmt->value = ParseExpr();
            ExpectNewline();
        } else if (Accept("if")) {
            stmt->kind = StmtKind::If;
            stmt->branches.push_back(ParseConditional("if"));
            while (IsName("elif")) {
                Take();
                stmt->branches.push_back(ParseConditional("elif"));
            }
            if (Accept("else")) {
                Expect(":", "':' after 'else'");
                stmt->elseBody = ParseBlock();
            }
        } else if (Accept("while")) {
            stmt->kind = StmtKind::While;
            stmt->branches.push_back(ParseConditional("while"));
        } else if (Accept("for")) {
            stmt->kind = StmtKind::For;
            stmt->name = ExpectName("a loop variable after 'for'");
            Expect("in", "'in' after the loop variable");
            stmt->value = ParseExpr();
            Expect(":", "':' after the loop");
            stmt->body = ParseBlock();
        } else if (Accept("return")) {
            stmt->kind = StmtKind::Return;
            if (!Is(TokenKind::Newline) && !Is(TokenKind::Dedent) && !Is(TokenKind::End)) {
                stmt->value = ParseExpr();
            }
            ExpectNewline();
        } else if (Accept("break")) {
            stmt->kind = StmtKind::Break;
            ExpectNewline();
        } else if (Accept("continue")) {
            stmt->kind = StmtKind::Continue;
            ExpectNewline();
        } else if (Accept("pass")) {
            stmt->kind = StmtKind::Pass;
            ExpectNewline();
        } else if (Accept("wait")) {
            stmt->kind = StmtKind::Wait;
            stmt->value = ParseExpr();
            ExpectNewline();
        } else {
            ExprPtr expr = ParseExpr();
            if (Is(TokenKind::Symbol) && (Current().text == "=" || Current().text == "+=" ||
                                             Current().text == "-=" || Current().text == "*=" ||
                                             Current().text == "/=")) {
                if (expr->kind != ExprKind::Name && expr->kind != ExprKind::Member &&
                    expr->kind != ExprKind::Index) {
                    Fail("only a name, a field or an element can be assigned to");
                }
                stmt->kind = StmtKind::Assign;
                stmt->op = Take().text;
                stmt->target = std::move(expr);
                stmt->value = ParseExpr();
            } else {
                stmt->kind = StmtKind::Expr;
                stmt->value = std::move(expr);
            }
            ExpectNewline();
        }
        return stmt;
    }

    ConditionalBlock ParseConditional(const std::string_view keyword) {
        ConditionalBlock branch;
        branch.condition = ParseExpr();
        Expect(":", "':' after the " + std::string{keyword} + " condition");
        branch.body = ParseBlock();
        return branch;
    }

    // ---- expressions, lowest precedence first

    ExprPtr MakeBinary(std::string op, ExprPtr left, ExprPtr right, const SourceLocation at) {
        auto expr = std::make_unique<Expr>();
        expr->kind = ExprKind::Binary;
        expr->at = at;
        expr->text = std::move(op);
        expr->operands.push_back(std::move(left));
        expr->operands.push_back(std::move(right));
        return expr;
    }

    ExprPtr ParseExpr() {
        if (m_recursionDepth >= kMaximumBlueprintParseDepth) {
            Fail("expression is nested too deeply (maximum depth is 128)");
        }
        struct DepthScope final {
            std::size_t& depth;
            explicit DepthScope(std::size_t& d) noexcept : depth(d) { ++depth; }
            ~DepthScope() noexcept { --depth; }
        } scope{m_recursionDepth};
        return ParseOr();
    }

    ExprPtr ParseOr() {
        ExprPtr left = ParseAnd();
        while (IsName("or")) {
            const SourceLocation at = Take().at;
            left = MakeBinary("or", std::move(left), ParseAnd(), at);
        }
        return left;
    }

    ExprPtr ParseAnd() {
        ExprPtr left = ParseNot();
        while (IsName("and")) {
            const SourceLocation at = Take().at;
            left = MakeBinary("and", std::move(left), ParseNot(), at);
        }
        return left;
    }

    ExprPtr ParseNot() {
        if (IsName("not")) {
            auto expr = std::make_unique<Expr>();
            expr->kind = ExprKind::Unary;
            expr->at = Take().at;
            expr->text = "not";
            expr->operands.push_back(ParseNot());
            return expr;
        }
        return ParseCompare();
    }

    ExprPtr ParseCompare() {
        ExprPtr left = ParseRange();
        while (IsSymbol("==") || IsSymbol("!=") || IsSymbol("<") || IsSymbol("<=") || IsSymbol(">") ||
               IsSymbol(">=")) {
            const Token& op = Take();
            left = MakeBinary(op.text, std::move(left), ParseRange(), op.at);
        }
        return left;
    }

    ExprPtr ParseRange() {
        ExprPtr left = ParseSum();
        if (IsSymbol("..")) {
            const SourceLocation at = Take().at;
            left = MakeBinary("..", std::move(left), ParseSum(), at);
        }
        return left;
    }

    ExprPtr ParseSum() {
        ExprPtr left = ParseProduct();
        while (IsSymbol("+") || IsSymbol("-")) {
            const Token& op = Take();
            left = MakeBinary(op.text, std::move(left), ParseProduct(), op.at);
        }
        return left;
    }

    ExprPtr ParseProduct() {
        ExprPtr left = ParseUnary();
        while (IsSymbol("*") || IsSymbol("/") || IsSymbol("%")) {
            const Token& op = Take();
            left = MakeBinary(op.text, std::move(left), ParseUnary(), op.at);
        }
        return left;
    }

    ExprPtr ParseUnary() {
        if (IsSymbol("-")) {
            auto expr = std::make_unique<Expr>();
            expr->kind = ExprKind::Unary;
            expr->at = Take().at;
            expr->text = "-";
            expr->operands.push_back(ParseUnary());
            return expr;
        }
        return ParsePostfix();
    }

    ExprPtr ParsePostfix() {
        ExprPtr expr = ParsePrimary();
        while (true) {
            if (IsSymbol(".")) {
                auto member = std::make_unique<Expr>();
                member->kind = ExprKind::Member;
                member->at = Take().at;
                member->text = ExpectName("a field or method name after '.'");
                member->operands.push_back(std::move(expr));
                expr = std::move(member);
            } else if (IsSymbol("(")) {
                auto call = std::make_unique<Expr>();
                call->kind = ExprKind::Call;
                call->at = Take().at;
                call->operands.push_back(std::move(expr));
                if (!Accept(")")) {
                    do {
                        call->operands.push_back(ParseExpr());
                    } while (Accept(","));
                    Expect(")", "')' after the arguments");
                }
                expr = std::move(call);
            } else if (IsSymbol("[")) {
                auto index = std::make_unique<Expr>();
                index->kind = ExprKind::Index;
                index->at = Take().at;
                index->operands.push_back(std::move(expr));
                index->operands.push_back(ParseExpr());
                Expect("]", "']' after the index");
                expr = std::move(index);
            } else {
                return expr;
            }
        }
    }

    ExprPtr ParsePrimary() {
        auto expr = std::make_unique<Expr>();
        expr->at = Current().at;
        if (Is(TokenKind::Number)) {
            const Token& token = Take();
            expr->kind = ExprKind::Number;
            expr->isInteger = token.isInteger;
            const auto [end, error] =
                std::from_chars(token.text.data(), token.text.data() + token.text.size(), expr->number);
            if (error != std::errc{} || end != token.text.data() + token.text.size()) {
                m_diagnostics.push_back({token.at, "'" + token.text + "' is not a number Blueprint can hold"});
            }
            // A unit right after a number on the same line: "2 s", "90 deg".
            if (Is(TokenKind::Name) && IsBlueprintNotationUnit(Current().text)) {
                expr->unit = Take().text;
                expr->isInteger = false;
            }
            return expr;
        }
        if (Is(TokenKind::String)) {
            ParseInterpolation(*expr, Take());
            return expr;
        }
        if (IsName("true") || IsName("false")) {
            expr->kind = ExprKind::Bool;
            expr->boolean = Take().text == "true";
            return expr;
        }
        if (Accept("none")) {
            expr->kind = ExprKind::None;
            return expr;
        }
        if (Accept("(")) {
            // `(x)` is still a parenthesized value; `(x, y)` and `()` are tuples. A lone pair of
            // brackets with one value and no comma stays grouped, so old scripts parse as before.
            if (Accept(")")) {
                expr->kind = ExprKind::Tuple;
                return expr;
            }
            ExprPtr inner = ParseExpr();
            if (!IsSymbol(",")) {
                Expect(")", "')'");
                return inner;
            }
            expr->kind = ExprKind::Tuple;
            expr->operands.push_back(std::move(inner));
            do {
                Take();
                if (IsSymbol(")")) {
                    break;
                }
                expr->operands.push_back(ParseExpr());
            } while (IsSymbol(","));
            Expect(")", "')' after the tuple");
            return expr;
        }
        if (Accept("[")) {
            expr->kind = ExprKind::List;
            if (!Accept("]")) {
                do {
                    expr->operands.push_back(ParseExpr());
                } while (Accept(",") && !IsSymbol("]"));
                Expect("]", "']' after the list");
            }
            return expr;
        }
        if (Accept("{")) {
            expr->kind = ExprKind::Map;
            if (!Accept("}")) {
                do {
                    expr->operands.push_back(ParseExpr());
                    Expect(":", "':' between a key and its value");
                    expr->operands.push_back(ParseExpr());
                } while (Accept(",") && !IsSymbol("}"));
                Expect("}", "'}' after the map");
            }
            return expr;
        }
        if (Is(TokenKind::Name) && !IsKeyword(Current().text)) {
            expr->kind = ExprKind::Name;
            expr->text = Take().text;
            return expr;
        }
        Fail("expected a value but found " + Describe());
    }

    /// Splits "hp: {health} / {max_health}" into literal segments and parsed expressions.
    void ParseInterpolation(Expr& expr, const Token& token) {
        expr.kind = ExprKind::String;
        std::string segment;
        const std::string& text = token.text;
        for (std::size_t i = 0U; i < text.size(); ++i) {
            if (text[i] == '{' && i + 1U < text.size() && text[i + 1U] == '{') {
                segment += '{';
                ++i;
            } else if (text[i] == '}' && i + 1U < text.size() && text[i + 1U] == '}') {
                segment += '}';
                ++i;
            } else if (text[i] == '{') {
                const std::size_t close = text.find('}', i + 1U);
                if (close == std::string::npos) {
                    m_diagnostics.push_back({token.at, "a '{' in this string is never closed - write '{{' for a brace"});
                    segment += text.substr(i);
                    break;
                }
                std::vector<Diagnostic> inner;
                std::vector<Token> tokens = Lexer(std::string_view{text}.substr(i + 1U, close - i - 1U), inner).Run();
                Parser part(std::move(tokens), inner);
                ExprPtr value;
                try {
                    part.SkipNewlines();
                    value = part.ParseExpr();
                } catch (const SyntaxError&) {
                }
                if (!inner.empty() || !value) {
                    m_diagnostics.push_back({token.at, "the part '{" + text.substr(i + 1U, close - i - 1U) +
                                                           "}' of this string is not a valid expression"});
                } else {
                    expr.segments.push_back(std::move(segment));
                    segment.clear();
                    expr.operands.push_back(std::move(value));
                }
                i = close;
            } else {
                segment += text[i];
            }
        }
        expr.segments.push_back(std::move(segment));
    }

    std::vector<Token> m_tokens;
    std::vector<Diagnostic>& m_diagnostics;
    std::size_t m_index = 0U;
    std::size_t m_recursionDepth = 0U;
};

} // namespace

bool IsBlueprintNotationUnit(const std::string_view word) noexcept {
    for (const std::string_view unit : kUnits) {
        if (unit == word) {
            return true;
        }
    }
    return false;
}

ParseResult ParseBlueprintNotation(const std::string_view source) {
    ParseResult result;
    std::vector<Token> tokens = Lexer(source, result.diagnostics).Run();
    result.file = Parser(std::move(tokens), result.diagnostics).Run();
    std::ranges::stable_sort(result.diagnostics, [](const Diagnostic& a, const Diagnostic& b) {
        return a.at.line != b.at.line ? a.at.line < b.at.line : a.at.column < b.at.column;
    });
    return result;
}

} // namespace UVE::Blueprint
