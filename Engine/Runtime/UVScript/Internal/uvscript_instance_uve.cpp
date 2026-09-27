// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/uvscript/uvscript_instance_uve.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "uvscript_program_uve.h"

namespace UVE::UVScript {
namespace {

/// One event's run is cut off after this many instructions, so a loop that never ends stops the
/// script instead of the game.
constexpr std::size_t kInstructionBudgetUVE = 1'000'000U;
constexpr std::size_t kMaximumCallDepthUVE = 256U;

struct FrameUVE final {
    const ChunkUVE* chunk = nullptr;
    std::size_t ip = 0U;
    std::size_t base = 0U;
};

/// A handler that is running or paused on `wait`: its own value stack and call frames.
struct CoroutineUVE final {
    std::vector<ValueUVE> stack;
    std::vector<FrameUVE> frames;
    double waitRemaining = 0.0;
};

/// Thrown inside Run to stop the current coroutine with a message.
struct RuntimeErrorUVE final {
    std::string message;
};

[[nodiscard]] bool IsNumberUVE(const ValueUVE& v) noexcept {
    return std::holds_alternative<std::int64_t>(v) || std::holds_alternative<double>(v);
}

[[nodiscard]] double AsDoubleUVE(const ValueUVE& v) {
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return static_cast<double>(*i);
    }
    if (const auto* d = std::get_if<double>(&v)) {
        return *d;
    }
    throw RuntimeErrorUVE{"expected a number"};
}

[[nodiscard]] Vec3ValueUVE AsVecUVE(const ValueUVE& v) {
    if (const auto* vec = std::get_if<Vec3ValueUVE>(&v)) {
        return *vec;
    }
    throw RuntimeErrorUVE{"expected a vec3"};
}

[[nodiscard]] ValueUVE ArithmeticUVE(const OpUVE op, const ValueUVE& a, const ValueUVE& b) {
    const auto* ia = std::get_if<std::int64_t>(&a);
    const auto* ib = std::get_if<std::int64_t>(&b);
    if (ia != nullptr && ib != nullptr) {
        switch (op) {
            case OpUVE::Add: return *ia + *ib;
            case OpUVE::Sub: return *ia - *ib;
            case OpUVE::Mul: return *ia * *ib;
            case OpUVE::Mod:
                if (*ib == 0) {
                    throw RuntimeErrorUVE{"'%' by zero"};
                }
                return *ia % *ib;
            case OpUVE::Div:
                if (*ib == 0) {
                    throw RuntimeErrorUVE{"division by zero"};
                }
                return static_cast<double>(*ia) / static_cast<double>(*ib);
            default: break;
        }
    }
    if (IsNumberUVE(a) && IsNumberUVE(b)) {
        const double x = AsDoubleUVE(a);
        const double y = AsDoubleUVE(b);
        switch (op) {
            case OpUVE::Add: return x + y;
            case OpUVE::Sub: return x - y;
            case OpUVE::Mul: return x * y;
            case OpUVE::Div:
                if (y == 0.0) {
                    throw RuntimeErrorUVE{"division by zero"};
                }
                return x / y;
            case OpUVE::Mod:
                if (y == 0.0) {
                    throw RuntimeErrorUVE{"'%' by zero"};
                }
                return std::fmod(x, y);
            default: break;
        }
    }
    if (const auto* sa = std::get_if<std::string>(&a); sa != nullptr && op == OpUVE::Add) {
        return *sa + std::get<std::string>(b);
    }
    if (std::holds_alternative<Vec3ValueUVE>(a) && std::holds_alternative<Vec3ValueUVE>(b)) {
        const Vec3ValueUVE u = AsVecUVE(a);
        const Vec3ValueUVE v = AsVecUVE(b);
        return op == OpUVE::Add ? Vec3ValueUVE{u.x + v.x, u.y + v.y, u.z + v.z}
                                : Vec3ValueUVE{u.x - v.x, u.y - v.y, u.z - v.z};
    }
    if (std::holds_alternative<Vec3ValueUVE>(a)) {
        const Vec3ValueUVE u = AsVecUVE(a);
        const double s = AsDoubleUVE(b);
        if (op == OpUVE::Div && s == 0.0) {
            throw RuntimeErrorUVE{"division by zero"};
        }
        return op == OpUVE::Mul ? Vec3ValueUVE{u.x * s, u.y * s, u.z * s} : Vec3ValueUVE{u.x / s, u.y / s, u.z / s};
    }
    const double s = AsDoubleUVE(a);
    const Vec3ValueUVE v = AsVecUVE(b);
    return Vec3ValueUVE{v.x * s, v.y * s, v.z * s};
}

[[nodiscard]] int CompareUVE(const ValueUVE& a, const ValueUVE& b) {
    const double x = AsDoubleUVE(a);
    const double y = AsDoubleUVE(b);
    return x < y ? -1 : x > y ? 1 : 0;
}

[[nodiscard]] bool EqualUVE(const ValueUVE& a, const ValueUVE& b) {
    if (IsNumberUVE(a) && IsNumberUVE(b)) {
        return AsDoubleUVE(a) == AsDoubleUVE(b);
    }
    return a == b;
}

} // namespace

