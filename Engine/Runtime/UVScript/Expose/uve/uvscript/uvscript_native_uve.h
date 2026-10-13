// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// The runtime that C++ generated from UVScript (see uvscript_codegen_uve.h) is compiled against.
// Generated code is a translation of a program's bytecode into straight C++ - no instruction fetch,
// no dispatch loop - that calls the same value operations the interpreter uses, so a script behaves
// the same whichever way it runs. Nothing here is meant to be called by hand.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "uve/uvscript/uvscript_host_uve.h"
#include "uve/uvscript/uvscript_value_uve.h"

namespace UVE::UVScript::Native {

/// Stops the running handler or function with a message ("division by zero").
struct ErrorUVE final {
    std::string message;
};

/// One handler or function in progress. A handler that reaches `wait` returns Waiting with
/// `resume` set, and is called again later with the same frame to carry on from there.
struct FrameUVE final {
    std::vector<ValueUVE> locals;
    std::vector<ValueUVE> stack;
    std::uint32_t resume = 0U;
    double waitSeconds = 0.0;
    ValueUVE result;
};

enum class StatusUVE : std::uint8_t {
    Finished,
    Waiting,
};

/// What a running chunk may touch: the instance's fields and the object's host. It also carries the
/// instruction budget and the current source line, for the same errors the interpreter reports.
struct ContextUVE final {
    std::vector<ValueUVE>& fields;
    UVScriptHostUVE& host;
    std::size_t budget = 0U;
    std::size_t depth = 0U;
    std::uint32_t line = 0U;

    /// Called before every translated instruction.
    void StepUVE(const std::uint32_t sourceLine) {
        line = sourceLine;
        if (budget-- == 0U) {
            throw ErrorUVE{"this ran too long without stopping - is a loop missing its exit?"};
        }
    }
};

using ChunkFunctionUVE = StatusUVE (*)(ContextUVE& context, FrameUVE& frame);

/// A whole program in native form. `chunks[i]` is the program's function i (handlers included) and
/// `chunks[initIndex]` runs the field initializers.
struct ProgramTableUVE final {
    std::uint64_t fingerprint = 0U;
    std::span<const ChunkFunctionUVE> chunks;
    std::size_t initIndex = 0U;
};

/// Makes a native program available. Generated code does this from a static object, so linking a
/// generated file into the game is all it takes. A later table with the same fingerprint wins.
void RegisterNativeProgramUVE(const ProgramTableUVE& table);
/// The native form of the program with this fingerprint, or null (it then runs interpreted).
[[nodiscard]] const ProgramTableUVE* FindNativeProgramUVE(std::uint64_t fingerprint) noexcept;

/// Registers a table when constructed; one static instance per generated file.
struct RegistrationUVE final {
    explicit RegistrationUVE(const ProgramTableUVE& table) { RegisterNativeProgramUVE(table); }
};

// ---- Value operations, shared with the interpreter. Each throws ErrorUVE on a run-time error.

enum class ArithmeticOpUVE : std::uint8_t {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
};

enum class CompareOpUVE : std::uint8_t {
    Lt,
    Le,
    Gt,
    Ge,
};

[[nodiscard]] double AsDoubleUVE(const ValueUVE& value);
[[nodiscard]] Vec3ValueUVE AsVec3UVE(const ValueUVE& value);
[[nodiscard]] bool AsBoolUVE(const ValueUVE& value);
[[nodiscard]] ObjectRefUVE AsObjectRefUVE(const ValueUVE& value);
[[nodiscard]] const std::string& AsStringUVE(const ValueUVE& value);
[[nodiscard]] ValueUVE ArithmeticUVE(ArithmeticOpUVE op, const ValueUVE& a, const ValueUVE& b);
[[nodiscard]] ValueUVE NegateUVE(ValueUVE value);
[[nodiscard]] bool EqualUVE(const ValueUVE& a, const ValueUVE& b);
[[nodiscard]] bool CompareUVE(CompareOpUVE op, const ValueUVE& a, const ValueUVE& b);
[[nodiscard]] ValueUVE GetComponentUVE(const ValueUVE& vector, int component);
[[nodiscard]] ValueUVE SetComponentUVE(const ValueUVE& vector, int component, const ValueUVE& value);
[[nodiscard]] inline ValueUVE PopUVE(std::vector<ValueUVE>& stack) {
    ValueUVE value = std::move(stack.back());
    stack.pop_back();
    return value;
}

/// Calls another function of the same program (`fn` never waits) with `frame` holding its
/// arguments and locals; the result is left in `frame.result`.
inline void CallChunkUVE(ContextUVE& context, const ChunkFunctionUVE callee, FrameUVE& frame) {
    // The interpreter counts the caller's frame too, so it refuses at the same depth.
    if (context.depth + 1U >= 256U) {
        throw ErrorUVE{"functions call each other too deeply"};
    }
    ++context.depth;
    const StatusUVE status = callee(context, frame);
    --context.depth;
    if (status != StatusUVE::Finished) {
        throw ErrorUVE{"a function cannot wait"};
    }
}

/// The collection inside `value`, or an error when it holds anything else.
[[nodiscard]] const CollectionValueUVE& AsCollectionUVE(const ValueUVE& value);
/// Builds a collection from the last values of `stack`, removing them: `count` items for a list
/// or tuple, key, value, ... (`pairs` pairs) for a map, where a repeated key keeps the last value.
[[nodiscard]] ValueUVE BuildListUVE(std::vector<ValueUVE>& stack, std::size_t count);
[[nodiscard]] ValueUVE BuildMapUVE(std::vector<ValueUVE>& stack, std::size_t pairs);
[[nodiscard]] ValueUVE BuildTupleUVE(std::vector<ValueUVE>& stack, std::size_t count);
/// Reads `container[key]`: an int item of a list or tuple, or a map's value for the key.
[[nodiscard]] ValueUVE GetIndexUVE(const ValueUVE& container, const ValueUVE& key);
/// A copy of `container` with `key` replaced (or, for a map, added): nothing is changed in place.
[[nodiscard]] ValueUVE SetIndexUVE(const ValueUVE& container, const ValueUVE& key, const ValueUVE& value);
/// Pops a tuple and pushes its items in order; an error unless it holds exactly `count` items.
void UnpackUVE(std::vector<ValueUVE>& stack, std::size_t count);

/// Formats and joins the last `count` values of `stack`, removing them.
[[nodiscard]] ValueUVE ConcatUVE(std::vector<ValueUVE>& stack, std::size_t count);
/// A built-in (`sqrt`, `print`, ...) by its compiler id.
[[nodiscard]] ValueUVE CallBuiltinUVE(UVScriptHostUVE& host, int builtin, std::span<const ValueUVE> args);

} // namespace UVE::UVScript::Native
