// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "pawn.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsFiniteInput(const Gameplay::GameplayInput& input) noexcept {
    return std::isfinite(input.move.x) && std::isfinite(input.move.y) && std::isfinite(input.move.z) &&
           std::isfinite(input.rise) && std::isfinite(input.lookPointer.x) &&
           std::isfinite(input.lookPointer.y) && std::isfinite(input.lookStick.x) &&
           std::isfinite(input.lookStick.y);
}

[[nodiscard]] bool IsEarlier(const Entity lhs, const Entity rhs) noexcept {
    return lhs.index != rhs.index ? lhs.index < rhs.index : lhs.generation < rhs.generation;
}

[[nodiscard]] bool IsEarlierLink(const PossessionLink& lhs,
                                    const PossessionLink& rhs) noexcept {
    if (lhs.controller.index != rhs.controller.index) {
        return lhs.controller.index < rhs.controller.index;
    }
    if (lhs.controller.generation != rhs.controller.generation) {
        return lhs.controller.generation < rhs.controller.generation;
    }
    return IsEarlier(lhs.pawn, rhs.pawn);
}

// Links outlive their targets: a destroyed controller leaves a stale handle behind, and the
// typed HasComponent<T>() asserts liveness in debug builds. Every query below that follows a
// link - rather than a handle proven live by iteration - goes through this guard.
template <typename TComponent>
[[nodiscard]] bool HasLiveComponent(const IEntityManager& entityManager,
                                       const Entity entity) {
    return entity != kInvalidEntity && entityManager.IsAlive(entity) &&
           entityManager.HasComponent<TComponent>(entity);
}

} // namespace

bool IsControllerComponentValid(const ControllerComponent& value) noexcept {
    return static_cast<std::uint8_t>(value.kind) <=
           static_cast<std::uint8_t>(ControllerKind::AI);
}

bool IsPawnComponentValid(const PawnComponent& value) noexcept {
    return IsFiniteInput(value.input);
}

Entity FindPossessedPawn(const IEntityManager& entityManager,
                               const Entity controllerEntity) {
    if (!HasLiveComponent<ControllerComponent>(entityManager, controllerEntity)) {
        return kInvalidEntity;
    }
    const Entity pawn =
        entityManager.GetComponent<ControllerComponent>(controllerEntity).pawn;
    if (!HasLiveComponent<PawnComponent>(entityManager, pawn) ||
        entityManager.GetComponent<PawnComponent>(pawn).controller != controllerEntity) {
        return kInvalidEntity;
    }
    return pawn;
}

Entity FindPawnController(const IEntityManager& entityManager, const Entity pawnEntity) {
    if (!HasLiveComponent<PawnComponent>(entityManager, pawnEntity)) {
        return kInvalidEntity;
    }
    const Entity controller =
        entityManager.GetComponent<PawnComponent>(pawnEntity).controller;
    if (!HasLiveComponent<ControllerComponent>(entityManager, controller) ||
        entityManager.GetComponent<ControllerComponent>(controller).pawn != pawnEntity) {
        return kInvalidEntity;
    }
    return controller;
}

bool PossessController(IEntityManager& entityManager, const Entity controllerEntity,
                          const Entity pawnEntity) {
    if (!HasLiveComponent<ControllerComponent>(entityManager, controllerEntity) ||
        !HasLiveComponent<PawnComponent>(entityManager, pawnEntity)) {
        return false;
    }
    ControllerComponent& controller =
        entityManager.GetComponent<ControllerComponent>(controllerEntity);
    PawnComponent& pawn = entityManager.GetComponent<PawnComponent>(pawnEntity);
    if (controller.pawn == pawnEntity && pawn.controller == controllerEntity) {
        return true;
    }
    if (HasLiveComponent<PawnComponent>(entityManager, controller.pawn)) {
        PawnComponent& previous = entityManager.GetComponent<PawnComponent>(controller.pawn);
        if (previous.controller == controllerEntity) {
            previous.controller = kInvalidEntity;
            previous.input = {};
        }
    }
    if (HasLiveComponent<ControllerComponent>(entityManager, pawn.controller)) {
        ControllerComponent& previous =
            entityManager.GetComponent<ControllerComponent>(pawn.controller);
        if (previous.pawn == pawnEntity) {
            previous.pawn = kInvalidEntity;
        }
    }
    controller.pawn = pawnEntity;
    pawn.controller = controllerEntity;
    return true;
}

