// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/uvscript/uvscript_compiler_uve.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <numbers>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "uve/uvscript/uvscript_parser_uve.h"
#include "uvscript_program_uve.h"

namespace UVE::UVScript {
namespace {

using Kind = TypeUVE::KindUVE;

struct LocalUVE final {
    std::uint32_t slot = 0U;
    TypeUVE type;
};

struct FunctionSignatureUVE final {
    std::uint32_t index = 0U;
    std::vector<TypeUVE> params;
    TypeUVE result;
};

struct BuiltinInfoUVE final {
    std::string_view name;
    BuiltinUVE id;
    std::size_t arity;
};

constexpr std::array<BuiltinInfoUVE, 16> kBuiltinsUVE{{
    {"print", BuiltinUVE::Print, 1U},       {"sqrt", BuiltinUVE::Sqrt, 1U},
    {"abs", BuiltinUVE::Abs, 1U},           {"min", BuiltinUVE::Min, 2U},
    {"max", BuiltinUVE::Max, 2U},           {"clamp", BuiltinUVE::Clamp, 3U},
    {"lerp", BuiltinUVE::Lerp, 3U},         {"sin", BuiltinUVE::Sin, 1U},
    {"cos", BuiltinUVE::Cos, 1U},           {"floor", BuiltinUVE::Floor, 1U},
    {"vec3", BuiltinUVE::Vec3, 3U},         {"int", BuiltinUVE::Int, 1U},
    {"float", BuiltinUVE::Float, 1U},       {"str", BuiltinUVE::Str, 1U},
    {"length", BuiltinUVE::Length, 1U},     {"normalize", BuiltinUVE::Normalize, 1U},
}};

[[nodiscard]] const BuiltinInfoUVE* FindBuiltinUVE(const std::string_view name) noexcept {
    for (const BuiltinInfoUVE& builtin : kBuiltinsUVE) {
        if (builtin.name == name) {
            return &builtin;
        }
    }
    return nullptr;
}

/// "input.axis" for `input.axis`, empty for anything that is not a plain dotted name.
[[nodiscard]] std::string DottedPathUVE(const ExprUVE& expr) {
    if (expr.kind == ExprKindUVE::Name) {
        return expr.text;
    }
    if (expr.kind == ExprKindUVE::Member) {
        const std::string base = DottedPathUVE(*expr.operands[0]);
        return base.empty() ? std::string{} : base + "." + expr.text;
    }
    return {};
}

[[nodiscard]] bool EndsWithReturnUVE(const BlockUVE& block) {
    if (block.empty()) {
        return false;
    }
    const StmtUVE& last = *block.back();
    if (last.kind == StmtKindUVE::Return) {
        return true;
    }
    if (last.kind == StmtKindUVE::If && !last.elseBody.empty()) {
        return std::ranges::all_of(last.branches, [](const ConditionalBlockUVE& b) { return EndsWithReturnUVE(b.body); }) &&
               EndsWithReturnUVE(last.elseBody);
    }
    return false;
}

class CompilerUVE final {
public:
    CompilerUVE(const UVScriptHostUVE& host, std::vector<DiagnosticUVE>& diagnostics, ProgramUVE& program)
        : m_host(host), m_diagnostics(diagnostics), m_program(program) {}

    void Run(const FileUVE& file) {
        DeclareFields(file);
        DeclareFunctions(file);
        CompileFieldInitializers(file);
        for (const FunctionUVE& function : file.functions) {
            CompileFunction(function);
        }
        for (const HandlerUVE& handler : file.handlers) {
            CompileHandler(handler);
        }
    }

private:
    // ------------------------------------------------------------ diagnostics and types

    void Error(const SourceLocationUVE at, std::string message) {
        m_diagnostics.push_back({at, std::move(message)});
    }

    std::optional<TypeUVE> ResolveType(const TypeRefUVE& ref) {
        if (!ref.arguments.empty()) {
            Error(ref.at, "'" + ref.name + "[...]' collections are not in UVScript yet");
            return std::nullopt;
        }
        if (ref.name == "int") return TypeUVE::IntUVE();
        if (ref.name == "float") return TypeUVE::FloatUVE();
        if (ref.name == "bool") return TypeUVE::BoolUVE();
        if (ref.name == "str") return TypeUVE::StrUVE();
        if (ref.name == "vec3") return TypeUVE::Vec3UVE();
        if (!ref.name.empty() && std::isupper(static_cast<unsigned char>(ref.name.front())) != 0) {
            return TypeUVE::ObjectUVE(ref.name);
        }
        Error(ref.at, "'" + ref.name + "' is not a type - use int, float, bool, str, vec3 or a node kind");
        return std::nullopt;
    }

    /// Checks that `from` can be stored where `to` is expected, converting int to float. Emits the
    /// conversion right after the value that is on top of the stack.
    bool Coerce(const TypeUVE& from, const TypeUVE& to, const SourceLocationUVE at, const std::string& what) {
        if (from.kind == Kind::Error || to.kind == Kind::Error) {
            return true;
        }
        if (from.kind == to.kind) {
            return true;
        }
        if (from.kind == Kind::Int && to.kind == Kind::Float) {
            Emit(OpUVE::ToFloat, 0, 0, at);
            return true;
        }
        Error(at, what + " needs " + to.NameUVE() + " but this is " + from.NameUVE());
        return false;
    }

    // ------------------------------------------------------------ emitting

    std::size_t Emit(const OpUVE op, const std::int32_t a, const std::int32_t b, const SourceLocationUVE at) {
        m_chunk->code.push_back({op, a, b, at.line});
        return m_chunk->code.size() - 1U;
    }

