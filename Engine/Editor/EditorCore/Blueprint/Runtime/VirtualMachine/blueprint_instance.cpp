// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_instance.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>
#include <utility>

#include "blueprint_native.h"
#include "blueprint_program.h"

namespace UVE::Blueprint {
namespace {

/// One event's run is cut off after this many instructions, so a loop that never ends stops the
/// script instead of the game.
constexpr std::size_t kInstructionBudget = 1'000'000U;
constexpr std::size_t kMaximumCallDepth = 256U;

/// Cross-node calls re-enter Call through the host; without a cap two scripts calling each
/// other would eat the C++ stack. Same depth as calls within one script.
thread_local std::size_t g_callDepth = 0U;
struct CallDepthGuard final {
    CallDepthGuard() { ++g_callDepth; }
    ~CallDepthGuard() { --g_callDepth; }
};

struct Frame final {
    const Chunk* chunk = nullptr;
    std::size_t ip = 0U;
    std::size_t base = 0U;
    /// Source line of the previous instruction checked in this frame. Breakpoints and steps
    /// trigger on arriving at a line, so the instructions after a `Call` on the caller's line
    /// do not re-trigger the breakpoint that the call already passed.
    std::uint32_t debugLine = 0U;
};

/// A handler that is running or paused on `wait`. Interpreted, it is a value stack and call
/// frames; native, the generated chunk's own frame.
struct Coroutine final {
    std::vector<Value> stack;
    std::vector<Frame> frames;
    double waitRemaining = 0.0;
    std::size_t nativeChunk = 0U;
    Native::Frame nativeFrame;
    bool nativeRunning = false;

    [[nodiscard]] bool IsRunning() const noexcept { return nativeRunning || !frames.empty(); }
};

using Native::Error;

[[nodiscard]] Native::ArithmeticOp ToArithmetic(const Op op) noexcept {
    switch (op) {
        case Op::Sub: return Native::ArithmeticOp::Sub;
        case Op::Mul: return Native::ArithmeticOp::Mul;
        case Op::Div: return Native::ArithmeticOp::Div;
        case Op::Mod: return Native::ArithmeticOp::Mod;
        default: return Native::ArithmeticOp::Add;
    }
}

[[nodiscard]] Native::CompareOp ToCompare(const Op op) noexcept {
    switch (op) {
        case Op::Le: return Native::CompareOp::Le;
        case Op::Gt: return Native::CompareOp::Gt;
        case Op::Ge: return Native::CompareOp::Ge;
        default: return Native::CompareOp::Lt;
    }
}

} // namespace

/// StartChunk's index for the field initializers, which are not one of the program's functions.
constexpr std::size_t kInitChunk = static_cast<std::size_t>(-1);

struct BlueprintInstance::State final {
    std::shared_ptr<const Program> program;
    /// The program's native form when one is linked in (and not refused), else null.
    const Native::ProgramTable* native = nullptr;
    BlueprintHost* host = nullptr;
    std::vector<Value> fields;
    std::vector<Coroutine> waiting;
    std::string lastError;

    enum class DebugAction : std::uint8_t { Continue, StepInto, StepOver };
    bool debuggerEnabled = false;
    std::size_t executionDepth = 0U;
    std::unordered_set<std::uint32_t> breakpoints;
    DebugAction debugAction = DebugAction::Continue;
    std::size_t debugStepDepth = 0U;
    bool debugSkipFirstInstruction = false;
    bool debugPaused = false;
    std::optional<Coroutine> pausedCoroutine;
    bool pausedFromCall = false;
    std::optional<ScriptDebugSnapshot> debugSnapshot;

