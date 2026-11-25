// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Bytecode to C++. Each chunk becomes one function; each instruction becomes a block of straight
// C++ that calls the same value operations as the interpreter (uvscript_native_uve.h), with jumps
// as `goto` and every `wait` as a return that the next call resumes from. The result keeps the
// interpreter's semantics exactly - including the instruction budget and line numbers in errors -
// while dropping its fetch-decode-dispatch loop.

#include "blueprint_codegen.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <vector>
#include <string>
#include <variant>

#include "blueprint_program.h"

namespace UVE::Blueprint {
namespace {

[[nodiscard]] std::string Hex(const std::uint64_t value) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "0x%016llxULL", static_cast<unsigned long long>(value));
    return buffer;
}

/// A double written so the compiler reads back exactly the same bits.
[[nodiscard]] std::string DoubleLiteral(const double value) {
    return "std::bit_cast<double>(std::uint64_t{" + Hex(std::bit_cast<std::uint64_t>(value)) + "})";
}

/// A C++ string literal. Anything unusual is written as a three-digit octal escape, which unlike
/// \x cannot swallow the character after it.
[[nodiscard]] std::string StringLiteral(const std::string_view text) {
    std::string out = "\"";
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            out += '\\';
            out += character;
        } else if (byte >= 0x20U && byte < 0x7FU && character != '?') {
            out += character;
        } else {
            char escape[8];
            std::snprintf(escape, sizeof(escape), "\\%03o", byte);
            out += escape;
        }
    }
    return out + "\"";
}

[[nodiscard]] std::string ValueLiteral(const Value& value) {
    std::function<std::string(const Value&)> emit;
    emit = [&](const Value& current) -> std::string {
        return std::visit(
            [&](const auto& v) -> std::string {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return "Value{}";
                } else if constexpr (std::is_same_v<T, bool>) {
                    return v ? "Value{true}" : "Value{false}";
                } else if constexpr (std::is_same_v<T, std::int64_t>) {
                    return "Value{std::int64_t{" + std::to_string(v) + "}}";
                } else if constexpr (std::is_same_v<T, double>) {
                    return "Value{" + DoubleLiteral(v) + "}";
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return "Value{std::string{" + StringLiteral(v) + "}}";
                } else if constexpr (std::is_same_v<T, Vec3Value>) {
                    return "Value{Vec3Value{" + DoubleLiteral(v.x) + ", " + DoubleLiteral(v.y) + ", " +
                           DoubleLiteral(v.z) + "}}";
                } else if constexpr (std::is_same_v<T, ObjectRef>) {
                    return "Value{ObjectRef{" + std::to_string(v.id) + "U}}";
                } else {
                    const char* maker = v->kind == CollectionValue::Kind::List     ? "MakeListValue"
                                        : v->kind == CollectionValue::Kind::Map     ? "MakeMapValue"
                                                                                         : "MakeTupleValue";
                    std::string out = std::string{maker} + "(std::vector<Value>{";
                    for (const Value& item : v->items) {
                        out += emit(item) + ", ";
                    }
                    return out + "})";
                }
            },
            current);
    };
    return emit(value);
}

[[nodiscard]] const char* ArithmeticName(const Op op) {
    switch (op) {
        case Op::Sub: return "Sub";
        case Op::Mul: return "Mul";
        case Op::Div: return "Div";
        case Op::Mod: return "Mod";
        default: return "Add";
    }
}

[[nodiscard]] const char* CompareName(const Op op) {
    switch (op) {
        case Op::Le: return "Le";
        case Op::Gt: return "Gt";
        case Op::Ge: return "Ge";
        default: return "Lt";
    }
}


// ---- Typed chunks. A chunk that never waits and whose every value has a type the checker knows
// is written with plain C++ variables (double, int64_t, bool, std::string, Vec3Value) instead
// of Value, one variable per local and per stack depth. It performs the same operations in the
// same order with the same checks and messages as Arithmetic and friends, so it still agrees
// with the interpreter exactly - it just stops paying for the variant on every step.

enum class Slot : std::uint8_t {
    Bool,
    Int,
    Float,
    Str,
    Vec3,
    None,
};

[[nodiscard]] std::optional<Slot> SlotOfType(const Type& type) {
    switch (type.kind) {
        case Type::Kind::Bool: return Slot::Bool;
        case Type::Kind::Int: return Slot::Int;
        case Type::Kind::Float: return Slot::Float;
        case Type::Kind::Str: return Slot::Str;
        case Type::Kind::Vec3: return Slot::Vec3;
        case Type::Kind::None: return Slot::None;
        default: return std::nullopt;
    }
}

[[nodiscard]] std::optional<Slot> SlotOfValue(const Value& value) {
    switch (value.index()) {
        case 0U: return Slot::None;
        case 1U: return Slot::Bool;
        case 2U: return Slot::Int;
        case 3U: return Slot::Float;
        case 4U: return Slot::Str;
        case 5U: return Slot::Vec3;
        default: return std::nullopt;
    }
}

[[nodiscard]] const char* CppType(const Slot slot) {
    switch (slot) {
        case Slot::Bool: return "bool";
        case Slot::Int: return "std::int64_t";
        case Slot::Float: return "double";
        case Slot::Str: return "std::string";
        case Slot::Vec3: return "Vec3Value";
        case Slot::None: return "void";
    }
    return "void";
}

[[nodiscard]] char Suffix(const Slot slot) {
    switch (slot) {
        case Slot::Bool: return 'b';
        case Slot::Int: return 'i';
        case Slot::Float: return 'd';
        case Slot::Str: return 's';
        case Slot::Vec3: return 'v';
        case Slot::None: return 'n';
    }
    return 'n';
}

[[nodiscard]] bool IsNumberSlot(const Slot slot) noexcept { return slot == Slot::Int || slot == Slot::Float; }

/// What a typed chunk looks like: the stack's types before every instruction, and the locals'.
struct TypedPlan final {
    std::vector<Slot> locals;
    Slot result = Slot::None;
    std::vector<std::optional<std::vector<Slot>>> stackBefore;
};

