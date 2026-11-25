// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_compiler.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <functional>
#include <numbers>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "blueprint_parser.h"
#include "blueprint_program.h"

namespace UVE::Blueprint {
namespace {

using Kind = Type::Kind;

struct Local final {
    std::uint32_t slot = 0U;
    Type type;
};

struct FunctionSignature final {
    std::uint32_t index = 0U;
    std::vector<Type> params;
    Type result;
};

struct BuiltinInfo final {
    std::string_view name;
    Builtin id;
    std::size_t arity;
};

constexpr std::array<BuiltinInfo, 20> kBuiltins{{
    {"print", Builtin::Print, 1U},       {"sqrt", Builtin::Sqrt, 1U},
    {"abs", Builtin::Abs, 1U},           {"min", Builtin::Min, 2U},
    {"max", Builtin::Max, 2U},           {"clamp", Builtin::Clamp, 3U},
    {"lerp", Builtin::Lerp, 3U},         {"sin", Builtin::Sin, 1U},
    {"cos", Builtin::Cos, 1U},           {"floor", Builtin::Floor, 1U},
    {"vec3", Builtin::Vec3, 3U},         {"int", Builtin::Int, 1U},
    {"float", Builtin::Float, 1U},       {"str", Builtin::Str, 1U},
    {"length", Builtin::Length, 1U},     {"normalize", Builtin::Normalize, 1U},
    {"push", Builtin::Push, 2U},         {"keys", Builtin::Keys, 1U},
    {"contains", Builtin::Contains, 2U}, {"remove", Builtin::Remove, 2U},
}};

[[nodiscard]] const BuiltinInfo* FindBuiltin(const std::string_view name) noexcept {
    for (const BuiltinInfo& builtin : kBuiltins) {
        if (builtin.name == name) {
            return &builtin;
        }
    }
    return nullptr;
}

/// "input.axis" for `input.axis`, empty for anything that is not a plain dotted name.
[[nodiscard]] std::string DottedPath(const Expr& expr) {
    if (expr.kind == ExprKind::Name) {
        return expr.text;
    }
    if (expr.kind == ExprKind::Member) {
        const std::string base = DottedPath(*expr.operands[0]);
        return base.empty() ? std::string{} : base + "." + expr.text;
    }
    return {};
}

[[nodiscard]] bool EndsWithReturn(const Block& block) {
    if (block.empty()) {
        return false;
    }
    const Stmt& last = *block.back();
    if (last.kind == StmtKind::Return) {
        return true;
    }
    if (last.kind == StmtKind::If && !last.elseBody.empty()) {
        return std::ranges::all_of(last.branches, [](const ConditionalBlock& b) { return EndsWithReturn(b.body); }) &&
               EndsWithReturn(last.elseBody);
    }
    return false;
}

class Compiler final {
public:
    Compiler(const UVScriptHost& host, std::vector<Diagnostic>& diagnostics, Program& program)
        : m_host(host), m_diagnostics(diagnostics), m_program(program) {}

    void Run(const File& file) {
        DeclareFields(file);
        DeclareFunctions(file);
        CompileFieldInitializers(file);
        for (const Function& function : file.functions) {
            CompileFunction(function);
        }
        for (const Handler& handler : file.handlers) {
            CompileHandler(handler);
        }
    }

private:
    // ------------------------------------------------------------ diagnostics and types

    void Error(const SourceLocation at, std::string message) {
        m_diagnostics.push_back({at, std::move(message)});
    }

    std::optional<Type> ResolveType(const TypeRef& ref) {
        if (ref.name == "list" || ref.name == "map" || ref.name == "tuple") {
            const bool arityOk = ref.name == "list" ? ref.arguments.size() == 1U
                                 : ref.name == "map" ? ref.arguments.size() == 2U
                                                     : !ref.arguments.empty();
            if (!arityOk) {
                Error(ref.at, ref.name == "list"   ? "write the item type, as in 'list[int]'"
                                : ref.name == "map" ? "write the key and value types, as in 'map[str, int]'"
                                                    : "write the item types, as in 'tuple[int, str]'");
                return std::nullopt;
            }
            std::vector<Type> elements;
            for (const TypeRef& argument : ref.arguments) {
                std::optional<Type> element = ResolveType(argument);
                if (!element.has_value()) {
                    return std::nullopt;
                }
                elements.push_back(*element);
            }
            if (ref.name == "list") {
                return Type::List(std::move(elements.front()));
            }
            if (ref.name == "map") {
                const Kind elementKind = elements.front().kind;
                if (elementKind != Kind::Int && elementKind != Kind::Str && elementKind != Kind::Bool &&
                    elementKind != Kind::Error) {
                    Error(ref.arguments.front().at, "map keys are int, str or bool, not " + elements.front().Name());
                    return std::nullopt;
                }
                return Type::Map(std::move(elements[0]), std::move(elements[1]));
            }
            return Type::Tuple(std::move(elements));
        }
        if (!ref.arguments.empty()) {
            Error(ref.at, "only list[...], map[...] and tuple[...] take types inside brackets");
            return std::nullopt;
        }
        if (ref.name == "int") return Type::Int();
        if (ref.name == "float") return Type::Float();
        if (ref.name == "bool") return Type::Bool();
        if (ref.name == "str") return Type::Str();
        if (ref.name == "vec3") return Type::Vec3();
        if (!ref.name.empty() && std::isupper(static_cast<unsigned char>(ref.name.front())) != 0) {
            return Type::Object(ref.name);
        }
        Error(ref.at, "'" + ref.name + "' is not a type - use int, float, bool, str, vec3 or an object kind");
        return std::nullopt;
    }

    /// Checks that `from` can be stored where `to` is expected, converting int to float. Emits the
    /// conversion right after the value that is on top of the stack.
    bool Coerce(const Type& from, const Type& to, const SourceLocation at, const std::string& what) {
        if (from.kind == Kind::Error || to.kind == Kind::Error) {
            return true;
        }
        if (from.kind == to.kind) {
            // Element types must match exactly: a list[int] is not a list[float], and the checker
            // never converts items behind the program's back.
            if (from.IsCollection() && from != to) {
                Error(at, what + " needs " + to.Name() + " but this is " + from.Name());
                return false;
            }
            return true;
        }
        if (from.kind == Kind::Int && to.kind == Kind::Float) {
            Emit(Op::ToFloat, 0, 0, at);
            return true;
        }
        Error(at, what + " needs " + to.Name() + " but this is " + from.Name());
        return false;
    }

    // ------------------------------------------------------------ emitting

    std::size_t Emit(const Op op, const std::int32_t a, const std::int32_t b, const SourceLocation at) {
        m_chunk->code.push_back({op, a, b, at.line});
        return m_chunk->code.size() - 1U;
    }

    void Patch(const std::size_t instruction) {
        m_chunk->code[instruction].a = static_cast<std::int32_t>(m_chunk->code.size());
    }

    std::int32_t Constant(Value value) {
        m_program.constants.push_back(std::move(value));
        return static_cast<std::int32_t>(m_program.constants.size() - 1U);
    }

    /// A host property or function name, remembered with its type so native code can use it.
    std::int32_t HostName(const std::string& name, const Type& type) {
        const std::int32_t index = Constant(name);
        m_program.hostTypes.emplace_back(static_cast<std::uint32_t>(index), type);
        return index;
    }

    void PushValue(Value value, const SourceLocation at) { Emit(Op::PushConst, Constant(std::move(value)), 0, at); }

    [[nodiscard]] static Value DefaultValue(const Type& type) {
        switch (type.kind) {
            case Kind::Bool: return false;
            case Kind::Int: return std::int64_t{0};
            case Kind::Float: return 0.0;
            case Kind::Str: return std::string{};
            case Kind::Vec3: return Vec3Value{};
            case Kind::Object: return ObjectRef{};
            case Kind::List: return MakeListValue({});
            case Kind::Map: return MakeMapValue({});
            case Kind::Tuple: return MakeTupleValue({});
            default: return std::monostate{};
        }
    }

    // ------------------------------------------------------------ declarations

    void DeclareFields(const File& file) {
        for (const Field& field : file.fields) {
            if (m_fields.contains(field.name) || m_host.DescribeProperty(field.name).has_value()) {
                Error(field.at, "'" + field.name + "' is already declared" +
                                    (m_fields.contains(field.name) ? "" : " - the object has a property by that name"));
                continue;
            }
            Type type = Type::Error();
            if (field.type.has_value()) {
                type = ResolveType(*field.type).value_or(Type::Error());
            } else {
                type = LiteralTypeOf(*field.initializer);
            }
            m_fields.emplace(field.name, static_cast<std::uint32_t>(m_program.fields.size()));
            m_program.fields.push_back({field.name, type, field.kind});
        }
    }

    /// A field without a written type takes its initializer's type; a field initializer is checked
    /// fully later, so this only needs to be right for well-typed ones.
    Type LiteralTypeOf(const Expr& expr) {
        Chunk scratch;
        Chunk* saved = m_chunk;
        m_chunk = &scratch;
        const std::size_t diagnostics = m_diagnostics.size();
        const std::size_t constants = m_program.constants.size();
        const Type type = CompileExpr(expr);
        m_diagnostics.resize(diagnostics);
        m_program.constants.resize(constants);
        m_chunk = saved;
        return type;
    }

