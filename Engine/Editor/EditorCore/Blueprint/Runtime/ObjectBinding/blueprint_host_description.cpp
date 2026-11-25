// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_host_description.h"

#include <cctype>
#include <string>
#include <utility>

namespace UVE::Blueprint {
namespace {

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] bool IsName(const std::string_view text) noexcept {
    if (text.empty() || std::isdigit(static_cast<unsigned char>(text.front())) != 0) {
        return false;
    }
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_' && c != '.') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<Type> ParseType(const std::string_view word) {
    if (word == "bool") return Type::Bool();
    if (word == "int") return Type::Int();
    if (word == "float") return Type::Float();
    if (word == "str") return Type::Str();
    if (word == "vec3") return Type::Vec3();
    if (word == "none") return Type::None();
    if (IsName(word) && word.find('.') == std::string_view::npos) return Type::Object(std::string{word});
    return std::nullopt;
}

/// "name(t, t)" into the name and the types.
[[nodiscard]] std::optional<std::pair<std::string, std::vector<Type>>> ParseSignature(std::string_view text) {
    const std::size_t open = text.find('(');
    const std::size_t close = text.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open ||
        !Trim(text.substr(close + 1U)).empty()) {
        return std::nullopt;
    }
    const std::string_view name = Trim(text.substr(0U, open));
    if (!IsName(name)) {
        return std::nullopt;
    }
    std::vector<Type> types;
    std::string_view list = Trim(text.substr(open + 1U, close - open - 1U));
    while (!list.empty()) {
        const std::size_t comma = list.find(',');
        const std::optional<Type> type = ParseType(Trim(list.substr(0U, comma)));
        if (!type.has_value()) {
            return std::nullopt;
        }
        types.push_back(*type);
        list = comma == std::string_view::npos ? std::string_view{} : Trim(list.substr(comma + 1U));
    }
    return std::pair{std::string{name}, std::move(types)};
}

} // namespace

std::optional<DescribedHost> DescribedHost::Parse(const std::string_view text, std::string& error) {
    DescribedHost host;
    std::size_t lineNumber = 0U;
    std::size_t start = 0U;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find('\n', start), text.size());
        ++lineNumber;
        std::string_view line = text.substr(start, end - start);
        start = end + 1U;
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0U, hash);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        const auto fail = [&](const std::string& why) {
            error = "line " + std::to_string(lineNumber) + ": " + why;
            return std::nullopt;
        };
        const std::size_t space = line.find(' ');
        const std::string_view keyword = line.substr(0U, space);
        const std::string_view rest = space == std::string_view::npos ? std::string_view{} : Trim(line.substr(space));
        if (keyword == "property") {
            // property <name> <type> [readonly]
            std::vector<std::string_view> words;
            std::string_view remaining = rest;
            while (!remaining.empty()) {
                const std::size_t gap = remaining.find(' ');
                words.push_back(remaining.substr(0U, gap));
                remaining = gap == std::string_view::npos ? std::string_view{} : Trim(remaining.substr(gap));
            }
            const bool readonly = words.size() == 3U && words[2] == "readonly";
            if ((words.size() != 2U && !readonly) || !IsName(words[0])) {
                return fail("expected 'property <name> <type> [readonly]'");
            }
            const std::optional<Type> type = ParseType(words[1]);
            if (!type.has_value()) {
                return fail("unknown type '" + std::string{words[1]} + "'");
            }
            host.m_properties.insert_or_assign(std::string{words[0]}, HostProperty{*type, !readonly});
        } else if (keyword == "function") {
            // function <name>(<types>) -> <type>
            const std::size_t arrow = rest.find("->");
            const auto signature = ParseSignature(rest.substr(0U, arrow));
            const std::optional<Type> result =
                arrow == std::string_view::npos ? std::nullopt : ParseType(Trim(rest.substr(arrow + 2U)));
            if (!signature.has_value() || !result.has_value()) {
                return fail("expected 'function <name>(<types>) -> <type>'");
            }
            host.m_functions.insert_or_assign(signature->first, HostFunction{signature->second, *result});
        } else if (keyword == "event") {
            const auto signature = ParseSignature(rest);
            if (!signature.has_value()) {
                return fail("expected 'event <name>(<types>)'");
            }
            host.m_events.insert_or_assign(signature->first, signature->second);
        } else {
            return fail("expected 'property', 'function' or 'event'");
        }
    }
    return host;
}

std::optional<HostProperty> DescribedHost::DescribeProperty(const std::string_view name) const {
    const auto it = m_properties.find(name);
    return it == m_properties.end() ? std::nullopt : std::optional{it->second};
}

std::optional<HostFunction> DescribedHost::DescribeFunction(const std::string_view name) const {
    const auto it = m_functions.find(name);
    return it == m_functions.end() ? std::nullopt : std::optional{it->second};
}

std::optional<std::vector<Type>> DescribedHost::DescribeEvent(const std::string_view event) const {
    const auto it = m_events.find(event);
    return it == m_events.end() ? std::nullopt : std::optional{it->second};
}

Value DescribedHost::GetProperty(std::string_view /*name*/) { return {}; }
void DescribedHost::SetProperty(std::string_view /*name*/, const Value& /*value*/) {}
Value DescribedHost::CallFunction(std::string_view /*name*/, std::span<const Value> /*args*/) { return {}; }
void DescribedHost::CallMethod(ObjectRef /*target*/, std::string_view /*method*/,
                                     std::span<const Value> /*args*/) {}
void DescribedHost::Print(std::string_view /*text*/) {}

} // namespace UVE::Blueprint
