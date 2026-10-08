// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "uve/core/shader_cache_path_policy_uve.h"

namespace UVE::Core::Tests {
namespace {

TEST(ShaderCachePathPolicyUVETest, AcceptsNonemptyRelativeDirectories) {
    EXPECT_TRUE(IsSafeShaderCachePathUVE(std::filesystem::path("shader_cache")));
    EXPECT_TRUE(IsSafeShaderCachePathUVE(std::filesystem::path("profiles/desktop/shader_cache")));
    EXPECT_TRUE(IsSafeShaderCachePathUVE(std::filesystem::path(".")));
}

TEST(ShaderCachePathPolicyUVETest, RejectsRootedPathsAndParentTraversal) {
    EXPECT_FALSE(IsSafeShaderCachePathUVE(std::filesystem::path{}));
    EXPECT_FALSE(IsSafeShaderCachePathUVE(std::filesystem::path("/shader_cache")));
    EXPECT_FALSE(IsSafeShaderCachePathUVE(std::filesystem::path("../shader_cache")));
    EXPECT_FALSE(IsSafeShaderCachePathUVE(std::filesystem::path("profiles/../../shader_cache")));
    EXPECT_FALSE(IsSafeShaderCachePathUVE(std::filesystem::path("profiles/cache/..")));
}

TEST(ShaderCachePathPolicyUVETest, RejectsEmbeddedNullBytes) {
    const std::filesystem::path pathWithNull(std::string("cache\0/shaders", 14U));
    EXPECT_FALSE(IsSafeShaderCachePathUVE(pathWithNull));
}

} // namespace
} // namespace UVE::Core::Tests