    [[nodiscard]] ScriptDebugSnapshot MakeDebugSnapshot(const Coroutine& co,
                                                            const Frame& frame,
                                                            const std::size_t instructionIndex) const {
        ScriptDebugSnapshot snapshot;
        snapshot.line = frame.chunk->code[instructionIndex].line;
        snapshot.functionName = frame.chunk->name;
        snapshot.instructionIndex = instructionIndex;
        snapshot.callDepth = co.frames.size();
        const std::size_t begin = std::min(frame.base, co.stack.size());
        const std::size_t end = std::min(co.stack.size(), frame.base + frame.chunk->localCount);
        snapshot.locals.assign(co.stack.begin() + static_cast<std::ptrdiff_t>(begin),
                               co.stack.begin() + static_cast<std::ptrdiff_t>(end));
        snapshot.fields.reserve(program->fields.size());
        for (std::size_t i = 0U; i < program->fields.size(); ++i) {
            snapshot.fields.emplace_back(program->fields[i].name, fields[i]);
        }
        return snapshot;
    }

    [[nodiscard]] ScriptDebugResumeResult ResumePaused(const DebugAction action) {
        if (!pausedCoroutine.has_value()) {
            return {ScriptDebugState::NotPaused, std::nullopt};
        }
        debugAction = action;
        debugStepDepth = pausedCoroutine->frames.size();
        lastError.clear();
        debugSkipFirstInstruction = true;
        debugPaused = false;
        Coroutine co = std::move(*pausedCoroutine);
        pausedCoroutine.reset();
        const bool fromCall = pausedFromCall;
        std::optional<Value> result = Resume(co);
        if (debugPaused) {
            pausedCoroutine = std::move(co);
            return {ScriptDebugState::Paused, std::nullopt};
        }
        if (co.IsRunning()) {
            waiting.push_back(std::move(co));
            return {ScriptDebugState::Waiting, std::nullopt};
        }
        if (!lastError.empty()) {
            return {ScriptDebugState::Failed, std::nullopt};
        }
        return {ScriptDebugState::Completed, fromCall ? result : std::nullopt};
    }

    [[nodiscard]] bool DebuggerBusy() const noexcept {
        return debugPaused || (debuggerEnabled && executionDepth != 0U);
    }

    /// Starts `chunk` with `args` on a fresh coroutine.
    Coroutine Start(const Chunk& chunk, const std::span<const Value> args) const {
        Coroutine co;
        co.stack.assign(args.begin(), args.end());
        co.stack.resize(chunk.localCount);
        co.frames.push_back({&chunk, 0U, 0U});
        return co;
    }

