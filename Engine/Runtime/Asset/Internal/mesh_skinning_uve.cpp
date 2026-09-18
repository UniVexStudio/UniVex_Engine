// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/mesh_skinning_uve.h"

#include <cmath>

#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset {
namespace {

[[nodiscard]] bool IsMatrixFiniteUVE(const Math::Matrix4x4UVE& matrix) noexcept {
    for (const auto& row : matrix.m) {
        for (const float value : row) {
            if (!std::isfinite(value)) {
                return false;
            }
        }
    }
    return true;
}

/// Transforms `point` by `matrix` as an affine point, accumulating in FLOAT.
///
/// Deliberately not Math::TransformPointUVE, which accumulates in double. That is the right choice
/// for a general-purpose transform, but it is the wrong one here, for two reasons.
///
/// First, consistency inside a single vertex: a skinned normal has no double-precision equivalent
/// (TransformDirectionUVE below is float, as any direction transform naturally is), so using a
/// double path for the position and a float path for the normal would apply two different
/// arithmetics to the same vertex under the same matrix.
///
/// Second, and the reason this matters beyond tidiness: a GPU skinning kernel has no float64 - it
/// is an optional Vulkan feature absent from whole classes of hardware. Keeping the CPU on double
/// would put a permanent ~1 ULP disagreement between the two paths on roughly one vertex in six
/// (measured, not estimated), which would make an exact CPU/GPU comparison impossible and force
/// every skinning test onto a tolerance. A tolerance is exactly what stops a test noticing a real
/// defect, because a genuinely wrong weight also looks like a small error. The precision given up
/// is on the order of 1e-7 relative on model-space positions, which is nothing; the ability to
/// assert bit-for-bit agreement forever is worth considerably more.
[[nodiscard]] Math::Vector3UVE TransformPointFloatUVE(const Math::Matrix4x4UVE& matrix,
                                                      const Math::Vector3UVE& point) noexcept {
    return Math::Vector3UVE{matrix.m[0][0] * point.x + matrix.m[0][1] * point.y +
                                matrix.m[0][2] * point.z + matrix.m[0][3],
                            matrix.m[1][0] * point.x + matrix.m[1][1] * point.y +
                                matrix.m[1][2] * point.z + matrix.m[1][3],
                            matrix.m[2][0] * point.x + matrix.m[2][1] * point.y +
                                matrix.m[2][2] * point.z + matrix.m[2][3]};
}

/// Transforms `direction` by `matrix`'s upper-left 3x3, ignoring translation.
///
/// The difference from the point transform above IS the point: a normal is a direction, and
/// running it through the point transform would displace it by the joint's translation, which on
/// a joint far from the origin turns a unit normal into a vector pointing roughly at that joint.
[[nodiscard]] Math::Vector3UVE TransformDirectionUVE(const Math::Matrix4x4UVE& matrix,
                                                     const Math::Vector3UVE& direction) noexcept {
    return Math::Vector3UVE{
        matrix.m[0][0] * direction.x + matrix.m[0][1] * direction.y + matrix.m[0][2] * direction.z,
        matrix.m[1][0] * direction.x + matrix.m[1][1] * direction.y + matrix.m[1][2] * direction.z,
        matrix.m[2][0] * direction.x + matrix.m[2][1] * direction.y + matrix.m[2][2] * direction.z};
}

/// The weighted sum of the influencing joints' matrices.
///
/// Blending the MATRICES and then transforming once - rather than transforming by each joint and
/// blending the results - is both cheaper and what every GPU skinning path does. For linear blend
/// skinning the two are algebraically identical, since the transform is linear in the matrix; they
/// are not bit-identical in floating point, so the choice has to be the same on both sides of a
/// CPU/GPU comparison. This is that choice, stated once.
[[nodiscard]] Math::Matrix4x4UVE BlendSkinningMatricesUVE(
    const MeshSkinningInfluenceUVE& influence,
    const std::span<const Math::Matrix4x4UVE> skinningMatrices) noexcept {
    Math::Matrix4x4UVE blended;
    for (auto& row : blended.m) {
        for (float& value : row) {
            value = 0.0F;
        }
    }
    for (std::size_t slot = 0U; slot < kMaxJointInfluencesUVE; ++slot) {
        const float weight = influence.weights[slot];
        if (weight == 0.0F) {
            // Skipped rather than multiplied by zero, so a joint with zero influence cannot
            // contribute a NaN through 0 * infinity.
            continue;
        }
        const Math::Matrix4x4UVE& jointMatrix = skinningMatrices[influence.joints[slot]];
        for (std::size_t row = 0U; row < 4U; ++row) {
            for (std::size_t column = 0U; column < 4U; ++column) {
                blended.m[row][column] += jointMatrix.m[row][column] * weight;
            }
        }
    }
    return blended;
}

} // namespace

