// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "developer_console.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <new>
#include <utility>

namespace UVE::Editor {
namespace {

void IncrementGeneration(std::uint64_t& generation) noexcept {
    if (generation < std::numeric_limits<std::uint64_t>::max()) {
        ++generation;
    }
}

[[nodiscard]] bool MatchesSeverity(const DeveloperConsoleSeverity severity,
                                      const DeveloperConsoleSeverityFilter filter) noexcept {
    switch (filter) {
        case DeveloperConsoleSeverityFilter::All:
            return true;
        case DeveloperConsoleSeverityFilter::Info:
            return severity == DeveloperConsoleSeverity::Info;
        case DeveloperConsoleSeverityFilter::Warning:
            return severity == DeveloperConsoleSeverity::Warning;
        case DeveloperConsoleSeverityFilter::Error:
            return severity == DeveloperConsoleSeverity::Error;
    }
    return false;
}

} // namespace

DeveloperConsole::DeveloperConsole(const DeveloperConsoleBuildPolicy policy)
    : m_policy(policy), m_access(policy == DeveloperConsoleBuildPolicy::Shipping ? DeveloperConsoleAccess::Denied
                                                                                     : DeveloperConsoleAccess::Full) {
    RegisterBuiltIns();
}

bool DeveloperConsole::IsBoundedIdentifier(const std::string_view value) noexcept {
    if (value.empty() || value.size() > kMaximumIdentifierBytes) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_' || character == '.' || character == '-';
    });
}

bool DeveloperConsole::IsBoundedValue(const std::string_view value) noexcept {
    return value.size() <= kMaximumValueBytes &&
           std::all_of(value.begin(), value.end(), [](const char character) { return character != '\n' && character != '\r'; });
}

bool DeveloperConsole::IsBoundedPrincipal(const std::string_view value) noexcept {
    return !value.empty() && value.size() <= kMaximumPrincipalBytes &&
           std::all_of(value.begin(), value.end(), [](const char character) {
               return std::iscntrl(static_cast<unsigned char>(character)) == 0;
           });
}

std::string DeveloperConsole::Trim(const std::string_view value) {
    std::size_t first = 0U;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1U])) != 0) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

bool DeveloperConsole::IsAvailable() const noexcept {
    return m_policy == DeveloperConsoleBuildPolicy::Development;
}