    /// Runs until the coroutine finishes (returns its result), waits (returns nothing, `co` keeps
    /// its place) or fails (lastError is set, returns nothing and clears the frames).
    std::optional<Value> Run(Coroutine& co) {
        std::size_t budget = kInstructionBudget;
        std::uint32_t line = 0U;
        try {
            while (!co.frames.empty()) {
                if (budget-- == 0U) {
                    throw Error{"this ran too long without stopping - is a loop missing its exit?"};
                }
                Frame& frame = co.frames.back();
                const std::size_t instructionIndex = frame.ip;
                const Instruction& in = frame.chunk->code[instructionIndex];
                line = in.line;
                if (debuggerEnabled) {
                    const bool arrivedAtLine = frame.debugLine != line;
                    bool pause = false;
                    if (debugSkipFirstInstruction) {
                        debugSkipFirstInstruction = false;
                    } else if (arrivedAtLine && breakpoints.contains(line)) {
                        pause = true;
                    } else if (debugAction == DebugAction::StepInto) {
                        pause = arrivedAtLine;
                    } else if (debugAction == DebugAction::StepOver) {
                        pause = co.frames.size() <= debugStepDepth && arrivedAtLine;
                    }
                    frame.debugLine = line;
                    if (pause) {
                        debugPaused = true;
                        debugSnapshot = MakeDebugSnapshot(co, frame, instructionIndex);
                        return std::nullopt;
                    }
                }
                ++frame.ip;
                std::vector<Value>& s = co.stack;
                const auto pop = [&s] {
                    Value v = std::move(s.back());
                    s.pop_back();
                    return v;
                };
                switch (in.op) {
                    case Op::PushConst: s.push_back(program->constants[static_cast<std::size_t>(in.a)]); break;
                    case Op::Pop: s.pop_back(); break;
                    case Op::LoadLocal: s.push_back(s[frame.base + static_cast<std::size_t>(in.a)]); break;
                    case Op::StoreLocal: s[frame.base + static_cast<std::size_t>(in.a)] = pop(); break;
                    case Op::LoadField: s.push_back(fields[static_cast<std::size_t>(in.a)]); break;
                    case Op::StoreField: fields[static_cast<std::size_t>(in.a)] = pop(); break;
                    case Op::LoadProp:
                        s.push_back(host->GetProperty(std::get<std::string>(program->constants[static_cast<std::size_t>(in.a)])));
                        break;
                    case Op::StoreProp: {
                        const Value value = pop();
                        host->SetProperty(std::get<std::string>(program->constants[static_cast<std::size_t>(in.a)]), value);
                        break;
                    }
                    case Op::Add:
                    case Op::Sub:
                    case Op::Mul:
                    case Op::Div:
                    case Op::Mod: {
                        const Value b = pop();
                        const Value a = pop();
                        s.push_back(Native::Arithmetic(ToArithmetic(in.op), a, b));
                        break;
                    }
                    case Op::Neg: s.push_back(Native::Negate(pop())); break;
                    case Op::Not: s.push_back(!Native::AsBool(pop())); break;
                    case Op::Eq:
                    case Op::Ne: {
                        const Value b = pop();
                        const Value a = pop();
                        s.push_back(Native::Equal(a, b) == (in.op == Op::Eq));
                        break;
                    }
                    case Op::Lt:
                    case Op::Le:
                    case Op::Gt:
                    case Op::Ge: {
                        const Value b = pop();
                        const Value a = pop();
                        s.push_back(Native::Compare(ToCompare(in.op), a, b));
                        break;
                    }
                    case Op::ToFloat: s.push_back(Native::AsDouble(pop())); break;
                    case Op::GetComponent: s.push_back(Native::GetComponent(pop(), in.a)); break;
                    case Op::SetComponent: {
                        const Value value = pop();
                        const Value vector = pop();
                        s.push_back(Native::SetComponent(vector, in.a, value));
                        break;
                    }
                    case Op::Concat: {
                        Value text = Native::Concat(s, static_cast<std::size_t>(in.a));
                        s.push_back(std::move(text));
                        break;
                    }
                    case Op::BuildList: s.push_back(Native::BuildList(s, static_cast<std::size_t>(in.a))); break;
                    case Op::BuildMap: s.push_back(Native::BuildMap(s, static_cast<std::size_t>(in.a))); break;
                    case Op::BuildTuple: s.push_back(Native::BuildTuple(s, static_cast<std::size_t>(in.a))); break;
                    case Op::GetIndex: {
                        const Value key = pop();
                        const Value container = pop();
                        s.push_back(Native::GetIndex(container, key));
                        break;
                    }
                    case Op::SetIndex: {
                        const Value value = pop();
                        const Value key = pop();
                        const Value container = pop();
                        s.push_back(Native::SetIndex(container, key, value));
                        break;
                    }
                    case Op::Unpack: Native::Unpack(s, static_cast<std::size_t>(in.a)); break;
                    case Op::Jump: frame.ip = static_cast<std::size_t>(in.a); break;
                    case Op::JumpIfFalse:
                        if (!Native::AsBool(pop())) frame.ip = static_cast<std::size_t>(in.a);
                        break;
                    case Op::JumpIfFalseKeep:
                        if (!Native::AsBool(s.back())) frame.ip = static_cast<std::size_t>(in.a);
                        else s.pop_back();
                        break;
                    case Op::JumpIfTrueKeep:
                        if (Native::AsBool(s.back())) frame.ip = static_cast<std::size_t>(in.a);
                        else s.pop_back();
                        break;
                    case Op::Call: {
                        if (co.frames.size() >= kMaximumCallDepth) {
                            throw Error{"functions call each other too deeply"};
                        }
                        const Chunk& callee = program->functions[static_cast<std::size_t>(in.a)];
                        const std::size_t base = s.size() - static_cast<std::size_t>(in.b);
                        s.resize(base + callee.localCount);
                        co.frames.push_back({&callee, 0U, base});
                        break;
                    }
                    case Op::CallBuiltin: {
                        const std::size_t argc = static_cast<std::size_t>(in.b);
                        const std::vector<Value> args(s.end() - static_cast<std::ptrdiff_t>(argc), s.end());
                        s.resize(s.size() - argc);
                        s.push_back(Native::CallBuiltin(*host, in.a, args));
                        break;
                    }
                    case Op::CallHost: {
                        const std::size_t argc = static_cast<std::size_t>(in.b);
                        const std::vector<Value> args(s.end() - static_cast<std::ptrdiff_t>(argc), s.end());
                        s.resize(s.size() - argc);
                        s.push_back(host->CallFunction(std::get<std::string>(program->constants[static_cast<std::size_t>(in.a)]), args));
                        break;
                    }
                    case Op::CallMethod: {
                        const std::size_t argc = static_cast<std::size_t>(in.b);
                        const std::vector<Value> args(s.end() - static_cast<std::ptrdiff_t>(argc), s.end());
                        s.resize(s.size() - argc);
                        const ObjectRef target = Native::AsObjectRef(pop());
                        host->CallMethod(target, std::get<std::string>(program->constants[static_cast<std::size_t>(in.a)]), args);
                        s.push_back(Value{});
                        break;
                    }
                    case Op::Return:
                    case Op::ReturnNone: {
                        Value result = in.op == Op::Return ? pop() : Value{};
                        const std::size_t base = frame.base;
                        co.frames.pop_back();
                        s.resize(base);
                        if (co.frames.empty()) {
                            return result;
                        }
                        s.push_back(std::move(result));
                        break;
                    }
                    case Op::Wait:
                        co.waitRemaining = std::max(0.0, Native::AsDouble(pop()));
                        return std::nullopt;
                }
            }
        } catch (const Error& error) {
            lastError = "line " + std::to_string(line) + ": " + error.message;
        } catch (const std::bad_variant_access&) {
            lastError = "line " + std::to_string(line) + ": a value had an unexpected type";
        }
        debugPaused = false;
        co.frames.clear();
        return std::nullopt;
    }

