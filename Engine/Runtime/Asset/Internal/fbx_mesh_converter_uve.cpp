// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/fbx_mesh_converter_uve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ufbx.h>

#include "uve/logging/logging_macros_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Asset {

namespace {

struct SceneDeleterUVE final {
    void operator()(ufbx_scene* const scene) const noexcept { ufbx_free_scene(scene); }
};
using ScenePtrUVE = std::unique_ptr<ufbx_scene, SceneDeleterUVE>;

// Everything this engine reads is converted on load: metres, +Y up, right-handed. Applying the
// conversion to the geometry itself (not to a root transform) means the merged vertices need no
// further correction. Nothing outside the bytes handed in is ever opened.
[[nodiscard]] ufbx_load_opts MakeLoadOptionsUVE(const bool geometry) noexcept {
    ufbx_load_opts options{};
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0;
    options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    options.generate_missing_normals = true;
    options.load_external_files = false;
    options.ignore_embedded = true;
    options.ignore_animation = true;
    options.ignore_geometry = !geometry;
    // FBX only: the parser also reads OBJ, which has its own importer and rules here.
    options.file_format = UFBX_FILE_FORMAT_FBX;
    return options;
}

[[nodiscard]] ScenePtrUVE LoadSceneUVE(const std::span<const std::byte> source, const bool geometry) {
    if (source.empty() || source.size() > kMaximumFbxMeshSourceBytesUVE) {
        return nullptr;
    }
    const ufbx_load_opts options = MakeLoadOptionsUVE(geometry);
    ufbx_error error{};
    ScenePtrUVE scene{ufbx_load_memory(source.data(), source.size(), &options, &error)};
    if (!scene) {
        std::array<char, 512> message{};
        static_cast<void>(ufbx_format_error(message.data(), message.size(), &error));
        UVE_ERROR("FbxMeshConverterUVE: {}", message.data());
    }
    return scene;
}

[[nodiscard]] Math::Vector3UVE ToVectorUVE(const ufbx_vec3 value) noexcept {
    return Math::Vector3UVE{static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)};
}

[[nodiscard]] double MatrixDeterminantUVE(const ufbx_matrix& m) noexcept {
    return (m.m00 * ((m.m11 * m.m22) - (m.m12 * m.m21))) - (m.m01 * ((m.m10 * m.m22) - (m.m12 * m.m20))) +
           (m.m02 * ((m.m10 * m.m21) - (m.m11 * m.m20)));
}

[[nodiscard]] bool IsFiniteVertexUVE(const MeshVertexUVE& vertex) noexcept {
    return Math::IsFiniteUVE(vertex.position) && Math::IsFiniteUVE(vertex.normal) && std::isfinite(vertex.u) &&
           std::isfinite(vertex.v);
}

// Corners that agree on position, normal and UV are one vertex. Keyed on the exact bytes of those
// eight floats: a hash of the values would treat 0.0 and -0.0 as one and NaN as never equal, and
// sharing is only safe for corners that are bit-for-bit the same.
struct CornerKeyUVE final {
    std::array<float, 8> values{};
    bool operator==(const CornerKeyUVE& other) const noexcept {
        return std::memcmp(values.data(), other.values.data(), sizeof(values)) == 0;
    }
};

struct CornerKeyHashUVE final {
    std::size_t operator()(const CornerKeyUVE& key) const noexcept {
        std::uint64_t hash = 1469598103934665603ULL;
        const auto* const bytes = reinterpret_cast<const unsigned char*>(key.values.data());
        for (std::size_t index = 0U; index < sizeof(key.values); ++index) {
            hash = (hash ^ bytes[index]) * 1099511628211ULL;
        }
        return static_cast<std::size_t>(hash);
    }
};

[[nodiscard]] CornerKeyUVE MakeCornerKeyUVE(const MeshVertexUVE& vertex) noexcept {
    return CornerKeyUVE{{vertex.position.x, vertex.position.y, vertex.position.z, vertex.normal.x, vertex.normal.y,
                         vertex.normal.z, vertex.u, vertex.v}};
}

} // namespace

