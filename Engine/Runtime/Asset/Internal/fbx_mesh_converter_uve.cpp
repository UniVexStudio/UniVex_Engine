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
[[nodiscard]] ufbx_load_opts MakeLoadOptionsUVE(const bool geometry, const bool animation = false) noexcept {
    ufbx_load_opts options{};
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0;
    options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    options.generate_missing_normals = true;
    options.load_external_files = false;
    options.ignore_embedded = true;
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

namespace {

/// One bone of the file, in the order the skeleton lists them: parents first.
struct FbxBoneNodeUVE final {
    const ufbx_node* node = nullptr;
    std::string name;
    std::int32_t parentIndex = -1;
    /// The nodes between this bone and its parent bone (or the root), nearest first: a group or
    /// null an exporter put in the chain. Their transforms are folded into the bone's local pose.
    std::vector<const ufbx_node*> between;
};

/// Every bone node, depth first from the root, with unique names - the one walk both the skeleton
/// and its clips use, so a clip's track names always match the skeleton's bones. Empty optional
/// when there are more than `maximumBones`.
[[nodiscard]] std::optional<std::vector<FbxBoneNodeUVE>> CollectFbxBonesUVE(const ufbx_scene& scene,
                                                                             const std::size_t maximumBones) {
    std::vector<FbxBoneNodeUVE> bones;
    std::unordered_map<const ufbx_node*, std::int32_t> boneOfNode;
    std::set<std::string> usedNames;
    std::vector<const ufbx_node*> pending{scene.root_node};
    while (!pending.empty()) {
        const ufbx_node* const node = pending.back();
        pending.pop_back();
        for (std::size_t child = node->children.count; child > 0U; --child) {
            pending.push_back(node->children.data[child - 1U]);
        }
        if (node->bone == nullptr) {
            continue;
        }
        if (bones.size() >= maximumBones) {
            UVE_ERROR("FbxMeshConverterUVE: more than {} bones", maximumBones);
            return std::nullopt;
        }
        FbxBoneNodeUVE bone;
        bone.node = node;
        for (const ufbx_node* ancestor = node->parent; ancestor != nullptr; ancestor = ancestor->parent) {
            if (const auto found = boneOfNode.find(ancestor); found != boneOfNode.end()) {
                bone.parentIndex = found->second;
                break;
            }
            bone.between.push_back(ancestor);
        }
        const std::string name(node->name.data, node->name.length);
        std::string unique = name.empty() ? std::string("Bone") : name;
        const std::string stem = unique;
        for (int suffix = 1; usedNames.count(unique) != 0U; ++suffix) {
            unique = stem + "_" + std::to_string(suffix);
        }
        usedNames.insert(unique);
        bone.name = std::move(unique);
        boneOfNode.emplace(node, static_cast<std::int32_t>(bones.size()));
        bones.push_back(std::move(bone));
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

std::optional<GltfSkeletonUVE> ReadFbxSkeletonUVE(const std::span<const std::byte> source,
                                                  const std::size_t maximumJoints) {
    try {
        const ScenePtrUVE scene = LoadSceneUVE(source, false);
        if (!scene) {
            return std::nullopt;
        }
        const std::optional<std::vector<FbxBoneNodeUVE>> bones = CollectFbxBonesUVE(*scene, maximumJoints);
        if (!bones.has_value() || bones->empty()) {
            return std::nullopt;
        }
        GltfSkeletonUVE skeleton;
        skeleton.skinCount = scene->skin_deformers.count;
        for (const FbxBoneNodeUVE& bone : *bones) {
            // The pose relative to the nearest bone above it: everything between is folded in, so
            // the chain still meets up.
            ufbx_matrix local = bone.node->node_to_parent;
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
        const std::optional<std::vector<FbxBoneNodeUVE>> bones = CollectFbxBonesUVE(*scene, maximumBones);
        if (!bones.has_value() || bones->empty()) {
            return clips;
        }
        const double framesPerSecond =
            std::isfinite(scene->settings.frames_per_second) && scene->settings.frames_per_second > 0.0
                ? scene->settings.frames_per_second
                : 30.0;
        std::set<std::string> usedIds;
        for (const ufbx_anim_stack* const stack : scene->anim_stacks) {
            const double begin = stack->time_begin;
            const double duration = stack->time_end - stack->time_begin;
            if (!std::isfinite(begin) || !std::isfinite(duration) || duration <= 0.0) {
                continue; // a take with no length has nothing to play
            }
            // One sample a frame, fewer when the take is longer than the per-track bound allows.
            const double wanted = std::ceil(duration * framesPerSecond) + 1.0;
            const std::size_t count = static_cast<std::size_t>(
                std::clamp(wanted, 2.0, static_cast<double>(kMaximumAnimationAssetSamplesUVE)));
            const double step = duration / static_cast<double>(count - 1U);

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
            clip.bones.reserve(bones->size());
            bool valid = true;
            for (const FbxBoneNodeUVE& bone : *bones) {
                AnimationAssetBoneTrackUVE track;
                track.bone = bone.name;
                track.samples.reserve(count);
                for (std::size_t frame = 0U; frame < count && valid; ++frame) {
                    const double local = frame + 1U == count ? duration : static_cast<double>(frame) * step;
                    const double time = begin + local;
                    ufbx_transform evaluated = ufbx_evaluate_transform(stack->anim, bone.node, time);
                    ufbx_matrix matrix = ufbx_transform_to_matrix(&evaluated);
                    for (const ufbx_node* const between : bone.between) {
                        ufbx_transform betweenPose = ufbx_evaluate_transform(stack->anim, between, time);
                        const ufbx_matrix betweenMatrix = ufbx_transform_to_matrix(&betweenPose);
                        matrix = ufbx_matrix_mul(&betweenMatrix, &matrix);
                    }
                    const ufbx_transform pose = ufbx_matrix_to_transform(&matrix);
                    valid = IsFiniteTransformUVE(pose);
                    track.samples.push_back(AnimationAssetSampleUVE{local, ToAnimationPoseUVE(pose)});
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
    } catch (const std::bad_alloc&) {
        UVE_ERROR("FbxMeshConverterUVE: out of memory while reading animation");
        clips.clear();
    }
    return clips;
}

} // namespace UVE::Asset