    void Patch(const std::size_t instruction) {
        m_chunk->code[instruction].a = static_cast<std::int32_t>(m_chunk->code.size());
    }

    std::int32_t Constant(ValueUVE value) {
        m_program.constants.push_back(std::move(value));
        return static_cast<std::int32_t>(m_program.constants.size() - 1U);
    }

    /// A host property or function name, remembered with its type so native code can use it.
    std::int32_t HostName(const std::string& name, const TypeUVE& type) {
        const std::int32_t index = Constant(name);
        m_program.hostTypes.emplace_back(static_cast<std::uint32_t>(index), type);
        return index;
    }

    void PushValue(ValueUVE value, const SourceLocationUVE at) { Emit(OpUVE::PushConst, Constant(std::move(value)), 0, at); }

    [[nodiscard]] static ValueUVE DefaultValue(const TypeUVE& type) {
        switch (type.kind) {
            case Kind::Bool: return false;
            case Kind::Int: return std::int64_t{0};
            case Kind::Float: return 0.0;
            case Kind::Str: return std::string{};
            case Kind::Vec3: return Vec3ValueUVE{};
            case Kind::Object: return ObjectRefUVE{};
            default: return std::monostate{};
        }
    }

    // ------------------------------------------------------------ declarations

    void DeclareFields(const FileUVE& file) {
        for (const FieldUVE& field : file.fields) {
            if (m_fields.contains(field.name) || m_host.DescribePropertyUVE(field.name).has_value()) {
                Error(field.at, "'" + field.name + "' is already declared" +
                                    (m_fields.contains(field.name) ? "" : " - the node has a property by that name"));
                continue;
            }
            TypeUVE type = TypeUVE::ErrorUVE();
            if (field.type.has_value()) {
                type = ResolveType(*field.type).value_or(TypeUVE::ErrorUVE());
            } else {
                type = LiteralTypeOf(*field.initializer);
            }
            m_fields.emplace(field.name, static_cast<std::uint32_t>(m_program.fields.size()));
            m_program.fields.push_back({field.name, type, field.kind});
        }
    }

    /// A field without a written type takes its initializer's type; a field initializer is checked
    /// fully later, so this only needs to be right for well-typed ones.
    TypeUVE LiteralTypeOf(const ExprUVE& expr) {
        ChunkUVE scratch;
        ChunkUVE* saved = m_chunk;
        m_chunk = &scratch;
        const std::size_t diagnostics = m_diagnostics.size();
        const std::size_t constants = m_program.constants.size();
        const TypeUVE type = CompileExpr(expr);
        m_diagnostics.resize(diagnostics);
        m_program.constants.resize(constants);
        m_chunk = saved;
        return type;
    }

    void DeclareFunctions(const FileUVE& file) {
        for (const FunctionUVE& function : file.functions) {
            if (m_functions.contains(function.name) || FindBuiltinUVE(function.name) != nullptr) {
                Error(function.at, "a function named '" + function.name + "' already exists");
                continue;
            }
            FunctionSignatureUVE signature;
            for (const ParamUVE& param : function.params) {
                if (!param.type.has_value()) {
                    Error(param.at, "give '" + param.name + "' a type, as in '" + param.name + ": float'");
                    signature.params.push_back(TypeUVE::ErrorUVE());
                } else {
                    signature.params.push_back(ResolveType(*param.type).value_or(TypeUVE::ErrorUVE()));
                }
            }
            signature.result = function.returnType.has_value()
                                   ? ResolveType(*function.returnType).value_or(TypeUVE::ErrorUVE())
                                   : TypeUVE::NoneUVE();
            signature.index = static_cast<std::uint32_t>(m_program.functions.size());
            m_program.functions.push_back({function.name, static_cast<std::uint32_t>(function.params.size()), 0U, {}, {}, signature.result});
            m_functions.emplace(function.name, std::move(signature));
        }
    }

    // ------------------------------------------------------------ bodies

    void BeginChunk(ChunkUVE& chunk) {
        m_chunk = &chunk;
        m_scopes.clear();
        m_scopes.emplace_back();
        m_nextSlot = 0U;
        m_loops.clear();
    }

    void EndChunk(const SourceLocationUVE at) {
        Emit(OpUVE::ReturnNone, 0, 0, at);
        m_chunk->localCount = std::max(m_chunk->localCount, m_nextSlot);
    }

    std::uint32_t DeclareLocal(const std::string& name, TypeUVE type, const SourceLocationUVE at) {
        if (m_scopes.back().contains(name)) {
            Error(at, "'" + name + "' is already declared in this block");
        }
        const std::uint32_t slot = m_nextSlot++;
        SetLocalTypeUVE(slot, type);
        m_scopes.back()[name] = {slot, std::move(type)};
        m_chunk->localCount = std::max(m_chunk->localCount, m_nextSlot);
        return slot;
    }

    /// Slots are never reused within a chunk, so each has exactly one type.
    void SetLocalTypeUVE(const std::uint32_t slot, const TypeUVE& type) {
        if (m_chunk->localTypes.size() <= slot) {
            m_chunk->localTypes.resize(slot + 1U);
        }
        m_chunk->localTypes[slot] = type;
    }

    [[nodiscard]] const LocalUVE* FindLocal(const std::string& name) const {
        for (auto scope = m_scopes.rbegin(); scope != m_scopes.rend(); ++scope) {
            if (const auto it = scope->find(name); it != scope->end()) {
                return &it->second;
            }
        }
        return nullptr;
    }

