// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Editor {

class DeveloperConsole;
using DeveloperConsoleCommandHandler = std::function<void(DeveloperConsole&, std::string_view)>;

enum class DeveloperConsoleSeverity : std::uint8_t {
    Info = 0,
    Warning,
    Error,
};

enum class DeveloperConsoleSeverityFilter : std::uint8_t {
    All = 0,
    Info,
    Warning,
    Error,
};

enum class DeveloperConsoleBuildPolicy : std::uint8_t {
    Development = 0,
    Shipping,
};

enum class DeveloperConsoleAccess : std::uint8_t {
    Denied = 0,
    ReadOnly,
    Full,
};

enum class DeveloperConsoleAuthorizationCode : std::uint8_t {
    Applied = 0,
    Unchanged,
    Unavailable,
    InvalidAccess,
};

struct DeveloperConsoleAuthorizationResult final {
    DeveloperConsoleAuthorizationCode code = DeveloperConsoleAuthorizationCode::Applied;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == DeveloperConsoleAuthorizationCode::Applied ||
               code == DeveloperConsoleAuthorizationCode::Unchanged;
    }
};

enum class DeveloperConsoleCommandRegistrationCode : std::uint8_t {
    Accepted = 0,
    InvalidIdentifier,
    InvalidHelp,
    MissingHandler,
    Unauthorized,
    CapacityExceeded,
    DuplicateIdentifier,
    Unavailable,
};

struct DeveloperConsoleCommandRegistrationResult final {
    DeveloperConsoleCommandRegistrationCode code = DeveloperConsoleCommandRegistrationCode::Accepted;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == DeveloperConsoleCommandRegistrationCode::Accepted;
    }
};

enum class DeveloperConsoleCVarRegistrationCode : std::uint8_t {
    Accepted = 0,
    InvalidName,
    InvalidValue,
    Unauthorized,
    CapacityExceeded,
    DuplicateName,
    Unavailable,
};

struct DeveloperConsoleCVarRegistrationResult final {
    DeveloperConsoleCVarRegistrationCode code = DeveloperConsoleCVarRegistrationCode::Accepted;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == DeveloperConsoleCVarRegistrationCode::Accepted;
    }
};

enum class DeveloperConsoleCVarMutationCode : std::uint8_t {
    Applied = 0,
    Unchanged,
    Unavailable,
    UnknownName,
    ReadOnly,
    Unauthorized,
    InvalidValue,
};

struct DeveloperConsoleCVarMutationResult final {
    DeveloperConsoleCVarMutationCode code = DeveloperConsoleCVarMutationCode::Applied;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == DeveloperConsoleCVarMutationCode::Applied ||
               code == DeveloperConsoleCVarMutationCode::Unchanged;
    }
};

enum class DeveloperConsoleExecutionCode : std::uint8_t {
    Executed = 0,
    Unavailable,
    InvalidInput,
    UnknownCommand,
    Unauthorized,
};

struct DeveloperConsoleExecutionResult final {
    DeveloperConsoleExecutionCode code = DeveloperConsoleExecutionCode::Executed;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == DeveloperConsoleExecutionCode::Executed;
    }
};

enum class DeveloperConsoleSeverityFilterCode : std::uint8_t {
    Applied = 0,
    Unchanged,
    Unavailable,
    InvalidFilter,
};

struct DeveloperConsoleSeverityFilterResult final {
    DeveloperConsoleSeverityFilterCode code = DeveloperConsoleSeverityFilterCode::Applied;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == DeveloperConsoleSeverityFilterCode::Applied ||
               code == DeveloperConsoleSeverityFilterCode::Unchanged;
    }
};

enum class DeveloperConsoleCompletionPrefixCode : std::uint8_t {
    Applied = 0,
    Unchanged,
    Unavailable,
    InvalidPrefix,
};

struct DeveloperConsoleCompletionPrefixResult final {
    DeveloperConsoleCompletionPrefixCode code = DeveloperConsoleCompletionPrefixCode::Applied;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == DeveloperConsoleCompletionPrefixCode::Applied ||
               code == DeveloperConsoleCompletionPrefixCode::Unchanged;
    }
};

enum class DeveloperConsoleHistoryNavigationCode : std::uint8_t {
    Applied = 0,
    Unavailable,
    InvalidDelta,
    EmptyHistory,
    Boundary,
};

