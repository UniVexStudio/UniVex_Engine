// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "obj_mesh_converter.h"

#include <algorithm>
#include <array>
#include <charconv>
#if defined(__ANDROID__)
#include <cerrno>
#include <cstdlib>
#endif
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "obj_metadata.h"
#include "vector2.h"
#include "logging_macros.h"
#include "vector3.h"

namespace UVE::Asset {
namespace {

constexpr float kDegenerateTriangleEpsilonSquared = 0.00000001F;

[[nodiscard]] bool IsFiniteBounds(const Math::Aabb& bounds) noexcept {
    return Math::IsFinite(bounds.min) && Math::IsFinite(bounds.max) &&
           bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y && bounds.min.z <= bounds.max.z;
}

[[nodiscard]] std::string_view NextToken(std::string_view& rest) noexcept {
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) {
        rest.remove_prefix(1U);
    }
    const std::size_t end = rest.find_first_of(" \t");
    const std::string_view token = rest.substr(0U, end);
    rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end);
    return token;
}

[[nodiscard]] std::optional<float> ParseFiniteFloat(const std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }
#if defined(__ANDROID__)
    std::string copy(text);
    char* end = nullptr;
    errno = 0;
    const float value = std::strtof(copy.c_str(), &end);
    if (errno == ERANGE || end != copy.c_str() + copy.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
#else
    float value = 0.0F;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value,
                                        std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
#endif
}

[[nodiscard]] bool ParsePosition(std::string_view rest, Math::Vector3& outPosition) noexcept {
    const std::optional<float> x = ParseFiniteFloat(NextToken(rest));
    const std::optional<float> y = ParseFiniteFloat(NextToken(rest));
    const std::optional<float> z = ParseFiniteFloat(NextToken(rest));
    if (!x || !y || !z) {
        return false;
    }

    const std::string_view weightText = NextToken(rest);
    if (!weightText.empty()) {
        const std::optional<float> weight = ParseFiniteFloat(weightText);
        if (!weight || std::abs(*weight) <= kDegenerateTriangleEpsilonSquared) {
            return false;
        }
        outPosition = Math::Vector3{*x / *weight, *y / *weight, *z / *weight};
    } else {
        outPosition = Math::Vector3{*x, *y, *z};
    }
    if (!NextToken(rest).empty()) {
        return false;
    }
    return std::isfinite(outPosition.x) && std::isfinite(outPosition.y) && std::isfinite(outPosition.z);
}

[[nodiscard]] bool ParseTexcoord(std::string_view rest, Math::Vector2& outTexcoord) noexcept {
    const std::optional<float> u = ParseFiniteFloat(NextToken(rest));
    const std::optional<float> v = ParseFiniteFloat(NextToken(rest));
    if (!u || !v) {
        return false;
    }
    if (!NextToken(rest).empty()) {
        return false;
    }
    outTexcoord = Math::Vector2{*u, *v};
    return true;
}

[[nodiscard]] bool ParseNormal(std::string_view rest, Math::Vector3& outNormal) noexcept {
    const std::optional<float> x = ParseFiniteFloat(NextToken(rest));
    const std::optional<float> y = ParseFiniteFloat(NextToken(rest));
    const std::optional<float> z = ParseFiniteFloat(NextToken(rest));
    if (!x || !y || !z) {
        return false;
    }
    if (!NextToken(rest).empty()) {
        return false;
    }
    outNormal = Math::Vector3{*x, *y, *z};
    const float lengthSquared = Math::LengthSquared(outNormal);
    return std::isfinite(lengthSquared) && lengthSquared > kDegenerateTriangleEpsilonSquared;
}

void UpdateBounds(const Math::Vector3& position, bool& hasBounds, Math::Aabb& bounds) noexcept {
    if (!hasBounds) {
        bounds = Math::Aabb{position, position};
        hasBounds = true;
        return;
    }
    bounds.min.x = std::min(bounds.min.x, position.x);
    bounds.min.y = std::min(bounds.min.y, position.y);
    bounds.min.z = std::min(bounds.min.z, position.z);
    bounds.max.x = std::max(bounds.max.x, position.x);
    bounds.max.y = std::max(bounds.max.y, position.y);
    bounds.max.z = std::max(bounds.max.z, position.z);
}