    void CompileFieldInitializers(const FileUVE& file) {
        BeginChunk(m_program.init);
        m_inHandler = false;
        m_result = TypeUVE::NoneUVE();
        for (const FieldUVE& field : file.fields) {
            const auto it = m_fields.find(field.name);
            if (it == m_fields.end()) {
                continue;
            }
            const TypeUVE& type = m_program.fields[it->second].type;
            if (field.initializer) {
                const TypeUVE value = CompileExpr(*field.initializer);
                Coerce(value, type, field.initializer->at, "'" + field.name + "'");
            } else {
                PushValue(DefaultValue(type), field.at);
            }
            Emit(OpUVE::StoreField, static_cast<std::int32_t>(it->second), 0, field.at);
        }
        EndChunk({});
    }

    void CompileFunction(const FunctionUVE& function) {
        const auto it = m_functions.find(function.name);
        if (it == m_functions.end()) {
            return;
        }
        const FunctionSignatureUVE& signature = it->second;
        BeginChunk(m_program.functions[signature.index]);
        m_inHandler = false;
        m_result = signature.result;
        for (std::size_t i = 0U; i < function.params.size(); ++i) {
            DeclareLocal(function.params[i].name, signature.params[i], function.params[i].at);
        }
        CompileBlock(function.body);
        if (signature.result.kind != Kind::None && signature.result.kind != Kind::Error &&
            !EndsWithReturnUVE(function.body)) {
            Error(function.at, "'" + function.name + "' can reach its end without returning a " + signature.result.NameUVE());
        }
        EndChunk(function.at);
    }