    /// Starts `chunk` (a program function index, or the init chunk) the way this instance runs.
    Coroutine StartChunk(const std::size_t index, const std::span<const Value> args) const {
        const Chunk& chunk = index == kInitChunk ? program->init : program->functions[index];
        if (native == nullptr) {
            return Start(chunk, args);
        }
        Coroutine co;
        co.nativeChunk = index == kInitChunk ? native->initIndex : index;
        co.nativeFrame.locals.assign(args.begin(), args.end());
        co.nativeFrame.locals.resize(chunk.localCount);
        co.nativeRunning = true;
        return co;
    }

    /// Run, for either form of coroutine.
    std::optional<Value> Resume(Coroutine& co) {
        struct ExecutionGuard final {
            std::size_t& depth;
            explicit ExecutionGuard(std::size_t& value) : depth(value) { ++depth; }
            ~ExecutionGuard() { --depth; }
        } guard{executionDepth};
        if (!co.nativeRunning) {
            std::optional<Value> result = Run(co);
            if (!debugPaused) {
                debugAction = DebugAction::Continue;
            }
            return result;
        }
        Native::Context context{fields, *host, kInstructionBudget, 0U, 0U};
        try {
            if (native->chunks[co.nativeChunk](context, co.nativeFrame) == Native::Status::Waiting) {
                co.waitRemaining = co.nativeFrame.waitSeconds;
                return std::nullopt;
            }
            co.nativeRunning = false;
            return std::move(co.nativeFrame.result);
        } catch (const Error& error) {
            lastError = "line " + std::to_string(context.line) + ": " + error.message;
        } catch (const std::bad_variant_access&) {
            lastError = "line " + std::to_string(context.line) + ": a value had an unexpected type";
        }
        co.nativeRunning = false;
        return std::nullopt;
    }
};

namespace {

/// Whether a value handed in from outside fits a parameter's declared type. Both ways of running
/// rely on it: generated code unboxes parameters as their declared type.
[[nodiscard]] bool ArgsFit(const Chunk& chunk, const std::span<const Value> args) {
    for (std::size_t i = 0U; i < args.size() && i < chunk.localTypes.size(); ++i) {
        const Value& v = args[i];
        bool fits = true;
        switch (chunk.localTypes[i].kind) {
            case Type::Kind::Bool: fits = std::holds_alternative<bool>(v); break;
            case Type::Kind::Int: fits = std::holds_alternative<std::int64_t>(v); break;
            case Type::Kind::Float: fits = std::holds_alternative<double>(v); break;
            case Type::Kind::Str: fits = std::holds_alternative<std::string>(v); break;
            case Type::Kind::Vec3: fits = std::holds_alternative<Vec3Value>(v); break;
            default: break;
        }
        if (!fits) {
            return false;
        }
    }
    return true;
}

} // namespace

BlueprintInstance::BlueprintInstance(std::shared_ptr<const Program> program, BlueprintHost& host,
                                     const Execution execution)
    : m_state(std::make_unique<State>()) {
    m_state->program = std::move(program);
    m_state->host = &host;
    if (execution == Execution::Auto) {
        m_state->native = Native::FindNativeProgram(m_state->program->fingerprint);
        // A table from a different build of the same fingerprint would not line up; refuse it.
        if (m_state->native != nullptr && m_state->native->chunks.size() != m_state->program->functions.size() + 1U) {
            m_state->native = nullptr;
        }
    }
    m_state->fields.resize(m_state->program->fields.size());
    Coroutine init = m_state->StartChunk(kInitChunk, {});
    static_cast<void>(m_state->Resume(init));
}

BlueprintInstance::~BlueprintInstance() = default;

bool BlueprintInstance::RaiseEvent(const std::string_view event, const std::span<const Value> args) {
    if (m_state->DebuggerBusy()) return false;
    const auto& handlers = m_state->program->handlers;
    const auto it = std::ranges::find_if(handlers, [event](const auto& h) { return h.first == event; });
    if (it == handlers.end()) {
        return false;
    }
    const Chunk& chunk = m_state->program->functions[it->second];
    if (args.size() != chunk.paramCount) {
        m_state->lastError = "'" + std::string{event} + "' was raised with the wrong number of values";
        return false;
    }
    if (!ArgsFit(chunk, args)) {
        m_state->lastError = "'" + std::string{event} + "' was raised with a value of the wrong type";
        return false;
    }
    const std::string before = m_state->lastError;
    m_state->lastError.clear();
    Coroutine co = m_state->StartChunk(it->second, args);
    static_cast<void>(m_state->Resume(co));
    if (m_state->debugPaused) {
        m_state->pausedCoroutine = std::move(co);
        m_state->pausedFromCall = false;
    } else if (co.IsRunning()) {
        m_state->waiting.push_back(std::move(co));
    }
    const bool ok = m_state->lastError.empty();
    if (ok) {
        m_state->lastError = before;
    }
    return ok;
}

void BlueprintInstance::Advance(const double seconds) {
    if (m_state->DebuggerBusy()) return;
    // Only those already waiting when this call starts count down now; a handler that waits again
    // while resuming joins the back of the line for the next call.
    std::vector<Coroutine> waiting = std::move(m_state->waiting);
    m_state->waiting.clear();
    for (Coroutine& co : waiting) {
        co.waitRemaining -= seconds;
        // Keep other ready handlers queued once one owns the debugger pause. Time still
        // advances once for every handler that was waiting on entry to this tick.
        if (m_state->debugPaused) {
            m_state->waiting.push_back(std::move(co));
            continue;
        }
        if (co.waitRemaining > 1e-9) {
            m_state->waiting.push_back(std::move(co));
            continue;
        }
        static_cast<void>(m_state->Resume(co));
        if (m_state->debugPaused) {
            m_state->pausedCoroutine = std::move(co);
            m_state->pausedFromCall = false;
        } else if (co.IsRunning()) {
            m_state->waiting.push_back(std::move(co));
        }
    }
}

bool BlueprintInstance::HasFunction(const std::string_view name, const std::size_t argc) const noexcept {
    for (const Chunk& chunk : m_state->program->functions) {
        if (chunk.name == name && static_cast<std::size_t>(chunk.paramCount) == argc) {
            return true;
        }
    }
    return false;
}

std::optional<Value> BlueprintInstance::Call(const std::string_view name, const std::span<const Value> args) {
    if (m_state->DebuggerBusy()) return std::nullopt;
    if (g_callDepth >= kMaximumCallDepth) {
        // Fail closed like a missing method, without an error: the chain stops, and every call
        // already entered still completes. Writing lastError here would fail the outer calls too,
        // which all ran fine - only the call past the cap never started.
        return std::nullopt;
    }
    const CallDepthGuard depth;
    const auto& functions = m_state->program->functions;
    for (std::size_t index = 0U; index < functions.size(); ++index) {
        const Chunk& chunk = functions[index];
        if (chunk.name == name && chunk.paramCount == args.size()) {
            if (!ArgsFit(chunk, args)) {
                m_state->lastError = "'" + std::string{name} + "' was called with a value of the wrong type";
                return std::nullopt;
            }
            m_state->lastError.clear();
            Coroutine co = m_state->StartChunk(index, args);
            std::optional<Value> result = m_state->Resume(co);
            if (m_state->debugPaused) {
                m_state->pausedCoroutine = std::move(co);
                m_state->pausedFromCall = true;
            }
            return m_state->lastError.empty() ? result : std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<Value> BlueprintInstance::GetField(const std::string_view name) const {
    const auto& fields = m_state->program->fields;
    for (std::size_t i = 0U; i < fields.size(); ++i) {
        if (fields[i].name == name) {
            return m_state->fields[i];
        }
    }
    return std::nullopt;
}

bool BlueprintInstance::SetField(const std::string_view name, const Value& value) {
    const auto& fields = m_state->program->fields;
    for (std::size_t i = 0U; i < fields.size(); ++i) {
        if (fields[i].name != name) {
            continue;
        }
        if (fields[i].kind == FieldKind::Const || value.index() != m_state->fields[i].index()) {
            return false;
        }
        m_state->fields[i] = value;
        return true;
    }
    return false;
}

bool BlueprintInstance::IsNative() const noexcept {
    return m_state->native != nullptr;
}

std::size_t BlueprintInstance::GetWaitingCount() const noexcept {
    return m_state->waiting.size();
}

const std::string& BlueprintInstance::GetLastError() const noexcept {
    return m_state->lastError;
}

bool BlueprintInstance::EnableDebugger() {
    if (IsNative() || m_state->executionDepth != 0U) return false;
    m_state->debuggerEnabled = true;
    return true;
}

void BlueprintInstance::DisableDebugger() noexcept {
    m_state->debuggerEnabled = false;
    m_state->breakpoints.clear();
    m_state->pausedCoroutine.reset();
    m_state->debugSnapshot.reset();
    m_state->debugPaused = false;
    m_state->debugSkipFirstInstruction = false;
    m_state->debugAction = State::DebugAction::Continue;
}

bool BlueprintInstance::SetDebuggerBreakpoints(const std::span<const std::uint32_t> lines) {
    if (!EnableDebugger()) return false;
    m_state->breakpoints.clear();
    for (const std::uint32_t line : lines) {
        if (line != 0U) m_state->breakpoints.insert(line);
    }
    return true;
}

void BlueprintInstance::ClearDebuggerBreakpoints() noexcept { m_state->breakpoints.clear(); }
bool BlueprintInstance::IsDebuggerEnabled() const noexcept { return m_state->debuggerEnabled; }
bool BlueprintInstance::IsDebugPaused() const noexcept { return m_state->debugPaused; }
std::optional<ScriptDebugSnapshot> BlueprintInstance::GetDebugSnapshot() const {
    return m_state->debugPaused ? m_state->debugSnapshot : std::nullopt;
}
ScriptDebugResumeResult BlueprintInstance::ContinueDebug() {
    return m_state->ResumePaused(State::DebugAction::Continue);
}
ScriptDebugResumeResult BlueprintInstance::StepIntoDebug() {
    return m_state->ResumePaused(State::DebugAction::StepInto);
}
ScriptDebugResumeResult BlueprintInstance::StepOverDebug() {
    return m_state->ResumePaused(State::DebugAction::StepOver);
}

} // namespace UVE::Blueprint
