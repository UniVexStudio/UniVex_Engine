// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "utility_ai.h"

#include <limits>

#include <gtest/gtest.h>

namespace UVE::Scene::Tests {

namespace {

BlackboardComponent MakeBoard() {
    BlackboardComponent board;
    EXPECT_TRUE(SetBlackboardValue(board, "health", 0.5F));
    EXPECT_TRUE(SetBlackboardValue(board, "enemyNear", 1.0F));
    return board;
}

AiAction MakeAction(const char* id, const float base = 1.0F) {
    AiAction action;
    action.actionId = id;
    action.baseScore = base;
    return action;
}

} // namespace

TEST(UtilityAiTest, Curves_EvaluateDocumentedShapes) {
    EXPECT_FLOAT_EQ(EvaluateAiResponseCurve(AiResponseCurve::Linear, 0.25F), 0.25F);
    EXPECT_FLOAT_EQ(EvaluateAiResponseCurve(AiResponseCurve::Quadratic, 0.5F), 0.25F);
    EXPECT_FLOAT_EQ(EvaluateAiResponseCurve(AiResponseCurve::InverseLinear, 0.25F), 0.75F);
    EXPECT_FLOAT_EQ(EvaluateAiResponseCurve(AiResponseCurve::InverseQuadratic, 0.5F), 0.25F);
    // Inputs sanitize to 0..1; non-finite reads as zero.
    EXPECT_FLOAT_EQ(EvaluateAiResponseCurve(AiResponseCurve::Linear, 7.0F), 1.0F);
    EXPECT_FLOAT_EQ(EvaluateAiResponseCurve(AiResponseCurve::Linear, -2.0F), 0.0F);
    EXPECT_FLOAT_EQ(EvaluateAiResponseCurve(AiResponseCurve::Linear,
                                               std::numeric_limits<float>::quiet_NaN()),
                    0.0F);
    EXPECT_FLOAT_EQ(EvaluateAiResponseCurve(AiResponseCurve::InverseLinear,
                                               std::numeric_limits<float>::quiet_NaN()),
                    1.0F);
}

TEST(UtilityAiTest, Blackboard_SetGetRemove) {
    BlackboardComponent board;
    EXPECT_FLOAT_EQ(GetBlackboardValue(board, "missing"), 0.0F);
    ASSERT_TRUE(SetBlackboardValue(board, "health", 0.5F));
    EXPECT_FLOAT_EQ(GetBlackboardValue(board, "health"), 0.5F);
    ASSERT_TRUE(SetBlackboardValue(board, "health", 0.25F));
    EXPECT_FLOAT_EQ(GetBlackboardValue(board, "health"), 0.25F);
    ASSERT_EQ(board.entries.size(), 1U);
    EXPECT_FALSE(SetBlackboardValue(board, "", 1.0F));
    EXPECT_FALSE(SetBlackboardValue(board, "nan",
                                        std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(RemoveBlackboardValue(board, "missing"));
    EXPECT_TRUE(RemoveBlackboardValue(board, "health"));
    EXPECT_TRUE(board.entries.empty());

    BlackboardComponent full;
    for (std::size_t index = 0U; index < kMaximumAiBlackboardEntries; ++index) {
        ASSERT_TRUE(SetBlackboardValue(full, "k" + std::to_string(index), 1.0F));
    }
    EXPECT_FALSE(SetBlackboardValue(full, "overflow", 1.0F));
    EXPECT_TRUE(SetBlackboardValue(full, "k0", 0.0F));
}

TEST(UtilityAiTest, Score_MultipliesWeightedConsiderations) {
    const BlackboardComponent board = MakeBoard();
    AiAction action = MakeAction("fight");
    action.considerations.push_back(AiConsideration{"health", AiResponseCurve::Linear, 1.0F});
    action.considerations.push_back(
        AiConsideration{"enemyNear", AiResponseCurve::Linear, 1.0F});
    EXPECT_FLOAT_EQ(ScoreAiAction(action, board), 0.5F);

    AiAction weighted = MakeAction("fight");
    weighted.considerations.push_back(
        AiConsideration{"health", AiResponseCurve::Linear, 2.0F});
    EXPECT_FLOAT_EQ(ScoreAiAction(weighted, board), 0.25F);

    AiAction ignored = MakeAction("fight");
    ignored.considerations.push_back(AiConsideration{"health", AiResponseCurve::Linear, 0.0F});
    EXPECT_FLOAT_EQ(ScoreAiAction(ignored, board), 1.0F);

    const AiAction plain = MakeAction("idle", 0.3F);
    EXPECT_FLOAT_EQ(ScoreAiAction(plain, board), 0.3F);

    AiAction compensated = action;
    compensated.compensateConsiderationCount = true;
    EXPECT_GT(ScoreAiAction(compensated, board), ScoreAiAction(action, board));
    EXPECT_NEAR(ScoreAiAction(compensated, board), 0.625F, 1.0e-5F);
}

TEST(UtilityAiTest, Score_VetoesOnZeroAndMissingInputs) {
    BlackboardComponent board;
    ASSERT_TRUE(SetBlackboardValue(board, "health", 0.0F));
    AiAction action = MakeAction("fight");
    action.considerations.push_back(AiConsideration{"health", AiResponseCurve::Linear, 1.0F});
    action.considerations.push_back(
        AiConsideration{"neverFed", AiResponseCurve::Linear, 1.0F});
    EXPECT_FLOAT_EQ(ScoreAiAction(action, board), 0.0F);
}

TEST(UtilityAiTest, Select_PicksBestAndHonoursHysteresis) {
    const BlackboardComponent board;
    AiBrainComponent brain;
    brain.hysteresis = 0.1F;
    brain.actions.push_back(MakeAction("patrol", 0.6F));
    brain.actions.push_back(MakeAction("chase", 0.65F));

    AiActionSelection first = SelectAiAction(brain, board);
    EXPECT_EQ(first.actionId, "chase");
    EXPECT_TRUE(first.changed);
    brain.currentAction = first.actionId;
    brain.currentScore = first.score;

    // Within the margin: the incumbent keeps its post.
    brain.actions[1].baseScore = 0.5F;
    const AiActionSelection kept = SelectAiAction(brain, board);
    EXPECT_EQ(kept.actionId, "chase");
    EXPECT_FALSE(kept.changed);

    // Past the margin: the challenger takes over.
    brain.actions[0].baseScore = 0.9F;
    const AiActionSelection switched = SelectAiAction(brain, board);
    EXPECT_EQ(switched.actionId, "patrol");
    EXPECT_TRUE(switched.changed);
}

TEST(UtilityAiTest, Select_AllZeroSelectsNothing) {
    const BlackboardComponent board;
    AiBrainComponent brain;
    brain.actions.push_back(MakeAction("patrol", 0.0F));
    const AiActionSelection none = SelectAiAction(brain, board);
    EXPECT_TRUE(none.actionId.empty());
    EXPECT_FALSE(none.changed);

    brain.currentAction = "patrol";
    const AiActionSelection dropped = SelectAiAction(brain, board);
    EXPECT_TRUE(dropped.actionId.empty());
    EXPECT_TRUE(dropped.changed);
}

TEST(UtilityAiTest, Validity_RejectsBrokenBrainsAndBoards) {
    AiBrainComponent good;
    good.actions.push_back(MakeAction("patrol", 0.5F));
    good.actions.front().considerations.push_back(
        AiConsideration{"boredom", AiResponseCurve::Linear, 1.0F});
    EXPECT_TRUE(IsAiBrainComponentValid(good));

    AiBrainComponent dupe = good;
    dupe.actions.push_back(MakeAction("patrol", 0.5F));
    EXPECT_FALSE(IsAiBrainComponentValid(dupe));

    AiBrainComponent badBase = good;
    badBase.actions.front().baseScore = 1.5F;
    EXPECT_FALSE(IsAiBrainComponentValid(badBase));

    AiBrainComponent badWeight = good;
    badWeight.actions.front().considerations.front().weight = -1.0F;
    EXPECT_FALSE(IsAiBrainComponentValid(badWeight));

    AiBrainComponent badCurve = good;
    badCurve.actions.front().considerations.front().curve =
        static_cast<AiResponseCurve>(0xFFU);
    EXPECT_FALSE(IsAiBrainComponentValid(badCurve));

    AiBrainComponent badHysteresis = good;
    badHysteresis.hysteresis = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsAiBrainComponentValid(badHysteresis));

    BlackboardComponent dupeKeys;
    dupeKeys.entries.push_back(AiBlackboardEntry{"health", 1.0F});
    dupeKeys.entries.push_back(AiBlackboardEntry{"health", 0.5F});
    EXPECT_FALSE(IsBlackboardComponentValid(dupeKeys));
}

} // namespace UVE::Scene::Tests
