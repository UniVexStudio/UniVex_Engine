// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/uvscript/uvscript_value_uve.h"

#include <charconv>
#include <cmath>
#include <system_error>
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

[[nodiscard]] std::string_view TrimUVE(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1U);
    }
    return text;
}

/// The whole of `text` as a number, or nothing (empty, trailing junk, out of range, not finite).
template <typename T>
[[nodiscard]] std::optional<T> ParseNumberUVE(std::string_view text) {
    text = TrimUVE(text);
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(value)) {
            return std::nullopt;
        }
    }
    return value;
}

} // namespace

std::optional<ValueUVE> ParseValueTextUVE(const std::string_view text, const TypeUVE& type) {
    switch (type.kind) {
        case TypeUVE::KindUVE::Bool: {
            const std::string_view word = TrimUVE(text);
            if (word == "true" || word == "false") {
                return ValueUVE{word == "true"};
            }
            return std::nullopt;
        }
        case TypeUVE::KindUVE::Int:
            if (const auto value = ParseNumberUVE<std::int64_t>(text)) {
                return ValueUVE{*value};
            }
            return std::nullopt;
        case TypeUVE::KindUVE::Float:
            if (const auto value = ParseNumberUVE<double>(text)) {
                return ValueUVE{*value};
            }
            return std::nullopt;
        case TypeUVE::KindUVE::Str:
            return ValueUVE{std::string{text}};
        case TypeUVE::KindUVE::Vec3: {
            std::string_view inner = TrimUVE(text);
            if (inner.starts_with('(') && inner.ends_with(')')) {
                inner = inner.substr(1U, inner.size() - 2U);
            }
            double parts[3]{};
            for (std::size_t index = 0U; index < 3U; ++index) {
                const std::size_t comma = index < 2U ? inner.find(',') : std::string_view::npos;
                if (index < 2U && comma == std::string_view::npos) {
                    return std::nullopt;
                }
                const auto part = ParseNumberUVE<double>(inner.substr(0U, comma));
                if (!part.has_value()) {
                    return std::nullopt;
                }
                parts[index] = *part;
                inner = index < 2U ? inner.substr(comma + 1U) : std::string_view{};
            }
            return ValueUVE{Vec3ValueUVE{parts[0], parts[1], parts[2]}};
        }
        case TypeUVE::KindUVE::None:
        case TypeUVE::KindUVE::Object:
        case TypeUVE::KindUVE::Error:
            break;
    }
    return std::nullopt;
}

std::string TypeUVE::NameUVE() const {
    switch (kind) {
        case KindUVE::None: return "none";
        case KindUVE::Bool: return "bool";
        case KindUVE::Int: return "int";
        case KindUVE::Float: return "float";
        case KindUVE::Str: return "str";
        case KindUVE::Vec3: return "vec3";
        case KindUVE::Object: return object.empty() ? std::string{"Node"} : object;
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
