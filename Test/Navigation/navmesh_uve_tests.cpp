// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Tests for the baked navmesh itself: what a polygon means, which side is which, and what the
// queries answer for a point that is on the mesh, beside it, or above it.
//
// The mesh is built by hand here rather than baked. These cases are about the representation the
// bake and the pathfinder share - the winding convention, the surface band, the layer filter - and
// a hand-built mesh states them in a form the reader can check by eye, which a bake's output cannot.

#include "uve/navigation/navmesh_uve.h"

#include <cmath>
#include <cstddef>
#include <optional>

#include <gtest/gtest.h>

namespace UVE::Navigation::Tests {
namespace {

constexpr float kEpsilon = 1e-4F;

/// A flat rectangle from (minimumX, minimumZ) to (maximumX, maximumZ) at `height`, wound the way
/// the bake winds one: counter-clockwise as seen from above, so SignedAreaXZUVE() is positive.
[[nodiscard]] NavmeshPolygonUVE MakeFlatRectangleUVE(const float minimumX, const float minimumZ, const float maximumX,
                                                     const float maximumZ, const float height,
                                                     const std::uint32_t layers = 1U) {
    NavmeshPolygonUVE polygon{};
    polygon.vertexCount = 4U;
    polygon.navigationLayers = layers;
    polygon.vertices[0] = Math::Vector3UVE{minimumX, height, minimumZ};
    polygon.vertices[1] = Math::Vector3UVE{maximumX, height, minimumZ};
    polygon.vertices[2] = Math::Vector3UVE{maximumX, height, maximumZ};
    polygon.vertices[3] = Math::Vector3UVE{minimumX, height, maximumZ};
    polygon.areaSquareMetres = std::fabs(SignedAreaXZUVE(polygon.vertices.data(), polygon.vertexCount));
    polygon.center = Math::Vector3UVE{(minimumX + maximumX) * 0.5F, height, (minimumZ + maximumZ) * 0.5F};
    return polygon;
}

[[nodiscard]] NavmeshUVE MakeSinglePolygonMeshUVE(const float height = 0.0F, const std::uint32_t layers = 1U) {
    NavmeshUVE mesh{};
    mesh.polygons.push_back(MakeFlatRectangleUVE(-2.0F, -1.0F, 2.0F, 1.0F, height, layers));
    mesh.bounds = Math::AabbUVE{Math::Vector3UVE{-2.0F, height - 1.0F, -1.0F},
                                Math::Vector3UVE{2.0F, height + 4.0F, 1.0F}};
    mesh.navigationLayers = layers;
    mesh.cellSize = 0.5F;
    mesh.agentRadius = 0.5F;
    mesh.agentHeight = 1.8F;
    mesh.maximumSlopeDegrees = 45.0F;
    mesh.maximumStepHeight = 0.4F;
    return mesh;
}

TEST(NavmeshUVETest, SignedAreaXZUVE_IsPositiveForTheBakesOwnWinding) {
    // The one convention the bake, the portal orientation and the string-pulling all share. The
    // bake emits (minX,minZ) -> (maxX,minZ) -> (maxX,maxZ) -> (minX,maxZ), so that order has to be
    // the positive one - if it ever flips, every portal's left and right flip with it.
    const NavmeshPolygonUVE rectangle = MakeFlatRectangleUVE(0.0F, 0.0F, 2.0F, 3.0F, 1.0F);
    EXPECT_GT(SignedAreaXZUVE(rectangle.vertices.data(), rectangle.vertexCount), 0.0F);

    const Math::Vector3UVE reversed[4] = {rectangle.vertices[3], rectangle.vertices[2], rectangle.vertices[1],
                                          rectangle.vertices[0]};
    EXPECT_LT(SignedAreaXZUVE(reversed, 4U), 0.0F);
    EXPECT_FLOAT_EQ(std::fabs(SignedAreaXZUVE(reversed, 4U)), 6.0F) << "3 m by 2 m";
    EXPECT_FLOAT_EQ(SignedAreaXZUVE(nullptr, 4U), 0.0F);
    EXPECT_FLOAT_EQ(SignedAreaXZUVE(rectangle.vertices.data(), 2U), 0.0F) << "two points are not a polygon";
}

TEST(NavmeshUVETest, FindPolygonUVE_AnswersInsideTheSurfaceBandAndNowhereElse) {
    NavmeshUVE mesh = MakeSinglePolygonMeshUVE(2.0F);
    ASSERT_EQ(mesh.GetPolygonCountUVE(), 1U);

    EXPECT_TRUE(mesh.FindPolygonUVE(Math::Vector3UVE{0.0F, 2.0F, 0.0F}).has_value()) << "on the surface";
    EXPECT_TRUE(mesh.FindPolygonUVE(Math::Vector3UVE{1.9F, 2.0F, 0.9F}).has_value()) << "near the corner";
    EXPECT_TRUE(mesh.FindPolygonUVE(Math::Vector3UVE{0.0F, 2.3F, 0.0F}).has_value())
        << "within the step band: a point this far above the surface is still on it";
    EXPECT_FALSE(mesh.FindPolygonUVE(Math::Vector3UVE{0.0F, 6.0F, 0.0F}).has_value())
        << "a point six metres up is not standing on this polygon, however far inside it looks from above";
    EXPECT_FALSE(mesh.FindPolygonUVE(Math::Vector3UVE{2.5F, 2.0F, 0.0F}).has_value()) << "outside the outline";
    EXPECT_FALSE(mesh.FindPolygonUVE(Math::Vector3UVE{0.0F, 2.0F, -1.5F}).has_value()) << "outside the outline";
}

TEST(NavmeshUVETest, FindNearestPolygonUVE_ProjectsOffMeshPointsAndHonoursLayers) {
    NavmeshUVE mesh{};
    mesh.polygons.push_back(MakeFlatRectangleUVE(0.0F, 0.0F, 2.0F, 2.0F, 0.0F, 1U));
    mesh.polygons.push_back(MakeFlatRectangleUVE(10.0F, 0.0F, 12.0F, 2.0F, 0.0F, 4U));
    mesh.maximumStepHeight = 0.4F;

    // A point two metres off the first polygon's edge: inside a three-metre budget, outside a
    // one-metre one.
    const Math::Vector3UVE besideFirst{4.0F, 0.0F, 1.0F};
    const std::optional<std::size_t> withinBudget = mesh.FindNearestPolygonUVE(besideFirst, 3.0F, 0xFFFFFFFFU);
    ASSERT_TRUE(withinBudget.has_value());
    EXPECT_EQ(*withinBudget, 0U);
    EXPECT_FALSE(mesh.FindNearestPolygonUVE(besideFirst, 1.0F, 0xFFFFFFFFU).has_value());

    // A point two metres off a polygon on layer 4: invisible to a request that only accepts layer 1,
    // and the nearest match once that layer is asked for. A 10 m budget reaches the far polygon
    // only when the mask allows it.
    const Math::Vector3UVE besideSecond{12.5F, 0.0F, 1.0F};
    const std::optional<std::size_t> layerFiltered = mesh.FindNearestPolygonUVE(besideSecond, 11.0F, 0x1U);
    ASSERT_TRUE(layerFiltered.has_value()) << "the layer-1 polygon is ten and a half metres away and still in budget";
    EXPECT_EQ(*layerFiltered, 0U);
    const std::optional<std::size_t> layerFour = mesh.FindNearestPolygonUVE(besideSecond, 11.0F, 0x4U);
    ASSERT_TRUE(layerFour.has_value());
    EXPECT_EQ(*layerFour, 1U);
    EXPECT_FALSE(mesh.FindNearestPolygonUVE(Math::Vector3UVE{50.0F, 0.0F, 50.0F}, 3.0F, 0xFFFFFFFFU).has_value());
}

TEST(NavmeshUVETest, ProjectPointUVE_LiftsAPointOntoTheSurfacesOwnHeight) {
    // A polygon that spans a slope: its near edge is a metre below its far edge, so a projected
    // point between them must come back at the interpolated height rather than at the polygon's
    // average - that interpolation is what a path across a ramp walks on.
    NavmeshPolygonUVE polygon{};
    polygon.vertexCount = 4U;
    polygon.vertices[0] = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    polygon.vertices[1] = Math::Vector3UVE{4.0F, 0.0F, 0.0F};
    polygon.vertices[2] = Math::Vector3UVE{4.0F, 1.0F, 4.0F};
    polygon.vertices[3] = Math::Vector3UVE{0.0F, 1.0F, 4.0F};
    polygon.center = Math::Vector3UVE{2.0F, 0.5F, 2.0F};
    polygon.areaSquareMetres = 16.0F;

    NavmeshUVE mesh{};
    mesh.polygons.push_back(polygon);

    const std::optional<Math::Vector3UVE> middle = mesh.ProjectPointUVE(Math::Vector3UVE{2.0F, 5.0F, 2.0F}, 0U);
    ASSERT_TRUE(middle.has_value());
    EXPECT_NEAR(middle->x, 2.0F, kEpsilon);
    EXPECT_NEAR(middle->z, 2.0F, kEpsilon);
    EXPECT_NEAR(middle->y, 0.5F, kEpsilon) << "halfway up a 0 m to 1 m slope";

    // A point outside the outline is clamped onto it, and the height follows the clamped position:
    // a metre past the low edge stays at the low edge's height.
    const std::optional<Math::Vector3UVE> clamped = mesh.ProjectPointUVE(Math::Vector3UVE{-1.0F, 9.0F, -1.0F}, 0U);
    ASSERT_TRUE(clamped.has_value());
    EXPECT_NEAR(clamped->x, 0.0F, kEpsilon);
    EXPECT_NEAR(clamped->z, 0.0F, kEpsilon);
    EXPECT_NEAR(clamped->y, 0.0F, kEpsilon);

    EXPECT_FALSE(mesh.ProjectPointUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, 7U).has_value())
        << "a stale polygon index is refused rather than read out of range";
    EXPECT_EQ(mesh.PolygonCenterUVE(7U), Math::Vector3UVE{}) << "and a stale centre is a usable point";
}

TEST(NavmeshUVETest, WalkableAreaAndClearUVE_DescribeTheMeshAndResetIt) {
    NavmeshUVE mesh = MakeSinglePolygonMeshUVE();
    mesh.polygons.push_back(MakeFlatRectangleUVE(4.0F, 0.0F, 6.0F, 2.0F, 0.0F));
    EXPECT_FALSE(mesh.IsEmptyUVE());
    EXPECT_FLOAT_EQ(mesh.GetWalkableAreaSquareMetresUVE(), 8.0F + 4.0F);
    EXPECT_FLOAT_EQ(mesh.GetWalkableAreaSquareMetresUVE(), 12.0F);
    EXPECT_EQ(mesh.GetPortalCountUVE(), 0U);

    mesh.ClearUVE();
    EXPECT_TRUE(mesh.IsEmptyUVE());
    EXPECT_EQ(mesh.GetPolygonCountUVE(), 0U);
    EXPECT_FLOAT_EQ(mesh.GetWalkableAreaSquareMetresUVE(), 0.0F);
    EXPECT_FLOAT_EQ(mesh.agentRadius, 0.0F) << "the settings that produced a mesh go with the mesh";
}

} // namespace
} // namespace UVE::Navigation::Tests
