// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ai/utility_ai_uve.h"

#include <limits>

#include <gtest/gtest.h>

namespace UVE::Scene::Tests {

namespace {

BlackboardComponentUVE MakeBoardUVE() {
    BlackboardComponentUVE board;
    EXPECT_TRUE(SetBlackboardValueUVE(board, "health", 0.5F));
    EXPECT_TRUE(SetBlackboardValueUVE(board, "enemyNear", 1.0F));
    return board;
}

AiActionUVE MakeActionUVE(const char* id, const float base = 1.0F) {
    AiActionUVE action;
    action.actionId = id;
    action.baseScore = base;
    return action;
}

} // namespace

TEST(UtilityAiUVETest, Curves_EvaluateDocumentedShapes) {
    EXPECT_FLOAT_EQ(EvaluateAiResponseCurveUVE(AiResponseCurveUVE::Linear, 0.25F), 0.25F);
    EXPECT_FLOAT_EQ(EvaluateAiResponseCurveUVE(AiResponseCurveUVE::Quadratic, 0.5F), 0.25F);
    EXPECT_FLOAT_EQ(EvaluateAiResponseCurveUVE(AiResponseCurveUVE::InverseLinear, 0.25F), 0.75F);
    EXPECT_FLOAT_EQ(EvaluateAiResponseCurveUVE(AiResponseCurveUVE::InverseQuadratic, 0.5F), 0.25F);
    // Inputs sanitize to 0..1; non-finite reads as zero.
    EXPECT_FLOAT_EQ(EvaluateAiResponseCurveUVE(AiResponseCurveUVE::Linear, 7.0F), 1.0F);
    EXPECT_FLOAT_EQ(EvaluateAiResponseCurveUVE(AiResponseCurveUVE::Linear, -2.0F), 0.0F);
    EXPECT_FLOAT_EQ(EvaluateAiResponseCurveUVE(AiResponseCurveUVE::Linear,
                                               std::numeric_limits<float>::quiet_NaN()),
                    0.0F);
    EXPECT_FLOAT_EQ(EvaluateAiResponseCurveUVE(AiResponseCurveUVE::InverseLinear,
                                               std::numeric_limits<float>::quiet_NaN()),
                    1.0F);
}

TEST(UtilityAiUVETest, Blackboard_SetGetRemove) {
    BlackboardComponentUVE board;
    EXPECT_FLOAT_EQ(GetBlackboardValueUVE(board, "missing"), 0.0F);
    ASSERT_TRUE(SetBlackboardValueUVE(board, "health", 0.5F));
    EXPECT_FLOAT_EQ(GetBlackboardValueUVE(board, "health"), 0.5F);
    ASSERT_TRUE(SetBlackboardValueUVE(board, "health", 0.25F));
    EXPECT_FLOAT_EQ(GetBlackboardValueUVE(board, "health"), 0.25F);
    ASSERT_EQ(board.entries.size(), 1U);
    EXPECT_FALSE(SetBlackboardValueUVE(board, "", 1.0F));
    EXPECT_FALSE(SetBlackboardValueUVE(board, "nan",
                                        std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(RemoveBlackboardValueUVE(board, "missing"));
    EXPECT_TRUE(RemoveBlackboardValueUVE(board, "health"));
    EXPECT_TRUE(board.entries.empty());

    BlackboardComponentUVE full;
    for (std::size_t index = 0U; index < kMaximumAiBlackboardEntriesUVE; ++index) {
        ASSERT_TRUE(SetBlackboardValueUVE(full, "k" + std::to_string(index), 1.0F));
    }
    EXPECT_FALSE(SetBlackboardValueUVE(full, "overflow", 1.0F));
    EXPECT_TRUE(SetBlackboardValueUVE(full, "k0", 0.0F));
}

TEST(UtilityAiUVETest, Score_MultipliesWeightedConsiderations) {
    const BlackboardComponentUVE board = MakeBoardUVE();
    AiActionUVE action = MakeActionUVE("fight");
    action.considerations.push_back(AiConsiderationUVE{"health", AiResponseCurveUVE::Linear, 1.0F});
    action.considerations.push_back(
        AiConsiderationUVE{"enemyNear", AiResponseCurveUVE::Linear, 1.0F});
    EXPECT_FLOAT_EQ(ScoreAiActionUVE(action, board), 0.5F);

    AiActionUVE weighted = MakeActionUVE("fight");
    weighted.considerations.push_back(
        AiConsiderationUVE{"health", AiResponseCurveUVE::Linear, 2.0F});
    EXPECT_FLOAT_EQ(ScoreAiActionUVE(weighted, board), 0.25F);

    AiActionUVE ignored = MakeActionUVE("fight");
    ignored.considerations.push_back(AiConsiderationUVE{"health", AiResponseCurveUVE::Linear, 0.0F});
    EXPECT_FLOAT_EQ(ScoreAiActionUVE(ignored, board), 1.0F);

    const AiActionUVE plain = MakeActionUVE("idle", 0.3F);
    EXPECT_FLOAT_EQ(ScoreAiActionUVE(plain, board), 0.3F);
}

TEST(UtilityAiUVETest, Score_VetoesOnZeroAndMissingInputs) {
    BlackboardComponentUVE board;
    ASSERT_TRUE(SetBlackboardValueUVE(board, "health", 0.0F));
    AiActionUVE action = MakeActionUVE("fight");
    action.considerations.push_back(AiConsiderationUVE{"health", AiResponseCurveUVE::Linear, 1.0F});
    action.considerations.push_back(
        AiConsiderationUVE{"neverFed", AiResponseCurveUVE::Linear, 1.0F});
    EXPECT_FLOAT_EQ(ScoreAiActionUVE(action, board), 0.0F);
}

TEST(UtilityAiUVETest, Select_PicksBestAndHonoursHysteresis) {
    const BlackboardComponentUVE board;
    AiBrainComponentUVE brain;
    brain.hysteresis = 0.1F;
    brain.actions.push_back(MakeActionUVE("patrol", 0.6F));
    brain.actions.push_back(MakeActionUVE("chase", 0.65F));

    AiActionSelectionUVE first = SelectAiActionUVE(brain, board);
    EXPECT_EQ(first.actionId, "chase");
    EXPECT_TRUE(first.changed);
    brain.currentAction = first.actionId;
    brain.currentScore = first.score;

    // Within the margin: the incumbent keeps its post.
    brain.actions[1].baseScore = 0.5F;
    const AiActionSelectionUVE kept = SelectAiActionUVE(brain, board);
    EXPECT_EQ(kept.actionId, "chase");
    EXPECT_FALSE(kept.changed);

    // Past the margin: the challenger takes over.
    brain.actions[0].baseScore = 0.9F;
    const AiActionSelectionUVE switched = SelectAiActionUVE(brain, board);
    EXPECT_EQ(switched.actionId, "patrol");
    EXPECT_TRUE(switched.changed);
}

TEST(UtilityAiUVETest, Select_AllZeroSelectsNothing) {
    const BlackboardComponentUVE board;
    AiBrainComponentUVE brain;
    brain.actions.push_back(MakeActionUVE("patrol", 0.0F));
    const AiActionSelectionUVE none = SelectAiActionUVE(brain, board);
    EXPECT_TRUE(none.actionId.empty());
    EXPECT_FALSE(none.changed);

    brain.currentAction = "patrol";
    const AiActionSelectionUVE dropped = SelectAiActionUVE(brain, board);
    EXPECT_TRUE(dropped.actionId.empty());
    EXPECT_TRUE(dropped.changed);
}

TEST(UtilityAiUVETest, Validity_RejectsBrokenBrainsAndBoards) {
    AiBrainComponentUVE good;
    good.actions.push_back(MakeActionUVE("patrol", 0.5F));
    good.actions.front().considerations.push_back(
        AiConsiderationUVE{"boredom", AiResponseCurveUVE::Linear, 1.0F});
    EXPECT_TRUE(IsAiBrainComponentValidUVE(good));

    AiBrainComponentUVE dupe = good;
    dupe.actions.push_back(MakeActionUVE("patrol", 0.5F));
    EXPECT_FALSE(IsAiBrainComponentValidUVE(dupe));

    AiBrainComponentUVE badBase = good;
    badBase.actions.front().baseScore = 1.5F;
    EXPECT_FALSE(IsAiBrainComponentValidUVE(badBase));

    AiBrainComponentUVE badWeight = good;
    badWeight.actions.front().considerations.front().weight = -1.0F;
    EXPECT_FALSE(IsAiBrainComponentValidUVE(badWeight));

    AiBrainComponentUVE badCurve = good;
    badCurve.actions.front().considerations.front().curve =
        static_cast<AiResponseCurveUVE>(0xFFU);
    EXPECT_FALSE(IsAiBrainComponentValidUVE(badCurve));

    AiBrainComponentUVE badHysteresis = good;
    badHysteresis.hysteresis = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsAiBrainComponentValidUVE(badHysteresis));

    BlackboardComponentUVE dupeKeys;
    dupeKeys.entries.push_back(AiBlackboardEntryUVE{"health", 1.0F});
    dupeKeys.entries.push_back(AiBlackboardEntryUVE{"health", 0.5F});
    EXPECT_FALSE(IsBlackboardComponentValidUVE(dupeKeys));
}

} // namespace UVE::Scene::Tests
