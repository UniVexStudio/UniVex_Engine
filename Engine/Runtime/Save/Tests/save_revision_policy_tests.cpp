#include "save_revision_policy.h"

#include <gtest/gtest.h>

namespace UVE::Save::Tests {

TEST(SaveRevisionPolicyTest, MapsRevisionStatusesToSyncActions) {
    EXPECT_EQ(EvaluateSaveSyncAction(SaveRevisionStatus::Unchanged), SaveSyncAction::NoOp);
    EXPECT_EQ(EvaluateSaveSyncAction(SaveRevisionStatus::LocalAhead), SaveSyncAction::Upload);
    EXPECT_EQ(EvaluateSaveSyncAction(SaveRevisionStatus::RemoteAhead), SaveSyncAction::Download);
    EXPECT_EQ(EvaluateSaveSyncAction(SaveRevisionStatus::Conflict), SaveSyncAction::Conflict);
    EXPECT_EQ(EvaluateSaveSyncAction(SaveRevisionStatus::Invalid), SaveSyncAction::Invalid);
}

TEST(SaveRevisionPolicyTest, ClassifiesUnchangedLocalAheadRemoteAheadAndConflict) {
    EXPECT_EQ(EvaluateSaveRevision(4U, 4U, 4U), SaveRevisionStatus::Unchanged);
    EXPECT_EQ(EvaluateSaveRevision(4U, 5U, 4U), SaveRevisionStatus::LocalAhead);
    EXPECT_EQ(EvaluateSaveRevision(4U, 4U, 5U), SaveRevisionStatus::RemoteAhead);
    EXPECT_EQ(EvaluateSaveRevision(4U, 5U, 6U), SaveRevisionStatus::Conflict);
}

TEST(SaveRevisionPolicyTest, RejectsZeroRevisionFacts) {
    EXPECT_EQ(EvaluateSaveRevision(0U, 4U, 4U), SaveRevisionStatus::Invalid);
    EXPECT_EQ(EvaluateSaveRevision(4U, 0U, 4U), SaveRevisionStatus::Invalid);
    EXPECT_EQ(EvaluateSaveRevision(4U, 4U, 0U), SaveRevisionStatus::Invalid);
}

} // namespace UVE::Save::Tests
