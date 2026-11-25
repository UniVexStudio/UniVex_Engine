// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "developer_console.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace UVE::Editor::Tests {

TEST(DeveloperConsole, BuiltInsAndRegisteredCVarAreDeterministic) {
    DeveloperConsole console;
    ASSERT_TRUE(console.RegisterCVar("r.vsync", "1"));

    ASSERT_TRUE(console.Execute("help"));
    ASSERT_TRUE(console.Execute("cvar r.vsync"));
    ASSERT_TRUE(console.Execute("cvar r.vsync 0"));

    const DeveloperConsoleSnapshot snapshot = console.GetSnapshot();
    ASSERT_GE(snapshot.output.size(), 4U);
    EXPECT_EQ(snapshot.cvars.size(), 1U);
    EXPECT_EQ(snapshot.cvars.front().name, "r.vsync");
    EXPECT_EQ(snapshot.cvars.front().value, "0");
    EXPECT_EQ(snapshot.history.size(), 3U);
    EXPECT_FALSE(snapshot.outputTruncated);
}

TEST(DeveloperConsole, ExplicitCommandsAndUnknownInputStayNativeAndBounded) {
    DeveloperConsole console;
    std::string receivedArguments;
    ASSERT_TRUE(console.RegisterCommand("echo", "Append a diagnostic echo.",
                                        [&receivedArguments](DeveloperConsole& target, const std::string_view arguments) {
                                            receivedArguments = std::string(arguments);
                                            target.Append(DeveloperConsoleSeverity::Info, receivedArguments);
                                        }));

    ASSERT_TRUE(console.Execute("echo hello"));
    EXPECT_EQ(receivedArguments, "hello");
    EXPECT_FALSE(console.Execute("notRegistered"));
    EXPECT_FALSE(console.Execute(std::string(DeveloperConsole::kMaximumValueBytes + 1U, 'x')));

    const DeveloperConsoleSnapshot snapshot = console.GetSnapshot();
    ASSERT_FALSE(snapshot.output.empty());
    EXPECT_EQ(snapshot.output.back().severity, DeveloperConsoleSeverity::Error);
    EXPECT_EQ(snapshot.history.size(), 2U);
}

TEST(DeveloperConsole, RegisterCVarDetailedReturnsStructuredDiagnosticsForEachRejectionCode) {
    DeveloperConsole console;

    const DeveloperConsoleCVarRegistrationResult invalidName = console.RegisterCVarDetailed("invalid name", "1");
    EXPECT_EQ(invalidName.code, DeveloperConsoleCVarRegistrationCode::InvalidName);
    EXPECT_FALSE(invalidName.IsAccepted());
    EXPECT_FALSE(invalidName.message.empty());

    const DeveloperConsoleCVarRegistrationResult invalidValue =
        console.RegisterCVarDetailed("r.invalid", std::string(DeveloperConsole::kMaximumValueBytes + 1U, 'x'));
    EXPECT_EQ(invalidValue.code, DeveloperConsoleCVarRegistrationCode::InvalidValue);
    EXPECT_FALSE(invalidValue.IsAccepted());
    EXPECT_FALSE(invalidValue.message.empty());

    const DeveloperConsoleCVarRegistrationResult accepted = console.RegisterCVarDetailed("r.runtime", "1", true);
    EXPECT_EQ(accepted.code, DeveloperConsoleCVarRegistrationCode::Accepted);
    EXPECT_TRUE(accepted.IsAccepted());
    EXPECT_FALSE(accepted.message.empty());

    const DeveloperConsoleCVarRegistrationResult duplicate = console.RegisterCVarDetailed("r.runtime", "2");
    EXPECT_EQ(duplicate.code, DeveloperConsoleCVarRegistrationCode::DuplicateName);
    EXPECT_FALSE(duplicate.IsAccepted());
    EXPECT_FALSE(duplicate.message.empty());

    for (std::size_t index = 0U; index < DeveloperConsole::kMaximumCVars - 1U; ++index) {
        ASSERT_TRUE(console.RegisterCVar("r.custom." + std::to_string(index), "0"));
    }
    const DeveloperConsoleCVarRegistrationResult capacity = console.RegisterCVarDetailed("r.capacity", "0");
    EXPECT_EQ(capacity.code, DeveloperConsoleCVarRegistrationCode::CapacityExceeded);
    EXPECT_FALSE(capacity.IsAccepted());
    EXPECT_FALSE(capacity.message.empty());
}

