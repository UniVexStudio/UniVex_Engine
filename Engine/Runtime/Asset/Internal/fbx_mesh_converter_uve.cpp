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
#include <optional>
#include <set>
#include <string>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ufbx.h>

#include "uve/logging/logging_macros_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/utilities/hash_uve.h"

namespace UVE::Asset {

namespace {

struct SceneDeleterUVE final {
    void operator()(ufbx_scene* const scene) const noexcept { ufbx_free_scene(scene); }
};
using ScenePtrUVE = std::unique_ptr<ufbx_scene, SceneDeleterUVE>;

// Everything this engine reads is converted on load: metres, +Y up, right-handed. Applying the
// conversion to the geometry itself (not to a root transform) means the merged vertices need no
// further correction. Nothing outside the bytes handed in is ever opened.
[[nodiscard]] ufbx_load_opts MakeLoadOptionsUVE(const bool geometry, const bool animation = false) noexcept {
    ufbx_load_opts options{};
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0;
    options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    options.generate_missing_normals = true;
    options.load_external_files = false;
    options.ignore_embedded = false;
    options.ignore_animation = !animation;
    options.ignore_geometry = !geometry;
    // FBX only: the parser also reads OBJ, which has its own importer and rules here.
    options.file_format = UFBX_FILE_FORMAT_FBX;
    return options;
}

[[nodiscard]] ScenePtrUVE LoadSceneUVE(const std::span<const std::byte> source, const bool geometry,
                                       const bool animation = false) {
    if (source.empty() || source.size() > kMaximumFbxMeshSourceBytesUVE) {
        return nullptr;
    }
    const ufbx_load_opts options = MakeLoadOptionsUVE(geometry, animation);
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
    std::array<float, 16> values{};
    bool operator==(const CornerKeyUVE& other) const noexcept {
        return std::memcmp(values.data(), other.values.data(), sizeof(values)) == 0;
    }
};

struct CornerKeyHashUVE final {
    std::size_t operator()(const CornerKeyUVE& key) const noexcept {
        // Keeps the legacy transposed seed this table shipped with (see
        // kFnv1a64LegacyOffsetBasisUVE): same values as before, now via the shared hasher.
        return static_cast<std::size_t>(Utilities::HashBytesUVE(
            key.values.data(), sizeof(key.values), Utilities::kFnv1a64LegacyOffsetBasisUVE));
    }
};

/// Corners that share a position but not their skin weights are two vertices: the influences are
/// part of the key (all zero for a static mesh).
[[nodiscard]] CornerKeyUVE MakeCornerKeyUVE(const MeshVertexUVE& vertex, const MeshSkinningInfluenceUVE& skin) noexcept {
    return CornerKeyUVE{{vertex.position.x, vertex.position.y, vertex.position.z, vertex.normal.x, vertex.normal.y,
                         vertex.normal.z, vertex.u, vertex.v, static_cast<float>(skin.joints[0]),
                         static_cast<float>(skin.joints[1]), static_cast<float>(skin.joints[2]),
                         static_cast<float>(skin.joints[3]), skin.weights[0], skin.weights[1], skin.weights[2],
                         skin.weights[3]}};
}

[[nodiscard]] Math::Matrix4x4UVE ToMatrixUVE(const ufbx_matrix& m) noexcept {
    Math::Matrix4x4UVE result;
    const double rows[3][4] = {{m.m00, m.m01, m.m02, m.m03}, {m.m10, m.m11, m.m12, m.m13}, {m.m20, m.m21, m.m22, m.m23}};
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            result.m[row][column] = static_cast<float>(rows[row][column]);
        }
    }
    return result;
}


/// One bone of the file, in the order the skeleton lists them: parents first.
struct FbxBoneObjectUVE final {
    const ufbx_node* object = nullptr;
    std::string name;
    std::int32_t parentIndex = -1;
    /// The objects between this bone and its parent bone (or the root), nearest first: a group or
    /// null an exporter put in the chain. Their transforms are folded into the bone's local pose.
    std::vector<const ufbx_node*> between;
};

/// What to do when a file holds more bones than the caller can take.
enum class FbxBoneOverflowUVE : std::uint8_t {
    /// Refuse: the caller cannot represent a partial rig, so a partial answer would be a lie.
    Fail = 0,
    /// Keep the first `maximumBones` and report how many were left out. A mesh whose skin refers to
    /// a dropped bone does not break: its weights are skipped and the rest renormalized, and a part
    /// riding a dropped bone falls back to the nearest kept ancestor. Bending a hand wrong is a far
    /// better outcome than a character that does not import at all.
    Truncate,
};

