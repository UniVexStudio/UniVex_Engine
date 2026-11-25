// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include "editor_tool_session.h"

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] Scene::TransformComponent MakeTransform(const float x) {
    Scene::TransformComponent transform{};
    transform.localPosition.x = x;
    return transform;
}

TEST(EditorToolSessionTest, Begin_CapturesBaselineAndReentrantBeginIsStrictNoOp) {
    EditorToolSession session;
    const Scene::Entity firstEntity{1U, 1U};
    const Scene::Entity secondEntity{2U, 1U};
    const Scene::TransformComponent baseline = MakeTransform(3.0F);

    ASSERT_TRUE(session.Begin(firstEntity, EditorToolSessionMode::Translate, baseline, true));
    ASSERT_EQ(session.GetPhase(), EditorToolSessionPhase::Previewing);
    ASSERT_TRUE(session.GetSnapshot().has_value());
    EXPECT_EQ(session.GetSnapshot()->entity, firstEntity);
    EXPECT_EQ(session.GetSnapshot()->mode, EditorToolSessionMode::Translate);
    EXPECT_EQ(session.GetSnapshot()->baselineTransform.localPosition.x, 3.0F);
    EXPECT_EQ(session.GetSnapshot()->lastAppliedTransform.localPosition.x, 3.0F);
    EXPECT_TRUE(session.GetSnapshot()->baselineDirty);
    EXPECT_EQ(session.GetLastOutcome(), EditorToolSessionOutcome::None);

    EXPECT_FALSE(session.Begin(secondEntity, EditorToolSessionMode::Scale, MakeTransform(9.0F), false));
    ASSERT_EQ(session.GetPhase(), EditorToolSessionPhase::Previewing);
    ASSERT_TRUE(session.GetSnapshot().has_value());
    EXPECT_EQ(session.GetSnapshot()->entity, firstEntity);
    EXPECT_EQ(session.GetSnapshot()->mode, EditorToolSessionMode::Translate);
    EXPECT_EQ(session.GetSnapshot()->baselineTransform.localPosition.x, 3.0F);
    EXPECT_EQ(session.GetLastOutcome(), EditorToolSessionOutcome::None);
}

TEST(EditorToolSessionTest, PreviewAndChangedCommit_ReturnOneCopiedTransactionWithoutFurtherMutation) {
    EditorToolSession session;
    const Scene::Entity entity{4U, 1U};
    const Scene::TransformComponent baseline = MakeTransform(0.0F);
    const Scene::TransformComponent preview = MakeTransform(5.0F);

    ASSERT_TRUE(session.Begin(entity, EditorToolSessionMode::Rotate, baseline, false));
    ASSERT_TRUE(session.RecordPreviewApplied(preview));
    const std::optional<EditorToolSessionSnapshot> committed = session.Commit(true);

    ASSERT_TRUE(committed.has_value());
    EXPECT_EQ(committed->entity, entity);
    EXPECT_EQ(committed->baselineTransform.localPosition.x, 0.0F);
    EXPECT_EQ(committed->lastAppliedTransform.localPosition.x, 5.0F);
    EXPECT_EQ(session.GetPhase(), EditorToolSessionPhase::Idle);
    EXPECT_EQ(session.GetLastOutcome(), EditorToolSessionOutcome::Committed);
    EXPECT_FALSE(session.GetSnapshot().has_value());
}

TEST(EditorToolSessionTest, ZeroDeltaCommit_CompletesWithoutHistoryOutcome) {
    EditorToolSession session;
    const Scene::Entity entity{5U, 1U};
    const Scene::TransformComponent baseline = MakeTransform(1.0F);

    ASSERT_TRUE(session.Begin(entity, EditorToolSessionMode::Scale, baseline, true));
    ASSERT_TRUE(session.RecordPreviewApplied(baseline));
    const std::optional<EditorToolSessionSnapshot> completed = session.Commit(false);

    ASSERT_TRUE(completed.has_value());
    EXPECT_EQ(completed->lastAppliedTransform.localPosition.x, baseline.localPosition.x);
    EXPECT_EQ(session.GetPhase(), EditorToolSessionPhase::Idle);
    EXPECT_EQ(session.GetLastOutcome(), EditorToolSessionOutcome::CompletedWithoutChange);
}

TEST(EditorToolSessionTest, Cancel_ReturnsExactBaselineAndRestoreFailureIsExplicit) {
    EditorToolSession session;
    const Scene::Entity entity{6U, 1U};
    const Scene::TransformComponent baseline = MakeTransform(-2.0F);
    const Scene::TransformComponent preview = MakeTransform(8.0F);

    ASSERT_TRUE(session.Begin(entity, EditorToolSessionMode::Translate, baseline, false));
    ASSERT_TRUE(session.RecordPreviewApplied(preview));
    const std::optional<EditorToolSessionSnapshot> cancelled = session.Cancel(preview);

    ASSERT_TRUE(cancelled.has_value());
    EXPECT_EQ(cancelled->baselineTransform.localPosition.x, -2.0F);
    EXPECT_EQ(cancelled->lastAppliedTransform.localPosition.x, 8.0F);
    EXPECT_EQ(session.GetPhase(), EditorToolSessionPhase::Idle);
    EXPECT_EQ(session.GetLastOutcome(), EditorToolSessionOutcome::Cancelled);

    session.MarkRestoreFailed();
    EXPECT_EQ(session.GetLastOutcome(), EditorToolSessionOutcome::RestoreFailed);
    EXPECT_FALSE(session.Cancel(preview).has_value());
}

TEST(EditorToolSessionTest, Cancel_ExternalTransformConflictPreservesExternalValue) {
    EditorToolSession session;
    const Scene::Entity entity{7U, 1U};
    const Scene::TransformComponent baseline = MakeTransform(0.0F);
    const Scene::TransformComponent preview = MakeTransform(2.0F);
    const Scene::TransformComponent externallyModified = MakeTransform(9.0F);

    ASSERT_TRUE(session.Begin(entity, EditorToolSessionMode::Translate, baseline, false));
    ASSERT_TRUE(session.RecordPreviewApplied(preview));

    EXPECT_FALSE(session.Cancel(externallyModified).has_value());
    EXPECT_EQ(session.GetPhase(), EditorToolSessionPhase::Idle);
    EXPECT_EQ(session.GetLastOutcome(), EditorToolSessionOutcome::ExternalTransformConflict);
    EXPECT_FALSE(session.GetSnapshot().has_value());
}

TEST(EditorToolSessionTest, InvalidOperations_RejectWithoutCreatingSession) {
    EditorToolSession session;
    EXPECT_FALSE(session.Begin(Scene::kInvalidEntity, EditorToolSessionMode::Translate,
                                  Scene::TransformComponent{}, false));
    EXPECT_EQ(session.GetPhase(), EditorToolSessionPhase::Idle);
    EXPECT_EQ(session.GetLastOutcome(), EditorToolSessionOutcome::Rejected);
    EXPECT_FALSE(session.RecordPreviewApplied(Scene::TransformComponent{}));
    EXPECT_FALSE(session.Commit(true).has_value());
}

} // namespace
} // namespace UVE::Editor::Tests