[[nodiscard]] Math::Vector3 ComputeFaceNormal(const Math::Vector3& first,
                                                     const Math::Vector3& second,
                                                     const Math::Vector3& third) noexcept {
    const Math::Vector3 firstEdge = second - first;
    const Math::Vector3 secondEdge = third - first;
    if (!Math::IsFinite(firstEdge) || !Math::IsFinite(secondEdge)) {
        return {};
    }
    const Math::Vector3 cross = Math::Cross(firstEdge, secondEdge);
    const float lengthSquared = Math::LengthSquared(cross);
    if (!std::isfinite(lengthSquared) || lengthSquared <= kDegenerateTriangleEpsilonSquared) {
        return {};
    }
    const Math::Vector3 normalized = Math::Normalize(cross);
    return Math::IsFinite(normalized) ? normalized : Math::Vector3{};
}

/// The `usemtl` argument: everything after the directive with the surrounding whitespace removed,
/// which is the same reading ImportObjMaterials takes of the same line. Material names may
/// contain spaces, so the remainder is kept whole.
[[nodiscard]] std::string_view MaterialArgument(std::string_view rest) noexcept {
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) {
        rest.remove_prefix(1U);
    }
    while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t')) {
        rest.remove_suffix(1U);
    }
    return rest;
}

} // namespace

