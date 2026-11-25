// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_value.h"

#include <charconv>
#include <cmath>
#include <system_error>
#include <cstdio>
#include <string>
#include <type_traits>

namespace UVE::Blueprint {
namespace {

[[nodiscard]] std::string FormatNumber(const double value) {
    if (std::isfinite(value) && value == std::floor(value) && std::fabs(value) < 1e15) {
        return std::to_string(static_cast<long long>(value)) + ".0";
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    return buffer;
}

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
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
[[nodiscard]] std::optional<T> ParseNumber(std::string_view text) {
    text = Trim(text);
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

std::optional<Value> ParseValueText(const std::string_view text, const Type& type) {
    switch (type.kind) {
        case Type::Kind::Bool: {
            const std::string_view word = Trim(text);
            if (word == "true" || word == "false") {
                return Value{word == "true"};
            }
            return std::nullopt;
        }
        case Type::Kind::Int:
            if (const auto value = ParseNumber<std::int64_t>(text)) {
                return Value{*value};
            }
            return std::nullopt;
        case Type::Kind::Float:
            if (const auto value = ParseNumber<double>(text)) {
                return Value{*value};
            }
            return std::nullopt;
        case Type::Kind::Str:
            return Value{std::string{text}};
        case Type::Kind::Vec3: {
            std::string_view inner = Trim(text);
            if (inner.starts_with('(') && inner.ends_with(')')) {
                inner = inner.substr(1U, inner.size() - 2U);
            }
            double parts[3]{};
            for (std::size_t index = 0U; index < 3U; ++index) {
                const std::size_t comma = index < 2U ? inner.find(',') : std::string_view::npos;
                if (index < 2U && comma == std::string_view::npos) {
                    return std::nullopt;
                }
                const auto part = ParseNumber<double>(inner.substr(0U, comma));
                if (!part.has_value()) {
                    return std::nullopt;
                }
                parts[index] = *part;
                inner = index < 2U ? inner.substr(comma + 1U) : std::string_view{};
            }
            return Value{Vec3Value{parts[0], parts[1], parts[2]}};
        }
        case Type::Kind::None:
        case Type::Kind::Object:
        case Type::Kind::List:
        case Type::Kind::Map:
        case Type::Kind::Tuple:
        case Type::Kind::Error:
            break;
    }
    return std::nullopt;
}

std::string Type::Name() const {
    switch (kind) {
        case Kind::None: return "none";
        case Kind::Bool: return "bool";
        case Kind::Int: return "int";
        case Kind::Float: return "float";
        case Kind::Str: return "str";
        case Kind::Vec3: return "vec3";
        case Kind::Object: return object.empty() ? std::string{"Object"} : object;
        case Kind::List: return elements.empty() ? "list" : "list[" + elements[0].Name() + "]";
        case Kind::Map:
            if (elements.size() < 2U) {
                return "map";
            }
            return "map[" + elements[0].Name() + ", " + elements[1].Name() + "]";
        case Kind::Tuple: {
            std::string text = "(";
            for (std::size_t i = 0U; i < elements.size(); ++i) {
                text += (i == 0U ? "" : ", ") + elements[i].Name();
            }
            return text + (elements.size() == 1U ? ",)" : ")");
        }
        case Kind::Error: return "?";
    }
    return "?";
}

std::string FormatValue(const Value& value) {
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
                return FormatNumber(v);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return v;
            } else if constexpr (std::is_same_v<T, Vec3Value>) {
                return "(" + FormatNumber(v.x) + ", " + FormatNumber(v.y) + ", " + FormatNumber(v.z) + ")";
            } else if constexpr (std::is_same_v<T, ObjectRef>) {
                return "object#" + std::to_string(v.id);
            } else {
                const CollectionValue& box = *v;
                const auto join = [](const std::vector<Value>& items, const std::size_t begin,
                                     const std::size_t step) {
                    std::string text;
                    for (std::size_t i = begin; i < items.size(); i += step) {
                        text += (i == begin ? "" : ", ") + FormatValue(items[i]);
                    }
                    return text;
                };
                if (box.kind == CollectionValue::Kind::Map) {
                    std::string text = "{";
                    for (std::size_t i = 0U; i + 1U < box.items.size(); i += 2U) {
                        text += (i == 0U ? "" : ", ") + FormatValue(box.items[i]) + ": " +
                                FormatValue(box.items[i + 1U]);
                    }
                    return text + "}";
                }
                const char bracket = box.kind == CollectionValue::Kind::List ? '[' : '(';
                const std::string close = box.kind == CollectionValue::Kind::List ? "]"
                                          : box.items.size() == 1U                                   ? ",)"
                                                                                                     : ")";
                return bracket + join(box.items, 0U, 1U) + close;
            }
        },
        value);
}

} // namespace UVE::Blueprint
