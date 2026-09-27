// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/uvscript/uvscript_instance_uve.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "uve/uvscript/uvscript_native_uve.h"
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

/// A handler that is running or paused on `wait`. Interpreted, it is a value stack and call
/// frames; native, the generated chunk's own frame.
struct CoroutineUVE final {
    std::vector<ValueUVE> stack;
    std::vector<FrameUVE> frames;
    double waitRemaining = 0.0;
    std::size_t nativeChunk = 0U;
    Native::FrameUVE nativeFrame;
    bool nativeRunning = false;

    [[nodiscard]] bool IsRunningUVE() const noexcept { return nativeRunning || !frames.empty(); }
};

using Native::ErrorUVE;

[[nodiscard]] Native::ArithmeticOpUVE ToArithmeticUVE(const OpUVE op) noexcept {
    switch (op) {
        case OpUVE::Sub: return Native::ArithmeticOpUVE::Sub;
        case OpUVE::Mul: return Native::ArithmeticOpUVE::Mul;
        case OpUVE::Div: return Native::ArithmeticOpUVE::Div;
        case OpUVE::Mod: return Native::ArithmeticOpUVE::Mod;
        default: return Native::ArithmeticOpUVE::Add;
    }
}

[[nodiscard]] Native::CompareOpUVE ToCompareUVE(const OpUVE op) noexcept {
    switch (op) {
        case OpUVE::Le: return Native::CompareOpUVE::Le;
        case OpUVE::Gt: return Native::CompareOpUVE::Gt;
        case OpUVE::Ge: return Native::CompareOpUVE::Ge;
        default: return Native::CompareOpUVE::Lt;
    }
}

} // namespace

/// StartChunk's index for the field initializers, which are not one of the program's functions.
constexpr std::size_t kInitChunkUVE = static_cast<std::size_t>(-1);

struct ScriptInstanceUVE::StateUVE final {
    std::shared_ptr<const ProgramUVE> program;
    /// The program's native form when one is linked in (and not refused), else null.
    const Native::ProgramTableUVE* native = nullptr;
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
                    throw ErrorUVE{"this ran too long without stopping - is a loop missing its exit?"};
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
                        s.push_back(Native::ArithmeticUVE(ToArithmeticUVE(in.op), a, b));
                        break;
                    }
                    case OpUVE::Neg: s.push_back(Native::NegateUVE(pop())); break;
                    case OpUVE::Not: s.push_back(!Native::AsBoolUVE(pop())); break;
                    case OpUVE::Eq:
                    case OpUVE::Ne: {
                        const ValueUVE b = pop();
                        const ValueUVE a = pop();
                        s.push_back(Native::EqualUVE(a, b) == (in.op == OpUVE::Eq));
                        break;
                    }
                    case OpUVE::Lt:
                    case OpUVE::Le:
                    case OpUVE::Gt:
                    case OpUVE::Ge: {
                        const ValueUVE b = pop();
                        const ValueUVE a = pop();
                        s.push_back(Native::CompareUVE(ToCompareUVE(in.op), a, b));
                        break;
                    }
                    case OpUVE::ToFloat: s.push_back(Native::AsDoubleUVE(pop())); break;
                    case OpUVE::GetComponent: s.push_back(Native::GetComponentUVE(pop(), in.a)); break;
                    case OpUVE::SetComponent: {
                        const ValueUVE value = pop();
                        const ValueUVE vector = pop();
                        s.push_back(Native::SetComponentUVE(vector, in.a, value));
                        break;
                    }
                    case OpUVE::Concat: {
                        ValueUVE text = Native::ConcatUVE(s, static_cast<std::size_t>(in.a));
                        s.push_back(std::move(text));
                        break;
                    }
                    case OpUVE::Jump: frame.ip = static_cast<std::size_t>(in.a); break;
                    case OpUVE::JumpIfFalse:
                        if (!Native::AsBoolUVE(pop())) frame.ip = static_cast<std::size_t>(in.a);
                        break;
                    case OpUVE::JumpIfFalseKeep:
                        if (!Native::AsBoolUVE(s.back())) frame.ip = static_cast<std::size_t>(in.a);
                        else s.pop_back();
                        break;
                    case OpUVE::JumpIfTrueKeep:
                        if (Native::AsBoolUVE(s.back())) frame.ip = static_cast<std::size_t>(in.a);
                        else s.pop_back();
                        break;
                    case OpUVE::Call: {
                        if (co.frames.size() >= kMaximumCallDepthUVE) {
                            throw ErrorUVE{"functions call each other too deeply"};
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
                        s.push_back(Native::CallBuiltinUVE(*host, in.a, args));
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
                        co.waitRemaining = std::max(0.0, Native::AsDoubleUVE(pop()));
                        return std::nullopt;
                }
            }
        } catch (const ErrorUVE& error) {
            lastError = "line " + std::to_string(line) + ": " + error.message;
        } catch (const std::bad_variant_access&) {
            lastError = "line " + std::to_string(line) + ": a value had an unexpected type";
        }
        co.frames.clear();
        return std::nullopt;
    }

    /// Starts `chunk` (a program function index, or the init chunk) the way this instance runs.
    CoroutineUVE StartChunk(const std::size_t index, const std::span<const ValueUVE> args) const {
        const ChunkUVE& chunk = index == kInitChunkUVE ? program->init : program->functions[index];
        if (native == nullptr) {
            return Start(chunk, args);
        }
        CoroutineUVE co;
        co.nativeChunk = index == kInitChunkUVE ? native->initIndex : index;
        co.nativeFrame.locals.assign(args.begin(), args.end());
        co.nativeFrame.locals.resize(chunk.localCount);
        co.nativeRunning = true;
        return co;
    }

    /// Run, for either form of coroutine.
    std::optional<ValueUVE> Resume(CoroutineUVE& co) {
        if (!co.nativeRunning) {
            return Run(co);
        }
        Native::ContextUVE context{fields, *host, kInstructionBudgetUVE, 0U, 0U};
        try {
            if (native->chunks[co.nativeChunk](context, co.nativeFrame) == Native::StatusUVE::Waiting) {
                co.waitRemaining = co.nativeFrame.waitSeconds;
                return std::nullopt;
            }
            co.nativeRunning = false;
            return std::move(co.nativeFrame.result);
        } catch (const ErrorUVE& error) {
            lastError = "line " + std::to_string(context.line) + ": " + error.message;
        } catch (const std::bad_variant_access&) {
            lastError = "line " + std::to_string(context.line) + ": a value had an unexpected type";
        }
        co.nativeRunning = false;
        return std::nullopt;
    }
};

