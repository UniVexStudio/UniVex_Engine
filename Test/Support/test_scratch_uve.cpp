// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "Support/test_scratch_uve.h"

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

[[nodiscard]] unsigned long long CurrentProcessIdUVE() {
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
[[nodiscard]] std::filesystem::path MakeScratchRootUVE() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / "uve_tests" /
           (std::to_string(CurrentProcessIdUVE()) + "_" + std::to_string(stamp));
}

/// Creates the scratch root and makes it the working directory for the whole process, then tears
/// it down again. GoogleTest runs this once per process, and gtest_discover_tests gives each test
/// case its own process, so in practice this is per-test-case isolation.
class ScratchDirectoryEnvironmentUVE final : public ::testing::Environment {
public:
    void SetUp() override {
        m_originalWorkingDirectory = std::filesystem::current_path();

        std::error_code errorCode;
        const bool created = std::filesystem::create_directories(ScratchRootUVE(), errorCode);
        ASSERT_FALSE(errorCode) << "could not create the scratch root " << ScratchRootUVE() << ": "
                                << errorCode.message();
        ASSERT_TRUE(created) << "the scratch root " << ScratchRootUVE()
                             << " already existed - the per-process name is not unique";

        std::filesystem::current_path(ScratchRootUVE(), errorCode);
        ASSERT_FALSE(errorCode) << "could not enter the scratch root " << ScratchRootUVE() << ": "
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
            std::cerr << "[  SCRATCH ] kept for inspection: " << ScratchRootUVE() << '\n';
            return;
        }

        std::filesystem::remove_all(ScratchRootUVE(), errorCode);
    }

private:
    std::filesystem::path m_originalWorkingDirectory;
};

/// All four test executables link GTest::gtest_main, so there is no main() to register the
/// environment from. A static initializer does it instead. This is safe against
/// static-initialization order because GoogleTest keeps its environment list inside
/// UnitTest::GetInstance(), a function-local static that constructs on first use. The translation
/// unit is named directly in each add_executable(), so the linker cannot drop it the way it could
/// if this lived in a static library.
struct ScratchEnvironmentRegistrarUVE final {
    ScratchEnvironmentRegistrarUVE() { ::testing::AddGlobalTestEnvironment(new ScratchDirectoryEnvironmentUVE()); }
};

[[maybe_unused]] const ScratchEnvironmentRegistrarUVE kScratchEnvironmentRegistrarUVE{};

} // namespace

const std::filesystem::path& ScratchRootUVE() {
    static const std::filesystem::path root = MakeScratchRootUVE();
    return root;
}

std::filesystem::path ScratchPathUVE(const std::string_view name) {
    return ScratchRootUVE() / name;
}

std::filesystem::path MakeTestCaseDirectoryUVE(const std::string_view label) {
    const ::testing::TestInfo* const testInfo = ::testing::UnitTest::GetInstance()->current_test_info();

    // current_test_info() is null outside a running test (a fixture's static setup, say). Falling
    // back to a fixed name is safe: the scratch root is still per-process.
    std::string directoryName =
        testInfo != nullptr ? std::string(testInfo->test_suite_name()) + "." + testInfo->name() : "unnamed";

    // Parameterised and typed suites put '/' in their names, which would silently create nested
    // directories instead of the one being asked for.
    std::replace(directoryName.begin(), directoryName.end(), '/', '_');

    std::filesystem::path directory = ScratchRootUVE() / directoryName;
    if (!label.empty()) {
        directory /= label;
    }

    std::error_code errorCode;
    std::filesystem::create_directories(directory, errorCode);
    EXPECT_FALSE(errorCode) << "could not create the test-case directory " << directory << ": "
                            << errorCode.message();
    return directory;
}

std::filesystem::path RepositoryRootUVE() {
    return std::filesystem::path(UVE_SOURCE_DIR);
}

} // namespace UVE::Tests
