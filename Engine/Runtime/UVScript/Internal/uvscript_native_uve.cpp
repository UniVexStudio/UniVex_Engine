// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The value operations every UVScript program runs on, interpreted or native, and the registry of
// native programs.

#include "uve/uvscript/uvscript_native_uve.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "uvscript_program_uve.h"

namespace UVE::UVScript::Native {
namespace {

[[nodiscard]] bool IsNumberUVE(const ValueUVE& v) noexcept {
    return std::holds_alternative<std::int64_t>(v) || std::holds_alternative<double>(v);
}

struct RegistryUVE final {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, ProgramTableUVE> tables;
};

/// Function-local, so registrations from static objects in other files never run before it exists.
[[nodiscard]] RegistryUVE& GetRegistryUVE() {
    static RegistryUVE registry;
    return registry;
}

} // namespace

void RegisterNativeProgramUVE(const ProgramTableUVE& table) {
    RegistryUVE& registry = GetRegistryUVE();
    const std::scoped_lock lock(registry.mutex);
    registry.tables.insert_or_assign(table.fingerprint, table);
}

const ProgramTableUVE* FindNativeProgramUVE(const std::uint64_t fingerprint) noexcept {
    RegistryUVE& registry = GetRegistryUVE();
    const std::scoped_lock lock(registry.mutex);
    const auto it = registry.tables.find(fingerprint);
    return it == registry.tables.end() ? nullptr : &it->second;
}

double AsDoubleUVE(const ValueUVE& value) {
    if (const auto* i = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*i);
    }
    if (const auto* d = std::get_if<double>(&value)) {
        return *d;
    }
    throw ErrorUVE{"expected a number"};
}

Vec3ValueUVE AsVec3UVE(const ValueUVE& value) {
    if (const auto* vec = std::get_if<Vec3ValueUVE>(&value)) {
        return *vec;
    }
    throw ErrorUVE{"expected a vec3"};
}

bool AsBoolUVE(const ValueUVE& value) {
    if (const auto* b = std::get_if<bool>(&value)) {
        return *b;
    }
    throw ErrorUVE{"a value had an unexpected type"};
}

const std::string& AsStringUVE(const ValueUVE& value) {
    if (const auto* s = std::get_if<std::string>(&value)) {
        return *s;
    }
    throw ErrorUVE{"a value had an unexpected type"};
}

ValueUVE ArithmeticUVE(const ArithmeticOpUVE op, const ValueUVE& a, const ValueUVE& b) {
    const auto* ia = std::get_if<std::int64_t>(&a);
    const auto* ib = std::get_if<std::int64_t>(&b);
    if (ia != nullptr && ib != nullptr) {
        switch (op) {
            case ArithmeticOpUVE::Add: return *ia + *ib;
            case ArithmeticOpUVE::Sub: return *ia - *ib;
            case ArithmeticOpUVE::Mul: return *ia * *ib;
            case ArithmeticOpUVE::Mod:
                if (*ib == 0) {
                    throw ErrorUVE{"'%' by zero"};
                }
                return *ia % *ib;
            case ArithmeticOpUVE::Div:
                if (*ib == 0) {
                    throw ErrorUVE{"division by zero"};
                }
                return static_cast<double>(*ia) / static_cast<double>(*ib);
        }
    }
    if (IsNumberUVE(a) && IsNumberUVE(b)) {
        const double x = AsDoubleUVE(a);
        const double y = AsDoubleUVE(b);
        switch (op) {
            case ArithmeticOpUVE::Add: return x + y;
            case ArithmeticOpUVE::Sub: return x - y;
            case ArithmeticOpUVE::Mul: return x * y;
            case ArithmeticOpUVE::Div:
                if (y == 0.0) {
                    throw ErrorUVE{"division by zero"};
                }
                return x / y;
            case ArithmeticOpUVE::Mod:
                if (y == 0.0) {
                    throw ErrorUVE{"'%' by zero"};
                }
                return std::fmod(x, y);
        }
    }
    if (const auto* sa = std::get_if<std::string>(&a); sa != nullptr && op == ArithmeticOpUVE::Add) {
        return *sa + AsStringUVE(b);
    }
    if (std::holds_alternative<Vec3ValueUVE>(a) && std::holds_alternative<Vec3ValueUVE>(b)) {
        const Vec3ValueUVE u = AsVec3UVE(a);
        const Vec3ValueUVE v = AsVec3UVE(b);
        return op == ArithmeticOpUVE::Add ? Vec3ValueUVE{u.x + v.x, u.y + v.y, u.z + v.z}
                                          : Vec3ValueUVE{u.x - v.x, u.y - v.y, u.z - v.z};
    }
    if (std::holds_alternative<Vec3ValueUVE>(a)) {
        const Vec3ValueUVE u = AsVec3UVE(a);
        const double s = AsDoubleUVE(b);
        if (op == ArithmeticOpUVE::Div && s == 0.0) {
            throw ErrorUVE{"division by zero"};
        }
        return op == ArithmeticOpUVE::Mul ? Vec3ValueUVE{u.x * s, u.y * s, u.z * s}
                                          : Vec3ValueUVE{u.x / s, u.y / s, u.z / s};
    }
    const double s = AsDoubleUVE(a);
    const Vec3ValueUVE v = AsVec3UVE(b);
    return Vec3ValueUVE{v.x * s, v.y * s, v.z * s};
}

