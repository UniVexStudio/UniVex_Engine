// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/gameplay/pawn_uve.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsFiniteInputUVE(const Gameplay::GameplayInputUVE& input) noexcept {
    return std::isfinite(input.move.x) && std::isfinite(input.move.y) && std::isfinite(input.move.z) &&
           std::isfinite(input.rise) && std::isfinite(input.lookPointer.x) &&
           std::isfinite(input.lookPointer.y) && std::isfinite(input.lookStick.x) &&
           std::isfinite(input.lookStick.y);
}

[[nodiscard]] bool IsEarlierUVE(const EntityUVE lhs, const EntityUVE rhs) noexcept {
    return lhs.index != rhs.index ? lhs.index < rhs.index : lhs.generation < rhs.generation;
}

[[nodiscard]] bool IsEarlierLinkUVE(const PossessionLinkUVE& lhs,
                                    const PossessionLinkUVE& rhs) noexcept {
    if (lhs.controller.index != rhs.controller.index) {
        return lhs.controller.index < rhs.controller.index;
    }
    if (lhs.controller.generation != rhs.controller.generation) {
        return lhs.controller.generation < rhs.controller.generation;
    }
    return IsEarlierUVE(lhs.pawn, rhs.pawn);
}

// Links outlive their targets: a destroyed controller leaves a stale handle behind, and the
// typed HasComponentUVE<T>() asserts liveness in debug builds. Every query below that follows a
// link - rather than a handle proven live by iteration - goes through this guard.
template <typename TComponent>
[[nodiscard]] bool HasLiveComponentUVE(const IEntityManagerUVE& entityManager,
                                       const EntityUVE entity) {
    return entity != kInvalidEntityUVE && entityManager.IsAliveUVE(entity) &&
           entityManager.HasComponentUVE<TComponent>(entity);
}

} // namespace

bool IsControllerComponentValidUVE(const ControllerComponentUVE& value) noexcept {
    return static_cast<std::uint8_t>(value.kind) <=
           static_cast<std::uint8_t>(ControllerKindUVE::AI);
}

bool IsPawnComponentValidUVE(const PawnComponentUVE& value) noexcept {
    return IsFiniteInputUVE(value.input);
}

EntityUVE FindPossessedPawnUVE(const IEntityManagerUVE& entityManager,
                               const EntityUVE controllerEntity) {
    if (!HasLiveComponentUVE<ControllerComponentUVE>(entityManager, controllerEntity)) {
        return kInvalidEntityUVE;
    }
    const EntityUVE pawn =
        entityManager.GetComponentUVE<ControllerComponentUVE>(controllerEntity).pawn;
    if (!HasLiveComponentUVE<PawnComponentUVE>(entityManager, pawn) ||
        entityManager.GetComponentUVE<PawnComponentUVE>(pawn).controller != controllerEntity) {
        return kInvalidEntityUVE;
    }
    return pawn;
}

EntityUVE FindPawnControllerUVE(const IEntityManagerUVE& entityManager, const EntityUVE pawnEntity) {
    if (!HasLiveComponentUVE<PawnComponentUVE>(entityManager, pawnEntity)) {
        return kInvalidEntityUVE;
    }
    const EntityUVE controller =
        entityManager.GetComponentUVE<PawnComponentUVE>(pawnEntity).controller;
    if (!HasLiveComponentUVE<ControllerComponentUVE>(entityManager, controller) ||
        entityManager.GetComponentUVE<ControllerComponentUVE>(controller).pawn != pawnEntity) {
        return kInvalidEntityUVE;
    }
    return controller;
}

bool PossessControllerUVE(IEntityManagerUVE& entityManager, const EntityUVE controllerEntity,
                          const EntityUVE pawnEntity) {
    if (!HasLiveComponentUVE<ControllerComponentUVE>(entityManager, controllerEntity) ||
        !HasLiveComponentUVE<PawnComponentUVE>(entityManager, pawnEntity)) {
        return false;
    }
    ControllerComponentUVE& controller =
        entityManager.GetComponentUVE<ControllerComponentUVE>(controllerEntity);
    PawnComponentUVE& pawn = entityManager.GetComponentUVE<PawnComponentUVE>(pawnEntity);
    if (controller.pawn == pawnEntity && pawn.controller == controllerEntity) {
        return true;
    }
    if (HasLiveComponentUVE<PawnComponentUVE>(entityManager, controller.pawn)) {
        PawnComponentUVE& previous = entityManager.GetComponentUVE<PawnComponentUVE>(controller.pawn);
        if (previous.controller == controllerEntity) {
            previous.controller = kInvalidEntityUVE;
            previous.input = {};
        }
    }
    if (HasLiveComponentUVE<ControllerComponentUVE>(entityManager, pawn.controller)) {
        ControllerComponentUVE& previous =
            entityManager.GetComponentUVE<ControllerComponentUVE>(pawn.controller);
        if (previous.pawn == pawnEntity) {
            previous.pawn = kInvalidEntityUVE;
        }
    }
    controller.pawn = pawnEntity;
    pawn.controller = controllerEntity;
    return true;
}

