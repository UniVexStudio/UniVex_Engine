// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "pawn.h"

#include <limits>

#include <gtest/gtest.h>

#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"

namespace UVE::Scene::Tests {
namespace {

class PawnTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};

    Entity MakeController(const ControllerKind kind = ControllerKind::Player) {
        const Entity entity = entityManager.CreateEntity();
        ControllerComponent controller;
        controller.kind = kind;
        entityManager.AddComponent<ControllerComponent>(entity, controller);
        return entity;
    }

    Entity MakePawn() {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<PawnComponent>(entity, PawnComponent{});
        return entity;
    }

    Gameplay::GameplayInput Snapshot() {
        Gameplay::GameplayInput snapshot;
        snapshot.move = {1.0F, 2.0F, 3.0F};
        snapshot.rise = 4.0F;
        snapshot.jumpPressed = true;
        snapshot.lookPointer = {5.0F, 6.0F};
        snapshot.lookStick = {7.0F, 8.0F};
        snapshot.interactPressed = true;
        return snapshot;
    }
};

TEST_F(PawnTest, Possess_LinksBothSidesAndStealsExclusively) {
    const Entity firstController = MakeController();
    const Entity secondController = MakeController(ControllerKind::AI);
    const Entity firstPawn = MakePawn();
    const Entity secondPawn = MakePawn();

    ASSERT_TRUE(PossessController(entityManager, firstController, firstPawn));
    EXPECT_EQ(FindPossessedPawn(entityManager, firstController), firstPawn);
    EXPECT_EQ(FindPawnController(entityManager, firstPawn), firstController);
    RouteGameplayInput(entityManager, Snapshot());
    ASSERT_EQ(entityManager.GetComponent<PawnComponent>(firstPawn).input, Snapshot());

    // Stealing from the controller side unlinks the previous pawn and purges its input.
    ASSERT_TRUE(PossessController(entityManager, firstController, secondPawn));
    EXPECT_EQ(FindPossessedPawn(entityManager, firstController), secondPawn);
    EXPECT_EQ(FindPawnController(entityManager, firstPawn), kInvalidEntity);
    EXPECT_EQ(entityManager.GetComponent<PawnComponent>(firstPawn).input,
              Gameplay::GameplayInput{});

    // Stealing from the pawn side unlinks the previous controller.
    ASSERT_TRUE(PossessController(entityManager, secondController, secondPawn));
    EXPECT_EQ(FindPossessedPawn(entityManager, firstController), kInvalidEntity);
    EXPECT_EQ(FindPawnController(entityManager, secondPawn), secondController);

    // Re-possessing a mutual pair is a no-op success.
    EXPECT_TRUE(PossessController(entityManager, secondController, secondPawn));
    EXPECT_EQ(FindPawnController(entityManager, secondPawn), secondController);

    // A self-driven entity (both components, possessing itself) is allowed.
    const Entity self = entityManager.CreateEntity();
    entityManager.AddComponent<ControllerComponent>(self, ControllerComponent{});
    entityManager.AddComponent<PawnComponent>(self, PawnComponent{});
    EXPECT_TRUE(PossessController(entityManager, self, self));
    EXPECT_EQ(FindPossessedPawn(entityManager, self), self);
}

TEST_F(PawnTest, Possess_FailsClosedWithoutBothComponents) {
    const Entity controller = MakeController();
    const Entity pawn = MakePawn();
    const Entity bare = entityManager.CreateEntity();

    EXPECT_FALSE(PossessController(entityManager, controller, bare));
    EXPECT_FALSE(PossessController(entityManager, bare, pawn));
    EXPECT_FALSE(PossessController(entityManager, bare, bare));
    EXPECT_EQ(entityManager.GetComponent<ControllerComponent>(controller).pawn,
              kInvalidEntity);
    EXPECT_EQ(entityManager.GetComponent<PawnComponent>(pawn).controller, kInvalidEntity);
}

