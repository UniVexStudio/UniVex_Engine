// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/navigation/navmesh_uve.h"

#include <algorithm>
#include <cmath>
#include <limits>


namespace UVE::Navigation {

namespace {

/// The XZ cross product's y-sign of (b - a) x (p - a), the half-plane test every convex polygon
/// query is built from. Its sign convention is SignedAreaXZUVE()'s, by construction: the same
/// expression, summed over a polygon's edges, IS its signed area.
[[nodiscard]] float EdgeSideUVE(const Math::Vector3UVE& a, const Math::Vector3UVE& b,
                                const Math::Vector3UVE& point) noexcept {
    return (b.x - a.x) * (point.z - a.z) - (b.z - a.z) * (point.x - a.x);
}

[[nodiscard]] bool IsInsidePolygonXZUVE(const NavmeshPolygonUVE& polygon, const Math::Vector3UVE& point,
                                        const float windingSign) noexcept {
    for (std::uint32_t vertexIndex = 0U; vertexIndex < polygon.vertexCount; ++vertexIndex) {
        const Math::Vector3UVE& a = polygon.vertices[vertexIndex];
        const Math::Vector3UVE& b = polygon.vertices[(vertexIndex + 1U) % polygon.vertexCount];
        if (EdgeSideUVE(a, b, point) * windingSign < 0.0F) {
            return false;
        }
    }
    return true;
}

/// The height a polygon's surface has above (x, z).
///
/// A baked polygon is an axis-aligned rectangle whose four corner heights came from the cells
/// around each corner, so the surface between them is bilinear. Interpolating is what makes a
/// polygon that spans a gentle slope follow it: a flat average would leave every waypoint on such a
/// polygon floating above the uphill half and buried in the downhill half.
[[nodiscard]] float SurfaceHeightUVE(const NavmeshPolygonUVE& polygon, const float x, const float z) noexcept {
    float minimumX = polygon.vertices[0].x;
    float maximumX = polygon.vertices[0].x;
    float minimumZ = polygon.vertices[0].z;
    float maximumZ = polygon.vertices[0].z;
    for (std::uint32_t vertexIndex = 1U; vertexIndex < polygon.vertexCount; ++vertexIndex) {
        minimumX = std::min(minimumX, polygon.vertices[vertexIndex].x);
        maximumX = std::max(maximumX, polygon.vertices[vertexIndex].x);
        minimumZ = std::min(minimumZ, polygon.vertices[vertexIndex].z);
        maximumZ = std::max(maximumZ, polygon.vertices[vertexIndex].z);
    }
    const float spanX = maximumX - minimumX;
    const float spanZ = maximumZ - minimumZ;
    if (!(spanX > 0.0F) || !(spanZ > 0.0F)) {
        return polygon.center.y;
    }

    // Corner heights by position rather than by index: the bake winds a rectangle
    // counter-clockwise, but reading a height off the corner it belongs to must not depend on that
    // choice surviving a future shape.
    float lowLow = 0.0F;
    float highLow = 0.0F;
    float lowHigh = 0.0F;
    float highHigh = 0.0F;
    std::size_t cornerCount = 0U;
    for (std::uint32_t vertexIndex = 0U; vertexIndex < polygon.vertexCount; ++vertexIndex) {
        const Math::Vector3UVE& vertex = polygon.vertices[vertexIndex];
        const bool atMinimumX = std::fabs(vertex.x - minimumX) <= std::fabs(vertex.x - maximumX);
        const bool atMinimumZ = std::fabs(vertex.z - minimumZ) <= std::fabs(vertex.z - maximumZ);
        if (atMinimumX && atMinimumZ) {
            lowLow = vertex.y;
        } else if (!atMinimumX && atMinimumZ) {
            highLow = vertex.y;
        } else if (atMinimumX) {
            lowHigh = vertex.y;
        } else {
            highHigh = vertex.y;
        }
        ++cornerCount;
    }
    if (cornerCount != 4U) {
        return polygon.center.y;
    }

    const float u = std::clamp((x - minimumX) / spanX, 0.0F, 1.0F);
    const float v = std::clamp((z - minimumZ) / spanZ, 0.0F, 1.0F);
    const float low = lowLow + (highLow - lowLow) * u;
    const float high = lowHigh + (highHigh - lowHigh) * u;
    return low + (high - low) * v;
}

/// The point of `polygon` closest to `point`, in the XZ plane, lifted onto the polygon's surface.
[[nodiscard]] Math::Vector3UVE ClosestPointUVE(const NavmeshPolygonUVE& polygon, const Math::Vector3UVE& point,
                                               const float windingSign) noexcept {
    if (IsInsidePolygonXZUVE(polygon, point, windingSign)) {
        return Math::Vector3UVE{point.x, SurfaceHeightUVE(polygon, point.x, point.z), point.z};
    }
    Math::Vector3UVE closest{};
    float closestSquaredDistance = std::numeric_limits<float>::max();
    for (std::uint32_t vertexIndex = 0U; vertexIndex < polygon.vertexCount; ++vertexIndex) {
        const Math::Vector3UVE& a = polygon.vertices[vertexIndex];
        const Math::Vector3UVE& b = polygon.vertices[(vertexIndex + 1U) % polygon.vertexCount];
        const float edgeX = b.x - a.x;
        const float edgeZ = b.z - a.z;
        const float edgeLengthSquared = edgeX * edgeX + edgeZ * edgeZ;
        float t = 0.0F;
        if (edgeLengthSquared > 0.0F) {
            t = std::clamp(((point.x - a.x) * edgeX + (point.z - a.z) * edgeZ) / edgeLengthSquared, 0.0F, 1.0F);
        }
        const float candidateX = a.x + edgeX * t;
        const float candidateZ = a.z + edgeZ * t;
        const float deltaX = point.x - candidateX;
        const float deltaZ = point.z - candidateZ;
        const float squaredDistance = deltaX * deltaX + deltaZ * deltaZ;
        if (squaredDistance < closestSquaredDistance) {
            closestSquaredDistance = squaredDistance;
            closest = Math::Vector3UVE{candidateX, a.y + (b.y - a.y) * t, candidateZ};
        }
    }
    return closest;
}

/// Whether `point` sits within a traversable band of `polygon`'s surface. The band is the bake's own
/// step height, so a point that can be walked to from a neighbouring polygon is also a point this
/// polygon accepts as "here" - one tolerance, not two that disagree at a ledge.
[[nodiscard]] bool IsWithinSurfaceBandUVE(const NavmeshUVE& mesh, const NavmeshPolygonUVE& polygon,
                                          const Math::Vector3UVE& point) noexcept {
    const float surface = SurfaceHeightUVE(polygon, point.x, point.z);
    return std::fabs(point.y - surface) <= std::max(mesh.maximumStepHeight, 0.0F);
}

} // namespace

float SignedAreaXZUVE(const Math::Vector3UVE* vertices, const std::size_t vertexCount) noexcept {
    if (vertices == nullptr || vertexCount < 3U) {
        return 0.0F;
    }
    float twiceArea = 0.0F;
    for (std::size_t vertexIndex = 0U; vertexIndex < vertexCount; ++vertexIndex) {
        const Math::Vector3UVE& a = vertices[vertexIndex];
        const Math::Vector3UVE& b = vertices[(vertexIndex + 1U) % vertexCount];
        twiceArea += a.x * b.z - b.x * a.z;
    }
    return twiceArea * 0.5F;
}

void NavmeshUVE::ClearUVE() noexcept {
    polygons.clear();
    portals.clear();
    bounds = Math::AabbUVE{};
    navigationLayers = 1U;
    cellSize = 0.0F;
    agentRadius = 0.0F;
    agentHeight = 0.0F;
    maximumSlopeDegrees = 0.0F;
    maximumStepHeight = 0.0F;
}

bool NavmeshUVE::IsEmptyUVE() const noexcept {
    return polygons.empty();
}

std::size_t NavmeshUVE::GetPolygonCountUVE() const noexcept {
    return polygons.size();
}

std::size_t NavmeshUVE::GetPortalCountUVE() const noexcept {
    return portals.size();
}

float NavmeshUVE::GetWalkableAreaSquareMetresUVE() const noexcept {
    float area = 0.0F;
    for (const NavmeshPolygonUVE& polygon : polygons) {
        area += polygon.areaSquareMetres;
    }
    return area;
}

std::optional<std::size_t> NavmeshUVE::FindPolygonUVE(const Math::Vector3UVE& worldPoint) const noexcept {
    for (std::size_t polygonIndex = 0U; polygonIndex < polygons.size(); ++polygonIndex) {
        const NavmeshPolygonUVE& polygon = polygons[polygonIndex];
        if (polygon.vertexCount < 3U) {
            continue;
        }
        const float windingSign = SignedAreaXZUVE(polygon.vertices.data(), polygon.vertexCount) >= 0.0F ? 1.0F : -1.0F;
        if (IsInsidePolygonXZUVE(polygon, worldPoint, windingSign) &&
            IsWithinSurfaceBandUVE(*this, polygon, worldPoint)) {
            return polygonIndex;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> NavmeshUVE::FindNearestPolygonUVE(const Math::Vector3UVE& worldPoint,
                                                            const float maximumDistanceMetres,
                                                            const std::uint32_t layerMask) const noexcept {
    if (!(maximumDistanceMetres >= 0.0F)) {
        return std::nullopt;
    }
    std::optional<std::size_t> nearest;
    float nearestDistance = maximumDistanceMetres;
    for (std::size_t polygonIndex = 0U; polygonIndex < polygons.size(); ++polygonIndex) {
        const NavmeshPolygonUVE& polygon = polygons[polygonIndex];
        if (polygon.vertexCount < 3U || (polygon.navigationLayers & layerMask) == 0U) {
            continue;
        }
        // A cheap rejection first: a polygon whose centre is further away than its own radius plus
        // the budget cannot be the nearest one, and this keeps the search off the edge-clamping
        // arithmetic for most of a large mesh.
        const float centerDistance = Math::LengthUVE(polygon.center - worldPoint);
        if (centerDistance - std::sqrt(polygon.areaSquareMetres) > nearestDistance) {
            continue;
        }
        const float windingSign = SignedAreaXZUVE(polygon.vertices.data(), polygon.vertexCount) >= 0.0F ? 1.0F : -1.0F;
        const Math::Vector3UVE closest = ClosestPointUVE(polygon, worldPoint, windingSign);
        const float distance = Math::LengthUVE(closest - worldPoint);
        if (distance <= nearestDistance) {
            nearestDistance = distance;
            nearest = polygonIndex;
        }
    }
    return nearest;
}

std::optional<Math::Vector3UVE> NavmeshUVE::ProjectPointUVE(const Math::Vector3UVE& worldPoint,
                                                            const std::size_t polygon) const noexcept {
    if (polygon >= polygons.size() || polygons[polygon].vertexCount < 3U) {
        return std::nullopt;
    }
    const NavmeshPolygonUVE& target = polygons[polygon];
    const float windingSign = SignedAreaXZUVE(target.vertices.data(), target.vertexCount) >= 0.0F ? 1.0F : -1.0F;
    return ClosestPointUVE(target, worldPoint, windingSign);
}

Math::Vector3UVE NavmeshUVE::PolygonCenterUVE(const std::size_t polygon) const noexcept {
    if (polygon >= polygons.size()) {
        return Math::Vector3UVE{};
    }
    return polygons[polygon].center;
}

} // namespace UVE::Navigation
