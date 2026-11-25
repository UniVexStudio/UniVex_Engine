// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "crash_reporter.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace UVE::Platform::Tests {
namespace {

TEST(CrashReporterTest, InstallAndRelease_RemovesTheUnusedReservedReport) {
    const std::filesystem::path dumpDirectory = ::UVE::Tests::MakeTestCaseDirectory("crash-reports");
    std::filesystem::path reportPath;
    {
        CrashReporter reporter;
        const CrashReporterResult result = reporter.Install(dumpDirectory, "com.example.crash-test");
        ASSERT_TRUE(result.IsInstalled()) << result.message;
        reportPath = result.reportPath;
        EXPECT_TRUE(std::filesystem::is_regular_file(reportPath));

        CrashReporter duplicate;
        const CrashReporterResult duplicateResult =
            duplicate.Install(dumpDirectory, "com.example.crash-test");
        EXPECT_EQ(duplicateResult.code, CrashReporterCode::AlreadyInstalled);
    }
    EXPECT_FALSE(std::filesystem::exists(reportPath));

    CrashReporter afterRelease;
    EXPECT_TRUE(afterRelease.Install(dumpDirectory, "com.example.crash-test").IsInstalled());
}

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
TEST(CrashReporterTest, FatalSignalInChild_LeavesAnAsyncSafeReport) {
    const std::filesystem::path dumpDirectory = ::UVE::Tests::MakeTestCaseDirectory("fatal-signal");
    const pid_t child = fork();
    ASSERT_NE(child, static_cast<pid_t>(-1));
    if (child == 0) {
        CrashReporter reporter;
        const CrashReporterResult result = reporter.Install(dumpDirectory, "com.example.signal-test");
        if (!result.IsInstalled()) {
            _exit(20);
        }
        static_cast<void>(raise(SIGABRT));
        _exit(21);
    }

    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 128 + SIGABRT);

    std::filesystem::path reportPath;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dumpDirectory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".log") {
            reportPath = entry.path();
            break;
        }
    }
    ASSERT_FALSE(reportPath.empty());
    std::ifstream input(reportPath, std::ios::binary);
    ASSERT_TRUE(input.is_open());
    const std::string report{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    EXPECT_NE(report.find("Fatal POSIX signal"), std::string::npos);
    EXPECT_NE(report.find("Application: com.example.signal-test"), std::string::npos);
}
#endif

} // namespace
} // namespace UVE::Platform::Tests
