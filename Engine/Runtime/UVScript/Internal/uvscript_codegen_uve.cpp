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
#include <set>
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
    return std::visit(
        [](const auto& v) -> std::string {
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
            } else {
                return "ValueUVE{NodeRefUVE{" + std::to_string(v.id) + "U}}";
            }
        },
        value);
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

class GeneratorUVE final {
public:
    explicit GeneratorUVE(const ProgramUVE& program) : m_program(program) {}

    [[nodiscard]] std::string Run(const std::string_view origin) {
        const std::size_t initIndex = m_program.functions.size();
        m_out += "// Generated from " + std::string{origin} + " by uvsc. Do not edit: regenerate it.\n";
        m_out += "// Program fingerprint " + HexUVE(m_program.fingerprint) + ".\n\n";
        m_out += "#include <algorithm>\n#include <bit>\n#include <cstdint>\n#include <string>\n#include <vector>\n\n";
        m_out += "#include \"uve/uvscript/uvscript_native_uve.h\"\n\n";
        m_out += "namespace {\n\nusing namespace UVE::UVScript;\nusing namespace UVE::UVScript::Native;\n\n";
        if (!m_program.constants.empty()) {
            m_out += "const ValueUVE kConstants[] = {\n";
            for (const ValueUVE& constant : m_program.constants) {
                m_out += "    " + ValueLiteralUVE(constant) + ",\n";
            }
            m_out += "};\n\n";
        }
        for (std::size_t i = 0U; i <= initIndex; ++i) {
            m_out += "StatusUVE Chunk" + std::to_string(i) + "(ContextUVE& c, FrameUVE& f);\n";
        }
        m_out += "\n";
        for (std::size_t i = 0U; i < initIndex; ++i) {
            Chunk(m_program.functions[i], i);
        }
        Chunk(m_program.init, initIndex);
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

private:
    [[nodiscard]] std::string Name(const std::int32_t constant) const {
        return StringLiteralUVE(std::get<std::string>(m_program.constants[static_cast<std::size_t>(constant)]));
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
};

} // namespace

std::string GenerateUVScriptNativeCppUVE(const ProgramUVE& program, const std::string_view origin) {
    return GeneratorUVE(program).Run(origin);
}

} // namespace UVE::UVScript