    void DeclareFunctions(const File& file) {
        for (const Function& function : file.functions) {
            if (m_functions.contains(function.name) || FindBuiltin(function.name) != nullptr) {
                Error(function.at, "a function named '" + function.name + "' already exists");
                continue;
            }
            FunctionSignature signature;
            for (const Param& param : function.params) {
                if (!param.type.has_value()) {
                    Error(param.at, "give '" + param.name + "' a type, as in '" + param.name + ": float'");
                    signature.params.push_back(Type::Error());
                } else {
                    signature.params.push_back(ResolveType(*param.type).value_or(Type::Error()));
                }
            }
            signature.result = function.returnType.has_value()
                                   ? ResolveType(*function.returnType).value_or(Type::Error())
                                   : Type::None();
            signature.index = static_cast<std::uint32_t>(m_program.functions.size());
            m_program.functions.push_back({function.name, static_cast<std::uint32_t>(function.params.size()), 0U, {}, {}, signature.result});
            m_functions.emplace(function.name, std::move(signature));
        }
    }

    // ------------------------------------------------------------ bodies

    void BeginChunk(Chunk& chunk) {
        m_chunk = &chunk;
        m_scopes.clear();
        m_scopes.emplace_back();
        m_nextSlot = 0U;
        m_loops.clear();
    }

    void EndChunk(const SourceLocation at) {
        Emit(Op::ReturnNone, 0, 0, at);
        m_chunk->localCount = std::max(m_chunk->localCount, m_nextSlot);
    }

    std::uint32_t DeclareLocal(const std::string& name, Type type, const SourceLocation at) {
        if (m_scopes.back().contains(name)) {
            Error(at, "'" + name + "' is already declared in this block");
        }
        const std::uint32_t slot = m_nextSlot++;
        SetLocalType(slot, type);
        m_scopes.back()[name] = {slot, std::move(type)};
        m_chunk->localCount = std::max(m_chunk->localCount, m_nextSlot);
        return slot;
    }

    /// Slots are never reused within a chunk, so each has exactly one type.
    void SetLocalType(const std::uint32_t slot, const Type& type) {
        if (m_chunk->localTypes.size() <= slot) {
            m_chunk->localTypes.resize(slot + 1U);
        }
        m_chunk->localTypes[slot] = type;
    }

    [[nodiscard]] const Local* FindLocal(const std::string& name) const {
        for (auto scope = m_scopes.rbegin(); scope != m_scopes.rend(); ++scope) {
            if (const auto it = scope->find(name); it != scope->end()) {
                return &it->second;
            }
        }
        return nullptr;
    }

    /// An empty `[]` or `{}` takes its type from an annotated target: `let xs: list[int] = []`.
    /// Emits the empty build and answers the target type, or nothing when this is not the case.
    std::optional<Type> EmptyCollectionTarget(const Expr& expr, const Type& target) {
        const bool list = expr.kind == ExprKind::List && target.kind == Kind::List && target.elements.size() == 1U;
        const bool map = expr.kind == ExprKind::Map && target.kind == Kind::Map && target.elements.size() == 2U;
        if ((!list && !map) || !expr.operands.empty()) {
            return std::nullopt;
        }
        Emit(list ? Op::BuildList : Op::BuildMap, 0, 0, expr.at);
        return target;
    }

    /// Compiles `expr` where `target` is wanted, converting int to float - and letting an empty
    /// `[]` or `{}` take the target's type.
    Type CompileCoerced(const Expr& expr, const Type& target, const SourceLocation at,
                           const std::string& what) {
        if (const std::optional<Type> empty = EmptyCollectionTarget(expr, target)) {
            return *empty;
        }
        // An empty literal aimed at a broken type has nothing useful to add to the first error.
        const bool untypedEmpty = target.kind == Kind::Error && (expr.kind == ExprKind::List || expr.kind == ExprKind::Map) &&
                                  expr.operands.empty();
        if (untypedEmpty) {
            PushValue(std::monostate{}, expr.at);
            return Type::Error();
        }
        const Type type = CompileExpr(expr);
        Coerce(type, target, at, what);
        return type;
    }

    void CompileFieldInitializers(const File& file) {
        BeginChunk(m_program.init);
        m_inHandler = false;
        m_result = Type::None();
        for (const Field& field : file.fields) {
            const auto it = m_fields.find(field.name);
            if (it == m_fields.end()) {
                continue;
            }
            const Type& type = m_program.fields[it->second].type;
            if (field.initializer) {
                CompileCoerced(*field.initializer, type, field.initializer->at, "'" + field.name + "'");
            } else {
                PushValue(DefaultValue(type), field.at);
            }
            Emit(Op::StoreField, static_cast<std::int32_t>(it->second), 0, field.at);
        }
        EndChunk({});
    }

    void CompileFunction(const Function& function) {
        const auto it = m_functions.find(function.name);
        if (it == m_functions.end()) {
            return;
        }
        const FunctionSignature& signature = it->second;
        BeginChunk(m_program.functions[signature.index]);
        m_inHandler = false;
        m_result = signature.result;
        for (std::size_t i = 0U; i < function.params.size(); ++i) {
            DeclareLocal(function.params[i].name, signature.params[i], function.params[i].at);
        }
        CompileBlock(function.body);
        if (signature.result.kind != Kind::None && signature.result.kind != Kind::Error &&
            !EndsWithReturn(function.body)) {
            Error(function.at, "'" + function.name + "' can reach its end without returning a " + signature.result.Name());
        }
        EndChunk(function.at);
    }

    void CompileHandler(const Handler& handler) {
        const std::optional<std::vector<Type>> params = m_host.DescribeEvent(handler.event);
        if (!params.has_value()) {
            Error(handler.at, "this object has no event '" + handler.event + "'");
            return;
        }
        if (std::ranges::any_of(m_program.handlers, [&](const auto& h) { return h.first == handler.event; })) {
            Error(handler.at, "there is already an 'on " + handler.event + "' block");
            return;
        }
        if (handler.params.size() != params->size()) {
            Error(handler.at, "'" + handler.event + "' gives " + std::to_string(params->size()) + " value(s), not " +
                                  std::to_string(handler.params.size()));
            return;
        }
        const auto index = static_cast<std::uint32_t>(m_program.functions.size());
        m_program.functions.push_back({"on " + handler.event, static_cast<std::uint32_t>(params->size()), 0U, {}, {}, Type::None()});
        m_program.handlers.emplace_back(handler.event, index);
        BeginChunk(m_program.functions.back());
        m_inHandler = true;
        m_result = Type::None();
        for (std::size_t i = 0U; i < handler.params.size(); ++i) {
            const Param& param = handler.params[i];
            if (param.type.has_value()) {
                const std::optional<Type> written = ResolveType(*param.type);
                if (written.has_value() && written->kind != (*params)[i].kind) {
                    Error(param.at, "'" + param.name + "' is a " + (*params)[i].Name() + " here, not " + written->Name());
                }
            }
            DeclareLocal(param.name, (*params)[i], param.at);
        }
        CompileBlock(handler.body);
        EndChunk(handler.at);
    }

    // ------------------------------------------------------------ statements

    void CompileBlock(const Block& block) {
        m_scopes.emplace_back();
        for (const StmtPtr& stmt : block) {
            CompileStatement(*stmt);
        }
        m_scopes.pop_back();
    }

