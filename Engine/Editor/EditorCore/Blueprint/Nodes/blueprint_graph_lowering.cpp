// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_graph_lowering.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

#include "blueprint_parser.h"

namespace UVE::Asset {
namespace {

namespace B = Blueprint;
using T = BlueprintPinType;
using Role = BlueprintNodeRole;
using Pin = BlueprintPinSpec;

[[nodiscard]] std::string Identifier(std::string_view text) {
    std::string out;
    for (const char c : text) {
        out.push_back(std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' ? c : '_');
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), '_');
    }
    return out;
}

/// An unwired input's typed value, written as the small expression notation the node templates use.
[[nodiscard]] std::string Literal(const T type, const std::string& text) {
    switch (type) {
        case T::Bool: return text == "true" || text == "1" ? "true" : "false";
        case T::Int: {
            try {
                return std::to_string(std::stoll(text.empty() ? "0" : text));
            } catch (...) {
                return "0";
            }
        }
        case T::Float: {
            double value = 0.0;
            try {
                value = text.empty() ? 0.0 : std::stod(text);
            } catch (...) {
            }
            std::ostringstream s;
            s.precision(9);
            s << value;
            std::string out = s.str();
            if (out.find_first_of(".eE") == std::string::npos) {
                out += ".0";
            }
            return out;
        }
        case T::Vector: {
            std::string clean;
            for (const char c : text) {
                if (c != '(' && c != ')' && c != ' ') {
                    clean.push_back(c);
                }
            }
            std::vector<std::string> parts;
            std::stringstream in(clean);
            for (std::string part; std::getline(in, part, ',');) {
                parts.push_back(part);
            }
            parts.resize(3U);
            return "vec3(" + Literal(T::Float, parts[0]) + ", " + Literal(T::Float, parts[1]) + ", " +
                   Literal(T::Float, parts[2]) + ")";
        }
        case T::String: break; // built directly, see StringLiteral
        case T::Object: return "world.self()"; // an unwired object pin means this object
        case T::ObjectArray:
        case T::IntArray:
        case T::FloatArray:
        case T::StringArray: return "[]";
        case T::Exec: break;
    }
    return "none";
}

// ---- syntax tree helpers -----------------------------------------------------------------------------

void StampExpr(B::Expr& expr, const std::uint32_t line) {
    expr.at = B::SourceLocation{line, 0U};
    for (B::ExprPtr& operand : expr.operands) {
        if (operand) StampExpr(*operand, line);
    }
}

/// Stamps the expressions a statement itself holds (not the statements nested in it).
void StampStatement(B::Stmt& stmt, const std::uint32_t line) {
    stmt.at = B::SourceLocation{line, 0U};
    if (stmt.target) StampExpr(*stmt.target, line);
    if (stmt.value) StampExpr(*stmt.value, line);
    for (B::ConditionalBlock& branch : stmt.branches) {
        if (branch.condition) StampExpr(*branch.condition, line);
    }
}

[[nodiscard]] B::ExprPtr Clone(const B::Expr& source) {
    auto copy = std::make_unique<B::Expr>();
    copy->kind = source.kind;
    copy->at = source.at;
    copy->text = source.text;
    copy->number = source.number;
    copy->isInteger = source.isInteger;
    copy->boolean = source.boolean;
    copy->unit = source.unit;
    copy->segments = source.segments;
    for (const B::ExprPtr& operand : source.operands) {
        copy->operands.push_back(operand ? Clone(*operand) : nullptr);
    }
    return copy;
}

[[nodiscard]] B::ExprPtr MakeExpr(const B::ExprKind kind) {
    auto expr = std::make_unique<B::Expr>();
    expr->kind = kind;
    return expr;
}

[[nodiscard]] B::ExprPtr NameExpr(std::string name) {
    B::ExprPtr expr = MakeExpr(B::ExprKind::Name);
    expr->text = std::move(name);
    return expr;
}

[[nodiscard]] B::ExprPtr NoneExpr() { return MakeExpr(B::ExprKind::None); }

[[nodiscard]] B::ExprPtr BoolExpr(const bool value) {
    B::ExprPtr expr = MakeExpr(B::ExprKind::Bool);
    expr->boolean = value;
    return expr;
}

[[nodiscard]] B::ExprPtr IntExpr(const long long value) {
    B::ExprPtr expr = MakeExpr(B::ExprKind::Number);
    expr->number = static_cast<double>(value);
    expr->isInteger = true;
    return expr;
}

/// A typed string value: literal text, never interpolation.
[[nodiscard]] B::ExprPtr StringLiteral(const std::string& text) {
    B::ExprPtr expr = MakeExpr(B::ExprKind::String);
    std::string clean = text;
    std::ranges::replace(clean, '\n', ' ');
    expr->segments.push_back(std::move(clean));
    return expr;
}

[[nodiscard]] B::ExprPtr BinaryExpr(std::string op, B::ExprPtr left, B::ExprPtr right) {
    B::ExprPtr expr = MakeExpr(B::ExprKind::Binary);
    expr->text = std::move(op);
    expr->operands.push_back(std::move(left));
    expr->operands.push_back(std::move(right));
    return expr;
}

[[nodiscard]] B::ExprPtr UnaryExpr(std::string op, B::ExprPtr operand) {
    B::ExprPtr expr = MakeExpr(B::ExprKind::Unary);
    expr->text = std::move(op);
    expr->operands.push_back(std::move(operand));
    return expr;
}

[[nodiscard]] B::ExprPtr MemberExpr(B::ExprPtr object, std::string member) {
    B::ExprPtr expr = MakeExpr(B::ExprKind::Member);
    expr->text = std::move(member);
    expr->operands.push_back(std::move(object));
    return expr;
}

[[nodiscard]] B::ExprPtr CallExpr(B::ExprPtr callee, std::vector<B::ExprPtr> arguments) {
    B::ExprPtr expr = MakeExpr(B::ExprKind::Call);
    expr->operands.push_back(std::move(callee));
    for (B::ExprPtr& argument : arguments) expr->operands.push_back(std::move(argument));
    return expr;
}

[[nodiscard]] B::ExprPtr IndexExpr(B::ExprPtr object, B::ExprPtr index) {
    B::ExprPtr expr = MakeExpr(B::ExprKind::Index);
    expr->operands.push_back(std::move(object));
    expr->operands.push_back(std::move(index));
    return expr;
}

[[nodiscard]] B::StmtPtr MakeStmt(const B::StmtKind kind) {
    auto stmt = std::make_unique<B::Stmt>();
    stmt->kind = kind;
    return stmt;
}

[[nodiscard]] B::StmtPtr AssignStmt(B::ExprPtr target, std::string op, B::ExprPtr value) {
    B::StmtPtr stmt = MakeStmt(B::StmtKind::Assign);
    stmt->target = std::move(target);
    stmt->op = std::move(op);
    stmt->value = std::move(value);
    return stmt;
}

[[nodiscard]] B::StmtPtr LetStmt(std::string name, B::ExprPtr value) {
    B::StmtPtr stmt = MakeStmt(B::StmtKind::Let);
    stmt->name = std::move(name);
    stmt->value = std::move(value);
    return stmt;
}

/// `list[Object3D]`, `int`: a written type.
[[nodiscard]] B::TypeRef MakeType(const std::string_view text) {
    B::TypeRef type;
    const std::size_t open = text.find('[');
    if (open == std::string_view::npos) {
        type.name = std::string{text};
        return type;
    }
    type.name = std::string{text.substr(0U, open)};
    const std::size_t close = text.rfind(']');
    if (close != std::string_view::npos && close > open) {
        type.arguments.push_back(MakeType(text.substr(open + 1U, close - open - 1U)));
    }
    return type;
}

// ---- node templates ----------------------------------------------------------------------------------

/// Parses one expression written in the node-template notation ("lerp(a, b, t)"); null when it does not parse.
[[nodiscard]] B::ExprPtr ParseExpression(const std::string& text) {
    B::ParseResult parsed = B::ParseUVScript("on __template:\n    let __value = " + text + "\n");
    if (!parsed.IsSuccess() || parsed.file.handlers.empty() || parsed.file.handlers[0].body.empty()) {
        return nullptr;
    }
    return std::move(parsed.file.handlers[0].body[0]->value);
}

/// Parses one statement written in the node-template notation ("physics.apply_impulse(v)", "x = 3").
[[nodiscard]] B::StmtPtr ParseStatementText(const std::string& text) {
    B::ParseResult parsed = B::ParseUVScript("on __template:\n    " + text + "\n");
    if (!parsed.IsSuccess() || parsed.file.handlers.empty() || parsed.file.handlers[0].body.empty()) {
        return nullptr;
    }
    return std::move(parsed.file.handlers[0].body[0]);
}

/// A template with each `{n}` made into the name `__in<n>`, to be replaced by the n-th input's expression.
[[nodiscard]] std::string WithHoles(const std::string& pattern) {
    std::string out;
    for (std::size_t i = 0U; i < pattern.size(); ++i) {
        if (pattern[i] == '{') {
            const std::size_t close = pattern.find('}', i);
            if (close != std::string::npos) {
                const std::string index = pattern.substr(i + 1U, close - i - 1U);
                if (!index.empty() && std::ranges::all_of(index, [](const char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
                    out += "__in" + index;
                    i = close;
                    continue;
                }
            }
        }
        out.push_back(pattern[i]);
    }
    return out;
}

[[nodiscard]] bool IsHole(const B::Expr& expr, std::size_t& index) {
    constexpr std::string_view kPrefix = "__in";
    if (expr.kind != B::ExprKind::Name || expr.text.size() <= kPrefix.size() || !expr.text.starts_with(kPrefix)) {
        return false;
    }
    const std::string digits = expr.text.substr(kPrefix.size());
    if (!std::ranges::all_of(digits, [](const char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
        return false;
    }
    index = static_cast<std::size_t>(std::stoul(digits));
    return true;
}

/// Replaces every hole under `slot` with a copy of the input it names (`none` for a missing one).
void FillHoles(B::ExprPtr& slot, const std::vector<B::ExprPtr>& inputs) {
    if (!slot) return;
    std::size_t index = 0U;
    if (IsHole(*slot, index)) {
        slot = index < inputs.size() && inputs[index] ? Clone(*inputs[index]) : NoneExpr();
        return;
    }
    for (B::ExprPtr& operand : slot->operands) FillHoles(operand, inputs);
}

[[nodiscard]] B::ExprPtr InstantiateExpression(const std::string& pattern, const std::vector<B::ExprPtr>& inputs) {
    B::ExprPtr expr = ParseExpression(WithHoles(pattern));
    FillHoles(expr, inputs);
    return expr;
}

[[nodiscard]] B::StmtPtr InstantiateStatement(const std::string& pattern, const std::vector<B::ExprPtr>& inputs) {
    B::StmtPtr stmt = ParseStatementText(WithHoles(pattern));
    if (stmt) {
        FillHoles(stmt->target, inputs);
        FillHoles(stmt->value, inputs);
    }
    return stmt;
}

/// The timer a Retriggerable Delay node waits on.
[[nodiscard]] std::string RetriggerTimer(const std::uint32_t id) { return "__delay" + std::to_string(id); }

class Lowerer final {
public:
    explicit Lowerer(const BlueprintAsset& blueprint) : m_bp(blueprint) {
        for (const BlueprintNode& node : m_bp.nodes) {
            m_nodes[node.id] = &node;
        }
    }

    BlueprintLowering Run(const std::string_view objectName, const std::string_view objectKind) {
        B::File& file = m_out.file;
        B::Header header;
        header.name = Identifier(objectName.empty() ? std::string_view{"Object"} : objectName);
        header.baseKind = std::string{objectKind};
        file.header = std::move(header);

        std::vector<B::Field> declared;
        for (const BlueprintVariable& variable : m_bp.variables) {
            B::Field field;
            field.kind = variable.exposed ? B::FieldKind::Export : B::FieldKind::Var;
            field.name = Identifier(variable.name);
            field.type = MakeType(BlueprintPinTypeScriptName(variable.type));
            if (variable.type == T::Object) {
                // no starting value
            } else if (variable.type == T::String) {
                field.initializer = StringLiteral(variable.defaultText);
            } else {
                field.initializer = ParseExpression(Literal(variable.type, variable.defaultText));
            }
            declared.push_back(std::move(field));
        }

        // Event chains, grouped by event so two Event Tick nodes run one after the other.
        std::map<std::string, std::vector<const BlueprintNode*>> byEvent;
        std::vector<std::string> order;
        for (const BlueprintNode& node : m_bp.nodes) {
            const BlueprintNodeType* const type = FindBlueprintNodeType(node.type);
            if (type == nullptr) {
                m_out.problems.emplace_back(node.id, "unknown node \"" + node.type + "\"");
                continue;
            }
            if (type->role == Role::Event) {
                if (!byEvent.contains(type->event)) {
                    order.push_back(type->event);
                }
                byEvent[type->event].push_back(&node);
            }
            if (type->role == Role::RetriggerableDelay) {
                m_delays.push_back(node.id);
            }
        }
        // A Retriggerable Delay's Completed runs when its timer does.
        if (!m_delays.empty() && !byEvent.contains("timer")) {
            order.push_back("timer");
            byEvent["timer"];
        }
        for (const std::string& event : order) {
            const auto& nodes = byEvent[event];
            const BlueprintNodeType* const type =
                nodes.empty() ? FindBlueprintNodeType("event_timer") : FindBlueprintNodeType(nodes.front()->type);
            const std::uint32_t at = nodes.empty() ? m_delays.front() : nodes.front()->id;
            B::Handler handler;
            handler.event = event;
            handler.at = B::SourceLocation{at, 0U};
            for (std::size_t i = 1U; i < type->outputs.size(); ++i) {
                B::Param param;
                param.name = "arg" + std::to_string(i);
                param.at = B::SourceLocation{at, 0U};
                handler.params.push_back(std::move(param));
            }
            m_blocks.push_back(&handler.body);
            m_depth = 1;
            for (const BlueprintNode* node : nodes) {
                // A widget event names its widget: only that one's (empty: every widget's).
                const std::string widget = !type->inputs.empty() && !node->inputValues.empty() ? node->inputValues[0] : std::string{};
                if (!widget.empty() && type->outputs.size() > 1U) {
                    B::StmtPtr guard = MakeStmt(B::StmtKind::If);
                    B::ConditionalBlock branch;
                    branch.condition = BinaryExpr("==", NameExpr("arg1"), StringLiteral(widget));
                    branch.body = EmitBlock(node->id, 0U);
                    guard->branches.push_back(std::move(branch));
                    StampStatement(*guard, node->id);
                    Add(std::move(guard));
                } else {
                    EmitFrom(node->id, 0U);
                }
            }
            if (event == "timer") {
                for (const std::uint32_t delay : m_delays) {
                    B::StmtPtr guard = MakeStmt(B::StmtKind::If);
                    B::ConditionalBlock branch;
                    branch.condition = BinaryExpr("==", NameExpr("arg1"), StringLiteral(RetriggerTimer(delay)));
                    branch.body = EmitBlock(delay, 0U);
                    guard->branches.push_back(std::move(branch));
                    StampStatement(*guard, delay);
                    Add(std::move(guard));
                }
            }
            if (handler.body.empty()) {
                B::StmtPtr pass = MakeStmt(B::StmtKind::Pass);
                pass->at = B::SourceLocation{at, 0U};
                handler.body.push_back(std::move(pass));
            }
            m_blocks.pop_back();
            file.handlers.push_back(std::move(handler));
        }
        for (B::Field& field : m_fields) {
            declared.push_back(std::move(field));
        }
        file.fields = std::move(declared);
        return std::move(m_out);
    }

private:
    void Add(B::StmtPtr stmt) { m_blocks.back()->push_back(std::move(stmt)); }

    /// Adds a statement made from `node`, stamping what it holds with the node's id.
    void AddFor(const std::uint32_t node, B::StmtPtr stmt) {
        if (!stmt) {
            m_out.problems.emplace_back(node, "its code could not be read");
            return;
        }
        StampStatement(*stmt, node);
        Add(std::move(stmt));
    }

    /// The field an action's value output `pin` is kept in (`ret7`, then `ret7_2` ...).
    static std::string ReturnField(const std::string& tag, const std::uint32_t pin) {
        return pin <= 1U ? "ret" + tag : "ret" + tag + "_" + std::to_string(pin);
    }

    /// A field once per node, however many exec wires lead into it.
    void FieldOnce(const std::string& name, const std::optional<std::string>& type, B::ExprPtr initializer) {
        if (!m_declared.insert(name).second) {
            return;
        }
        B::Field field;
        field.kind = B::FieldKind::Var;
        field.name = name;
        if (type) {
            field.type = MakeType(*type);
        }
        field.initializer = std::move(initializer);
        m_fields.push_back(std::move(field));
    }

    /// An unwired input's typed value (a field's starting value cannot be an expression).
    [[nodiscard]] std::string TypedValue(const BlueprintNode& node, const std::uint32_t pin) const {
        const BlueprintNodeType* const type = FindBlueprintNodeType(node.type);
        if (type == nullptr || pin >= type->inputs.size()) {
            return "none";
        }
        const std::string& typed = pin < node.inputValues.size() && !node.inputValues[pin].empty() ? node.inputValues[pin] : type->inputs[pin].defaultText;
        return type->inputs[pin].type == T::String ? "\"" + typed + "\"" : Literal(type->inputs[pin].type, typed);
    }

    [[nodiscard]] const BlueprintWire* WireFromOutput(const std::uint32_t node, const std::uint32_t pin) const {
        for (const BlueprintWire& wire : m_bp.wires) {
            if (wire.fromNode == node && wire.fromPin == pin) {
                return &wire;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const BlueprintWire* WireToInput(const std::uint32_t node, const std::uint32_t pin) const {
        for (const BlueprintWire& wire : m_bp.wires) {
            if (wire.toNode == node && wire.toPin == pin) {
                return &wire;
            }
        }
        return nullptr;
    }

    /// Follows the exec wire out of `node`'s output `pin` and adds what it leads to.
    void EmitFrom(const std::uint32_t node, const std::uint32_t pin) {
        if (const BlueprintWire* wire = WireFromOutput(node, pin)) {
            EmitNode(wire->toNode, wire->toPin);
        }
    }

    /// A block of what follows `node`'s output `pin`; an empty one holds `pass`.
    [[nodiscard]] B::Block EmitBlock(const std::uint32_t node, const std::uint32_t pin) {
        B::Block block;
        m_blocks.push_back(&block);
        ++m_depth;
        EmitFrom(node, pin);
        --m_depth;
        m_blocks.pop_back();
        if (block.empty()) {
            B::StmtPtr pass = MakeStmt(B::StmtKind::Pass);
            pass->at = B::SourceLocation{node, 0U};
            block.push_back(std::move(pass));
        }
        return block;
    }

    /// An `if`/`elif`/`else` made of one condition and the block it guards.
    [[nodiscard]] B::StmtPtr IfStmt(B::ExprPtr condition, B::Block body) {
        B::StmtPtr stmt = MakeStmt(B::StmtKind::If);
        B::ConditionalBlock branch;
        branch.condition = std::move(condition);
        branch.body = std::move(body);
        stmt->branches.push_back(std::move(branch));
        return stmt;
    }

    void EmitNode(const std::uint32_t id, const std::uint32_t entry = 0U) {
        const auto found = m_nodes.find(id);
        if (found == m_nodes.end()) {
            return;
        }
        if (m_active.contains(id) || m_depth > 64) {
            m_out.problems.emplace_back(id, "its exec wires lead back into themselves");
            return;
        }
        const BlueprintNode& node = *found->second;
        const BlueprintNodeType* const type = FindBlueprintNodeType(node.type);
        if (type == nullptr) {
            return;
        }
        m_active.insert(id);
        const std::string tag = std::to_string(id);
        switch (type->role) {
            case Role::Action: {
                std::vector<B::ExprPtr> args;
                for (std::uint32_t i = 0U; i < type->inputs.size(); ++i) {
                    args.push_back(type->inputs[i].type == T::Exec ? nullptr : InputExpr(node, i));
                }
                if (!type->outputCode.empty() && type->outputs.size() > 1U) {
                    // An action with values: each kept in a field for the nodes after it.
                    for (std::uint32_t out = 1U; out < type->outputs.size() && out - 1U < type->outputCode.size(); ++out) {
                        const T valueType = type->outputs[out].type;
                        const std::string field = ReturnField(tag, out);
                        FieldOnce(field, std::string{BlueprintPinTypeScriptName(valueType)},
                                  valueType == T::Object ? nullptr
                                                         : valueType == T::String ? StringLiteral("")
                                                                                  : ParseExpression(Literal(valueType, "")));
                        B::ExprPtr value = InstantiateExpression(type->outputCode[out - 1U], args);
                        if (!value) {
                            m_out.problems.emplace_back(id, "its code could not be read");
                            continue;
                        }
                        AddFor(id, AssignStmt(NameExpr(field), "=", std::move(value)));
                    }
                } else {
                    AddFor(id, InstantiateStatement(type->code, args));
                }
                EmitFrom(id, 0U);
                break;
            }
            case Role::SetVariable:
                AddFor(id, AssignStmt(NameExpr(Identifier(node.variable)), "=", InputExpr(node, 1U)));
                EmitFrom(id, 0U);
                break;
            case Role::Branch: {
                B::StmtPtr stmt = IfStmt(InputExpr(node, 1U), EmitBlock(id, 0U));
                if (WireFromOutput(id, 1U) != nullptr) {
                    stmt->elseBody = EmitBlock(id, 1U);
                }
                AddFor(id, std::move(stmt));
                break;
            }
            case Role::Sequence:
                for (std::uint32_t pin = 0U; pin < type->outputs.size(); ++pin) {
                    EmitFrom(id, pin);
                }
                break;
            case Role::ForLoop: {
                B::StmtPtr stmt = MakeStmt(B::StmtKind::For);
                stmt->name = "i" + tag;
                stmt->value = BinaryExpr("..", InputExpr(node, 1U), BinaryExpr("+", InputExpr(node, 2U), IntExpr(1)));
                stmt->body = EmitBlock(id, 0U);
                AddFor(id, std::move(stmt));
                EmitFrom(id, 2U);
                break;
            }
            case Role::While: {
                B::StmtPtr stmt = MakeStmt(B::StmtKind::While);
                B::ConditionalBlock branch;
                branch.condition = InputExpr(node, 1U);
                branch.body = EmitBlock(id, 0U);
                stmt->branches.push_back(std::move(branch));
                AddFor(id, std::move(stmt));
                EmitFrom(id, 1U);
                break;
            }
            case Role::DoOnce: {
                FieldOnce("once" + tag, std::nullopt, BoolExpr(false));
                B::Block body;
                m_blocks.push_back(&body);
                AddFor(id, AssignStmt(NameExpr("once" + tag), "=", BoolExpr(true)));
                ++m_depth;
                EmitFrom(id, 0U);
                --m_depth;
                m_blocks.pop_back();
                AddFor(id, IfStmt(UnaryExpr("not", NameExpr("once" + tag)), std::move(body)));
                break;
            }
            case Role::FlipFlop: {
                FieldOnce("flip" + tag, std::nullopt, BoolExpr(false));
                AddFor(id, AssignStmt(NameExpr("flip" + tag), "=", UnaryExpr("not", NameExpr("flip" + tag))));
                B::StmtPtr stmt = IfStmt(NameExpr("flip" + tag), EmitBlock(id, 0U));
                if (WireFromOutput(id, 1U) != nullptr) {
                    stmt->elseBody = EmitBlock(id, 1U);
                }
                AddFor(id, std::move(stmt));
                break;
            }
            case Role::Delay: {
                B::StmtPtr stmt = MakeStmt(B::StmtKind::Wait);
                stmt->value = InputExpr(node, 1U);
                AddFor(id, std::move(stmt));
                EmitFrom(id, 0U);
                break;
            }
            case Role::IsValid: {
                B::StmtPtr stmt = IfStmt(BinaryExpr("!=", InputExpr(node, 1U), NoneExpr()), EmitBlock(id, 0U));
                if (WireFromOutput(id, 1U) != nullptr) {
                    stmt->elseBody = EmitBlock(id, 1U);
                }
                AddFor(id, std::move(stmt));
                break;
            }
            case Role::Gate: {
                // Open unless "Start Closed": Enter passes only while open.
                FieldOnce("gate" + tag, std::nullopt, BoolExpr(TypedValue(node, 4U) != "true"));
                if (entry == 1U) {
                    AddFor(id, AssignStmt(NameExpr("gate" + tag), "=", BoolExpr(true)));
                } else if (entry == 2U) {
                    AddFor(id, AssignStmt(NameExpr("gate" + tag), "=", BoolExpr(false)));
                } else if (entry == 3U) {
                    AddFor(id, AssignStmt(NameExpr("gate" + tag), "=", UnaryExpr("not", NameExpr("gate" + tag))));
                } else {
                    AddFor(id, IfStmt(NameExpr("gate" + tag), EmitBlock(id, 0U)));
                }
                break;
            }
            case Role::DoN: {
                FieldOnce("count" + tag, std::string{"int"}, IntExpr(0));
                if (entry == 2U) {
                    AddFor(id, AssignStmt(NameExpr("count" + tag), "=", IntExpr(0)));
                } else {
                    B::Block body;
                    m_blocks.push_back(&body);
                    AddFor(id, AssignStmt(NameExpr("count" + tag), "+=", IntExpr(1)));
                    ++m_depth;
                    EmitFrom(id, 0U);
                    --m_depth;
                    m_blocks.pop_back();
                    AddFor(id, IfStmt(BinaryExpr("<", NameExpr("count" + tag), InputExpr(node, 1U)), std::move(body)));
                }
                break;
            }
            case Role::MultiGate: {
                FieldOnce("step" + tag, std::string{"int"}, IntExpr(0));
                const std::uint32_t outs = static_cast<std::uint32_t>(type->outputs.size());
                if (entry == 1U) {
                    AddFor(id, AssignStmt(NameExpr("step" + tag), "=", IntExpr(0)));
                    break;
                }
                // Taken before it runs on, so a Delay further down cannot run an output twice.
                B::Block wrap;
                wrap.push_back(AssignStmt(NameExpr("step" + tag), "=", IntExpr(0)));
                StampStatement(*wrap.back(), id);
                AddFor(id, IfStmt(BinaryExpr("and", InputExpr(node, 2U),
                                             BinaryExpr(">=", NameExpr("step" + tag), IntExpr(static_cast<long long>(outs)))),
                                  std::move(wrap)));
                AddFor(id, LetStmt("at" + tag, NameExpr("step" + tag)));
                AddFor(id, AssignStmt(NameExpr("step" + tag), "+=", IntExpr(1)));
                B::StmtPtr chain = MakeStmt(B::StmtKind::If);
                for (std::uint32_t out = 0U; out < outs; ++out) {
                    if (WireFromOutput(id, out) == nullptr) {
                        continue;
                    }
                    B::ConditionalBlock branch;
                    branch.condition = BinaryExpr("==", NameExpr("at" + tag), IntExpr(static_cast<long long>(out)));
                    branch.body = EmitBlock(id, out);
                    chain->branches.push_back(std::move(branch));
                }
                if (!chain->branches.empty()) {
                    AddFor(id, std::move(chain));
                }
                break;
            }
            case Role::RetriggerableDelay: {
                // A timer of its own: entering again starts it over; what follows runs in `on timer`.
                std::vector<B::ExprPtr> args;
                args.push_back(StringLiteral(RetriggerTimer(id)));
                args.push_back(InputExpr(node, 1U));
                args.push_back(BoolExpr(false));
                B::StmtPtr stmt = MakeStmt(B::StmtKind::Expr);
                stmt->value = CallExpr(MemberExpr(NameExpr("timers"), "set"), std::move(args));
                AddFor(id, std::move(stmt));
                break;
            }
            case Role::SwitchInt:
            case Role::SwitchString: {
                const bool text = type->role == Role::SwitchString;
                const std::uint32_t cases = static_cast<std::uint32_t>(type->outputs.size()) - 1U;
                AddFor(id, LetStmt("pick" + tag, InputExpr(node, 1U)));
                B::StmtPtr chain = MakeStmt(B::StmtKind::If);
                for (std::uint32_t out = 0U; out < cases; ++out) {
                    if (WireFromOutput(id, out) == nullptr) {
                        continue;
                    }
                    B::ConditionalBlock branch;
                    branch.condition = BinaryExpr("==", NameExpr("pick" + tag),
                                                  text ? InputExpr(node, 2U + out) : IntExpr(static_cast<long long>(out)));
                    branch.body = EmitBlock(id, out);
                    chain->branches.push_back(std::move(branch));
                }
                if (WireFromOutput(id, cases) != nullptr) {
                    if (chain->branches.empty()) {
                        EmitFrom(id, cases);
                    } else {
                        chain->elseBody = EmitBlock(id, cases);
                    }
                }
                if (!chain->branches.empty()) {
                    AddFor(id, std::move(chain));
                }
                break;
            }
            case Role::ForEach: {
                // Over a copy taken once, so the body may change the array it walks.
                const std::string items = "items" + tag;
                FieldOnce(items, std::string{BlueprintPinTypeScriptName(type->inputs[1].type)}, MakeExpr(B::ExprKind::List));
                AddFor(id, AssignStmt(NameExpr(items), "=", InputExpr(node, 1U)));
                B::StmtPtr loop = MakeStmt(B::StmtKind::For);
                loop->name = "i" + tag;
                loop->value = BinaryExpr("..", IntExpr(0), CallExpr(MemberExpr(NameExpr(items), "length"), {}));
                m_blocks.push_back(&loop->body);
                AddFor(id, LetStmt("e" + tag, IndexExpr(NameExpr(items), NameExpr("i" + tag))));
                ++m_depth;
                EmitFrom(id, 0U);
                --m_depth;
                m_blocks.pop_back();
                AddFor(id, std::move(loop));
                EmitFrom(id, 3U);
                break;
            }
            case Role::Event:
                m_out.problems.emplace_back(id, "an event cannot be wired into");
                break;
            case Role::Pure:
            case Role::GetVariable:
                m_out.problems.emplace_back(id, "has no exec input");
                break;
        }
        m_active.erase(id);
    }

    /// The expression for input `pin` of `node`: the wired output's, else the typed value.
    [[nodiscard]] B::ExprPtr InputExpr(const BlueprintNode& node, const std::uint32_t pin) {
        const BlueprintNodePins pins = ResolveBlueprintNodePins(m_bp, node);
        if (const BlueprintWire* wire = WireToInput(node.id, pin)) {
            return OutputExpr(wire->fromNode, wire->fromPin);
        }
        if (pin >= pins.inputs.size()) {
            return NoneExpr();
        }
        const std::string& typed = pin < node.inputValues.size() && !node.inputValues[pin].empty() ? node.inputValues[pin] : pins.inputs[pin].defaultText;
        if (pins.inputs[pin].type == T::String) {
            return StringLiteral(typed);
        }
        B::ExprPtr value = ParseExpression(Literal(pins.inputs[pin].type, typed));
        return value ? std::move(value) : NoneExpr();
    }

    [[nodiscard]] B::ExprPtr OutputExpr(const std::uint32_t id, const std::uint32_t pin) {
        const auto found = m_nodes.find(id);
        if (found == m_nodes.end()) {
            return NoneExpr();
        }
        const BlueprintNode& node = *found->second;
        const BlueprintNodeType* const type = FindBlueprintNodeType(node.type);
        if (type == nullptr) {
            return NoneExpr();
        }
        const std::string tag = std::to_string(id);
        switch (type->role) {
            case Role::Event: return NameExpr("arg" + std::to_string(pin));
            case Role::Action: return type->outputCode.empty() ? NoneExpr() : NameExpr(ReturnField(tag, pin));
            case Role::DoN: return NameExpr("count" + tag);
            case Role::ForEach: return NameExpr(pin == 1U ? "e" + tag : "i" + tag);
            case Role::GetVariable:
            case Role::SetVariable: return NameExpr(Identifier(node.variable));
            case Role::ForLoop: return NameExpr("i" + tag);
            case Role::FlipFlop: return NameExpr("flip" + tag);
            case Role::Pure: {
                if (m_pureDepth > 64 || pin >= type->outputCode.size()) {
                    m_out.problems.emplace_back(id, "its data wires lead back into themselves");
                    return NoneExpr();
                }
                ++m_pureDepth;
                std::vector<B::ExprPtr> args;
                for (std::uint32_t i = 0U; i < type->inputs.size(); ++i) {
                    args.push_back(InputExpr(node, i));
                }
                --m_pureDepth;
                B::ExprPtr value = InstantiateExpression(type->outputCode[pin], args);
                return value ? std::move(value) : NoneExpr();
            }
            default: return NoneExpr();
        }
    }

    const BlueprintAsset& m_bp;
    std::map<std::uint32_t, const BlueprintNode*> m_nodes;
    std::vector<B::Block*> m_blocks;
    std::vector<B::Field> m_fields;
    std::set<std::uint32_t> m_active;
    std::set<std::string> m_declared;
    std::vector<std::uint32_t> m_delays;
    int m_pureDepth = 0;
    int m_depth = 0;
    BlueprintLowering m_out;
};

} // namespace

std::string_view BlueprintPinTypeScriptName(const BlueprintPinType type) noexcept {
    switch (type) {
        case BlueprintPinType::Bool: return "bool";
        case BlueprintPinType::Int: return "int";
        case BlueprintPinType::Float: return "float";
        case BlueprintPinType::Vector: return "vec3";
        case BlueprintPinType::String: return "str";
        case BlueprintPinType::Object: return "Object3D";
        case BlueprintPinType::ObjectArray: return "list[Object3D]";
        case BlueprintPinType::IntArray: return "list[int]";
        case BlueprintPinType::FloatArray: return "list[float]";
        case BlueprintPinType::StringArray: return "list[str]";
        case BlueprintPinType::Exec: break;
    }
    return "none";
}

BlueprintNodePins ResolveBlueprintNodePins(const BlueprintAsset& blueprint, const BlueprintNode& node) {
    BlueprintNodePins pins;
    const BlueprintNodeType* const type = FindBlueprintNodeType(node.type);
    if (type == nullptr) {
        return pins;
    }
    pins.inputs = type->inputs;
    pins.outputs = type->outputs;
    if (type->role == BlueprintNodeRole::GetVariable || type->role == BlueprintNodeRole::SetVariable) {
        const auto variable = std::find_if(blueprint.variables.begin(), blueprint.variables.end(),
                                           [&node](const BlueprintVariable& v) { return v.name == node.variable; });
        const BlueprintPinType value = variable != blueprint.variables.end() ? variable->type : T::Float;
        const std::string name = variable != blueprint.variables.end() ? variable->name : node.variable;
        for (Pin& pin : pins.inputs) {
            if (pin.type != T::Exec) {
                pin.type = value;
                pin.name = name;
                pin.defaultText = variable != blueprint.variables.end() ? variable->defaultText : std::string{};
            }
        }
        for (Pin& pin : pins.outputs) {
            if (pin.type != T::Exec) {
                pin.type = value;
                pin.name = name;
            }
        }
    }
    return pins;
}

BlueprintLowering LowerBlueprintGraph(const BlueprintAsset& blueprint, const std::string_view objectName,
                                      const std::string_view objectKind) {
    Lowerer lowerer(blueprint);
    return lowerer.Run(objectName, objectKind);
}

BlueprintCompilation CompileBlueprintGraph(const BlueprintAsset& blueprint, const std::string_view objectName,
                                           const std::string_view objectKind, const Blueprint::UVScriptHost& host) {
    BlueprintLowering lowered = LowerBlueprintGraph(blueprint, objectName, objectKind);
    BlueprintCompilation compilation;
    compilation.problems = std::move(lowered.problems);
    compilation.result = Blueprint::CompileUVScript(lowered.file, host);
    return compilation;
}

} // namespace UVE::Asset