    void CompileHandler(const HandlerUVE& handler) {
        const std::optional<std::vector<TypeUVE>> params = m_host.DescribeEventUVE(handler.event);
        if (!params.has_value()) {
            Error(handler.at, "this node has no event '" + handler.event + "'");
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
        m_program.functions.push_back({"on " + handler.event, static_cast<std::uint32_t>(params->size()), 0U, {}, {}, TypeUVE::NoneUVE()});
        m_program.handlers.emplace_back(handler.event, index);
        BeginChunk(m_program.functions.back());
        m_inHandler = true;
        m_result = TypeUVE::NoneUVE();
        for (std::size_t i = 0U; i < handler.params.size(); ++i) {
            const ParamUVE& param = handler.params[i];
            if (param.type.has_value()) {
                const std::optional<TypeUVE> written = ResolveType(*param.type);
                if (written.has_value() && written->kind != (*params)[i].kind) {
                    Error(param.at, "'" + param.name + "' is a " + (*params)[i].NameUVE() + " here, not " + written->NameUVE());
                }
            }
            DeclareLocal(param.name, (*params)[i], param.at);
        }
        CompileBlock(handler.body);
        EndChunk(handler.at);
    }

    // ------------------------------------------------------------ statements

    void CompileBlock(const BlockUVE& block) {
        m_scopes.emplace_back();
        for (const StmtPtrUVE& stmt : block) {
            CompileStatement(*stmt);
        }
        m_scopes.pop_back();
    }

    void CompileStatement(const StmtUVE& stmt) {
        switch (stmt.kind) {
            case StmtKindUVE::Let: {
                TypeUVE type = CompileExpr(*stmt.value);
                if (stmt.type.has_value()) {
                    const std::optional<TypeUVE> written = ResolveType(*stmt.type);
                    if (written.has_value()) {
                        Coerce(type, *written, stmt.value->at, "'" + stmt.name + "'");
                        type = *written;
                    }
                } else if (type.kind == Kind::None) {
                    Error(stmt.at, "'" + stmt.name + "' would be none - give it a value");
                }
                Emit(OpUVE::StoreLocal, static_cast<std::int32_t>(DeclareLocal(stmt.name, type, stmt.at)), 0, stmt.at);
                break;
            }
            case StmtKindUVE::Assign:
                CompileAssign(stmt);
                break;
            case StmtKindUVE::Expr:
                if (stmt.value->kind != ExprKindUVE::Call) {
                    Error(stmt.at, "this line works out a value and then drops it - did you mean to assign it?");
                }
                CompileExpr(*stmt.value);
                Emit(OpUVE::Pop, 0, 0, stmt.at);
                break;
            case StmtKindUVE::If: {
                std::vector<std::size_t> exits;
                for (const ConditionalBlockUVE& branch : stmt.branches) {
                    CompileCondition(*branch.condition);
                    const std::size_t skip = Emit(OpUVE::JumpIfFalse, 0, 0, branch.condition->at);
                    CompileBlock(branch.body);
                    exits.push_back(Emit(OpUVE::Jump, 0, 0, stmt.at));
                    Patch(skip);
                }
                CompileBlock(stmt.elseBody);
                for (const std::size_t exit : exits) {
                    Patch(exit);
                }
                break;
            }
            case StmtKindUVE::While: {
                const std::size_t top = m_chunk->code.size();
                CompileCondition(*stmt.branches[0].condition);
                const std::size_t exit = Emit(OpUVE::JumpIfFalse, 0, 0, stmt.at);
                m_loops.push_back({top, {}, {}});
                CompileBlock(stmt.branches[0].body);
                Emit(OpUVE::Jump, static_cast<std::int32_t>(top), 0, stmt.at);
                Patch(exit);
                for (const std::size_t brk : m_loops.back().breaks) {
                    Patch(brk);
                }
                m_loops.pop_back();
                break;
            }
            case StmtKindUVE::For:
                CompileFor(stmt);
                break;
            case StmtKindUVE::Return:
                if (m_inHandler && stmt.value) {
                    Error(stmt.at, "an 'on' block cannot return a value");
                } else if (stmt.value) {
                    const TypeUVE type = CompileExpr(*stmt.value);
                    if (m_result.kind == Kind::None) {
                        Error(stmt.at, "this function returns nothing - add '-> " + type.NameUVE() + "' to return a value");
                    } else {
                        Coerce(type, m_result, stmt.value->at, "the return value");
                    }
                    Emit(OpUVE::Return, 0, 0, stmt.at);
                } else {
                    if (m_result.kind != Kind::None && m_result.kind != Kind::Error) {
                        Error(stmt.at, "return a " + m_result.NameUVE() + " here");
                    }
                    Emit(OpUVE::ReturnNone, 0, 0, stmt.at);
                }
                break;
            case StmtKindUVE::Break:
            case StmtKindUVE::Continue:
                if (m_loops.empty()) {
                    Error(stmt.at, std::string{"'"} + (stmt.kind == StmtKindUVE::Break ? "break" : "continue") +
                                       "' only works inside a loop");
                } else if (stmt.kind == StmtKindUVE::Break) {
                    m_loops.back().breaks.push_back(Emit(OpUVE::Jump, 0, 0, stmt.at));
                } else if (m_loops.back().continueTarget != kUnknownTargetUVE) {
                    Emit(OpUVE::Jump, static_cast<std::int32_t>(m_loops.back().continueTarget), 0, stmt.at);
                } else {
                    m_loops.back().continues.push_back(Emit(OpUVE::Jump, 0, 0, stmt.at));
                }
                break;
            case StmtKindUVE::Pass:
                break;
            case StmtKindUVE::Wait: {
                if (!m_inHandler) {
                    Error(stmt.at, "'wait' only works inside an 'on' block");
                }
                const TypeUVE type = CompileExpr(*stmt.value);
                if (!type.IsNumericUVE() && type.kind != Kind::Error) {
                    Error(stmt.value->at, "'wait' needs a time, as in 'wait 0.5 s'");
                }
                Coerce(type, TypeUVE::FloatUVE(), stmt.value->at, "'wait'");
                Emit(OpUVE::Wait, 0, 0, stmt.at);
                break;
            }
        }
    }

    void CompileCondition(const ExprUVE& condition) {
        const TypeUVE type = CompileExpr(condition);
        if (type.kind != Kind::Bool && type.kind != Kind::Error) {
            Error(condition.at, "a condition must be true or false, not " + type.NameUVE());
        }
    }

    void CompileFor(const StmtUVE& stmt) {
        const ExprUVE& range = *stmt.value;
        if (range.kind != ExprKindUVE::Binary || range.text != "..") {
            Error(range.at, "'for' walks a range for now, as in 'for i in 0..10'");
            return;
        }
        m_scopes.emplace_back();
        const TypeUVE start = CompileExpr(*range.operands[0]);
        const std::uint32_t counter = DeclareLocal(stmt.name, TypeUVE::IntUVE(), stmt.at);
        Emit(OpUVE::StoreLocal, static_cast<std::int32_t>(counter), 0, stmt.at);
        const TypeUVE end = CompileExpr(*range.operands[1]);
        const std::uint32_t limit = m_nextSlot++;
        SetLocalTypeUVE(limit, TypeUVE::IntUVE());
        m_chunk->localCount = std::max(m_chunk->localCount, m_nextSlot);
        Emit(OpUVE::StoreLocal, static_cast<std::int32_t>(limit), 0, stmt.at);
        for (const TypeUVE& bound : {start, end}) {
            if (bound.kind != Kind::Int && bound.kind != Kind::Error) {
                Error(range.at, "a range counts whole numbers - both ends must be int");
                break;
            }
        }
        const std::size_t top = m_chunk->code.size();
        Emit(OpUVE::LoadLocal, static_cast<std::int32_t>(counter), 0, stmt.at);
        Emit(OpUVE::LoadLocal, static_cast<std::int32_t>(limit), 0, stmt.at);
        Emit(OpUVE::Lt, 0, 0, stmt.at);
        const std::size_t exit = Emit(OpUVE::JumpIfFalse, 0, 0, stmt.at);
        m_loops.push_back({kUnknownTargetUVE, {}, {}});
        CompileBlock(stmt.body);
        const std::size_t step = m_chunk->code.size();
        for (const std::size_t cont : m_loops.back().continues) {
            m_chunk->code[cont].a = static_cast<std::int32_t>(step);
        }
        Emit(OpUVE::LoadLocal, static_cast<std::int32_t>(counter), 0, stmt.at);
        PushValue(std::int64_t{1}, stmt.at);
        Emit(OpUVE::Add, 0, 0, stmt.at);
        Emit(OpUVE::StoreLocal, static_cast<std::int32_t>(counter), 0, stmt.at);
        Emit(OpUVE::Jump, static_cast<std::int32_t>(top), 0, stmt.at);
        Patch(exit);
        for (const std::size_t brk : m_loops.back().breaks) {
            Patch(brk);
        }
        m_loops.pop_back();
        m_scopes.pop_back();
    }

    /// Where an assignment writes: a local, a field or a host property.
    struct PlaceUVE final {
        OpUVE load = OpUVE::LoadLocal;
        OpUVE store = OpUVE::StoreLocal;
        std::int32_t operand = 0;
        TypeUVE type;
    };

    std::optional<PlaceUVE> ResolvePlace(const ExprUVE& target) {
        const std::string path = DottedPathUVE(target);
        if (target.kind == ExprKindUVE::Name) {
            if (const LocalUVE* local = FindLocal(target.text)) {
                return PlaceUVE{OpUVE::LoadLocal, OpUVE::StoreLocal, static_cast<std::int32_t>(local->slot), local->type};
            }
            if (const auto field = m_fields.find(target.text); field != m_fields.end()) {
                const FieldInfoUVE& info = m_program.fields[field->second];
                if (info.kind == FieldKindUVE::Const) {
                    Error(target.at, "'" + target.text + "' is a const and cannot change");
                    return std::nullopt;
                }
                return PlaceUVE{OpUVE::LoadField, OpUVE::StoreField, static_cast<std::int32_t>(field->second), info.type};
            }
        }
        if (!path.empty()) {
            if (const std::optional<HostPropertyUVE> property = m_host.DescribePropertyUVE(path)) {
                if (!property->writable) {
                    Error(target.at, "'" + path + "' can be read but not changed");
                    return std::nullopt;
                }
                const std::int32_t name = HostName(path, property->type);
                return PlaceUVE{OpUVE::LoadProp, OpUVE::StoreProp, name, property->type};
            }
        }
        Error(target.at, path.empty() ? "this cannot be assigned to" : "'" + path + "' is not a variable, field or property");
        return std::nullopt;
    }

    [[nodiscard]] static OpUVE CompoundOp(const std::string& op) noexcept {
        return op == "+=" ? OpUVE::Add : op == "-=" ? OpUVE::Sub : op == "*=" ? OpUVE::Mul : OpUVE::Div;
    }

    void CompileAssign(const StmtUVE& stmt) {
        const ExprUVE& target = *stmt.target;
        std::int32_t component = -1;
        const ExprUVE* placeExpr = &target;
        // `velocity.x = ...` writes one component of a vec3 place.
        if (target.kind == ExprKindUVE::Member && (target.text == "x" || target.text == "y" || target.text == "z") &&
            !m_host.DescribePropertyUVE(DottedPathUVE(target)).has_value()) {
            component = target.text == "x" ? 0 : target.text == "y" ? 1 : 2;
            placeExpr = target.operands[0].get();
        }
        const std::optional<PlaceUVE> place = ResolvePlace(*placeExpr);
        if (!place.has_value()) {
            return;
        }
        TypeUVE slotType = place->type;
        if (component >= 0) {
            if (place->type.kind != Kind::Vec3 && place->type.kind != Kind::Error) {
                Error(target.at, "'." + target.text + "' only exists on a vec3");
                return;
            }
            Emit(place->load, place->operand, 0, stmt.at);
            slotType = TypeUVE::FloatUVE();
        }
        if (stmt.op != "=") {
            if (component >= 0) {
                Emit(place->load, place->operand, 0, stmt.at);
                Emit(OpUVE::GetComponent, component, 0, stmt.at);
            } else {
                Emit(place->load, place->operand, 0, stmt.at);
            }
            const TypeUVE value = CompileExpr(*stmt.value);
            const TypeUVE result = CheckArithmetic(stmt.op.substr(0U, 1U), slotType, value, stmt.at, true);
            Emit(CompoundOp(stmt.op), 0, 0, stmt.at);
            if (result.kind != Kind::Error && result.kind != slotType.kind && slotType.kind != Kind::Error) {
                Error(stmt.at, "'" + stmt.op + "' would turn this " + slotType.NameUVE() + " into " + result.NameUVE());
            }
        } else {
            const TypeUVE value = CompileExpr(*stmt.value);
            Coerce(value, slotType, stmt.value->at, "this assignment");
        }
        if (component >= 0) {
            Emit(OpUVE::SetComponent, component, 0, stmt.at);
        }
        Emit(place->store, place->operand, 0, stmt.at);
    }

    // ------------------------------------------------------------ expressions

    /// Types a binary arithmetic operator whose left operand is compiled and whose right operand is
    /// the value just compiled. Only the right side can still be converted here, so a left int is
    /// converted by the caller before the right is compiled (see CompileBinary).
    TypeUVE CheckArithmetic(const std::string& op, const TypeUVE& left, const TypeUVE& right, const SourceLocationUVE at,
                            const bool leftAlreadyEmitted) {
        static_cast<void>(leftAlreadyEmitted);
        if (left.kind == Kind::Error || right.kind == Kind::Error) {
            return TypeUVE::ErrorUVE();
        }
        if (left.IsNumericUVE() && right.IsNumericUVE()) {
            if (left.kind == Kind::Float && right.kind == Kind::Int) {
                Emit(OpUVE::ToFloat, 0, 0, at);
            }
            if (op == "/") {
                return TypeUVE::FloatUVE();
            }
            return left.kind == Kind::Float || right.kind == Kind::Float ? TypeUVE::FloatUVE() : TypeUVE::IntUVE();
        }
        if (op == "+" && left.kind == Kind::Str && right.kind == Kind::Str) {
            return TypeUVE::StrUVE();
        }
        if ((op == "+" || op == "-") && left.kind == Kind::Vec3 && right.kind == Kind::Vec3) {
            return TypeUVE::Vec3UVE();
        }
        if ((op == "*" || op == "/") && left.kind == Kind::Vec3 && right.IsNumericUVE()) {
            if (right.kind == Kind::Int) {
                Emit(OpUVE::ToFloat, 0, 0, at);
            }
            return TypeUVE::Vec3UVE();
        }
        if (op == "*" && left.IsNumericUVE() && right.kind == Kind::Vec3) {
            return TypeUVE::Vec3UVE();
        }
        Error(at, "'" + op + "' does not work on " + left.NameUVE() + " and " + right.NameUVE());
        return TypeUVE::ErrorUVE();
    }

    /// The type an expression will have, without emitting anything.
    TypeUVE PeekType(const ExprUVE& expr) {
        ChunkUVE scratch;
        ChunkUVE* saved = m_chunk;
        m_chunk = &scratch;
        const std::size_t diagnostics = m_diagnostics.size();
        const std::size_t constants = m_program.constants.size();
        const TypeUVE type = CompileExpr(expr);
        m_diagnostics.resize(diagnostics);
        m_program.constants.resize(constants);
        m_chunk = saved;
        return type;
    }

    TypeUVE CompileBinary(const ExprUVE& expr) {
        const std::string& op = expr.text;
        const ExprUVE& leftExpr = *expr.operands[0];
        const ExprUVE& rightExpr = *expr.operands[1];
        if (op == "and" || op == "or") {
            const TypeUVE left = CompileExpr(leftExpr);
            const std::size_t jump = Emit(op == "and" ? OpUVE::JumpIfFalseKeep : OpUVE::JumpIfTrueKeep, 0, 0, expr.at);
            const TypeUVE right = CompileExpr(rightExpr);
            Patch(jump);
            for (const TypeUVE& side : {left, right}) {
                if (side.kind != Kind::Bool && side.kind != Kind::Error) {
                    Error(expr.at, "'" + op + "' joins true/false values, not " + side.NameUVE());
                    return TypeUVE::ErrorUVE();
                }
            }
            return TypeUVE::BoolUVE();
        }
        if (op == "..") {
            Error(expr.at, "a range only goes after 'for ... in'");
            return TypeUVE::ErrorUVE();
        }
        // The left side is converted to float before the right side is on the stack, when the
        // right side is a float (or the operator is '/').
        const TypeUVE leftType = CompileExpr(leftExpr);
        const TypeUVE rightPeek = PeekType(rightExpr);
        const bool isCompare = op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
        if (leftType.kind == Kind::Int && (rightPeek.kind == Kind::Float || op == "/")) {
            Emit(OpUVE::ToFloat, 0, 0, expr.at);
        }
        const TypeUVE rightType = CompileExpr(rightExpr);
        if (op == "/" && rightType.kind == Kind::Int && leftType.IsNumericUVE()) {
            Emit(OpUVE::ToFloat, 0, 0, expr.at);
        }
        const TypeUVE leftAfter = leftType.kind == Kind::Int && (rightPeek.kind == Kind::Float || op == "/")
                                      ? TypeUVE::FloatUVE()
                                      : leftType;
        const TypeUVE rightAfter = op == "/" && rightType.kind == Kind::Int && leftType.IsNumericUVE()
                                       ? TypeUVE::FloatUVE()
                                       : rightType;
        if (isCompare) {
            if (leftAfter.kind == Kind::Error || rightAfter.kind == Kind::Error) {
                return TypeUVE::BoolUVE();
            }
            if (leftAfter.kind == Kind::Float && rightAfter.kind == Kind::Int) {
                Emit(OpUVE::ToFloat, 0, 0, expr.at);
            }
            const bool numeric = leftAfter.IsNumericUVE() && rightAfter.IsNumericUVE();
            const bool ordered = op != "==" && op != "!=";
            if (ordered ? !numeric : !(numeric || leftAfter.kind == rightAfter.kind)) {
                Error(expr.at, "'" + op + "' cannot compare " + leftAfter.NameUVE() + " with " + rightAfter.NameUVE());
            }
            Emit(op == "==" ? OpUVE::Eq : op == "!=" ? OpUVE::Ne : op == "<" ? OpUVE::Lt : op == "<=" ? OpUVE::Le
                 : op == ">" ? OpUVE::Gt : OpUVE::Ge, 0, 0, expr.at);
            return TypeUVE::BoolUVE();
        }
        const TypeUVE result = CheckArithmetic(op, leftAfter, rightAfter, expr.at, true);
        if (op == "%" && result.kind == Kind::Vec3) {
            Error(expr.at, "'%' does not work on vec3");
        }
        Emit(op == "+" ? OpUVE::Add : op == "-" ? OpUVE::Sub : op == "*" ? OpUVE::Mul : op == "/" ? OpUVE::Div : OpUVE::Mod,
             0, 0, expr.at);
        return result;
    }

    TypeUVE CompileExpr(const ExprUVE& expr) {
        switch (expr.kind) {
            case ExprKindUVE::Number: {
                if (expr.isInteger) {
                    PushValue(static_cast<std::int64_t>(expr.number), expr.at);
                    return TypeUVE::IntUVE();
                }
                double value = expr.number;
                if (expr.unit == "deg") value *= std::numbers::pi / 180.0;
                else if (expr.unit == "ms") value /= 1000.0;
                else if (expr.unit == "cm") value /= 100.0;
                else if (expr.unit == "km") value *= 1000.0;
                PushValue(value, expr.at);
                return TypeUVE::FloatUVE();
            }
            case ExprKindUVE::String:
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
                    Emit(OpUVE::Concat, count, 0, expr.at);
                }
                return TypeUVE::StrUVE();
            case ExprKindUVE::Bool:
                PushValue(expr.boolean, expr.at);
                return TypeUVE::BoolUVE();
            case ExprKindUVE::None:
                PushValue(std::monostate{}, expr.at);
                return TypeUVE::NoneUVE();
            case ExprKindUVE::Name:
                return CompileName(expr);
            case ExprKindUVE::Unary: {
                const TypeUVE type = CompileExpr(*expr.operands[0]);
                if (expr.text == "not") {
                    if (type.kind != Kind::Bool && type.kind != Kind::Error) {
                        Error(expr.at, "'not' works on true/false values, not " + type.NameUVE());
                    }
                    Emit(OpUVE::Not, 0, 0, expr.at);
                    return TypeUVE::BoolUVE();
                }
                if (!type.IsNumericUVE() && type.kind != Kind::Vec3 && type.kind != Kind::Error) {
                    Error(expr.at, "'-' does not work on " + type.NameUVE());
                    return TypeUVE::ErrorUVE();
                }
                Emit(OpUVE::Neg, 0, 0, expr.at);
                return type;
            }
            case ExprKindUVE::Binary:
                return CompileBinary(expr);
            case ExprKindUVE::Member:
                return CompileMember(expr);
            case ExprKindUVE::Call:
                return CompileCall(expr);
            case ExprKindUVE::Index:
                Error(expr.at, "indexing with [] comes with collections, which are not in UVScript yet");
                return TypeUVE::ErrorUVE();
        }
        return TypeUVE::ErrorUVE();
    }