    void CompileStatement(const Stmt& stmt) {
        switch (stmt.kind) {
            case StmtKind::Let: {
                if (!stmt.names.empty()) {
                    CompileUnpack(stmt);
                    break;
                }
                Type type = Type::Error();
                if (stmt.type.has_value()) {
                    const std::optional<Type> written = ResolveType(*stmt.type);
                    if (written.has_value()) {
                        static_cast<void>(CompileCoerced(*stmt.value, *written, stmt.value->at, "'" + stmt.name + "'"));
                        type = *written;
                    } else {
                        type = CompileCoerced(*stmt.value, Type::Error(), stmt.value->at, "");
                    }
                } else {
                    type = CompileExpr(*stmt.value);
                    if (type.kind == Kind::None) {
                        Error(stmt.at, "'" + stmt.name + "' would be none - give it a value");
                    }
                }
                Emit(Op::StoreLocal, static_cast<std::int32_t>(DeclareLocal(stmt.name, type, stmt.at)), 0, stmt.at);
                break;
            }
            case StmtKind::Assign:
                CompileAssign(stmt);
                break;
            case StmtKind::Expr:
                // `xs.push(v)` on its own line grows the variable's list; anywhere else it is a
                // plain call whose result must be used.
                if (CompileListSugar(*stmt.value, stmt.at)) {
                    break;
                }
                if (stmt.value->kind != ExprKind::Call) {
                    Error(stmt.at, "this line works out a value and then drops it - did you mean to assign it?");
                }
                CompileExpr(*stmt.value);
                Emit(Op::Pop, 0, 0, stmt.at);
                break;
            case StmtKind::If: {
                std::vector<std::size_t> exits;
                for (const ConditionalBlock& branch : stmt.branches) {
                    CompileCondition(*branch.condition);
                    const std::size_t skip = Emit(Op::JumpIfFalse, 0, 0, branch.condition->at);
                    CompileBlock(branch.body);
                    exits.push_back(Emit(Op::Jump, 0, 0, stmt.at));
                    Patch(skip);
                }
                CompileBlock(stmt.elseBody);
                for (const std::size_t exit : exits) {
                    Patch(exit);
                }
                break;
            }
            case StmtKind::While: {
                const std::size_t top = m_chunk->code.size();
                CompileCondition(*stmt.branches[0].condition);
                const std::size_t exit = Emit(Op::JumpIfFalse, 0, 0, stmt.at);
                m_loops.push_back({top, {}, {}});
                CompileBlock(stmt.branches[0].body);
                Emit(Op::Jump, static_cast<std::int32_t>(top), 0, stmt.at);
                Patch(exit);
                for (const std::size_t brk : m_loops.back().breaks) {
                    Patch(brk);
                }
                m_loops.pop_back();
                break;
            }
            case StmtKind::For:
                CompileFor(stmt);
                break;
            case StmtKind::Return:
                if (m_inHandler && stmt.value) {
                    Error(stmt.at, "an 'on' block cannot return a value");
                } else if (stmt.value) {
                    if (m_result.kind == Kind::None) {
                        const Type type = CompileExpr(*stmt.value);
                        Error(stmt.at, "this function returns nothing - add '-> " + type.Name() + "' to return a value");
                    } else {
                        CompileCoerced(*stmt.value, m_result, stmt.value->at, "the return value");
                    }
                    Emit(Op::Return, 0, 0, stmt.at);
                } else {
                    if (m_result.kind != Kind::None && m_result.kind != Kind::Error) {
                        Error(stmt.at, "return a " + m_result.Name() + " here");
                    }
                    Emit(Op::ReturnNone, 0, 0, stmt.at);
                }
                break;
            case StmtKind::Break:
            case StmtKind::Continue:
                if (m_loops.empty()) {
                    Error(stmt.at, std::string{"'"} + (stmt.kind == StmtKind::Break ? "break" : "continue") +
                                       "' only works inside a loop");
                } else if (stmt.kind == StmtKind::Break) {
                    m_loops.back().breaks.push_back(Emit(Op::Jump, 0, 0, stmt.at));
                } else if (m_loops.back().continueTarget != kUnknownTarget) {
                    Emit(Op::Jump, static_cast<std::int32_t>(m_loops.back().continueTarget), 0, stmt.at);
                } else {
                    m_loops.back().continues.push_back(Emit(Op::Jump, 0, 0, stmt.at));
                }
                break;
            case StmtKind::Pass:
                break;
            case StmtKind::Wait: {
                if (!m_inHandler) {
                    Error(stmt.at, "'wait' only works inside an 'on' block");
                }
                const Type type = CompileExpr(*stmt.value);
                if (!type.IsNumeric() && type.kind != Kind::Error) {
                    Error(stmt.value->at, "'wait' needs a time, as in 'wait 0.5 s'");
                }
                Coerce(type, Type::Float(), stmt.value->at, "'wait'");
                Emit(Op::Wait, 0, 0, stmt.at);
                break;
            }
        }
    }

    void CompileCondition(const Expr& condition) {
        const Type type = CompileExpr(condition);
        if (type.kind != Kind::Bool && type.kind != Kind::Error) {
            Error(condition.at, "a condition must be true or false, not " + type.Name());
        }
    }

    /// A local slot no source name can see, for loop counters and temporaries.
    std::uint32_t HiddenSlot(Type type) {
        const std::uint32_t slot = m_nextSlot++;
        SetLocalType(slot, std::move(type));
        m_chunk->localCount = std::max(m_chunk->localCount, m_nextSlot);
        return slot;
    }

    /// `let (a, b) = pair()`: the tuple's items land in the names, each with its own type.
    void CompileUnpack(const Stmt& stmt) {
        const Type type = CompileExpr(*stmt.value);
        std::vector<Type> elements = type.kind == Kind::Tuple ? type.elements : std::vector<Type>{};
        if (type.kind != Kind::Error && (type.kind != Kind::Tuple || elements.size() != stmt.names.size())) {
            Error(stmt.at, type.kind == Kind::Tuple
                                    ? "this " + type.Name() + " unpacks into " + std::to_string(elements.size()) +
                                          ", not " + std::to_string(stmt.names.size())
                                    : "only a tuple unpacks into names, not " + type.Name());
        }
        if (type.kind != Kind::Tuple || elements.size() != stmt.names.size()) {
            elements.assign(stmt.names.size(), Type::Error());
        }
        // Unpack pushes the first item first, so the last name is stored first.
        Emit(Op::Unpack, static_cast<std::int32_t>(stmt.names.size()), 0, stmt.at);
        std::vector<std::uint32_t> slots;
        for (std::size_t i = 0U; i < stmt.names.size(); ++i) {
            if (elements[i].kind == Kind::None) {
                Error(stmt.at, "'" + stmt.names[i] + "' would be none - give it a value");
            }
            slots.push_back(DeclareLocal(stmt.names[i], elements[i], stmt.at));
        }
        for (std::size_t i = slots.size(); i-- > 0U;) {
            Emit(Op::StoreLocal, static_cast<std::int32_t>(slots[i]), 0, stmt.at);
        }
    }

    /// `xs.push(v)`, `xs.remove(i)` and `m.remove(k)` as whole statements: the call's result is
    /// written back to the variable, so the line reads as changing the list in place. True when
    /// the expression had that shape (whether it compiled or not).
    bool CompileListSugar(const Expr& expr, const SourceLocation at) {
        if (expr.kind != ExprKind::Call || expr.operands[0]->kind != ExprKind::Member) {
            return false;
        }
        const Expr& member = *expr.operands[0];
        const bool push = member.text == "push";
        if (!push && member.text != "remove") {
            return false;
        }
        const Type base = PeekType(*member.operands[0]);
        const bool list = base.kind == Kind::List;
        const bool map = base.kind == Kind::Map;
        if ((!list && !map) || (map && push)) {
            return false;
        }
        if (expr.operands.size() != 2U) {
            Error(at, push ? "write 'xs.push(value)' - push adds one value"
                           : "write 'xs.remove(index)' - remove drops one item");
            return true;
        }
        const std::optional<Place> place = ResolvePlace(*member.operands[0]);
        if (!place.has_value()) {
            return true;
        }
        Emit(place->load, place->operand, 0, at);
        const std::vector<Type>& elements = place->type.elements;
        if (push && !elements.empty()) {
            CompileCoerced(*expr.operands[1], elements[0], expr.operands[1]->at, "'push'");
        } else if (list) {
            CompileCoerced(*expr.operands[1], Type::Int(), expr.operands[1]->at, "'remove'");
        } else if (!elements.empty()) {
            CompileCoerced(*expr.operands[1], elements[0], expr.operands[1]->at, "'remove'");
        } else {
            CompileCoerced(*expr.operands[1], Type::Error(), expr.operands[1]->at, "");
        }
        Emit(Op::CallBuiltin, static_cast<std::int32_t>(push ? Builtin::Push : Builtin::Remove), 2, at);
        Emit(place->store, place->operand, 0, at);
        return true;
    }