struct ScriptInstanceUVE::StateUVE final {
    std::shared_ptr<const ProgramUVE> program;
    UVScriptHostUVE* host = nullptr;
    std::vector<ValueUVE> fields;
    std::vector<CoroutineUVE> waiting;
    std::string lastError;

    /// Starts `chunk` with `args` on a fresh coroutine.
    CoroutineUVE Start(const ChunkUVE& chunk, const std::span<const ValueUVE> args) const {
        CoroutineUVE co;
        co.stack.assign(args.begin(), args.end());
        co.stack.resize(chunk.localCount);
        co.frames.push_back({&chunk, 0U, 0U});
        return co;
    }

    /// Runs until the coroutine finishes (returns its result), waits (returns nothing, `co` keeps
    /// its place) or fails (lastError is set, returns nothing and clears the frames).
    std::optional<ValueUVE> Run(CoroutineUVE& co) {
        std::size_t budget = kInstructionBudgetUVE;
        std::uint32_t line = 0U;
        try {
            while (!co.frames.empty()) {
                if (budget-- == 0U) {
                    throw RuntimeErrorUVE{"this ran too long without stopping - is a loop missing its exit?"};
                }
                FrameUVE& frame = co.frames.back();
                const InstructionUVE& in = frame.chunk->code[frame.ip++];
                line = in.line;
                std::vector<ValueUVE>& s = co.stack;
                const auto pop = [&s] {
                    ValueUVE v = std::move(s.back());
                    s.pop_back();
                    return v;
                };
                switch (in.op) {
                    case OpUVE::PushConst: s.push_back(program->constants[static_cast<std::size_t>(in.a)]); break;
                    case OpUVE::Pop: s.pop_back(); break;
                    case OpUVE::LoadLocal: s.push_back(s[frame.base + static_cast<std::size_t>(in.a)]); break;
                    case OpUVE::StoreLocal: s[frame.base + static_cast<std::size_t>(in.a)] = pop(); break;
                    case OpUVE::LoadField: s.push_back(fields[static_cast<std::size_t>(in.a)]); break;
                    case OpUVE::StoreField: fields[static_cast<std::size_t>(in.a)] = pop(); break;
                    case OpUVE::LoadProp:
                        s.push_back(host->GetPropertyUVE(std::get<std::string>(program->constants[static_cast<std::size_t>(in.a)])));
                        break;
                    case OpUVE::StoreProp: {
                        const ValueUVE value = pop();
                        host->SetPropertyUVE(std::get<std::string>(program->constants[static_cast<std::size_t>(in.a)]), value);
                        break;
                    }
                    case OpUVE::Add:
                    case OpUVE::Sub:
                    case OpUVE::Mul:
                    case OpUVE::Div:
                    case OpUVE::Mod: {
                        const ValueUVE b = pop();
                        const ValueUVE a = pop();
                        s.push_back(ArithmeticUVE(in.op, a, b));
                        break;
                    }
                    case OpUVE::Neg: {
                        ValueUVE v = pop();
                        if (auto* i = std::get_if<std::int64_t>(&v)) *i = -*i;
                        else if (auto* d = std::get_if<double>(&v)) *d = -*d;
                        else if (auto* vec = std::get_if<Vec3ValueUVE>(&v)) *vec = {-vec->x, -vec->y, -vec->z};
                        s.push_back(std::move(v));
                        break;
                    }
                    case OpUVE::Not: s.push_back(!std::get<bool>(pop())); break;
                    case OpUVE::Eq:
                    case OpUVE::Ne: {
                        const ValueUVE b = pop();
                        const ValueUVE a = pop();
                        s.push_back(EqualUVE(a, b) == (in.op == OpUVE::Eq));
                        break;
                    }
                    case OpUVE::Lt:
                    case OpUVE::Le:
                    case OpUVE::Gt:
                    case OpUVE::Ge: {
                        const ValueUVE b = pop();
                        const ValueUVE a = pop();
                        const int c = CompareUVE(a, b);
                        s.push_back(in.op == OpUVE::Lt ? c < 0 : in.op == OpUVE::Le ? c <= 0 : in.op == OpUVE::Gt ? c > 0 : c >= 0);
                        break;
                    }
                    case OpUVE::ToFloat: s.push_back(AsDoubleUVE(pop())); break;
                    case OpUVE::GetComponent: {
                        const Vec3ValueUVE v = AsVecUVE(pop());
                        s.push_back(in.a == 0 ? v.x : in.a == 1 ? v.y : v.z);
                        break;
                    }
                    case OpUVE::SetComponent: {
                        const double value = AsDoubleUVE(pop());
                        Vec3ValueUVE v = AsVecUVE(pop());
                        (in.a == 0 ? v.x : in.a == 1 ? v.y : v.z) = value;
                        s.push_back(v);
                        break;
                    }
                    case OpUVE::Concat: {
                        std::string text;
                        const std::size_t count = static_cast<std::size_t>(in.a);
                        for (std::size_t i = s.size() - count; i < s.size(); ++i) {
                            text += FormatValueUVE(s[i]);
                        }
                        s.resize(s.size() - count);
                        s.push_back(std::move(text));
                        break;
                    }
                    case OpUVE::Jump: frame.ip = static_cast<std::size_t>(in.a); break;
                    case OpUVE::JumpIfFalse:
                        if (!std::get<bool>(pop())) frame.ip = static_cast<std::size_t>(in.a);
                        break;
                    case OpUVE::JumpIfFalseKeep:
                        if (!std::get<bool>(s.back())) frame.ip = static_cast<std::size_t>(in.a);
                        else s.pop_back();
                        break;
                    case OpUVE::JumpIfTrueKeep:
                        if (std::get<bool>(s.back())) frame.ip = static_cast<std::size_t>(in.a);
                        else s.pop_back();
                        break;
                    case OpUVE::Call: {
                        if (co.frames.size() >= kMaximumCallDepthUVE) {
                            throw RuntimeErrorUVE{"functions call each other too deeply"};
                        }
                        const ChunkUVE& callee = program->functions[static_cast<std::size_t>(in.a)];
                        const std::size_t base = s.size() - static_cast<std::size_t>(in.b);
                        s.resize(base + callee.localCount);
                        co.frames.push_back({&callee, 0U, base});
                        break;
                    }
                    case OpUVE::CallBuiltin: {
                        const std::size_t argc = static_cast<std::size_t>(in.b);
                        const std::vector<ValueUVE> args(s.end() - static_cast<std::ptrdiff_t>(argc), s.end());
                        s.resize(s.size() - argc);
                        s.push_back(CallBuiltin(static_cast<BuiltinUVE>(in.a), args));
                        break;
                    }
                    case OpUVE::CallHost: {
                        const std::size_t argc = static_cast<std::size_t>(in.b);
                        const std::vector<ValueUVE> args(s.end() - static_cast<std::ptrdiff_t>(argc), s.end());
                        s.resize(s.size() - argc);
                        s.push_back(host->CallFunctionUVE(std::get<std::string>(program->constants[static_cast<std::size_t>(in.a)]), args));
                        break;
                    }
                    case OpUVE::Return:
                    case OpUVE::ReturnNone: {
                        ValueUVE result = in.op == OpUVE::Return ? pop() : ValueUVE{};
                        const std::size_t base = frame.base;
                        co.frames.pop_back();
                        s.resize(base);
                        if (co.frames.empty()) {
                            return result;
                        }
                        s.push_back(std::move(result));
                        break;
                    }
                    case OpUVE::Wait:
                        co.waitRemaining = std::max(0.0, AsDoubleUVE(pop()));
                        return std::nullopt;
                }
            }
        } catch (const RuntimeErrorUVE& error) {
            lastError = "line " + std::to_string(line) + ": " + error.message;
        } catch (const std::bad_variant_access&) {
            lastError = "line " + std::to_string(line) + ": a value had an unexpected type";
        }
        co.frames.clear();
        return std::nullopt;
    }

