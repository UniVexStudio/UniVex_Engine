// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Tests for the bake: what a region's volume turns into when it is rasterized against the real
// collision world.
//
// Everything here runs against real colliders and the real raycast system - nothing is mocked,
// because every question worth asking about a navmesh is a question about the level it was baked
// from: is the floor walkable, is the wall not, does a kerb below the step height stay connected,
// and does a surface have room above it to be stood on. The suite is grouped that way:
//   * the floor            - a flat room bakes to walkable polygons with their own surface
//   * the agent's size     - clearance erosion, headroom inside the region, and the layer the
//                            surface came from
//   * the region           - the cell cap, degenerate boxes, and empty air
//   * connections          - portals across a kerb, and the step height that decides one exists

#include "uve/navigation/navmesh_bake_uve.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/collider_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/physics/raycast_system_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Navigation::Tests {
namespace {

constexpr float kEpsilon = 1e-3F;

class NavmeshBakeUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    Physics::RaycastSystemUVE raycastSystem;

    /// A box collider standing as level geometry, with its world transform published first so the
    /// raycast system's own cache sees it - the same order any other system building on the world
    /// uses.
    Scene::EntityUVE MakeBoxUVE(const Math::Vector3UVE position, const Math::Vector3UVE halfExtents,
                                const std::uint32_t collisionLayer = 1U,
                                const Math::QuaternionUVE rotation = Math::QuaternionUVE{}) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE local;
        local.localPosition = position;
        local.localRotation = rotation;
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        sceneGraph.UpdateUVE(entityManager);
        Scene::ColliderComponentUVE collider{halfExtents};
        collider.collisionLayer = collisionLayer;
        entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(entity, collider);
        return entity;
    }

    /// A floor whose top surface is exactly `topHeight`: the box is placed so its own top face is
    /// that height, which is what the bake's rays hit.
    Scene::EntityUVE MakeFloorUVE(const float topHeight, const float halfExtentXZ = 6.0F,
                                  const std::uint32_t collisionLayer = 1U) {
        const Math::Vector3UVE halfExtents{halfExtentXZ, 0.5F, halfExtentXZ};
        return MakeBoxUVE(Math::Vector3UVE{0.0F, topHeight - halfExtents.y, 0.0F}, halfExtents, collisionLayer);
    }

    [[nodiscard]] static Math::AabbUVE MakeRegionUVE(const float halfExtentXZ = 5.0F, const float height = 4.0F) {
        return Math::AabbUVE{Math::Vector3UVE{-halfExtentXZ, -1.0F, -halfExtentXZ},
                             Math::Vector3UVE{halfExtentXZ, height, halfExtentXZ}};
    }

    [[nodiscard]] NavmeshBakeSettingsUVE MakeSettingsUVE(const float cellSize = 0.5F,
                                                        const float agentRadius = 0.5F) const {
        NavmeshBakeSettingsUVE settings{};
        settings.cellSize = cellSize;
        settings.agentRadius = agentRadius;
        settings.agentHeight = 1.8F;
        return settings;
    }

    [[nodiscard]] NavmeshBakeReportUVE BakeUVE(const NavmeshBakeSettingsUVE& settings, const Math::AabbUVE& region,
                                               NavmeshUVE& outMesh) {
        return BakeNavmeshUVE(entityManager, raycastSystem, settings, region, outMesh);
    }
};

