// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "engine_version.h"

#include <gtest/gtest.h>

#include "engine_core.h"

namespace UVE::Core::Tests {
namespace {

TEST(VersionTest, ToString_FormatsAllFourComponents) {
    const Version version{1, 2, 3, 4};
    EXPECT_EQ(version.ToString(), "1.2.3+build.4");
}

TEST(VersionTest, EqualityOperators_CompareAllComponents) {
    const Version a{1, 2, 3, 4};
    const Version b{1, 2, 3, 4};
    const Version c{1, 2, 3, 5};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(VersionTest, EngineCore_ReturnsExpectedVersionConstant) {
    const Version version = EngineCore::GetEngineVersion();
    EXPECT_EQ(version, (Version{0, 5, 5, 0}));
}

} // namespace
} // namespace UVE::Core::Tests