    TypeUVE CompileName(const ExprUVE& expr) {
        if (const LocalUVE* local = FindLocal(expr.text)) {
            Emit(OpUVE::LoadLocal, static_cast<std::int32_t>(local->slot), 0, expr.at);
            return local->type;
        }
        if (const auto field = m_fields.find(expr.text); field != m_fields.end()) {
            Emit(OpUVE::LoadField, static_cast<std::int32_t>(field->second), 0, expr.at);
            return m_program.fields[field->second].type;
        }
        if (const std::optional<HostPropertyUVE> property = m_host.DescribePropertyUVE(expr.text)) {
            Emit(OpUVE::LoadProp, HostName(expr.text, property->type), 0, expr.at);
            return property->type;
        }
        if (m_functions.contains(expr.text) || FindBuiltinUVE(expr.text) != nullptr) {
            Error(expr.at, "'" + expr.text + "' is a function - call it with ()");
        } else {
            Error(expr.at, "nothing is called '" + expr.text + "' here");
        }
        return TypeUVE::ErrorUVE();
    }

    TypeUVE CompileMember(const ExprUVE& expr) {
        const std::string path = DottedPathUVE(expr);
        const bool baseIsVariable = expr.operands[0]->kind == ExprKindUVE::Name &&
                                    (FindLocal(expr.operands[0]->text) != nullptr || m_fields.contains(expr.operands[0]->text));
        if (!path.empty() && !baseIsVariable) {
            if (const std::optional<HostPropertyUVE> property = m_host.DescribePropertyUVE(path)) {
                Emit(OpUVE::LoadProp, HostName(path, property->type), 0, expr.at);
                return property->type;
            }
        }
        const TypeUVE base = CompileExpr(*expr.operands[0]);
        if (base.kind == Kind::Vec3 && (expr.text == "x" || expr.text == "y" || expr.text == "z")) {
            Emit(OpUVE::GetComponent, expr.text == "x" ? 0 : expr.text == "y" ? 1 : 2, 0, expr.at);
            return TypeUVE::FloatUVE();
        }
        if (base.kind != Kind::Error) {
            Error(expr.at, base.NameUVE() + " has no '" + expr.text + "'");
        }
        return TypeUVE::ErrorUVE();
    }