TEST_F(NavmeshBakeUVETest, BakeNavmeshUVE_AFlatFloorBecomesWalkablePolygons) {
    const Scene::EntityUVE floor = MakeFloorUVE(0.0F);
    ASSERT_TRUE(entityManager.IsAliveUVE(floor));
    NavmeshUVE mesh;
    // No clearance erosion in this one, so what the floor alone turns into is what is asserted; the
    // agent's own size is the next test's subject.
    const NavmeshBakeReportUVE report = BakeUVE(MakeSettingsUVE(0.5F, 0.0F), MakeRegionUVE(), mesh);

    EXPECT_TRUE(report.bakePossible);
    EXPECT_EQ(report.columnsPlanned, 400U) << "10 m by 10 m at 0.5 m cells";
    EXPECT_EQ(report.columnsTested, 400U) << "every planned column is rasterized; none is skipped";
    EXPECT_EQ(report.noGroundCells, 0U);
    EXPECT_EQ(report.walkableCells, 400U);
    EXPECT_GT(report.polygonCount, 0U);

    EXPECT_FALSE(mesh.IsEmptyUVE());
    EXPECT_FLOAT_EQ(mesh.cellSize, 0.5F);
    EXPECT_FLOAT_EQ(mesh.agentRadius, 0.0F);
    EXPECT_FLOAT_EQ(mesh.agentHeight, 1.8F);
    EXPECT_FLOAT_EQ(mesh.maximumStepHeight, 0.4F);
    EXPECT_NEAR(mesh.GetWalkableAreaSquareMetresUVE(), 100.0F, 0.5F)
        << "ground flat enough to be one height everywhere covers the whole region";

    // Every polygon sits on the floor's own top surface, which is what makes the mesh usable for a
    // body standing on it: the heights came from the ray hits, not from the region's box.
    for (const NavmeshPolygonUVE& polygon : mesh.polygons) {
        EXPECT_NEAR(polygon.center.y, 0.0F, kEpsilon);
        EXPECT_EQ(polygon.vertexCount, 4U);
        EXPECT_GT(polygon.areaSquareMetres, 0.0F);
        EXPECT_EQ(polygon.navigationLayers, 1U) << "the layer of the surface it was rasterized from";
        EXPECT_GT(SignedAreaXZUVE(polygon.vertices.data(), polygon.vertexCount), 0.0F)
            << "baked polygons wind counter-clockwise seen from above";
    }
}

TEST_F(NavmeshBakeUVETest, BakeNavmeshUVE_ErodesTheGroundAnAgentCannotStandOn) {
    MakeFloorUVE(0.0F);
    NavmeshUVE openMesh;
    const NavmeshBakeReportUVE openReport = BakeUVE(MakeSettingsUVE(0.5F, 0.0F), MakeRegionUVE(), openMesh);
    ASSERT_TRUE(openReport.bakePossible);
    EXPECT_EQ(openReport.erodedCells, 0U) << "an agent with no width loses nothing";
    const float openArea = openMesh.GetWalkableAreaSquareMetresUVE();
    EXPECT_NEAR(openArea, 100.0F, 0.5F);

    NavmeshUVE agentMesh;
    const NavmeshBakeReportUVE agentReport = BakeUVE(MakeSettingsUVE(0.5F, 1.0F), MakeRegionUVE(), agentMesh);
    ASSERT_TRUE(agentReport.bakePossible);
    EXPECT_GT(agentReport.erodedCells, 0U) << "the agent's width takes ground away at every edge";
    EXPECT_EQ(agentReport.walkableCells, openReport.walkableCells)
        << "the two bakes rasterized the same ground; only the erosion differs";
    EXPECT_LT(agentMesh.GetWalkableAreaSquareMetresUVE(), openArea);

    // A one-metre radius erodes one metre off every side of a ten-metre room, and the ground that
    // is left is what an agent that wide can actually stand on rather than merely walk over.
    EXPECT_NEAR(agentMesh.GetWalkableAreaSquareMetresUVE(), 64.0F, 1.5F);
    ASSERT_FALSE(agentMesh.IsEmptyUVE());
    float minimumX = agentMesh.polygons.front().vertices[0].x;
    float maximumX = minimumX;
    for (const NavmeshPolygonUVE& polygon : agentMesh.polygons) {
        for (std::uint32_t vertexIndex = 0U; vertexIndex < polygon.vertexCount; ++vertexIndex) {
            minimumX = std::min(minimumX, polygon.vertices[vertexIndex].x);
            maximumX = std::max(maximumX, polygon.vertices[vertexIndex].x);
        }
    }
    EXPECT_NEAR(minimumX, -4.0F, 0.75F) << "the region is 5 m out and the agent reaches 1 m further";
    EXPECT_NEAR(maximumX, 4.0F, 0.75F);
}