TEST_F(PawnTest, Unpossess_ClearsBothSidesAndPurgesInput) {
    const Entity controller = MakeController();
    const Entity pawn = MakePawn();
    ASSERT_TRUE(PossessController(entityManager, controller, pawn));
    RouteGameplayInput(entityManager, Snapshot());

    EXPECT_TRUE(UnpossessController(entityManager, controller));
    EXPECT_EQ(entityManager.GetComponent<ControllerComponent>(controller).pawn,
              kInvalidEntity);
    EXPECT_EQ(entityManager.GetComponent<PawnComponent>(pawn).controller, kInvalidEntity);
    EXPECT_EQ(entityManager.GetComponent<PawnComponent>(pawn).input,
              Gameplay::GameplayInput{});

    EXPECT_FALSE(UnpossessController(entityManager, controller));
    EXPECT_FALSE(UnpossessController(entityManager, entityManager.CreateEntity()));
}

TEST_F(PawnTest, Route_DeliversToPlayerPawnsOnlyAndRepairsDanglingLinks) {
    const Entity playerController = MakeController(ControllerKind::Player);
    const Entity playerPawn = MakePawn();
    const Entity aiController = MakeController(ControllerKind::AI);
    const Entity aiPawn = MakePawn();
    ASSERT_TRUE(PossessController(entityManager, playerController, playerPawn));
    ASSERT_TRUE(PossessController(entityManager, aiController, aiPawn));

    RouteGameplayInput(entityManager, Snapshot());
    EXPECT_EQ(entityManager.GetComponent<PawnComponent>(playerPawn).input, Snapshot());
    EXPECT_EQ(entityManager.GetComponent<PawnComponent>(aiPawn).input,
              Gameplay::GameplayInput{});

    // A destroyed controller is repaired on the next route: link cleared, input purged.
    entityManager.DestroyEntity(playerController);
    RouteGameplayInput(entityManager, Snapshot());
    EXPECT_EQ(entityManager.GetComponent<PawnComponent>(playerPawn).controller,
              kInvalidEntity);
    EXPECT_EQ(entityManager.GetComponent<PawnComponent>(playerPawn).input,
              Gameplay::GameplayInput{});

    // A removed pawn component is repaired from the controller side just the same.
    entityManager.RemoveComponent<PawnComponent>(aiPawn);
    RouteGameplayInput(entityManager, Snapshot());
    EXPECT_EQ(entityManager.GetComponent<ControllerComponent>(aiController).pawn,
              kInvalidEntity);

    // A non-finite snapshot still repairs but never writes.
    const Entity freshController = MakeController();
    const Entity freshPawn = MakePawn();
    ASSERT_TRUE(PossessController(entityManager, freshController, freshPawn));
    Gameplay::GameplayInput broken = Snapshot();
    broken.move.x = std::numeric_limits<float>::quiet_NaN();
    RouteGameplayInput(entityManager, broken);
    EXPECT_EQ(entityManager.GetComponent<PawnComponent>(freshPawn).input,
              Gameplay::GameplayInput{});
    EXPECT_EQ(FindPossessedPawn(entityManager, freshController), freshPawn);
}

TEST_F(PawnTest, Finders_ReturnInvalidForAnythingButAMutualLink) {
    const Entity controller = MakeController();
    const Entity pawn = MakePawn();
    EXPECT_EQ(FindPossessedPawn(entityManager, controller), kInvalidEntity);
    EXPECT_EQ(FindPawnController(entityManager, pawn), kInvalidEntity);
    EXPECT_EQ(FindPossessedPawn(entityManager, entityManager.CreateEntity()),
              kInvalidEntity);

    ASSERT_TRUE(PossessController(entityManager, controller, pawn));
    EXPECT_EQ(FindPossessedPawn(entityManager, controller), pawn);
    EXPECT_EQ(FindPawnController(entityManager, pawn), controller);

    // A hand-broken one-sided link reads as no link at all.
    entityManager.GetComponent<ControllerComponent>(controller).pawn = MakePawn();
    EXPECT_EQ(FindPossessedPawn(entityManager, controller), kInvalidEntity);
    EXPECT_EQ(FindPawnController(entityManager, pawn), kInvalidEntity);
}