TEST(DeveloperConsole, SetCVarDetailedReturnsStructuredMutationDiagnostics) {
    DeveloperConsole console;
    ASSERT_TRUE(console.RegisterCVar("r.mutable", "1"));
    ASSERT_TRUE(console.RegisterCVar("r.locked", "1", true));

    const DeveloperConsoleCVarMutationResult unknown = console.SetCVarDetailed("r.unknown", "2");
    EXPECT_EQ(unknown.code, DeveloperConsoleCVarMutationCode::UnknownName);
    EXPECT_FALSE(unknown.IsAccepted());
    EXPECT_FALSE(unknown.message.empty());

    const DeveloperConsoleCVarMutationResult readOnly = console.SetCVarDetailed("r.locked", "2");
    EXPECT_EQ(readOnly.code, DeveloperConsoleCVarMutationCode::ReadOnly);
    EXPECT_FALSE(readOnly.IsAccepted());
    EXPECT_FALSE(readOnly.message.empty());

    const DeveloperConsoleCVarMutationResult invalidValue =
        console.SetCVarDetailed("r.mutable", "line\nbreak");
    EXPECT_EQ(invalidValue.code, DeveloperConsoleCVarMutationCode::InvalidValue);
    EXPECT_FALSE(invalidValue.IsAccepted());
    EXPECT_FALSE(invalidValue.message.empty());

    const DeveloperConsoleCVarMutationResult unchanged = console.SetCVarDetailed("r.mutable", "1");
    EXPECT_EQ(unchanged.code, DeveloperConsoleCVarMutationCode::Unchanged);
    EXPECT_TRUE(unchanged.IsAccepted());

    const DeveloperConsoleCVarMutationResult applied = console.SetCVarDetailed("r.mutable", "2");
    EXPECT_EQ(applied.code, DeveloperConsoleCVarMutationCode::Applied);
    EXPECT_TRUE(applied.IsAccepted());
    EXPECT_TRUE(console.SetCVar("r.mutable", "3"));
    const DeveloperConsoleSnapshot snapshot = console.GetSnapshot();
    const auto mutableCVar = std::find_if(snapshot.cvars.begin(), snapshot.cvars.end(), [](const DeveloperConsoleCVar& cvar) {
        return cvar.name == "r.mutable";
    });
    ASSERT_NE(mutableCVar, snapshot.cvars.end());
    EXPECT_EQ(mutableCVar->value, "3");

    DeveloperConsole shipping(DeveloperConsoleBuildPolicy::Shipping);
    const DeveloperConsoleCommandRegistrationResult commandUnavailable = shipping.RegisterCommandDetailed(
        "r.shipping.command", "Unavailable command.", [](DeveloperConsole&, std::string_view) {});
    EXPECT_EQ(commandUnavailable.code, DeveloperConsoleCommandRegistrationCode::Unavailable);
    EXPECT_FALSE(commandUnavailable.IsAccepted());
    const DeveloperConsoleCVarRegistrationResult cvarUnavailable =
        shipping.RegisterCVarDetailed("r.shipping", "1");
    EXPECT_EQ(cvarUnavailable.code, DeveloperConsoleCVarRegistrationCode::Unavailable);
    EXPECT_FALSE(cvarUnavailable.IsAccepted());
    const DeveloperConsoleCVarMutationResult unavailable = shipping.SetCVarDetailed("r.shipping", "2");
    EXPECT_EQ(unavailable.code, DeveloperConsoleCVarMutationCode::Unavailable);
    EXPECT_FALSE(unavailable.IsAccepted());
    EXPECT_FALSE(unavailable.message.empty());
}

