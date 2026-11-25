// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "shader_cache_path_policy.h"

namespace UVE::Core::Tests {
namespace {

TEST(ShaderCachePathPolicyTest, AcceptsNonemptyRelativeDirectories) {
    EXPECT_TRUE(IsSafeShaderCachePath(std::filesystem::path("shader_cache")));
    EXPECT_TRUE(IsSafeShaderCachePath(std::filesystem::path("profiles/desktop/shader_cache")));
    EXPECT_TRUE(IsSafeShaderCachePath(std::filesystem::path(".")));
}

TEST(ShaderCachePathPolicyTest, RejectsRootedPathsAndParentTraversal) {
    EXPECT_FALSE(IsSafeShaderCachePath(std::filesystem::path{}));
    EXPECT_FALSE(IsSafeShaderCachePath(std::filesystem::path("/shader_cache")));
    EXPECT_FALSE(IsSafeShaderCachePath(std::filesystem::path("../shader_cache")));
    EXPECT_FALSE(IsSafeShaderCachePath(std::filesystem::path("profiles/../../shader_cache")));
    EXPECT_FALSE(IsSafeShaderCachePath(std::filesystem::path("profiles/cache/..")));
}

TEST(ShaderCachePathPolicyTest, RejectsEmbeddedNullBytes) {
    const std::filesystem::path pathWithNull(std::string("cache\0/shaders", 14U));
    EXPECT_FALSE(IsSafeShaderCachePath(pathWithNull));
}

} // namespace
} // namespace UVE::Core::Tests