/// Every bone object, depth first from the root, with unique names - the one walk both the skeleton
/// and its clips use, so a clip's track names always match the skeleton's bones.
///
/// Depth first order is what makes truncation safe: a parent is always collected before its
/// children, so the first `maximumBones` bones never contain one whose parent was left out.
/// `outDropped`, when non-null, receives the number of bones the cap excluded. Empty optional when
/// the cap is exceeded and `overflow` is Fail.
[[nodiscard]] std::optional<std::vector<FbxBoneObjectUVE>> CollectFbxBonesUVE(
    const ufbx_scene& scene, const std::size_t maximumBones,
    const FbxBoneOverflowUVE overflow = FbxBoneOverflowUVE::Fail, std::size_t* const outDropped = nullptr) {
    std::vector<FbxBoneObjectUVE> bones;
    std::unordered_map<const ufbx_node*, std::int32_t> boneOfObject;
    std::set<std::string> usedNames;
    std::size_t dropped = 0U;
    std::vector<const ufbx_node*> pending{scene.root_node};
    while (!pending.empty()) {
        const ufbx_node* const object = pending.back();
        pending.pop_back();
        for (std::size_t child = object->children.count; child > 0U; --child) {
            pending.push_back(object->children.data[child - 1U]);
        }
        if (object->bone == nullptr) {
            continue;
        }
        if (bones.size() >= maximumBones) {
            if (overflow == FbxBoneOverflowUVE::Fail) {
                UVE_ERROR("FbxMeshConverterUVE: more than {} bones", maximumBones);
                if (outDropped != nullptr) {
                    *outDropped = 0U;
                }
                return std::nullopt;
            }
            ++dropped; // still walked, so the count is every bone the cap excluded
            continue;
        }
        FbxBoneObjectUVE bone;
        bone.object = object;
        for (const ufbx_node* ancestor = object->parent; ancestor != nullptr; ancestor = ancestor->parent) {
            if (const auto found = boneOfObject.find(ancestor); found != boneOfObject.end()) {
                bone.parentIndex = found->second;
                break;
            }
            bone.between.push_back(ancestor);
        }
        const std::string name(object->name.data, object->name.length);
        std::string unique = name.empty() ? std::string("Bone") : name;
        const std::string stem = unique;
        for (int suffix = 1; usedNames.count(unique) != 0U; ++suffix) {
            unique = stem + "_" + std::to_string(suffix);
        }
        usedNames.insert(unique);
        bone.name = std::move(unique);
        boneOfObject.emplace(object, static_cast<std::int32_t>(bones.size()));
        bones.push_back(std::move(bone));
    }
    if (outDropped != nullptr) {
        *outDropped = dropped;
    }
    return bones;
}

[[nodiscard]] bool IsFiniteTransformUVE(const ufbx_transform& pose) noexcept {
    return std::isfinite(pose.translation.x) && std::isfinite(pose.translation.y) && std::isfinite(pose.translation.z) &&
           std::isfinite(pose.rotation.x) && std::isfinite(pose.rotation.y) && std::isfinite(pose.rotation.z) &&
           std::isfinite(pose.rotation.w) && std::isfinite(pose.scale.x) && std::isfinite(pose.scale.y) &&
           std::isfinite(pose.scale.z);
}

[[nodiscard]] AnimationAssetPoseUVE ToAnimationPoseUVE(const ufbx_transform& pose) noexcept {
    return AnimationAssetPoseUVE{ToVectorUVE(pose.translation),
                                 Math::QuaternionUVE{static_cast<float>(pose.rotation.x),
                                                     static_cast<float>(pose.rotation.y),
                                                     static_cast<float>(pose.rotation.z),
                                                     static_cast<float>(pose.rotation.w)},
                                 ToVectorUVE(pose.scale)};
}

} // namespace

