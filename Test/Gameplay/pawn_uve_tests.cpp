// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/gameplay/pawn_uve.h"

#include <limits>

#include <gtest/gtest.h>

#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"

namespace UVE::Scene::Tests {
namespace {

class PawnUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    EntityUVE MakeControllerUVE(const ControllerKindUVE kind = ControllerKindUVE::Player) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        ControllerComponentUVE controller;
        controller.kind = kind;
        entityManager.AddComponentUVE<ControllerComponentUVE>(entity, controller);
        return entity;
    }

    EntityUVE MakePawnUVE() {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<PawnComponentUVE>(entity, PawnComponentUVE{});
        return entity;
    }

    Gameplay::GameplayInputUVE SnapshotUVE() {
        Gameplay::GameplayInputUVE snapshot;
        snapshot.move = {1.0F, 2.0F, 3.0F};
        snapshot.rise = 4.0F;
        snapshot.jumpPressed = true;
        snapshot.lookPointer = {5.0F, 6.0F};
        snapshot.lookStick = {7.0F, 8.0F};
        snapshot.interactPressed = true;
        return snapshot;
    }
};

TEST_F(PawnUVETest, Possess_LinksBothSidesAndStealsExclusively) {
    const EntityUVE firstController = MakeControllerUVE();
    const EntityUVE secondController = MakeControllerUVE(ControllerKindUVE::AI);
    const EntityUVE firstPawn = MakePawnUVE();
    const EntityUVE secondPawn = MakePawnUVE();

    ASSERT_TRUE(PossessControllerUVE(entityManager, firstController, firstPawn));
    EXPECT_EQ(FindPossessedPawnUVE(entityManager, firstController), firstPawn);
    EXPECT_EQ(FindPawnControllerUVE(entityManager, firstPawn), firstController);
    RouteGameplayInputUVE(entityManager, SnapshotUVE());
    ASSERT_EQ(entityManager.GetComponentUVE<PawnComponentUVE>(firstPawn).input, SnapshotUVE());

    // Stealing from the controller side unlinks the previous pawn and purges its input.
    ASSERT_TRUE(PossessControllerUVE(entityManager, firstController, secondPawn));
    EXPECT_EQ(FindPossessedPawnUVE(entityManager, firstController), secondPawn);
    EXPECT_EQ(FindPawnControllerUVE(entityManager, firstPawn), kInvalidEntityUVE);
    EXPECT_EQ(entityManager.GetComponentUVE<PawnComponentUVE>(firstPawn).input,
              Gameplay::GameplayInputUVE{});

    // Stealing from the pawn side unlinks the previous controller.
    ASSERT_TRUE(PossessControllerUVE(entityManager, secondController, secondPawn));
    EXPECT_EQ(FindPossessedPawnUVE(entityManager, firstController), kInvalidEntityUVE);
    EXPECT_EQ(FindPawnControllerUVE(entityManager, secondPawn), secondController);

    // Re-possessing a mutual pair is a no-op success.
    EXPECT_TRUE(PossessControllerUVE(entityManager, secondController, secondPawn));
    EXPECT_EQ(FindPawnControllerUVE(entityManager, secondPawn), secondController);

    // A self-driven entity (both components, possessing itself) is allowed.
    const EntityUVE self = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<ControllerComponentUVE>(self, ControllerComponentUVE{});
    entityManager.AddComponentUVE<PawnComponentUVE>(self, PawnComponentUVE{});
    EXPECT_TRUE(PossessControllerUVE(entityManager, self, self));
    EXPECT_EQ(FindPossessedPawnUVE(entityManager, self), self);
}

TEST_F(PawnUVETest, Possess_FailsClosedWithoutBothComponents) {
    const EntityUVE controller = MakeControllerUVE();
    const EntityUVE pawn = MakePawnUVE();
    const EntityUVE bare = entityManager.CreateEntityUVE();

    EXPECT_FALSE(PossessControllerUVE(entityManager, controller, bare));
    EXPECT_FALSE(PossessControllerUVE(entityManager, bare, pawn));
    EXPECT_FALSE(PossessControllerUVE(entityManager, bare, bare));
    EXPECT_EQ(entityManager.GetComponentUVE<ControllerComponentUVE>(controller).pawn,
              kInvalidEntityUVE);
    EXPECT_EQ(entityManager.GetComponentUVE<PawnComponentUVE>(pawn).controller, kInvalidEntityUVE);
}