    /// `for x in xs` walks a list by index; `for k in m` walks a map's keys. The container is
    /// copied into a hidden slot first, so changing the variable inside the loop never disturbs
    /// the walk.
    void CompileForCollection(const Stmt& stmt) {
        const Type container = PeekType(*stmt.value);
        if (container.kind != Kind::List && container.kind != Kind::Map && container.kind != Kind::Error) {
            Error(stmt.value->at, container.kind == Kind::Tuple
                                           ? "a tuple unpacks with 'let (a, b) = ...' instead of a loop"
                                           : "'for' walks a range, a list or a map's keys - not " + container.Name());
            return;
        }
        const bool map = container.kind == Kind::Map;
        Type item = Type::Error();
        if (!container.elements.empty()) {
            item = container.elements[0];
        }
        m_scopes.emplace_back();
        CompileExpr(*stmt.value);
        if (map) {
            Emit(Op::CallBuiltin, static_cast<std::int32_t>(Builtin::Keys), 1, stmt.at);
        }
        const std::uint32_t box = HiddenSlot(Type::List(item));
        Emit(Op::StoreLocal, static_cast<std::int32_t>(box), 0, stmt.at);
        Emit(Op::LoadLocal, static_cast<std::int32_t>(box), 0, stmt.at);
        Emit(Op::CallBuiltin, static_cast<std::int32_t>(Builtin::Length), 1, stmt.at);
        const std::uint32_t limit = HiddenSlot(Type::Int());
        Emit(Op::StoreLocal, static_cast<std::int32_t>(limit), 0, stmt.at);
        PushValue(std::int64_t{0}, stmt.at);
        const std::uint32_t counter = HiddenSlot(Type::Int());
        Emit(Op::StoreLocal, static_cast<std::int32_t>(counter), 0, stmt.at);
        const std::uint32_t slot = DeclareLocal(stmt.name, item, stmt.at);
        const std::size_t top = m_chunk->code.size();
        Emit(Op::LoadLocal, static_cast<std::int32_t>(counter), 0, stmt.at);
        Emit(Op::LoadLocal, static_cast<std::int32_t>(limit), 0, stmt.at);
        Emit(Op::Lt, 0, 0, stmt.at);
        const std::size_t exit = Emit(Op::JumpIfFalse, 0, 0, stmt.at);
        m_loops.push_back({kUnknownTarget, {}, {}});
        Emit(Op::LoadLocal, static_cast<std::int32_t>(box), 0, stmt.at);
        Emit(Op::LoadLocal, static_cast<std::int32_t>(counter), 0, stmt.at);
        Emit(Op::GetIndex, 0, 0, stmt.at);
        Emit(Op::StoreLocal, static_cast<std::int32_t>(slot), 0, stmt.at);
        CompileBlock(stmt.body);
        const std::size_t step = m_chunk->code.size();
        for (const std::size_t cont : m_loops.back().continues) {
            m_chunk->code[cont].a = static_cast<std::int32_t>(step);
        }
        Emit(Op::LoadLocal, static_cast<std::int32_t>(counter), 0, stmt.at);
        PushValue(std::int64_t{1}, stmt.at);
        Emit(Op::Add, 0, 0, stmt.at);
        Emit(Op::StoreLocal, static_cast<std::int32_t>(counter), 0, stmt.at);
        Emit(Op::Jump, static_cast<std::int32_t>(top), 0, stmt.at);
        Patch(exit);
        for (const std::size_t brk : m_loops.back().breaks) {
            Patch(brk);
        }
        m_loops.pop_back();
        m_scopes.pop_back();
    }

    void CompileFor(const Stmt& stmt) {
        const Expr& range = *stmt.value;
        if (range.kind != ExprKind::Binary || range.text != "..") {
            CompileForCollection(stmt);
            return;
        }
        m_scopes.emplace_back();
        const Type start = CompileExpr(*range.operands[0]);
        const std::uint32_t counter = DeclareLocal(stmt.name, Type::Int(), stmt.at);
        Emit(Op::StoreLocal, static_cast<std::int32_t>(counter), 0, stmt.at);
        const Type end = CompileExpr(*range.operands[1]);
        const std::uint32_t limit = m_nextSlot++;
        SetLocalType(limit, Type::Int());
        m_chunk->localCount = std::max(m_chunk->localCount, m_nextSlot);
        Emit(Op::StoreLocal, static_cast<std::int32_t>(limit), 0, stmt.at);
        for (const Type& bound : {start, end}) {
            if (bound.kind != Kind::Int && bound.kind != Kind::Error) {
                Error(range.at, "a range counts whole numbers - both ends must be int");
                break;
            }
        }
        const std::size_t top = m_chunk->code.size();
        Emit(Op::LoadLocal, static_cast<std::int32_t>(counter), 0, stmt.at);
        Emit(Op::LoadLocal, static_cast<std::int32_t>(limit), 0, stmt.at);
        Emit(Op::Lt, 0, 0, stmt.at);
        const std::size_t exit = Emit(Op::JumpIfFalse, 0, 0, stmt.at);
        m_loops.push_back({kUnknownTarget, {}, {}});
        CompileBlock(stmt.body);
        const std::size_t step = m_chunk->code.size();
        for (const std::size_t cont : m_loops.back().continues) {
            m_chunk->code[cont].a = static_cast<std::int32_t>(step);
        }
        Emit(Op::LoadLocal, static_cast<std::int32_t>(counter), 0, stmt.at);
        PushValue(std::int64_t{1}, stmt.at);
        Emit(Op::Add, 0, 0, stmt.at);
        Emit(Op::StoreLocal, static_cast<std::int32_t>(counter), 0, stmt.at);
        Emit(Op::Jump, static_cast<std::int32_t>(top), 0, stmt.at);
        Patch(exit);
        for (const std::size_t brk : m_loops.back().breaks) {
            Patch(brk);
        }
        m_loops.pop_back();
        m_scopes.pop_back();
    }

    /// Where an assignment writes: a local, a field or a host property.
    struct Place final {
        Op load = Op::LoadLocal;
        Op store = Op::StoreLocal;
        std::int32_t operand = 0;
        Type type;
    };

    std::optional<Place> ResolvePlace(const Expr& target) {
        const std::string path = DottedPath(target);
        if (target.kind == ExprKind::Name) {
            if (const Local* local = FindLocal(target.text)) {
                return Place{Op::LoadLocal, Op::StoreLocal, static_cast<std::int32_t>(local->slot), local->type};
            }
            if (const auto field = m_fields.find(target.text); field != m_fields.end()) {
                const FieldInfo& info = m_program.fields[field->second];
                if (info.kind == FieldKind::Const) {
                    Error(target.at, "'" + target.text + "' is a const and cannot change");
                    return std::nullopt;
                }
                return Place{Op::LoadField, Op::StoreField, static_cast<std::int32_t>(field->second), info.type};
            }
        }
        if (!path.empty()) {
            if (const std::optional<HostProperty> property = m_host.DescribeProperty(path)) {
                if (!property->writable) {
                    Error(target.at, "'" + path + "' can be read but not changed");
                    return std::nullopt;
                }
                const std::int32_t name = HostName(path, property->type);
                return Place{Op::LoadProp, Op::StoreProp, name, property->type};
            }
        }
        Error(target.at, path.empty() ? "this cannot be assigned to" : "'" + path + "' is not a variable, field or property");
        return std::nullopt;
    }

    [[nodiscard]] static Op CompoundOp(const std::string& op) noexcept {
        return op == "+=" ? Op::Add : op == "-=" ? Op::Sub : op == "*=" ? Op::Mul : Op::Div;
    }

    /// `xs[i] = v` and `m[k] = v`: the container is loaded, the item replaced, and the new
    /// container written back - which is why the base must be a variable, not a bare value.
    void CompileIndexAssign(const Stmt& stmt) {
        const Expr& target = *stmt.target;
        const Expr& base = *target.operands[0];
        const Expr& key = *target.operands[1];
        if (base.kind == ExprKind::Index) {
            Error(target.at, "an element of an element cannot be assigned yet - unpack it into a variable first");
            return;
        }
        const std::optional<Place> place = ResolvePlace(base);
        if (!place.has_value()) {
            return;
        }
        // A const collection can be read but never changed, through the name or an element.
        if (base.kind == ExprKind::Name) {
            if (const auto field = m_fields.find(base.text);
                field != m_fields.end() && m_program.fields[field->second].kind == FieldKind::Const) {
                Error(target.at, "'" + base.text + "' is a const and cannot change");
                return;
            }
        }
        const Type container = place->type;
        if (container.kind != Kind::List && container.kind != Kind::Map && container.kind != Kind::Error) {
            Error(target.at, container.kind == Kind::Tuple ? "a tuple cannot change - build a new one instead"
                                                           : "only a list or map element can be assigned to");
            return;
        }
        Type keyType = Type::Error();
        Type itemType = Type::Error();
        if (container.kind == Kind::List) {
            keyType = Type::Int();
            if (!container.elements.empty()) {
                itemType = container.elements[0];
            }
        } else if (container.elements.size() >= 2U) {
            keyType = container.elements[0];
            itemType = container.elements[1];
        }
        if (stmt.op == "=") {
            Emit(place->load, place->operand, 0, stmt.at);
            Coerce(CompileExpr(key), keyType, key.at, "this index");
            CompileCoerced(*stmt.value, itemType, stmt.value->at, "this assignment");
            Emit(Op::SetIndex, 0, 0, stmt.at);
            Emit(place->store, place->operand, 0, stmt.at);
            return;
        }
        // `xs[i] += v`: the base and key are each worked out once, into hidden slots, so a call
        // inside either runs a single time.
        Emit(place->load, place->operand, 0, stmt.at);
        const std::uint32_t box = HiddenSlot(container);
        Emit(Op::StoreLocal, static_cast<std::int32_t>(box), 0, stmt.at);
        Coerce(CompileExpr(key), keyType, key.at, "this index");
        const std::uint32_t slot = HiddenSlot(keyType);
        Emit(Op::StoreLocal, static_cast<std::int32_t>(slot), 0, stmt.at);
        Emit(Op::LoadLocal, static_cast<std::int32_t>(box), 0, stmt.at);
        Emit(Op::LoadLocal, static_cast<std::int32_t>(slot), 0, stmt.at);
        Emit(Op::GetIndex, 0, 0, stmt.at);
        Type left = itemType;
        if (itemType.kind == Kind::Int && PeekType(*stmt.value).kind == Kind::Float) {
            Emit(Op::ToFloat, 0, 0, stmt.at);
            left = Type::Float();
        }
        const Type value = CompileExpr(*stmt.value);
        const Type result = CheckArithmetic(stmt.op.substr(0U, 1U), left, value, stmt.at, true);
        Emit(CompoundOp(stmt.op), 0, 0, stmt.at);
        if (result.kind != Kind::Error && result.kind != itemType.kind && itemType.kind != Kind::Error) {
            Error(stmt.at, "'" + stmt.op + "' would turn this " + itemType.Name() + " into " + result.Name());
        }
        const std::uint32_t updated = HiddenSlot(result);
        Emit(Op::StoreLocal, static_cast<std::int32_t>(updated), 0, stmt.at);
        Emit(Op::LoadLocal, static_cast<std::int32_t>(box), 0, stmt.at);
        Emit(Op::LoadLocal, static_cast<std::int32_t>(slot), 0, stmt.at);
        Emit(Op::LoadLocal, static_cast<std::int32_t>(updated), 0, stmt.at);
        Emit(Op::SetIndex, 0, 0, stmt.at);
        Emit(place->store, place->operand, 0, stmt.at);
    }