bool ConvertFbxMeshUVE(const std::span<const std::byte> source, MeshAssetUVE& outMesh,
                       std::vector<FbxSurfaceRangeUVE>* const outSurfaces, const std::size_t maximumJoints) {
    if (outSurfaces != nullptr) {
        outSurfaces->clear();
    }
    try {
        const ScenePtrUVE scene = LoadSceneUVE(source, true);
        if (!scene) {
            return false;
        }

        MeshAssetUVE candidate;
        std::unordered_map<CornerKeyUVE, std::uint32_t, CornerKeyHashUVE> shared;

        // Material index per file material, so a mesh part can name the material it draws with.
        // Built from the scene's own list: a node's `materials` are pointers into it, possibly a
        // different subset per instance, and only the file's order is what the importer publishes.
        std::unordered_map<const ufbx_material*, std::uint32_t> materialSourceIndex;
        if (outSurfaces != nullptr) {
            materialSourceIndex.reserve(scene->materials.count);
            for (std::size_t materialIndex = 0U; materialIndex < scene->materials.count; ++materialIndex) {
                const ufbx_material* const material = scene->materials.data[materialIndex];
                if (material != nullptr) {
                    materialSourceIndex.emplace(material, static_cast<std::uint32_t>(materialIndex));
                }
            }
        }

        // Triangles are collected per material instead of straight into the index buffer. The file
        // may interleave materials face by face, and a surface table has to be one contiguous run
        // per material to be worth saving - so the runs are built here and concatenated below, in
        // first-use order. Without `outSurfaces` there is a single bucket and the order is exactly
        // the order the faces appear in, which is what this converter did before.
        struct IndexBucketUVE final {
            std::uint32_t materialSourceIndex = kInvalidMeshSurfaceMaterialSlotUVE;
            std::vector<std::uint32_t> indices;
        };
        std::vector<IndexBucketUVE> indexBuckets(1U);
        std::unordered_map<std::uint32_t, std::size_t> bucketOfMaterial;
        const auto bucketForMaterialUVE = [&](const std::uint32_t material) -> std::size_t {
            if (outSurfaces == nullptr) {
                return 0U;
            }
            const auto [found, inserted] = bucketOfMaterial.try_emplace(material, indexBuckets.size());
            if (inserted) {
                indexBuckets.push_back(IndexBucketUVE{material, {}});
            }
            return found->second;
        };

        // A skinned file keeps its skin: every bone becomes a joint (the same bones, order and
        // names the skeleton and its clips use), and each corner its strongest four influences.
        std::unordered_map<const ufbx_node*, std::uint32_t> jointOfObject;
        const bool skinned = scene->skin_deformers.count > 0U;
        if (skinned) {
            // Truncating rather than refusing: a rig with more bones than a joint list can hold
            // still produces a usable mesh, and the alternative - failing the whole conversion -
            // leaves a character that does not import at all.
            std::size_t droppedBones = 0U;
            const std::optional<std::vector<FbxBoneObjectUVE>> bones =
                CollectFbxBonesUVE(*scene, maximumJoints, FbxBoneOverflowUVE::Truncate, &droppedBones);
            if (!bones.has_value() || bones->empty()) {
                return false;
            }
            if (droppedBones > 0U) {
                UVE_WARNING("FbxMeshConverterUVE: the rig holds {} bones, over the {} bone limit; the mesh keeps "
                            "the first {} and drops {}, whose weights are renormalized onto the bones that remain",
                            maximumJoints + droppedBones, maximumJoints, bones->size(), droppedBones);
            }
            for (const FbxBoneObjectUVE& bone : *bones) {
                MeshJointUVE joint;
                joint.name = bone.name;
                joint.parentIndex = bone.parentIndex < 0 ? kInvalidJointParentUVE
                                                         : static_cast<std::uint32_t>(bone.parentIndex);
                // A bone no cluster binds keeps its current pose as its bind pose.
                const ufbx_matrix inverse = ufbx_matrix_invert(&bone.object->node_to_world);
                joint.inverseBindMatrix = ToMatrixUVE(inverse);
                jointOfObject.emplace(bone.object, static_cast<std::uint32_t>(candidate.joints.size()));
                candidate.joints.push_back(std::move(joint));
            }
            for (const ufbx_skin_deformer* const skin : scene->skin_deformers) {
                for (const ufbx_skin_cluster* const cluster : skin->clusters) {
                    if (cluster->bone_node == nullptr) {
                        continue;
                    }
                    if (const auto found = jointOfObject.find(cluster->bone_node); found != jointOfObject.end()) {
                        const ufbx_matrix inverse = ufbx_matrix_invert(&cluster->bind_to_world);
                        candidate.joints[found->second].inverseBindMatrix = ToMatrixUVE(inverse);
                    }
                }
            }
        }
        std::vector<std::uint32_t> triangle;
        bool hasBounds = false;

        for (const ufbx_mesh* const mesh : scene->meshes) {
            if (mesh == nullptr || !mesh->vertex_position.exists || mesh->num_triangles == 0U) {
                continue;
            }
            triangle.assign(mesh->max_face_triangles * 3U, 0U);
            const ufbx_skin_deformer* const skin =
                skinned && mesh->skin_deformers.count > 0U ? mesh->skin_deformers.data[0] : nullptr;
            for (const ufbx_node* const object : mesh->instances) {
                // A part with no skin of its own rides the nearest bone above it (or the first).
                std::uint32_t rigidJoint = 0U;
                for (const ufbx_node* ancestor = object; skinned && ancestor != nullptr; ancestor = ancestor->parent) {
                    if (const auto found = jointOfObject.find(ancestor); found != jointOfObject.end()) {
                        rigidJoint = found->second;
                        break;
                    }
                }
                const ufbx_matrix toWorld = object->geometry_to_world;
                const ufbx_matrix normalToWorld = ufbx_matrix_for_normals(&toWorld);
                // A mirrored instance (negative scale) turns its triangles inside out; swapping two
                // corners keeps them facing the way the surface does.
                const bool mirrored = MatrixDeterminantUVE(toWorld) < 0.0;
                // Which material each FACE draws with. ufbx splits a mesh into one part per
                // material, and a part's index is the material index; the face list of each part
                // is therefore the only place that association lives.
                std::vector<std::uint32_t> faceMaterial;
                if (outSurfaces != nullptr && mesh->material_parts.count > 0U) {
                    faceMaterial.assign(mesh->num_faces, kInvalidMeshSurfaceMaterialSlotUVE);
                    for (const ufbx_mesh_part& part : mesh->material_parts) {
                        for (std::size_t faceSlot = 0U; faceSlot < part.face_indices.count; ++faceSlot) {
                            const std::uint32_t faceIndex = part.face_indices.data[faceSlot];
                            if (faceIndex < faceMaterial.size()) {
                                faceMaterial[faceIndex] = part.index;
                            }
                        }
                    }
                }
                const auto faceMaterialSourceIndexUVE = [&](const std::uint32_t faceIndex) -> std::uint32_t {
                    if (faceMaterial.empty()) {
                        return kInvalidMeshSurfaceMaterialSlotUVE;
                    }
                    const std::uint32_t part = faceMaterial[faceIndex];
                    if (part == kInvalidMeshSurfaceMaterialSlotUVE) {
                        return kInvalidMeshSurfaceMaterialSlotUVE;
                    }
                    // Per-instance materials first - the same mesh can be drawn with a different
                    // set on each node - then the mesh's own list.
                    const ufbx_material* material = nullptr;
                    if (object->materials.count > part) {
                        material = object->materials.data[part];
                    } else if (mesh->materials.count > part) {
                        material = mesh->materials.data[part];
                    }
                    if (material == nullptr) {
                        return kInvalidMeshSurfaceMaterialSlotUVE;
                    }
                    const auto found = materialSourceIndex.find(material);
                    return found == materialSourceIndex.end() ? kInvalidMeshSurfaceMaterialSlotUVE : found->second;
                };
                for (std::size_t faceIndex = 0U; faceIndex < mesh->num_faces; ++faceIndex) {
                    const ufbx_face face = mesh->faces.data[faceIndex];
                    const std::size_t bucket = bucketForMaterialUVE(
                        faceMaterialSourceIndexUVE(static_cast<std::uint32_t>(faceIndex)));
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
                                : ufbx_vec3{{{0.0, 1.0, 0.0}}};
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
                        MeshSkinningInfluenceUVE influence;
                        if (skinned) {
                            influence.joints[0] = rigidJoint;
                            influence.weights[0] = 1.0F;
                        }
                        if (skin != nullptr) {
                            const std::uint32_t meshVertex = mesh->vertex_indices.data[index];
                            if (meshVertex < skin->vertices.count) {
                                // ufbx lists each vertex's weights strongest first.
                                const ufbx_skin_vertex& weights = skin->vertices.data[meshVertex];
                                MeshSkinningInfluenceUVE picked;
                                float total = 0.0F;
                                std::size_t slot = 0U;
                                for (std::uint32_t w = 0U; w < weights.num_weights && slot < kMaxJointInfluencesUVE; ++w) {
                                    const ufbx_skin_weight& weight = skin->weights.data[weights.weight_begin + w];
                                    const ufbx_skin_cluster* const cluster = skin->clusters.data[weight.cluster_index];
                                    const auto joint = cluster->bone_node != nullptr ? jointOfObject.find(cluster->bone_node)
                                                                                      : jointOfObject.end();
                                    const float value = static_cast<float>(weight.weight);
                                    if (joint == jointOfObject.end() || !std::isfinite(value) || value <= 0.0F) {
                                        continue;
                                    }
                                    picked.joints[slot] = joint->second;
                                    picked.weights[slot] = value;
                                    total += value;
                                    ++slot;
                                }
                                if (total > 0.0F) {
                                    for (float& value : picked.weights) {
                                        value /= total;
                                    }
                                    influence = picked;
                                    // In bind pose: where the skin puts it when every bone is at its bind.
                                    const ufbx_skin_cluster* const strongest =
                                        skin->clusters.data[skin->weights.data[weights.weight_begin].cluster_index];
                                    const ufbx_matrix bind = ufbx_matrix_mul(&strongest->bind_to_world,
                                                                             &strongest->geometry_to_bone);
                                    vertex.position = ToVectorUVE(
                                        ufbx_transform_position(&bind, ufbx_get_vertex_vec3(&mesh->vertex_position, index)));
                                }
                            }
                        }

                        const auto [found, inserted] = shared.try_emplace(
                            MakeCornerKeyUVE(vertex, influence), static_cast<std::uint32_t>(candidate.vertices.size()));
                        if (inserted) {
                            if (candidate.vertices.size() >= kMaximumFbxMeshVerticesUVE) {
                                UVE_ERROR("FbxMeshConverterUVE: more than {} vertices", kMaximumFbxMeshVerticesUVE);
                                return false;
                            }
                            candidate.vertices.push_back(vertex);
                            if (skinned) {
                                candidate.skinningInfluences.push_back(influence);
                            }
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
                        indexBuckets[bucket].indices.push_back(found->second);
                    }
                }
            }
        }

        // Concatenate the per-material runs, in the order their materials were first used. This is
        // the index buffer from here on; tangents and every later check see the same triangles,
        // just grouped.
        std::vector<FbxSurfaceRangeUVE> surfaceRanges;
        std::size_t indexTotal = 0U;
        for (const IndexBucketUVE& bucket : indexBuckets) {
            indexTotal += bucket.indices.size();
        }
        candidate.indices.reserve(indexTotal);
        for (const IndexBucketUVE& bucket : indexBuckets) {
            if (bucket.indices.empty()) {
                continue;
            }
            if (outSurfaces != nullptr) {
                surfaceRanges.push_back(FbxSurfaceRangeUVE{
                    static_cast<std::uint32_t>(candidate.indices.size()),
                    static_cast<std::uint32_t>(bucket.indices.size()), bucket.materialSourceIndex});
            }
            candidate.indices.insert(candidate.indices.end(), bucket.indices.begin(), bucket.indices.end());
        }
        if (outSurfaces != nullptr) {
            // One run is the whole buffer, which is what having no surface table already means.
            if (surfaceRanges.size() < 2U || surfaceRanges.size() > kMaximumMeshSurfacesUVE) {
                surfaceRanges.clear();
            }
            outSurfaces->swap(surfaceRanges);
        }

        if (candidate.vertices.empty() || candidate.indices.empty() || candidate.indices.size() % 3U != 0U) {
            UVE_ERROR("FbxMeshConverterUVE: the file holds no triangles");
            return false;
        }
        if (!TryGenerateMeshTangentsUVE(candidate.vertices, candidate.indices)) {
            return false;
        }
        if (skinned && !IsMeshSkinningDataValidUVE(candidate)) {
            UVE_ERROR("FbxMeshConverterUVE: the skin did not convert into valid skinning data");
            return false;
        }
        outMesh = std::move(candidate);
        return true;
    } catch (const std::bad_alloc&) {
        UVE_ERROR("FbxMeshConverterUVE: out of memory");
        return false;
    }
}