    ValueUVE CallBuiltin(const BuiltinUVE id, const std::vector<ValueUVE>& a) {
        const bool allInt = std::ranges::all_of(a, [](const ValueUVE& v) { return std::holds_alternative<std::int64_t>(v); });
        switch (id) {
            case BuiltinUVE::Print: host->PrintUVE(FormatValueUVE(a[0])); return {};
            case BuiltinUVE::Str: return FormatValueUVE(a[0]);
            case BuiltinUVE::Sqrt: {
                const double x = AsDoubleUVE(a[0]);
                if (x < 0.0) {
                    throw RuntimeErrorUVE{"sqrt of a negative number"};
                }
                return std::sqrt(x);
            }
            case BuiltinUVE::Sin: return std::sin(AsDoubleUVE(a[0]));
            case BuiltinUVE::Cos: return std::cos(AsDoubleUVE(a[0]));
            case BuiltinUVE::Floor: return std::floor(AsDoubleUVE(a[0]));
            case BuiltinUVE::Lerp: return AsDoubleUVE(a[0]) + (AsDoubleUVE(a[1]) - AsDoubleUVE(a[0])) * AsDoubleUVE(a[2]);
            case BuiltinUVE::Int: return static_cast<std::int64_t>(AsDoubleUVE(a[0]));
            case BuiltinUVE::Float: return AsDoubleUVE(a[0]);
            case BuiltinUVE::Abs:
                if (allInt) return std::abs(std::get<std::int64_t>(a[0]));
                return std::fabs(AsDoubleUVE(a[0]));
            case BuiltinUVE::Min:
                if (allInt) return std::min(std::get<std::int64_t>(a[0]), std::get<std::int64_t>(a[1]));
                return std::min(AsDoubleUVE(a[0]), AsDoubleUVE(a[1]));
            case BuiltinUVE::Max:
                if (allInt) return std::max(std::get<std::int64_t>(a[0]), std::get<std::int64_t>(a[1]));
                return std::max(AsDoubleUVE(a[0]), AsDoubleUVE(a[1]));
            case BuiltinUVE::Clamp:
                if (allInt) {
                    return std::clamp(std::get<std::int64_t>(a[0]), std::get<std::int64_t>(a[1]),
                                      std::max(std::get<std::int64_t>(a[1]), std::get<std::int64_t>(a[2])));
                }
                return std::clamp(AsDoubleUVE(a[0]), AsDoubleUVE(a[1]), std::max(AsDoubleUVE(a[1]), AsDoubleUVE(a[2])));
            case BuiltinUVE::Vec3: return Vec3ValueUVE{AsDoubleUVE(a[0]), AsDoubleUVE(a[1]), AsDoubleUVE(a[2])};
            case BuiltinUVE::Length: {
                const Vec3ValueUVE v = AsVecUVE(a[0]);
                return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            }
            case BuiltinUVE::Normalize: {
                const Vec3ValueUVE v = AsVecUVE(a[0]);
                const double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                return length == 0.0 ? v : Vec3ValueUVE{v.x / length, v.y / length, v.z / length};
            }
        }
        return {};
    }
};

