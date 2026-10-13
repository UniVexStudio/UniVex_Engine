// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Bytecode to C++. Each chunk becomes one function; each instruction becomes a block of straight
// C++ that calls the same value operations as the interpreter (uvscript_native_uve.h), with jumps
// as `goto` and every `wait` as a return that the next call resumes from. The result keeps the
// interpreter's semantics exactly - including the instruction budget and line numbers in errors -
// while dropping its fetch-decode-dispatch loop.

#include "uve/uvscript/uvscript_codegen_uve.h"

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

#include "uvscript_program_uve.h"

namespace UVE::UVScript {
namespace {

[[nodiscard]] std::string HexUVE(const std::uint64_t value) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "0x%016llxULL", static_cast<unsigned long long>(value));
    return buffer;
}

/// A double written so the compiler reads back exactly the same bits.
[[nodiscard]] std::string DoubleLiteralUVE(const double value) {
    return "std::bit_cast<double>(std::uint64_t{" + HexUVE(std::bit_cast<std::uint64_t>(value)) + "})";
}

/// A C++ string literal. Anything unusual is written as a three-digit octal escape, which unlike
/// \x cannot swallow the character after it.
[[nodiscard]] std::string StringLiteralUVE(const std::string_view text) {
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

[[nodiscard]] std::string ValueLiteralUVE(const ValueUVE& value) {
    std::function<std::string(const ValueUVE&)> emit;
    emit = [&](const ValueUVE& current) -> std::string {
        return std::visit(
            [&](const auto& v) -> std::string {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return "ValueUVE{}";
                } else if constexpr (std::is_same_v<T, bool>) {
                    return v ? "ValueUVE{true}" : "ValueUVE{false}";
                } else if constexpr (std::is_same_v<T, std::int64_t>) {
                    return "ValueUVE{std::int64_t{" + std::to_string(v) + "}}";
                } else if constexpr (std::is_same_v<T, double>) {
                    return "ValueUVE{" + DoubleLiteralUVE(v) + "}";
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return "ValueUVE{std::string{" + StringLiteralUVE(v) + "}}";
                } else if constexpr (std::is_same_v<T, Vec3ValueUVE>) {
                    return "ValueUVE{Vec3ValueUVE{" + DoubleLiteralUVE(v.x) + ", " + DoubleLiteralUVE(v.y) + ", " +
                           DoubleLiteralUVE(v.z) + "}}";
                } else if constexpr (std::is_same_v<T, ObjectRefUVE>) {
                    return "ValueUVE{ObjectRefUVE{" + std::to_string(v.id) + "U}}";
                } else {
                    const char* maker = v->kind == CollectionValueUVE::KindUVE::List     ? "MakeListValueUVE"
                                        : v->kind == CollectionValueUVE::KindUVE::Map     ? "MakeMapValueUVE"
                                                                                         : "MakeTupleValueUVE";
                    std::string out = std::string{maker} + "(std::vector<ValueUVE>{";
                    for (const ValueUVE& item : v->items) {
                        out += emit(item) + ", ";
                    }
                    return out + "})";
                }
            },
            current);
    };
    return emit(value);
}

[[nodiscard]] const char* ArithmeticNameUVE(const OpUVE op) {
    switch (op) {
        case OpUVE::Sub: return "Sub";
        case OpUVE::Mul: return "Mul";
        case OpUVE::Div: return "Div";
        case OpUVE::Mod: return "Mod";
        default: return "Add";
    }
}

[[nodiscard]] const char* CompareNameUVE(const OpUVE op) {
    switch (op) {
        case OpUVE::Le: return "Le";
        case OpUVE::Gt: return "Gt";
        case OpUVE::Ge: return "Ge";
        default: return "Lt";
    }
}


// ---- Typed chunks. A chunk that never waits and whose every value has a type the checker knows
// is written with plain C++ variables (double, int64_t, bool, std::string, Vec3ValueUVE) instead
// of ValueUVE, one variable per local and per stack depth. It performs the same operations in the
// same order with the same checks and messages as ArithmeticUVE and friends, so it still agrees
// with the interpreter exactly - it just stops paying for the variant on every step.

enum class SlotUVE : std::uint8_t {
    Bool,
    Int,
    Float,
    Str,
    Vec3,
    None,
};

[[nodiscard]] std::optional<SlotUVE> SlotOfTypeUVE(const TypeUVE& type) {
    switch (type.kind) {
        case TypeUVE::KindUVE::Bool: return SlotUVE::Bool;
        case TypeUVE::KindUVE::Int: return SlotUVE::Int;
        case TypeUVE::KindUVE::Float: return SlotUVE::Float;
        case TypeUVE::KindUVE::Str: return SlotUVE::Str;
        case TypeUVE::KindUVE::Vec3: return SlotUVE::Vec3;
        case TypeUVE::KindUVE::None: return SlotUVE::None;
        default: return std::nullopt;
    }
}

[[nodiscard]] std::optional<SlotUVE> SlotOfValueUVE(const ValueUVE& value) {
    switch (value.index()) {
        case 0U: return SlotUVE::None;
        case 1U: return SlotUVE::Bool;
        case 2U: return SlotUVE::Int;
        case 3U: return SlotUVE::Float;
        case 4U: return SlotUVE::Str;
        case 5U: return SlotUVE::Vec3;
        default: return std::nullopt;
    }
}

[[nodiscard]] const char* CppTypeUVE(const SlotUVE slot) {
    switch (slot) {
        case SlotUVE::Bool: return "bool";
        case SlotUVE::Int: return "std::int64_t";
        case SlotUVE::Float: return "double";
        case SlotUVE::Str: return "std::string";
        case SlotUVE::Vec3: return "Vec3ValueUVE";
        case SlotUVE::None: return "void";
    }
    return "void";
}

[[nodiscard]] char SuffixUVE(const SlotUVE slot) {
    switch (slot) {
        case SlotUVE::Bool: return 'b';
        case SlotUVE::Int: return 'i';
        case SlotUVE::Float: return 'd';
        case SlotUVE::Str: return 's';
        case SlotUVE::Vec3: return 'v';
        case SlotUVE::None: return 'n';
    }
    return 'n';
}

[[nodiscard]] bool IsNumberSlotUVE(const SlotUVE slot) noexcept { return slot == SlotUVE::Int || slot == SlotUVE::Float; }

/// What a typed chunk looks like: the stack's types before every instruction, and the locals'.
struct TypedPlanUVE final {
    std::vector<SlotUVE> locals;
    SlotUVE result = SlotUVE::None;
    std::vector<std::optional<std::vector<SlotUVE>>> stackBefore;
};