bool ConvertObjMesh(const std::string_view source, MeshAsset& outMesh,
                       std::vector<ObjSurfaceRange>* const outSurfaces) {
    if (outSurfaces != nullptr) {
        outSurfaces->clear();
    }
    if (source.empty() || source.size() > kMaximumObjMeshSourceBytes) {
        return false;
    }

    std::vector<Math::Vector3> positions;
    std::vector<Math::Vector2> texcoords;
    std::vector<Math::Vector3> normals;
    positions.reserve(64U);
    texcoords.reserve(64U);
    normals.reserve(64U);

    MeshAsset candidate;
    bool hasBounds = false;

    // Surface tracking. Runs are recorded only while they are still worth saving: once a file has
    // more `usemtl` runs than a mesh can hold, `surfacesOverflowed` stops the bookkeeping and the
    // result keeps the whole-buffer draw instead of a table that could never be saved.
    std::string currentMaterial;
    std::vector<ObjSurfaceRange> surfaceRanges;
    bool surfacesOverflowed = false;
    const auto beginSurfaceRun = [&](const std::size_t firstIndex) {
        if (outSurfaces == nullptr || surfacesOverflowed) {
            return;
        }
        if (!surfaceRanges.empty()) {
            surfaceRanges.back().indexCount =
                static_cast<std::uint32_t>(firstIndex) - surfaceRanges.back().firstIndex;
            if (surfaceRanges.back().materialName == currentMaterial) {
                return; // same material continuing a run
            }
        }
        if (surfaceRanges.size() >= kMaximumMeshSurfaces) {
            surfacesOverflowed = true;
            surfaceRanges.clear();
            return;
        }
        surfaceRanges.push_back(
            ObjSurfaceRange{static_cast<std::uint32_t>(firstIndex), 0U, currentMaterial});
    };

    std::size_t lineStart = 0U;
    while (lineStart <= source.size()) {
        const std::size_t lineEnd = source.find('\n', lineStart);
        std::string_view line = source.substr(lineStart, lineEnd == std::string_view::npos
                                                        ? std::string_view::npos
                                                        : lineEnd - lineStart);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1U);
        }
        const std::size_t commentStart = line.find('#');
        if (commentStart != std::string_view::npos) {
            line = line.substr(0U, commentStart);
        }
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.remove_prefix(1U);
        }

        if (!line.empty()) {
            std::string_view rest = line;
            const std::string_view directive = NextToken(rest);
            if (directive == "v") {
                if (positions.size() >= kMaximumObjMeshVertices) {
                    return false;
                }
                Math::Vector3 position;
                if (!ParsePosition(rest, position)) {
                    return false;
                }
                positions.push_back(position);
            } else if (directive == "vt") {
                if (texcoords.size() >= kMaximumObjMeshVertices) {
                    return false;
                }
                Math::Vector2 texcoord;
                if (!ParseTexcoord(rest, texcoord)) {
                    return false;
                }
                texcoords.push_back(texcoord);
            } else if (directive == "vn") {
                if (normals.size() >= kMaximumObjMeshVertices) {
                    return false;
                }
                Math::Vector3 normal;
                if (!ParseNormal(rest, normal)) {
                    return false;
                }
                normals.push_back(Math::Normalize(normal));
            } else if (directive == "usemtl") {
                currentMaterial.assign(MaterialArgument(rest));
            } else if (directive == "f") {
                std::vector<ObjFaceVertex> faceVertices;
                faceVertices.reserve(8U);
                while (true) {
                    const std::string_view token = NextToken(rest);
                    if (token.empty()) {
                        break;
                    }
                    if (faceVertices.size() >= kMaximumObjMeshVertices / 3U + 2U) {
                        return false;
                    }
                    ObjFaceVertex faceVertex;
                    if (!ResolveObjFaceVertex(token, static_cast<std::uint32_t>(positions.size()),
                                                 static_cast<std::uint32_t>(texcoords.size()),
                                                 static_cast<std::uint32_t>(normals.size()), faceVertex)) {
                        return false;
                    }
                    faceVertices.push_back(faceVertex);
                }
                if (faceVertices.size() < 3U) {
                    return false;
                }

                for (std::size_t triangleIndex = 1U; triangleIndex + 1U < faceVertices.size(); ++triangleIndex) {
                    const std::array<ObjFaceVertex, 3> triangle{
                        faceVertices[0], faceVertices[triangleIndex], faceVertices[triangleIndex + 1U]};
                    const Math::Vector3 firstPosition = positions[triangle[0].positionIndex];
                    const Math::Vector3 secondPosition = positions[triangle[1].positionIndex];
                    const Math::Vector3 thirdPosition = positions[triangle[2].positionIndex];
                    const Math::Vector3 faceNormal =
                        ComputeFaceNormal(firstPosition, secondPosition, thirdPosition);
                    if (Math::LengthSquared(faceNormal) <= kDegenerateTriangleEpsilonSquared ||
                        candidate.vertices.size() > kMaximumObjMeshVertices - 3U) {
                        return false;
                    }

                    const std::array<Math::Vector3, 3> trianglePositions{
                        firstPosition, secondPosition, thirdPosition};
                    // One run per material, opened at the first index of the triangle that uses it.
                    // A zero-length run cannot be emitted: the check happens before the three
                    // indices are appended, so every run owns at least one whole triangle.
                    beginSurfaceRun(candidate.indices.size());
                    for (std::size_t corner = 0U; corner < triangle.size(); ++corner) {
                        const ObjFaceVertex& faceVertex = triangle[corner];
                        MeshVertex vertex;
                        vertex.position = trianglePositions[corner];
                        vertex.normal = faceVertex.normalIndex.has_value()
                                            ? normals[*faceVertex.normalIndex]
                                            : faceNormal;
                        if (faceVertex.texcoordIndex.has_value()) {
                            const Math::Vector2& texcoord = texcoords[*faceVertex.texcoordIndex];
                            vertex.u = texcoord.x;
                            vertex.v = texcoord.y;
                        }
                        candidate.vertices.push_back(vertex);
                        candidate.indices.push_back(static_cast<std::uint32_t>(candidate.vertices.size() - 1U));
                        UpdateBounds(vertex.position, hasBounds, candidate.localBounds);
                    }
                }
            }
        }

        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1U;
    }

    if (candidate.vertices.empty() || candidate.indices.size() != candidate.vertices.size() || !hasBounds) {
        return false;
    }
    if (outSurfaces != nullptr) {
        if (!surfacesOverflowed && !surfaceRanges.empty()) {
            surfaceRanges.back().indexCount = static_cast<std::uint32_t>(candidate.indices.size()) -
                                              surfaceRanges.back().firstIndex;
        }
        // One run is the whole buffer, which is what having no ranges already means.
        if (surfacesOverflowed || surfaceRanges.size() < 2U) {
            surfaceRanges.clear();
        }
        outSurfaces->swap(surfaceRanges);
    }
    if (!TryGenerateMeshTangents(candidate.vertices, candidate.indices)) {
        return false;
    }
    if (!IsFiniteBounds(candidate.localBounds)) {
        return false;
    }
    for (const MeshVertex& vertex : candidate.vertices) {
        if (!Math::IsFinite(vertex.position) || !Math::IsFinite(vertex.normal) ||
            !std::isfinite(vertex.u) || !std::isfinite(vertex.v) || !Math::IsFinite(vertex.tangent) ||
            !std::isfinite(vertex.tangentHandedness)) {
            return false;
        }
    }
    outMesh = std::move(candidate);
    return true;
}

} // namespace UVE::Asset