TEST(DeveloperConsole, ExecuteDetailedReturnsStructuredDiagnosticsAndPreservesHistoryPolicy) {
    DeveloperConsole console;
    ASSERT_TRUE(console.RegisterCommand("echo", "Append a diagnostic echo.", [](DeveloperConsole& target, std::string_view arguments) {
        target.Append(DeveloperConsoleSeverity::Info, std::string(arguments));
    }));

    const DeveloperConsoleExecutionResult executed = console.ExecuteDetailed("echo hello");
    EXPECT_EQ(executed.code, DeveloperConsoleExecutionCode::Executed);
    EXPECT_TRUE(executed.IsAccepted());
    EXPECT_FALSE(executed.message.empty());
    EXPECT_TRUE(console.Execute("echo bool"));

    const DeveloperConsoleExecutionResult invalidInput = console.ExecuteDetailed("");
    EXPECT_EQ(invalidInput.code, DeveloperConsoleExecutionCode::InvalidInput);
    EXPECT_FALSE(invalidInput.IsAccepted());
    EXPECT_FALSE(invalidInput.message.empty());

    const DeveloperConsoleExecutionResult unknown = console.ExecuteDetailed("missing");
    EXPECT_EQ(unknown.code, DeveloperConsoleExecutionCode::UnknownCommand);
    EXPECT_FALSE(unknown.IsAccepted());
    EXPECT_FALSE(unknown.message.empty());
    EXPECT_EQ(console.GetSnapshot().history.size(), 3U);

    DeveloperConsole shipping(DeveloperConsoleBuildPolicy::Shipping);
    const DeveloperConsoleExecutionResult unavailable = shipping.ExecuteDetailed("help");
    EXPECT_EQ(unavailable.code, DeveloperConsoleExecutionCode::Unavailable);
    EXPECT_FALSE(unavailable.IsAccepted());
    EXPECT_FALSE(unavailable.message.empty());
    EXPECT_TRUE(shipping.GetSnapshot().history.empty());
}

TEST(DeveloperConsole, RegisterCommandDetailedReturnsStructuredDiagnosticsForEachRejectionCode) {
    DeveloperConsole console;
    const auto handler = [](DeveloperConsole&, std::string_view) {};

    const DeveloperConsoleCommandRegistrationResult invalidIdentifier =
        console.RegisterCommandDetailed("invalid identifier", "Help", handler);
    EXPECT_EQ(invalidIdentifier.code, DeveloperConsoleCommandRegistrationCode::InvalidIdentifier);
    EXPECT_FALSE(invalidIdentifier.IsAccepted());
    EXPECT_FALSE(invalidIdentifier.message.empty());

    const DeveloperConsoleCommandRegistrationResult invalidHelp = console.RegisterCommandDetailed("empty.help", "", handler);
    EXPECT_EQ(invalidHelp.code, DeveloperConsoleCommandRegistrationCode::InvalidHelp);
    EXPECT_FALSE(invalidHelp.IsAccepted());
    EXPECT_FALSE(invalidHelp.message.empty());

    const DeveloperConsoleCommandRegistrationResult missingHandler =
        console.RegisterCommandDetailed("missing.handler", "Help", DeveloperConsoleCommandHandler{});
    EXPECT_EQ(missingHandler.code, DeveloperConsoleCommandRegistrationCode::MissingHandler);
    EXPECT_FALSE(missingHandler.IsAccepted());
    EXPECT_FALSE(missingHandler.message.empty());

    const DeveloperConsoleCommandRegistrationResult accepted =
        console.RegisterCommandDetailed("runtime.inspect", "Inspect runtime state.", handler);
    EXPECT_EQ(accepted.code, DeveloperConsoleCommandRegistrationCode::Accepted);
    EXPECT_TRUE(accepted.IsAccepted());
    EXPECT_FALSE(accepted.message.empty());

    const DeveloperConsoleCommandRegistrationResult duplicate =
        console.RegisterCommandDetailed("runtime.inspect", "Inspect runtime state again.", handler);
    EXPECT_EQ(duplicate.code, DeveloperConsoleCommandRegistrationCode::DuplicateIdentifier);
    EXPECT_FALSE(duplicate.IsAccepted());
    EXPECT_FALSE(duplicate.message.empty());

    for (std::size_t index = 0U; index < DeveloperConsole::kMaximumCommands - 4U; ++index) {
        ASSERT_TRUE(console.RegisterCommand("custom." + std::to_string(index), "Custom command.", handler));
    }
    const DeveloperConsoleCommandRegistrationResult capacity =
        console.RegisterCommandDetailed("capacity.exceeded", "Capacity test.", handler);
    EXPECT_EQ(capacity.code, DeveloperConsoleCommandRegistrationCode::CapacityExceeded);
    EXPECT_FALSE(capacity.IsAccepted());
    EXPECT_FALSE(capacity.message.empty());
}