/// The result type of a builtin given its argument types, as CallBuiltinUVE computes it.
[[nodiscard]] std::optional<SlotUVE> BuiltinResultUVE(const BuiltinUVE id, const std::span<const SlotUVE> args) {
    const bool allInt = std::ranges::all_of(args, [](const SlotUVE s) { return s == SlotUVE::Int; });
    switch (id) {
        case BuiltinUVE::Print: return SlotUVE::None;
        case BuiltinUVE::Str: return SlotUVE::Str;
        case BuiltinUVE::Sqrt:
        case BuiltinUVE::Sin:
        case BuiltinUVE::Cos:
        case BuiltinUVE::Floor:
        case BuiltinUVE::Lerp:
        case BuiltinUVE::Float: return SlotUVE::Float;
        // `length` is overloaded: a vec3's magnitude stays a float, a str's count is whole.
        // Collections never reach the typed planner, so anything else bails out below.
        case BuiltinUVE::Length:
            if (!args.empty() && args[0] == SlotUVE::Vec3) {
                return SlotUVE::Float;
            }
            return !args.empty() && args[0] == SlotUVE::Str ? std::optional<SlotUVE>{SlotUVE::Int} : std::nullopt;
        case BuiltinUVE::Int: return SlotUVE::Int;
        case BuiltinUVE::Abs:
        case BuiltinUVE::Min:
        case BuiltinUVE::Max:
        case BuiltinUVE::Clamp: return allInt ? SlotUVE::Int : SlotUVE::Float;
        case BuiltinUVE::Vec3:
        case BuiltinUVE::Normalize: return SlotUVE::Vec3;
        case BuiltinUVE::Push:
        case BuiltinUVE::Keys:
        case BuiltinUVE::Contains:
        case BuiltinUVE::Remove: return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<SlotUVE> ArithmeticResultUVE(const OpUVE op, const SlotUVE a, const SlotUVE b) {
    if (a == SlotUVE::Int && b == SlotUVE::Int) {
        return op == OpUVE::Div ? SlotUVE::Float : SlotUVE::Int;
    }
    if (IsNumberSlotUVE(a) && IsNumberSlotUVE(b)) {
        return SlotUVE::Float;
    }
    if (a == SlotUVE::Str && b == SlotUVE::Str && op == OpUVE::Add) {
        return SlotUVE::Str;
    }
    if (a == SlotUVE::Vec3 && b == SlotUVE::Vec3 && (op == OpUVE::Add || op == OpUVE::Sub)) {
        return SlotUVE::Vec3;
    }
    if (a == SlotUVE::Vec3 && IsNumberSlotUVE(b) && (op == OpUVE::Mul || op == OpUVE::Div)) {
        return SlotUVE::Vec3;
    }
    if (IsNumberSlotUVE(a) && b == SlotUVE::Vec3 && op == OpUVE::Mul) {
        return SlotUVE::Vec3;
    }
    return std::nullopt;
}

/// Works out every stack type, or nothing when the chunk cannot be typed (it waits, uses an object
/// value, or merges different types at a jump target).
[[nodiscard]] std::optional<TypedPlanUVE> PlanTypedChunkUVE(const ProgramUVE& program, const ChunkUVE& chunk) {
    TypedPlanUVE plan;
    for (std::size_t i = 0U; i < chunk.localCount; ++i) {
        const std::optional<SlotUVE> slot =
            i < chunk.localTypes.size() ? SlotOfTypeUVE(chunk.localTypes[i]) : std::nullopt;
        if (!slot.has_value() || *slot == SlotUVE::None) {
            return std::nullopt;
        }
        plan.locals.push_back(*slot);
    }
    const std::optional<SlotUVE> result = SlotOfTypeUVE(chunk.result);
    if (!result.has_value()) {
        return std::nullopt;
    }
    plan.result = *result;
    const auto hostType = [&program](const std::int32_t constant) -> std::optional<SlotUVE> {
        for (const auto& [index, type] : program.hostTypes) {
            if (index == static_cast<std::uint32_t>(constant)) {
                return SlotOfTypeUVE(type);
            }
        }
        return std::nullopt;
    };

    plan.stackBefore.assign(chunk.code.size() + 1U, std::nullopt);
    std::vector<std::size_t> work{0U};
    plan.stackBefore[0] = std::vector<SlotUVE>{};
    const auto flow = [&plan, &work](const std::size_t target, const std::vector<SlotUVE>& stack) {
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
        const InstructionUVE& in = chunk.code[n];
        std::vector<SlotUVE> s = *plan.stackBefore[n];
        const auto need = [&s](const std::size_t count) { return s.size() >= count; };
        bool fallsThrough = true;
        switch (in.op) {
            case OpUVE::PushConst: {
                const std::optional<SlotUVE> slot = SlotOfValueUVE(program.constants[static_cast<std::size_t>(in.a)]);
                if (!slot.has_value()) return std::nullopt;
                s.push_back(*slot);
                break;
            }
            case OpUVE::Pop:
                if (!need(1U)) return std::nullopt;
                s.pop_back();
                break;
            case OpUVE::LoadLocal: s.push_back(plan.locals[static_cast<std::size_t>(in.a)]); break;
            case OpUVE::StoreLocal:
                if (!need(1U) || s.back() != plan.locals[static_cast<std::size_t>(in.a)]) return std::nullopt;
                s.pop_back();
                break;
            case OpUVE::LoadField: {
                const std::optional<SlotUVE> slot = SlotOfTypeUVE(program.fields[static_cast<std::size_t>(in.a)].type);
                if (!slot.has_value() || *slot == SlotUVE::None) return std::nullopt;
                s.push_back(*slot);
                break;
            }
            case OpUVE::StoreField: {
                const std::optional<SlotUVE> slot = SlotOfTypeUVE(program.fields[static_cast<std::size_t>(in.a)].type);
                if (!need(1U) || slot != s.back()) return std::nullopt;
                s.pop_back();
                break;
            }
            case OpUVE::LoadProp: {
                const std::optional<SlotUVE> slot = hostType(in.a);
                if (!slot.has_value() || *slot == SlotUVE::None) return std::nullopt;
                s.push_back(*slot);
                break;
            }
            case OpUVE::StoreProp:
                if (!need(1U) || hostType(in.a) != s.back()) return std::nullopt;
                s.pop_back();
                break;
            case OpUVE::Add:
            case OpUVE::Sub:
            case OpUVE::Mul:
            case OpUVE::Div:
            case OpUVE::Mod: {
                if (!need(2U)) return std::nullopt;
                const std::optional<SlotUVE> r = ArithmeticResultUVE(in.op, s[s.size() - 2U], s.back());
                if (!r.has_value()) return std::nullopt;
                s.pop_back();
                s.back() = *r;
                break;
            }
            case OpUVE::Neg:
                if (!need(1U) || (!IsNumberSlotUVE(s.back()) && s.back() != SlotUVE::Vec3)) return std::nullopt;
                break;
            case OpUVE::Not:
                if (!need(1U) || s.back() != SlotUVE::Bool) return std::nullopt;
                break;
            case OpUVE::Eq:
            case OpUVE::Ne: {
                if (!need(2U)) return std::nullopt;
                const SlotUVE a = s[s.size() - 2U];
                const SlotUVE b = s.back();
                if (!(IsNumberSlotUVE(a) && IsNumberSlotUVE(b)) && a != b) return std::nullopt;
                if (a == SlotUVE::None) return std::nullopt;
                s.pop_back();
                s.back() = SlotUVE::Bool;
                break;
            }
            case OpUVE::Lt:
            case OpUVE::Le:
            case OpUVE::Gt:
            case OpUVE::Ge:
                if (!need(2U) || !IsNumberSlotUVE(s[s.size() - 2U]) || !IsNumberSlotUVE(s.back())) return std::nullopt;
                s.pop_back();
                s.back() = SlotUVE::Bool;
                break;
            case OpUVE::ToFloat:
                if (!need(1U) || !IsNumberSlotUVE(s.back())) return std::nullopt;
                s.back() = SlotUVE::Float;
                break;
            case OpUVE::GetComponent:
                if (!need(1U) || s.back() != SlotUVE::Vec3) return std::nullopt;
                s.back() = SlotUVE::Float;
                break;
            case OpUVE::SetComponent:
                if (!need(2U) || s[s.size() - 2U] != SlotUVE::Vec3 || !IsNumberSlotUVE(s.back())) return std::nullopt;
                s.pop_back();
                break;
            case OpUVE::Concat:
                if (!need(static_cast<std::size_t>(in.a))) return std::nullopt;
                s.resize(s.size() - static_cast<std::size_t>(in.a));
                s.push_back(SlotUVE::Str);
                break;
            case OpUVE::Jump:
                if (!flow(static_cast<std::size_t>(in.a), s)) return std::nullopt;
                fallsThrough = false;
                break;
            case OpUVE::JumpIfFalse:
                if (!need(1U) || s.back() != SlotUVE::Bool) return std::nullopt;
                s.pop_back();
                if (!flow(static_cast<std::size_t>(in.a), s)) return std::nullopt;
                break;
            case OpUVE::JumpIfFalseKeep:
            case OpUVE::JumpIfTrueKeep:
                if (!need(1U) || s.back() != SlotUVE::Bool) return std::nullopt;
                if (!flow(static_cast<std::size_t>(in.a), s)) return std::nullopt;
                s.pop_back();
                break;
            case OpUVE::Call: {
                const ChunkUVE& callee = program.functions[static_cast<std::size_t>(in.a)];
                const std::optional<SlotUVE> r = SlotOfTypeUVE(callee.result);
                if (!need(static_cast<std::size_t>(in.b)) || !r.has_value()) return std::nullopt;
                for (std::size_t i = 0U; i < static_cast<std::size_t>(in.b); ++i) {
                    const std::optional<SlotUVE> param =
                        i < callee.localTypes.size() ? SlotOfTypeUVE(callee.localTypes[i]) : std::nullopt;
                    if (param != s[s.size() - static_cast<std::size_t>(in.b) + i]) return std::nullopt;
                }
                s.resize(s.size() - static_cast<std::size_t>(in.b));
                s.push_back(*r);
                break;
            }
            case OpUVE::CallBuiltin: {
                if (!need(static_cast<std::size_t>(in.b))) return std::nullopt;
                const std::vector<SlotUVE> args(s.end() - in.b, s.end());
                if (std::ranges::any_of(args, [](const SlotUVE a) { return a == SlotUVE::None; })) return std::nullopt;
                const std::optional<SlotUVE> r = BuiltinResultUVE(static_cast<BuiltinUVE>(in.a), args);
                if (!r.has_value()) return std::nullopt;
                s.resize(s.size() - static_cast<std::size_t>(in.b));
                s.push_back(*r);
                break;
            }
            case OpUVE::CallHost: {
                const std::optional<SlotUVE> r = hostType(in.a);
                if (!need(static_cast<std::size_t>(in.b)) || !r.has_value()) return std::nullopt;
                s.resize(s.size() - static_cast<std::size_t>(in.b));
                s.push_back(*r);
                break;
            }
            case OpUVE::Return:
                if (!need(1U) || s.back() != plan.result) return std::nullopt;
                fallsThrough = false;
                break;
            case OpUVE::ReturnNone: fallsThrough = false; break;
            case OpUVE::CallMethod: return std::nullopt; // dynamic dispatch stays boxed, like Wait
            case OpUVE::Wait: return std::nullopt;
            // Collections stay boxed: the typed planner only knows scalars and vec3.
            case OpUVE::BuildList:
            case OpUVE::BuildMap:
            case OpUVE::BuildTuple:
            case OpUVE::GetIndex:
            case OpUVE::SetIndex:
            case OpUVE::Unpack: return std::nullopt;
        }
        if (fallsThrough && !flow(n + 1U, s)) {
            return std::nullopt;
        }
    }
    return plan;
}

/// Emits one typed chunk as `T<i>(ContextUVE&, params...)`.
class TypedEmitterUVE final {
public:
    TypedEmitterUVE(const ProgramUVE& program, const std::vector<std::optional<TypedPlanUVE>>& plans, std::string& out)
        : m_program(program), m_plans(plans), m_out(out) {}

    [[nodiscard]] static std::string Signature(const ChunkUVE& chunk, const TypedPlanUVE& plan, const std::size_t index) {
        std::string text = std::string{CppTypeUVE(plan.result)} + " T" + std::to_string(index) + "(ContextUVE& c";
        for (std::size_t i = 0U; i < chunk.paramCount; ++i) {
            text += ", " + std::string{CppTypeUVE(plan.locals[i])} + " a" + std::to_string(i);
        }
        return text + ")";
    }

    void Emit(const ChunkUVE& chunk, const TypedPlanUVE& plan, const std::size_t index) {
        m_chunk = &chunk;
        m_plan = &plan;
        m_out += "// " + (chunk.name.empty() ? std::string{"field initializers"} : chunk.name) + " (typed)\n";
        m_out += Signature(chunk, plan, index) + " {\n";
        for (std::size_t i = 0U; i < plan.locals.size(); ++i) {
            m_out += "    [[maybe_unused]] " + std::string{CppTypeUVE(plan.locals[i])} + " L" + std::to_string(i) +
                     (i < chunk.paramCount ? " = std::move(a" + std::to_string(i) + ");\n" : "{};\n");
        }
        // One variable per stack depth and type, declared up front so every goto is legal.
        std::set<std::pair<std::size_t, SlotUVE>> slots;
        for (const auto& stack : plan.stackBefore) {
            if (stack.has_value()) {
                for (std::size_t d = 0U; d < stack->size(); ++d) {
                    if ((*stack)[d] != SlotUVE::None) {
                        slots.emplace(d, (*stack)[d]);
                    }
                }
            }
        }
        for (std::size_t n = 0U; n < chunk.code.size(); ++n) {
            // Results written by an instruction whose next stack is not recorded (a return) need no slot.
            if (n + 1U < plan.stackBefore.size() && plan.stackBefore[n + 1U].has_value()) {
                for (std::size_t d = 0U; d < plan.stackBefore[n + 1U]->size(); ++d) {
                    if ((*plan.stackBefore[n + 1U])[d] != SlotUVE::None) {
                        slots.emplace(d, (*plan.stackBefore[n + 1U])[d]);
                    }
                }
            }
        }
        for (const auto& [depth, slot] : slots) {
            m_out += "    [[maybe_unused]] " + std::string{CppTypeUVE(slot)} + " " + Var(depth, slot) + "{};\n";
        }
        std::set<std::size_t> labels;
        for (const InstructionUVE& in : chunk.code) {
            if (in.op == OpUVE::Jump || in.op == OpUVE::JumpIfFalse || in.op == OpUVE::JumpIfFalseKeep ||
                in.op == OpUVE::JumpIfTrueKeep) {
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
            Instruction(chunk.code[n], *plan.stackBefore[n]);
        }
        if (labels.contains(chunk.code.size())) {
            m_out += "I" + std::to_string(chunk.code.size()) + ":\n";
        }
        m_out += plan.result == SlotUVE::None ? "    return;\n}\n\n" : "    return {};\n}\n\n";
    }

private:
    [[nodiscard]] static std::string Var(const std::size_t depth, const SlotUVE slot) {
        return std::string{"s"} + std::to_string(depth) + SuffixUVE(slot);
    }

    [[nodiscard]] static std::string AsDouble(const std::string& name, const SlotUVE slot) {
        return slot == SlotUVE::Int ? "static_cast<double>(" + name + ")" : name;
    }

    [[nodiscard]] static std::string Literal(const ValueUVE& value) {
        return std::visit(
            [](const auto& v) -> std::string {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, bool>) {
                    return v ? "true" : "false";
                } else if constexpr (std::is_same_v<T, std::int64_t>) {
                    return "std::int64_t{" + std::to_string(v) + "}";
                } else if constexpr (std::is_same_v<T, double>) {
                    return DoubleLiteralUVE(v);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return "std::string{" + StringLiteralUVE(v) + "}";
                } else if constexpr (std::is_same_v<T, Vec3ValueUVE>) {
                    return "Vec3ValueUVE{" + DoubleLiteralUVE(v.x) + ", " + DoubleLiteralUVE(v.y) + ", " +
                           DoubleLiteralUVE(v.z) + "}";
                } else {
                    return "{}";
                }
            },
            value);
    }

    [[nodiscard]] static std::string Box(const std::string& name, const SlotUVE slot) {
        return slot == SlotUVE::None ? std::string{"ValueUVE{}"} : "ValueUVE{" + name + "}";
    }

    [[nodiscard]] static std::string Unbox(const std::string& value, const SlotUVE slot) {
        return "std::get<" + std::string{CppTypeUVE(slot)} + ">(" + value + ")";
    }

    [[nodiscard]] std::string Name(const std::int32_t constant) const {
        return StringLiteralUVE(std::get<std::string>(m_program.constants[static_cast<std::size_t>(constant)]));
    }

    void Instruction(const InstructionUVE& in, const std::vector<SlotUVE>& s) {
        const std::size_t d = s.size();
        const auto top = [&s, d](const std::size_t fromTop) { return Var(d - fromTop, s[d - fromTop]); };
        const auto topType = [&s, d](const std::size_t fromTop) { return s[d - fromTop]; };
        const std::string a = std::to_string(in.a);
        std::string body;
        switch (in.op) {
            case OpUVE::PushConst: {
                const ValueUVE& value = m_program.constants[static_cast<std::size_t>(in.a)];
                const SlotUVE slot = *SlotOfValueUVE(value);
                if (slot != SlotUVE::None) {
                    body = Var(d, slot) + " = " + Literal(value) + ";";
                }
                break;
            }
            case OpUVE::Pop: break;
            case OpUVE::LoadLocal:
                body = Var(d, m_plan->locals[static_cast<std::size_t>(in.a)]) + " = L" + a + ";";
                break;
            case OpUVE::StoreLocal: body = "L" + a + " = std::move(" + top(1U) + ");"; break;
            case OpUVE::LoadField: {
                const SlotUVE slot = *SlotOfTypeUVE(m_program.fields[static_cast<std::size_t>(in.a)].type);
                body = Var(d, slot) + " = " + Unbox("c.fields[" + a + "]", slot) + ";";
                break;
            }
            case OpUVE::StoreField: body = "c.fields[" + a + "] = " + Box(top(1U), topType(1U)) + ";"; break;
            case OpUVE::LoadProp: {
                const SlotUVE slot = HostSlot(in.a);
                body = Var(d, slot) + " = " + Unbox("c.host.GetPropertyUVE(" + Name(in.a) + ")", slot) + ";";
                break;
            }
            case OpUVE::StoreProp:
                body = "c.host.SetPropertyUVE(" + Name(in.a) + ", " + Box(top(1U), topType(1U)) + ");";
                break;
            case OpUVE::Add:
            case OpUVE::Sub:
            case OpUVE::Mul:
            case OpUVE::Div:
            case OpUVE::Mod: body = Arithmetic(in.op, top(2U), topType(2U), top(1U), topType(1U), d - 2U); break;
            case OpUVE::Neg: {
                const std::string x = top(1U);
                body = topType(1U) == SlotUVE::Vec3 ? x + " = Vec3ValueUVE{-" + x + ".x, -" + x + ".y, -" + x + ".z};"
                                                    : x + " = -" + x + ";";
                break;
            }
            case OpUVE::Not: body = top(1U) + " = !" + top(1U) + ";"; break;
            case OpUVE::Eq:
            case OpUVE::Ne: {
                const bool numbers = IsNumberSlotUVE(topType(2U)) && IsNumberSlotUVE(topType(1U));
                const std::string x = numbers ? AsDouble(top(2U), topType(2U)) : top(2U);
                const std::string y = numbers ? AsDouble(top(1U), topType(1U)) : top(1U);
                body = Var(d - 2U, SlotUVE::Bool) + " = " + x + (in.op == OpUVE::Eq ? " == " : " != ") + y + ";";
                break;
            }
            case OpUVE::Lt:
            case OpUVE::Le:
            case OpUVE::Gt:
            case OpUVE::Ge: {
                const char* op = in.op == OpUVE::Lt ? " < " : in.op == OpUVE::Le ? " <= " : in.op == OpUVE::Gt ? " > " : " >= ";
                body = Var(d - 2U, SlotUVE::Bool) + " = " + AsDouble(top(2U), topType(2U)) + op +
                       AsDouble(top(1U), topType(1U)) + ";";
                break;
            }
            case OpUVE::ToFloat: body = Var(d - 1U, SlotUVE::Float) + " = " + AsDouble(top(1U), topType(1U)) + ";"; break;
            case OpUVE::GetComponent:
                body = Var(d - 1U, SlotUVE::Float) + " = " + top(1U) + (in.a == 0 ? ".x;" : in.a == 1 ? ".y;" : ".z;");
                break;
            case OpUVE::SetComponent:
                body = top(2U) + (in.a == 0 ? ".x" : in.a == 1 ? ".y" : ".z") + " = " + AsDouble(top(1U), topType(1U)) + ";";
                break;
            case OpUVE::Concat: {
                std::string joined;
                for (std::size_t i = static_cast<std::size_t>(in.a); i > 0U; --i) {
                    joined += (joined.empty() ? "" : " + ") + std::string{"FormatValueUVE("} + Box(top(i), topType(i)) + ")";
                }
                if (joined.empty()) {
                    joined = "std::string{}";
                }
                body = "{ std::string t = " + joined + "; " + Var(d - static_cast<std::size_t>(in.a), SlotUVE::Str) +
                       " = std::move(t); }";
                break;
            }
            case OpUVE::Jump: body = "goto I" + a + ";"; break;
            case OpUVE::JumpIfFalse: body = "if (!" + top(1U) + ") goto I" + a + ";"; break;
            case OpUVE::JumpIfFalseKeep: body = "if (!" + top(1U) + ") goto I" + a + ";"; break;
            case OpUVE::JumpIfTrueKeep: body = "if (" + top(1U) + ") goto I" + a + ";"; break;
            case OpUVE::Call: body = Call(in, s); break;
            case OpUVE::CallBuiltin: body = Builtin(in, s); break;
            case OpUVE::CallHost: {
                const std::size_t argc = static_cast<std::size_t>(in.b);
                std::string args;
                for (std::size_t i = argc; i > 0U; --i) {
                    args += (args.empty() ? "" : ", ") + Box(top(i), topType(i));
                }
                const SlotUVE result = HostSlot(in.a);
                const std::string call = "c.host.CallFunctionUVE(" + Name(in.a) + ", std::vector<ValueUVE>{" + args + "})";
                body = result == SlotUVE::None ? "static_cast<void>(" + call + ");"
                                               : Var(d - argc, result) + " = " + Unbox(call, result) + ";";
                break;
            }
            case OpUVE::Return: body = "return " + top(1U) + ";"; break;
            case OpUVE::ReturnNone: body = m_plan->result == SlotUVE::None ? "return;" : "return {};"; break;
            case OpUVE::CallMethod: break; // never typed
            case OpUVE::Wait: break;       // never typed
            case OpUVE::BuildList: break;  // never typed
            case OpUVE::BuildMap: break;   // never typed
            case OpUVE::BuildTuple: break; // never typed
            case OpUVE::GetIndex: break;   // never typed
            case OpUVE::SetIndex: break;   // never typed
            case OpUVE::Unpack: break;     // never typed
        }
        m_out += "    { c.StepUVE(" + std::to_string(in.line) + "U); " + body + " }\n";
    }

    [[nodiscard]] SlotUVE HostSlot(const std::int32_t constant) const {
        for (const auto& [index, type] : m_program.hostTypes) {
            if (index == static_cast<std::uint32_t>(constant)) {
                return *SlotOfTypeUVE(type);
            }
        }
        return SlotUVE::None;
    }

    /// ArithmeticUVE, specialised for the operand types this site always has.
    [[nodiscard]] static std::string Arithmetic(const OpUVE op, const std::string& x, const SlotUVE tx,
                                                const std::string& y, const SlotUVE ty, const std::size_t depth) {
        const SlotUVE rt = *ArithmeticResultUVE(op, tx, ty);
        const std::string r = Var(depth, rt);
        if (tx == SlotUVE::Int && ty == SlotUVE::Int) {
            switch (op) {
                case OpUVE::Add: return r + " = " + x + " + " + y + ";";
                case OpUVE::Sub: return r + " = " + x + " - " + y + ";";
                case OpUVE::Mul: return r + " = " + x + " * " + y + ";";
                case OpUVE::Mod:
                    return "if (" + y + " == 0) throw ErrorUVE{\"'%' by zero\"}; " + r + " = " + x + " % " + y + ";";
                default:
                    return "if (" + y + " == 0) throw ErrorUVE{\"division by zero\"}; " + r + " = static_cast<double>(" + x +
                           ") / static_cast<double>(" + y + ");";
            }
        }
        if (IsNumberSlotUVE(tx) && IsNumberSlotUVE(ty)) {
            const std::string dx = AsDouble(x, tx);
            const std::string dy = AsDouble(y, ty);
            switch (op) {
                case OpUVE::Add: return r + " = " + dx + " + " + dy + ";";
                case OpUVE::Sub: return r + " = " + dx + " - " + dy + ";";
                case OpUVE::Mul: return r + " = " + dx + " * " + dy + ";";
                case OpUVE::Div:
                    return "if (" + dy + " == 0.0) throw ErrorUVE{\"division by zero\"}; " + r + " = " + dx + " / " + dy + ";";
                default:
                    return "if (" + dy + " == 0.0) throw ErrorUVE{\"'%' by zero\"}; " + r + " = std::fmod(" + dx + ", " + dy +
                           ");";
            }
        }
        if (tx == SlotUVE::Str) {
            return r + " = " + x + " + " + y + ";";
        }
        if (tx == SlotUVE::Vec3 && ty == SlotUVE::Vec3) {
            const char* o = op == OpUVE::Add ? " + " : " - ";
            return r + " = Vec3ValueUVE{" + x + ".x" + o + y + ".x, " + x + ".y" + o + y + ".y, " + x + ".z" + o + y + ".z};";
        }
        if (tx == SlotUVE::Vec3) {
            const std::string sc = AsDouble(y, ty);
            if (op == OpUVE::Mul) {
                return r + " = Vec3ValueUVE{" + x + ".x * " + sc + ", " + x + ".y * " + sc + ", " + x + ".z * " + sc + "};";
            }
            return "if (" + sc + " == 0.0) throw ErrorUVE{\"division by zero\"}; " + r + " = Vec3ValueUVE{" + x + ".x / " + sc +
                   ", " + x + ".y / " + sc + ", " + x + ".z / " + sc + "};";
        }
        const std::string sc = AsDouble(x, tx);
        return r + " = Vec3ValueUVE{" + y + ".x * " + sc + ", " + y + ".y * " + sc + ", " + y + ".z * " + sc + "};";
    }

    [[nodiscard]] std::string Call(const InstructionUVE& in, const std::vector<SlotUVE>& s) const {
        const std::size_t d = s.size();
        const std::size_t argc = static_cast<std::size_t>(in.b);
        const std::size_t callee = static_cast<std::size_t>(in.a);
        const SlotUVE result = *SlotOfTypeUVE(m_program.functions[callee].result);
        const std::string guard = "if (c.depth + 1U >= 256U) throw ErrorUVE{\"functions call each other too deeply\"}; ++c.depth; ";
        if (m_plans[callee].has_value()) {
            std::string args;
            for (std::size_t i = argc; i > 0U; --i) {
                args += ", " + Var(d - i, s[d - i]);
            }
            const std::string call = "T" + std::to_string(callee) + "(c" + args + ")";
            return guard + (result == SlotUVE::None ? call + ";" : Var(d - argc, result) + " = " + call + ";") + " --c.depth;";
        }
        // A callee that could not be typed runs through the boxed entry point.
        const ChunkUVE& target = m_program.functions[callee];
        std::string text = "FrameUVE g; g.locals.resize(" + std::to_string(target.localCount) + "U); ";
        for (std::size_t i = argc; i > 0U; --i) {
            text += "g.locals[" + std::to_string(argc - i) + "] = " + Box(Var(d - i, s[d - i]), s[d - i]) + "; ";
        }
        text += "CallChunkUVE(c, Chunk" + std::to_string(callee) + ", g);";
        if (result != SlotUVE::None) {
            text += " " + Var(d - argc, result) + " = " + Unbox("g.result", result) + ";";
        }
        return text;
    }

    /// CallBuiltinUVE, specialised by argument types.
    [[nodiscard]] static std::string Builtin(const InstructionUVE& in, const std::vector<SlotUVE>& s) {
        const std::size_t d = s.size();
        const std::size_t argc = static_cast<std::size_t>(in.b);
        const std::vector<SlotUVE> types(s.end() - static_cast<std::ptrdiff_t>(argc), s.end());
        const auto arg = [&](const std::size_t i) { return Var(d - argc + i, s[d - argc + i]); };
        const auto num = [&](const std::size_t i) { return AsDouble(arg(i), s[d - argc + i]); };
        const SlotUVE rt = *BuiltinResultUVE(static_cast<BuiltinUVE>(in.a), types);
        const std::string r = rt == SlotUVE::None ? std::string{} : Var(d - argc, rt);
        const bool allInt = rt == SlotUVE::Int;
        switch (static_cast<BuiltinUVE>(in.a)) {
            case BuiltinUVE::Print: return "c.host.PrintUVE(FormatValueUVE(" + Box(arg(0), types[0]) + "));";
            case BuiltinUVE::Str: return "{ std::string t = FormatValueUVE(" + Box(arg(0), types[0]) + "); " + r + " = std::move(t); }";
            case BuiltinUVE::Sqrt:
                return "{ const double x = " + num(0) + "; if (x < 0.0) throw ErrorUVE{\"sqrt of a negative number\"}; " + r +
                       " = std::sqrt(x); }";
            case BuiltinUVE::Sin: return r + " = std::sin(" + num(0) + ");";
            case BuiltinUVE::Cos: return r + " = std::cos(" + num(0) + ");";
            case BuiltinUVE::Floor: return r + " = std::floor(" + num(0) + ");";
            case BuiltinUVE::Lerp: return r + " = " + num(0) + " + (" + num(1) + " - " + num(0) + ") * " + num(2) + ";";
            case BuiltinUVE::Int: return r + " = static_cast<std::int64_t>(" + num(0) + ");";
            case BuiltinUVE::Float: return r + " = " + num(0) + ";";
            case BuiltinUVE::Abs: return allInt ? r + " = std::abs(" + arg(0) + ");" : r + " = std::fabs(" + num(0) + ");";
            case BuiltinUVE::Min:
                return allInt ? r + " = std::min(" + arg(0) + ", " + arg(1) + ");"
                              : r + " = std::min(" + num(0) + ", " + num(1) + ");";
            case BuiltinUVE::Max:
                return allInt ? r + " = std::max(" + arg(0) + ", " + arg(1) + ");"
                              : r + " = std::max(" + num(0) + ", " + num(1) + ");";
            case BuiltinUVE::Clamp:
                return allInt ? r + " = std::clamp(" + arg(0) + ", " + arg(1) + ", std::max(" + arg(1) + ", " + arg(2) + "));"
                              : r + " = std::clamp(" + num(0) + ", " + num(1) + ", std::max(" + num(1) + ", " + num(2) + "));";
            case BuiltinUVE::Vec3: return r + " = Vec3ValueUVE{" + num(0) + ", " + num(1) + ", " + num(2) + "};";
            case BuiltinUVE::Length:
                if (types[0] == SlotUVE::Str) {
                    return r + " = static_cast<std::int64_t>(" + arg(0) + ".size());";
                }
                return r + " = std::sqrt(" + arg(0) + ".x * " + arg(0) + ".x + " + arg(0) + ".y * " + arg(0) + ".y + " + arg(0) +
                       ".z * " + arg(0) + ".z);";
            case BuiltinUVE::Normalize:
                return "{ const Vec3ValueUVE v = " + arg(0) +
                       "; const double n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); " + r +
                       " = n == 0.0 ? v : Vec3ValueUVE{v.x / n, v.y / n, v.z / n}; }";
            // Collection builtins never type: the planner bails before typed emission.
            case BuiltinUVE::Push:
            case BuiltinUVE::Keys:
            case BuiltinUVE::Contains:
            case BuiltinUVE::Remove: return {};
        }
        return {};
    }

    const ProgramUVE& m_program;
    const std::vector<std::optional<TypedPlanUVE>>& m_plans;
    std::string& m_out;
    const ChunkUVE* m_chunk = nullptr;
    const TypedPlanUVE* m_plan = nullptr;
};

class GeneratorUVE final {
public:
    explicit GeneratorUVE(const ProgramUVE& program) : m_program(program) {}

    [[nodiscard]] std::string Run(const std::string_view origin) {
        const std::size_t initIndex = m_program.functions.size();
        m_out += "// Generated from " + std::string{origin} + " by uvsc. Do not edit: regenerate it.\n";
        m_out += "// Program fingerprint " + HexUVE(m_program.fingerprint) + ".\n\n";
        m_out += "#include <algorithm>\n#include <bit>\n#include <cmath>\n#include <cstdint>\n#include <cstdlib>\n#include <string>\n"
                 "#include <utility>\n#include <variant>\n#include <vector>\n\n";
        m_out += "#include \"uve/uvscript/uvscript_native_uve.h\"\n\n";
        m_out += "namespace {\n\nusing namespace UVE::UVScript;\nusing namespace UVE::UVScript::Native;\n\n";
        if (!m_program.constants.empty()) {
            m_out += "const ValueUVE kConstants[] = {\n";
            for (const ValueUVE& constant : m_program.constants) {
                m_out += "    " + ValueLiteralUVE(constant) + ",\n";
            }
            m_out += "};\n\n";
        }
        // Which chunks can be typed; the rest stay boxed.
        m_plans.clear();
        for (std::size_t i = 0U; i <= initIndex; ++i) {
            m_plans.push_back(PlanTypedChunkUVE(m_program, ChunkAt(i)));
        }
        for (std::size_t i = 0U; i <= initIndex; ++i) {
            m_out += "StatusUVE Chunk" + std::to_string(i) + "(ContextUVE& c, FrameUVE& f);\n";
            if (m_plans[i].has_value()) {
                m_out += TypedEmitterUVE::Signature(ChunkAt(i), *m_plans[i], i) + ";\n";
            }
        }
        m_out += "\n";
        for (std::size_t i = 0U; i <= initIndex; ++i) {
            if (m_plans[i].has_value()) {
                TypedEmitterUVE(m_program, m_plans, m_out).Emit(ChunkAt(i), *m_plans[i], i);
                TypedWrapper(ChunkAt(i), *m_plans[i], i);
            } else {
                Chunk(ChunkAt(i), i);
            }
        }
        m_out += "const ChunkFunctionUVE kChunks[] = {";
        for (std::size_t i = 0U; i <= initIndex; ++i) {
            m_out += (i == 0U ? "" : ", ") + std::string{"Chunk"} + std::to_string(i);
        }
        m_out += "};\n\n";
        m_out += "[[maybe_unused]] const RegistrationUVE kRegistration{ProgramTableUVE{" + HexUVE(m_program.fingerprint) +
                 ", kChunks, " + std::to_string(initIndex) + "U}};\n\n";
        m_out += "} // namespace\n";
        return std::move(m_out);
    }

    /// How many chunks came out typed (for tests and diagnostics).
    [[nodiscard]] std::size_t TypedCountUVE() const {
        return static_cast<std::size_t>(std::ranges::count_if(m_plans, [](const auto& plan) { return plan.has_value(); }));
    }

private:
    [[nodiscard]] const ChunkUVE& ChunkAt(const std::size_t index) const {
        return index == m_program.functions.size() ? m_program.init : m_program.functions[index];
    }

    [[nodiscard]] std::string Name(const std::int32_t constant) const {
        return StringLiteralUVE(std::get<std::string>(m_program.constants[static_cast<std::size_t>(constant)]));
    }

    /// The uniform entry point for a typed chunk: unbox the arguments, call it, box the result.
    void TypedWrapper(const ChunkUVE& chunk, const TypedPlanUVE& plan, const std::size_t index) {
        m_out += "StatusUVE Chunk" + std::to_string(index) + "(ContextUVE& c, FrameUVE& f) {\n    ";
        std::string call = "T" + std::to_string(index) + "(c";
        for (std::size_t i = 0U; i < chunk.paramCount; ++i) {
            call += ", std::get<" + std::string{CppTypeUVE(plan.locals[i])} + ">(f.locals[" + std::to_string(i) + "])";
        }
        call += ")";
        m_out += plan.result == SlotUVE::None ? call + ";\n    f.result = ValueUVE{};\n"
                                              : "f.result = ValueUVE{" + call + "};\n";
        m_out += "    f.resume = 0U;\n    return StatusUVE::Finished;\n}\n\n";
    }

    void Chunk(const ChunkUVE& chunk, const std::size_t index) {
        // Labels only where something jumps or resumes: an unused label is a warning.
        std::set<std::size_t> labels;
        std::set<std::size_t> resumes;
        for (std::size_t n = 0U; n < chunk.code.size(); ++n) {
            const InstructionUVE& in = chunk.code[n];
            if (in.op == OpUVE::Jump || in.op == OpUVE::JumpIfFalse || in.op == OpUVE::JumpIfFalseKeep ||
                in.op == OpUVE::JumpIfTrueKeep) {
                labels.insert(static_cast<std::size_t>(in.a));
            } else if (in.op == OpUVE::Wait) {
                labels.insert(n + 1U);
                resumes.insert(n + 1U);
            }
        }
        m_out += "// " + (chunk.name.empty() ? std::string{"field initializers"} : chunk.name) + "\n";
        m_out += "StatusUVE Chunk" + std::to_string(index) + "(ContextUVE& c, FrameUVE& f) {\n";
        m_out += "    [[maybe_unused]] std::vector<ValueUVE>& s = f.stack;\n";
        m_out += "    [[maybe_unused]] std::vector<ValueUVE>& l = f.locals;\n";
        m_out += "    switch (f.resume) {\n        case 0U: break;\n";
        for (const std::size_t resume : resumes) {
            m_out += "        case " + std::to_string(resume) + "U: goto I" + std::to_string(resume) + ";\n";
        }
        m_out += "        default: throw ErrorUVE{\"a paused handler cannot continue\"};\n    }\n";
        for (std::size_t n = 0U; n < chunk.code.size(); ++n) {
            if (labels.contains(n)) {
                m_out += "I" + std::to_string(n) + ":\n";
            }
            Instruction(chunk.code[n], n);
        }
        if (labels.contains(chunk.code.size())) {
            m_out += "I" + std::to_string(chunk.code.size()) + ":\n";
        }
        m_out += "    f.result = ValueUVE{};\n    return StatusUVE::Finished;\n}\n\n";
    }

    void Instruction(const InstructionUVE& in, const std::size_t n) {
        const std::string a = std::to_string(in.a);
        const std::string b = std::to_string(in.b);
        std::string body;
        switch (in.op) {
            case OpUVE::PushConst: body = "s.push_back(kConstants[" + a + "]);"; break;
            case OpUVE::Pop: body = "s.pop_back();"; break;
            case OpUVE::LoadLocal: body = "s.push_back(l[" + a + "]);"; break;
            case OpUVE::StoreLocal: body = "l[" + a + "] = PopUVE(s);"; break;
            case OpUVE::LoadField: body = "s.push_back(c.fields[" + a + "]);"; break;
            case OpUVE::StoreField: body = "c.fields[" + a + "] = PopUVE(s);"; break;
            case OpUVE::LoadProp: body = "s.push_back(c.host.GetPropertyUVE(" + Name(in.a) + "));"; break;
            case OpUVE::StoreProp:
                body = "const ValueUVE v = PopUVE(s); c.host.SetPropertyUVE(" + Name(in.a) + ", v);";
                break;
            case OpUVE::Add:
            case OpUVE::Sub:
            case OpUVE::Mul:
            case OpUVE::Div:
            case OpUVE::Mod:
                body = "const ValueUVE y = PopUVE(s); const ValueUVE x = PopUVE(s); "
                       "s.push_back(ArithmeticUVE(ArithmeticOpUVE::" +
                       std::string{ArithmeticNameUVE(in.op)} + ", x, y));";
                break;
            case OpUVE::Neg: body = "ValueUVE v = NegateUVE(PopUVE(s)); s.push_back(std::move(v));"; break;
            case OpUVE::Not: body = "const bool v = !AsBoolUVE(PopUVE(s)); s.push_back(v);"; break;
            case OpUVE::Eq:
            case OpUVE::Ne:
                body = "const ValueUVE y = PopUVE(s); const ValueUVE x = PopUVE(s); s.push_back(EqualUVE(x, y) == " +
                       std::string{in.op == OpUVE::Eq ? "true" : "false"} + ");";
                break;
            case OpUVE::Lt:
            case OpUVE::Le:
            case OpUVE::Gt:
            case OpUVE::Ge:
                body = "const ValueUVE y = PopUVE(s); const ValueUVE x = PopUVE(s); "
                       "s.push_back(CompareUVE(CompareOpUVE::" +
                       std::string{CompareNameUVE(in.op)} + ", x, y));";
                break;
            case OpUVE::ToFloat: body = "const double v = AsDoubleUVE(PopUVE(s)); s.push_back(v);"; break;
            case OpUVE::GetComponent: body = "ValueUVE v = GetComponentUVE(PopUVE(s), " + a + "); s.push_back(std::move(v));"; break;
            case OpUVE::SetComponent:
                body = "const ValueUVE v = PopUVE(s); const ValueUVE vec = PopUVE(s); "
                       "s.push_back(SetComponentUVE(vec, " + a + ", v));";
                break;
            case OpUVE::BuildList:
                body = "ValueUVE v = BuildListUVE(s, " + a + "U); s.push_back(std::move(v));";
                break;
            case OpUVE::BuildMap: body = "ValueUVE v = BuildMapUVE(s, " + a + "U); s.push_back(std::move(v));"; break;
            case OpUVE::BuildTuple:
                body = "ValueUVE v = BuildTupleUVE(s, " + a + "U); s.push_back(std::move(v));";
                break;
            case OpUVE::GetIndex:
                body = "const ValueUVE k = PopUVE(s); const ValueUVE box = PopUVE(s); s.push_back(GetIndexUVE(box, k));";
                break;
            case OpUVE::SetIndex:
                body = "const ValueUVE v = PopUVE(s); const ValueUVE k = PopUVE(s); const ValueUVE box = PopUVE(s); "
                       "s.push_back(SetIndexUVE(box, k, v));";
                break;
            case OpUVE::Unpack: body = "UnpackUVE(s, " + a + "U);"; break;
            case OpUVE::Concat: body = "ValueUVE t = ConcatUVE(s, " + a + "U); s.push_back(std::move(t));"; break;
            case OpUVE::Jump: body = "goto I" + a + ";"; break;
            case OpUVE::JumpIfFalse: body = "if (!AsBoolUVE(PopUVE(s))) goto I" + a + ";"; break;
            case OpUVE::JumpIfFalseKeep: body = "if (!AsBoolUVE(s.back())) goto I" + a + "; s.pop_back();"; break;
            case OpUVE::JumpIfTrueKeep: body = "if (AsBoolUVE(s.back())) goto I" + a + "; s.pop_back();"; break;
            case OpUVE::Call: {
                const ChunkUVE& callee = m_program.functions[static_cast<std::size_t>(in.a)];
                body = "FrameUVE g; g.locals.assign(s.end() - " + b + ", s.end()); s.resize(s.size() - " + b +
                       "U); g.locals.resize(" + std::to_string(callee.localCount) + "U); CallChunkUVE(c, Chunk" + a +
                       ", g); s.push_back(std::move(g.result));";
                break;
            }
            case OpUVE::CallBuiltin:
                body = "const std::vector<ValueUVE> args(s.end() - " + b + ", s.end()); s.resize(s.size() - " + b +
                       "U); ValueUVE r = CallBuiltinUVE(c.host, " + a + ", args); s.push_back(std::move(r));";
                break;
            case OpUVE::CallHost:
                body = "const std::vector<ValueUVE> args(s.end() - " + b + ", s.end()); s.resize(s.size() - " + b +
                       "U); ValueUVE r = c.host.CallFunctionUVE(" + Name(in.a) + ", args); s.push_back(std::move(r));";
                break;
            case OpUVE::CallMethod:
                body = "const std::vector<ValueUVE> args(s.end() - " + b + ", s.end()); s.resize(s.size() - " + b +
                       "U); c.host.CallMethodUVE(AsObjectRefUVE(PopUVE(s)), " + Name(in.a) +
                       ", args); s.push_back(ValueUVE{});";
                break;
            case OpUVE::Return: body = "f.result = PopUVE(s); f.resume = 0U; return StatusUVE::Finished;"; break;
            case OpUVE::ReturnNone: body = "f.result = ValueUVE{}; f.resume = 0U; return StatusUVE::Finished;"; break;
            case OpUVE::Wait:
                body = "f.waitSeconds = std::max(0.0, AsDoubleUVE(PopUVE(s))); f.resume = " + std::to_string(n + 1U) +
                       "U; return StatusUVE::Waiting;";
                break;
        }
        m_out += "    { c.StepUVE(" + std::to_string(in.line) + "U); " + body + " }\n";
    }

    const ProgramUVE& m_program;
    std::string m_out;
    std::vector<std::optional<TypedPlanUVE>> m_plans;
};

} // namespace

std::string GenerateUVScriptNativeCppUVE(const ProgramUVE& program, const std::string_view origin) {
    return GeneratorUVE(program).Run(origin);
}

} // namespace UVE::UVScript