    void CompileAssign(const Stmt& stmt) {
        const Expr& target = *stmt.target;
        if (target.kind == ExprKind::Index) {
            CompileIndexAssign(stmt);
            return;
        }
        std::int32_t component = -1;
        const Expr* placeExpr = &target;
        // `velocity.x = ...` writes one component of a vec3 place.
        if (target.kind == ExprKind::Member && (target.text == "x" || target.text == "y" || target.text == "z") &&
            !m_host.DescribeProperty(DottedPath(target)).has_value()) {
            component = target.text == "x" ? 0 : target.text == "y" ? 1 : 2;
            placeExpr = target.operands[0].get();
        }
        const std::optional<Place> place = ResolvePlace(*placeExpr);
        if (!place.has_value()) {
            return;
        }
        Type slotType = place->type;
        if (component >= 0) {
            if (place->type.kind != Kind::Vec3 && place->type.kind != Kind::Error) {
                Error(target.at, "'." + target.text + "' only exists on a vec3");
                return;
            }
            Emit(place->load, place->operand, 0, stmt.at);
            slotType = Type::Float();
        }
        if (stmt.op != "=") {
            if (component >= 0) {
                Emit(place->load, place->operand, 0, stmt.at);
                Emit(Op::GetComponent, component, 0, stmt.at);
            } else {
                Emit(place->load, place->operand, 0, stmt.at);
            }
            const Type value = CompileExpr(*stmt.value);
            const Type result = CheckArithmetic(stmt.op.substr(0U, 1U), slotType, value, stmt.at, true);
            Emit(CompoundOp(stmt.op), 0, 0, stmt.at);
            if (result.kind != Kind::Error && result.kind != slotType.kind && slotType.kind != Kind::Error) {
                Error(stmt.at, "'" + stmt.op + "' would turn this " + slotType.Name() + " into " + result.Name());
            }
        } else {
            const Type value = CompileExpr(*stmt.value);
            Coerce(value, slotType, stmt.value->at, "this assignment");
        }
        if (component >= 0) {
            Emit(Op::SetComponent, component, 0, stmt.at);
        }
        Emit(place->store, place->operand, 0, stmt.at);
    }

    // ------------------------------------------------------------ expressions

    /// Types a binary arithmetic operator whose left operand is compiled and whose right operand is
    /// the value just compiled. Only the right side can still be converted here, so a left int is
    /// converted by the caller before the right is compiled (see CompileBinary).
    Type CheckArithmetic(const std::string& op, const Type& left, const Type& right, const SourceLocation at,
                            const bool leftAlreadyEmitted) {
        static_cast<void>(leftAlreadyEmitted);
        if (left.kind == Kind::Error || right.kind == Kind::Error) {
            return Type::Error();
        }
        if (left.IsNumeric() && right.IsNumeric()) {
            if (left.kind == Kind::Float && right.kind == Kind::Int) {
                Emit(Op::ToFloat, 0, 0, at);
            }
            if (op == "/") {
                return Type::Float();
            }
            return left.kind == Kind::Float || right.kind == Kind::Float ? Type::Float() : Type::Int();
        }
        if (op == "+" && left.kind == Kind::Str && right.kind == Kind::Str) {
            return Type::Str();
        }
        if (op == "+" && left.kind == Kind::List && right.kind == Kind::List) {
            if (left != right) {
                Error(at, "these lists hold different types (" + left.Name() + " and " + right.Name() + ")");
                return Type::Error();
            }
            return left;
        }
        if ((op == "+" || op == "-") && left.kind == Kind::Vec3 && right.kind == Kind::Vec3) {
            return Type::Vec3();
        }
        if ((op == "*" || op == "/") && left.kind == Kind::Vec3 && right.IsNumeric()) {
            if (right.kind == Kind::Int) {
                Emit(Op::ToFloat, 0, 0, at);
            }
            return Type::Vec3();
        }
        if (op == "*" && left.IsNumeric() && right.kind == Kind::Vec3) {
            return Type::Vec3();
        }
        Error(at, "'" + op + "' does not work on " + left.Name() + " and " + right.Name());
        return Type::Error();
    }

    /// The type an expression will have, without emitting anything.
    Type PeekType(const Expr& expr) {
        Chunk scratch;
        Chunk* saved = m_chunk;
        m_chunk = &scratch;
        const std::size_t diagnostics = m_diagnostics.size();
        const std::size_t constants = m_program.constants.size();
        const Type type = CompileExpr(expr);
        m_diagnostics.resize(diagnostics);
        m_program.constants.resize(constants);
        m_chunk = saved;
        return type;
    }

    Type CompileBinary(const Expr& expr) {
        const std::string& op = expr.text;
        const Expr& leftExpr = *expr.operands[0];
        const Expr& rightExpr = *expr.operands[1];
        if (op == "and" || op == "or") {
            const Type left = CompileExpr(leftExpr);
            const std::size_t jump = Emit(op == "and" ? Op::JumpIfFalseKeep : Op::JumpIfTrueKeep, 0, 0, expr.at);
            const Type right = CompileExpr(rightExpr);
            Patch(jump);
            for (const Type& side : {left, right}) {
                if (side.kind != Kind::Bool && side.kind != Kind::Error) {
                    Error(expr.at, "'" + op + "' joins true/false values, not " + side.Name());
                    return Type::Error();
                }
            }
            return Type::Bool();
        }
        if (op == "..") {
            Error(expr.at, "a range only goes after 'for ... in'");
            return Type::Error();
        }
        // The left side is converted to float before the right side is on the stack, when the
        // right side is a float (or the operator is '/').
        const Type leftType = CompileExpr(leftExpr);
        const Type rightPeek = PeekType(rightExpr);
        const bool isCompare = op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
        if (leftType.kind == Kind::Int && (rightPeek.kind == Kind::Float || op == "/")) {
            Emit(Op::ToFloat, 0, 0, expr.at);
        }
        const Type rightType = CompileExpr(rightExpr);
        if (op == "/" && rightType.kind == Kind::Int && leftType.IsNumeric()) {
            Emit(Op::ToFloat, 0, 0, expr.at);
        }
        const Type leftAfter = leftType.kind == Kind::Int && (rightPeek.kind == Kind::Float || op == "/")
                                      ? Type::Float()
                                      : leftType;
        const Type rightAfter = op == "/" && rightType.kind == Kind::Int && leftType.IsNumeric()
                                       ? Type::Float()
                                       : rightType;
        if (isCompare) {
            if (leftAfter.kind == Kind::Error || rightAfter.kind == Kind::Error) {
                return Type::Bool();
            }
            if (leftAfter.kind == Kind::Float && rightAfter.kind == Kind::Int) {
                Emit(Op::ToFloat, 0, 0, expr.at);
            }
            const bool numeric = leftAfter.IsNumeric() && rightAfter.IsNumeric();
            const bool ordered = op != "==" && op != "!=";
            // A lookup can come back empty, so an object may be asked whether it is `none`.
            const bool absence = !ordered && ((leftAfter.kind == Kind::None && rightAfter.kind == Kind::Object) ||
                                              (leftAfter.kind == Kind::Object && rightAfter.kind == Kind::None));
            // Collections compare only with their own type: list[int] against list[str] is a bug.
            const bool same =
                leftAfter.kind == rightAfter.kind && (!leftAfter.IsCollection() || leftAfter == rightAfter);
            if (ordered ? !numeric : !(numeric || same || absence)) {
                Error(expr.at, "'" + op + "' cannot compare " + leftAfter.Name() + " with " + rightAfter.Name());
            }
            Emit(op == "==" ? Op::Eq : op == "!=" ? Op::Ne : op == "<" ? Op::Lt : op == "<=" ? Op::Le
                 : op == ">" ? Op::Gt : Op::Ge, 0, 0, expr.at);
            return Type::Bool();
        }
        const Type result = CheckArithmetic(op, leftAfter, rightAfter, expr.at, true);
        if (op == "%" && result.kind == Kind::Vec3) {
            Error(expr.at, "'%' does not work on vec3");
        }
        Emit(op == "+" ? Op::Add : op == "-" ? Op::Sub : op == "*" ? Op::Mul : op == "/" ? Op::Div : Op::Mod,
             0, 0, expr.at);
        return result;
    }

