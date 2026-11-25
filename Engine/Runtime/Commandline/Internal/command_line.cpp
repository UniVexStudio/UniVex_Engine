// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "command_line.h"

#include <cstddef>

namespace UVE::CommandLine {

namespace {
constexpr std::string_view kFlagPrefix = "--";
} // namespace

CommandLine::CommandLine(std::vector<std::string> args) {
    Parse(args);
}

CommandLine::CommandLine(int argc, char** argv) {
    std::vector<std::string> args;
    if (argc > 1) {
        args.reserve(static_cast<std::size_t>(argc - 1));
        for (int index = 1; index < argc; ++index) {
            args.emplace_back(argv[index]);
        }
    }
    Parse(args);
}

void CommandLine::Parse(const std::vector<std::string>& args) {
    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::string& token = args[index];
        if (token.compare(0, kFlagPrefix.size(), kFlagPrefix) != 0) {
            continue; // bare token not preceded by a recognized flag - ignored
        }

        const std::string normalizedName = token.substr(kFlagPrefix.size());
        if (normalizedName.empty()) {
            continue; // bare "--" with no name - not a valid flag
        }

        const bool hasNextToken = (index + 1) < args.size();
        const bool nextTokenIsFlag =
            hasNextToken && args[index + 1].compare(0, kFlagPrefix.size(), kFlagPrefix) == 0;

        if (hasNextToken && !nextTokenIsFlag) {
            m_flags[normalizedName] = args[index + 1];
            ++index; // consume the value token
        } else {
            m_flags[normalizedName] = std::nullopt;
        }
    }
}

bool CommandLine::HasFlag(std::string_view name) const noexcept {
    return m_flags.find(std::string(name)) != m_flags.end();
}

std::string CommandLine::GetValue(std::string_view name, std::string_view defaultValue) const {
    const std::optional<std::string> value = GetOptionalValue(name);
    return value.has_value() ? *value : std::string(defaultValue);
}

std::optional<std::string> CommandLine::GetOptionalValue(std::string_view name) const {
    const auto flagIt = m_flags.find(std::string(name));
    return flagIt != m_flags.end() ? flagIt->second : std::nullopt;
}

} // namespace UVE::CommandLine
