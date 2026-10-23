// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/render_systems/primitive_geometry_uve.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace UVE::Render {

namespace {

constexpr float kPrimitiveHalfExtentUVE = 0.5F;
constexpr std::uint32_t kSphereSlicesUVE = 24U;
constexpr std::uint32_t kSphereStacksUVE = 16U;
constexpr float kGeometryEpsilonUVE = 0.000001F;

void AddOutwardTriangleUVE(PrimitiveGeometryUVE& geometry, std::uint32_t first, std::uint32_t second,
                           std::uint32_t third) {
    const Math::Vector3UVE& a = geometry.vertices[first].position;
    const Math::Vector3UVE& b = geometry.vertices[second].position;
    const Math::Vector3UVE& c = geometry.vertices[third].position;
    const Math::Vector3UVE faceNormal = Math::CrossUVE(b - a, c - a);
    const Math::Vector3UVE expectedOutward = a + b + c;
    if (Math::DotUVE(faceNormal, expectedOutward) < 0.0F) {
        std::swap(second, third);
    }
    geometry.indices.push_back(first);
    geometry.indices.push_back(second);
    geometry.indices.push_back(third);
}

[[nodiscard]] PrimitiveGeometryUVE MakeCubeGeometryUVE() {
    PrimitiveGeometryUVE geometry;
    geometry.localBounds = Math::AabbUVE{{-kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE},
                                         {kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE}};
    struct FaceUVE final {
        Math::Vector3UVE normal;
        std::array<Math::Vector3UVE, 4> positions;
    };
    const std::array<FaceUVE, 6> faces{
        FaceUVE{{1.0F, 0.0F, 0.0F}, {{{kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE},
                                      {kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE},
                                      {kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE},
                                      {kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE}}}},
        FaceUVE{{-1.0F, 0.0F, 0.0F}, {{{-kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE},
                                       {-kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE},
                                       {-kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE},
                                       {-kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE}}}},
        FaceUVE{{0.0F, 1.0F, 0.0F}, {{{-kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE},
                                      {kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE},
                                      {kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE},
                                      {-kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE}}}},
        FaceUVE{{0.0F, -1.0F, 0.0F}, {{{-kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE},
                                       {kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE},
                                       {kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE},
                                       {-kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE}}}},
        FaceUVE{{0.0F, 0.0F, 1.0F}, {{{-kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE},
                                      {-kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE},
                                      {kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE},
                                      {kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE}}}},
        FaceUVE{{0.0F, 0.0F, -1.0F}, {{{kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE},
                                       {kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE},
                                       {-kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE},
                                       {-kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE}}}},
    };
    constexpr std::array<std::array<float, 2>, 4> uvs{{{{0.0F, 0.0F}}, {{1.0F, 0.0F}}, {{1.0F, 1.0F}}, {{0.0F, 1.0F}}}};
    geometry.vertices.reserve(faces.size() * 4U);
    geometry.indices.reserve(faces.size() * 6U);
    for (const FaceUVE& face : faces) {
        const std::uint32_t base = static_cast<std::uint32_t>(geometry.vertices.size());
        for (std::size_t vertexIndex = 0U; vertexIndex < face.positions.size(); ++vertexIndex) {
            geometry.vertices.push_back(Asset::MeshVertexUVE{face.positions[vertexIndex], face.normal,
                                                              uvs[vertexIndex][0], uvs[vertexIndex][1]});
        }
        AddOutwardTriangleUVE(geometry, base, base + 1U, base + 2U);
        AddOutwardTriangleUVE(geometry, base, base + 2U, base + 3U);
    }
    return geometry;
}

[[nodiscard]] PrimitiveGeometryUVE MakePlaneGeometryUVE() {
    PrimitiveGeometryUVE geometry;
    geometry.localBounds = Math::AabbUVE{{-kPrimitiveHalfExtentUVE, -kGeometryEpsilonUVE, -kPrimitiveHalfExtentUVE},
                                         {kPrimitiveHalfExtentUVE, kGeometryEpsilonUVE, kPrimitiveHalfExtentUVE}};
    const Math::Vector3UVE normal{0.0F, 1.0F, 0.0F};
    geometry.vertices = {
        Asset::MeshVertexUVE{{-kPrimitiveHalfExtentUVE, 0.0F, -kPrimitiveHalfExtentUVE}, normal, 0.0F, 0.0F},
        Asset::MeshVertexUVE{{kPrimitiveHalfExtentUVE, 0.0F, -kPrimitiveHalfExtentUVE}, normal, 1.0F, 0.0F},
        Asset::MeshVertexUVE{{kPrimitiveHalfExtentUVE, 0.0F, kPrimitiveHalfExtentUVE}, normal, 1.0F, 1.0F},
        Asset::MeshVertexUVE{{-kPrimitiveHalfExtentUVE, 0.0F, kPrimitiveHalfExtentUVE}, normal, 0.0F, 1.0F},
    };
    // The plane is origin-centered, so its triangle centroid cannot determine outward direction.
    // Author the fixed +Y winding explicitly instead of using AddOutwardTriangleUVE's radial test.
    geometry.indices = {0U, 2U, 1U, 0U, 3U, 2U};
    return geometry;
}

[[nodiscard]] PrimitiveGeometryUVE MakeUVSphereGeometryUVE() {
    PrimitiveGeometryUVE geometry;
    geometry.localBounds = Math::AabbUVE{{-kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE, -kPrimitiveHalfExtentUVE},
                                         {kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE, kPrimitiveHalfExtentUVE}};
    const std::uint32_t columns = kSphereSlicesUVE + 1U;
    geometry.vertices.reserve(static_cast<std::size_t>(columns) * (kSphereStacksUVE + 1U));
    geometry.indices.reserve(static_cast<std::size_t>(kSphereSlicesUVE) * (6U * (kSphereStacksUVE - 1U)));

    for (std::uint32_t stack = 0U; stack <= kSphereStacksUVE; ++stack) {
        const float v = static_cast<float>(stack) / static_cast<float>(kSphereStacksUVE);
        const float latitude = v * std::numbers::pi_v<float>;
        const float sineLatitude = std::sin(latitude);
        const float cosineLatitude = std::cos(latitude);
        for (std::uint32_t slice = 0U; slice <= kSphereSlicesUVE; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(kSphereSlicesUVE);
            const float longitude = u * 2.0F * std::numbers::pi_v<float>;
            const float sineLongitude = slice == kSphereSlicesUVE ? 0.0F : std::sin(longitude);
            const float cosineLongitude = slice == kSphereSlicesUVE ? 1.0F : std::cos(longitude);
            Math::Vector3UVE normal{sineLatitude * cosineLongitude, cosineLatitude,
                                    sineLatitude * sineLongitude};
            Math::Vector3UVE position = normal * kPrimitiveHalfExtentUVE;
            if (stack == 0U) {
                normal = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
                position = normal * kPrimitiveHalfExtentUVE;
            } else if (stack == kSphereStacksUVE) {
                normal = Math::Vector3UVE{0.0F, -1.0F, 0.0F};
                position = normal * kPrimitiveHalfExtentUVE;
            }
            geometry.vertices.push_back(Asset::MeshVertexUVE{position, normal, u, v});
        }
    }

    const auto indexAt = [](const std::uint32_t stack, const std::uint32_t slice) noexcept {
        return stack * (kSphereSlicesUVE + 1U) + slice;
    };
    for (std::uint32_t slice = 0U; slice < kSphereSlicesUVE; ++slice) {
        AddOutwardTriangleUVE(geometry, indexAt(0U, slice), indexAt(1U, slice + 1U), indexAt(1U, slice));
    }
    for (std::uint32_t stack = 1U; stack + 1U < kSphereStacksUVE; ++stack) {
        for (std::uint32_t slice = 0U; slice < kSphereSlicesUVE; ++slice) {
            const std::uint32_t topLeft = indexAt(stack, slice);
            const std::uint32_t topRight = indexAt(stack, slice + 1U);
            const std::uint32_t bottomLeft = indexAt(stack + 1U, slice);
            const std::uint32_t bottomRight = indexAt(stack + 1U, slice + 1U);
            AddOutwardTriangleUVE(geometry, topLeft, topRight, bottomRight);
            AddOutwardTriangleUVE(geometry, topLeft, bottomRight, bottomLeft);
        }
    }
    for (std::uint32_t slice = 0U; slice < kSphereSlicesUVE; ++slice) {
        AddOutwardTriangleUVE(geometry, indexAt(kSphereStacksUVE - 1U, slice),
                               indexAt(kSphereStacksUVE - 1U, slice + 1U),
                               indexAt(kSphereStacksUVE, slice));
    }
    return geometry;
}


/// A capsule standing along +Y: a barrel of radius 0.5 between two cap centres 0.5 either side of
/// the origin, closed by a hemisphere at each end. It is the shape the collider of the same name
/// describes, and the shape the half extents (0.5 wide, 1.0 tall) promise.
[[nodiscard]] PrimitiveGeometryUVE MakeCapsuleGeometryUVE() {
    constexpr float kRadiusUVE = 0.5F;
    constexpr float kHalfHeightUVE = 1.0F;
    constexpr float kCapCentreUVE = kHalfHeightUVE - kRadiusUVE;
    constexpr std::uint32_t kSlicesUVE = 24U;
    // Rings BETWEEN the barrel and the pole. The pole is one vertex of its own: a ring at the pole
    // radius would be a degenerate circle, and the triangles reaching it would have no area.
    constexpr std::uint32_t kCapRingsUVE = 7U;
    constexpr std::uint32_t kRingVerticesUVE = kSlicesUVE + 1U;

    PrimitiveGeometryUVE geometry;
    geometry.localBounds = Math::AabbUVE{{-kRadiusUVE, -kHalfHeightUVE, -kRadiusUVE},
                                         {kRadiusUVE, kHalfHeightUVE, kRadiusUVE}};

    // Barrel: two rings joined strip-wise. Winding is authored, not inferred: the strip runs
    // counter-clockwise seen from +Y, so (bottom, top) pairs face outward.
    const auto pushRing = [&](const float y, const float radius, const float v) {
        const std::uint32_t base = static_cast<std::uint32_t>(geometry.vertices.size());
        for (std::uint32_t slice = 0U; slice <= kSlicesUVE; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(kSlicesUVE);
            const float angle = u * 2.0F * std::numbers::pi_v<float>;
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            geometry.vertices.push_back(Asset::MeshVertexUVE{
                {cosine * radius, y, sine * radius}, {cosine, 0.0F, sine}, u, v});
        }
        return base;
    };
    const std::uint32_t bottomRing = pushRing(-kCapCentreUVE, kRadiusUVE, 0.5F);
    const std::uint32_t topRing = pushRing(kCapCentreUVE, kRadiusUVE, 0.5F);
    for (std::uint32_t slice = 0U; slice < kSlicesUVE; ++slice) {
        const std::uint32_t a = bottomRing + slice;
        const std::uint32_t b = a + 1U;
        const std::uint32_t c = topRing + slice;
        const std::uint32_t d = c + 1U;
        constexpr std::uint32_t kQuadUVE[] = {0U, 2U, 3U, 0U, 3U, 1U};
        const std::uint32_t corners[] = {a, b, c, d};
        for (const std::uint32_t corner : kQuadUVE) {
            geometry.indices.push_back(corners[corner]);
        }
    }

    for (const float side : {1.0F, -1.0F}) {
        const std::uint32_t firstRing = static_cast<std::uint32_t>(geometry.vertices.size());
        for (std::uint32_t stack = 1U; stack <= kCapRingsUVE; ++stack) {
            const float step = static_cast<float>(stack) / static_cast<float>(kCapRingsUVE + 1U);
            const float polar = step * (std::numbers::pi_v<float> * 0.5F);
            const float ringRadius = kRadiusUVE * std::cos(polar);
            const float y = kCapCentreUVE + (side * kRadiusUVE * std::sin(polar));
            for (std::uint32_t slice = 0U; slice <= kSlicesUVE; ++slice) {
                const float u = static_cast<float>(slice) / static_cast<float>(kSlicesUVE);
                const float angle = u * 2.0F * std::numbers::pi_v<float>;
                const float cosine = std::cos(angle);
                const float sine = std::sin(angle);
                const float cosinePolar = std::cos(polar);
                geometry.vertices.push_back(Asset::MeshVertexUVE{
                    {cosine * ringRadius, y, sine * ringRadius},
                    {cosine * cosinePolar, side * std::sin(polar), sine * cosinePolar}, u,
                    0.5F + (step * 0.5F)});
            }
        }
        // Quad strip from the barrel to the last cap ring, then a fan to the pole.
        for (std::uint32_t stack = 0U; stack + 1U < kCapRingsUVE; ++stack) {
            for (std::uint32_t slice = 0U; slice < kSlicesUVE; ++slice) {
                const std::uint32_t inner = firstRing + (stack * kRingVerticesUVE) + slice;
                const std::uint32_t outer = inner + kRingVerticesUVE;
                const std::uint32_t corners[] = {inner, inner + 1U, outer, outer + 1U};
                constexpr std::uint32_t kTopQuadUVE[] = {0U, 2U, 3U, 0U, 3U, 1U};
                constexpr std::uint32_t kBottomQuadUVE[] = {0U, 3U, 2U, 0U, 1U, 3U};
                const std::uint32_t* const order = side > 0.0F ? kTopQuadUVE : kBottomQuadUVE;
                for (std::size_t corner = 0U; corner < 6U; ++corner) {
                    geometry.indices.push_back(corners[order[corner]]);
                }
            }
        }
        const std::uint32_t lastRing = firstRing + ((kCapRingsUVE - 1U) * kRingVerticesUVE);
        const std::uint32_t pole = static_cast<std::uint32_t>(geometry.vertices.size());
        geometry.vertices.push_back(
            Asset::MeshVertexUVE{{0.0F, side * kHalfHeightUVE, 0.0F}, {0.0F, side, 0.0F}, 0.5F, 1.0F});
        for (std::uint32_t slice = 0U; slice < kSlicesUVE; ++slice) {
            const std::uint32_t a = lastRing + slice;
            const std::uint32_t b = a + 1U;
            if (side > 0.0F) {
                geometry.indices.insert(geometry.indices.end(), {a, pole, b});
            } else {
                geometry.indices.insert(geometry.indices.end(), {a, b, pole});
            }
        }
    }
    return geometry;
}

/// A cylinder standing along +Y: radius 0.5, height 2.0, with flat caps closed by their own centre
/// vertex and their own outward normals.
[[nodiscard]] PrimitiveGeometryUVE MakeCylinderGeometryUVE() {
    constexpr float kRadiusUVE = 0.5F;
    constexpr float kHalfHeightUVE = 1.0F;
    constexpr std::uint32_t kSlicesUVE = 24U;

    PrimitiveGeometryUVE geometry;
    geometry.localBounds = Math::AabbUVE{{-kRadiusUVE, -kHalfHeightUVE, -kRadiusUVE},
                                         {kRadiusUVE, kHalfHeightUVE, kRadiusUVE}};
    const auto pushRing = [&](const float y, const float v) {
        const std::uint32_t base = static_cast<std::uint32_t>(geometry.vertices.size());
        for (std::uint32_t slice = 0U; slice <= kSlicesUVE; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(kSlicesUVE);
            const float angle = u * 2.0F * std::numbers::pi_v<float>;
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            geometry.vertices.push_back(Asset::MeshVertexUVE{
                {cosine * kRadiusUVE, y, sine * kRadiusUVE}, {cosine, 0.0F, sine}, u, v});
        }
        return base;
    };
    const std::uint32_t bottomRing = pushRing(-kHalfHeightUVE, 0.0F);
    const std::uint32_t topRing = pushRing(kHalfHeightUVE, 1.0F);
    for (std::uint32_t slice = 0U; slice < kSlicesUVE; ++slice) {
        const std::uint32_t a = bottomRing + slice;
        const std::uint32_t b = a + 1U;
        const std::uint32_t c = topRing + slice;
        const std::uint32_t d = c + 1U;
        const std::uint32_t corners[] = {a, b, c, d};
        constexpr std::uint32_t kQuadUVE[] = {0U, 2U, 3U, 0U, 3U, 1U};
        for (const std::uint32_t corner : kQuadUVE) {
            geometry.indices.push_back(corners[corner]);
        }
    }
    for (const float side : {1.0F, -1.0F}) {
        const std::uint32_t centre = static_cast<std::uint32_t>(geometry.vertices.size());
        geometry.vertices.push_back(
            Asset::MeshVertexUVE{{0.0F, side * kHalfHeightUVE, 0.0F}, {0.0F, side, 0.0F}, 0.5F, 0.5F});
        const std::uint32_t ring = static_cast<std::uint32_t>(geometry.vertices.size());
        for (std::uint32_t slice = 0U; slice <= kSlicesUVE; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(kSlicesUVE);
            const float angle = u * 2.0F * std::numbers::pi_v<float>;
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            geometry.vertices.push_back(Asset::MeshVertexUVE{
                {cosine * kRadiusUVE, side * kHalfHeightUVE, sine * kRadiusUVE}, {0.0F, side, 0.0F},
                (cosine * 0.5F) + 0.5F, (sine * 0.5F) + 0.5F});
        }
        for (std::uint32_t slice = 0U; slice < kSlicesUVE; ++slice) {
            const std::uint32_t a = ring + slice;
            const std::uint32_t b = a + 1U;
            // A fan around the centre: counter-clockwise seen from outside, which is clockwise
            // seen from +Y on the top and the other way on the bottom.
            if (side > 0.0F) {
                geometry.indices.insert(geometry.indices.end(), {centre, b, a});
            } else {
                geometry.indices.insert(geometry.indices.end(), {centre, a, b});
            }
        }
    }
    return geometry;
}

} // namespace

const PrimitiveGeometryUVE& GetPrimitiveGeometryUVE(const Scene::PrimitiveMeshKindUVE kind) noexcept {
    static const PrimitiveGeometryUVE cube = MakeCubeGeometryUVE();
    static const PrimitiveGeometryUVE sphere = MakeUVSphereGeometryUVE();
    static const PrimitiveGeometryUVE plane = MakePlaneGeometryUVE();
    static const PrimitiveGeometryUVE capsule = MakeCapsuleGeometryUVE();
    static const PrimitiveGeometryUVE cylinder = MakeCylinderGeometryUVE();
    switch (kind) {
        case Scene::PrimitiveMeshKindUVE::Cube:
            return cube;
        case Scene::PrimitiveMeshKindUVE::UVSphere:
            return sphere;
        case Scene::PrimitiveMeshKindUVE::Plane:
            return plane;
        case Scene::PrimitiveMeshKindUVE::Capsule:
            return capsule;
        case Scene::PrimitiveMeshKindUVE::Cylinder:
            return cylinder;
    }
    return cube;
}

} // namespace UVE::Render
