// univex/integration/SelectionOutlineGeometry.h (private to this module - not under Expose/)
// -----------------------------------------------------------------------
// The triangles the selection outline is drawn from.
//
// Lives in the engine bridge beside EntityPicker because it reads the live
// ECS, and uses the same geometry the picker tests clicks against (the
// primitive's real indexed triangles, placed by the same world matrix the
// renderer uses) - so the outline follows exactly what can be clicked.
// -----------------------------------------------------------------------
#pragma once

#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "univex/render/SelectionOutline.h"

namespace univex::integration {

/// World-space triangles, three vertices each, of every selected mesh and of every mesh below a
/// selected node - selecting a group outlines what is in it. Vertices of `active`, and of what is
/// below it, carry kSelectionOutlineActiveWeight; the rest of the selection
/// kSelectionOutlineOtherWeight. A mesh reached both ways takes the stronger.
///
/// Skipped, matching what the renderer draws and the picker can hit: entities without a clean
/// world transform or a valid PrimitiveMeshComponentUVE, nodes hidden in the hierarchy, and the
/// editor's own internal entities.
[[nodiscard]] std::vector<univex::render::SelectionOutlineVertex> CollectSelectionOutlineTrianglesUVE(
    UVE::Scene::IEntityManagerUVE& entityManager, const std::vector<UVE::Scene::EntityUVE>& selection,
    UVE::Scene::EntityUVE active);

} // namespace univex::integration