ScriptInstanceUVE::ScriptInstanceUVE(std::shared_ptr<const ProgramUVE> program, UVScriptHostUVE& host,
                                     const ExecutionUVE execution)
    : m_state(std::make_unique<StateUVE>()) {
    m_state->program = std::move(program);
    m_state->host = &host;
    if (execution == ExecutionUVE::Auto) {
        m_state->native = Native::FindNativeProgramUVE(m_state->program->fingerprint);
        // A table from a different build of the same fingerprint would not line up; refuse it.
        if (m_state->native != nullptr && m_state->native->chunks.size() != m_state->program->functions.size() + 1U) {
            m_state->native = nullptr;
        }
    }
    m_state->fields.resize(m_state->program->fields.size());
    CoroutineUVE init = m_state->StartChunk(kInitChunkUVE, {});
    static_cast<void>(m_state->Resume(init));
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
    CoroutineUVE co = m_state->StartChunk(it->second, args);
    static_cast<void>(m_state->Resume(co));
    if (co.IsRunningUVE()) {
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
        static_cast<void>(m_state->Resume(co));
        if (co.IsRunningUVE()) {
            m_state->waiting.push_back(std::move(co));
        }
    }
}

std::optional<ValueUVE> ScriptInstanceUVE::CallUVE(const std::string_view name, const std::span<const ValueUVE> args) {
    const auto& functions = m_state->program->functions;
    for (std::size_t index = 0U; index < functions.size(); ++index) {
        const ChunkUVE& chunk = functions[index];
        if (chunk.name == name && chunk.paramCount == args.size()) {
            m_state->lastError.clear();
            CoroutineUVE co = m_state->StartChunk(index, args);
            std::optional<ValueUVE> result = m_state->Resume(co);
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

bool ScriptInstanceUVE::IsNativeUVE() const noexcept {
    return m_state->native != nullptr;
}

std::size_t ScriptInstanceUVE::GetWaitingCountUVE() const noexcept {
    return m_state->waiting.size();
}

const std::string& ScriptInstanceUVE::GetLastErrorUVE() const noexcept {
    return m_state->lastError;
}

} // namespace UVE::UVScript
