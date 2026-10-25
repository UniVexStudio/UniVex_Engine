// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "uve/render_systems/primitive_geometry_uve.h"

namespace UVE::Render::Tests {
namespace {

[[nodiscard]] float TriangleTwiceAreaSquaredUVE(const Asset::MeshVertexUVE& first,
                                                const Asset::MeshVertexUVE& second,
                                                const Asset::MeshVertexUVE& third) noexcept {
    return Math::LengthSquaredUVE(Math::CrossUVE(second.position - first.position, third.position - first.position));
}

} // namespace

TEST(PrimitiveGeometryUVETest, CubeAndPlane_HaveDeterministicTopologyBoundsAndOutwardFaces) {
    const PrimitiveGeometryUVE& cube = GetPrimitiveGeometryUVE(Scene::PrimitiveMeshKindUVE::Cube);
    EXPECT_EQ(cube.vertices.size(), 24U);
    EXPECT_EQ(cube.indices.size(), 36U);
    EXPECT_EQ(cube.localBounds.min, (Math::Vector3UVE{-0.5F, -0.5F, -0.5F}));
    EXPECT_EQ(cube.localBounds.max, (Math::Vector3UVE{0.5F, 0.5F, 0.5F}));

    const PrimitiveGeometryUVE& plane = GetPrimitiveGeometryUVE(Scene::PrimitiveMeshKindUVE::Plane);
    EXPECT_EQ(plane.vertices.size(), 4U);
    EXPECT_EQ(plane.indices.size(), 6U);
    EXPECT_EQ(plane.localBounds.min, (Math::Vector3UVE{-0.5F, -0.000001F, -0.5F}));
    EXPECT_EQ(plane.localBounds.max, (Math::Vector3UVE{0.5F, 0.000001F, 0.5F}));

    for (const PrimitiveGeometryUVE* geometry : {&cube, &plane}) {
        for (std::size_t index = 0U; index < geometry->indices.size(); index += 3U) {
            const Asset::MeshVertexUVE& first = geometry->vertices[geometry->indices[index]];
            const Asset::MeshVertexUVE& second = geometry->vertices[geometry->indices[index + 1U]];
            const Asset::MeshVertexUVE& third = geometry->vertices[geometry->indices[index + 2U]];
            EXPECT_GT(TriangleTwiceAreaSquaredUVE(first, second, third), 0.00000001F);
            const Math::Vector3UVE faceNormal = Math::CrossUVE(second.position - first.position, third.position - first.position);
            EXPECT_GT(Math::DotUVE(faceNormal, first.normal + second.normal + third.normal), 0.0F);
        }
    }
}

TEST(PrimitiveGeometryUVETest, UVSphere_HasStableSeamPolesUnitNormalsAndNonDegenerateOutwardTopology) {
    const PrimitiveGeometryUVE& sphere = GetPrimitiveGeometryUVE(Scene::PrimitiveMeshKindUVE::UVSphere);
    constexpr std::size_t kSlicesUVE = 24U;
    constexpr std::size_t kStacksUVE = 16U;
    constexpr std::size_t kColumnsUVE = kSlicesUVE + 1U;
    EXPECT_EQ(sphere.vertices.size(), kColumnsUVE * (kStacksUVE + 1U));
    EXPECT_EQ(sphere.indices.size(), 6U * kSlicesUVE * (kStacksUVE - 1U));
    EXPECT_EQ(sphere.localBounds.min, (Math::Vector3UVE{-0.5F, -0.5F, -0.5F}));
    EXPECT_EQ(sphere.localBounds.max, (Math::Vector3UVE{0.5F, 0.5F, 0.5F}));

    for (std::size_t stack = 0U; stack <= kStacksUVE; ++stack) {
        const Asset::MeshVertexUVE& seamStart = sphere.vertices[stack * kColumnsUVE];
        const Asset::MeshVertexUVE& seamEnd = sphere.vertices[stack * kColumnsUVE + kSlicesUVE];
        EXPECT_EQ(seamStart.position, seamEnd.position);
        EXPECT_EQ(seamStart.normal, seamEnd.normal);
        EXPECT_EQ(seamStart.u, 0.0F);
        EXPECT_EQ(seamEnd.u, 1.0F);
    }

    for (std::size_t slice = 0U; slice <= kSlicesUVE; ++slice) {
        const Asset::MeshVertexUVE& northPole = sphere.vertices[slice];
        const Asset::MeshVertexUVE& southPole = sphere.vertices[kStacksUVE * kColumnsUVE + slice];
        EXPECT_EQ(northPole.position, (Math::Vector3UVE{0.0F, 0.5F, 0.0F}));
        EXPECT_EQ(northPole.normal, (Math::Vector3UVE{0.0F, 1.0F, 0.0F}));
        EXPECT_EQ(southPole.position, (Math::Vector3UVE{0.0F, -0.5F, 0.0F}));
        EXPECT_EQ(southPole.normal, (Math::Vector3UVE{0.0F, -1.0F, 0.0F}));
    }

    for (const Asset::MeshVertexUVE& vertex : sphere.vertices) {
        EXPECT_TRUE(Math::IsFiniteUVE(vertex.position));
        EXPECT_TRUE(Math::IsFiniteUVE(vertex.normal));
        EXPECT_TRUE(std::isfinite(vertex.u));
        EXPECT_TRUE(std::isfinite(vertex.v));
        EXPECT_NEAR(Math::LengthSquaredUVE(vertex.normal), 1.0F, 0.00001F);
    }
    for (std::size_t index = 0U; index < sphere.indices.size(); index += 3U) {
        const Asset::MeshVertexUVE& first = sphere.vertices[sphere.indices[index]];
        const Asset::MeshVertexUVE& second = sphere.vertices[sphere.indices[index + 1U]];
        const Asset::MeshVertexUVE& third = sphere.vertices[sphere.indices[index + 2U]];
        EXPECT_GT(TriangleTwiceAreaSquaredUVE(first, second, third), 0.00000001F);
        const Math::Vector3UVE faceNormal = Math::CrossUVE(second.position - first.position, third.position - first.position);
        EXPECT_GT(Math::DotUVE(faceNormal, first.position + second.position + third.position), 0.0F);
    }
}


// A capsule and a cylinder used to fall back to the cube's geometry, so choosing either shape drew
// a box. These tests pin the two things that made that fallback invisible: the shape's own bounds,
// and triangle winding that would have shipped inside-out once the geometry existed.
TEST(PrimitiveGeometryUVETest, CapsuleAndCylinder_AreTheirOwnShapesWithOutwardFaces) {
    const PrimitiveGeometryUVE& capsule = GetPrimitiveGeometryUVE(Scene::PrimitiveMeshKindUVE::Capsule);
    const PrimitiveGeometryUVE& cylinder = GetPrimitiveGeometryUVE(Scene::PrimitiveMeshKindUVE::Cylinder);
    const PrimitiveGeometryUVE& cube = GetPrimitiveGeometryUVE(Scene::PrimitiveMeshKindUVE::Cube);

    for (const PrimitiveGeometryUVE* geometry : {&capsule, &cylinder}) {
        EXPECT_FALSE(geometry->vertices.empty());
        EXPECT_EQ(geometry->indices.size() % 3U, 0U);
        // The declared half extents: 0.5 wide, 1.0 tall. A cube answers {0.5, 0.5, 0.5}.
        EXPECT_EQ(geometry->localBounds.min, (Math::Vector3UVE{-0.5F, -1.0F, -0.5F}));
        EXPECT_EQ(geometry->localBounds.max, (Math::Vector3UVE{0.5F, 1.0F, 0.5F}));
        EXPECT_NE(geometry->indices.size(), cube.indices.size());
        EXPECT_GE(geometry->indices.size(), cube.indices.size());

        for (std::size_t index = 0U; index < geometry->indices.size(); index += 3U) {
            const Asset::MeshVertexUVE& first = geometry->vertices[geometry->indices[index]];
            const Asset::MeshVertexUVE& second = geometry->vertices[geometry->indices[index + 1U]];
            const Asset::MeshVertexUVE& third = geometry->vertices[geometry->indices[index + 2U]];
            // No degenerate triangles: a ring collapsed onto a pole would show up here as zero area.
            EXPECT_GT(TriangleTwiceAreaSquaredUVE(first, second, third), 0.00000001F) << index;
            const Math::Vector3UVE faceNormal =
                Math::CrossUVE(second.position - first.position, third.position - first.position);
            EXPECT_GT(Math::DotUVE(faceNormal, first.normal + second.normal + third.normal), 0.0F) << index;
        }
    }

    // A capsule is a barrel with hemispherical ends, so its widest ring sits at |y| = 0.5 and the
    // ends reach the full half height. A cylinder is a barrel with flat ends all the way up.
    bool capsuleHasBarrelRing = false;
    bool capsuleHasPole = false;
    for (const Asset::MeshVertexUVE& vertex : capsule.vertices) {
        const float radius = std::sqrt((vertex.position.x * vertex.position.x) +
                                       (vertex.position.z * vertex.position.z));
        if (radius > 0.499F && std::fabs(vertex.position.y) < 0.51F) {
            capsuleHasBarrelRing = true;
        }
        if (std::fabs(vertex.position.y) > 0.999F) {
            capsuleHasPole = true;
        }
        EXPECT_LE(radius, 0.5001F);
    }
    EXPECT_TRUE(capsuleHasBarrelRing);
    EXPECT_TRUE(capsuleHasPole);

    std::size_t flatCapNormals = 0U;
    for (const Asset::MeshVertexUVE& vertex : cylinder.vertices) {
        if (std::fabs(std::fabs(vertex.normal.y) - 1.0F) < 0.0001F) {
            ++flatCapNormals;
        }
    }
    // Both caps, each a centre plus its ring.
    EXPECT_EQ(flatCapNormals, 2U * 26U);
}

// The capsule shipped for one slice with two holes in it: the bottom hemisphere was built around
// the TOP cap's centre, and neither hemisphere was stitched to its barrel ring. Both defects are
// invisible to a test that only looks at triangles one at a time - every triangle was fine, there
// was simply a band of nothing between the pieces - so this walks the edges instead. A closed
// surface uses every edge exactly twice; a hole shows up as an edge used once.
TEST(PrimitiveGeometryUVETest, EveryPrimitiveIsAClosedSurface) {
    struct KeyUVE final {
        long long x, y, z;
        [[nodiscard]] bool operator<(const KeyUVE& other) const noexcept {
            if (x != other.x) return x < other.x;
            if (y != other.y) return y < other.y;
            return z < other.z;
        }
        [[nodiscard]] bool operator==(const KeyUVE& other) const noexcept {
            return x == other.x && y == other.y && z == other.z;
        }
    };
    // Positions are snapped before comparing so the seam vertices that are deliberately duplicated
    // for their own normals still count as one point. A tenth of a millimetre is far coarser than
    // the closest two distinct vertices of any primitive and far finer than the geometry.
    const auto quantise = [](const Math::Vector3UVE& position) {
        constexpr double kScaleUVE = 10000.0;
        return KeyUVE{static_cast<long long>(std::llround(position.x * kScaleUVE)),
                      static_cast<long long>(std::llround(position.y * kScaleUVE)),
                      static_cast<long long>(std::llround(position.z * kScaleUVE))};
    };
    struct EdgeUVE final {
        KeyUVE a, b;
        [[nodiscard]] bool operator<(const EdgeUVE& other) const noexcept {
            if (a < other.a) return true;
            if (other.a < a) return false;
            return b < other.b;
        }
        [[nodiscard]] bool operator==(const EdgeUVE& other) const noexcept { return a == other.a && b == other.b; }
    };

    for (const Scene::PrimitiveMeshKindUVE kind : {Scene::PrimitiveMeshKindUVE::Cube, Scene::PrimitiveMeshKindUVE::Capsule,
                                                   Scene::PrimitiveMeshKindUVE::Cylinder}) {
        const PrimitiveGeometryUVE& geometry = GetPrimitiveGeometryUVE(kind);
        std::vector<EdgeUVE> edges;
        edges.reserve(geometry.indices.size());
        for (std::size_t index = 0U; index + 2U < geometry.indices.size(); index += 3U) {
            KeyUVE corner[3] = {quantise(geometry.vertices[geometry.indices[index]].position),
                                quantise(geometry.vertices[geometry.indices[index + 1U]].position),
                                quantise(geometry.vertices[geometry.indices[index + 2U]].position)};
            // A collapsed triangle would make the edge walk lie about closure, so it is asserted
            // rather than skipped.
            ASSERT_TRUE(!(corner[0] == corner[1]) && !(corner[1] == corner[2]) && !(corner[0] == corner[2]))
                << "degenerate triangle at " << index << " for kind " << static_cast<int>(kind);
            for (std::size_t side = 0U; side < 3U; ++side) {
                const KeyUVE& first = corner[side];
                const KeyUVE& second = corner[(side + 1U) % 3U];
                edges.push_back(first < second ? EdgeUVE{first, second} : EdgeUVE{second, first});
            }
        }
        std::sort(edges.begin(), edges.end());
        std::size_t openEdges = 0U;
        std::size_t overusedEdges = 0U;
        for (std::size_t begin = 0U; begin < edges.size();) {
            std::size_t end = begin + 1U;
            while (end < edges.size() && edges[end] == edges[begin]) {
                ++end;
            }
            if (end - begin == 1U) {
                ++openEdges;
            } else if (end - begin > 2U) {
                ++overusedEdges;
            }
            begin = end;
        }
        EXPECT_EQ(openEdges, 0U) << "kind " << static_cast<int>(kind);
        EXPECT_EQ(overusedEdges, 0U) << "kind " << static_cast<int>(kind);
    }
}

// The capsule is symmetric about y = 0 by construction. The bottom hemisphere used to be built
// from the top cap's centre, which put its rings just under the top cap and left a cone running
// down to the pole - so the assertion is about the shape, not about any one vertex.
TEST(PrimitiveGeometryUVETest, CapsuleIsSymmetricAboutItsEquator) {
    const PrimitiveGeometryUVE& capsule = GetPrimitiveGeometryUVE(Scene::PrimitiveMeshKindUVE::Capsule);
    float lowest = 1.0e9F;
    float highest = -1.0e9F;
    for (const Asset::MeshVertexUVE& vertex : capsule.vertices) {
        lowest = std::min(lowest, vertex.position.y);
        highest = std::max(highest, vertex.position.y);
    }
    EXPECT_NEAR(lowest, -1.0F, 1.0e-5F);
    EXPECT_NEAR(highest, 1.0F, 1.0e-5F);

    // Every vertex on one cap has a mirror on the other at the same radius AND the opposite
    // height. Radius alone would be satisfied by the vertex's own row, which is exactly the
    // mistake being guarded against: the broken build had both caps near the top.
    const auto radiusOf = [](const Asset::MeshVertexUVE& vertex) {
        return std::sqrt((vertex.position.x * vertex.position.x) +
                         (vertex.position.z * vertex.position.z));
    };
    for (const Asset::MeshVertexUVE& vertex : capsule.vertices) {
        const float radius = radiusOf(vertex);
        const bool mirrored = std::any_of(capsule.vertices.begin(), capsule.vertices.end(),
                                          [&vertex, radius, &radiusOf](const Asset::MeshVertexUVE& candidate) {
                                              return std::fabs(candidate.position.y + vertex.position.y) < 1.0e-5F &&
                                                     std::fabs(radiusOf(candidate) - radius) < 1.0e-5F;
                                          });
        EXPECT_TRUE(mirrored) << "y " << vertex.position.y << " radius " << radius << " has no mirror";
    }
}

TEST(PrimitiveGeometryUVETest, LitGeometry_CarriesAUnitTangentBasisForEveryPrimitiveKind) {
    constexpr std::array<Scene::PrimitiveMeshKindUVE, 5U> kKindsUVE{
        Scene::PrimitiveMeshKindUVE::Cube,     Scene::PrimitiveMeshKindUVE::UVSphere,
        Scene::PrimitiveMeshKindUVE::Plane,    Scene::PrimitiveMeshKindUVE::Capsule,
        Scene::PrimitiveMeshKindUVE::Cylinder,
    };
    for (const Scene::PrimitiveMeshKindUVE kind : kKindsUVE) {
        const PrimitiveGeometryUVE& lit = GetPrimitiveLitGeometryUVE(kind);
        const PrimitiveGeometryUVE& raw = GetPrimitiveGeometryUVE(kind);
        // The lit variant is the same surface: only the tangent basis differs.
        EXPECT_EQ(lit.vertices.size(), raw.vertices.size());
        EXPECT_EQ(lit.indices.size(), raw.indices.size());
        EXPECT_EQ(lit.localBounds.min, raw.localBounds.min);
        EXPECT_EQ(lit.localBounds.max, raw.localBounds.max);

        for (const Asset::MeshVertexUVE& vertex : lit.vertices) {
            EXPECT_NEAR(Math::LengthUVE(vertex.tangent), 1.0F, 1.0e-4F);
            // A tangent that is not perpendicular to the normal would skew every normal map
            // sampled through the TBN basis this vertex builds.
            EXPECT_NEAR(Math::DotUVE(vertex.tangent, vertex.normal), 0.0F, 1.0e-3F) << "kind "
                                                                                     << static_cast<int>(kind);
            EXPECT_TRUE(vertex.tangentHandedness == 1.0F || vertex.tangentHandedness == -1.0F);
        }

        // Derived once per kind, never per call.
        EXPECT_EQ(&GetPrimitiveLitGeometryUVE(kind), &lit);
    }
}

} // namespace UVE::Render::Tests