struct DeveloperConsoleHistoryNavigationResult final {
    DeveloperConsoleHistoryNavigationCode code = DeveloperConsoleHistoryNavigationCode::Applied;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == DeveloperConsoleHistoryNavigationCode::Applied;
    }
};

enum class DeveloperConsoleClearCode : std::uint8_t {
    Applied = 0,
    Unavailable,
    Unauthorized,
    Unchanged,
};

struct DeveloperConsoleClearResult final {
    DeveloperConsoleClearCode code = DeveloperConsoleClearCode::Applied;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == DeveloperConsoleClearCode::Applied;
    }
};

enum class DeveloperConsoleBuildPolicyCode : std::uint8_t {
    Applied = 0,
    Unchanged,
    Locked,
};

struct DeveloperConsoleBuildPolicyResult final {
    DeveloperConsoleBuildPolicyCode code = DeveloperConsoleBuildPolicyCode::Applied;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == DeveloperConsoleBuildPolicyCode::Applied;
    }
};

enum class DeveloperConsoleAuditAction : std::uint8_t {
    CommandExecution = 0,
    CVarMutation,
    AccessChange,
    BuildPolicyChange,
};

struct DeveloperConsolePrincipal final {
    std::string subject = "local";
    std::string session = "default";

    [[nodiscard]] bool operator==(const DeveloperConsolePrincipal&) const = default;
};

struct DeveloperConsoleAuditRecord final {
    std::uint64_t sequence = 0U;
    DeveloperConsoleAuditAction action = DeveloperConsoleAuditAction::CommandExecution;
    DeveloperConsolePrincipal principal;
    std::string target;
    std::string detail;
    bool accepted = false;

    [[nodiscard]] bool operator==(const DeveloperConsoleAuditRecord&) const = default;
};

using DeveloperConsoleAuditSink = std::function<void(const DeveloperConsoleAuditRecord&)>;

struct DeveloperConsoleCompletion final {
    std::string identifier;
    std::string help;
    [[nodiscard]] bool operator==(const DeveloperConsoleCompletion&) const = default;
};

struct DeveloperConsoleEntry final {
    DeveloperConsoleSeverity severity = DeveloperConsoleSeverity::Info;
    std::string text;
    [[nodiscard]] bool operator==(const DeveloperConsoleEntry&) const = default;
};

struct DeveloperConsoleCVar final {
    std::string name;
    std::string value;
    bool readOnly = false;
    [[nodiscard]] bool operator==(const DeveloperConsoleCVar&) const = default;
};

struct DeveloperConsoleSnapshot final {
    std::uint64_t generation = 0U;
    bool available = true;
    bool developmentOnly = true;
    DeveloperConsoleAccess access = DeveloperConsoleAccess::Full;
    DeveloperConsoleSeverityFilter severityFilter = DeveloperConsoleSeverityFilter::All;
    std::int32_t historyCursor = -1;
    std::string historyEntry;
    bool outputTruncated = false;
    bool historyTruncated = false;
    bool cvarsTruncated = false;
    bool completionTruncated = false;
    std::vector<DeveloperConsoleEntry> output;
    std::vector<std::string> history;
    std::vector<DeveloperConsoleCVar> cvars;
    std::vector<DeveloperConsoleCompletion> completions;
    [[nodiscard]] bool operator==(const DeveloperConsoleSnapshot&) const = default;
};

class DeveloperConsole final {
public:
    static constexpr std::size_t kMaximumCommands = 64U;
    static constexpr std::size_t kMaximumHistory = 128U;
    static constexpr std::size_t kMaximumOutput = 128U;
    static constexpr std::size_t kMaximumCVars = 128U;
    static constexpr std::size_t kMaximumCompletions = 128U;
    static constexpr std::size_t kMaximumIdentifierBytes = 64U;
    static constexpr std::size_t kMaximumValueBytes = 256U;
    static constexpr std::size_t kMaximumPrincipalBytes = 64U;

    explicit DeveloperConsole(DeveloperConsoleBuildPolicy policy = DeveloperConsoleBuildPolicy::Development);
    DeveloperConsole(const DeveloperConsole&) = delete;
    DeveloperConsole& operator=(const DeveloperConsole&) = delete;