/// The result type of a builtin given its argument types, as CallBuiltin computes it.
[[nodiscard]] std::optional<Slot> BuiltinResult(const Builtin id, const std::span<const Slot> args) {
    const bool allInt = std::ranges::all_of(args, [](const Slot s) { return s == Slot::Int; });
    switch (id) {
        case Builtin::Print: return Slot::None;
        case Builtin::Str: return Slot::Str;
        case Builtin::Sqrt:
        case Builtin::Sin:
        case Builtin::Cos:
        case Builtin::Floor:
        case Builtin::Lerp:
        case Builtin::Float: return Slot::Float;
        // `length` is overloaded: a vec3's magnitude stays a float, a str's count is whole.
        // Collections never reach the typed planner, so anything else bails out below.
        case Builtin::Length:
            if (!args.empty() && args[0] == Slot::Vec3) {
                return Slot::Float;
            }
            return !args.empty() && args[0] == Slot::Str ? std::optional<Slot>{Slot::Int} : std::nullopt;
        case Builtin::Int: return Slot::Int;
        case Builtin::Abs:
        case Builtin::Min:
        case Builtin::Max:
        case Builtin::Clamp: return allInt ? Slot::Int : Slot::Float;
        case Builtin::Vec3:
        case Builtin::Normalize: return Slot::Vec3;
        case Builtin::Push:
        case Builtin::Keys:
        case Builtin::Contains:
        case Builtin::Remove: return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Slot> ArithmeticResult(const Op op, const Slot a, const Slot b) {
    if (a == Slot::Int && b == Slot::Int) {
        return op == Op::Div ? Slot::Float : Slot::Int;
    }
    if (IsNumberSlot(a) && IsNumberSlot(b)) {
        return Slot::Float;
    }
    if (a == Slot::Str && b == Slot::Str && op == Op::Add) {
        return Slot::Str;
    }
    if (a == Slot::Vec3 && b == Slot::Vec3 && (op == Op::Add || op == Op::Sub)) {
        return Slot::Vec3;
    }
    if (a == Slot::Vec3 && IsNumberSlot(b) && (op == Op::Mul || op == Op::Div)) {
        return Slot::Vec3;
    }
    if (IsNumberSlot(a) && b == Slot::Vec3 && op == Op::Mul) {
        return Slot::Vec3;
    }
    return std::nullopt;
}

/// Works out every stack type, or nothing when the chunk cannot be typed (it waits, uses an object
/// value, or merges different types at a jump target).
[[nodiscard]] std::optional<TypedPlan> PlanTypedChunk(const Program& program, const Chunk& chunk) {
    TypedPlan plan;
    for (std::size_t i = 0U; i < chunk.localCount; ++i) {
        const std::optional<Slot> slot =
            i < chunk.localTypes.size() ? SlotOfType(chunk.localTypes[i]) : std::nullopt;
        if (!slot.has_value() || *slot == Slot::None) {
            return std::nullopt;
        }
        plan.locals.push_back(*slot);
    }
    const std::optional<Slot> result = SlotOfType(chunk.result);
    if (!result.has_value()) {
        return std::nullopt;
    }
    plan.result = *result;
    const auto hostType = [&program](const std::int32_t constant) -> std::optional<Slot> {
        for (const auto& [index, type] : program.hostTypes) {
            if (index == static_cast<std::uint32_t>(constant)) {
                return SlotOfType(type);
            }
        }
        return std::nullopt;
    };

    plan.stackBefore.assign(chunk.code.size() + 1U, std::nullopt);
    std::vector<std::size_t> work{0U};
    plan.stackBefore[0] = std::vector<Slot>{};
    const auto flow = [&plan, &work](const std::size_t target, const std::vector<Slot>& stack) {
        if (target >= plan.stackBefore.size()) {
            return false;
        }
        if (!plan.stackBefore[target].has_value()) {
            plan.stackBefore[target] = stack;
            work.push_back(target);
            return true;
        }
        return *plan.stackBefore[target] == stack;
    };
    while (!work.empty()) {
        const std::size_t n = work.back();
        work.pop_back();
        if (n >= chunk.code.size()) {
            continue;
        }
        const Instruction& in = chunk.code[n];
        std::vector<Slot> s = *plan.stackBefore[n];
        const auto need = [&s](const std::size_t count) { return s.size() >= count; };
        bool fallsThrough = true;
        switch (in.op) {
            case Op::PushConst: {
                const std::optional<Slot> slot = SlotOfValue(program.constants[static_cast<std::size_t>(in.a)]);
                if (!slot.has_value()) return std::nullopt;
                s.push_back(*slot);
                break;
            }
            case Op::Pop:
                if (!need(1U)) return std::nullopt;
                s.pop_back();
                break;
            case Op::LoadLocal: s.push_back(plan.locals[static_cast<std::size_t>(in.a)]); break;
            case Op::StoreLocal:
                if (!need(1U) || s.back() != plan.locals[static_cast<std::size_t>(in.a)]) return std::nullopt;
                s.pop_back();
                break;
            case Op::LoadField: {
                const std::optional<Slot> slot = SlotOfType(program.fields[static_cast<std::size_t>(in.a)].type);
                if (!slot.has_value() || *slot == Slot::None) return std::nullopt;
                s.push_back(*slot);
                break;
            }
            case Op::StoreField: {
                const std::optional<Slot> slot = SlotOfType(program.fields[static_cast<std::size_t>(in.a)].type);
                if (!need(1U) || slot != s.back()) return std::nullopt;
                s.pop_back();
                break;
            }
            case Op::LoadProp: {
                const std::optional<Slot> slot = hostType(in.a);
                if (!slot.has_value() || *slot == Slot::None) return std::nullopt;
                s.push_back(*slot);
                break;
            }
            case Op::StoreProp:
                if (!need(1U) || hostType(in.a) != s.back()) return std::nullopt;
                s.pop_back();
                break;
            case Op::Add:
            case Op::Sub:
            case Op::Mul:
            case Op::Div:
            case Op::Mod: {
                if (!need(2U)) return std::nullopt;
                const std::optional<Slot> r = ArithmeticResult(in.op, s[s.size() - 2U], s.back());
                if (!r.has_value()) return std::nullopt;
                s.pop_back();
                s.back() = *r;
                break;
            }
            case Op::Neg:
                if (!need(1U) || (!IsNumberSlot(s.back()) && s.back() != Slot::Vec3)) return std::nullopt;
                break;
            case Op::Not:
                if (!need(1U) || s.back() != Slot::Bool) return std::nullopt;
                break;
            case Op::Eq:
            case Op::Ne: {
                if (!need(2U)) return std::nullopt;
                const Slot a = s[s.size() - 2U];
                const Slot b = s.back();
                if (!(IsNumberSlot(a) && IsNumberSlot(b)) && a != b) return std::nullopt;
                if (a == Slot::None) return std::nullopt;
                s.pop_back();
                s.back() = Slot::Bool;
                break;
            }
            case Op::Lt:
            case Op::Le:
            case Op::Gt:
            case Op::Ge:
                if (!need(2U) || !IsNumberSlot(s[s.size() - 2U]) || !IsNumberSlot(s.back())) return std::nullopt;
                s.pop_back();
                s.back() = Slot::Bool;
                break;
            case Op::ToFloat:
                if (!need(1U) || !IsNumberSlot(s.back())) return std::nullopt;
                s.back() = Slot::Float;
                break;
            case Op::GetComponent:
                if (!need(1U) || s.back() != Slot::Vec3) return std::nullopt;
                s.back() = Slot::Float;
                break;
            case Op::SetComponent:
                if (!need(2U) || s[s.size() - 2U] != Slot::Vec3 || !IsNumberSlot(s.back())) return std::nullopt;
                s.pop_back();
                break;
            case Op::Concat:
                if (!need(static_cast<std::size_t>(in.a))) return std::nullopt;
                s.resize(s.size() - static_cast<std::size_t>(in.a));
                s.push_back(Slot::Str);
                break;
            case Op::Jump:
                if (!flow(static_cast<std::size_t>(in.a), s)) return std::nullopt;
                fallsThrough = false;
                break;
            case Op::JumpIfFalse:
                if (!need(1U) || s.back() != Slot::Bool) return std::nullopt;
                s.pop_back();
                if (!flow(static_cast<std::size_t>(in.a), s)) return std::nullopt;
                break;
            case Op::JumpIfFalseKeep:
            case Op::JumpIfTrueKeep:
                if (!need(1U) || s.back() != Slot::Bool) return std::nullopt;
                if (!flow(static_cast<std::size_t>(in.a), s)) return std::nullopt;
                s.pop_back();
                break;
            case Op::Call: {
                const Chunk& callee = program.functions[static_cast<std::size_t>(in.a)];
                const std::optional<Slot> r = SlotOfType(callee.result);
                if (!need(static_cast<std::size_t>(in.b)) || !r.has_value()) return std::nullopt;
                for (std::size_t i = 0U; i < static_cast<std::size_t>(in.b); ++i) {
                    const std::optional<Slot> param =
                        i < callee.localTypes.size() ? SlotOfType(callee.localTypes[i]) : std::nullopt;
                    if (param != s[s.size() - static_cast<std::size_t>(in.b) + i]) return std::nullopt;
                }
                s.resize(s.size() - static_cast<std::size_t>(in.b));
                s.push_back(*r);
                break;
            }
            case Op::CallBuiltin: {
                if (!need(static_cast<std::size_t>(in.b))) return std::nullopt;
                const std::vector<Slot> args(s.end() - in.b, s.end());
                if (std::ranges::any_of(args, [](const Slot a) { return a == Slot::None; })) return std::nullopt;
                const std::optional<Slot> r = BuiltinResult(static_cast<Builtin>(in.a), args);
                if (!r.has_value()) return std::nullopt;
                s.resize(s.size() - static_cast<std::size_t>(in.b));
                s.push_back(*r);
                break;
            }
            case Op::CallHost: {
                const std::optional<Slot> r = hostType(in.a);
                if (!need(static_cast<std::size_t>(in.b)) || !r.has_value()) return std::nullopt;
                s.resize(s.size() - static_cast<std::size_t>(in.b));
                s.push_back(*r);
                break;
            }
            case Op::Return:
                if (!need(1U) || s.back() != plan.result) return std::nullopt;
                fallsThrough = false;
                break;
            case Op::ReturnNone: fallsThrough = false; break;
            case Op::CallMethod: return std::nullopt; // dynamic dispatch stays boxed, like Wait
            case Op::Wait: return std::nullopt;
            // Collections stay boxed: the typed planner only knows scalars and vec3.
            case Op::BuildList:
            case Op::BuildMap:
            case Op::BuildTuple:
            case Op::GetIndex:
            case Op::SetIndex:
            case Op::Unpack: return std::nullopt;
        }
        if (fallsThrough && !flow(n + 1U, s)) {
            return std::nullopt;
        }
    }
    return plan;
}

/// Emits one typed chunk as `T<i>(Context&, params...)`.
class TypedEmitter final {
public:
    TypedEmitter(const Program& program, const std::vector<std::optional<TypedPlan>>& plans, std::string& out)
        : m_program(program), m_plans(plans), m_out(out) {}

    [[nodiscard]] static std::string Signature(const Chunk& chunk, const TypedPlan& plan, const std::size_t index) {
        std::string text = std::string{CppType(plan.result)} + " T" + std::to_string(index) + "(Context& c";
        for (std::size_t i = 0U; i < chunk.paramCount; ++i) {
            text += ", " + std::string{CppType(plan.locals[i])} + " a" + std::to_string(i);
        }
        return text + ")";
    }

    void Emit(const Chunk& chunk, const TypedPlan& plan, const std::size_t index) {
        m_chunk = &chunk;
        m_plan = &plan;
        m_out += "// " + (chunk.name.empty() ? std::string{"field initializers"} : chunk.name) + " (typed)\n";
        m_out += Signature(chunk, plan, index) + " {\n";
        for (std::size_t i = 0U; i < plan.locals.size(); ++i) {
            m_out += "    [[maybe_unused]] " + std::string{CppType(plan.locals[i])} + " L" + std::to_string(i) +
                     (i < chunk.paramCount ? " = std::move(a" + std::to_string(i) + ");\n" : "{};\n");
        }
        // One variable per stack depth and type, declared up front so every goto is legal.
        std::set<std::pair<std::size_t, Slot>> slots;
        for (const auto& stack : plan.stackBefore) {
            if (stack.has_value()) {
                for (std::size_t d = 0U; d < stack->size(); ++d) {
                    if ((*stack)[d] != Slot::None) {
                        slots.emplace(d, (*stack)[d]);
                    }
                }
            }
        }
        for (std::size_t n = 0U; n < chunk.code.size(); ++n) {
            // Results written by an instruction whose next stack is not recorded (a return) need no slot.
            if (n + 1U < plan.stackBefore.size() && plan.stackBefore[n + 1U].has_value()) {
                for (std::size_t d = 0U; d < plan.stackBefore[n + 1U]->size(); ++d) {
                    if ((*plan.stackBefore[n + 1U])[d] != Slot::None) {
                        slots.emplace(d, (*plan.stackBefore[n + 1U])[d]);
                    }
                }
            }
        }
        for (const auto& [depth, slot] : slots) {
            m_out += "    [[maybe_unused]] " + std::string{CppType(slot)} + " " + Var(depth, slot) + "{};\n";
        }
        std::set<std::size_t> labels;
        for (const Instruction& in : chunk.code) {
            if (in.op == Op::Jump || in.op == Op::JumpIfFalse || in.op == Op::JumpIfFalseKeep ||
                in.op == Op::JumpIfTrueKeep) {
                labels.insert(static_cast<std::size_t>(in.a));
            }
        }
        for (std::size_t n = 0U; n < chunk.code.size(); ++n) {
            if (labels.contains(n)) {
                m_out += "I" + std::to_string(n) + ":\n";
            }
            if (!plan.stackBefore[n].has_value()) {
                continue; // unreachable
            }
            EmitInstruction(chunk.code[n], *plan.stackBefore[n]);
        }
        if (labels.contains(chunk.code.size())) {
            m_out += "I" + std::to_string(chunk.code.size()) + ":\n";
        }
        m_out += plan.result == Slot::None ? "    return;\n}\n\n" : "    return {};\n}\n\n";
    }

private:
    [[nodiscard]] static std::string Var(const std::size_t depth, const Slot slot) {
        return std::string{"s"} + std::to_string(depth) + Suffix(slot);
    }

    [[nodiscard]] static std::string AsDouble(const std::string& name, const Slot slot) {
        return slot == Slot::Int ? "static_cast<double>(" + name + ")" : name;
    }

    [[nodiscard]] static std::string Literal(const Value& value) {
        return std::visit(
            [](const auto& v) -> std::string {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, bool>) {
                    return v ? "true" : "false";
                } else if constexpr (std::is_same_v<T, std::int64_t>) {
                    return "std::int64_t{" + std::to_string(v) + "}";
                } else if constexpr (std::is_same_v<T, double>) {
                    return DoubleLiteral(v);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return "std::string{" + StringLiteral(v) + "}";
                } else if constexpr (std::is_same_v<T, Vec3Value>) {
                    return "Vec3Value{" + DoubleLiteral(v.x) + ", " + DoubleLiteral(v.y) + ", " +
                           DoubleLiteral(v.z) + "}";
                } else {
                    return "{}";
                }
            },
            value);
    }

    [[nodiscard]] static std::string Box(const std::string& name, const Slot slot) {
        return slot == Slot::None ? std::string{"Value{}"} : "Value{" + name + "}";
    }

    [[nodiscard]] static std::string Unbox(const std::string& value, const Slot slot) {
        return "std::get<" + std::string{CppType(slot)} + ">(" + value + ")";
    }

    [[nodiscard]] std::string Name(const std::int32_t constant) const {
        return StringLiteral(std::get<std::string>(m_program.constants[static_cast<std::size_t>(constant)]));
    }

    void EmitInstruction(const Instruction& in, const std::vector<Slot>& s) {
        const std::size_t d = s.size();
        const auto top = [&s, d](const std::size_t fromTop) { return Var(d - fromTop, s[d - fromTop]); };
        const auto topType = [&s, d](const std::size_t fromTop) { return s[d - fromTop]; };
        const std::string a = std::to_string(in.a);
        std::string body;
        switch (in.op) {
            case Op::PushConst: {
                const Value& value = m_program.constants[static_cast<std::size_t>(in.a)];
                const Slot slot = *SlotOfValue(value);
                if (slot != Slot::None) {
                    body = Var(d, slot) + " = " + Literal(value) + ";";
                }
                break;
            }
            case Op::Pop: break;
            case Op::LoadLocal:
                body = Var(d, m_plan->locals[static_cast<std::size_t>(in.a)]) + " = L" + a + ";";
                break;
            case Op::StoreLocal: body = "L" + a + " = std::move(" + top(1U) + ");"; break;
            case Op::LoadField: {
                const Slot slot = *SlotOfType(m_program.fields[static_cast<std::size_t>(in.a)].type);
                body = Var(d, slot) + " = " + Unbox("c.fields[" + a + "]", slot) + ";";
                break;
            }
            case Op::StoreField: body = "c.fields[" + a + "] = " + Box(top(1U), topType(1U)) + ";"; break;
            case Op::LoadProp: {
                const Slot slot = HostSlot(in.a);
                body = Var(d, slot) + " = " + Unbox("c.host.GetProperty(" + Name(in.a) + ")", slot) + ";";
                break;
            }
            case Op::StoreProp:
                body = "c.host.SetProperty(" + Name(in.a) + ", " + Box(top(1U), topType(1U)) + ");";
                break;
            case Op::Add:
            case Op::Sub:
            case Op::Mul:
            case Op::Div:
            case Op::Mod: body = Arithmetic(in.op, top(2U), topType(2U), top(1U), topType(1U), d - 2U); break;
            case Op::Neg: {
                const std::string x = top(1U);
                body = topType(1U) == Slot::Vec3 ? x + " = Vec3Value{-" + x + ".x, -" + x + ".y, -" + x + ".z};"
                                                    : x + " = -" + x + ";";
                break;
            }
            case Op::Not: body = top(1U) + " = !" + top(1U) + ";"; break;
            case Op::Eq:
            case Op::Ne: {
                const bool numbers = IsNumberSlot(topType(2U)) && IsNumberSlot(topType(1U));
                const std::string x = numbers ? AsDouble(top(2U), topType(2U)) : top(2U);
                const std::string y = numbers ? AsDouble(top(1U), topType(1U)) : top(1U);
                body = Var(d - 2U, Slot::Bool) + " = " + x + (in.op == Op::Eq ? " == " : " != ") + y + ";";
                break;
            }
            case Op::Lt:
            case Op::Le:
            case Op::Gt:
            case Op::Ge: {
                const char* op = in.op == Op::Lt ? " < " : in.op == Op::Le ? " <= " : in.op == Op::Gt ? " > " : " >= ";
                body = Var(d - 2U, Slot::Bool) + " = " + AsDouble(top(2U), topType(2U)) + op +
                       AsDouble(top(1U), topType(1U)) + ";";
                break;
            }
            case Op::ToFloat: body = Var(d - 1U, Slot::Float) + " = " + AsDouble(top(1U), topType(1U)) + ";"; break;
            case Op::GetComponent:
                body = Var(d - 1U, Slot::Float) + " = " + top(1U) + (in.a == 0 ? ".x;" : in.a == 1 ? ".y;" : ".z;");
                break;
            case Op::SetComponent:
                body = top(2U) + (in.a == 0 ? ".x" : in.a == 1 ? ".y" : ".z") + " = " + AsDouble(top(1U), topType(1U)) + ";";
                break;
            case Op::Concat: {
                std::string joined;
                for (std::size_t i = static_cast<std::size_t>(in.a); i > 0U; --i) {
                    joined += (joined.empty() ? "" : " + ") + std::string{"FormatValue("} + Box(top(i), topType(i)) + ")";
                }
                if (joined.empty()) {
                    joined = "std::string{}";
                }
                body = "{ std::string t = " + joined + "; " + Var(d - static_cast<std::size_t>(in.a), Slot::Str) +
                       " = std::move(t); }";
                break;
            }
            case Op::Jump: body = "goto I" + a + ";"; break;
            case Op::JumpIfFalse: body = "if (!" + top(1U) + ") goto I" + a + ";"; break;
            case Op::JumpIfFalseKeep: body = "if (!" + top(1U) + ") goto I" + a + ";"; break;
            case Op::JumpIfTrueKeep: body = "if (" + top(1U) + ") goto I" + a + ";"; break;
            case Op::Call: body = Call(in, s); break;
            case Op::CallBuiltin: body = EmitBuiltin(in, s); break;
            case Op::CallHost: {
                const std::size_t argc = static_cast<std::size_t>(in.b);
                std::string args;
                for (std::size_t i = argc; i > 0U; --i) {
                    args += (args.empty() ? "" : ", ") + Box(top(i), topType(i));
                }
                const Slot result = HostSlot(in.a);
                const std::string call = "c.host.CallFunction(" + Name(in.a) + ", std::vector<Value>{" + args + "})";
                body = result == Slot::None ? "static_cast<void>(" + call + ");"
                                               : Var(d - argc, result) + " = " + Unbox(call, result) + ";";
                break;
            }
            case Op::Return: body = "return " + top(1U) + ";"; break;
            case Op::ReturnNone: body = m_plan->result == Slot::None ? "return;" : "return {};"; break;
            case Op::CallMethod: break; // never typed
            case Op::Wait: break;       // never typed
            case Op::BuildList: break;  // never typed
            case Op::BuildMap: break;   // never typed
            case Op::BuildTuple: break; // never typed
            case Op::GetIndex: break;   // never typed
            case Op::SetIndex: break;   // never typed
            case Op::Unpack: break;     // never typed
        }
        m_out += "    { c.Step(" + std::to_string(in.line) + "U); " + body + " }\n";
    }

    [[nodiscard]] Slot HostSlot(const std::int32_t constant) const {
        for (const auto& [index, type] : m_program.hostTypes) {
            if (index == static_cast<std::uint32_t>(constant)) {
                return *SlotOfType(type);
            }
        }
        return Slot::None;
    }

    /// Arithmetic, specialised for the operand types this site always has.
    [[nodiscard]] static std::string Arithmetic(const Op op, const std::string& x, const Slot tx,
                                                const std::string& y, const Slot ty, const std::size_t depth) {
        const Slot rt = *ArithmeticResult(op, tx, ty);
        const std::string r = Var(depth, rt);
        if (tx == Slot::Int && ty == Slot::Int) {
            switch (op) {
                case Op::Add: return r + " = " + x + " + " + y + ";";
                case Op::Sub: return r + " = " + x + " - " + y + ";";
                case Op::Mul: return r + " = " + x + " * " + y + ";";
                case Op::Mod:
                    return "if (" + y + " == 0) throw Error{\"'%' by zero\"}; " + r + " = " + x + " % " + y + ";";
                default:
                    return "if (" + y + " == 0) throw Error{\"division by zero\"}; " + r + " = static_cast<double>(" + x +
                           ") / static_cast<double>(" + y + ");";
            }
        }
        if (IsNumberSlot(tx) && IsNumberSlot(ty)) {
            const std::string dx = AsDouble(x, tx);
            const std::string dy = AsDouble(y, ty);
            switch (op) {
                case Op::Add: return r + " = " + dx + " + " + dy + ";";
                case Op::Sub: return r + " = " + dx + " - " + dy + ";";
                case Op::Mul: return r + " = " + dx + " * " + dy + ";";
                case Op::Div:
                    return "if (" + dy + " == 0.0) throw Error{\"division by zero\"}; " + r + " = " + dx + " / " + dy + ";";
                default:
                    return "if (" + dy + " == 0.0) throw Error{\"'%' by zero\"}; " + r + " = std::fmod(" + dx + ", " + dy +
                           ");";
            }
        }
        if (tx == Slot::Str) {
            return r + " = " + x + " + " + y + ";";
        }
        if (tx == Slot::Vec3 && ty == Slot::Vec3) {
            const char* o = op == Op::Add ? " + " : " - ";
            return r + " = Vec3Value{" + x + ".x" + o + y + ".x, " + x + ".y" + o + y + ".y, " + x + ".z" + o + y + ".z};";
        }
        if (tx == Slot::Vec3) {
            const std::string sc = AsDouble(y, ty);
            if (op == Op::Mul) {
                return r + " = Vec3Value{" + x + ".x * " + sc + ", " + x + ".y * " + sc + ", " + x + ".z * " + sc + "};";
            }
            return "if (" + sc + " == 0.0) throw Error{\"division by zero\"}; " + r + " = Vec3Value{" + x + ".x / " + sc +
                   ", " + x + ".y / " + sc + ", " + x + ".z / " + sc + "};";
        }
        const std::string sc = AsDouble(x, tx);
        return r + " = Vec3Value{" + y + ".x * " + sc + ", " + y + ".y * " + sc + ", " + y + ".z * " + sc + "};";
    }

    [[nodiscard]] std::string Call(const Instruction& in, const std::vector<Slot>& s) const {
        const std::size_t d = s.size();
        const std::size_t argc = static_cast<std::size_t>(in.b);
        const std::size_t callee = static_cast<std::size_t>(in.a);
        const Slot result = *SlotOfType(m_program.functions[callee].result);
        const std::string guard = "if (c.depth + 1U >= 256U) throw Error{\"functions call each other too deeply\"}; ++c.depth; ";
        if (m_plans[callee].has_value()) {
            std::string args;
            for (std::size_t i = argc; i > 0U; --i) {
                args += ", " + Var(d - i, s[d - i]);
            }
            const std::string call = "T" + std::to_string(callee) + "(c" + args + ")";
            return guard + (result == Slot::None ? call + ";" : Var(d - argc, result) + " = " + call + ";") + " --c.depth;";
        }
        // A callee that could not be typed runs through the boxed entry point.
        const Chunk& target = m_program.functions[callee];
        std::string text = "Frame g; g.locals.resize(" + std::to_string(target.localCount) + "U); ";
        for (std::size_t i = argc; i > 0U; --i) {
            text += "g.locals[" + std::to_string(argc - i) + "] = " + Box(Var(d - i, s[d - i]), s[d - i]) + "; ";
        }
        text += "CallChunk(c, Chunk" + std::to_string(callee) + ", g);";
        if (result != Slot::None) {
            text += " " + Var(d - argc, result) + " = " + Unbox("g.result", result) + ";";
        }
        return text;
    }

    /// CallBuiltin, specialised by argument types.
    [[nodiscard]] static std::string EmitBuiltin(const Instruction& in, const std::vector<Slot>& s) {
        const std::size_t d = s.size();
        const std::size_t argc = static_cast<std::size_t>(in.b);
        const std::vector<Slot> types(s.end() - static_cast<std::ptrdiff_t>(argc), s.end());
        const auto arg = [&](const std::size_t i) { return Var(d - argc + i, s[d - argc + i]); };
        const auto num = [&](const std::size_t i) { return AsDouble(arg(i), s[d - argc + i]); };
        const Slot rt = *BuiltinResult(static_cast<Builtin>(in.a), types);
        const std::string r = rt == Slot::None ? std::string{} : Var(d - argc, rt);
        const bool allInt = rt == Slot::Int;
        switch (static_cast<Builtin>(in.a)) {
            case Builtin::Print: return "c.host.Print(FormatValue(" + Box(arg(0), types[0]) + "));";
            case Builtin::Str: return "{ std::string t = FormatValue(" + Box(arg(0), types[0]) + "); " + r + " = std::move(t); }";
            case Builtin::Sqrt:
                return "{ const double x = " + num(0) + "; if (x < 0.0) throw Error{\"sqrt of a negative number\"}; " + r +
                       " = std::sqrt(x); }";
            case Builtin::Sin: return r + " = std::sin(" + num(0) + ");";
            case Builtin::Cos: return r + " = std::cos(" + num(0) + ");";
            case Builtin::Floor: return r + " = std::floor(" + num(0) + ");";
            case Builtin::Lerp: return r + " = " + num(0) + " + (" + num(1) + " - " + num(0) + ") * " + num(2) + ";";
            case Builtin::Int: return r + " = static_cast<std::int64_t>(" + num(0) + ");";
            case Builtin::Float: return r + " = " + num(0) + ";";
            case Builtin::Abs: return allInt ? r + " = std::abs(" + arg(0) + ");" : r + " = std::fabs(" + num(0) + ");";
            case Builtin::Min:
                return allInt ? r + " = std::min(" + arg(0) + ", " + arg(1) + ");"
                              : r + " = std::min(" + num(0) + ", " + num(1) + ");";
            case Builtin::Max:
                return allInt ? r + " = std::max(" + arg(0) + ", " + arg(1) + ");"
                              : r + " = std::max(" + num(0) + ", " + num(1) + ");";
            case Builtin::Clamp:
                return allInt ? r + " = std::clamp(" + arg(0) + ", " + arg(1) + ", std::max(" + arg(1) + ", " + arg(2) + "));"
                              : r + " = std::clamp(" + num(0) + ", " + num(1) + ", std::max(" + num(1) + ", " + num(2) + "));";
            case Builtin::Vec3: return r + " = Vec3Value{" + num(0) + ", " + num(1) + ", " + num(2) + "};";
            case Builtin::Length:
                if (types[0] == Slot::Str) {
                    return r + " = static_cast<std::int64_t>(" + arg(0) + ".size());";
                }
                return r + " = std::sqrt(" + arg(0) + ".x * " + arg(0) + ".x + " + arg(0) + ".y * " + arg(0) + ".y + " + arg(0) +
                       ".z * " + arg(0) + ".z);";
            case Builtin::Normalize:
                return "{ const Vec3Value v = " + arg(0) +
                       "; const double n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); " + r +
                       " = n == 0.0 ? v : Vec3Value{v.x / n, v.y / n, v.z / n}; }";
            // Collection builtins never type: the planner bails before typed emission.
            case Builtin::Push:
            case Builtin::Keys:
            case Builtin::Contains:
            case Builtin::Remove: return {};
        }
        return {};
    }

    const Program& m_program;
    const std::vector<std::optional<TypedPlan>>& m_plans;
    std::string& m_out;
    const Chunk* m_chunk = nullptr;
    const TypedPlan* m_plan = nullptr;
};

class Generator final {
public:
    explicit Generator(const Program& program) : m_program(program) {}

    [[nodiscard]] std::string Run(const std::string_view origin) {
        const std::size_t initIndex = m_program.functions.size();
        m_out += "// Generated from " + std::string{origin} + " by uvsc. Do not edit: regenerate it.\n";
        m_out += "// Program fingerprint " + Hex(m_program.fingerprint) + ".\n\n";
        m_out += "#include <algorithm>\n#include <bit>\n#include <cmath>\n#include <cstdint>\n#include <cstdlib>\n#include <string>\n"
                 "#include <utility>\n#include <variant>\n#include <vector>\n\n";
        m_out += "#include \"blueprint_native.h\"\n\n";
        m_out += "namespace {\n\nusing namespace UVE::Blueprint;\nusing namespace UVE::Blueprint::Native;\n\n";
        if (!m_program.constants.empty()) {
            m_out += "const Value kConstants[] = {\n";
            for (const Value& constant : m_program.constants) {
                m_out += "    " + ValueLiteral(constant) + ",\n";
            }
            m_out += "};\n\n";
        }
        // Which chunks can be typed; the rest stay boxed.
        m_plans.clear();
        for (std::size_t i = 0U; i <= initIndex; ++i) {
            m_plans.push_back(PlanTypedChunk(m_program, ChunkAt(i)));
        }
        for (std::size_t i = 0U; i <= initIndex; ++i) {
            m_out += "Status Chunk" + std::to_string(i) + "(Context& c, Frame& f);\n";
            if (m_plans[i].has_value()) {
                m_out += TypedEmitter::Signature(ChunkAt(i), *m_plans[i], i) + ";\n";
            }
        }
        m_out += "\n";
        for (std::size_t i = 0U; i <= initIndex; ++i) {
            if (m_plans[i].has_value()) {
                TypedEmitter(m_program, m_plans, m_out).Emit(ChunkAt(i), *m_plans[i], i);
                TypedWrapper(ChunkAt(i), *m_plans[i], i);
            } else {
                EmitChunk(ChunkAt(i), i);
            }
        }
        m_out += "const ChunkFunction kChunks[] = {";
        for (std::size_t i = 0U; i <= initIndex; ++i) {
            m_out += (i == 0U ? "" : ", ") + std::string{"Chunk"} + std::to_string(i);
        }
        m_out += "};\n\n";
        m_out += "[[maybe_unused]] const Registration kRegistration{ProgramTable{" + Hex(m_program.fingerprint) +
                 ", kChunks, " + std::to_string(initIndex) + "U}};\n\n";
        m_out += "} // namespace\n";
        return std::move(m_out);
    }

    /// How many chunks came out typed (for tests and diagnostics).
    [[nodiscard]] std::size_t TypedCount() const {
        return static_cast<std::size_t>(std::ranges::count_if(m_plans, [](const auto& plan) { return plan.has_value(); }));
    }

private:
    [[nodiscard]] const Chunk& ChunkAt(const std::size_t index) const {
        return index == m_program.functions.size() ? m_program.init : m_program.functions[index];
    }

    [[nodiscard]] std::string Name(const std::int32_t constant) const {
        return StringLiteral(std::get<std::string>(m_program.constants[static_cast<std::size_t>(constant)]));
    }

    /// The uniform entry point for a typed chunk: unbox the arguments, call it, box the result.
    void TypedWrapper(const Chunk& chunk, const TypedPlan& plan, const std::size_t index) {
        m_out += "Status Chunk" + std::to_string(index) + "(Context& c, Frame& f) {\n    ";
        std::string call = "T" + std::to_string(index) + "(c";
        for (std::size_t i = 0U; i < chunk.paramCount; ++i) {
            call += ", std::get<" + std::string{CppType(plan.locals[i])} + ">(f.locals[" + std::to_string(i) + "])";
        }
        call += ")";
        m_out += plan.result == Slot::None ? call + ";\n    f.result = Value{};\n"
                                              : "f.result = Value{" + call + "};\n";
        m_out += "    f.resume = 0U;\n    return Status::Finished;\n}\n\n";
    }

    void EmitChunk(const Chunk& chunk, const std::size_t index) {
        // Labels only where something jumps or resumes: an unused label is a warning.
        std::set<std::size_t> labels;
        std::set<std::size_t> resumes;
        for (std::size_t n = 0U; n < chunk.code.size(); ++n) {
            const Instruction& in = chunk.code[n];
            if (in.op == Op::Jump || in.op == Op::JumpIfFalse || in.op == Op::JumpIfFalseKeep ||
                in.op == Op::JumpIfTrueKeep) {
                labels.insert(static_cast<std::size_t>(in.a));
            } else if (in.op == Op::Wait) {
                labels.insert(n + 1U);
                resumes.insert(n + 1U);
            }
        }
        m_out += "// " + (chunk.name.empty() ? std::string{"field initializers"} : chunk.name) + "\n";
        m_out += "Status Chunk" + std::to_string(index) + "(Context& c, Frame& f) {\n";
        m_out += "    [[maybe_unused]] std::vector<Value>& s = f.stack;\n";
        m_out += "    [[maybe_unused]] std::vector<Value>& l = f.locals;\n";
        m_out += "    switch (f.resume) {\n        case 0U: break;\n";
        for (const std::size_t resume : resumes) {
            m_out += "        case " + std::to_string(resume) + "U: goto I" + std::to_string(resume) + ";\n";
        }
        m_out += "        default: throw Error{\"a paused handler cannot continue\"};\n    }\n";
        for (std::size_t n = 0U; n < chunk.code.size(); ++n) {
            if (labels.contains(n)) {
                m_out += "I" + std::to_string(n) + ":\n";
            }
            EmitInstruction(chunk.code[n], n);
        }
        if (labels.contains(chunk.code.size())) {
            m_out += "I" + std::to_string(chunk.code.size()) + ":\n";
        }
        m_out += "    f.result = Value{};\n    return Status::Finished;\n}\n\n";
    }

    void EmitInstruction(const Instruction& in, const std::size_t n) {
        const std::string a = std::to_string(in.a);
        const std::string b = std::to_string(in.b);
        std::string body;
        switch (in.op) {
            case Op::PushConst: body = "s.push_back(kConstants[" + a + "]);"; break;
            case Op::Pop: body = "s.pop_back();"; break;
            case Op::LoadLocal: body = "s.push_back(l[" + a + "]);"; break;
            case Op::StoreLocal: body = "l[" + a + "] = Pop(s);"; break;
            case Op::LoadField: body = "s.push_back(c.fields[" + a + "]);"; break;
            case Op::StoreField: body = "c.fields[" + a + "] = Pop(s);"; break;
            case Op::LoadProp: body = "s.push_back(c.host.GetProperty(" + Name(in.a) + "));"; break;
            case Op::StoreProp:
                body = "const Value v = Pop(s); c.host.SetProperty(" + Name(in.a) + ", v);";
                break;
            case Op::Add:
            case Op::Sub:
            case Op::Mul:
            case Op::Div:
            case Op::Mod:
                body = "const Value y = Pop(s); const Value x = Pop(s); "
                       "s.push_back(Arithmetic(ArithmeticOp::" +
                       std::string{ArithmeticName(in.op)} + ", x, y));";
                break;
            case Op::Neg: body = "Value v = Negate(Pop(s)); s.push_back(std::move(v));"; break;
            case Op::Not: body = "const bool v = !AsBool(Pop(s)); s.push_back(v);"; break;
            case Op::Eq:
            case Op::Ne:
                body = "const Value y = Pop(s); const Value x = Pop(s); s.push_back(Equal(x, y) == " +
                       std::string{in.op == Op::Eq ? "true" : "false"} + ");";
                break;
            case Op::Lt:
            case Op::Le:
            case Op::Gt:
            case Op::Ge:
                body = "const Value y = Pop(s); const Value x = Pop(s); "
                       "s.push_back(Compare(CompareOp::" +
                       std::string{CompareName(in.op)} + ", x, y));";
                break;
            case Op::ToFloat: body = "const double v = AsDouble(Pop(s)); s.push_back(v);"; break;
            case Op::GetComponent: body = "Value v = GetComponent(Pop(s), " + a + "); s.push_back(std::move(v));"; break;
            case Op::SetComponent:
                body = "const Value v = Pop(s); const Value vec = Pop(s); "
                       "s.push_back(SetComponent(vec, " + a + ", v));";
                break;
            case Op::BuildList:
                body = "Value v = BuildList(s, " + a + "U); s.push_back(std::move(v));";
                break;
            case Op::BuildMap: body = "Value v = BuildMap(s, " + a + "U); s.push_back(std::move(v));"; break;
            case Op::BuildTuple:
                body = "Value v = BuildTuple(s, " + a + "U); s.push_back(std::move(v));";
                break;
            case Op::GetIndex:
                body = "const Value k = Pop(s); const Value box = Pop(s); s.push_back(GetIndex(box, k));";
                break;
            case Op::SetIndex:
                body = "const Value v = Pop(s); const Value k = Pop(s); const Value box = Pop(s); "
                       "s.push_back(SetIndex(box, k, v));";
                break;
            case Op::Unpack: body = "Unpack(s, " + a + "U);"; break;
            case Op::Concat: body = "Value t = Concat(s, " + a + "U); s.push_back(std::move(t));"; break;
            case Op::Jump: body = "goto I" + a + ";"; break;
            case Op::JumpIfFalse: body = "if (!AsBool(Pop(s))) goto I" + a + ";"; break;
            case Op::JumpIfFalseKeep: body = "if (!AsBool(s.back())) goto I" + a + "; s.pop_back();"; break;
            case Op::JumpIfTrueKeep: body = "if (AsBool(s.back())) goto I" + a + "; s.pop_back();"; break;
            case Op::Call: {
                const Chunk& callee = m_program.functions[static_cast<std::size_t>(in.a)];
                body = "Frame g; g.locals.assign(s.end() - " + b + ", s.end()); s.resize(s.size() - " + b +
                       "U); g.locals.resize(" + std::to_string(callee.localCount) + "U); CallChunk(c, Chunk" + a +
                       ", g); s.push_back(std::move(g.result));";
                break;
            }
            case Op::CallBuiltin:
                body = "const std::vector<Value> args(s.end() - " + b + ", s.end()); s.resize(s.size() - " + b +
                       "U); Value r = CallBuiltin(c.host, " + a + ", args); s.push_back(std::move(r));";
                break;
            case Op::CallHost:
                body = "const std::vector<Value> args(s.end() - " + b + ", s.end()); s.resize(s.size() - " + b +
                       "U); Value r = c.host.CallFunction(" + Name(in.a) + ", args); s.push_back(std::move(r));";
                break;
            case Op::CallMethod:
                body = "const std::vector<Value> args(s.end() - " + b + ", s.end()); s.resize(s.size() - " + b +
                       "U); c.host.CallMethod(AsObjectRef(Pop(s)), " + Name(in.a) +
                       ", args); s.push_back(Value{});";
                break;
            case Op::Return: body = "f.result = Pop(s); f.resume = 0U; return Status::Finished;"; break;
            case Op::ReturnNone: body = "f.result = Value{}; f.resume = 0U; return Status::Finished;"; break;
            case Op::Wait:
                body = "f.waitSeconds = std::max(0.0, AsDouble(Pop(s))); f.resume = " + std::to_string(n + 1U) +
                       "U; return Status::Waiting;";
                break;
        }
        m_out += "    { c.Step(" + std::to_string(in.line) + "U); " + body + " }\n";
    }

    const Program& m_program;
    std::string m_out;
    std::vector<std::optional<TypedPlan>> m_plans;
};

} // namespace

std::string GenerateUVScriptNativeCpp(const Program& program, const std::string_view origin) {
    return Generator(program).Run(origin);
}

} // namespace UVE::Blueprint