bool UnpossessController(IEntityManager& entityManager, const Entity controllerEntity) {
    if (!HasLiveComponent<ControllerComponent>(entityManager, controllerEntity)) {
        return false;
    }
    ControllerComponent& controller =
        entityManager.GetComponent<ControllerComponent>(controllerEntity);
    if (controller.pawn == kInvalidEntity) {
        return false;
    }
    const Entity pawnEntity = controller.pawn;
    controller.pawn = kInvalidEntity;
    if (HasLiveComponent<PawnComponent>(entityManager, pawnEntity)) {
        PawnComponent& pawn = entityManager.GetComponent<PawnComponent>(pawnEntity);
        if (pawn.controller == controllerEntity) {
            pawn.controller = kInvalidEntity;
            pawn.input = {};
        }
    }
    return true;
}

void RouteGameplayInput(IEntityManager& entityManager,
                           const Gameplay::GameplayInput& snapshot) {
    const bool writable = IsFiniteInput(snapshot);
    std::vector<Entity> controllers;
    entityManager.ForEach<ControllerComponent>(
        [&controllers](const Entity entity, const ControllerComponent&) {
            controllers.push_back(entity);
        });
    std::sort(controllers.begin(), controllers.end(), IsEarlier);
    for (const Entity controllerEntity : controllers) {
        ControllerComponent& controller =
            entityManager.GetComponent<ControllerComponent>(controllerEntity);
        if (controller.pawn == kInvalidEntity) {
            continue;
        }
        const Entity pawnEntity = controller.pawn;
        if (!HasLiveComponent<PawnComponent>(entityManager, pawnEntity) ||
            entityManager.GetComponent<PawnComponent>(pawnEntity).controller != controllerEntity) {
            controller.pawn = kInvalidEntity;
            continue;
        }
        if (writable && controller.kind == ControllerKind::Player) {
            entityManager.GetComponent<PawnComponent>(pawnEntity).input = snapshot;
        }
    }
    std::vector<Entity> pawns;
    entityManager.ForEach<PawnComponent>([&pawns](const Entity entity, const PawnComponent&) {
        pawns.push_back(entity);
    });
    std::sort(pawns.begin(), pawns.end(), IsEarlier);
    for (const Entity pawnEntity : pawns) {
        PawnComponent& pawn = entityManager.GetComponent<PawnComponent>(pawnEntity);
        if (pawn.controller == kInvalidEntity) {
            continue;
        }
        const Entity controllerEntity = pawn.controller;
        if (!HasLiveComponent<ControllerComponent>(entityManager, controllerEntity) ||
            entityManager.GetComponent<ControllerComponent>(controllerEntity).pawn != pawnEntity) {
            pawn.controller = kInvalidEntity;
            pawn.input = {};
        }
    }
}

PossessionLifecycleReport PossessionLifecycleTracker::Update(
    IEntityManager& entityManager) {
    PossessionLifecycleReport report;
    report.previousLinkCount = m_activeLinks.size();
    std::vector<PossessionLink> current;
    entityManager.ForEach<ControllerComponent>(
        [&entityManager, &current](const Entity controller, const ControllerComponent&) {
            const Entity pawn = FindPossessedPawn(entityManager, controller);
            if (pawn != kInvalidEntity) {
                current.push_back(PossessionLink{controller, pawn});
            }
        });
    std::sort(current.begin(), current.end(), IsEarlierLink);
    report.currentLinkCount = current.size();
    std::vector<PossessionLink> formed;
    std::vector<PossessionLink> broken;
    std::set_difference(current.begin(), current.end(), m_activeLinks.begin(), m_activeLinks.end(),
                         std::back_inserter(formed), IsEarlierLink);
    std::set_difference(m_activeLinks.begin(), m_activeLinks.end(), current.begin(), current.end(),
                         std::back_inserter(broken), IsEarlierLink);
    for (const PossessionLink& link : formed) {
        report.transitions.push_back(
            PossessionTransition{PossessionTransitionKind::Possessed, link.controller, link.pawn});
    }
    for (const PossessionLink& link : broken) {
        report.transitions.push_back(PossessionTransition{
            PossessionTransitionKind::Unpossessed, link.controller, link.pawn});
    }
    m_activeLinks = std::move(current);
    return report;
}

void PossessionLifecycleTracker::Reset() noexcept {
    m_activeLinks.clear();
}

} // namespace UVE::Scene