bool UnpossessControllerUVE(IEntityManagerUVE& entityManager, const EntityUVE controllerEntity) {
    if (!HasLiveComponentUVE<ControllerComponentUVE>(entityManager, controllerEntity)) {
        return false;
    }
    ControllerComponentUVE& controller =
        entityManager.GetComponentUVE<ControllerComponentUVE>(controllerEntity);
    if (controller.pawn == kInvalidEntityUVE) {
        return false;
    }
    const EntityUVE pawnEntity = controller.pawn;
    controller.pawn = kInvalidEntityUVE;
    if (HasLiveComponentUVE<PawnComponentUVE>(entityManager, pawnEntity)) {
        PawnComponentUVE& pawn = entityManager.GetComponentUVE<PawnComponentUVE>(pawnEntity);
        if (pawn.controller == controllerEntity) {
            pawn.controller = kInvalidEntityUVE;
            pawn.input = {};
        }
    }
    return true;
}

void RouteGameplayInputUVE(IEntityManagerUVE& entityManager,
                           const Gameplay::GameplayInputUVE& snapshot) {
    const bool writable = IsFiniteInputUVE(snapshot);
    std::vector<EntityUVE> controllers;
    entityManager.ForEachUVE<ControllerComponentUVE>(
        [&controllers](const EntityUVE entity, const ControllerComponentUVE&) {
            controllers.push_back(entity);
        });
    std::sort(controllers.begin(), controllers.end(), IsEarlierUVE);
    for (const EntityUVE controllerEntity : controllers) {
        ControllerComponentUVE& controller =
            entityManager.GetComponentUVE<ControllerComponentUVE>(controllerEntity);
        if (controller.pawn == kInvalidEntityUVE) {
            continue;
        }
        const EntityUVE pawnEntity = controller.pawn;
        if (!HasLiveComponentUVE<PawnComponentUVE>(entityManager, pawnEntity) ||
            entityManager.GetComponentUVE<PawnComponentUVE>(pawnEntity).controller != controllerEntity) {
            controller.pawn = kInvalidEntityUVE;
            continue;
        }
        if (writable && controller.kind == ControllerKindUVE::Player) {
            entityManager.GetComponentUVE<PawnComponentUVE>(pawnEntity).input = snapshot;
        }
    }
    std::vector<EntityUVE> pawns;
    entityManager.ForEachUVE<PawnComponentUVE>([&pawns](const EntityUVE entity, const PawnComponentUVE&) {
        pawns.push_back(entity);
    });
    std::sort(pawns.begin(), pawns.end(), IsEarlierUVE);
    for (const EntityUVE pawnEntity : pawns) {
        PawnComponentUVE& pawn = entityManager.GetComponentUVE<PawnComponentUVE>(pawnEntity);
        if (pawn.controller == kInvalidEntityUVE) {
            continue;
        }
        const EntityUVE controllerEntity = pawn.controller;
        if (!HasLiveComponentUVE<ControllerComponentUVE>(entityManager, controllerEntity) ||
            entityManager.GetComponentUVE<ControllerComponentUVE>(controllerEntity).pawn != pawnEntity) {
            pawn.controller = kInvalidEntityUVE;
            pawn.input = {};
        }
    }
}

PossessionLifecycleReportUVE PossessionLifecycleTrackerUVE::UpdateUVE(
    IEntityManagerUVE& entityManager) {
    PossessionLifecycleReportUVE report;
    report.previousLinkCount = m_activeLinks.size();
    std::vector<PossessionLinkUVE> current;
    entityManager.ForEachUVE<ControllerComponentUVE>(
        [&entityManager, &current](const EntityUVE controller, const ControllerComponentUVE&) {
            const EntityUVE pawn = FindPossessedPawnUVE(entityManager, controller);
            if (pawn != kInvalidEntityUVE) {
                current.push_back(PossessionLinkUVE{controller, pawn});
            }
        });
    std::sort(current.begin(), current.end(), IsEarlierLinkUVE);
    report.currentLinkCount = current.size();
    std::vector<PossessionLinkUVE> formed;
    std::vector<PossessionLinkUVE> broken;
    std::set_difference(current.begin(), current.end(), m_activeLinks.begin(), m_activeLinks.end(),
                         std::back_inserter(formed), IsEarlierLinkUVE);
    std::set_difference(m_activeLinks.begin(), m_activeLinks.end(), current.begin(), current.end(),
                         std::back_inserter(broken), IsEarlierLinkUVE);
    for (const PossessionLinkUVE& link : formed) {
        report.transitions.push_back(
            PossessionTransitionUVE{PossessionTransitionKindUVE::Possessed, link.controller, link.pawn});
    }
    for (const PossessionLinkUVE& link : broken) {
        report.transitions.push_back(PossessionTransitionUVE{
            PossessionTransitionKindUVE::Unpossessed, link.controller, link.pawn});
    }
    m_activeLinks = std::move(current);
    return report;
}

void PossessionLifecycleTrackerUVE::ResetUVE() noexcept {
    m_activeLinks.clear();
}

} // namespace UVE::Scene