std::optional<FbxSourceSummaryUVE> DescribeFbxSourceUVE(const std::span<const std::byte> source) {
    try {
        const ScenePtrUVE scene = LoadSceneUVE(source, false);
        if (!scene) {
            return std::nullopt;
        }
        FbxSourceSummaryUVE summary;
        summary.meshCount = scene->meshes.count;
        summary.boneCount = scene->bones.count;
        summary.animationCount = scene->anim_stacks.count;
        for (const ufbx_anim_stack* const stack : scene->anim_stacks) {
            const double length = stack->time_end - stack->time_begin;
            if (std::isfinite(length) && length > summary.longestAnimationSeconds) {
                summary.longestAnimationSeconds = length;
            }
        }
        summary.hasSkin = scene->skin_deformers.count > 0U;
        return summary;
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
}


std::optional<GltfSkeletonUVE> ReadFbxSkeletonUVE(const std::span<const std::byte> source,
                                                  const std::size_t maximumJoints) {
    try {
        const ScenePtrUVE scene = LoadSceneUVE(source, false);
        if (!scene) {
            return std::nullopt;
        }
        const std::optional<std::vector<FbxBoneObjectUVE>> bones = CollectFbxBonesUVE(*scene, maximumJoints);
        if (!bones.has_value() || bones->empty()) {
            return std::nullopt;
        }
        GltfSkeletonUVE skeleton;
        skeleton.skinCount = scene->skin_deformers.count;
        for (const FbxBoneObjectUVE& bone : *bones) {
            // The pose relative to the nearest bone above it: everything between is folded in, so
            // the chain still meets up.
            ufbx_matrix local = bone.object->node_to_parent;
            for (const ufbx_node* const between : bone.between) {
                local = ufbx_matrix_mul(&between->node_to_parent, &local);
            }
            const ufbx_transform pose = ufbx_matrix_to_transform(&local);
            if (!IsFiniteTransformUVE(pose)) {
                return std::nullopt;
            }
            GltfJointUVE joint;
            joint.name = bone.name;
            joint.parentIndex = bone.parentIndex;
            joint.translation = ToVectorUVE(pose.translation);
            joint.rotation = Math::QuaternionUVE{static_cast<float>(pose.rotation.x), static_cast<float>(pose.rotation.y),
                                                 static_cast<float>(pose.rotation.z), static_cast<float>(pose.rotation.w)};
            joint.scale = ToVectorUVE(pose.scale);
            skeleton.joints.push_back(std::move(joint));
        }
        return skeleton;
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
}

std::vector<AnimationClipAssetUVE> ReadFbxAnimationsUVE(const std::span<const std::byte> source,
                                                        const std::size_t maximumBones) {
    std::vector<AnimationClipAssetUVE> clips;
    try {
        const ScenePtrUVE scene = LoadSceneUVE(source, false, true);
        if (!scene) {
            return clips;
        }
        const std::optional<std::vector<FbxBoneObjectUVE>> bones = CollectFbxBonesUVE(*scene, maximumBones);
        if (!bones.has_value() || bones->empty()) {
            return clips;
        }
        const double framesPerSecond =
            std::isfinite(scene->settings.frames_per_second) && scene->settings.frames_per_second > 0.0
                ? scene->settings.frames_per_second
                : 30.0;
        // The skeleton every take was made for, the same rest pose ReadFbxSkeletonUVE gives.
        std::vector<AnimationAssetRestBoneUVE> rest;
        for (const FbxBoneObjectUVE& bone : *bones) {
            ufbx_matrix local = bone.object->node_to_parent;
            for (const ufbx_node* const between : bone.between) {
                local = ufbx_matrix_mul(&between->node_to_parent, &local);
            }
            const ufbx_transform pose = ufbx_matrix_to_transform(&local);
            if (!IsFiniteTransformUVE(pose)) {
                return clips;
            }
            const AnimationAssetPoseUVE converted = ToAnimationPoseUVE(pose);
            rest.push_back(AnimationAssetRestBoneUVE{bone.name, bone.parentIndex, converted.position, converted.rotation,
                                                     converted.scale});
        }
        std::set<std::string> usedIds;
        for (const ufbx_anim_stack* const stack : scene->anim_stacks) {
            const double begin = stack->time_begin;
            const double rawDuration = stack->time_end - stack->time_begin;
            if (!std::isfinite(begin) || !std::isfinite(rawDuration) || rawDuration < 0.0) {
                continue;
            }
            const double duration = rawDuration > 1.0e-6 ? rawDuration : (1.0 / framesPerSecond);
            // One sample a frame, fewer when the take is longer than the per-track bound allows.
            const double wanted = std::ceil(duration * framesPerSecond) + 1.0;
            const std::size_t count = static_cast<std::size_t>(
                std::clamp(wanted, 2.0, static_cast<double>(kMaximumAnimationAssetSamplesUVE)));

            AnimationClipAssetUVE clip;
            std::string id(stack->name.data, stack->name.length);
            if (const std::size_t bar = id.find('|'); bar != std::string::npos) {
                id = id.substr(bar + 1U); // "Armature|Run" is the take "Run" of that armature
            }
            id.erase(std::remove(id.begin(), id.end(), '\0'), id.end());
            if (id.empty()) {
                id = "Take";
            }
            id.resize(std::min(id.size(), kMaximumAnimationAssetIdentifierBytesUVE - 4U));
            const std::string stem = id;
            for (int suffix = 1; usedIds.count(id) != 0U; ++suffix) {
                id = stem + "_" + std::to_string(suffix);
            }
            usedIds.insert(id);
            clip.clipId = id;
            clip.durationSeconds = duration;
            clip.rest = rest;
            clip.bones.reserve(bones->size());
            bool valid = true;
            for (const FbxBoneObjectUVE& bone : *bones) {
                AnimationAssetBoneTrackUVE track;
                track.bone = bone.name;
                track.samples.reserve(count);
                for (std::size_t frame = 0U; frame < count && valid; ++frame) {
                    const double sampleLocal = frame + 1U == count ? duration : static_cast<double>(frame) * (duration / static_cast<double>(count - 1U));
                    const double evalLocal = rawDuration > 1.0e-6 ? sampleLocal : 0.0;
                    const double time = begin + evalLocal;
                    ufbx_transform evaluated = ufbx_evaluate_transform(stack->anim, bone.object, time);
                    ufbx_matrix matrix = ufbx_transform_to_matrix(&evaluated);
                    for (const ufbx_node* const between : bone.between) {
                        ufbx_transform betweenPose = ufbx_evaluate_transform(stack->anim, between, time);
                        const ufbx_matrix betweenMatrix = ufbx_transform_to_matrix(&betweenPose);
                        matrix = ufbx_matrix_mul(&betweenMatrix, &matrix);
                    }
                    const ufbx_transform pose = ufbx_matrix_to_transform(&matrix);
                    valid = IsFiniteTransformUVE(pose);
                    track.samples.push_back(AnimationAssetSampleUVE{sampleLocal, ToAnimationPoseUVE(pose)});
                }
                // A bone that does not move keeps one sample: its pose for the whole take.
                const bool still = std::all_of(track.samples.begin(), track.samples.end(),
                                               [&track](const AnimationAssetSampleUVE& sample) {
                                                   return sample.pose == track.samples.front().pose;
                                               });
                if (still && !track.samples.empty()) {
                    track.samples.resize(1U);
                }
                clip.bones.push_back(std::move(track));
            }
            if (valid && IsAnimationClipAssetValidUVE(clip)) {
                clips.push_back(std::move(clip));
            } else {
                UVE_WARNING("FbxMeshConverterUVE: take \"{}\" produced a non-finite or oversized clip; skipped", stem);
            }
        }
        if (clips.empty() && !rest.empty()) {
            AnimationClipAssetUVE clip;
            clip.clipId = "TPose";
            clip.durationSeconds = 1.0 / framesPerSecond;
            clip.rest = rest;
            clip.bones.reserve(rest.size());
            for (const AnimationAssetRestBoneUVE& bone : rest) {
                AnimationAssetBoneTrackUVE track;
                track.bone = bone.bone;
                track.samples.push_back(
                    AnimationAssetSampleUVE{0.0, AnimationAssetPoseUVE{bone.position, bone.rotation, bone.scale}});
                clip.bones.push_back(std::move(track));
            }
            if (IsAnimationClipAssetValidUVE(clip)) {
                clips.push_back(std::move(clip));
            }
        }
    } catch (const std::bad_alloc&) {
        UVE_ERROR("FbxMeshConverterUVE: out of memory while reading animation");
        clips.clear();
    }
    return clips;
}

namespace {

/// The reading side of the material conversion, in the same file as the mesh/skeleton/animation
/// readers so it shares their loader options: no external file is opened, every value stays a pure
/// function of the bytes handed in, and an embedded image is copied out as the file stored it rather
/// than decoded here - decoding is the import pipeline's job, and a converter that stays pure is one
/// that can be tested by handing it bytes.
[[nodiscard]] FbxTextureSourceUVE ReadTextureSourceUVE(const ufbx_material_map& map) {
    FbxTextureSourceUVE source;
    const ufbx_texture* const texture = map.texture;
    if (texture == nullptr || !map.texture_enabled) {
        return source;
    }
    if (texture->type != UFBX_TEXTURE_FILE) {
        source.procedural = true; // layered/procedural/shader: nothing to extract
        return source;
    }
    const auto asString = [](const ufbx_string& value) {
        return value.length != 0U ? std::string(value.data, value.length) : std::string{};
    };
    source.file = texture->relative_filename.length != 0U ? asString(texture->relative_filename) : asString(texture->filename);
    if (source.file.empty()) {
        // The name to fall back on when the material gives nothing to build a stem from.
        source.embeddedName = asString(texture->filename);
    }
    if (const ufbx_blob& content = texture->content; content.data != nullptr && content.size > 0U) {
        if (content.size > kMaximumFbxEmbeddedTextureBytesUVE) {
            UVE_WARNING("FbxMeshConverterUVE: embedded texture \\\"{}\\\" is {} bytes, over the {} byte ceiling; it is "
                        "dropped and its slot stays empty",
                        source.file.empty() ? source.embeddedName : source.file, content.size,
                        kMaximumFbxEmbeddedTextureBytesUVE);
        } else {
            const auto* const begin = static_cast<const std::byte*>(content.data);
            source.embedded.assign(begin, begin + content.size);
        }
    }
    if (source.file.empty() && source.embedded.empty()) {
        // A file texture the file itself neither names nor carries: there is nothing to follow.
        source.procedural = true;
    }
    return source;
}

/// Reads a color map into MaterialAssetUVE's validated range: channels are clamped to [0,1] and
/// `outClamped` reports that this happened, so the change is visible to callers instead of silent.
[[nodiscard]] Math::ColorUVE ToAlbedoUVE(const double red, const double green, const double blue,
                                         bool& outClamped) {
    const auto unit = [&outClamped](const double channel) {
        if (!std::isfinite(channel)) {
            outClamped = true;
            return 0.0F;
        }
        const auto value = static_cast<float>(channel);
        if (value < 0.0F || value > 1.0F) {
            outClamped = true;
            return std::clamp(value, 0.0F, 1.0F);
        }
        return value;
    };
    return Math::ColorUVE{unit(red), unit(green), unit(blue)};
}

[[nodiscard]] Math::ColorUVE ToNonnegativeColorUVE(const double red, const double green,
                                                   const double blue) {
    const auto channel = [](const double value) {
        if (!std::isfinite(value) || value < 0.0) {
            return 0.0F;
        }
        return static_cast<float>(value);
    };
    return Math::ColorUVE{channel(red), channel(green), channel(blue)};
}

} // namespace

std::optional<std::vector<FbxMaterialUVE>> ReadFbxMaterialsUVE(const std::span<const std::byte> source) {
    // Geometry and animation are irrelevant to materials, and both are skipped so a large model
    // does not pay for data this reader never touches.
    const ScenePtrUVE scene = LoadSceneUVE(source, /*geometry=*/false, /*animation=*/false);
    if (!scene) {
        return std::nullopt;
    }
    try {
        std::vector<FbxMaterialUVE> materials;
        materials.reserve(scene->materials.count);
        for (std::size_t index = 0U; index < scene->materials.count; ++index) {
            const ufbx_material* const sourceMaterial = scene->materials.data[index];
            if (sourceMaterial == nullptr) {
                continue;
            }
            FbxMaterialUVE converted;
            converted.sourceIndex = static_cast<std::uint32_t>(index);
            converted.name = sourceMaterial->name.length != 0U
                                 ? std::string(sourceMaterial->name.data, sourceMaterial->name.length)
                                 : std::string{};

            // Base color: FBX's PBR base_color first, then the classic diffuse color. Both are
            // authored in the file's own space; nothing here reinterprets them as linear/sRGB.
            const ufbx_material_map& baseColor = sourceMaterial->pbr.base_color;
            const ufbx_material_map& diffuseColor = sourceMaterial->fbx.diffuse_color;
            if (baseColor.has_value) {
                converted.material.albedoColor = ToAlbedoUVE(baseColor.value_vec4.x, baseColor.value_vec4.y,
                                                             baseColor.value_vec4.z, converted.albedoClamped);
            } else if (diffuseColor.has_value) {
                converted.material.albedoColor = ToAlbedoUVE(diffuseColor.value_vec3.x, diffuseColor.value_vec3.y,
                                                             diffuseColor.value_vec3.z, converted.albedoClamped);
            }

            const ufbx_material_map& metalness = sourceMaterial->pbr.metalness;
            if (metalness.has_value && std::isfinite(metalness.value_real)) {
                const auto value = static_cast<float>(metalness.value_real);
                if (value < 0.0F || value > 1.0F) {
                    converted.albedoClamped = true; // same "a validated range was enforced" signal
                }
                converted.material.metallic = std::clamp(value, 0.0F, 1.0F);
            }
            const ufbx_material_map& roughness = sourceMaterial->pbr.roughness;
            if (roughness.has_value && std::isfinite(roughness.value_real)) {
                converted.material.roughness = std::clamp(static_cast<float>(roughness.value_real), 0.0F, 1.0F);
            }

            // Emission: the colour and its energy are separate authored values, and so are they in
            // MaterialAssetUVE (emissiveEnergy multiplies emissiveColor).
            const ufbx_material_map& emissionColor = sourceMaterial->pbr.emission_color;
            if (emissionColor.has_value) {
                converted.material.emissiveColor = ToNonnegativeColorUVE(
                    emissionColor.value_vec3.x, emissionColor.value_vec3.y, emissionColor.value_vec3.z);
            }
            const ufbx_material_map& emissionFactor = sourceMaterial->pbr.emission_factor;
            if (emissionFactor.has_value && std::isfinite(emissionFactor.value_real) &&
                emissionFactor.value_real >= 0.0) {
                converted.material.emissiveEnergy = static_cast<float>(emissionFactor.value_real);
                if (!emissionColor.has_value && emissionFactor.value_real > 0.0) {
                    converted.material.emissiveColor = Math::ColorUVE{1.0F, 1.0F, 1.0F};
                }
            }

            // Two honest authoring styles mean "this material blends": the classic FBX
            // TransparencyFactor (0 opaque, 1 fully transparent, verified against the parser) and
            // an explicit PBR opacity below one, which is what glTF/OSL-style materials carry.
            // MaterialAssetUVE records it as a flag (RenderQueueUVE buckets on it) and keeps
            // alphaCutoff at zero, because an FBX material holds no cutoff value.
            const ufbx_material_map& opacity = sourceMaterial->pbr.opacity;
            const ufbx_material_map& transparency = sourceMaterial->fbx.transparency_factor;
            const bool pbrBlends = opacity.has_value && std::isfinite(opacity.value_real) &&
                                   opacity.value_real >= 0.0 && opacity.value_real < 1.0;
            const bool classicBlends = transparency.has_value &&
                                       std::isfinite(transparency.value_real) &&
                                       transparency.value_real > 0.0;
            if (pbrBlends || classicBlends) {
                converted.material.isTransparent = true;
            }

            converted.albedo = ReadTextureSourceUVE(sourceMaterial->pbr.base_color);
            converted.normal = ReadTextureSourceUVE(sourceMaterial->pbr.normal_map);
            converted.emissive = ReadTextureSourceUVE(sourceMaterial->pbr.emission_color);
            converted.ambientOcclusion = ReadTextureSourceUVE(sourceMaterial->pbr.ambient_occlusion);

            if (!IsMaterialAssetValidUVE(converted.material)) {
                UVE_WARNING("FbxMeshConverterUVE: material \"{}\" produced an invalid PBR descriptor; skipped",
                            converted.name);
                continue;
            }
            materials.push_back(std::move(converted));
        }
        return materials;
    } catch (const std::bad_alloc&) {
        UVE_ERROR("FbxMeshConverterUVE: out of memory while reading materials");
        return std::nullopt;
    }
}

} // namespace UVE::Asset