    /// Compiles the arguments against `params`, converting int to float where a float is wanted.
    bool CompileArguments(const ExprUVE& call, const std::vector<TypeUVE>& params, const std::string& name) {
        const std::size_t given = call.operands.size() - 1U;
        if (given != params.size()) {
            Error(call.at, "'" + name + "' takes " + std::to_string(params.size()) + " value(s), not " + std::to_string(given));
            return false;
        }
        for (std::size_t i = 0U; i < given; ++i) {
            const TypeUVE type = CompileExpr(*call.operands[i + 1U]);
            Coerce(type, params[i], call.operands[i + 1U]->at, "'" + name + "' argument " + std::to_string(i + 1U));
        }
        return true;
    }

    TypeUVE CompileCall(const ExprUVE& call) {
        const ExprUVE& callee = *call.operands[0];
        const std::string path = DottedPathUVE(callee);
        const auto argc = static_cast<std::int32_t>(call.operands.size() - 1U);
        if (callee.kind == ExprKindUVE::Name) {
            if (const auto fn = m_functions.find(path); fn != m_functions.end()) {
                CompileArguments(call, fn->second.params, path);
                Emit(OpUVE::Call, static_cast<std::int32_t>(fn->second.index), argc, call.at);
                return fn->second.result;
            }
            if (const BuiltinInfoUVE* builtin = FindBuiltinUVE(path)) {
                return CompileBuiltin(call, *builtin);
            }
        }
        if (!path.empty()) {
            if (const std::optional<HostFunctionUVE> fn = m_host.DescribeFunctionUVE(path)) {
                CompileArguments(call, fn->params, path);
                Emit(OpUVE::CallHost, HostName(path, fn->result), argc, call.at);
                return fn->result;
            }
        }
        Error(call.at, path.empty() ? "only functions can be called" : "there is no function '" + path + "'");
        return TypeUVE::ErrorUVE();
    }