TEST(DeveloperConsole, SetSeverityFilterDetailedReturnsStructuredDiagnostics) {
    DeveloperConsole console;
    console.Append(DeveloperConsoleSeverity::Info, "info");
    console.Append(DeveloperConsoleSeverity::Warning, "warning");

    const DeveloperConsoleSeverityFilterResult applied =
        console.SetSeverityFilterDetailed(DeveloperConsoleSeverityFilter::Warning);
    EXPECT_EQ(applied.code, DeveloperConsoleSeverityFilterCode::Applied);
    EXPECT_TRUE(applied.IsAccepted());
    EXPECT_FALSE(applied.message.empty());

    const DeveloperConsoleSeverityFilterResult unchanged =
        console.SetSeverityFilterDetailed(DeveloperConsoleSeverityFilter::Warning);
    EXPECT_EQ(unchanged.code, DeveloperConsoleSeverityFilterCode::Unchanged);
    EXPECT_TRUE(unchanged.IsAccepted());

    const DeveloperConsoleSeverityFilterResult invalid = console.SetSeverityFilterDetailed(
        static_cast<DeveloperConsoleSeverityFilter>(255U));
    EXPECT_EQ(invalid.code, DeveloperConsoleSeverityFilterCode::InvalidFilter);
    EXPECT_FALSE(invalid.IsAccepted());
    EXPECT_FALSE(invalid.message.empty());
    ASSERT_TRUE(console.SetSeverityFilter(DeveloperConsoleSeverityFilter::Error));

    DeveloperConsole shipping(DeveloperConsoleBuildPolicy::Shipping);
    const DeveloperConsoleSeverityFilterResult unavailable =
        shipping.SetSeverityFilterDetailed(DeveloperConsoleSeverityFilter::Error);
    EXPECT_EQ(unavailable.code, DeveloperConsoleSeverityFilterCode::Unavailable);
    EXPECT_FALSE(unavailable.IsAccepted());
    EXPECT_FALSE(unavailable.message.empty());
}

TEST(DeveloperConsole, SetCompletionPrefixDetailedReturnsStructuredDiagnostics) {
    DeveloperConsole console;
    ASSERT_TRUE(console.RegisterCommand("camera.reset", "Reset the active camera.", [](DeveloperConsole&, std::string_view) {}));

    const DeveloperConsoleCompletionPrefixResult applied = console.SetCompletionPrefixDetailed(" camera ");
    EXPECT_EQ(applied.code, DeveloperConsoleCompletionPrefixCode::Applied);
    EXPECT_TRUE(applied.IsAccepted());
    EXPECT_FALSE(applied.message.empty());
    ASSERT_TRUE(console.SetCompletionPrefix("camera"));

    const DeveloperConsoleCompletionPrefixResult unchanged = console.SetCompletionPrefixDetailed("camera");
    EXPECT_EQ(unchanged.code, DeveloperConsoleCompletionPrefixCode::Unchanged);
    EXPECT_TRUE(unchanged.IsAccepted());

    const DeveloperConsoleCompletionPrefixResult invalid =
        console.SetCompletionPrefixDetailed(std::string(DeveloperConsole::kMaximumValueBytes + 1U, 'x'));
    EXPECT_EQ(invalid.code, DeveloperConsoleCompletionPrefixCode::InvalidPrefix);
    EXPECT_FALSE(invalid.IsAccepted());
    EXPECT_FALSE(invalid.message.empty());
    ASSERT_EQ(console.GetSnapshot().completions.size(), 1U);
    EXPECT_EQ(console.GetSnapshot().completions.front().identifier, "camera.reset");

    DeveloperConsole shipping(DeveloperConsoleBuildPolicy::Shipping);
    const DeveloperConsoleCompletionPrefixResult unavailable =
        shipping.SetCompletionPrefixDetailed("camera");
    EXPECT_EQ(unavailable.code, DeveloperConsoleCompletionPrefixCode::Unavailable);
    EXPECT_FALSE(unavailable.IsAccepted());
    EXPECT_FALSE(unavailable.message.empty());
}

