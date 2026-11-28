// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// The runtime that C++ generated from UVScript (see blueprint_codegen.h) is compiled against.
// Generated code is a translation of a program's bytecode into straight C++ - no instruction fetch,
// no dispatch loop - that calls the same value operations the interpreter uses, so a script behaves
// the same whichever way it runs. Nothing here is meant to be called by hand.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "blueprint_host.h"
#include "blueprint_value.h"

namespace UVE::Blueprint::Native {

/// Stops the running handler or function with a message ("division by zero").
struct Error final {
    std::string message;
};

/// One handler or function in progress. A handler that reaches `wait` returns Waiting with
/// `resume` set, and is called again later with the same frame to carry on from there.
struct Frame final {
    std::vector<Value> locals;
    std::vector<Value> stack;
    std::uint32_t resume = 0U;
    double waitSeconds = 0.0;
    Value result;
};

enum class Status : std::uint8_t {
    Finished,
    Waiting,
};

/// What a running chunk may touch: the instance's fields and the object's host. It also carries the
/// instruction budget and the current source line, for the same errors the interpreter reports.
struct Context final {
    std::vector<Value>& fields;
    UVScriptHost& host;
    std::size_t budget = 0U;
    std::size_t depth = 0U;
    std::uint32_t line = 0U;

    /// Called before every translated instruction.
    void Step(const std::uint32_t sourceLine) {
        line = sourceLine;
        if (budget-- == 0U) {
            throw Error{"this ran too long without stopping - is a loop missing its exit?"};
        }
    }
};

using ChunkFunction = Status (*)(Context& context, Frame& frame);

/// A whole program in native form. `chunks[i]` is the program's function i (handlers included) and
/// `chunks[initIndex]` runs the field initializers.
struct ProgramTable final {
    std::uint64_t fingerprint = 0U;
    std::span<const ChunkFunction> chunks;
    std::size_t initIndex = 0U;
};

/// Makes a native program available. Generated code does this from a static object, so linking a
/// generated file into the game is all it takes. A later table with the same fingerprint wins.
void RegisterNativeProgram(const ProgramTable& table);
/// The native form of the program with this fingerprint, or null (it then runs interpreted).
[[nodiscard]] const ProgramTable* FindNativeProgram(std::uint64_t fingerprint) noexcept;

/// Registers a table when constructed; one static instance per generated file.
struct Registration final {
    explicit Registration(const ProgramTable& table) { RegisterNativeProgram(table); }
};

// ---- Value operations, shared with the interpreter. Each throws Error on a run-time error.

enum class ArithmeticOp : std::uint8_t {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
};

enum class CompareOp : std::uint8_t {
    Lt,
    Le,
    Gt,
    Ge,
};

[[nodiscard]] double AsDouble(const Value& value);
[[nodiscard]] Vec3Value AsVec3(const Value& value);
[[nodiscard]] bool AsBool(const Value& value);
[[nodiscard]] ObjectRef AsObjectRef(const Value& value);
[[nodiscard]] const std::string& AsString(const Value& value);
[[nodiscard]] Value Arithmetic(ArithmeticOp op, const Value& a, const Value& b);
[[nodiscard]] Value Negate(Value value);
[[nodiscard]] bool Equal(const Value& a, const Value& b);
[[nodiscard]] bool Compare(CompareOp op, const Value& a, const Value& b);
[[nodiscard]] Value GetComponent(const Value& vector, int component);
[[nodiscard]] Value SetComponent(const Value& vector, int component, const Value& value);
[[nodiscard]] inline Value Pop(std::vector<Value>& stack) {
    Value value = std::move(stack.back());
    stack.pop_back();
    return value;
}

/// Calls another function of the same program (`fn` never waits) with `frame` holding its
/// arguments and locals; the result is left in `frame.result`.
inline void CallChunk(Context& context, const ChunkFunction callee, Frame& frame) {
    // The interpreter counts the caller's frame too, so it refuses at the same depth.
    if (context.depth + 1U >= 256U) {
        throw Error{"functions call each other too deeply"};
    }
    ++context.depth;
    const Status status = callee(context, frame);
    --context.depth;
    if (status != Status::Finished) {
        throw Error{"a function cannot wait"};
    }
}

/// The collection inside `value`, or an error when it holds anything else.
[[nodiscard]] const CollectionValue& AsCollection(const Value& value);
/// Builds a collection from the last values of `stack`, removing them: `count` items for a list
/// or tuple, key, value, ... (`pairs` pairs) for a map, where a repeated key keeps the last value.
[[nodiscard]] Value BuildList(std::vector<Value>& stack, std::size_t count);
[[nodiscard]] Value BuildMap(std::vector<Value>& stack, std::size_t pairs);
[[nodiscard]] Value BuildTuple(std::vector<Value>& stack, std::size_t count);
/// Reads `container[key]`: an int item of a list or tuple, or a map's value for the key.
[[nodiscard]] Value GetIndex(const Value& container, const Value& key);
/// A copy of `container` with `key` replaced (or, for a map, added): nothing is changed in place.
[[nodiscard]] Value SetIndex(const Value& container, const Value& key, const Value& value);
/// Pops a tuple and pushes its items in order; an error unless it holds exactly `count` items.
void Unpack(std::vector<Value>& stack, std::size_t count);

/// Formats and joins the last `count` values of `stack`, removing them.
[[nodiscard]] Value Concat(std::vector<Value>& stack, std::size_t count);
/// A built-in (`sqrt`, `print`, ...) by its compiler id.
[[nodiscard]] Value CallBuiltin(UVScriptHost& host, int builtin, std::span<const Value> args);

} // namespace UVE::Blueprint::Native
