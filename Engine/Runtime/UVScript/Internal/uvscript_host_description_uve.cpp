// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/uvscript/uvscript_host_description_uve.h"

#include <cctype>
#include <string>
#include <utility>

namespace UVE::UVScript {
namespace {

[[nodiscard]] std::string_view TrimUVE(std::string_view text) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] bool IsNameUVE(const std::string_view text) noexcept {
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

[[nodiscard]] std::optional<TypeUVE> ParseTypeUVE(const std::string_view word) {
    if (word == "bool") return TypeUVE::BoolUVE();
    if (word == "int") return TypeUVE::IntUVE();
    if (word == "float") return TypeUVE::FloatUVE();
    if (word == "str") return TypeUVE::StrUVE();
    if (word == "vec3") return TypeUVE::Vec3UVE();
    if (word == "none") return TypeUVE::NoneUVE();
    if (IsNameUVE(word) && word.find('.') == std::string_view::npos) return TypeUVE::ObjectUVE(std::string{word});
    return std::nullopt;
}

/// "name(t, t)" into the name and the types.
[[nodiscard]] std::optional<std::pair<std::string, std::vector<TypeUVE>>> ParseSignatureUVE(std::string_view text) {
    const std::size_t open = text.find('(');
    const std::size_t close = text.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open ||
        !TrimUVE(text.substr(close + 1U)).empty()) {
        return std::nullopt;
    }
    const std::string_view name = TrimUVE(text.substr(0U, open));
    if (!IsNameUVE(name)) {
        return std::nullopt;
    }
    std::vector<TypeUVE> types;
    std::string_view list = TrimUVE(text.substr(open + 1U, close - open - 1U));
    while (!list.empty()) {
        const std::size_t comma = list.find(',');
        const std::optional<TypeUVE> type = ParseTypeUVE(TrimUVE(list.substr(0U, comma)));
        if (!type.has_value()) {
            return std::nullopt;
        }
        types.push_back(*type);
        list = comma == std::string_view::npos ? std::string_view{} : TrimUVE(list.substr(comma + 1U));
    }
    return std::pair{std::string{name}, std::move(types)};
}

} // namespace

std::optional<DescribedHostUVE> DescribedHostUVE::ParseUVE(const std::string_view text, std::string& error) {
    DescribedHostUVE host;
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
        line = TrimUVE(line);
        if (line.empty()) {
            continue;
        }
        const auto fail = [&](const std::string& why) {
            error = "line " + std::to_string(lineNumber) + ": " + why;
            return std::nullopt;
        };
        const std::size_t space = line.find(' ');
        const std::string_view keyword = line.substr(0U, space);
        const std::string_view rest = space == std::string_view::npos ? std::string_view{} : TrimUVE(line.substr(space));
        if (keyword == "property") {
            // property <name> <type> [readonly]
            std::vector<std::string_view> words;
            std::string_view remaining = rest;
            while (!remaining.empty()) {
                const std::size_t gap = remaining.find(' ');
                words.push_back(remaining.substr(0U, gap));
                remaining = gap == std::string_view::npos ? std::string_view{} : TrimUVE(remaining.substr(gap));
            }
            const bool readonly = words.size() == 3U && words[2] == "readonly";
            if ((words.size() != 2U && !readonly) || !IsNameUVE(words[0])) {
                return fail("expected 'property <name> <type> [readonly]'");
            }
            const std::optional<TypeUVE> type = ParseTypeUVE(words[1]);
            if (!type.has_value()) {
                return fail("unknown type '" + std::string{words[1]} + "'");
            }
            host.m_properties.insert_or_assign(std::string{words[0]}, HostPropertyUVE{*type, !readonly});
        } else if (keyword == "function") {
            // function <name>(<types>) -> <type>
            const std::size_t arrow = rest.find("->");
            const auto signature = ParseSignatureUVE(rest.substr(0U, arrow));
            const std::optional<TypeUVE> result =
                arrow == std::string_view::npos ? std::nullopt : ParseTypeUVE(TrimUVE(rest.substr(arrow + 2U)));
            if (!signature.has_value() || !result.has_value()) {
                return fail("expected 'function <name>(<types>) -> <type>'");
            }
            host.m_functions.insert_or_assign(signature->first, HostFunctionUVE{signature->second, *result});
        } else if (keyword == "event") {
            const auto signature = ParseSignatureUVE(rest);
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

std::optional<HostPropertyUVE> DescribedHostUVE::DescribePropertyUVE(const std::string_view name) const {
    const auto it = m_properties.find(name);
    return it == m_properties.end() ? std::nullopt : std::optional{it->second};
}

std::optional<HostFunctionUVE> DescribedHostUVE::DescribeFunctionUVE(const std::string_view name) const {
    const auto it = m_functions.find(name);
    return it == m_functions.end() ? std::nullopt : std::optional{it->second};
}

std::optional<std::vector<TypeUVE>> DescribedHostUVE::DescribeEventUVE(const std::string_view event) const {
    const auto it = m_events.find(event);
    return it == m_events.end() ? std::nullopt : std::optional{it->second};
}

ValueUVE DescribedHostUVE::GetPropertyUVE(std::string_view /*name*/) { return {}; }
void DescribedHostUVE::SetPropertyUVE(std::string_view /*name*/, const ValueUVE& /*value*/) {}
ValueUVE DescribedHostUVE::CallFunctionUVE(std::string_view /*name*/, std::span<const ValueUVE> /*args*/) { return {}; }
void DescribedHostUVE::CallMethodUVE(ObjectRefUVE /*target*/, std::string_view /*method*/,
                                     std::span<const ValueUVE> /*args*/) {}
void DescribedHostUVE::PrintUVE(std::string_view /*text*/) {}

} // namespace UVE::UVScript