TEST(DeveloperConsole, CompletionAndSeverityFilterAreNativeDeterministicFacts) {
    DeveloperConsole console;
    ASSERT_TRUE(console.RegisterCommand("camera.reset", "Reset the active camera.",
                                        [](DeveloperConsole& target, std::string_view) {
                                            target.Append(DeveloperConsoleSeverity::Info, "camera reset");
                                        }));
    console.Append(DeveloperConsoleSeverity::Info, "info");
    console.Append(DeveloperConsoleSeverity::Warning, "warning");
    console.Append(DeveloperConsoleSeverity::Error, "error");

    ASSERT_TRUE(console.SetCompletionPrefix("camera"));
    DeveloperConsoleSnapshot discovered = console.GetSnapshot();
    ASSERT_EQ(discovered.completions.size(), 1U);
    EXPECT_EQ(discovered.completions.front().identifier, "camera.reset");
    EXPECT_FALSE(discovered.completionTruncated);

    ASSERT_TRUE(console.SetSeverityFilter(DeveloperConsoleSeverityFilter::Error));
    const DeveloperConsoleSnapshot filtered = console.GetSnapshot();
    ASSERT_EQ(filtered.output.size(), 1U);
    EXPECT_EQ(filtered.output.front().severity, DeveloperConsoleSeverity::Error);
}

TEST(DeveloperConsole, SetAccessDetailed_EnforcesBoundedAuthorizationWithoutMutationBypass) {
    DeveloperConsole console;
    ASSERT_TRUE(console.RegisterCVarDetailed("r.runtime", "1").IsAccepted());

    const DeveloperConsoleAuthorizationResult readOnly =
        console.SetAccessDetailed(DeveloperConsoleAccess::ReadOnly);
    EXPECT_EQ(readOnly.code, DeveloperConsoleAuthorizationCode::Applied);
    EXPECT_TRUE(readOnly.IsAccepted());
    EXPECT_EQ(console.GetSnapshot().access, DeveloperConsoleAccess::ReadOnly);

    const DeveloperConsoleCommandRegistrationResult command =
        console.RegisterCommandDetailed("blocked.command", "Blocked command.", [](DeveloperConsole&, std::string_view) {});
    EXPECT_EQ(command.code, DeveloperConsoleCommandRegistrationCode::Unauthorized);
    EXPECT_EQ(console.GetSnapshot().completions.size(), 3U);
    EXPECT_EQ(console.ExecuteDetailed("help").code, DeveloperConsoleExecutionCode::Unauthorized);
    EXPECT_EQ(console.SetCVarDetailed("r.runtime", "2").code, DeveloperConsoleCVarMutationCode::Unauthorized);

    console.Append(DeveloperConsoleSeverity::Info, "retained");
    EXPECT_EQ(console.ClearDetailed().code, DeveloperConsoleClearCode::Unauthorized);
    EXPECT_EQ(console.GetSnapshot().output.size(), 1U);

    const DeveloperConsoleAuthorizationResult full = console.SetAccessDetailed(DeveloperConsoleAccess::Full);
    EXPECT_EQ(full.code, DeveloperConsoleAuthorizationCode::Applied);
    EXPECT_TRUE(console.RegisterCommand("allowed.command", "Allowed command.", [](DeveloperConsole&, std::string_view) {}));
    EXPECT_TRUE(console.Execute("allowed.command"));
    EXPECT_TRUE(console.SetCVar("r.runtime", "2"));

    const DeveloperConsoleAuthorizationResult invalid =
        console.SetAccessDetailed(static_cast<DeveloperConsoleAccess>(99U));
    EXPECT_EQ(invalid.code, DeveloperConsoleAuthorizationCode::InvalidAccess);
}

TEST(DeveloperConsole, SetAccessDetailed_ShippingRemainsPermanentlyDenied) {
    DeveloperConsole shipping(DeveloperConsoleBuildPolicy::Shipping);
    const DeveloperConsoleSnapshot initial = shipping.GetSnapshot();
    EXPECT_EQ(initial.access, DeveloperConsoleAccess::Denied);
    const DeveloperConsoleAuthorizationResult denied = shipping.SetAccessDetailed(DeveloperConsoleAccess::ReadOnly);
    EXPECT_EQ(denied.code, DeveloperConsoleAuthorizationCode::Unavailable);
    EXPECT_EQ(shipping.GetSnapshot().access, DeveloperConsoleAccess::Denied);
}