    /// Whether every operand is a number with at least one float: `[1, 2.5]` unifies to floats.
    bool UnifyFloat(const Expr& expr) {
        bool anyFloat = false;
        for (const ExprPtr& operand : expr.operands) {
            const Kind kind = PeekType(*operand).kind;
            if (kind != Kind::Int && kind != Kind::Float && kind != Kind::Error) {
                return false;
            }
            anyFloat = anyFloat || kind == Kind::Float;
        }
        return anyFloat;
    }

    Type CompileList(const Expr& expr) {
        if (expr.operands.empty()) {
            Error(expr.at,
                  "this list is empty, so its item type is unknown - write it where the list is stored, as in "
                  "'let xs: list[int] = []'");
            Emit(Op::BuildList, 0, 0, expr.at);
            return Type::Error();
        }
        const bool floats = UnifyFloat(expr);
        Type item = Type::Error();
        bool first = true;
        for (const ExprPtr& operand : expr.operands) {
            Type next = CompileExpr(*operand);
            if (floats && next.kind == Kind::Int) {
                Emit(Op::ToFloat, 0, 0, operand->at);
                next = Type::Float();
            }
            if (first) {
                item = next;
                first = false;
            } else if (item.kind != Kind::Error && next.kind != Kind::Error && item != next) {
                Error(operand->at, "this list holds " + item.Name() + ", not " + next.Name());
                item = Type::Error();
            }
        }
        if (item.kind == Kind::None) {
            Error(expr.at, "this list holds none - give it values of one type");
            item = Type::Error();
        }
        Emit(Op::BuildList, static_cast<std::int32_t>(expr.operands.size()), 0, expr.at);
        return item.kind == Kind::Error ? Type::Error() : Type::List(item);
    }

    Type CompileMap(const Expr& expr) {
        const std::size_t pairs = expr.operands.size() / 2U;
        if (pairs == 0U) {
            Error(expr.at,
                  "this map is empty, so its types are unknown - write them where the map is stored, as in "
                  "'let scores: map[str, int] = {}'");
            Emit(Op::BuildMap, 0, 0, expr.at);
            return Type::Error();
        }
        bool valuesFloat = false;
        bool valuesNumeric = true;
        for (std::size_t i = 0U; i < pairs; ++i) {
            const Kind kind = PeekType(*expr.operands[i * 2U + 1U]).kind;
            valuesNumeric = valuesNumeric && (kind == Kind::Int || kind == Kind::Float || kind == Kind::Error);
            valuesFloat = valuesFloat || kind == Kind::Float;
        }
        valuesFloat = valuesFloat && valuesNumeric;
        Type key = Type::Error();
        Type value = Type::Error();
        bool first = true;
        for (std::size_t i = 0U; i < pairs; ++i) {
            const Type nextKey = CompileExpr(*expr.operands[i * 2U]);
            Type nextValue = CompileExpr(*expr.operands[i * 2U + 1U]);
            if (valuesFloat && nextValue.kind == Kind::Int) {
                Emit(Op::ToFloat, 0, 0, expr.operands[i * 2U + 1U]->at);
                nextValue = Type::Float();
            }
            if (first) {
                key = nextKey;
                value = nextValue;
                first = false;
            } else {
                if (key.kind != Kind::Error && nextKey.kind != Kind::Error && key != nextKey) {
                    Error(expr.operands[i * 2U]->at, "this map's keys are " + key.Name() + ", not " + nextKey.Name());
                    key = Type::Error();
                }
                if (value.kind != Kind::Error && nextValue.kind != Kind::Error && value != nextValue) {
                    Error(expr.operands[i * 2U + 1U]->at,
                          "this map holds " + value.Name() + ", not " + nextValue.Name());
                    value = Type::Error();
                }
            }
        }
        if (key.kind != Kind::Int && key.kind != Kind::Str && key.kind != Kind::Bool && key.kind != Kind::Error) {
            Error(expr.at, "map keys are int, str or bool, not " + key.Name());
            key = Type::Error();
        }
        if (value.kind == Kind::None) {
            Error(expr.at, "this map holds none - give it values of one type");
            value = Type::Error();
        }
        Emit(Op::BuildMap, static_cast<std::int32_t>(pairs), 0, expr.at);
        if (key.kind == Kind::Error || value.kind == Kind::Error) {
            return Type::Error();
        }
        return Type::Map(key, value);
    }

    Type CompileTuple(const Expr& expr) {
        std::vector<Type> elements;
        bool ok = true;
        for (const ExprPtr& operand : expr.operands) {
            const Type next = CompileExpr(*operand);
            ok = ok && next.kind != Kind::Error;
            elements.push_back(next);
        }
        Emit(Op::BuildTuple, static_cast<std::int32_t>(expr.operands.size()), 0, expr.at);
        return ok ? Type::Tuple(std::move(elements)) : Type::Error();
    }

    Type CompileIndex(const Expr& expr) {
        const Type container = CompileExpr(*expr.operands[0]);
        const Expr& keyExpr = *expr.operands[1];
        const Type key = CompileExpr(keyExpr);
        if (container.kind == Kind::Error || key.kind == Kind::Error) {
            return Type::Error();
        }
        if (container.kind == Kind::List) {
            if (key.kind != Kind::Int) {
                Error(keyExpr.at, "a list index is a whole number, not " + key.Name());
                return Type::Error();
            }
            Emit(Op::GetIndex, 0, 0, expr.at);
            return container.elements.empty() ? Type::Error() : container.elements[0];
        }
        if (container.kind == Kind::Tuple) {
            // Each position has its own type, so the index must be written out.
            if (keyExpr.kind != ExprKind::Number || !keyExpr.isInteger || !keyExpr.unit.empty()) {
                Error(keyExpr.at, "a tuple index is written out, as in 'pair[0]' - each position has its own type");
                return Type::Error();
            }
            const auto index = static_cast<std::int64_t>(keyExpr.number);
            if (index < 0 || static_cast<std::size_t>(index) >= container.elements.size()) {
                Error(keyExpr.at, "this " + container.Name() + " has no index " + std::to_string(index));
                return Type::Error();
            }
            Emit(Op::GetIndex, 0, 0, expr.at);
            return container.elements[static_cast<std::size_t>(index)];
        }
        if (container.kind == Kind::Map) {
            if (container.elements.size() < 2U) {
                return Type::Error();
            }
            Coerce(key, container.elements[0], keyExpr.at, "this key");
            Emit(Op::GetIndex, 0, 0, expr.at);
            return container.elements[1];
        }
        Error(expr.at, "only a list, map or tuple uses [...], not " + container.Name());
        return Type::Error();
    }

    Type CompileExpr(const Expr& expr) {
        switch (expr.kind) {
            case ExprKind::Number: {
                if (expr.isInteger) {
                    PushValue(static_cast<std::int64_t>(expr.number), expr.at);
                    return Type::Int();
                }
                double value = expr.number;
                if (expr.unit == "deg") value *= std::numbers::pi / 180.0;
                else if (expr.unit == "ms") value /= 1000.0;
                else if (expr.unit == "cm") value /= 100.0;
                else if (expr.unit == "km") value *= 1000.0;
                PushValue(value, expr.at);
                return Type::Float();
            }
            case ExprKind::String:
                if (expr.operands.empty()) {
                    PushValue(expr.segments.empty() ? std::string{} : expr.segments.front(), expr.at);
                } else {
                    std::int32_t count = 0;
                    for (std::size_t i = 0U; i < expr.operands.size(); ++i) {
                        if (!expr.segments[i].empty()) {
                            PushValue(expr.segments[i], expr.at);
                            ++count;
                        }
                        CompileExpr(*expr.operands[i]);
                        ++count;
                    }
                    if (!expr.segments.back().empty()) {
                        PushValue(expr.segments.back(), expr.at);
                        ++count;
                    }
                    Emit(Op::Concat, count, 0, expr.at);
                }
                return Type::Str();
            case ExprKind::Bool:
                PushValue(expr.boolean, expr.at);
                return Type::Bool();
            case ExprKind::None:
                PushValue(std::monostate{}, expr.at);
                return Type::None();
            case ExprKind::Name:
                return CompileName(expr);
            case ExprKind::Unary: {
                const Type type = CompileExpr(*expr.operands[0]);
                if (expr.text == "not") {
                    if (type.kind != Kind::Bool && type.kind != Kind::Error) {
                        Error(expr.at, "'not' works on true/false values, not " + type.Name());
                    }
                    Emit(Op::Not, 0, 0, expr.at);
                    return Type::Bool();
                }
                if (!type.IsNumeric() && type.kind != Kind::Vec3 && type.kind != Kind::Error) {
                    Error(expr.at, "'-' does not work on " + type.Name());
                    return Type::Error();
                }
                Emit(Op::Neg, 0, 0, expr.at);
                return type;
            }
            case ExprKind::Binary:
                return CompileBinary(expr);
            case ExprKind::Member:
                return CompileMember(expr);
            case ExprKind::Call:
                return CompileCall(expr);
            case ExprKind::Index:
                return CompileIndex(expr);
            case ExprKind::List:
                return CompileList(expr);
            case ExprKind::Map:
                return CompileMap(expr);
            case ExprKind::Tuple:
                return CompileTuple(expr);
        }
        return Type::Error();
    }

