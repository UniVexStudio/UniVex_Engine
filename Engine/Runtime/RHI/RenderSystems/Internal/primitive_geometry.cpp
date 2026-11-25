// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "primitive_geometry.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>

namespace UVE::Render {

namespace {

constexpr float kPrimitiveHalfExtent = 0.5F;
constexpr std::uint32_t kSphereSlices = 24U;
constexpr std::uint32_t kSphereStacks = 16U;
constexpr float kGeometryEpsilon = 0.000001F;

void AddOutwardTriangle(PrimitiveGeometry& geometry, std::uint32_t first, std::uint32_t second,
                           std::uint32_t third) {
    const Math::Vector3& a = geometry.vertices[first].position;
    const Math::Vector3& b = geometry.vertices[second].position;
    const Math::Vector3& c = geometry.vertices[third].position;
    const Math::Vector3 faceNormal = Math::Cross(b - a, c - a);
    const Math::Vector3 expectedOutward = a + b + c;
    if (Math::Dot(faceNormal, expectedOutward) < 0.0F) {
        std::swap(second, third);
    }
    geometry.indices.push_back(first);
    geometry.indices.push_back(second);
    geometry.indices.push_back(third);
}

[[nodiscard]] PrimitiveGeometry MakeCubeGeometry() {
    PrimitiveGeometry geometry;
    geometry.localBounds = Math::Aabb{{-kPrimitiveHalfExtent, -kPrimitiveHalfExtent, -kPrimitiveHalfExtent},
                                         {kPrimitiveHalfExtent, kPrimitiveHalfExtent, kPrimitiveHalfExtent}};
    struct Face final {
        Math::Vector3 normal;
        std::array<Math::Vector3, 4> positions;
    };
    const std::array<Face, 6> faces{
        Face{{1.0F, 0.0F, 0.0F}, {{{kPrimitiveHalfExtent, -kPrimitiveHalfExtent, -kPrimitiveHalfExtent},
                                      {kPrimitiveHalfExtent, -kPrimitiveHalfExtent, kPrimitiveHalfExtent},
                                      {kPrimitiveHalfExtent, kPrimitiveHalfExtent, kPrimitiveHalfExtent},
                                      {kPrimitiveHalfExtent, kPrimitiveHalfExtent, -kPrimitiveHalfExtent}}}},
        Face{{-1.0F, 0.0F, 0.0F}, {{{-kPrimitiveHalfExtent, -kPrimitiveHalfExtent, kPrimitiveHalfExtent},
                                       {-kPrimitiveHalfExtent, -kPrimitiveHalfExtent, -kPrimitiveHalfExtent},
                                       {-kPrimitiveHalfExtent, kPrimitiveHalfExtent, -kPrimitiveHalfExtent},
                                       {-kPrimitiveHalfExtent, kPrimitiveHalfExtent, kPrimitiveHalfExtent}}}},
        Face{{0.0F, 1.0F, 0.0F}, {{{-kPrimitiveHalfExtent, kPrimitiveHalfExtent, -kPrimitiveHalfExtent},
                                      {kPrimitiveHalfExtent, kPrimitiveHalfExtent, -kPrimitiveHalfExtent},
                                      {kPrimitiveHalfExtent, kPrimitiveHalfExtent, kPrimitiveHalfExtent},
                                      {-kPrimitiveHalfExtent, kPrimitiveHalfExtent, kPrimitiveHalfExtent}}}},
        Face{{0.0F, -1.0F, 0.0F}, {{{-kPrimitiveHalfExtent, -kPrimitiveHalfExtent, kPrimitiveHalfExtent},
                                       {kPrimitiveHalfExtent, -kPrimitiveHalfExtent, kPrimitiveHalfExtent},
                                       {kPrimitiveHalfExtent, -kPrimitiveHalfExtent, -kPrimitiveHalfExtent},
                                       {-kPrimitiveHalfExtent, -kPrimitiveHalfExtent, -kPrimitiveHalfExtent}}}},
        Face{{0.0F, 0.0F, 1.0F}, {{{-kPrimitiveHalfExtent, -kPrimitiveHalfExtent, kPrimitiveHalfExtent},
                                      {-kPrimitiveHalfExtent, kPrimitiveHalfExtent, kPrimitiveHalfExtent},
                                      {kPrimitiveHalfExtent, kPrimitiveHalfExtent, kPrimitiveHalfExtent},
                                      {kPrimitiveHalfExtent, -kPrimitiveHalfExtent, kPrimitiveHalfExtent}}}},
        Face{{0.0F, 0.0F, -1.0F}, {{{kPrimitiveHalfExtent, -kPrimitiveHalfExtent, -kPrimitiveHalfExtent},
                                       {kPrimitiveHalfExtent, kPrimitiveHalfExtent, -kPrimitiveHalfExtent},
                                       {-kPrimitiveHalfExtent, kPrimitiveHalfExtent, -kPrimitiveHalfExtent},
                                       {-kPrimitiveHalfExtent, -kPrimitiveHalfExtent, -kPrimitiveHalfExtent}}}},
    };
    constexpr std::array<std::array<float, 2>, 4> uvs{{{{0.0F, 0.0F}}, {{1.0F, 0.0F}}, {{1.0F, 1.0F}}, {{0.0F, 1.0F}}}};
    geometry.vertices.reserve(faces.size() * 4U);
    geometry.indices.reserve(faces.size() * 6U);
    for (const Face& face : faces) {
        const std::uint32_t base = static_cast<std::uint32_t>(geometry.vertices.size());
        for (std::size_t vertexIndex = 0U; vertexIndex < face.positions.size(); ++vertexIndex) {
            geometry.vertices.push_back(Asset::MeshVertex{face.positions[vertexIndex], face.normal,
                                                              uvs[vertexIndex][0], uvs[vertexIndex][1]});
        }
        AddOutwardTriangle(geometry, base, base + 1U, base + 2U);
        AddOutwardTriangle(geometry, base, base + 2U, base + 3U);
    }
    return geometry;
}

[[nodiscard]] PrimitiveGeometry MakePlaneGeometry() {
    PrimitiveGeometry geometry;
    geometry.localBounds = Math::Aabb{{-kPrimitiveHalfExtent, -kGeometryEpsilon, -kPrimitiveHalfExtent},
                                         {kPrimitiveHalfExtent, kGeometryEpsilon, kPrimitiveHalfExtent}};
    const Math::Vector3 normal{0.0F, 1.0F, 0.0F};
    geometry.vertices = {
        Asset::MeshVertex{{-kPrimitiveHalfExtent, 0.0F, -kPrimitiveHalfExtent}, normal, 0.0F, 0.0F},
        Asset::MeshVertex{{kPrimitiveHalfExtent, 0.0F, -kPrimitiveHalfExtent}, normal, 1.0F, 0.0F},
        Asset::MeshVertex{{kPrimitiveHalfExtent, 0.0F, kPrimitiveHalfExtent}, normal, 1.0F, 1.0F},
        Asset::MeshVertex{{-kPrimitiveHalfExtent, 0.0F, kPrimitiveHalfExtent}, normal, 0.0F, 1.0F},
    };
    // The plane is origin-centered, so its triangle centroid cannot determine outward direction.
    // Author the fixed +Y winding explicitly instead of using AddOutwardTriangle's radial test.
    geometry.indices = {0U, 2U, 1U, 0U, 3U, 2U};
    return geometry;
}

[[nodiscard]] PrimitiveGeometry MakeUVSphereGeometry() {
    PrimitiveGeometry geometry;
    geometry.localBounds = Math::Aabb{{-kPrimitiveHalfExtent, -kPrimitiveHalfExtent, -kPrimitiveHalfExtent},
                                         {kPrimitiveHalfExtent, kPrimitiveHalfExtent, kPrimitiveHalfExtent}};
    const std::uint32_t columns = kSphereSlices + 1U;
    geometry.vertices.reserve(static_cast<std::size_t>(columns) * (kSphereStacks + 1U));
    geometry.indices.reserve(static_cast<std::size_t>(kSphereSlices) * (6U * (kSphereStacks - 1U)));

    for (std::uint32_t stack = 0U; stack <= kSphereStacks; ++stack) {
        const float v = static_cast<float>(stack) / static_cast<float>(kSphereStacks);
        const float latitude = v * std::numbers::pi_v<float>;
        const float sineLatitude = std::sin(latitude);
        const float cosineLatitude = std::cos(latitude);
        for (std::uint32_t slice = 0U; slice <= kSphereSlices; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(kSphereSlices);
            const float longitude = u * 2.0F * std::numbers::pi_v<float>;
            const float sineLongitude = slice == kSphereSlices ? 0.0F : std::sin(longitude);
            const float cosineLongitude = slice == kSphereSlices ? 1.0F : std::cos(longitude);
            Math::Vector3 normal{sineLatitude * cosineLongitude, cosineLatitude,
                                    sineLatitude * sineLongitude};
            Math::Vector3 position = normal * kPrimitiveHalfExtent;
            if (stack == 0U) {
                normal = Math::Vector3{0.0F, 1.0F, 0.0F};
                position = normal * kPrimitiveHalfExtent;
            } else if (stack == kSphereStacks) {
                normal = Math::Vector3{0.0F, -1.0F, 0.0F};
                position = normal * kPrimitiveHalfExtent;
            }
            geometry.vertices.push_back(Asset::MeshVertex{position, normal, u, v});
        }
    }

    const auto indexAt = [](const std::uint32_t stack, const std::uint32_t slice) noexcept {
        return stack * (kSphereSlices + 1U) + slice;
    };
    for (std::uint32_t slice = 0U; slice < kSphereSlices; ++slice) {
        AddOutwardTriangle(geometry, indexAt(0U, slice), indexAt(1U, slice + 1U), indexAt(1U, slice));
    }
    for (std::uint32_t stack = 1U; stack + 1U < kSphereStacks; ++stack) {
        for (std::uint32_t slice = 0U; slice < kSphereSlices; ++slice) {
            const std::uint32_t topLeft = indexAt(stack, slice);
            const std::uint32_t topRight = indexAt(stack, slice + 1U);
            const std::uint32_t bottomLeft = indexAt(stack + 1U, slice);
            const std::uint32_t bottomRight = indexAt(stack + 1U, slice + 1U);
            AddOutwardTriangle(geometry, topLeft, topRight, bottomRight);
            AddOutwardTriangle(geometry, topLeft, bottomRight, bottomLeft);
        }
    }
    for (std::uint32_t slice = 0U; slice < kSphereSlices; ++slice) {
        AddOutwardTriangle(geometry, indexAt(kSphereStacks - 1U, slice),
                               indexAt(kSphereStacks - 1U, slice + 1U),
                               indexAt(kSphereStacks, slice));
    }
    return geometry;
}


/// A capsule standing along +Y: a barrel of radius 0.5 between two cap centres 0.5 either side of
/// the origin, closed by a hemisphere at each end. It is the shape the collider of the same name
/// describes, and the shape the half extents (0.5 wide, 1.0 tall) promise.
[[nodiscard]] PrimitiveGeometry MakeCapsuleGeometry() {
    constexpr float kRadius = 0.5F;
    constexpr float kHalfHeight = 1.0F;
    constexpr float kCapCentre = kHalfHeight - kRadius;
    constexpr std::uint32_t kSlices = 24U;
    // Rings BETWEEN the barrel and the pole. The pole is one vertex of its own: a ring at the pole
    // radius would be a degenerate circle, and the triangles reaching it would have no area.
    constexpr std::uint32_t kCapRings = 7U;
    constexpr std::uint32_t kRingVertices = kSlices + 1U;

    PrimitiveGeometry geometry;
    geometry.localBounds = Math::Aabb{{-kRadius, -kHalfHeight, -kRadius},
                                         {kRadius, kHalfHeight, kRadius}};

    // Barrel: two rings joined strip-wise. Winding is authored, not inferred: the strip runs
    // counter-clockwise seen from +Y, so (bottom, top) pairs face outward.
    const auto pushRing = [&](const float y, const float radius, const float v) {
        const std::uint32_t base = static_cast<std::uint32_t>(geometry.vertices.size());
        for (std::uint32_t slice = 0U; slice <= kSlices; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(kSlices);
            const float angle = u * 2.0F * std::numbers::pi_v<float>;
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            geometry.vertices.push_back(Asset::MeshVertex{
                {cosine * radius, y, sine * radius}, {cosine, 0.0F, sine}, u, v});
        }
        return base;
    };
    const std::uint32_t bottomRing = pushRing(-kCapCentre, kRadius, 0.5F);
    const std::uint32_t topRing = pushRing(kCapCentre, kRadius, 0.5F);
    for (std::uint32_t slice = 0U; slice < kSlices; ++slice) {
        const std::uint32_t a = bottomRing + slice;
        const std::uint32_t b = a + 1U;
        const std::uint32_t c = topRing + slice;
        const std::uint32_t d = c + 1U;
        constexpr std::uint32_t kQuad[] = {0U, 2U, 3U, 0U, 3U, 1U};
        const std::uint32_t corners[] = {a, b, c, d};
        for (const std::uint32_t corner : kQuad) {
            geometry.indices.push_back(corners[corner]);
        }
    }

    for (const float side : {1.0F, -1.0F}) {
        // The cap's own equator is the barrel's ring, so its centre is on the same side of the
        // origin: +0.5 for the top cap, -0.5 for the bottom. Getting this sign wrong builds the
        // bottom hemisphere inside the barrel and stretches a cone from it down to the pole, which
        // reads as a capsule with its lower half torn out.
        const float capCentre = side * kCapCentre;
        const std::uint32_t barrelRing = side > 0.0F ? topRing : bottomRing;

        // Ring 0 of the cap is one polar step in from the equator, not at it: the pole's ring would
        // be degenerate, and an equator ring would be a duplicate of the barrel's. The quad strip
        // below therefore starts at the barrel ring itself, which is what closes the seam. Without
        // that first strip the hemisphere floats one ring step above the barrel - a visible gap all
        // the way round, and the reason a capsule rendered as three separate pieces.
        const std::uint32_t firstRing = static_cast<std::uint32_t>(geometry.vertices.size());
        for (std::uint32_t stack = 1U; stack <= kCapRings; ++stack) {
            const float step = static_cast<float>(stack) / static_cast<float>(kCapRings + 1U);
            const float polar = step * (std::numbers::pi_v<float> * 0.5F);
            const float ringRadius = kRadius * std::cos(polar);
            const float y = capCentre + (side * kRadius * std::sin(polar));
            const float cosinePolar = std::cos(polar);
            for (std::uint32_t slice = 0U; slice <= kSlices; ++slice) {
                const float u = static_cast<float>(slice) / static_cast<float>(kSlices);
                const float angle = u * 2.0F * std::numbers::pi_v<float>;
                const float cosine = std::cos(angle);
                const float sine = std::sin(angle);
                geometry.vertices.push_back(Asset::MeshVertex{
                    {cosine * ringRadius, y, sine * ringRadius},
                    {cosine * cosinePolar, side * std::sin(polar), sine * cosinePolar}, u,
                    0.5F + (step * 0.5F)});
            }
        }

        // Quad strip from the barrel to the last cap ring, then a fan to the pole. Written as a
        // list of rings so the barrel is just ring zero of the same walk: the seam and the cap
        // cannot drift apart into two different winding rules.
        std::array<std::uint32_t, kCapRings + 1U> rings{};
        rings[0] = barrelRing;
        for (std::uint32_t stack = 0U; stack < kCapRings; ++stack) {
            rings[stack + 1U] = firstRing + (stack * kRingVertices);
        }
        for (std::uint32_t stack = 0U; stack + 1U < rings.size(); ++stack) {
            for (std::uint32_t slice = 0U; slice < kSlices; ++slice) {
                const std::uint32_t inner = rings[stack] + slice;
                const std::uint32_t outer = rings[stack + 1U] + slice;
                const std::uint32_t corners[] = {inner, inner + 1U, outer, outer + 1U};
                constexpr std::uint32_t kTopQuad[] = {0U, 2U, 3U, 0U, 3U, 1U};
                constexpr std::uint32_t kBottomQuad[] = {0U, 3U, 2U, 0U, 1U, 3U};
                const std::uint32_t* const order = side > 0.0F ? kTopQuad : kBottomQuad;
                for (std::size_t corner = 0U; corner < 6U; ++corner) {
                    geometry.indices.push_back(corners[order[corner]]);
                }
            }
        }

        const std::uint32_t lastRing = firstRing + ((kCapRings - 1U) * kRingVertices);
        const std::uint32_t pole = static_cast<std::uint32_t>(geometry.vertices.size());
        geometry.vertices.push_back(
            Asset::MeshVertex{{0.0F, side * kHalfHeight, 0.0F}, {0.0F, side, 0.0F}, 0.5F, 1.0F});
        for (std::uint32_t slice = 0U; slice < kSlices; ++slice) {
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
[[nodiscard]] PrimitiveGeometry MakeCylinderGeometry() {
    constexpr float kRadius = 0.5F;
    constexpr float kHalfHeight = 1.0F;
    constexpr std::uint32_t kSlices = 24U;

    PrimitiveGeometry geometry;
    geometry.localBounds = Math::Aabb{{-kRadius, -kHalfHeight, -kRadius},
                                         {kRadius, kHalfHeight, kRadius}};
    const auto pushRing = [&](const float y, const float v) {
        const std::uint32_t base = static_cast<std::uint32_t>(geometry.vertices.size());
        for (std::uint32_t slice = 0U; slice <= kSlices; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(kSlices);
            const float angle = u * 2.0F * std::numbers::pi_v<float>;
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            geometry.vertices.push_back(Asset::MeshVertex{
                {cosine * kRadius, y, sine * kRadius}, {cosine, 0.0F, sine}, u, v});
        }
        return base;
    };
    const std::uint32_t bottomRing = pushRing(-kHalfHeight, 0.0F);
    const std::uint32_t topRing = pushRing(kHalfHeight, 1.0F);
    for (std::uint32_t slice = 0U; slice < kSlices; ++slice) {
        const std::uint32_t a = bottomRing + slice;
        const std::uint32_t b = a + 1U;
        const std::uint32_t c = topRing + slice;
        const std::uint32_t d = c + 1U;
        const std::uint32_t corners[] = {a, b, c, d};
        constexpr std::uint32_t kQuad[] = {0U, 2U, 3U, 0U, 3U, 1U};
        for (const std::uint32_t corner : kQuad) {
            geometry.indices.push_back(corners[corner]);
        }
    }
    for (const float side : {1.0F, -1.0F}) {
        const std::uint32_t centre = static_cast<std::uint32_t>(geometry.vertices.size());
        geometry.vertices.push_back(
            Asset::MeshVertex{{0.0F, side * kHalfHeight, 0.0F}, {0.0F, side, 0.0F}, 0.5F, 0.5F});
        const std::uint32_t ring = static_cast<std::uint32_t>(geometry.vertices.size());
        for (std::uint32_t slice = 0U; slice <= kSlices; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(kSlices);
            const float angle = u * 2.0F * std::numbers::pi_v<float>;
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            geometry.vertices.push_back(Asset::MeshVertex{
                {cosine * kRadius, side * kHalfHeight, sine * kRadius}, {0.0F, side, 0.0F},
                (cosine * 0.5F) + 0.5F, (sine * 0.5F) + 0.5F});
        }
        for (std::uint32_t slice = 0U; slice < kSlices; ++slice) {
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

const PrimitiveGeometry& GetPrimitiveGeometry(const Scene::PrimitiveMeshKind kind) noexcept {
    static const PrimitiveGeometry cube = MakeCubeGeometry();
    static const PrimitiveGeometry sphere = MakeUVSphereGeometry();
    static const PrimitiveGeometry plane = MakePlaneGeometry();
    static const PrimitiveGeometry capsule = MakeCapsuleGeometry();
    static const PrimitiveGeometry cylinder = MakeCylinderGeometry();
    switch (kind) {
        case Scene::PrimitiveMeshKind::Cube:
            return cube;
        case Scene::PrimitiveMeshKind::UVSphere:
            return sphere;
        case Scene::PrimitiveMeshKind::Plane:
            return plane;
        case Scene::PrimitiveMeshKind::Capsule:
            return capsule;
        case Scene::PrimitiveMeshKind::Cylinder:
            return cylinder;
    }
    return cube;
}

const PrimitiveGeometry& GetPrimitiveLitGeometry(const Scene::PrimitiveMeshKind kind) noexcept {
    // One tangent-complete copy per primitive, built on first use and never rebuilt: the geometry is
    // immutable, so the only cost is the derivation itself. Keyed by the kind's underlying value so
    // a future enumerator that outgrows the cache falls back to the cube instead of indexing out.
    static std::array<std::optional<PrimitiveGeometry>, 5U> litGeometry;
    const auto slot = static_cast<std::size_t>(kind);
    if (slot >= litGeometry.size()) {
        return GetPrimitiveLitGeometry(Scene::PrimitiveMeshKind::Cube);
    }
    std::optional<PrimitiveGeometry>& cached = litGeometry[slot];
    if (!cached.has_value()) {
        PrimitiveGeometry geometry = GetPrimitiveGeometry(kind);
        // The material path is the shader's tangent-space normal mapping, so the basis has to come
        // from the triangles and UVs - the same derivation every imported mesh goes through, rather
        // than the placeholder (1,0,0) the geometry tables leave in place.
        Asset::GenerateMeshTangents(geometry.vertices, geometry.indices);
        cached.emplace(std::move(geometry));
    }
    return *cached;
}

} // namespace UVE::Render
