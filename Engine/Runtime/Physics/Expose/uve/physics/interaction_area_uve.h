// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>

#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Physics {

/// What one interaction scan produced, so a caller and a test can both see the whole frame's
/// accounting instead of reading it back out of every area's runtime state.
///
/// The counts are exhaustive on purpose: `areaCount` counts every area component visited (a
/// disabled or malformed one is still visited and still cleared), `refreshedAreaCount` counts the
/// ones that passed the gates and were actually evaluated, and `truncatedAreaCount` counts the
/// areas whose authored `maximumCandidates` cut their list short - the overflow is reported here
/// AND on the area itself, never silently swallowed.
struct InteractionAreaScanResultUVE final {
    /// Eligible interactors this frame: a character controller carrying a valid collider and a
    /// world transform. This is the candidate set every area was evaluated against.
    std::size_t interactorCount = 0U;
    /// Every InteractionArea3DComponentUVE the scan visited.
    std::size_t areaCount = 0U;
    /// The areas that passed `enabled` + validator + world transform and were evaluated.
    std::size_t refreshedAreaCount = 0U;
    /// Areas whose authored `maximumCandidates` filled before their overlaps ran out.
    std::size_t truncatedAreaCount = 0U;
    /// The primary interactor this frame - the first in (index, generation) order - or
    /// kInvalidEntityUVE when nothing in the scene can interact.
    Scene::EntityUVE primaryInteractor = Scene::kInvalidEntityUVE;
    /// The single area marked focused this frame (nearest to the primary interactor, ties broken
    /// by (index, generation)), or kInvalidEntityUVE when nothing is focused.
    Scene::EntityUVE focusedArea = Scene::kInvalidEntityUVE;

    [[nodiscard]] bool HasPrimaryInteractorUVE() const noexcept {
        return primaryInteractor != Scene::kInvalidEntityUVE;
    }

    [[nodiscard]] bool HasFocusedAreaUVE() const noexcept {
        return focusedArea != Scene::kInvalidEntityUVE;
    }
};

/// Refreshes every InteractionArea3D's runtime state for one frame: which character controllers
/// are inside it, whether that list was cut short, and which single area the primary interactor
/// is focused on.
///
/// This is the whole per-frame contract, in one callable place, because the alternative is what
/// this engine had before: the same three passes living inside EngineCoreUVE's tick where nothing
/// without an EngineCoreUVE could exercise them. The engine core still owns WHEN this runs (the
/// fixed-step order is a tick decision); what runs is here, and it is testable against a plain
/// entity manager.
///
/// The contract, pass by pass:
///
///  * **Interactors** are character-controller entities that also carry a world transform and a
///    collider the collider validator accepts (a zero collision layer can never interact). Their
///    world pose and the collider's shape-aware local half extents are snapshotted once, so the
///    second pass is pure and no ECS iteration is held open across another one. A degenerate world
///    rotation falls back to identity rather than dropping the interactor.
///
///  * **Every area fails closed.** Any gate that stops an area participating this frame - disabled,
///    invalid component, no world transform - clears its runtime state in full, so a stale
///    interactor list or a stale focus flag can never outlive the reason it existed.
///
///  * **Overlap is exact and symmetric.** Two oriented boxes (world pose + authored half extents;
///    the area's own world scale is deliberately not applied, the ColliderComponentUVE /
///    AreaComponentUVE convention) must overlap by the 15-axis test - touching boundaries do not
///    count - and BOTH layer/mask directions must accept: `(interactor.layer & area.mask)` and
///    `(area.layer & interactor.mask)`, AreaOverlapSystemUVE's rule. An area never lists the
///    interactor living on its own entity.
///
///  * **`interactionTag` is carried, not filtered.** Two areas with different tags both track the
///    same interactors today; tag-based gating belongs to the gameplay layer that acts on the
///    focus, and inventing a rule here would silently change what scenes mean. It is serialized
///    and authored for that layer.
///
///  * **The list is bounded twice.** The authored `maximumCandidates` is honoured as-is, capped by
///    kMaximumInteractionAreaCandidatesUVE, the fixed storage. The moment a candidate does not fit,
///    `interactorsTruncated` is set - the list says "at least this many" rather than pretending it
///    is complete. An authored 0 never reaches this code: it fails the component validator
///    (`maximumCandidates > 0`), so the area is refused as invalid and cleared - tracking nobody on
///    purpose is a disabled area, not a zero budget.
///
///  * **Exactly one area is focused**, and only for the PRIMARY interactor: the first in
///    (index, generation) order, the same deterministic pick SpawnPoint3D selection and split-screen
///    players use. Focus goes to the overlapping area nearest that interactor's center (squared
///    distance - no square root for a comparison), ties broken by (index, generation). No
///    interactor means nothing is focused, and every stale flag was cleared in the pass above.
///
/// Acting on the focus (prompt UI, an "interact" binding, focus enter/exit events) is deliberately
/// not done here - that is the gameplay layer this engine does not own yet, and the same boundary
/// Hitbox3D/Hurtbox3D keep for damage.
[[nodiscard]] InteractionAreaScanResultUVE SyncInteractionAreasUVE(
    Scene::IEntityManagerUVE& entityManager);

} // namespace UVE::Physics