TEST_F(NavmeshBakeUVETest, BakeNavmeshUVE_SteepSurfacesAreNotGround) {
    // A plate tilted sixty degrees from level. Its own top face is what a downward ray meets, and a
    // sixty-degree face is a wall to any agent whose limit is forty-five.
    Math::QuaternionUVE tilt{};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 1.0471975512F, tilt))
        << "60 degrees about Z";
    MakeBoxUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{4.0F, 0.25F, 4.0F}, 1U, tilt);

    NavmeshUVE steepMesh;
    NavmeshBakeSettingsUVE steepSettings = MakeSettingsUVE();
    steepSettings.maximumSlopeDegrees = 45.0F;
    const NavmeshBakeReportUVE steepReport = BakeUVE(steepSettings, MakeRegionUVE(), steepMesh);
    EXPECT_GT(steepReport.steepCells, 0U);
    EXPECT_EQ(steepReport.walkableCells, 0U);
    EXPECT_EQ(steepReport.polygonCount, 0U);

    // The same geometry, to an agent that can climb it: the cells become ground. This is the half
    // of the test that proves the slope test is a comparison and not a rejection of tilted geometry
    // as such.
    NavmeshUVE climberMesh;
    NavmeshBakeSettingsUVE climberSettings = MakeSettingsUVE();
    climberSettings.maximumSlopeDegrees = 75.0F;
    climberSettings.agentHeight = 0.2F;
    const NavmeshBakeReportUVE climberReport = BakeUVE(climberSettings, MakeRegionUVE(), climberMesh);
    EXPECT_GT(climberReport.walkableCells, 0U);
    EXPECT_GT(climberReport.polygonCount, 0U);
}

TEST_F(NavmeshBakeUVETest, BakeNavmeshUVE_NeedsAgentHeightOfRoomAboveEverySurface) {
    MakeFloorUVE(0.0F);
    // A region one metre tall over a flat, clear floor: the floor is walkable ground, but there is
    // nowhere under the region's own roof for a 1.8 m agent to stand. The bake has to say so rather
    // than hand back a mesh nothing fits on - and the same volume is fine for an agent that is
    // short enough, which is what proves this is the height being compared and not the roof itself.
    const Math::AabbUVE squat{Math::Vector3UVE{-5.0F, -1.0F, -5.0F}, Math::Vector3UVE{5.0F, 1.0F, 5.0F}};

    NavmeshUVE tallMesh;
    const NavmeshBakeReportUVE tallReport = BakeUVE(MakeSettingsUVE(), squat, tallMesh);
    EXPECT_EQ(tallReport.blockedCells, tallReport.columnsPlanned)
        << "every column found ground and every column was too short for the agent";
    EXPECT_EQ(tallReport.walkableCells, 0U);
    EXPECT_EQ(tallReport.polygonCount, 0U);
    EXPECT_TRUE(tallMesh.IsEmptyUVE());

    NavmeshUVE shortMesh;
    NavmeshBakeSettingsUVE shortSettings = MakeSettingsUVE();
    shortSettings.agentHeight = 0.5F;
    const NavmeshBakeReportUVE shortReport = BakeUVE(shortSettings, squat, shortMesh);
    EXPECT_EQ(shortReport.blockedCells, 0U);
    EXPECT_GT(shortReport.walkableCells, 0U);
    EXPECT_GT(shortReport.polygonCount, 0U);
}

TEST_F(NavmeshBakeUVETest, BakeNavmeshUVE_BakesTheSurfaceTheColumnActuallyPresents) {
    MakeFloorUVE(0.0F);
    // A slab filling the region with its top at 1.2 m: the floor beneath it is covered by the slab,
    // so the surface a column presents is the slab's own top - walkable, since the region has 2.8 m
    // of room above it. The point of the test is the floor: it is not baked, because a downward ray
    // can never reach it, and a navmesh that claimed the floor existed under the slab would send
    // agents into the inside of solid geometry.
    MakeBoxUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::Vector3UVE{6.0F, 0.2F, 6.0F});

    NavmeshUVE mesh;
    const NavmeshBakeReportUVE report = BakeUVE(MakeSettingsUVE(), MakeRegionUVE(), mesh);
    EXPECT_GT(report.walkableCells, 0U);
    ASSERT_GT(report.polygonCount, 0U);
    for (const NavmeshPolygonUVE& polygon : mesh.polygons) {
        EXPECT_NEAR(polygon.center.y, 1.2F, 0.05F) << "the slab's top, not the floor under it";
    }
}