ScriptInstanceUVE::ScriptInstanceUVE(std::shared_ptr<const ProgramUVE> program, UVScriptHostUVE& host)
    : m_state(std::make_unique<StateUVE>()) {
    m_state->program = std::move(program);
    m_state->host = &host;
    m_state->fields.resize(m_state->program->fields.size());
    CoroutineUVE init = m_state->Start(m_state->program->init, {});
    static_cast<void>(m_state->Run(init));
}

ScriptInstanceUVE::~ScriptInstanceUVE() = default;

bool ScriptInstanceUVE::RaiseEventUVE(const std::string_view event, const std::span<const ValueUVE> args) {
    const auto& handlers = m_state->program->handlers;
    const auto it = std::ranges::find_if(handlers, [event](const auto& h) { return h.first == event; });
    if (it == handlers.end()) {
        return false;
    }
    const ChunkUVE& chunk = m_state->program->functions[it->second];
    if (args.size() != chunk.paramCount) {
        m_state->lastError = "'" + std::string{event} + "' was raised with the wrong number of values";
        return false;
    }
    const std::string before = m_state->lastError;
    m_state->lastError.clear();
    CoroutineUVE co = m_state->Start(chunk, args);
    static_cast<void>(m_state->Run(co));
    if (!co.frames.empty()) {
        m_state->waiting.push_back(std::move(co));
    }
    const bool ok = m_state->lastError.empty();
    if (ok) {
        m_state->lastError = before;
    }
    return ok;
}