    Type CompileName(const Expr& expr) {
        if (const Local* local = FindLocal(expr.text)) {
            Emit(Op::LoadLocal, static_cast<std::int32_t>(local->slot), 0, expr.at);
            return local->type;
        }
        if (const auto field = m_fields.find(expr.text); field != m_fields.end()) {
            Emit(Op::LoadField, static_cast<std::int32_t>(field->second), 0, expr.at);
            return m_program.fields[field->second].type;
        }
        if (const std::optional<HostProperty> property = m_host.DescribeProperty(expr.text)) {
            Emit(Op::LoadProp, HostName(expr.text, property->type), 0, expr.at);
            return property->type;
        }
        if (m_functions.contains(expr.text) || FindBuiltin(expr.text) != nullptr) {
            Error(expr.at, "'" + expr.text + "' is a function - call it with ()");
        } else {
            Error(expr.at, "nothing is called '" + expr.text + "' here");
        }
        return Type::Error();
    }

    Type CompileMember(const Expr& expr) {
        const std::string path = DottedPath(expr);
        const bool baseIsVariable = expr.operands[0]->kind == ExprKind::Name &&
                                    (FindLocal(expr.operands[0]->text) != nullptr || m_fields.contains(expr.operands[0]->text));
        if (!path.empty() && !baseIsVariable) {
            if (const std::optional<HostProperty> property = m_host.DescribeProperty(path)) {
                Emit(Op::LoadProp, HostName(path, property->type), 0, expr.at);
                return property->type;
            }
        }
        const Type base = CompileExpr(*expr.operands[0]);
        if (base.kind == Kind::Vec3 && (expr.text == "x" || expr.text == "y" || expr.text == "z")) {
            Emit(Op::GetComponent, expr.text == "x" ? 0 : expr.text == "y" ? 1 : 2, 0, expr.at);
            return Type::Float();
        }
        if (base.kind != Kind::Error) {
            Error(expr.at, base.Name() + " has no '" + expr.text + "'");
        }
        return Type::Error();
    }

    /// Compiles the arguments against `params`, converting int to float where a float is wanted.
    bool CompileArguments(const Expr& call, const std::vector<Type>& params, const std::string& name) {
        const std::size_t given = call.operands.size() - 1U;
        if (given != params.size()) {
            Error(call.at, "'" + name + "' takes " + std::to_string(params.size()) + " value(s), not " + std::to_string(given));
            return false;
        }
        for (std::size_t i = 0U; i < given; ++i) {
            CompileCoerced(*call.operands[i + 1U], params[i], call.operands[i + 1U]->at,
                           "'" + name + "' argument " + std::to_string(i + 1U));
        }
        return true;
    }

    /// `xs.push(v)`, `m.keys()`, `s.contains("b")` - collection and string helpers read as
    /// methods but compile to the plain built-in underneath, with the base first.
    Type CompileMethodCall(const Expr& call, const Type& base) {
        const std::string& method = call.operands[0]->text;
        const bool list = base.kind == Kind::List;
        const bool map = base.kind == Kind::Map;
        const bool tuple = base.kind == Kind::Tuple;
        const bool str = base.kind == Kind::Str;
        const bool ok = (method == "push" && list) || (method == "remove" && (list || map)) || (method == "keys" && map) ||
                        (method == "contains" && (list || map || tuple || str)) ||
                        (method == "length" && (list || map || tuple || str));
        if (!ok) {
            Error(call.at, base.Name() + " has no '" + method + "'");
            return Type::Error();
        }
        const BuiltinInfo* builtin = FindBuiltin(method);
        const std::size_t want = builtin->arity - 1U;
        const std::size_t given = call.operands.size() - 1U;
        if (given != want) {
            Error(call.at, "'" + method + "' takes " + std::to_string(want) + " value(s) after the " +
                               base.Name() + ", not " + std::to_string(given));
            return Type::Error();
        }
        std::vector<const Expr*> args;
        args.push_back(call.operands[0]->operands[0].get());
        for (std::size_t i = 1U; i < call.operands.size(); ++i) {
            args.push_back(call.operands[i].get());
        }
        return CompileBuiltinArgs(call.at, args, *builtin);
    }

    Type CompileCall(const Expr& call) {
        const Expr& callee = *call.operands[0];
        const std::string path = DottedPath(callee);
        const auto argc = static_cast<std::int32_t>(call.operands.size() - 1U);
        if (callee.kind == ExprKind::Member) {
            const Type base = PeekType(*callee.operands[0]);
            if (base.kind == Kind::List || base.kind == Kind::Map || base.kind == Kind::Tuple || base.kind == Kind::Str) {
                return CompileMethodCall(call, base);
            }
        }
        // `other.hide()` - a call on another node. The target is only known when the script runs,
        // so the method resolves then; the call is fire-and-forget and answers none either way.
        if (callee.kind == ExprKind::Member && PeekType(*callee.operands[0]).kind == Kind::Object) {
            static_cast<void>(CompileExpr(*callee.operands[0]));
            for (std::size_t i = 1U; i < call.operands.size(); ++i) {
                static_cast<void>(CompileExpr(*call.operands[i]));
            }
            Emit(Op::CallMethod, Constant(Value{callee.text}), argc, call.at);
            return Type::None();
        }
        if (callee.kind == ExprKind::Name) {
            if (const auto fn = m_functions.find(path); fn != m_functions.end()) {
                CompileArguments(call, fn->second.params, path);
                Emit(Op::Call, static_cast<std::int32_t>(fn->second.index), argc, call.at);
                return fn->second.result;
            }
            if (const BuiltinInfo* builtin = FindBuiltin(path)) {
                return CompileBuiltin(call, *builtin);
            }
        }
        if (!path.empty()) {
            if (const std::optional<HostFunction> fn = m_host.DescribeFunction(path)) {
                CompileArguments(call, fn->params, path);
                Emit(Op::CallHost, HostName(path, fn->result), argc, call.at);
                return fn->result;
            }
        }
        Error(call.at, path.empty() ? "only functions can be called" : "there is no function '" + path + "'");
        return Type::Error();
    }