TEST_F(NavmeshBakeUVETest, BakeNavmeshUVE_SamplesOnlyTheLayersItWasAskedForAndCarriesThem) {
    MakeFloorUVE(0.0F, 6.0F, 4U);

    NavmeshUVE wrongLayerMesh;
    NavmeshBakeSettingsUVE wrongLayerSettings = MakeSettingsUVE();
    wrongLayerSettings.queryLayerMask = 0x1U;
    const NavmeshBakeReportUVE wrongLayerReport = BakeUVE(wrongLayerSettings, MakeRegionUVE(), wrongLayerMesh);
    EXPECT_EQ(wrongLayerReport.noGroundCells, wrongLayerReport.columnsPlanned)
        << "a floor on layer 4 is invisible to a bake that only samples layer 1";
    EXPECT_EQ(wrongLayerReport.walkableCells, 0U);
    EXPECT_TRUE(wrongLayerMesh.IsEmptyUVE());

    NavmeshUVE rightLayerMesh;
    NavmeshBakeSettingsUVE rightLayerSettings = MakeSettingsUVE();
    rightLayerSettings.queryLayerMask = 0x4U;
    rightLayerSettings.navigationLayers = 8U;
    const NavmeshBakeReportUVE rightLayerReport = BakeUVE(rightLayerSettings, MakeRegionUVE(), rightLayerMesh);
    ASSERT_GT(rightLayerReport.polygonCount, 0U);
    for (const NavmeshPolygonUVE& polygon : rightLayerMesh.polygons) {
        EXPECT_EQ(polygon.navigationLayers, 4U)
            << "the polygon carries the surface's own collision layer, not the region's navigation layer";
    }
    EXPECT_EQ(rightLayerMesh.navigationLayers, 8U) << "the region's authored navigation layer is recorded";
}

TEST_F(NavmeshBakeUVETest, BakeNavmeshUVE_AKerbBelowTheStepHeightStaysConnected) {
    // Two floors meeting at x = 0, the far one 0.2 m higher. The 0.2 m kerb is a step an agent can
    // take or a wall, depending on the step height it was baked for - and the bake has to say which.
    MakeBoxUVE(Math::Vector3UVE{-5.0F, -0.5F, 0.0F}, Math::Vector3UVE{5.0F, 0.5F, 5.0F});
    MakeBoxUVE(Math::Vector3UVE{5.0F, -0.3F, 0.0F}, Math::Vector3UVE{5.0F, 0.5F, 5.0F});
    const Math::AabbUVE region = MakeRegionUVE(4.0F, 4.0F);

    NavmeshUVE steppedMesh;
    NavmeshBakeSettingsUVE steppedSettings = MakeSettingsUVE(1.0F, 0.5F);
    steppedSettings.mergeHeightToleranceMetres = 0.1F;
    const NavmeshBakeReportUVE steppedReport = BakeUVE(steppedSettings, region, steppedMesh);
    ASSERT_GE(steppedReport.polygonCount, 2U) << "two heights that far apart do not merge into one polygon";

    // The two levels' polygons are told apart by their own centres, which sit between the heights of
    // their corners: a corner on the kerb is the average of the cells on both sides, so a polygon
    // touching the kerb is neither exactly at 0 nor exactly at 0.2 and the split is a comparison.
    std::size_t lowPolygons = 0U;
    std::size_t highPolygons = 0U;
    for (const NavmeshPolygonUVE& polygon : steppedMesh.polygons) {
        if (polygon.center.y < 0.1F) {
            ++lowPolygons;
        } else {
            ++highPolygons;
        }
    }
    EXPECT_GT(lowPolygons, 0U);
    EXPECT_GT(highPolygons, 0U);

    // ...and the two polygons meet on the kerb: every vertex on the shared edge is the same height
    // on both sides of it, which is what stops an agent stepping into a crack the mesh invented.
    std::size_t sharedVertices = 0U;
    for (const NavmeshPolygonUVE& polygon : steppedMesh.polygons) {
        for (std::uint32_t vertexIndex = 0U; vertexIndex < polygon.vertexCount; ++vertexIndex) {
            const Math::Vector3UVE& vertex = polygon.vertices[vertexIndex];
            if (std::fabs(vertex.x) > kEpsilon) {
                continue;
            }
            ++sharedVertices;
            EXPECT_NEAR(vertex.y, 0.1F, kEpsilon)
                << "a vertex on the kerb is the average of the cells above and below it";
        }
    }
    EXPECT_GT(sharedVertices, 0U) << "the kerb is where the two polygons meet";
    // One coalesced portal between the two levels, stored in both directions - not one per cell
    // along the eight-cell border, which is what makes the string-pulling hug the real corner.
    EXPECT_EQ(steppedReport.portalCount, 1U);
    EXPECT_EQ(steppedMesh.GetPortalCountUVE(), 2U);

    // The same geometry with a step height below the kerb: the two levels stop being connected, and
    // the polygons stay exactly where they were - a wall between them, not a different mesh.
    NavmeshUVE walledMesh;
    NavmeshBakeSettingsUVE walledSettings = steppedSettings;
    walledSettings.maximumStepHeight = 0.1F;
    const NavmeshBakeReportUVE walledReport = BakeUVE(walledSettings, region, walledMesh);
    EXPECT_GE(walledReport.polygonCount, 2U);
    EXPECT_EQ(walledReport.portalCount, 0U);
    EXPECT_EQ(walledMesh.GetPortalCountUVE(), 0U);
}

