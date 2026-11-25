// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "engine_state.h"

#include <gtest/gtest.h>

// EngineState's role as a formal, enforced lifecycle (invalid
// transitions triggering UVE_ASSERT) is exercised end-to-end against the
// real EngineCore in engine_core_uve_tests.cpp — this file covers the
// pure, state-machine-only logic in IsValidTransition()/ToString().

namespace UVE::Core::Tests {
namespace {

TEST(EngineStateTest, ValidTransitions_FollowLifecycleOrder) {
    EXPECT_TRUE(IsValidTransition(EngineState::Uninitialized, EngineState::Initializing));
    EXPECT_TRUE(IsValidTransition(EngineState::Initializing, EngineState::Running));
    EXPECT_TRUE(IsValidTransition(EngineState::Running, EngineState::ShuttingDown));
    EXPECT_TRUE(IsValidTransition(EngineState::ShuttingDown, EngineState::Shutdown));
}

TEST(EngineStateTest, InvalidTransitions_AreRejected) {
    EXPECT_FALSE(IsValidTransition(EngineState::Uninitialized, EngineState::Running));
    EXPECT_FALSE(IsValidTransition(EngineState::Running, EngineState::Initializing));
    EXPECT_FALSE(IsValidTransition(EngineState::Shutdown, EngineState::Uninitialized));
    EXPECT_FALSE(IsValidTransition(EngineState::Running, EngineState::Running));
    EXPECT_FALSE(IsValidTransition(EngineState::Shutdown, EngineState::Initializing));
    EXPECT_FALSE(IsValidTransition(EngineState::Uninitialized, EngineState::ShuttingDown));
}

TEST(EngineStateTest, ToString_ReturnsExpectedNames) {
    EXPECT_EQ(ToString(EngineState::Uninitialized), "Uninitialized");
    EXPECT_EQ(ToString(EngineState::Initializing), "Initializing");
    EXPECT_EQ(ToString(EngineState::Running), "Running");
    EXPECT_EQ(ToString(EngineState::ShuttingDown), "ShuttingDown");
    EXPECT_EQ(ToString(EngineState::Shutdown), "Shutdown");
}

} // namespace
} // namespace UVE::Core::Tests