TEST(DeveloperConsole, SetBuildPolicyDetailedReturnsStructuredDiagnostics) {
    DeveloperConsole console;
    const DeveloperConsoleBuildPolicyResult unchanged =
        console.SetBuildPolicyDetailed(DeveloperConsoleBuildPolicy::Development);
    EXPECT_EQ(unchanged.code, DeveloperConsoleBuildPolicyCode::Unchanged);
    EXPECT_FALSE(unchanged.IsAccepted());
    EXPECT_FALSE(unchanged.message.empty());

    const DeveloperConsoleBuildPolicyResult applied =
        console.SetBuildPolicyDetailed(DeveloperConsoleBuildPolicy::Shipping);
    EXPECT_EQ(applied.code, DeveloperConsoleBuildPolicyCode::Applied);
    EXPECT_TRUE(applied.IsAccepted());
    EXPECT_FALSE(applied.message.empty());
    const DeveloperConsoleSnapshot shippingSnapshot = console.GetSnapshot();
    EXPECT_FALSE(shippingSnapshot.available);
    const DeveloperConsoleBuildPolicyResult locked =
        console.SetBuildPolicyDetailed(DeveloperConsoleBuildPolicy::Development);
    EXPECT_EQ(locked.code, DeveloperConsoleBuildPolicyCode::Locked);
    EXPECT_FALSE(locked.IsAccepted());
    EXPECT_FALSE(locked.message.empty());
    const DeveloperConsoleSnapshot afterRejectedDowngrade = console.GetSnapshot();
    EXPECT_FALSE(afterRejectedDowngrade.available);
    EXPECT_EQ(afterRejectedDowngrade.generation, shippingSnapshot.generation);
    EXPECT_FALSE(console.SetBuildPolicy(DeveloperConsoleBuildPolicy::Development));
}

TEST(DeveloperConsole, ShippingPolicy_ClosesRegistrationAndPreservesGenerationOnDowngrade) {
    DeveloperConsole shipping(DeveloperConsoleBuildPolicy::Shipping);
    const DeveloperConsoleSnapshot initial = shipping.GetSnapshot();
    EXPECT_FALSE(initial.available);
    EXPECT_TRUE(initial.developmentOnly);
    EXPECT_EQ(initial.access, DeveloperConsoleAccess::Denied);
    EXPECT_TRUE(initial.completions.empty());

    const DeveloperConsoleCommandRegistrationResult command = shipping.RegisterCommandDetailed(
        "late.command", "Late command.", [](DeveloperConsole&, std::string_view) {});
    EXPECT_EQ(command.code, DeveloperConsoleCommandRegistrationCode::Unavailable);
    const DeveloperConsoleCVarRegistrationResult cvar = shipping.RegisterCVarDetailed("late.cvar", "0");
    EXPECT_EQ(cvar.code, DeveloperConsoleCVarRegistrationCode::Unavailable);
    const DeveloperConsoleSnapshot afterRegistration = shipping.GetSnapshot();
    EXPECT_EQ(afterRegistration.generation, initial.generation);

    const DeveloperConsoleBuildPolicyResult downgrade =
        shipping.SetBuildPolicyDetailed(DeveloperConsoleBuildPolicy::Development);
    EXPECT_EQ(downgrade.code, DeveloperConsoleBuildPolicyCode::Locked);
    EXPECT_EQ(shipping.GetSnapshot().generation, initial.generation);
    EXPECT_FALSE(shipping.IsAvailable());
}