TEST_F(PawnTest, Validity_RejectsBadKindsAndNonFiniteInput) {
    EXPECT_TRUE(IsControllerComponentValid(ControllerComponent{}));
    EXPECT_TRUE(IsPawnComponentValid(PawnComponent{}));

    ControllerComponent badKind;
    badKind.kind = static_cast<ControllerKind>(7U);
    EXPECT_FALSE(IsControllerComponentValid(badKind));

    PawnComponent bad;
    bad.input.move.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsPawnComponentValid(bad));
    bad.input = {};
    bad.input.move.y = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsPawnComponentValid(bad));
    bad.input = {};
    bad.input.rise = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsPawnComponentValid(bad));
    bad.input = {};
    bad.input.lookPointer.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsPawnComponentValid(bad));
    bad.input = {};
    bad.input.lookStick.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsPawnComponentValid(bad));
}

TEST_F(PawnTest, Tracker_ReportsPossessAndUnpossessEdgesOnce) {
    PossessionLifecycleTracker tracker;
    EXPECT_EQ(tracker.GetActiveCount(), 0U);
    const Entity controller = MakeController();
    const Entity pawn = MakePawn();

    ASSERT_TRUE(PossessController(entityManager, controller, pawn));
    const PossessionLifecycleReport formed = tracker.Update(entityManager);
    EXPECT_EQ(formed.previousLinkCount, 0U);
    EXPECT_EQ(formed.currentLinkCount, 1U);
    ASSERT_EQ(formed.transitions.size(), 1U);
    EXPECT_EQ(formed.transitions.front().kind, PossessionTransitionKind::Possessed);
    EXPECT_EQ(formed.transitions.front().controller, controller);
    EXPECT_EQ(formed.transitions.front().pawn, pawn);
    EXPECT_EQ(tracker.GetActiveCount(), 1U);

    EXPECT_TRUE(tracker.Update(entityManager).transitions.empty());

    ASSERT_TRUE(UnpossessController(entityManager, controller));
    const PossessionLifecycleReport broken = tracker.Update(entityManager);
    EXPECT_EQ(broken.previousLinkCount, 1U);
    EXPECT_EQ(broken.currentLinkCount, 0U);
    ASSERT_EQ(broken.transitions.size(), 1U);
    EXPECT_EQ(broken.transitions.front().kind, PossessionTransitionKind::Unpossessed);
    EXPECT_EQ(broken.transitions.front().controller, controller);
    EXPECT_EQ(broken.transitions.front().pawn, pawn);
}

TEST_F(PawnTest, Tracker_OrdersFormedBeforeBrokenInLinkOrder) {
    PossessionLifecycleTracker tracker;
    const Entity firstController = MakeController();
    const Entity pawn = MakePawn();
    const Entity secondController = MakeController();
    ASSERT_TRUE(PossessController(entityManager, firstController, pawn));
    ASSERT_EQ(tracker.Update(entityManager).transitions.size(), 1U);

    ASSERT_TRUE(PossessController(entityManager, secondController, pawn));
    const PossessionLifecycleReport stolen = tracker.Update(entityManager);
    ASSERT_EQ(stolen.transitions.size(), 2U);
    EXPECT_EQ(stolen.transitions[0].kind, PossessionTransitionKind::Possessed);
    EXPECT_EQ(stolen.transitions[0].controller, secondController);
    EXPECT_EQ(stolen.transitions[1].kind, PossessionTransitionKind::Unpossessed);
    EXPECT_EQ(stolen.transitions[1].controller, firstController);
}