    [[nodiscard]] DeveloperConsoleCommandRegistrationResult RegisterCommandDetailed(
        std::string identifier, std::string help, DeveloperConsoleCommandHandler handler);
    [[nodiscard]] bool RegisterCommand(std::string identifier, std::string help,
                                       DeveloperConsoleCommandHandler handler);
    [[nodiscard]] DeveloperConsoleCVarRegistrationResult RegisterCVarDetailed(
        std::string name, std::string value, bool readOnly = false);
    [[nodiscard]] bool RegisterCVar(std::string name, std::string value, bool readOnly = false);
    [[nodiscard]] DeveloperConsoleExecutionResult ExecuteDetailed(std::string commandLine);
    [[nodiscard]] bool Execute(std::string commandLine);
    [[nodiscard]] DeveloperConsoleClearResult ClearDetailed() noexcept;
    [[nodiscard]] bool Clear() noexcept;
    [[nodiscard]] DeveloperConsoleCVarMutationResult SetCVarDetailed(std::string_view name,
                                                                            std::string_view value);
    [[nodiscard]] bool SetCVar(std::string_view name, std::string_view value);
    [[nodiscard]] DeveloperConsoleBuildPolicyResult SetBuildPolicyDetailed(
        DeveloperConsoleBuildPolicy policy) noexcept;
    [[nodiscard]] bool SetBuildPolicy(DeveloperConsoleBuildPolicy policy) noexcept;
    [[nodiscard]] DeveloperConsoleAuthorizationResult SetAccessDetailed(
        DeveloperConsoleAccess access) noexcept;
    [[nodiscard]] bool SetAccess(DeveloperConsoleAccess access) noexcept;
    [[nodiscard]] DeveloperConsoleSeverityFilterResult SetSeverityFilterDetailed(
        DeveloperConsoleSeverityFilter filter) noexcept;
    [[nodiscard]] bool SetSeverityFilter(DeveloperConsoleSeverityFilter filter) noexcept;
    [[nodiscard]] DeveloperConsoleCompletionPrefixResult SetCompletionPrefixDetailed(std::string prefix);
    [[nodiscard]] bool SetCompletionPrefix(std::string prefix);
    [[nodiscard]] DeveloperConsoleHistoryNavigationResult MoveHistoryDetailed(std::int32_t delta) noexcept;
    [[nodiscard]] bool MoveHistory(std::int32_t delta) noexcept;
    [[nodiscard]] bool IsAvailable() const noexcept;
    [[nodiscard]] DeveloperConsoleSnapshot GetSnapshot() const;

    /// Sets a copied caller-owned principal/session label for subsequent audit records.
    /// This does not authenticate or resolve the principal.
    [[nodiscard]] bool SetAuditPrincipal(DeveloperConsolePrincipal principal);
    void SetAuditSink(DeveloperConsoleAuditSink sink) noexcept;
    [[nodiscard]] DeveloperConsolePrincipal GetAuditPrincipal() const;

    void Append(DeveloperConsoleSeverity severity, std::string text);

private:
    struct Command final {
        std::string help;
        DeveloperConsoleCommandHandler handler;
    };

    [[nodiscard]] static bool IsBoundedIdentifier(std::string_view value) noexcept;
    [[nodiscard]] static bool IsBoundedValue(std::string_view value) noexcept;
    [[nodiscard]] static bool IsBoundedPrincipal(std::string_view value) noexcept;
    [[nodiscard]] static std::string Trim(std::string_view value);
    void EmitAudit(DeveloperConsoleAuditAction action, std::string_view target,
                      std::string_view detail, bool accepted) noexcept;
    void RegisterBuiltIns();
    void AddHistory(std::string value);
    [[nodiscard]] bool ExecuteCVar(std::string_view arguments);

    std::map<std::string, Command, std::less<>> m_commands;
    std::map<std::string, DeveloperConsoleCVar, std::less<>> m_cvars;
    std::vector<std::string> m_history;
    std::vector<DeveloperConsoleEntry> m_output;
    DeveloperConsoleBuildPolicy m_policy = DeveloperConsoleBuildPolicy::Development;
    DeveloperConsoleAccess m_access = DeveloperConsoleAccess::Full;
    DeveloperConsoleSeverityFilter m_severityFilter = DeveloperConsoleSeverityFilter::All;
    std::string m_completionPrefix;
    std::int32_t m_historyCursor = -1;
    bool m_completionTruncated = false;
    bool m_outputTruncated = false;
    bool m_historyTruncated = false;
    bool m_cvarsTruncated = false;
    DeveloperConsolePrincipal m_auditPrincipal;
    DeveloperConsoleAuditSink m_auditSink;
    std::uint64_t m_generation = 1U;
    std::uint64_t m_auditSequence = 0U;
};

} // namespace UVE::Editor