    TypeUVE CompileBuiltin(const ExprUVE& call, const BuiltinInfoUVE& builtin) {
        const std::size_t given = call.operands.size() - 1U;
        if (given != builtin.arity) {
            Error(call.at, "'" + std::string{builtin.name} + "' takes " + std::to_string(builtin.arity) + " value(s), not " +
                               std::to_string(given));
            return TypeUVE::ErrorUVE();
        }
        const auto emit = [&] { Emit(OpUVE::CallBuiltin, static_cast<std::int32_t>(builtin.id), static_cast<std::int32_t>(given), call.at); };
        const auto args = [&](const TypeUVE& want) {
            bool ok = true;
            for (std::size_t i = 1U; i < call.operands.size(); ++i) {
                ok = Coerce(CompileExpr(*call.operands[i]), want, call.operands[i]->at, "'" + std::string{builtin.name} + "'") && ok;
            }
            return ok;
        };
        switch (builtin.id) {
            case BuiltinUVE::Print:
            case BuiltinUVE::Str:
                CompileExpr(*call.operands[1]);
                emit();
                return builtin.id == BuiltinUVE::Print ? TypeUVE::NoneUVE() : TypeUVE::StrUVE();
            case BuiltinUVE::Int:
            case BuiltinUVE::Float: {
                const TypeUVE type = CompileExpr(*call.operands[1]);
                if (!type.IsNumericUVE() && type.kind != Kind::Error) {
                    Error(call.at, "'" + std::string{builtin.name} + "' converts numbers, not " + type.NameUVE());
                }
                emit();
                return builtin.id == BuiltinUVE::Int ? TypeUVE::IntUVE() : TypeUVE::FloatUVE();
            }
            case BuiltinUVE::Min:
            case BuiltinUVE::Max:
            case BuiltinUVE::Abs:
            case BuiltinUVE::Clamp: {
                // Whole numbers stay whole; any float makes the result a float.
                bool anyFloat = false;
                for (std::size_t i = 1U; i < call.operands.size(); ++i) {
                    anyFloat = anyFloat || PeekType(*call.operands[i]).kind == Kind::Float;
                }
                args(anyFloat ? TypeUVE::FloatUVE() : TypeUVE::IntUVE());
                emit();
                return anyFloat ? TypeUVE::FloatUVE() : TypeUVE::IntUVE();
            }
            case BuiltinUVE::Vec3:
                args(TypeUVE::FloatUVE());
                emit();
                return TypeUVE::Vec3UVE();
            case BuiltinUVE::Length:
            case BuiltinUVE::Normalize:
                args(TypeUVE::Vec3UVE());
                emit();
                return builtin.id == BuiltinUVE::Length ? TypeUVE::FloatUVE() : TypeUVE::Vec3UVE();
            default:
                args(TypeUVE::FloatUVE());
                emit();
                return TypeUVE::FloatUVE();
        }
    }