bool DeveloperConsole::SetAuditPrincipal(DeveloperConsolePrincipal principal) {
    if (!IsBoundedPrincipal(principal.subject) || !IsBoundedPrincipal(principal.session)) {
        return false;
    }
    try {
        m_auditPrincipal = std::move(principal);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

void DeveloperConsole::SetAuditSink(DeveloperConsoleAuditSink sink) noexcept {
    m_auditSink = std::move(sink);
}

DeveloperConsolePrincipal DeveloperConsole::GetAuditPrincipal() const {
    return m_auditPrincipal;
}

void DeveloperConsole::EmitAudit(const DeveloperConsoleAuditAction action,
                                       const std::string_view target,
                                       const std::string_view detail,
                                       const bool accepted) noexcept {
    if (!m_auditSink) {
        return;
    }
    try {
        const std::size_t boundedTargetSize = std::min(target.size(), kMaximumValueBytes);
        const std::size_t boundedDetailSize = std::min(detail.size(), kMaximumValueBytes);
        const std::uint64_t sequence = m_auditSequence < std::numeric_limits<std::uint64_t>::max()
                                           ? ++m_auditSequence
                                           : m_auditSequence;
        const DeveloperConsoleAuditRecord record{
            sequence,
            action,
            m_auditPrincipal,
            std::string(target.substr(0U, boundedTargetSize)),
            std::string(detail.substr(0U, boundedDetailSize)),
            accepted};
        m_auditSink(record);
    } catch (...) {
        // Caller-owned auditing must never change console behavior.
    }
}

DeveloperConsoleCommandRegistrationResult DeveloperConsole::RegisterCommandDetailed(
    std::string identifier, std::string help, DeveloperConsoleCommandHandler handler) {
    if (!IsAvailable()) {
        return {DeveloperConsoleCommandRegistrationCode::Unavailable,
                "Command registration is unavailable under the Shipping build policy."};
    }
    if (m_access != DeveloperConsoleAccess::Full) {
        return {DeveloperConsoleCommandRegistrationCode::Unauthorized,
                "Command registration requires Full console authorization."};
    }
    if (!IsBoundedIdentifier(identifier)) {
        return {DeveloperConsoleCommandRegistrationCode::InvalidIdentifier,
                "Command identifier is empty or contains unsupported characters."};
    }
    if (help.empty() || !IsBoundedValue(help)) {
        return {DeveloperConsoleCommandRegistrationCode::InvalidHelp,
                "Command help must be non-empty, bounded, and free of line breaks."};
    }
    if (!handler) {
        return {DeveloperConsoleCommandRegistrationCode::MissingHandler,
                "Command registration requires a callable handler."};
    }
    if (m_commands.size() >= kMaximumCommands) {
        return {DeveloperConsoleCommandRegistrationCode::CapacityExceeded,
                "The bounded command registry has reached its maximum capacity."};
    }
    if (m_commands.contains(identifier)) {
        return {DeveloperConsoleCommandRegistrationCode::DuplicateIdentifier,
                "A command with this identifier is already registered."};
    }

    m_commands.emplace(std::move(identifier), Command{std::move(help), std::move(handler)});
    IncrementGeneration(m_generation);
    return {DeveloperConsoleCommandRegistrationCode::Accepted, "Command registered."};
}

bool DeveloperConsole::RegisterCommand(std::string identifier, std::string help,
                                           DeveloperConsoleCommandHandler handler) {
    return RegisterCommandDetailed(std::move(identifier), std::move(help), std::move(handler)).IsAccepted();
}

DeveloperConsoleCVarRegistrationResult DeveloperConsole::RegisterCVarDetailed(std::string name, std::string value,
                                                                                const bool readOnly) {
    if (!IsAvailable()) {
        return {DeveloperConsoleCVarRegistrationCode::Unavailable,
                "CVAR registration is unavailable under the Shipping build policy."};
    }
    if (m_access != DeveloperConsoleAccess::Full) {
        return {DeveloperConsoleCVarRegistrationCode::Unauthorized,
                "CVAR registration requires Full console authorization."};
    }
    if (!IsBoundedIdentifier(name)) {
        return {DeveloperConsoleCVarRegistrationCode::InvalidName,
                "CVAR name is empty or contains unsupported characters."};
    }
    if (!IsBoundedValue(value)) {
        return {DeveloperConsoleCVarRegistrationCode::InvalidValue,
                "CVAR value exceeds the bounded size or contains a line break."};
    }
    if (m_cvars.size() >= kMaximumCVars) {
        return {DeveloperConsoleCVarRegistrationCode::CapacityExceeded,
                "The bounded CVAR registry has reached its maximum capacity."};
    }
    if (m_cvars.contains(name)) {
        return {DeveloperConsoleCVarRegistrationCode::DuplicateName,
                "A CVAR with this name is already registered."};
    }

    const std::string key = name;
    m_cvars.emplace(key, DeveloperConsoleCVar{key, std::move(value), readOnly});
    IncrementGeneration(m_generation);
    return {DeveloperConsoleCVarRegistrationCode::Accepted, "CVAR registered."};
}

bool DeveloperConsole::RegisterCVar(std::string name, std::string value, const bool readOnly) {
    return RegisterCVarDetailed(std::move(name), std::move(value), readOnly).IsAccepted();
}

void DeveloperConsole::RegisterBuiltIns() {
    (void)RegisterCommand("help", "List registered native commands.", [](DeveloperConsole& console, std::string_view) {
        for (const auto& [identifier, command] : console.m_commands) {
            console.Append(DeveloperConsoleSeverity::Info, identifier + " - " + command.help);
        }
    });
    (void)RegisterCommand("clear", "Clear console output while retaining command history.", [](DeveloperConsole& console, std::string_view) {
        (void)console.Clear();
    });
    (void)RegisterCommand("cvar", "Read or set a registered cvar: cvar <name> [value].", [](DeveloperConsole& console, const std::string_view arguments) {
        (void)console.ExecuteCVar(arguments);
    });
}

void DeveloperConsole::AddHistory(std::string value) {
    if (m_history.size() >= kMaximumHistory) {
        m_history.erase(m_history.begin());
        m_historyTruncated = true;
    }
    m_history.push_back(std::move(value));
    m_historyCursor = -1;
}

DeveloperConsoleExecutionResult DeveloperConsole::ExecuteDetailed(std::string commandLine) {
    if (!IsAvailable()) {
        EmitAudit(DeveloperConsoleAuditAction::CommandExecution, "", "Shipping policy unavailable", false);
        return {DeveloperConsoleExecutionCode::Unavailable,
                "Developer Console execution is unavailable under the Shipping build policy."};
    }
    if (m_access != DeveloperConsoleAccess::Full) {
        EmitAudit(DeveloperConsoleAuditAction::CommandExecution, "", "Full console authorization required", false);
        return {DeveloperConsoleExecutionCode::Unauthorized,
                "Developer Console execution requires Full console authorization."};
    }
    commandLine = Trim(commandLine);
    if (commandLine.empty() || commandLine.size() > kMaximumValueBytes ||
        commandLine.find_first_of("\r\n") != std::string::npos) {
        EmitAudit(DeveloperConsoleAuditAction::CommandExecution, commandLine, "Invalid command input", false);
        return {DeveloperConsoleExecutionCode::InvalidInput,
                "Command input must be non-empty, bounded, and free of line breaks."};
    }

    const std::size_t separator = commandLine.find_first_of(" \t");
    const std::string identifier = commandLine.substr(0U, separator);
    const auto iterator = m_commands.find(identifier);
    if (iterator == m_commands.end()) {
        AddHistory(commandLine);
        Append(DeveloperConsoleSeverity::Error, "Unknown console command: " + identifier);
        EmitAudit(DeveloperConsoleAuditAction::CommandExecution, identifier, "Unknown command", false);
        return {DeveloperConsoleExecutionCode::UnknownCommand, "No command is registered with this identifier."};
    }

    const std::string arguments = separator == std::string::npos ? std::string{} : Trim(commandLine.substr(separator));
    AddHistory(commandLine);
    iterator->second.handler(*this, arguments);
    IncrementGeneration(m_generation);
    EmitAudit(DeveloperConsoleAuditAction::CommandExecution, identifier, "Command executed", true);
    return {DeveloperConsoleExecutionCode::Executed, "Command executed."};
}

bool DeveloperConsole::Execute(std::string commandLine) {
    return ExecuteDetailed(std::move(commandLine)).IsAccepted();
}

DeveloperConsoleClearResult DeveloperConsole::ClearDetailed() noexcept {
    if (!IsAvailable()) {
        return {DeveloperConsoleClearCode::Unavailable,
                "Console clearing is unavailable under the Shipping build policy."};
    }
    if (m_access != DeveloperConsoleAccess::Full) {
        return {DeveloperConsoleClearCode::Unauthorized,
                "Console clearing requires Full console authorization."};
    }
    if (m_output.empty()) {
        return {DeveloperConsoleClearCode::Unchanged, "Console output is already clear."};
    }
    m_output.clear();
    m_outputTruncated = false;
    IncrementGeneration(m_generation);
    return {DeveloperConsoleClearCode::Applied, "Console output cleared."};
}

bool DeveloperConsole::Clear() noexcept {
    return ClearDetailed().IsAccepted();
}

DeveloperConsoleCVarMutationResult DeveloperConsole::SetCVarDetailed(const std::string_view name,
                                                                                const std::string_view value) {
    if (!IsAvailable()) {
        EmitAudit(DeveloperConsoleAuditAction::CVarMutation, name, "Shipping policy unavailable", false);
        return {DeveloperConsoleCVarMutationCode::Unavailable,
                "CVAR mutation is unavailable under the Shipping build policy."};
    }
    if (m_access != DeveloperConsoleAccess::Full) {
        EmitAudit(DeveloperConsoleAuditAction::CVarMutation, name, "Full console authorization required", false);
        return {DeveloperConsoleCVarMutationCode::Unauthorized,
                "CVAR mutation requires Full console authorization."};
    }
    const auto iterator = m_cvars.find(name);
    if (iterator == m_cvars.end()) {
        EmitAudit(DeveloperConsoleAuditAction::CVarMutation, name, "Unknown CVAR", false);
        return {DeveloperConsoleCVarMutationCode::UnknownName,
                "No CVAR is registered with this name."};
    }
    if (iterator->second.readOnly) {
        EmitAudit(DeveloperConsoleAuditAction::CVarMutation, name, "CVAR is read-only", false);
        return {DeveloperConsoleCVarMutationCode::ReadOnly,
                "The registered CVAR is read-only."};
    }
    if (!IsBoundedValue(value)) {
        EmitAudit(DeveloperConsoleAuditAction::CVarMutation, name, "Invalid CVAR value", false);
        return {DeveloperConsoleCVarMutationCode::InvalidValue,
                "CVAR value exceeds the bounded size or contains a line break."};
    }
    if (iterator->second.value == value) {
        EmitAudit(DeveloperConsoleAuditAction::CVarMutation, name, "CVAR unchanged", true);
        return {DeveloperConsoleCVarMutationCode::Unchanged, "CVAR value is unchanged."};
    }
    iterator->second.value = std::string(value);
    Append(DeveloperConsoleSeverity::Info, std::string(name) + " = " + std::string(value));
    EmitAudit(DeveloperConsoleAuditAction::CVarMutation, name, "CVAR value updated", true);
    return {DeveloperConsoleCVarMutationCode::Applied, "CVAR value updated."};
}

bool DeveloperConsole::SetCVar(const std::string_view name, const std::string_view value) {
    return SetCVarDetailed(name, value).IsAccepted();
}

bool DeveloperConsole::ExecuteCVar(const std::string_view arguments) {
    const std::string trimmed = Trim(arguments);
    const std::size_t separator = trimmed.find_first_of(" \t");
    const std::string name = trimmed.substr(0U, separator);
    const auto iterator = m_cvars.find(name);
    if (iterator == m_cvars.end()) {
        Append(DeveloperConsoleSeverity::Error, "Unknown cvar: " + name);
        return false;
    }
    if (separator == std::string::npos) {
        Append(DeveloperConsoleSeverity::Info, iterator->second.name + " = " + iterator->second.value);
        return true;
    }
    return SetCVar(name, Trim(trimmed.substr(separator)));
}

DeveloperConsoleBuildPolicyResult DeveloperConsole::SetBuildPolicyDetailed(
    const DeveloperConsoleBuildPolicy policy) noexcept {
    if (m_policy == policy) {
        EmitAudit(DeveloperConsoleAuditAction::BuildPolicyChange, "build_policy", "Build policy unchanged", true);
        return {DeveloperConsoleBuildPolicyCode::Unchanged, "Build policy is unchanged."};
    }
    if (m_policy == DeveloperConsoleBuildPolicy::Shipping &&
        policy == DeveloperConsoleBuildPolicy::Development) {
        EmitAudit(DeveloperConsoleAuditAction::BuildPolicyChange, "build_policy", "Shipping policy is monotonic", false);
        return {DeveloperConsoleBuildPolicyCode::Locked,
                "Shipping build policy is monotonic and cannot be relaxed at runtime."};
    }
    m_policy = policy;
    if (policy == DeveloperConsoleBuildPolicy::Shipping) {
        m_access = DeveloperConsoleAccess::Denied;
    }
    m_completionPrefix.clear();
    m_historyCursor = -1;
    IncrementGeneration(m_generation);
    EmitAudit(DeveloperConsoleAuditAction::BuildPolicyChange, "build_policy", "Build policy updated", true);
    return {DeveloperConsoleBuildPolicyCode::Applied, "Build policy updated."};
}

bool DeveloperConsole::SetBuildPolicy(const DeveloperConsoleBuildPolicy policy) noexcept {
    return SetBuildPolicyDetailed(policy).IsAccepted();
}

DeveloperConsoleAuthorizationResult DeveloperConsole::SetAccessDetailed(
    const DeveloperConsoleAccess access) noexcept {
    if (!IsAvailable()) {
        EmitAudit(DeveloperConsoleAuditAction::AccessChange, "console_access", "Shipping policy unavailable", false);
        return {DeveloperConsoleAuthorizationCode::Unavailable,
                "Console authorization is unavailable under the Shipping build policy."};
    }
    if (static_cast<std::uint8_t>(access) > static_cast<std::uint8_t>(DeveloperConsoleAccess::Full)) {
        EmitAudit(DeveloperConsoleAuditAction::AccessChange, "console_access", "Invalid access value", false);
        return {DeveloperConsoleAuthorizationCode::InvalidAccess,
                "Console authorization value is outside the supported range."};
    }
    if (m_access == access) {
        EmitAudit(DeveloperConsoleAuditAction::AccessChange, "console_access", "Access unchanged", true);
        return {DeveloperConsoleAuthorizationCode::Unchanged, "Console authorization is unchanged."};
    }
    m_access = access;
    IncrementGeneration(m_generation);
    EmitAudit(DeveloperConsoleAuditAction::AccessChange, "console_access", "Access updated", true);
    return {DeveloperConsoleAuthorizationCode::Applied, "Console authorization updated."};
}

bool DeveloperConsole::SetAccess(const DeveloperConsoleAccess access) noexcept {
    return SetAccessDetailed(access).IsAccepted();
}

DeveloperConsoleSeverityFilterResult DeveloperConsole::SetSeverityFilterDetailed(
    const DeveloperConsoleSeverityFilter filter) noexcept {
    if (!IsAvailable()) {
        return {DeveloperConsoleSeverityFilterCode::Unavailable,
                "Severity filtering is unavailable under the Shipping build policy."};
    }
    if (static_cast<std::uint8_t>(filter) > static_cast<std::uint8_t>(DeveloperConsoleSeverityFilter::Error)) {
        return {DeveloperConsoleSeverityFilterCode::InvalidFilter,
                "Severity filter value is outside the supported filter range."};
    }
    if (m_severityFilter == filter) {
        return {DeveloperConsoleSeverityFilterCode::Unchanged, "Severity filter is unchanged."};
    }
    m_severityFilter = filter;
    IncrementGeneration(m_generation);
    return {DeveloperConsoleSeverityFilterCode::Applied, "Severity filter updated."};
}

bool DeveloperConsole::SetSeverityFilter(const DeveloperConsoleSeverityFilter filter) noexcept {
    return SetSeverityFilterDetailed(filter).IsAccepted();
}

DeveloperConsoleCompletionPrefixResult DeveloperConsole::SetCompletionPrefixDetailed(std::string prefix) {
    if (!IsAvailable()) {
        return {DeveloperConsoleCompletionPrefixCode::Unavailable,
                "Completion prefix filtering is unavailable under the Shipping build policy."};
    }
    if (!IsBoundedValue(prefix)) {
        return {DeveloperConsoleCompletionPrefixCode::InvalidPrefix,
                "Completion prefix exceeds the bounded size or contains a line break."};
    }
    prefix = Trim(prefix);
    if (m_completionPrefix == prefix) {
        return {DeveloperConsoleCompletionPrefixCode::Unchanged, "Completion prefix is unchanged."};
    }
    m_completionPrefix = std::move(prefix);
    IncrementGeneration(m_generation);
    return {DeveloperConsoleCompletionPrefixCode::Applied, "Completion prefix updated."};
}

bool DeveloperConsole::SetCompletionPrefix(std::string prefix) {
    return SetCompletionPrefixDetailed(std::move(prefix)).IsAccepted();
}

DeveloperConsoleHistoryNavigationResult DeveloperConsole::MoveHistoryDetailed(
    const std::int32_t delta) noexcept {
    if (!IsAvailable()) {
        return {DeveloperConsoleHistoryNavigationCode::Unavailable,
                "History navigation is unavailable under the Shipping build policy."};
    }
    if (delta == 0) {
        return {DeveloperConsoleHistoryNavigationCode::InvalidDelta,
                "History navigation delta must be non-zero."};
    }
    if (m_history.empty()) {
        return {DeveloperConsoleHistoryNavigationCode::EmptyHistory,
                "History navigation requires at least one history entry."};
    }

    const std::int64_t current = m_historyCursor;
    std::int64_t next = current;
    if (current < 0) {
        next = delta < 0 ? static_cast<std::int64_t>(m_history.size() - 1U) : -1;
    } else if (delta > 0 && current == static_cast<std::int64_t>(m_history.size() - 1U)) {
        next = -1;
    } else {
        next += delta;
        next = std::clamp<std::int64_t>(next, 0, static_cast<std::int64_t>(m_history.size() - 1U));
    }
    if (next == current) {
        return {DeveloperConsoleHistoryNavigationCode::Boundary,
                "History navigation is already at the requested boundary."};
    }
    m_historyCursor = static_cast<std::int32_t>(next);
    IncrementGeneration(m_generation);
    return {DeveloperConsoleHistoryNavigationCode::Applied, "History cursor updated."};
}

bool DeveloperConsole::MoveHistory(const std::int32_t delta) noexcept {
    return MoveHistoryDetailed(delta).IsAccepted();
}

void DeveloperConsole::Append(const DeveloperConsoleSeverity severity, std::string text) {
    if (text.size() > kMaximumValueBytes) {
        text.resize(kMaximumValueBytes);
        m_outputTruncated = true;
    }
    if (m_output.size() >= kMaximumOutput) {
        m_output.erase(m_output.begin());
        m_outputTruncated = true;
    }
    m_output.push_back(DeveloperConsoleEntry{severity, std::move(text)});
    IncrementGeneration(m_generation);
}

DeveloperConsoleSnapshot DeveloperConsole::GetSnapshot() const {
    DeveloperConsoleSnapshot snapshot{};
    snapshot.generation = m_generation;
    snapshot.available = IsAvailable();
    snapshot.developmentOnly = true;
    snapshot.access = m_access;
    snapshot.severityFilter = m_severityFilter;
    snapshot.historyCursor = m_historyCursor;
    if (m_historyCursor >= 0 && static_cast<std::size_t>(m_historyCursor) < m_history.size()) {
        snapshot.historyEntry = m_history[static_cast<std::size_t>(m_historyCursor)];
    }
    snapshot.outputTruncated = m_outputTruncated;
    snapshot.historyTruncated = m_historyTruncated;
    snapshot.cvarsTruncated = m_cvarsTruncated;
    snapshot.completionTruncated = m_completionTruncated;
    for (const DeveloperConsoleEntry& entry : m_output) {
        if (MatchesSeverity(entry.severity, m_severityFilter)) {
            snapshot.output.push_back(entry);
        }
    }
    snapshot.history = m_history;
    for (const auto& [name, cvar] : m_cvars) {
        snapshot.cvars.push_back(cvar);
    }
    for (const auto& [identifier, command] : m_commands) {
        if (!m_completionPrefix.empty() && identifier.compare(0U, m_completionPrefix.size(), m_completionPrefix) != 0) {
            continue;
        }
        if (snapshot.completions.size() >= kMaximumCompletions) {
            snapshot.completionTruncated = true;
            break;
        }
        snapshot.completions.push_back(DeveloperConsoleCompletion{identifier, command.help});
    }
    return snapshot;
}

} // namespace UVE::Editor