ValueUVE NegateUVE(ValueUVE value) {
    if (auto* i = std::get_if<std::int64_t>(&value)) {
        *i = -*i;
    } else if (auto* d = std::get_if<double>(&value)) {
        *d = -*d;
    } else if (auto* vec = std::get_if<Vec3ValueUVE>(&value)) {
        *vec = {-vec->x, -vec->y, -vec->z};
    }
    return value;
}

bool EqualUVE(const ValueUVE& a, const ValueUVE& b) {
    if (IsNumberUVE(a) && IsNumberUVE(b)) {
        return AsDoubleUVE(a) == AsDoubleUVE(b);
    }
    return a == b;
}

bool CompareUVE(const CompareOpUVE op, const ValueUVE& a, const ValueUVE& b) {
    const double x = AsDoubleUVE(a);
    const double y = AsDoubleUVE(b);
    switch (op) {
        case CompareOpUVE::Lt: return x < y;
        case CompareOpUVE::Le: return x <= y;
        case CompareOpUVE::Gt: return x > y;
        case CompareOpUVE::Ge: return x >= y;
    }
    return false;
}

ValueUVE GetComponentUVE(const ValueUVE& vector, const int component) {
    const Vec3ValueUVE v = AsVec3UVE(vector);
    return component == 0 ? v.x : component == 1 ? v.y : v.z;
}

ValueUVE SetComponentUVE(const ValueUVE& vector, const int component, const ValueUVE& value) {
    Vec3ValueUVE v = AsVec3UVE(vector);
    (component == 0 ? v.x : component == 1 ? v.y : v.z) = AsDoubleUVE(value);
    return v;
}

ValueUVE ConcatUVE(std::vector<ValueUVE>& stack, const std::size_t count) {
    std::string text;
    for (std::size_t i = stack.size() - count; i < stack.size(); ++i) {
        text += FormatValueUVE(stack[i]);
    }
    stack.resize(stack.size() - count);
    return text;
}

ValueUVE CallBuiltinUVE(UVScriptHostUVE& host, const int builtin, const std::span<const ValueUVE> a) {
    const bool allInt = std::ranges::all_of(a, [](const ValueUVE& v) { return std::holds_alternative<std::int64_t>(v); });
    const auto integer = [&a](const std::size_t i) { return std::get<std::int64_t>(a[i]); };
    switch (static_cast<BuiltinUVE>(builtin)) {
        case BuiltinUVE::Print: host.PrintUVE(FormatValueUVE(a[0])); return {};
        case BuiltinUVE::Str: return FormatValueUVE(a[0]);
        case BuiltinUVE::Sqrt: {
            const double x = AsDoubleUVE(a[0]);
            if (x < 0.0) {
                throw ErrorUVE{"sqrt of a negative number"};
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
            if (allInt) return std::abs(integer(0));
            return std::fabs(AsDoubleUVE(a[0]));
        case BuiltinUVE::Min:
            if (allInt) return std::min(integer(0), integer(1));
            return std::min(AsDoubleUVE(a[0]), AsDoubleUVE(a[1]));
        case BuiltinUVE::Max:
            if (allInt) return std::max(integer(0), integer(1));
            return std::max(AsDoubleUVE(a[0]), AsDoubleUVE(a[1]));
        case BuiltinUVE::Clamp:
            if (allInt) return std::clamp(integer(0), integer(1), std::max(integer(1), integer(2)));
            return std::clamp(AsDoubleUVE(a[0]), AsDoubleUVE(a[1]), std::max(AsDoubleUVE(a[1]), AsDoubleUVE(a[2])));
        case BuiltinUVE::Vec3: return Vec3ValueUVE{AsDoubleUVE(a[0]), AsDoubleUVE(a[1]), AsDoubleUVE(a[2])};
        case BuiltinUVE::Length: {
            const Vec3ValueUVE v = AsVec3UVE(a[0]);
            return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        }
        case BuiltinUVE::Normalize: {
            const Vec3ValueUVE v = AsVec3UVE(a[0]);
            const double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            return length == 0.0 ? v : Vec3ValueUVE{v.x / length, v.y / length, v.z / length};
        }
    }
    return {};
}

} // namespace UVE::UVScript::Native
