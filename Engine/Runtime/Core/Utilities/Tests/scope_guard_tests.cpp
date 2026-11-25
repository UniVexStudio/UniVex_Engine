// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <utility>

#include <gtest/gtest.h>

#include "scope_guard.h"

namespace UVE::Utilities::Tests {
namespace {

TEST(ScopeGuardTest, FiresAtScopeExit) {
    int calls = 0;
    {
        ScopeGuard guard{[&calls] { ++calls; }};
        EXPECT_EQ(calls, 0);
    }
    EXPECT_EQ(calls, 1);
}

TEST(ScopeGuardTest, Dismiss_CancelsTheCall) {
    int calls = 0;
    {
        ScopeGuard guard{[&calls] { ++calls; }};
        guard.Dismiss();
    }
    EXPECT_EQ(calls, 0);
}

TEST(ScopeGuardTest, Move_TransfersTheFiringToTheTarget) {
    int calls = 0;
    {
        ScopeGuard source{[&calls] { ++calls; }};
        ScopeGuard target{std::move(source)};
        EXPECT_EQ(calls, 0);
    }
    EXPECT_EQ(calls, 1); // Exactly once: the moved-from guard stays silent.
}

TEST(ScopeGuardTest, DeferMacro_RunsAtScopeExit) {
    int calls = 0;
    {
        UVE_DEFER(++calls);
        EXPECT_EQ(calls, 0);
    }
    EXPECT_EQ(calls, 1);
}

} // namespace
} // namespace UVE::Utilities::Tests