TEST_F(PawnTest, Tracker_ReportsUnpossessedWithLastKnownIdsWhenADriverDies) {
    PossessionLifecycleTracker tracker;
    const Entity controller = MakeController();
    const Entity pawn = MakePawn();
    ASSERT_TRUE(PossessController(entityManager, controller, pawn));
    ASSERT_EQ(tracker.Update(entityManager).transitions.size(), 1U);

    entityManager.DestroyEntity(controller);
    const PossessionLifecycleReport report = tracker.Update(entityManager);
    EXPECT_EQ(report.currentLinkCount, 0U);
    ASSERT_EQ(report.transitions.size(), 1U);
    EXPECT_EQ(report.transitions.front().kind, PossessionTransitionKind::Unpossessed);
    EXPECT_EQ(report.transitions.front().controller, controller);
    EXPECT_EQ(report.transitions.front().pawn, pawn);
    EXPECT_EQ(tracker.GetActiveCount(), 0U);
}

TEST_F(PawnTest, Tracker_ResetDiscardsTheBaselineWithoutTransitions) {
    PossessionLifecycleTracker tracker;
    const Entity controller = MakeController();
    const Entity pawn = MakePawn();
    ASSERT_TRUE(PossessController(entityManager, controller, pawn));
    ASSERT_EQ(tracker.Update(entityManager).transitions.size(), 1U);

    tracker.Reset();
    EXPECT_EQ(tracker.GetActiveCount(), 0U);

    // Reset itself emits nothing, so the still-live link is new to the empty baseline.
    const PossessionLifecycleReport rediscovered = tracker.Update(entityManager);
    ASSERT_EQ(rediscovered.transitions.size(), 1U);
    EXPECT_EQ(rediscovered.transitions.front().kind, PossessionTransitionKind::Possessed);
    EXPECT_EQ(rediscovered.transitions.front().controller, controller);

    ASSERT_TRUE(UnpossessController(entityManager, controller));
    const PossessionLifecycleReport broken = tracker.Update(entityManager);
    ASSERT_EQ(broken.transitions.size(), 1U);
    EXPECT_EQ(broken.transitions.front().kind, PossessionTransitionKind::Unpossessed);
}

TEST_F(PawnTest, Tracker_IgnoresOneSidedLinks) {
    PossessionLifecycleTracker tracker;
    const Entity controller = MakeController();
    const Entity pawn = MakePawn();
    entityManager.GetComponent<PawnComponent>(pawn).controller = controller;
    const PossessionLifecycleReport report = tracker.Update(entityManager);
    EXPECT_EQ(report.currentLinkCount, 0U);
    EXPECT_TRUE(report.transitions.empty());
}

TEST_F(PawnTest, Tracker_OrdersMultipleLinksByControllerThenPawn) {
    PossessionLifecycleTracker tracker;
    // Entities created second-first on purpose (lower ids), but possessed first-link-first:
    // the report order must follow the (controller, pawn) sort, not possession history.
    const Entity secondController = MakeController();
    const Entity secondPawn = MakePawn();
    const Entity firstController = MakeController();
    const Entity firstPawn = MakePawn();
    ASSERT_TRUE(PossessController(entityManager, firstController, firstPawn));
    ASSERT_TRUE(PossessController(entityManager, secondController, secondPawn));

    const PossessionLifecycleReport report = tracker.Update(entityManager);
    EXPECT_EQ(report.currentLinkCount, 2U);
    ASSERT_EQ(report.transitions.size(), 2U);
    EXPECT_EQ(report.transitions[0].controller, secondController);
    EXPECT_EQ(report.transitions[0].pawn, secondPawn);
    EXPECT_EQ(report.transitions[1].controller, firstController);
    EXPECT_EQ(report.transitions[1].pawn, firstPawn);
}

} // namespace
} // namespace UVE::Scene::Tests