bool TryResolvePoseUVE(const std::span<const MeshJointUVE> joints,
                       const std::span<const Math::Matrix4x4UVE> localPose,
                       std::vector<Math::Matrix4x4UVE>& outSkinningMatrices) {
    if (joints.size() != localPose.size()) {
        UVE_ERROR("MeshSkinningUVE: a pose of {} transforms cannot drive a skeleton of {} joints",
                  localPose.size(), joints.size());
        return false;
    }
    if (!IsSkeletonTopologicallyOrderedUVE(joints)) {
        UVE_ERROR("MeshSkinningUVE: the skeleton is not topologically ordered, so a single forward "
                  "resolution pass would read a parent that has not been resolved yet");
        return false;
    }

    // Built locally and moved out at the end: a failure part-way through must not leave the caller
    // holding a half-resolved pose, which would look like valid data.
    std::vector<Math::Matrix4x4UVE> modelSpace;
    modelSpace.reserve(joints.size());
    std::vector<Math::Matrix4x4UVE> resolved;
    resolved.reserve(joints.size());

    for (std::size_t index = 0U; index < joints.size(); ++index) {
        const std::uint32_t parent = joints[index].parentIndex;
        // The ordering check above guarantees parent < index, so this read is always of an
        // already-resolved entry - that is the entire reason the ordering is a precondition.
        const Math::Matrix4x4UVE modelTransform =
            (parent == kInvalidJointParentUVE) ? localPose[index]
                                               : modelSpace[parent] * localPose[index];
        if (!IsMatrixFiniteUVE(modelTransform)) {
            UVE_ERROR("MeshSkinningUVE: joint {} resolved to a non-finite model transform", index);
            return false;
        }
        modelSpace.push_back(modelTransform);

        const Math::Matrix4x4UVE skinning = modelTransform * joints[index].inverseBindMatrix;
        if (!IsMatrixFiniteUVE(skinning)) {
            UVE_ERROR("MeshSkinningUVE: joint {} produced a non-finite skinning matrix", index);
            return false;
        }
        resolved.push_back(skinning);
    }

    outSkinningMatrices = std::move(resolved);
    return true;
}

bool TrySkinMeshUVE(const MeshAssetUVE& mesh,
                    const std::span<const Math::Matrix4x4UVE> skinningMatrices,
                    std::vector<MeshVertexUVE>& outVertices) {
    if (!mesh.IsSkinnedUVE()) {
        UVE_ERROR("MeshSkinningUVE: TrySkinMeshUVE was given a static mesh");
        return false;
    }
    if (!IsMeshSkinningDataValidUVE(mesh)) {
        return false; // Already logged the specific defect.
    }
    if (skinningMatrices.size() != mesh.joints.size()) {
        UVE_ERROR("MeshSkinningUVE: {} skinning matrices for {} joints", skinningMatrices.size(),
                  mesh.joints.size());
        return false;
    }

    std::vector<MeshVertexUVE> skinned;
    skinned.reserve(mesh.vertices.size());
    for (std::size_t index = 0U; index < mesh.vertices.size(); ++index) {
        const Math::Matrix4x4UVE blended =
            BlendSkinningMatricesUVE(mesh.skinningInfluences[index], skinningMatrices);

        MeshVertexUVE vertex = mesh.vertices[index];
        vertex.position = TransformPointFloatUVE(blended, vertex.position);
        // Directions, not points - see the helper's comment.
        vertex.normal = TransformDirectionUVE(blended, vertex.normal);
        vertex.tangent = TransformDirectionUVE(blended, vertex.tangent);
        // Handedness is a sign, not a direction; it survives the transform untouched. A joint with
        // a mirroring (negative determinant) transform would arguably flip it, but mirrored joints
        // are not something this engine's skeletons produce, and inventing a determinant test here
        // would add a branch a GPU path would have to reproduce exactly.

        if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) ||
            !std::isfinite(vertex.position.z) || !std::isfinite(vertex.normal.x) ||
            !std::isfinite(vertex.normal.y) || !std::isfinite(vertex.normal.z)) {
            UVE_ERROR("MeshSkinningUVE: vertex {} skinned to a non-finite position or normal",
                      index);
            return false;
        }
        skinned.push_back(vertex);
    }

    outVertices = std::move(skinned);
    return true;
}

} // namespace UVE::Asset