TEST(DeveloperConsole, MoveHistoryDetailedReturnsStructuredDiagnostics) {
    DeveloperConsole empty;
    const DeveloperConsoleHistoryNavigationResult emptyHistory = empty.MoveHistoryDetailed(-1);
    EXPECT_EQ(emptyHistory.code, DeveloperConsoleHistoryNavigationCode::EmptyHistory);
    EXPECT_FALSE(emptyHistory.IsAccepted());
    EXPECT_FALSE(emptyHistory.message.empty());

    ASSERT_TRUE(empty.Execute("help"));
    const DeveloperConsoleHistoryNavigationResult invalidDelta = empty.MoveHistoryDetailed(0);
    EXPECT_EQ(invalidDelta.code, DeveloperConsoleHistoryNavigationCode::InvalidDelta);
    EXPECT_FALSE(invalidDelta.IsAccepted());
    EXPECT_FALSE(invalidDelta.message.empty());

    const DeveloperConsoleHistoryNavigationResult applied = empty.MoveHistoryDetailed(-1);
    EXPECT_EQ(applied.code, DeveloperConsoleHistoryNavigationCode::Applied);
    EXPECT_TRUE(applied.IsAccepted());
    EXPECT_TRUE(empty.MoveHistory(1));

    const DeveloperConsoleHistoryNavigationResult boundary = empty.MoveHistoryDetailed(1);
    EXPECT_EQ(boundary.code, DeveloperConsoleHistoryNavigationCode::Boundary);
    EXPECT_FALSE(boundary.IsAccepted());
    EXPECT_FALSE(boundary.message.empty());

    DeveloperConsole shipping(DeveloperConsoleBuildPolicy::Shipping);
    const DeveloperConsoleHistoryNavigationResult unavailable = shipping.MoveHistoryDetailed(-1);
    EXPECT_EQ(unavailable.code, DeveloperConsoleHistoryNavigationCode::Unavailable);
    EXPECT_FALSE(unavailable.IsAccepted());
    EXPECT_FALSE(unavailable.message.empty());
}

TEST(DeveloperConsole, HistoryCursorAndShippingPolicyAreExplicit) {
    DeveloperConsole console;
    ASSERT_TRUE(console.Execute("help"));
    ASSERT_TRUE(console.MoveHistory(-1));
    DeveloperConsoleSnapshot history = console.GetSnapshot();
    EXPECT_EQ(history.historyCursor, 0);
    EXPECT_EQ(history.historyEntry, "help");
    ASSERT_TRUE(console.MoveHistory(1));
    EXPECT_EQ(console.GetSnapshot().historyCursor, -1);

    DeveloperConsole shipping(DeveloperConsoleBuildPolicy::Shipping);
    const DeveloperConsoleSnapshot unavailable = shipping.GetSnapshot();
    EXPECT_FALSE(unavailable.available);
    EXPECT_TRUE(unavailable.developmentOnly);
    EXPECT_FALSE(shipping.Execute("help"));
    EXPECT_FALSE(shipping.SetSeverityFilter(DeveloperConsoleSeverityFilter::Error));
    EXPECT_FALSE(shipping.SetCompletionPrefix("h"));
    EXPECT_FALSE(shipping.MoveHistory(-1));
}

TEST(DeveloperConsole, ClearDetailedReturnsStructuredDiagnostics) {
    DeveloperConsole console;
    const DeveloperConsoleClearResult unchanged = console.ClearDetailed();
    EXPECT_EQ(unchanged.code, DeveloperConsoleClearCode::Unchanged);
    EXPECT_FALSE(unchanged.IsAccepted());
    EXPECT_FALSE(unchanged.message.empty());

    console.Append(DeveloperConsoleSeverity::Info, "entry");
    const DeveloperConsoleClearResult applied = console.ClearDetailed();
    EXPECT_EQ(applied.code, DeveloperConsoleClearCode::Applied);
    EXPECT_TRUE(applied.IsAccepted());
    EXPECT_FALSE(applied.message.empty());
    EXPECT_TRUE(console.GetSnapshot().output.empty());
    EXPECT_FALSE(console.Clear());

    DeveloperConsole shipping(DeveloperConsoleBuildPolicy::Shipping);
    const DeveloperConsoleClearResult unavailable = shipping.ClearDetailed();
    EXPECT_EQ(unavailable.code, DeveloperConsoleClearCode::Unavailable);
    EXPECT_FALSE(unavailable.IsAccepted());
    EXPECT_FALSE(unavailable.message.empty());
}

TEST(DeveloperConsole, OutputAndHistoryExposeTruncationAndClearIsExplicit) {
    DeveloperConsole console;
    for (std::size_t index = 0U; index < DeveloperConsole::kMaximumOutput + 4U; ++index) {
        console.Append(DeveloperConsoleSeverity::Info, "entry");
    }
    for (std::size_t index = 0U; index < DeveloperConsole::kMaximumHistory + 4U; ++index) {
        ASSERT_TRUE(console.Execute("help"));
    }

    const DeveloperConsoleSnapshot beforeClear = console.GetSnapshot();
    EXPECT_TRUE(beforeClear.outputTruncated);
    EXPECT_TRUE(beforeClear.historyTruncated);
    EXPECT_EQ(beforeClear.output.size(), DeveloperConsole::kMaximumOutput);
    EXPECT_EQ(beforeClear.history.size(), DeveloperConsole::kMaximumHistory);
    EXPECT_TRUE(console.Clear());
    EXPECT_FALSE(console.Clear());
    EXPECT_TRUE(console.GetSnapshot().output.empty());
}

