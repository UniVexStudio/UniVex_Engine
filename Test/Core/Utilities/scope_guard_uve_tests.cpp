// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <utility>

#include <gtest/gtest.h>

#include "uve/utilities/scope_guard_uve.h"

namespace UVE::Utilities::Tests {
namespace {

TEST(ScopeGuardUVETest, FiresAtScopeExit) {
    int calls = 0;
    {
        ScopeGuardUVE guard{[&calls] { ++calls; }};
        EXPECT_EQ(calls, 0);
    }
    EXPECT_EQ(calls, 1);
}

TEST(ScopeGuardUVETest, Dismiss_CancelsTheCall) {
    int calls = 0;
    {
        ScopeGuardUVE guard{[&calls] { ++calls; }};
        guard.DismissUVE();
    }
    EXPECT_EQ(calls, 0);
}

TEST(ScopeGuardUVETest, Move_TransfersTheFiringToTheTarget) {
    int calls = 0;
    {
        ScopeGuardUVE source{[&calls] { ++calls; }};
        ScopeGuardUVE target{std::move(source)};
        EXPECT_EQ(calls, 0);
    }
    EXPECT_EQ(calls, 1); // Exactly once: the moved-from guard stays silent.
}

TEST(ScopeGuardUVETest, DeferMacro_RunsAtScopeExit) {
    int calls = 0;
    {
        UVE_DEFER(++calls);
        EXPECT_EQ(calls, 0);
    }
    EXPECT_EQ(calls, 1);
}

} // namespace
} // namespace UVE::Utilities::Tests