TEST_F(NavmeshBakeUVETest, BakeNavmeshUVE_MakesTheGridCoarserRatherThanLeavingACornerOfTheRegionOut) {
    MakeFloorUVE(0.0F, 12.0F);
    NavmeshBakeSettingsUVE settings = MakeSettingsUVE(0.1F, 0.0F);
    settings.maximumCells = 1000U;

    NavmeshUVE mesh;
    const NavmeshBakeReportUVE report = BakeUVE(settings, MakeRegionUVE(10.0F, 4.0F), mesh);

    EXPECT_TRUE(report.bakePossible);
    EXPECT_TRUE(report.resolutionClamped);
    EXPECT_GT(report.effectiveCellSize, 0.1F);
    EXPECT_LE(report.columnsPlanned, settings.maximumCells);
    EXPECT_EQ(report.columnsTested, report.columnsPlanned)
        << "the whole region is rasterized at the coarser cell size, not the first thousand columns of it";
    EXPECT_GT(report.polygonCount, 0U);
    EXPECT_FLOAT_EQ(mesh.cellSize, report.effectiveCellSize);
}

TEST_F(NavmeshBakeUVETest, BakeNavmeshUVE_RefusesARegionItCannotDescribe) {
    MakeFloorUVE(0.0F);
    NavmeshUVE mesh;

    const Math::AabbUVE degenerate{Math::Vector3UVE{-5.0F, -1.0F, -5.0F}, Math::Vector3UVE{5.0F, -1.0F, 5.0F}};
    NavmeshBakeReportUVE report = BakeUVE(MakeSettingsUVE(), degenerate, mesh);
    EXPECT_FALSE(report.bakePossible);
    EXPECT_TRUE(mesh.IsEmptyUVE());

    NavmeshBakeSettingsUVE noCells = MakeSettingsUVE(0.5F, 0.5F);
    noCells.cellSize = 0.0F;
    report = BakeUVE(noCells, MakeRegionUVE(), mesh);
    EXPECT_FALSE(report.bakePossible);

    NavmeshBakeSettingsUVE noLayers = MakeSettingsUVE(0.5F, 0.5F);
    noLayers.navigationLayers = 0U;
    report = BakeUVE(noLayers, MakeRegionUVE(), mesh);
    EXPECT_FALSE(report.bakePossible) << "a region on no layers could never be asked for";

    NavmeshBakeSettingsUVE rightAngle = MakeSettingsUVE(0.5F, 0.5F);
    rightAngle.maximumSlopeDegrees = 90.0F;
    report = BakeUVE(rightAngle, MakeRegionUVE(), mesh);
    EXPECT_FALSE(report.bakePossible) << "a ninety-degree slope limit is the absence of a limit, not one";
}

TEST_F(NavmeshBakeUVETest, BakeNavmeshUVE_ARegionOverNothingBakesToAnEmptyMesh) {
    NavmeshUVE mesh;
    const NavmeshBakeReportUVE report = BakeUVE(MakeSettingsUVE(), MakeRegionUVE(), mesh);

    EXPECT_TRUE(report.bakePossible) << "the region itself is fine; there is simply nothing in it";
    EXPECT_EQ(report.walkableCells, 0U);
    EXPECT_EQ(report.noGroundCells, report.columnsPlanned);
    EXPECT_EQ(report.polygonCount, 0U);
    EXPECT_TRUE(mesh.IsEmptyUVE());
    EXPECT_FLOAT_EQ(mesh.cellSize, report.effectiveCellSize) << "the settings are still recorded";
}

} // namespace
} // namespace UVE::Navigation::Tests
