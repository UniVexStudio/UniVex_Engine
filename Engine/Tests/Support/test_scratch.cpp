// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "Support/test_scratch.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#ifndef UVE_SOURCE_DIR
#error "UVE_SOURCE_DIR must be defined for every test target - see Test/CMakeLists.txt."
#endif

namespace UVE::Tests {
namespace {

[[nodiscard]] unsigned long long CurrentProcessId() {
#if defined(_WIN32)
    return static_cast<unsigned long long>(_getpid());
#else
    return static_cast<unsigned long long>(::getpid());
#endif
}

/// Process id alone is not enough: the operating system reuses process ids, so a directory left
/// behind by a crashed earlier run could collide with a live one. The steady-clock stamp closes
/// that - two processes would have to share both a process id and a nanosecond to collide, and
/// SetUp() below fails loudly rather than silently reusing the directory if they somehow do.
[[nodiscard]] std::filesystem::path MakeScratchRoot() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / "uve_tests" /
           (std::to_string(CurrentProcessId()) + "_" + std::to_string(stamp));
}

/// Creates the scratch root and makes it the working directory for the whole process, then tears
/// it down again. GoogleTest runs this once per process, and gtest_discover_tests gives each test
/// case its own process, so in practice this is per-test-case isolation.
class ScratchDirectoryEnvironment final : public ::testing::Environment {
public:
    void SetUp() override {
        m_originalWorkingDirectory = std::filesystem::current_path();

        std::error_code errorCode;
        const bool created = std::filesystem::create_directories(ScratchRoot(), errorCode);
        ASSERT_FALSE(errorCode) << "could not create the scratch root " << ScratchRoot() << ": "
                                << errorCode.message();
        ASSERT_TRUE(created) << "the scratch root " << ScratchRoot()
                             << " already existed - the per-process name is not unique";

        std::filesystem::current_path(ScratchRoot(), errorCode);
        ASSERT_FALSE(errorCode) << "could not enter the scratch root " << ScratchRoot() << ": "
                                << errorCode.message();
    }

    void TearDown() override {
        // Leave the scratch directory before removing it, so the process never sits inside a
        // directory that is being deleted.
        std::error_code errorCode;
        std::filesystem::current_path(m_originalWorkingDirectory, errorCode);

        // A failing run's scratch directory is evidence. Keeping it costs a little disk under
        // /tmp and saves reproducing the failure just to see what was on disk. A crashing process
        // never reaches this at all, which is the other reason every root lives under one
        // "uve_tests" parent: orphans are removable in a single command.
        if (::testing::UnitTest::GetInstance()->Failed()) {
            std::cerr << "[  SCRATCH ] kept for inspection: " << ScratchRoot() << '\n';
            return;
        }

        std::filesystem::remove_all(ScratchRoot(), errorCode);
    }

private:
    std::filesystem::path m_originalWorkingDirectory;
};

/// Every test executable that includes this file links GTest::gtest_main, so there is no main() to register the
/// environment from. A static initializer does it instead. This is safe against
/// static-initialization order because GoogleTest keeps its environment list inside
/// UnitTest::GetInstance(), a function-local static that constructs on first use. The translation
/// unit is named directly in each add_executable(), so the linker cannot drop it the way it could
/// if this lived in a static library.
struct ScratchEnvironmentRegistrar final {
    ScratchEnvironmentRegistrar() { ::testing::AddGlobalTestEnvironment(new ScratchDirectoryEnvironment()); }
};

[[maybe_unused]] const ScratchEnvironmentRegistrar kScratchEnvironmentRegistrar{};

} // namespace

const std::filesystem::path& ScratchRoot() {
    static const std::filesystem::path root = MakeScratchRoot();
    return root;
}

std::filesystem::path ScratchPath(const std::string_view name) {
    return ScratchRoot() / name;
}

std::filesystem::path MakeTestCaseDirectory(const std::string_view label) {
    const ::testing::TestInfo* const testInfo = ::testing::UnitTest::GetInstance()->current_test_info();

    // current_test_info() is null outside a running test (a fixture's static setup, say). Falling
    // back to a fixed name is safe: the scratch root is still per-process.
    std::string directoryName =
        testInfo != nullptr ? std::string(testInfo->test_suite_name()) + "." + testInfo->name() : "unnamed";

    // Parameterised and typed suites put '/' in their names, which would silently create nested
    // directories instead of the one being asked for.
    std::replace(directoryName.begin(), directoryName.end(), '/', '_');

    std::filesystem::path directory = ScratchRoot() / directoryName;
    if (!label.empty()) {
        directory /= label;
    }

    std::error_code errorCode;
    std::filesystem::create_directories(directory, errorCode);
    EXPECT_FALSE(errorCode) << "could not create the test-case directory " << directory << ": "
                            << errorCode.message();
    return directory;
}

std::filesystem::path RepositoryRoot() {
    return std::filesystem::path(UVE_SOURCE_DIR);
}

} // namespace UVE::Tests
