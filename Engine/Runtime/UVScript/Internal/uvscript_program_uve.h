// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// The bytecode UVScript compiles to. Internal: only the compiler writes it and only the instance
// runs it. A stack machine - every instruction pops its operands and pushes its result.

#include <cstdint>
#include <string>
#include <vector>

#include "uve/uvscript/uvscript_compiler_uve.h"

namespace UVE::UVScript {

enum class OpUVE : std::uint8_t {
    PushConst,    // a = constant index
    Pop,
    LoadLocal,    // a = slot
    StoreLocal,   // a = slot (pops)
    LoadField,    // a = field index
    StoreField,   // a = field index (pops)
    LoadProp,     // a = name constant
    StoreProp,    // a = name constant (pops)
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Neg,
    Not,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    ToFloat,      // int -> float
    GetComponent, // a = 0 x, 1 y, 2 z: vec3 -> float
    SetComponent, // a = component: [vec3, float] -> vec3
    Concat,       // a = count: formats and joins that many values into one string
    Jump,         // a = target
    JumpIfFalse,  // a = target (pops)
    JumpIfFalseKeep, // a = target; pops only when it does not jump (for `and`)
    JumpIfTrueKeep,  // a = target; pops only when it does not jump (for `or`)
    Call,         // a = function index, b = argument count
    CallBuiltin,  // a = builtin, b = argument count
    CallHost,     // a = name constant, b = argument count
    Return,       // pops the result
    ReturnNone,
    Wait,         // pops seconds
};

struct InstructionUVE final {
    OpUVE op = OpUVE::Pop;
    std::int32_t a = 0;
    std::int32_t b = 0;
    std::uint32_t line = 0U;
};

enum class BuiltinUVE : std::uint8_t {
    Print,
    Sqrt,
    Abs,
    Min,
    Max,
    Clamp,
    Lerp,
    Sin,
    Cos,
    Floor,
    Vec3,
    Int,
    Float,
    Str,
    Length,
    Normalize,
};

struct ChunkUVE final {
    std::string name;
    std::uint32_t paramCount = 0U;
    std::uint32_t localCount = 0U;
    std::vector<InstructionUVE> code;
    TypeUVE result;
};

struct ProgramUVE final {
    std::vector<ValueUVE> constants;
    std::vector<FieldInfoUVE> fields;
    /// Runs every field initializer; the instance calls it once.
    ChunkUVE init;
    std::vector<ChunkUVE> functions;
    /// Index into `functions` of each handler, by event name.
    std::vector<std::pair<std::string, std::uint32_t>> handlers;
    /// A hash of everything above: two programs with the same fingerprint run the same code, which
    /// is how generated native code finds the program it was generated from.
    std::uint64_t fingerprint = 0U;
};

[[nodiscard]] std::uint64_t ComputeProgramFingerprintUVE(const ProgramUVE& program);

} // namespace UVE::UVScript