    static constexpr std::size_t kUnknownTargetUVE = static_cast<std::size_t>(-1);

    struct LoopUVE final {
        std::size_t continueTarget = kUnknownTargetUVE;
        std::vector<std::size_t> breaks;
        std::vector<std::size_t> continues;
    };

    const UVScriptHostUVE& m_host;
    std::vector<DiagnosticUVE>& m_diagnostics;
    ProgramUVE& m_program;
    std::unordered_map<std::string, std::uint32_t> m_fields;
    std::unordered_map<std::string, FunctionSignatureUVE> m_functions;
    ChunkUVE* m_chunk = nullptr;
    std::vector<std::unordered_map<std::string, LocalUVE>> m_scopes;
    std::uint32_t m_nextSlot = 0U;
    std::vector<LoopUVE> m_loops;
    bool m_inHandler = false;
    TypeUVE m_result;
};

} // namespace

CompileResultUVE CompileUVScriptUVE(const FileUVE& file, const UVScriptHostUVE& host) {
    auto program = std::make_shared<ProgramUVE>();
    CompileResultUVE result;
    CompilerUVE(host, result.diagnostics, *program).Run(file);
    std::ranges::stable_sort(result.diagnostics, [](const DiagnosticUVE& a, const DiagnosticUVE& b) {
        return a.at.line != b.at.line ? a.at.line < b.at.line : a.at.column < b.at.column;
    });
    if (result.diagnostics.empty()) {
        program->fingerprint = ComputeProgramFingerprintUVE(*program);
        result.program = std::move(program);
    }
    return result;
}

CompileResultUVE CompileUVScriptSourceUVE(const std::string_view source, const UVScriptHostUVE& host) {
    ParseResultUVE parsed = ParseUVScriptUVE(source);
    if (!parsed.IsSuccessUVE()) {
        return {nullptr, std::move(parsed.diagnostics)};
    }
    return CompileUVScriptUVE(parsed.file, host);
}

std::vector<FieldInfoUVE> GetProgramFieldsUVE(const ProgramUVE& program) {
    return program.fields;
}

std::uint64_t GetProgramFingerprintUVE(const ProgramUVE& program) {
    return program.fingerprint;
}

std::uint64_t ComputeProgramFingerprintUVE(const ProgramUVE& program) {
    // FNV-1a over a plain serialization; stable across runs and builds.
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
    const auto type = [&number, &text](const TypeUVE& value) {
        number(static_cast<std::uint64_t>(value.kind));
        text(value.object);
    };
    const auto chunk = [&](const ChunkUVE& value) {
        text(value.name);
        number(value.paramCount);
        number(value.localCount);
        type(value.result);
        number(value.localTypes.size());
        for (const TypeUVE& local : value.localTypes) {
            type(local);
        }
        number(value.code.size());
        for (const InstructionUVE& in : value.code) {
            number(static_cast<std::uint64_t>(in.op));
            number(static_cast<std::uint64_t>(static_cast<std::int64_t>(in.a)));
            number(static_cast<std::uint64_t>(static_cast<std::int64_t>(in.b)));
            number(in.line);
        }
    };
    number(program.constants.size());
    for (const ValueUVE& constant : program.constants) {
        number(constant.index());
        text(FormatValueUVE(constant));
        if (const auto* real = std::get_if<double>(&constant)) {
            bytes(real, sizeof(*real)); // exact bits: the text form rounds
        } else if (const auto* vector = std::get_if<Vec3ValueUVE>(&constant)) {
            bytes(&vector->x, sizeof(double));
            bytes(&vector->y, sizeof(double));
            bytes(&vector->z, sizeof(double));
        }
    }
    number(program.fields.size());
    for (const FieldInfoUVE& field : program.fields) {
        text(field.name);
        type(field.type);
        number(static_cast<std::uint64_t>(field.kind));
    }
    chunk(program.init);
    number(program.functions.size());
    for (const ChunkUVE& function : program.functions) {
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

} // namespace UVE::UVScript