    Type CompileBuiltinArgs(const SourceLocation at, const std::vector<const Expr*>& args,
                                const BuiltinInfo& builtin) {
        if (args.size() != builtin.arity) {
            Error(at, "'" + std::string{builtin.name} + "' takes " + std::to_string(builtin.arity) + " value(s), not " +
                           std::to_string(args.size()));
            return Type::Error();
        }
        const auto emit = [&] {
            Emit(Op::CallBuiltin, static_cast<std::int32_t>(builtin.id), static_cast<std::int32_t>(args.size()), at);
        };
        const auto coerceAll = [&](const Type& want) {
            bool ok = true;
            for (const Expr* arg : args) {
                ok = Coerce(CompileExpr(*arg), want, arg->at, "'" + std::string{builtin.name} + "'") && ok;
            }
            return ok;
        };
        switch (builtin.id) {
            case Builtin::Print:
            case Builtin::Str:
                CompileExpr(*args[0]);
                emit();
                return builtin.id == Builtin::Print ? Type::None() : Type::Str();
            case Builtin::Int:
            case Builtin::Float: {
                const Type type = CompileExpr(*args[0]);
                if (!type.IsNumeric() && type.kind != Kind::Error) {
                    Error(at, "'" + std::string{builtin.name} + "' converts numbers, not " + type.Name());
                }
                emit();
                return builtin.id == Builtin::Int ? Type::Int() : Type::Float();
            }
            case Builtin::Min:
            case Builtin::Max:
            case Builtin::Abs:
            case Builtin::Clamp: {
                // Whole numbers stay whole; any float makes the result a float.
                bool anyFloat = false;
                for (const Expr* arg : args) {
                    anyFloat = anyFloat || PeekType(*arg).kind == Kind::Float;
                }
                coerceAll(anyFloat ? Type::Float() : Type::Int());
                emit();
                return anyFloat ? Type::Float() : Type::Int();
            }
            case Builtin::Vec3:
                coerceAll(Type::Float());
                emit();
                return Type::Vec3();
            case Builtin::Length: {
                // `length` is overloaded: a vec3's magnitude (a float, as before), or the count of
                // a str or collection (a whole number).
                const Type type = PeekType(*args[0]);
                if (type.kind != Kind::Vec3 && type.kind != Kind::Str && !type.IsCollection() &&
                    type.kind != Kind::Error) {
                    Error(at, "'length' measures a vec3, a str or a collection, not " + type.Name());
                }
                CompileExpr(*args[0]);
                emit();
                return type.kind == Kind::Vec3 || type.kind == Kind::Error ? Type::Float() : Type::Int();
            }
            case Builtin::Normalize:
                coerceAll(Type::Vec3());
                emit();
                return Type::Vec3();
            case Builtin::Push: {
                const Type list = CompileExpr(*args[0]);
                if (list.kind != Kind::List && list.kind != Kind::Error) {
                    CompileCoerced(*args[1], Type::Error(), args[1]->at, "");
                    Error(at, "'push' adds to a list, not " + list.Name());
                } else if (!list.elements.empty()) {
                    CompileCoerced(*args[1], list.elements[0], args[1]->at, "'push'");
                } else {
                    CompileCoerced(*args[1], Type::Error(), args[1]->at, "");
                }
                emit();
                return list.kind == Kind::List ? list : Type::Error();
            }
            case Builtin::Keys: {
                const Type map = CompileExpr(*args[0]);
                if (map.kind != Kind::Map && map.kind != Kind::Error) {
                    Error(at, "'keys' reads a map, not " + map.Name());
                    emit();
                    return Type::Error();
                }
                emit();
                return map.elements.empty() ? Type::Error() : Type::List(map.elements[0]);
            }
            case Builtin::Contains: {
                const Type container = CompileExpr(*args[0]);
                if (container.kind == Kind::Str) {
                    CompileCoerced(*args[1], Type::Str(), args[1]->at, "'contains'");
                } else if ((container.kind == Kind::List || container.kind == Kind::Map) && !container.elements.empty()) {
                    CompileCoerced(*args[1], container.elements[0], args[1]->at, "'contains'");
                } else if (container.kind == Kind::Tuple) {
                    const Type key = CompileExpr(*args[1]);
                    // Each position has its own type; the value must plausibly sit in one of them.
                    const bool numeric = key.IsNumeric();
                    const bool found =
                        key.kind == Kind::Error ||
                        std::ranges::any_of(container.elements, [&](const Type& item) {
                            return item == key || (numeric && item.IsNumeric());
                        });
                    if (!found) {
                        Error(args[1]->at, "this " + container.Name() + " never holds " + key.Name());
                    }
                } else {
                    // An errored or bare-typed container: harvest the argument's own errors only.
                    CompileCoerced(*args[1], Type::Error(), args[1]->at, "");
                    if (container.kind != Kind::Error && container.kind != Kind::List && container.kind != Kind::Map) {
                        Error(at, "'contains' looks in a str, list, map or tuple, not " + container.Name());
                    }
                }
                emit();
                return Type::Bool();
            }
            case Builtin::Remove: {
                const Type container = CompileExpr(*args[0]);
                const Type key = CompileExpr(*args[1]);
                if (container.kind == Kind::List) {
                    Coerce(key, Type::Int(), args[1]->at, "'remove'");
                } else if (container.kind == Kind::Map) {
                    if (!container.elements.empty()) {
                        Coerce(key, container.elements[0], args[1]->at, "'remove'");
                    }
                } else if (container.kind != Kind::Error) {
                    Error(at, container.kind == Kind::Tuple ? "'remove' cannot shrink a tuple - build a new one instead"
                                                            : "'remove' drops from a list or map, not " +
                                                                  container.Name());
                }
                emit();
                return container.kind == Kind::List || container.kind == Kind::Map ? container : Type::Error();
            }
            default:
                coerceAll(Type::Float());
                emit();
                return Type::Float();
        }
    }

    Type CompileBuiltin(const Expr& call, const BuiltinInfo& builtin) {
        std::vector<const Expr*> args;
        for (std::size_t i = 1U; i < call.operands.size(); ++i) {
            args.push_back(call.operands[i].get());
        }
        return CompileBuiltinArgs(call.at, args, builtin);
    }

    static constexpr std::size_t kUnknownTarget = static_cast<std::size_t>(-1);

    struct Loop final {
        std::size_t continueTarget = kUnknownTarget;
        std::vector<std::size_t> breaks;
        std::vector<std::size_t> continues;
    };

    const UVScriptHost& m_host;
    std::vector<Diagnostic>& m_diagnostics;
    Program& m_program;
    std::unordered_map<std::string, std::uint32_t> m_fields;
    std::unordered_map<std::string, FunctionSignature> m_functions;
    Chunk* m_chunk = nullptr;
    std::vector<std::unordered_map<std::string, Local>> m_scopes;
    std::uint32_t m_nextSlot = 0U;
    std::vector<Loop> m_loops;
    bool m_inHandler = false;
    Type m_result;
};

} // namespace

CompileResult CompileUVScript(const File& file, const UVScriptHost& host) {
    auto program = std::make_shared<Program>();
    CompileResult result;
    Compiler(host, result.diagnostics, *program).Run(file);
    std::ranges::stable_sort(result.diagnostics, [](const Diagnostic& a, const Diagnostic& b) {
        return a.at.line != b.at.line ? a.at.line < b.at.line : a.at.column < b.at.column;
    });
    if (result.diagnostics.empty()) {
        program->fingerprint = ComputeProgramFingerprint(*program);
        result.program = std::move(program);
    }
    return result;
}

CompileResult CompileUVScriptSource(const std::string_view source, const UVScriptHost& host) {
    ParseResult parsed = ParseUVScript(source);
    if (!parsed.IsSuccess()) {
        return {nullptr, std::move(parsed.diagnostics)};
    }
    return CompileUVScript(parsed.file, host);
}

std::vector<FieldInfo> GetProgramFields(const Program& program) {
    return program.fields;
}

std::uint64_t GetProgramFingerprint(const Program& program) {
    return program.fingerprint;
}

std::uint64_t ComputeProgramFingerprint(const Program& program) {
    // FNV-1a over a plain serialization; stable across runs and builds.
    // Kept local deliberately: this module is standard-library-only by design, and linking
    // uve_utilities would drag uve_platform in transitively. Same algorithm and values as
    // Utilities::Fnv1a64 — keep in sync.
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    const auto bytes = [&hash](const void* const data, const std::size_t size) {
        const auto* const p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0U; i < size; ++i) {
            hash = (hash ^ p[i]) * 0x100000001b3ULL;
        }
    };
    const auto number = [&bytes](const std::uint64_t value) { bytes(&value, sizeof(value)); };
    const auto text = [&bytes, &number](const std::string& value) {
        number(value.size());
        bytes(value.data(), value.size());
    };
    // Element types join the hash: list[int] and list[str] must never share a fingerprint,
    // or the wrong native code would run.
    std::function<void(const Type&)> type;
    type = [&](const Type& value) {
        number(static_cast<std::uint64_t>(value.kind));
        text(value.object);
        number(value.elements.size());
        for (const Type& element : value.elements) {
            type(element);
        }
    };
    const auto chunk = [&](const Chunk& value) {
        text(value.name);
        number(value.paramCount);
        number(value.localCount);
        type(value.result);
        number(value.localTypes.size());
        for (const Type& local : value.localTypes) {
            type(local);
        }
        number(value.code.size());
        for (const Instruction& in : value.code) {
            number(static_cast<std::uint64_t>(in.op));
            number(static_cast<std::uint64_t>(static_cast<std::int64_t>(in.a)));
            number(static_cast<std::uint64_t>(static_cast<std::int64_t>(in.b)));
            number(in.line);
        }
    };
    // A collection constant hashes exactly, item by item: the text form rounds floats.
    std::function<void(const Value&)> exact;
    exact = [&](const Value& value) {
        number(value.index());
        if (const auto* real = std::get_if<double>(&value)) {
            bytes(real, sizeof(*real));
        } else if (const auto* vector = std::get_if<Vec3Value>(&value)) {
            bytes(&vector->x, sizeof(double));
            bytes(&vector->y, sizeof(double));
            bytes(&vector->z, sizeof(double));
        } else if (const auto* box = std::get_if<std::shared_ptr<const CollectionValue>>(&value)) {
            number(static_cast<std::uint64_t>((*box)->kind));
            number((*box)->items.size());
            for (const Value& item : (*box)->items) {
                exact(item);
            }
        } else {
            text(FormatValue(value));
        }
    };
    number(program.constants.size());
    for (const Value& constant : program.constants) {
        number(constant.index());
        text(FormatValue(constant));
        if (const auto* real = std::get_if<double>(&constant)) {
            bytes(real, sizeof(*real)); // exact bits: the text form rounds
        } else if (const auto* vector = std::get_if<Vec3Value>(&constant)) {
            bytes(&vector->x, sizeof(double));
            bytes(&vector->y, sizeof(double));
            bytes(&vector->z, sizeof(double));
        } else if (std::holds_alternative<std::shared_ptr<const CollectionValue>>(constant)) {
            exact(constant);
        }
    }
    number(program.fields.size());
    for (const FieldInfo& field : program.fields) {
        text(field.name);
        type(field.type);
        number(static_cast<std::uint64_t>(field.kind));
    }
    chunk(program.init);
    number(program.functions.size());
    for (const Chunk& function : program.functions) {
        chunk(function);
    }
    number(program.hostTypes.size());
    for (const auto& [constant, hostType] : program.hostTypes) {
        number(constant);
        type(hostType);
    }
    number(program.handlers.size());
    for (const auto& [event, index] : program.handlers) {
        text(event);
        number(index);
    }
    return hash;
}

} // namespace UVE::Blueprint
