// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <filesystem>
#include <string_view>

namespace UVE::Tests {

/// The absolute scratch root owned by this test process, unique across every process on the
/// machine. Registered as a GoogleTest global environment which creates the directory and makes
/// it the process's working directory before any test runs, then removes it afterwards.
///
/// This exists because Test/CMakeLists.txt discovers tests with gtest_discover_tests, so every
/// test case is its own process, and all of those processes used to share one working directory.
/// Fixtures that derive scratch paths from fixed names then delete them on entry or teardown were
/// deleting each other's files, which made `ctest -j` fail at random. With a per-process working
/// directory, every current-directory-relative path in the suite is isolated for free.
[[nodiscard]] const std::filesystem::path& ScratchRootUVE();

/// An absolute path inside this process's scratch root.
///
/// Prefer a plain relative path in tests - the working directory is already the scratch root, so
/// "fixture.uvescene" is isolated without any help. Reach for this when a path must survive a
/// test changing the working directory, or when the value is handed to something that resolves it
/// later against an unknown directory.
[[nodiscard]] std::filesystem::path ScratchPathUVE(std::string_view name);

/// Creates and returns a directory belonging to the test case that is running right now, named
/// after it and rooted in this process's scratch root. Pass a label to get more than one.
///
/// Use this instead of ScratchPathUVE for a fixture's own working directory. The scratch root
/// alone is unique per process, which is enough under ctest because each case is its own process
/// - but a developer running the executable directly gets every case in ONE process, and those
/// cases would then share a directory. Keying on the current test name covers that too.
[[nodiscard]] std::filesystem::path MakeTestCaseDirectoryUVE(std::string_view label = {});

/// The repository root, from the UVE_SOURCE_DIR compile definition that Test/CMakeLists.txt sets
/// on all four test targets. Tests that read real files checked into the repository - built-in
/// shader sources, for one - must go through this: the working directory is a scratch directory,
/// and it was the build directory before that, so a repository-relative literal never resolved.
[[nodiscard]] std::filesystem::path RepositoryRootUVE();

} // namespace UVE::Tests
