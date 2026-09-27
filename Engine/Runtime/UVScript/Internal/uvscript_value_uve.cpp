// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/uvscript/uvscript_value_uve.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <type_traits>

namespace UVE::UVScript {
namespace {

[[nodiscard]] std::string FormatNumberUVE(const double value) {
    if (std::isfinite(value) && value == std::floor(value) && std::fabs(value) < 1e15) {
        return std::to_string(static_cast<long long>(value)) + ".0";
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    return buffer;
}

} // namespace

std::string TypeUVE::NameUVE() const {
    switch (kind) {
        case KindUVE::None: return "none";
        case KindUVE::Bool: return "bool";
        case KindUVE::Int: return "int";
        case KindUVE::Float: return "float";
        case KindUVE::Str: return "str";
        case KindUVE::Vec3: return "vec3";
        case KindUVE::Node: return node.empty() ? std::string{"Node"} : node;
        case KindUVE::Error: return "?";
    }
    return "?";
}

std::string FormatValueUVE(const ValueUVE& value) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "none";
            } else if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return std::to_string(v);
            } else if constexpr (std::is_same_v<T, double>) {
                return FormatNumberUVE(v);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return v;
            } else if constexpr (std::is_same_v<T, Vec3ValueUVE>) {
                return "(" + FormatNumberUVE(v.x) + ", " + FormatNumberUVE(v.y) + ", " + FormatNumberUVE(v.z) + ")";
            } else {
                return "node#" + std::to_string(v.id);
            }
        },
        value);
}

} // namespace UVE::UVScript