TEST(DeveloperConsole, AuditSinkCapturesCopiedPrincipalAndSecurityTransitions) {
    DeveloperConsole console;
    std::vector<DeveloperConsoleAuditRecord> records;
    console.SetAuditSink([&records](const DeveloperConsoleAuditRecord& record) {
        records.push_back(record);
    });

    ASSERT_TRUE(console.SetAuditPrincipal({"editor_user", "session_42"}));
    EXPECT_EQ(console.GetAuditPrincipal(), (DeveloperConsolePrincipal{"editor_user", "session_42"}));
    ASSERT_TRUE(console.RegisterCVar("r.audit", "0"));
    ASSERT_TRUE(console.SetCVar("r.audit", "1"));
    ASSERT_TRUE(console.Execute("help"));
    EXPECT_FALSE(console.Execute("missing"));
    ASSERT_TRUE(console.SetAccess(DeveloperConsoleAccess::ReadOnly));
    EXPECT_FALSE(console.SetCVar("r.audit", "2"));

    ASSERT_EQ(records.size(), 5U);
    EXPECT_EQ(records[0].sequence, 1U);
    EXPECT_EQ(records[0].action, DeveloperConsoleAuditAction::CVarMutation);
    EXPECT_EQ(records[0].principal.subject, "editor_user");
    EXPECT_EQ(records[0].principal.session, "session_42");
    EXPECT_EQ(records[0].target, "r.audit");
    EXPECT_TRUE(records[0].accepted);
    EXPECT_EQ(records[1].action, DeveloperConsoleAuditAction::CommandExecution);
    EXPECT_EQ(records[1].target, "help");
    EXPECT_TRUE(records[1].accepted);
    EXPECT_EQ(records[2].action, DeveloperConsoleAuditAction::CommandExecution);
    EXPECT_EQ(records[2].target, "missing");
    EXPECT_FALSE(records[2].accepted);
    EXPECT_EQ(records[3].action, DeveloperConsoleAuditAction::AccessChange);
    EXPECT_TRUE(records[3].accepted);
    EXPECT_EQ(records[4].action, DeveloperConsoleAuditAction::CVarMutation);
    EXPECT_EQ(records[4].target, "r.audit");
    EXPECT_FALSE(records[4].accepted);
    for (std::size_t index = 1U; index < records.size(); ++index) {
        EXPECT_EQ(records[index].sequence, records[index - 1U].sequence + 1U);
    }
}

TEST(DeveloperConsole, AuditPrincipalRejectsUnboundedOrControlLabelsAtomically) {
    DeveloperConsole console;
    ASSERT_TRUE(console.SetAuditPrincipal({"valid_user", "valid_session"}));
    const DeveloperConsolePrincipal original = console.GetAuditPrincipal();

    EXPECT_FALSE(console.SetAuditPrincipal({"", "session"}));
    EXPECT_FALSE(console.SetAuditPrincipal({std::string(DeveloperConsole::kMaximumPrincipalBytes + 1U, 'x'), "session"}));
    EXPECT_FALSE(console.SetAuditPrincipal({"user\n", "session"}));
    EXPECT_EQ(console.GetAuditPrincipal(), original);
}

TEST(DeveloperConsole, AuditSinkExceptionsDoNotChangeConsoleBehavior) {
    DeveloperConsole console;
    console.SetAuditSink([](const DeveloperConsoleAuditRecord&) {
        throw std::runtime_error("caller sink failure");
    });

    ASSERT_NO_THROW(EXPECT_TRUE(console.RegisterCVar("r.safe", "0")));
    ASSERT_NO_THROW(EXPECT_TRUE(console.SetCVar("r.safe", "1")));
    ASSERT_NO_THROW(EXPECT_TRUE(console.Execute("help")));
    EXPECT_EQ(console.GetSnapshot().cvars.front().value, "1");
}

} // namespace UVE::Editor::Tests