TEST_F(PawnUVETest, Unpossess_ClearsBothSidesAndPurgesInput) {
    const EntityUVE controller = MakeControllerUVE();
    const EntityUVE pawn = MakePawnUVE();
    ASSERT_TRUE(PossessControllerUVE(entityManager, controller, pawn));
    RouteGameplayInputUVE(entityManager, SnapshotUVE());

    EXPECT_TRUE(UnpossessControllerUVE(entityManager, controller));
    EXPECT_EQ(entityManager.GetComponentUVE<ControllerComponentUVE>(controller).pawn,
              kInvalidEntityUVE);
    EXPECT_EQ(entityManager.GetComponentUVE<PawnComponentUVE>(pawn).controller, kInvalidEntityUVE);
    EXPECT_EQ(entityManager.GetComponentUVE<PawnComponentUVE>(pawn).input,
              Gameplay::GameplayInputUVE{});

    EXPECT_FALSE(UnpossessControllerUVE(entityManager, controller));
    EXPECT_FALSE(UnpossessControllerUVE(entityManager, entityManager.CreateEntityUVE()));
}

TEST_F(PawnUVETest, Route_DeliversToPlayerPawnsOnlyAndRepairsDanglingLinks) {
    const EntityUVE playerController = MakeControllerUVE(ControllerKindUVE::Player);
    const EntityUVE playerPawn = MakePawnUVE();
    const EntityUVE aiController = MakeControllerUVE(ControllerKindUVE::AI);
    const EntityUVE aiPawn = MakePawnUVE();
    ASSERT_TRUE(PossessControllerUVE(entityManager, playerController, playerPawn));
    ASSERT_TRUE(PossessControllerUVE(entityManager, aiController, aiPawn));

    RouteGameplayInputUVE(entityManager, SnapshotUVE());
    EXPECT_EQ(entityManager.GetComponentUVE<PawnComponentUVE>(playerPawn).input, SnapshotUVE());
    EXPECT_EQ(entityManager.GetComponentUVE<PawnComponentUVE>(aiPawn).input,
              Gameplay::GameplayInputUVE{});

    // A destroyed controller is repaired on the next route: link cleared, input purged.
    entityManager.DestroyEntityUVE(playerController);
    RouteGameplayInputUVE(entityManager, SnapshotUVE());
    EXPECT_EQ(entityManager.GetComponentUVE<PawnComponentUVE>(playerPawn).controller,
              kInvalidEntityUVE);
    EXPECT_EQ(entityManager.GetComponentUVE<PawnComponentUVE>(playerPawn).input,
              Gameplay::GameplayInputUVE{});

    // A removed pawn component is repaired from the controller side just the same.
    entityManager.RemoveComponentUVE<PawnComponentUVE>(aiPawn);
    RouteGameplayInputUVE(entityManager, SnapshotUVE());
    EXPECT_EQ(entityManager.GetComponentUVE<ControllerComponentUVE>(aiController).pawn,
              kInvalidEntityUVE);

    // A non-finite snapshot still repairs but never writes.
    const EntityUVE freshController = MakeControllerUVE();
    const EntityUVE freshPawn = MakePawnUVE();
    ASSERT_TRUE(PossessControllerUVE(entityManager, freshController, freshPawn));
    Gameplay::GameplayInputUVE broken = SnapshotUVE();
    broken.move.x = std::numeric_limits<float>::quiet_NaN();
    RouteGameplayInputUVE(entityManager, broken);
    EXPECT_EQ(entityManager.GetComponentUVE<PawnComponentUVE>(freshPawn).input,
              Gameplay::GameplayInputUVE{});
    EXPECT_EQ(FindPossessedPawnUVE(entityManager, freshController), freshPawn);
}

TEST_F(PawnUVETest, Finders_ReturnInvalidForAnythingButAMutualLink) {
    const EntityUVE controller = MakeControllerUVE();
    const EntityUVE pawn = MakePawnUVE();
    EXPECT_EQ(FindPossessedPawnUVE(entityManager, controller), kInvalidEntityUVE);
    EXPECT_EQ(FindPawnControllerUVE(entityManager, pawn), kInvalidEntityUVE);
    EXPECT_EQ(FindPossessedPawnUVE(entityManager, entityManager.CreateEntityUVE()),
              kInvalidEntityUVE);

    ASSERT_TRUE(PossessControllerUVE(entityManager, controller, pawn));
    EXPECT_EQ(FindPossessedPawnUVE(entityManager, controller), pawn);
    EXPECT_EQ(FindPawnControllerUVE(entityManager, pawn), controller);

    // A hand-broken one-sided link reads as no link at all.
    entityManager.GetComponentUVE<ControllerComponentUVE>(controller).pawn = MakePawnUVE();
    EXPECT_EQ(FindPossessedPawnUVE(entityManager, controller), kInvalidEntityUVE);
    EXPECT_EQ(FindPawnControllerUVE(entityManager, pawn), kInvalidEntityUVE);
}