void ScriptInstanceUVE::AdvanceUVE(const double seconds) {
    // Only those already waiting when this call starts count down now; a handler that waits again
    // while resuming joins the back of the line for the next call.
    std::vector<CoroutineUVE> waiting = std::move(m_state->waiting);
    m_state->waiting.clear();
    for (CoroutineUVE& co : waiting) {
        co.waitRemaining -= seconds;
        if (co.waitRemaining > 1e-9) {
            m_state->waiting.push_back(std::move(co));
            continue;
        }
        static_cast<void>(m_state->Run(co));
        if (!co.frames.empty()) {
            m_state->waiting.push_back(std::move(co));
        }
    }
}

std::optional<ValueUVE> ScriptInstanceUVE::CallUVE(const std::string_view name, const std::span<const ValueUVE> args) {
    for (const ChunkUVE& chunk : m_state->program->functions) {
        if (chunk.name == name && chunk.paramCount == args.size()) {
            m_state->lastError.clear();
            CoroutineUVE co = m_state->Start(chunk, args);
            std::optional<ValueUVE> result = m_state->Run(co);
            return m_state->lastError.empty() ? result : std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<ValueUVE> ScriptInstanceUVE::GetFieldUVE(const std::string_view name) const {
    const auto& fields = m_state->program->fields;
    for (std::size_t i = 0U; i < fields.size(); ++i) {
        if (fields[i].name == name) {
            return m_state->fields[i];
        }
    }
    return std::nullopt;
}

bool ScriptInstanceUVE::SetFieldUVE(const std::string_view name, const ValueUVE& value) {
    const auto& fields = m_state->program->fields;
    for (std::size_t i = 0U; i < fields.size(); ++i) {
        if (fields[i].name != name) {
            continue;
        }
        if (fields[i].kind == FieldKindUVE::Const || value.index() != m_state->fields[i].index()) {
            return false;
        }
        m_state->fields[i] = value;
        return true;
    }
    return false;
}

std::size_t ScriptInstanceUVE::GetWaitingCountUVE() const noexcept {
    return m_state->waiting.size();
}

const std::string& ScriptInstanceUVE::GetLastErrorUVE() const noexcept {
    return m_state->lastError;
}

} // namespace UVE::UVScript
