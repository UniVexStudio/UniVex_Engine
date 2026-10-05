// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>

#include "uve/math/aabb_uve.h"
#include "uve/navigation/navmesh_uve.h"

namespace UVE::Scene {
class IEntityManagerUVE;
}

namespace UVE::Physics {
class IRaycastSystemUVE;
}

namespace UVE::Navigation {

/// What a region is baked for, and how finely.
///
/// Every field here is an agent property rather than a mesh property: the mesh has to know how wide
/// an agent is to erode the walkable area by that much, how tall it is to refuse ground under a
/// ceiling lower than that, how steep a slope it can climb, and how tall a step it can take. A
/// second agent with different proportions needs its own bake, which is why these settings are
/// recorded on the resulting mesh rather than kept beside it.
struct NavmeshBakeSettingsUVE final {
    /// The rasterization grid. Finer cells follow the geometry more closely and cost more rays;
    /// 0.5 m is the reference engine's own default scale for walkable ground.
    float cellSize = 0.5F;
    /// The agent's own radius: ground closer than this to a wall, a ledge or a rise is eroded away,
    /// because an agent of this width cannot stand there with its body on the mesh.
    float agentRadius = 0.5F;
    /// How much headroom the ground needs above it to be walkable.
    float agentHeight = 1.8F;
    /// Steepest climbable surface, in degrees from up. Steeper surfaces are not ground at all.
    float maximumSlopeDegrees = 45.0F;
    /// Tallest rise between two neighbouring cells that may still be walked. Taller than this and
    /// the cells are adjacent places on different levels, not a step.
    float maximumStepHeight = 0.4F;
    /// How far apart in height two cells may be and still merge into one polygon. Merging cells that
    /// agree only loosely is what keeps a long room from exploding into thousands of small polygons,
    /// and a polygon interpolates its corner heights, so the ground it spans is still followed
    /// rather than flattened.
    float mergeHeightToleranceMetres = 0.1F;
    /// The layers the baked polygons carry. A path request whose own layer mask does not intersect a
    /// polygon's layers cannot use it, which is how one region serves different agent types.
    std::uint32_t navigationLayers = 1U;
    /// Which colliders the rasterization samples, as a `(collisionLayer & queryLayerMask) != 0`
    /// filter. Defaults to everything; a region that should ignore props sets it to the static level
    /// layers only.
    std::uint32_t queryLayerMask = 0xFFFFFFFFU;
    /// The most cells one bake may rasterize. Past it the cell size is made coarser until the region
    /// fits, and the report says so - a bake that quietly covered only part of its region would
    /// produce a navmesh with a hole in it where the level has none.
    std::size_t maximumCells = 262144U;
};

/// What one bake did. Counters rather than a log, matching the other passes' reports: a caller that
/// only wants the mesh ignores them, and a test or the editor can pin them.
struct NavmeshBakeReportUVE final {
    /// Grid columns the region resolved to. `columnsTested` counts the ones actually rasterized,
    /// which is all of them: a bake either covers its region or says it did not.
    std::size_t columnsPlanned = 0U;
    std::size_t columnsTested = 0U;
    /// Columns whose downward ray found ground with acceptable slope and headroom, before the
    /// agent-radius erosion: this is what the region offers, and `erodedCells` is how much of it the
    /// agent's own width then took away.
    std::size_t walkableCells = 0U;
    /// Ground found but too steep.
    std::size_t steepCells = 0U;
    /// Ground found and flat enough, but something is closer above it than the agent is tall.
    std::size_t blockedCells = 0U;
    /// Columns the downward ray did not hit at all inside the region.
    std::size_t noGroundCells = 0U;
    /// Cells the agent-radius erosion took away: walkable in themselves, but too close to a wall,
    /// a ledge or the region's edge for an agent of that width to stand on.
    std::size_t erodedCells = 0U;
    std::size_t polygonCount = 0U;
    std::size_t portalCount = 0U;

    /// The cell size the bake actually used (coarser than requested when `resolutionClamped`), and
    /// whether the cell cap forced that.
    float effectiveCellSize = 0.0F;
    bool resolutionClamped = false;
    /// False when the region's box or the settings could not describe a bake at all (a degenerate
    /// box, a non-positive cell size). Nothing is written to the mesh in that case.
    bool bakePossible = false;
};

/// Rasterizes `bounds` against the collision world through `raycasts` and writes the walkable
/// polygons, their shared portals and the settings that produced them into `outMesh`.
///
/// The world is sampled by rays, not by walking colliders directly: this is the same query surface
/// every other system uses (boxes, spheres and capsules exactly; unknown shapes conservatively), so
/// what the navmesh considers walkable is what a character standing there would actually collide
/// with. `outMesh` is cleared first and left empty when the bake finds nothing walkable - a region
/// over a void bakes to an empty mesh, which is a fact about the region and not an error.
[[nodiscard]] NavmeshBakeReportUVE BakeNavmeshUVE(Scene::IEntityManagerUVE& entityManager,
                                                  const Physics::IRaycastSystemUVE& raycasts,
                                                  const NavmeshBakeSettingsUVE& settings,
                                                  const Math::AabbUVE& bounds, NavmeshUVE& outMesh);

} // namespace UVE::Navigation