TEST_F(PawnUVETest, Validity_RejectsBadKindsAndNonFiniteInput) {
    EXPECT_TRUE(IsControllerComponentValidUVE(ControllerComponentUVE{}));
    EXPECT_TRUE(IsPawnComponentValidUVE(PawnComponentUVE{}));

    ControllerComponentUVE badKind;
    badKind.kind = static_cast<ControllerKindUVE>(7U);
    EXPECT_FALSE(IsControllerComponentValidUVE(badKind));

    PawnComponentUVE bad;
    bad.input.move.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsPawnComponentValidUVE(bad));
    bad.input = {};
    bad.input.move.y = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsPawnComponentValidUVE(bad));
    bad.input = {};
    bad.input.rise = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsPawnComponentValidUVE(bad));
    bad.input = {};
    bad.input.lookPointer.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsPawnComponentValidUVE(bad));
    bad.input = {};
    bad.input.lookStick.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsPawnComponentValidUVE(bad));
}

TEST_F(PawnUVETest, Tracker_ReportsPossessAndUnpossessEdgesOnce) {
    PossessionLifecycleTrackerUVE tracker;
    EXPECT_EQ(tracker.GetActiveCountUVE(), 0U);
    const EntityUVE controller = MakeControllerUVE();
    const EntityUVE pawn = MakePawnUVE();

    ASSERT_TRUE(PossessControllerUVE(entityManager, controller, pawn));
    const PossessionLifecycleReportUVE formed = tracker.UpdateUVE(entityManager);
    EXPECT_EQ(formed.previousLinkCount, 0U);
    EXPECT_EQ(formed.currentLinkCount, 1U);
    ASSERT_EQ(formed.transitions.size(), 1U);
    EXPECT_EQ(formed.transitions.front().kind, PossessionTransitionKindUVE::Possessed);
    EXPECT_EQ(formed.transitions.front().controller, controller);
    EXPECT_EQ(formed.transitions.front().pawn, pawn);
    EXPECT_EQ(tracker.GetActiveCountUVE(), 1U);

    EXPECT_TRUE(tracker.UpdateUVE(entityManager).transitions.empty());

    ASSERT_TRUE(UnpossessControllerUVE(entityManager, controller));
    const PossessionLifecycleReportUVE broken = tracker.UpdateUVE(entityManager);
    EXPECT_EQ(broken.previousLinkCount, 1U);
    EXPECT_EQ(broken.currentLinkCount, 0U);
    ASSERT_EQ(broken.transitions.size(), 1U);
    EXPECT_EQ(broken.transitions.front().kind, PossessionTransitionKindUVE::Unpossessed);
    EXPECT_EQ(broken.transitions.front().controller, controller);
    EXPECT_EQ(broken.transitions.front().pawn, pawn);
}

TEST_F(PawnUVETest, Tracker_OrdersFormedBeforeBrokenInLinkOrder) {
    PossessionLifecycleTrackerUVE tracker;
    const EntityUVE firstController = MakeControllerUVE();
    const EntityUVE pawn = MakePawnUVE();
    const EntityUVE secondController = MakeControllerUVE();
    ASSERT_TRUE(PossessControllerUVE(entityManager, firstController, pawn));
    ASSERT_EQ(tracker.UpdateUVE(entityManager).transitions.size(), 1U);

    ASSERT_TRUE(PossessControllerUVE(entityManager, secondController, pawn));
    const PossessionLifecycleReportUVE stolen = tracker.UpdateUVE(entityManager);
    ASSERT_EQ(stolen.transitions.size(), 2U);
    EXPECT_EQ(stolen.transitions[0].kind, PossessionTransitionKindUVE::Possessed);
    EXPECT_EQ(stolen.transitions[0].controller, secondController);
    EXPECT_EQ(stolen.transitions[1].kind, PossessionTransitionKindUVE::Unpossessed);
    EXPECT_EQ(stolen.transitions[1].controller, firstController);
}

