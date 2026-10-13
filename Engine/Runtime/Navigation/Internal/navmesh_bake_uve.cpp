// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/navigation/navmesh_bake_uve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <unordered_map>
#include <vector>

#include "uve/component/collider_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/ray_uve.h"
#include "uve/math/scalar_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/physics/i_raycast_system_uve.h"
#include "uve/physics/raycast_query_uve.h"

namespace UVE::Navigation {

namespace {

constexpr std::uint32_t kUnassignedPolygonUVE = std::numeric_limits<std::uint32_t>::max();

/// One rasterized column of the region: what the downward ray found there and what the rest of the
/// bake decided about it.
struct CellUVE final {
    float groundHeight = 0.0F;
    /// Distance to the nearest non-walkable cell, in metres, filled in by the erosion pass.
    float clearanceMetres = 0.0F;
    std::uint32_t polygon = kUnassignedPolygonUVE;
    std::uint32_t layers = 0U;
    bool walkable = false;
};

/// One shared cell edge, before the per-pair merge that turns many of them into one portal.
struct SharedEdgeUVE final {
    /// True when the edge runs along Z (its X is the fixed coordinate).
    bool alongZ = false;
    float fixed = 0.0F;
    float from = 0.0F;
    float to = 0.0F;
};

[[nodiscard]] bool IsFiniteBoundsUVE(const Math::AabbUVE& bounds) noexcept {
    const Math::Vector3UVE extents = bounds.max - bounds.min;
    return Math::IsFiniteUVE(bounds.min) && Math::IsFiniteUVE(bounds.max) &&
           extents.x > 0.0F && extents.y > 0.0F && extents.z > 0.0F;
}

/// The number of grid columns a region needs, made coarser until it fits the bake's cell cap.
///
/// Coarsening rather than truncating: a bake that covered only the first N columns of its region
/// would leave a navmesh with a hole where the level has floor, and every path across that hole
/// would look like a pathfinding failure instead of a bake that ran out of budget.
[[nodiscard]] bool ResolveGridUVE(const Math::AabbUVE& bounds, const NavmeshBakeSettingsUVE& settings,
                                  std::size_t& outColumnsX, std::size_t& outColumnsZ, float& outCellSize,
                                  bool& outClamped) noexcept {
    const Math::Vector3UVE extents = bounds.max - bounds.min;
    float cellSize = settings.cellSize;
    if (!(cellSize > 0.0F) || settings.maximumCells == 0U) {
        return false;
    }
    const std::size_t maximumCells = settings.maximumCells;
    for (int attempt = 0; attempt < 32; ++attempt) {
        const double columnsX = std::ceil(static_cast<double>(extents.x) / static_cast<double>(cellSize));
        const double columnsZ = std::ceil(static_cast<double>(extents.z) / static_cast<double>(cellSize));
        if (columnsX < 1.0 || columnsZ < 1.0) {
            return false;
        }
        const double cells = columnsX * columnsZ;
        if (cells <= static_cast<double>(maximumCells)) {
            outColumnsX = static_cast<std::size_t>(columnsX);
            outColumnsZ = static_cast<std::size_t>(columnsZ);
            outCellSize = cellSize;
            outClamped = attempt > 0;
            return true;
        }
        // Coarsen by exactly the factor that would make this grid fit, then let the loop's own
        // rounding decide whether one more step is needed.
        cellSize = static_cast<float>(static_cast<double>(cellSize) * std::sqrt(cells / static_cast<double>(maximumCells)));
    }
    return false;
}

} // namespace

NavmeshBakeReportUVE BakeNavmeshUVE(Scene::IEntityManagerUVE& entityManager,
                                    const Physics::IRaycastSystemUVE& raycasts,
                                    const NavmeshBakeSettingsUVE& settings, const Math::AabbUVE& bounds,
                                    NavmeshUVE& outMesh) {
    NavmeshBakeReportUVE report{};
    outMesh.ClearUVE();
    if (!IsFiniteBoundsUVE(bounds) || !(settings.agentRadius >= 0.0F) || !(settings.agentHeight > 0.0F) ||
        !(settings.maximumStepHeight >= 0.0F) || !(settings.mergeHeightToleranceMetres >= 0.0F) ||
        !(settings.maximumSlopeDegrees >= 0.0F) || !(settings.maximumSlopeDegrees < 90.0F) ||
        settings.navigationLayers == 0U) {
        return report;
    }

    std::size_t columnsX = 0U;
    std::size_t columnsZ = 0U;
    float cellSize = 0.0F;
    bool resolutionClamped = false;
    if (!ResolveGridUVE(bounds, settings, columnsX, columnsZ, cellSize, resolutionClamped)) {
        return report;
    }
    report.bakePossible = true;
    report.columnsPlanned = columnsX * columnsZ;
    report.effectiveCellSize = cellSize;
    report.resolutionClamped = resolutionClamped;

    const Math::Vector3UVE extents = bounds.max - bounds.min;
    const float slopeCosineLimit = std::cos(Math::DegToRadUVE(settings.maximumSlopeDegrees));

    std::vector<CellUVE> cells(columnsX * columnsZ);
    const auto cellIndexUVE = [columnsX](const std::size_t column, const std::size_t row) { return row * columnsX + column; };
    const auto cellCenterXUVE = [&bounds, cellSize](const std::size_t column) {
        return bounds.min.x + (static_cast<float>(column) + 0.5F) * cellSize;
    };
    const auto cellCenterZUVE = [&bounds, cellSize](const std::size_t row) {
        return bounds.min.z + (static_cast<float>(row) + 0.5F) * cellSize;
    };

    // ------------------------------------------------------------------ rasterize
    for (std::size_t row = 0U; row < columnsZ; ++row) {
        for (std::size_t column = 0U; column < columnsX; ++column) {
            ++report.columnsTested;
            const float sampleX = cellCenterXUVE(column);
            const float sampleZ = cellCenterZUVE(row);

            Physics::RaycastQueryUVE groundQuery{};
            groundQuery.ray = Math::RayUVE{Math::Vector3UVE{sampleX, bounds.max.y, sampleZ},
                                           Math::Vector3UVE{0.0F, -1.0F, 0.0F}};
            groundQuery.maxDistance = extents.y;
            groundQuery.layerMask = settings.queryLayerMask;
            const std::optional<Physics::RaycastHitUVE> ground =
                raycasts.RaycastUVE(entityManager, groundQuery);
            if (!ground.has_value()) {
                ++report.noGroundCells;
                continue;
            }

            // NormalizeUVE() scales by the inverse length without guarding zero, so the zero-length
            // check comes first: a ray that began inside a collider reports exactly that.
            const float normalLengthSquared = Math::LengthSquaredUVE(ground->normal);
            const Math::Vector3UVE normalizedNormal =
                normalLengthSquared > 0.0F ? ground->normal * (1.0F / std::sqrt(normalLengthSquared))
                                           : Math::Vector3UVE{};
            // A ray that began inside a collider reports no normal at all. There is no surface to
            // classify, so the column is refused: guessing "flat" there would bake ground where the
            // level has the inside of a wall.
            if (normalizedNormal.y <= 0.0F || normalizedNormal.y < slopeCosineLimit) {
                ++report.steepCells;
                continue;
            }
            if (!Math::IsFiniteUVE(ground->point)) {
                ++report.noGroundCells;
                continue;
            }

            // Headroom: anything closer above the ground than the agent is tall leaves no room to
            // stand there. The room has to be inside the region, too - the bake certifies space it
            // can see, and above the region's top it can see nothing. A region authored shorter than
            // the agent is tall therefore comes out blocked instead of baking a mesh nothing fits
            // under, which is a report the caller can act on.
            if (bounds.max.y - ground->point.y < settings.agentHeight) {
                ++report.blockedCells;
                continue;
            }

            // The ray starts just above the surface so it cannot re-hit the ground it was cast from.
            const float surfaceLift = std::min(0.01F, std::max(settings.agentHeight * 0.01F, 0.0001F));
            Physics::RaycastQueryUVE headroomQuery{};
            headroomQuery.ray = Math::RayUVE{
                Math::Vector3UVE{ground->point.x, ground->point.y + surfaceLift, ground->point.z},
                Math::Vector3UVE{0.0F, 1.0F, 0.0F}};
            headroomQuery.maxDistance = settings.agentHeight;
            headroomQuery.layerMask = settings.queryLayerMask;
            if (raycasts.RaycastUVE(entityManager, headroomQuery).has_value()) {
                ++report.blockedCells;
                continue;
            }

            CellUVE& cell = cells[cellIndexUVE(column, row)];
            cell.walkable = true;
            cell.groundHeight = ground->point.y;
            // The polygon inherits the surface's own layer, so a request for agents that may walk on
            // layer 8 is a question this navmesh can answer exactly.
            const Scene::ColliderComponentUVE& collider =
                entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(ground->entity);
            cell.layers = collider.collisionLayer;
            ++report.walkableCells;
        }
    }

    // ------------------------------------------------------------------ erosion
    // Every walkable cell's distance to the nearest cell an agent cannot stand on, computed with a
    // two-pass chamfer transform over the grid (the standard approximation of the exact distance:
    // eight neighbours, straight and diagonal weights). Cells closer to an obstacle, a ledge or the
    // region's own edge than the agent's radius are then removed - which is what makes the mesh an
    // agent's mesh rather than a map of the floor.
    if (settings.agentRadius > 0.0F) {
        constexpr float kStraightWeightUVE = 1.0F;
        const float diagonalWeight = 1.4142135F;
        // One cell of padding all round, seeded as obstacles and never relaxed away from zero. The
        // bake knows nothing about the level past the region's own bounds, so the region's edge has
        // to count as exactly the room it gives an agent: without that ring a floor filling its
        // region perfectly would have no edge to erode, and the mesh would let agents path along a
        // drop the bake never saw.
        const std::size_t paddedWidth = columnsX + 2U;
        const std::size_t paddedHeight = columnsZ + 2U;
        // Ground an agent cannot stand on is seeded at half a cell, not at nothing: a cell's
        // clearance is the distance from its centre to the nearest point that is not walkable, and
        // that point is on the *edge* of the neighbouring cell - half a cell from the centre, not a
        // whole one. Seeding zero would let every mesh keep half a cell more ground than the agent
        // has room for, which is the difference between a path that fits and a path that clips a
        // wall.
        constexpr float kOutsideOffsetCellsUVE = -0.5F;
        std::vector<float> distance(paddedWidth * paddedHeight, kOutsideOffsetCellsUVE);
        const auto paddedIndexUVE = [paddedWidth](const std::size_t column, const std::size_t row) {
            return (row + 1U) * paddedWidth + (column + 1U);
        };
        for (std::size_t row = 0U; row < columnsZ; ++row) {
            for (std::size_t column = 0U; column < columnsX; ++column) {
                if (cells[cellIndexUVE(column, row)].walkable) {
                    distance[paddedIndexUVE(column, row)] = std::numeric_limits<float>::max();
                }
            }
        }

        // The chamfer transform, run over the padded grid so the ring is one of the neighbours every
        // edge cell relaxes from: two sweeps, each cell taking the cheapest of the neighbours it has
        // already seen, the diagonal steps weighted by root two so the approximation stays within a
        // few per cent of the true distance. Everything here is in the padded grid's own coordinates
        // - `paddedIndexUVE` is only for the rasterized cells themselves.
        const auto relaxUVE = [&distance, paddedWidth](const std::size_t column, const std::size_t row,
                                                      const std::size_t fromColumn, const std::size_t fromRow,
                                                      const float weight) {
            const float candidate = distance[fromRow * paddedWidth + fromColumn] + weight;
            float& target = distance[row * paddedWidth + column];
            target = std::min(target, candidate);
        };
        for (std::size_t row = 0U; row < paddedHeight; ++row) {
            for (std::size_t column = 0U; column < paddedWidth; ++column) {
                if (column > 0U) {
                    relaxUVE(column, row, column - 1U, row, kStraightWeightUVE);
                }
                if (row > 0U) {
                    relaxUVE(column, row, column, row - 1U, kStraightWeightUVE);
                    if (column > 0U) {
                        relaxUVE(column, row, column - 1U, row - 1U, diagonalWeight);
                    }
                    if (column + 1U < paddedWidth) {
                        relaxUVE(column, row, column + 1U, row - 1U, diagonalWeight);
                    }
                }
            }
        }
        for (std::size_t row = paddedHeight; row-- > 0U;) {
            for (std::size_t column = paddedWidth; column-- > 0U;) {
                if (column + 1U < paddedWidth) {
                    relaxUVE(column, row, column + 1U, row, kStraightWeightUVE);
                }
                if (row + 1U < paddedHeight) {
                    relaxUVE(column, row, column, row + 1U, kStraightWeightUVE);
                    if (column + 1U < paddedWidth) {
                        relaxUVE(column, row, column + 1U, row + 1U, diagonalWeight);
                    }
                    if (column > 0U) {
                        relaxUVE(column, row, column - 1U, row + 1U, diagonalWeight);
                    }
                }
            }
        }

        for (std::size_t row = 0U; row < columnsZ; ++row) {
            for (std::size_t column = 0U; column < columnsX; ++column) {
                CellUVE& cell = cells[cellIndexUVE(column, row)];
                if (!cell.walkable) {
                    continue;
                }
                cell.clearanceMetres = distance[paddedIndexUVE(column, row)] * cellSize;
                if (cell.clearanceMetres < settings.agentRadius) {
                    cell.walkable = false;
                    ++report.erodedCells;
                }
            }
        }
    }

    // ------------------------------------------------------------------ merge into polygons
    for (std::size_t row = 0U; row < columnsZ; ++row) {
        for (std::size_t column = 0U; column < columnsX; ++column) {
            const CellUVE& seed = cells[cellIndexUVE(column, row)];
            if (!seed.walkable || seed.polygon != kUnassignedPolygonUVE) {
                continue;
            }
            const auto mergeableUVE = [&cells, columnsX, &seed, &settings](const std::size_t candidateColumn,
                                                                           const std::size_t candidateRow) {
                const CellUVE& candidate = cells[candidateRow * columnsX + candidateColumn];
                return candidate.walkable && candidate.polygon == kUnassignedPolygonUVE &&
                       candidate.layers == seed.layers &&
                       std::fabs(candidate.groundHeight - seed.groundHeight) <= settings.mergeHeightToleranceMetres;
            };

            std::size_t width = 1U;
            while (column + width < columnsX && mergeableUVE(column + width, row)) {
                ++width;
            }
            std::size_t height = 1U;
            while (row + height < columnsZ) {
                bool wholeRowMergeable = true;
                for (std::size_t offset = 0U; offset < width; ++offset) {
                    if (!mergeableUVE(column + offset, row + height)) {
                        wholeRowMergeable = false;
                        break;
                    }
                }
                if (!wholeRowMergeable) {
                    break;
                }
                ++height;
            }

            const std::uint32_t polygonIndex = static_cast<std::uint32_t>(outMesh.polygons.size());
            for (std::size_t rowOffset = 0U; rowOffset < height; ++rowOffset) {
                for (std::size_t columnOffset = 0U; columnOffset < width; ++columnOffset) {
                    cells[cellIndexUVE(column + columnOffset, row + rowOffset)].polygon = polygonIndex;
                }
            }

            // Corner heights from the cells around each corner, not from the rectangle alone: a
            // corner on a polygon's border is shared with whatever is across that border, and taking
            // the same average on both sides is what keeps the two polygons meeting exactly instead
            // of a step apart.
            const auto cornerHeightUVE = [&cells, columnsX, columnsZ, &seed](const std::size_t cornerColumn,
                                                                            const std::size_t cornerRow) {
                float total = 0.0F;
                std::size_t counted = 0U;
                for (int rowOffset = -1; rowOffset <= 0; ++rowOffset) {
                    for (int columnOffset = -1; columnOffset <= 0; ++columnOffset) {
                        const int sampleColumn = static_cast<int>(cornerColumn) + columnOffset;
                        const int sampleRow = static_cast<int>(cornerRow) + rowOffset;
                        if (sampleColumn < 0 || sampleRow < 0 || sampleColumn >= static_cast<int>(columnsX) ||
                            sampleRow >= static_cast<int>(columnsZ)) {
                            continue;
                        }
                        const CellUVE& sample =
                            cells[static_cast<std::size_t>(sampleRow) * columnsX + static_cast<std::size_t>(sampleColumn)];
                        if (!sample.walkable || sample.layers != seed.layers) {
                            continue;
                        }
                        total += sample.groundHeight;
                        ++counted;
                    }
                }
                return counted > 0U ? total / static_cast<float>(counted) : seed.groundHeight;
            };

            const float minimumX = bounds.min.x + static_cast<float>(column) * cellSize;
            const float maximumX = bounds.min.x + static_cast<float>(column + width) * cellSize;
            const float minimumZ = bounds.min.z + static_cast<float>(row) * cellSize;
            const float maximumZ = bounds.min.z + static_cast<float>(row + height) * cellSize;
            const std::array<std::array<std::size_t, 2U>, 4U> corners{{
                {column, row},
                {column + width, row},
                {column + width, row + height},
                {column, row + height},
            }};
            const std::array<std::array<float, 2U>, 4U> cornerXZ{{
                {minimumX, minimumZ},
                {maximumX, minimumZ},
                {maximumX, maximumZ},
                {minimumX, maximumZ},
            }};

            NavmeshPolygonUVE polygon{};
            polygon.vertexCount = 4U;
            polygon.navigationLayers = seed.layers;
            for (std::size_t corner = 0U; corner < 4U; ++corner) {
                polygon.vertices[corner] = Math::Vector3UVE{cornerXZ[corner][0],
                                                            cornerHeightUVE(corners[corner][0], corners[corner][1]),
                                                            cornerXZ[corner][1]};
            }
            polygon.areaSquareMetres = std::fabs(SignedAreaXZUVE(polygon.vertices.data(), polygon.vertexCount));
            polygon.center = Math::Vector3UVE{
                (minimumX + maximumX) * 0.5F,
                (polygon.vertices[0].y + polygon.vertices[1].y + polygon.vertices[2].y + polygon.vertices[3].y) * 0.25F,
                (minimumZ + maximumZ) * 0.5F};
            outMesh.polygons.push_back(polygon);
        }
    }

    // ------------------------------------------------------------------ portals
    // Cell edges shared by two different polygons become portals, coalesced per polygon pair so a
    // border is one wide portal rather than one per cell. Coalescing matters for path quality: the
    // string-pulling step walks a portal's two endpoints, and a wall of 1-cell portals makes that
    // walk jitter between their midpoints instead of hugging the one corner that matters.
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<SharedEdgeUVE>> sharedEdges;
    for (std::size_t row = 0U; row < columnsZ; ++row) {
        for (std::size_t column = 0U; column < columnsX; ++column) {
            const CellUVE& cell = cells[cellIndexUVE(column, row)];
            if (!cell.walkable || cell.polygon == kUnassignedPolygonUVE) {
                continue;
            }
            const auto addEdgeUVE = [&sharedEdges, &cells, &settings, columnsX, &cell](const std::size_t neighbourColumn,
                                                                                       const std::size_t neighbourRow,
                                                                                       const SharedEdgeUVE& edge) {
                const CellUVE& neighbour = cells[neighbourRow * columnsX + neighbourColumn];
                if (!neighbour.walkable || neighbour.polygon == cell.polygon ||
                    neighbour.polygon == kUnassignedPolygonUVE) {
                    return;
                }
                if (std::fabs(neighbour.groundHeight - cell.groundHeight) > settings.maximumStepHeight) {
                    return;
                }
                const std::uint32_t low = std::min(cell.polygon, neighbour.polygon);
                const std::uint32_t high = std::max(cell.polygon, neighbour.polygon);
                sharedEdges[{low, high}].push_back(edge);
            };

            if (column + 1U < columnsX) {
                SharedEdgeUVE edge{};
                edge.alongZ = true;
                edge.fixed = bounds.min.x + static_cast<float>(column + 1U) * cellSize;
                edge.from = bounds.min.z + static_cast<float>(row) * cellSize;
                edge.to = bounds.min.z + static_cast<float>(row + 1U) * cellSize;
                addEdgeUVE(column + 1U, row, edge);
            }
            if (row + 1U < columnsZ) {
                SharedEdgeUVE edge{};
                edge.alongZ = false;
                edge.fixed = bounds.min.z + static_cast<float>(row + 1U) * cellSize;
                edge.from = bounds.min.x + static_cast<float>(column) * cellSize;
                edge.to = bounds.min.x + static_cast<float>(column + 1U) * cellSize;
                addEdgeUVE(column, row + 1U, edge);
            }
        }
    }

    const float mergeEpsilon = cellSize * 1.0e-3F;
    std::vector<std::vector<std::uint32_t>> portalsPerPolygon(outMesh.polygons.size());
    for (auto& [pair, edges] : sharedEdges) {
        const std::uint32_t lowPolygon = pair.first;
        const std::uint32_t highPolygon = pair.second;
        std::sort(edges.begin(), edges.end(), [](const SharedEdgeUVE& left, const SharedEdgeUVE& right) {
            if (left.alongZ != right.alongZ) {
                return left.alongZ < right.alongZ;
            }
            if (left.fixed != right.fixed) {
                return left.fixed < right.fixed;
            }
            return left.from < right.from;
        });

        std::size_t index = 0U;
        while (index < edges.size()) {
            SharedEdgeUVE merged = edges[index];
            std::size_t next = index + 1U;
            while (next < edges.size() && edges[next].alongZ == merged.alongZ &&
                   std::fabs(edges[next].fixed - merged.fixed) <= mergeEpsilon &&
                   edges[next].from <= merged.to + mergeEpsilon) {
                merged.to = std::max(merged.to, edges[next].to);
                ++next;
            }
            index = next;

            const Math::Vector3UVE edgeStart = merged.alongZ
                                                   ? Math::Vector3UVE{merged.fixed, 0.0F, merged.from}
                                                   : Math::Vector3UVE{merged.from, 0.0F, merged.fixed};
            const Math::Vector3UVE edgeEnd = merged.alongZ
                                                 ? Math::Vector3UVE{merged.fixed, 0.0F, merged.to}
                                                 : Math::Vector3UVE{merged.to, 0.0F, merged.fixed};
            const std::optional<Math::Vector3UVE> lowStart = outMesh.ProjectPointUVE(edgeStart, lowPolygon);
            const std::optional<Math::Vector3UVE> lowEnd = outMesh.ProjectPointUVE(edgeEnd, lowPolygon);
            const std::optional<Math::Vector3UVE> highStart = outMesh.ProjectPointUVE(edgeStart, highPolygon);
            const std::optional<Math::Vector3UVE> highEnd = outMesh.ProjectPointUVE(edgeEnd, highPolygon);
            if (!lowStart.has_value() || !lowEnd.has_value() || !highStart.has_value() || !highEnd.has_value()) {
                continue;
            }
            // The portal's height is the two surfaces' average: both polygons carry the same corner
            // heights by construction, so this is where they agree, and averaging means a rounding
            // difference becomes a half-millimetre of neither side rather than a step.
            const Math::Vector3UVE start{(lowStart->x + highStart->x) * 0.5F, (lowStart->y + highStart->y) * 0.5F,
                                         (lowStart->z + highStart->z) * 0.5F};
            const Math::Vector3UVE end{(lowEnd->x + highEnd->x) * 0.5F, (lowEnd->y + highEnd->y) * 0.5F,
                                       (lowEnd->z + highEnd->z) * 0.5F};

            // Both directions are stored, each with its own left/right: a portal walked the other way
            // round has its sides the other way round too, and making every reader re-derive that is
            // how a funnel ends up hugging the wrong corner.
            const auto appendPortalUVE = [&outMesh, &portalsPerPolygon](const std::uint32_t from, const std::uint32_t to,
                                                                        const Math::Vector3UVE& left,
                                                                        const Math::Vector3UVE& right) {
                NavmeshPortalUVE portal{};
                portal.from = from;
                portal.to = to;
                portal.left = left;
                portal.right = right;
                portalsPerPolygon[from].push_back(static_cast<std::uint32_t>(outMesh.portals.size()));
                outMesh.portals.push_back(portal);
            };
            const float lowToHighSide =
                (outMesh.polygons[highPolygon].center.x - outMesh.polygons[lowPolygon].center.x) *
                    (start.z - outMesh.polygons[lowPolygon].center.z) -
                (outMesh.polygons[highPolygon].center.z - outMesh.polygons[lowPolygon].center.z) *
                    (start.x - outMesh.polygons[lowPolygon].center.x);
            if (lowToHighSide >= 0.0F) {
                appendPortalUVE(lowPolygon, highPolygon, start, end);
                appendPortalUVE(highPolygon, lowPolygon, end, start);
            } else {
                appendPortalUVE(lowPolygon, highPolygon, end, start);
                appendPortalUVE(highPolygon, lowPolygon, start, end);
            }
            ++report.portalCount;
        }
    }

    // A polygon's portals are flattened into one contiguous window, because that is what A* walks:
    // a search step enumerates `polygons[from].portals` without following indices anywhere else.
    std::vector<NavmeshPortalUVE> ordered;
    ordered.reserve(outMesh.portals.size());
    for (std::size_t polygonIndex = 0U; polygonIndex < outMesh.polygons.size(); ++polygonIndex) {
        std::vector<std::uint32_t>& indices = portalsPerPolygon[polygonIndex];
        std::sort(indices.begin(), indices.end());
        NavmeshPolygonUVE& polygon = outMesh.polygons[polygonIndex];
        polygon.firstPortal = static_cast<std::uint32_t>(ordered.size());
        polygon.portalCount = static_cast<std::uint32_t>(indices.size());
        for (const std::uint32_t portalIndex : indices) {
            ordered.push_back(outMesh.portals[portalIndex]);
        }
    }
    outMesh.portals = std::move(ordered);

    outMesh.bounds = bounds;
    outMesh.navigationLayers = settings.navigationLayers;
    outMesh.cellSize = cellSize;
    outMesh.agentRadius = settings.agentRadius;
    outMesh.agentHeight = settings.agentHeight;
    outMesh.maximumSlopeDegrees = settings.maximumSlopeDegrees;
    outMesh.maximumStepHeight = settings.maximumStepHeight;

    report.polygonCount = outMesh.polygons.size();
    return report;
}

} // namespace UVE::Navigation