bool ConvertFbxMeshUVE(const std::span<const std::byte> source, MeshAssetUVE& outMesh) {
    try {
        const ScenePtrUVE scene = LoadSceneUVE(source, true);
        if (!scene) {
            return false;
        }

        MeshAssetUVE candidate;
        std::unordered_map<CornerKeyUVE, std::uint32_t, CornerKeyHashUVE> shared;
        std::vector<std::uint32_t> triangle;
        bool hasBounds = false;

        for (const ufbx_mesh* const mesh : scene->meshes) {
            if (mesh == nullptr || !mesh->vertex_position.exists || mesh->num_triangles == 0U) {
                continue;
            }
            triangle.assign(mesh->max_face_triangles * 3U, 0U);
            for (const ufbx_node* const node : mesh->instances) {
                const ufbx_matrix toWorld = node->geometry_to_world;
                const ufbx_matrix normalToWorld = ufbx_matrix_for_normals(&toWorld);
                // A mirrored instance (negative scale) turns its triangles inside out; swapping two
                // corners keeps them facing the way the surface does.
                const bool mirrored = MatrixDeterminantUVE(toWorld) < 0.0;
                for (const ufbx_face face : mesh->faces) {
                    const std::uint32_t triangles = ufbx_triangulate_face(triangle.data(), triangle.size(), mesh, face);
                    for (std::size_t corner = 0U; corner < static_cast<std::size_t>(triangles) * 3U; ++corner) {
                        // Mirrored: read each triangle as corners 0, 2, 1.
                        std::size_t pick = corner;
                        if (mirrored && corner % 3U == 1U) {
                            pick = corner + 1U;
                        } else if (mirrored && corner % 3U == 2U) {
                            pick = corner - 1U;
                        }
                        const std::uint32_t index = triangle[pick];
                        MeshVertexUVE vertex;
                        vertex.position = ToVectorUVE(
                            ufbx_transform_position(&toWorld, ufbx_get_vertex_vec3(&mesh->vertex_position, index)));
                        const ufbx_vec3 normal =
                            mesh->vertex_normal.exists
                                ? ufbx_transform_direction(&normalToWorld,
                                                           ufbx_get_vertex_vec3(&mesh->vertex_normal, index))
                                : ufbx_vec3{0.0, 1.0, 0.0};
                        vertex.normal = ToVectorUVE(normal);
                        const float length = std::sqrt((vertex.normal.x * vertex.normal.x) +
                                                       (vertex.normal.y * vertex.normal.y) +
                                                       (vertex.normal.z * vertex.normal.z));
                        vertex.normal = length > 0.0F && std::isfinite(length)
                                            ? Math::Vector3UVE{vertex.normal.x / length, vertex.normal.y / length,
                                                               vertex.normal.z / length}
                                            : Math::Vector3UVE{0.0F, 1.0F, 0.0F};
                        if (mesh->vertex_uv.exists) {
                            const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
                            vertex.u = static_cast<float>(uv.x);
                            vertex.v = static_cast<float>(uv.y);
                        }
                        if (!IsFiniteVertexUVE(vertex)) {
                            return false;
                        }

                        const auto [found, inserted] =
                            shared.try_emplace(MakeCornerKeyUVE(vertex), static_cast<std::uint32_t>(candidate.vertices.size()));
                        if (inserted) {
                            if (candidate.vertices.size() >= kMaximumFbxMeshVerticesUVE) {
                                UVE_ERROR("FbxMeshConverterUVE: more than {} vertices", kMaximumFbxMeshVerticesUVE);
                                return false;
                            }
                            candidate.vertices.push_back(vertex);
                            if (!hasBounds) {
                                candidate.localBounds = Math::AabbUVE{vertex.position, vertex.position};
                                hasBounds = true;
                            } else {
                                Math::AabbUVE& bounds = candidate.localBounds;
                                bounds.min = Math::Vector3UVE{std::min(bounds.min.x, vertex.position.x),
                                                              std::min(bounds.min.y, vertex.position.y),
                                                              std::min(bounds.min.z, vertex.position.z)};
                                bounds.max = Math::Vector3UVE{std::max(bounds.max.x, vertex.position.x),
                                                              std::max(bounds.max.y, vertex.position.y),
                                                              std::max(bounds.max.z, vertex.position.z)};
                            }
                        }
                        candidate.indices.push_back(found->second);
                    }
                }
            }
        }

        if (candidate.vertices.empty() || candidate.indices.empty() || candidate.indices.size() % 3U != 0U) {
            UVE_ERROR("FbxMeshConverterUVE: the file holds no triangles");
            return false;
        }
        if (!TryGenerateMeshTangentsUVE(candidate.vertices, candidate.indices)) {
            return false;
        }
        outMesh = std::move(candidate);
        return true;
    } catch (const std::bad_alloc&) {
        UVE_ERROR("FbxMeshConverterUVE: out of memory");
        return false;
    }
}

bool FbxSourceHasSkinUVE(const std::span<const std::byte> source) {
    try {
        const ScenePtrUVE scene = LoadSceneUVE(source, false);
        return scene && scene->skin_deformers.count > 0U;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace UVE::Asset