TEST_F(PawnUVETest, Tracker_ReportsUnpossessedWithLastKnownIdsWhenADriverDies) {
    PossessionLifecycleTrackerUVE tracker;
    const EntityUVE controller = MakeControllerUVE();
    const EntityUVE pawn = MakePawnUVE();
    ASSERT_TRUE(PossessControllerUVE(entityManager, controller, pawn));
    ASSERT_EQ(tracker.UpdateUVE(entityManager).transitions.size(), 1U);

    entityManager.DestroyEntityUVE(controller);
    const PossessionLifecycleReportUVE report = tracker.UpdateUVE(entityManager);
    EXPECT_EQ(report.currentLinkCount, 0U);
    ASSERT_EQ(report.transitions.size(), 1U);
    EXPECT_EQ(report.transitions.front().kind, PossessionTransitionKindUVE::Unpossessed);
    EXPECT_EQ(report.transitions.front().controller, controller);
    EXPECT_EQ(report.transitions.front().pawn, pawn);
    EXPECT_EQ(tracker.GetActiveCountUVE(), 0U);
}

TEST_F(PawnUVETest, Tracker_ResetDiscardsTheBaselineWithoutTransitions) {
    PossessionLifecycleTrackerUVE tracker;
    const EntityUVE controller = MakeControllerUVE();
    const EntityUVE pawn = MakePawnUVE();
    ASSERT_TRUE(PossessControllerUVE(entityManager, controller, pawn));
    ASSERT_EQ(tracker.UpdateUVE(entityManager).transitions.size(), 1U);

    tracker.ResetUVE();
    EXPECT_EQ(tracker.GetActiveCountUVE(), 0U);

    // Reset itself emits nothing, so the still-live link is new to the empty baseline.
    const PossessionLifecycleReportUVE rediscovered = tracker.UpdateUVE(entityManager);
    ASSERT_EQ(rediscovered.transitions.size(), 1U);
    EXPECT_EQ(rediscovered.transitions.front().kind, PossessionTransitionKindUVE::Possessed);
    EXPECT_EQ(rediscovered.transitions.front().controller, controller);

    ASSERT_TRUE(UnpossessControllerUVE(entityManager, controller));
    const PossessionLifecycleReportUVE broken = tracker.UpdateUVE(entityManager);
    ASSERT_EQ(broken.transitions.size(), 1U);
    EXPECT_EQ(broken.transitions.front().kind, PossessionTransitionKindUVE::Unpossessed);
}

TEST_F(PawnUVETest, Tracker_IgnoresOneSidedLinks) {
    PossessionLifecycleTrackerUVE tracker;
    const EntityUVE controller = MakeControllerUVE();
    const EntityUVE pawn = MakePawnUVE();
    entityManager.GetComponentUVE<PawnComponentUVE>(pawn).controller = controller;
    const PossessionLifecycleReportUVE report = tracker.UpdateUVE(entityManager);
    EXPECT_EQ(report.currentLinkCount, 0U);
    EXPECT_TRUE(report.transitions.empty());
}

TEST_F(PawnUVETest, Tracker_OrdersMultipleLinksByControllerThenPawn) {
    PossessionLifecycleTrackerUVE tracker;
    // Entities created second-first on purpose (lower ids), but possessed first-link-first:
    // the report order must follow the (controller, pawn) sort, not possession history.
    const EntityUVE secondController = MakeControllerUVE();
    const EntityUVE secondPawn = MakePawnUVE();
    const EntityUVE firstController = MakeControllerUVE();
    const EntityUVE firstPawn = MakePawnUVE();
    ASSERT_TRUE(PossessControllerUVE(entityManager, firstController, firstPawn));
    ASSERT_TRUE(PossessControllerUVE(entityManager, secondController, secondPawn));

    const PossessionLifecycleReportUVE report = tracker.UpdateUVE(entityManager);
    EXPECT_EQ(report.currentLinkCount, 2U);
    ASSERT_EQ(report.transitions.size(), 2U);
    EXPECT_EQ(report.transitions[0].controller, secondController);
    EXPECT_EQ(report.transitions[0].pawn, secondPawn);
    EXPECT_EQ(report.transitions[1].controller, firstController);
    EXPECT_EQ(report.transitions[1].pawn, firstPawn);
}

} // namespace
} // namespace UVE::Scene::Tests
