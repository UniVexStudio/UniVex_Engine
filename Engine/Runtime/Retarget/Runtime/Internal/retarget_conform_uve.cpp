// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/retarget/retarget_conform_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>

#include "retarget_keys_uve.h"
#include "retarget_pose_uve.h"

namespace UVE::Retarget {
namespace {

void SetErrorUVE(std::string* error, std::string text) {
    if (error != nullptr) {
        *error = std::move(text);
    }
}

[[nodiscard]] Math::QuaternionUVE InverseUVE(const Math::QuaternionUVE& value) noexcept {
    Math::QuaternionUVE out{};
    return Math::TryInverseUVE(value, out) ? out : Math::QuaternionUVE{};
}

[[nodiscard]] Math::QuaternionUVE NormalizedUVE(const Math::QuaternionUVE& value) noexcept {
    Math::QuaternionUVE out{};
    return Math::TryNormalizeUVE(value, out) ? out : Math::QuaternionUVE{};
}

/// Bones that turn to the A-pose on a conformed rig: the limbs and fingers. Hips, spine, neck,
/// head, clavicles, metacarpals, the thumb's base and the feet keep the rig's own shape.
[[nodiscard]] bool TurnsToThePoseUVE(const BoneKeyPartsUVE& key) {
    if (key.Is("upperarm") || key.Is("forearm") || key.Is("hand") || key.Is("thigh") || key.Is("shin")) {
        return true;
    }
    return key.IsFinger() && key.Number() > 0 && !(key.words[0] == "thumb" && key.Number() == 1);
}

/// The rotation (column vectors) of a matrix whose 3x3 part is a rotation times a uniform scale.
[[nodiscard]] bool DecomposeUVE(const Math::Matrix4x4UVE& matrix, WorldTransformUVE& out) {
    float r[3][3];
    float scales[3];
    for (int column = 0; column < 3; ++column) {
        scales[column] = std::sqrt(matrix.m[0][column] * matrix.m[0][column] + matrix.m[1][column] * matrix.m[1][column] +
                                   matrix.m[2][column] * matrix.m[2][column]);
        if (!(scales[column] > 1e-8F) || !std::isfinite(scales[column])) {
            return false;
        }
        for (int row = 0; row < 3; ++row) {
            r[row][column] = matrix.m[row][column] / scales[column];
        }
    }
    const float scale = scales[0];
    if (std::abs(scales[1] - scale) > 1e-3F * scale || std::abs(scales[2] - scale) > 1e-3F * scale) {
        return false; // not uniform
    }
    const float determinant = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) -
                              r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                              r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (determinant < 0.99F || determinant > 1.01F) {
        return false; // mirrored or sheared
    }
    Math::QuaternionUVE q{};
    const float trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0.0F) {
        const float s = std::sqrt(trace + 1.0F) * 2.0F;
        q = Math::QuaternionUVE{(r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, 0.25F * s};
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const float s = std::sqrt(1.0F + r[0][0] - r[1][1] - r[2][2]) * 2.0F;
        q = Math::QuaternionUVE{0.25F * s, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s, (r[2][1] - r[1][2]) / s};
    } else if (r[1][1] > r[2][2]) {
        const float s = std::sqrt(1.0F + r[1][1] - r[0][0] - r[2][2]) * 2.0F;
        q = Math::QuaternionUVE{(r[0][1] + r[1][0]) / s, 0.25F * s, (r[1][2] + r[2][1]) / s, (r[0][2] - r[2][0]) / s};
    } else {
        const float s = std::sqrt(1.0F + r[2][2] - r[0][0] - r[1][1]) * 2.0F;
        q = Math::QuaternionUVE{(r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, 0.25F * s, (r[1][0] - r[0][1]) / s};
    }
    out.position = Math::Vector3UVE{matrix.m[0][3], matrix.m[1][3], matrix.m[2][3]};
    out.rotation = NormalizedUVE(q);
    out.scale = scale;
    return true;
}

[[nodiscard]] Asset::AnimationAssetPoseUVE SampleTrackUVE(const Asset::AnimationAssetBoneTrackUVE& track, const double time) {
    const std::vector<Asset::AnimationAssetSampleUVE>& samples = track.samples;
    if (samples.size() == 1U || time <= samples.front().timeSeconds) {
        return samples.front().pose;
    }
    if (time >= samples.back().timeSeconds) {
        return samples.back().pose;
    }
    const auto after = std::ranges::upper_bound(samples, time, {}, &Asset::AnimationAssetSampleUVE::timeSeconds);
    const Asset::AnimationAssetSampleUVE& b = *after;
    const Asset::AnimationAssetSampleUVE& a = *(after - 1);
    const double span = b.timeSeconds - a.timeSeconds;
    const float t = span > 0.0 ? static_cast<float>((time - a.timeSeconds) / span) : 0.0F;
    Asset::AnimationAssetPoseUVE pose;
    pose.position = a.pose.position + (b.pose.position - a.pose.position) * t;
    pose.scale = a.pose.scale + (b.pose.scale - a.pose.scale) * t;
    if (!Math::TrySlerpUVE(a.pose.rotation, b.pose.rotation, t, pose.rotation)) {
        pose.rotation = a.pose.rotation;
    }
    return pose;
}

[[nodiscard]] bool SamePoseUVE(const Asset::AnimationAssetPoseUVE& a, const Asset::AnimationAssetPoseUVE& b) {
    constexpr float kTolerance = 1e-6F;
    const auto near = [](const float x, const float y) { return std::abs(x - y) <= kTolerance; };
    return near(a.position.x, b.position.x) && near(a.position.y, b.position.y) && near(a.position.z, b.position.z) &&
           near(a.rotation.x, b.rotation.x) && near(a.rotation.y, b.rotation.y) && near(a.rotation.z, b.rotation.z) &&
           near(a.rotation.w, b.rotation.w);
}

} // namespace

std::optional<ConformedRigUVE> ConformSkeletonUVE(const RetargetSkeletonUVE& rig, const HumanoidMatchUVE& match,
                                                  const HumanoidReferenceUVE& reference, std::string* error) {
    const std::size_t referenceCount = reference.skeleton.bones.size();
    if (!IsRetargetSkeletonValidUVE(rig) || match.referenceOfBone.size() != rig.bones.size() ||
        match.joints.size() != referenceCount || reference.info.size() != referenceCount) {
        SetErrorUVE(error, "the rig is malformed or was not matched against this reference");
        return std::nullopt;
    }
    std::int32_t hips = -1;
    for (std::size_t index = 0U; index < referenceCount; ++index) {
        if (reference.info[index].key == "C|hips") {
            hips = static_cast<std::int32_t>(index);
        }
    }
    if (hips < 0 || match.joints[static_cast<std::size_t>(hips)].bone < 0) {
        SetErrorUVE(error, "the rig has no hips");
        return std::nullopt;
    }
    const RetargetSkeletonUVE folded = FoldScaleUVE(rig);
    const std::vector<WorldTransformUVE> rigWorld = ComputeWorldTransformsUVE(folded);
    const std::vector<WorldTransformUVE> referenceWorld = ComputeWorldTransformsUVE(reference.skeleton);

    // ---- Layout: the humanoid's bones, then the rig's own ------------------------------------
    ConformedRigUVE out;
    out.heightScale = match.heightScale > 0.0F ? match.heightScale : 1.0F;
    RetargetSkeletonUVE& layout = out.skeleton;
    layout.bones = reference.skeleton.bones;
    std::vector<WorldTransformUVE> world(referenceCount);
    out.added.assign(referenceCount, true);
    out.referenceOfBone.resize(referenceCount);
    for (std::size_t index = 0U; index < referenceCount; ++index) {
        out.referenceOfBone[index] = static_cast<std::int32_t>(index);
    }
    out.boneOfRigBone.assign(rig.bones.size(), -1);
    for (std::size_t bone = 0U; bone < rig.bones.size(); ++bone) {
        if (const std::int32_t ref = match.referenceOfBone[bone]; ref >= 0) {
            out.boneOfRigBone[bone] = ref;
            world[static_cast<std::size_t>(ref)] = rigWorld[bone];
            out.added[static_cast<std::size_t>(ref)] = false;
        }
    }
    std::unordered_set<std::string> names;
    for (const RetargetBoneUVE& bone : layout.bones) {
        names.insert(bone.name);
    }
    for (std::size_t bone = 0U; bone < rig.bones.size(); ++bone) {
        if (out.boneOfRigBone[bone] >= 0) {
            continue;
        }
        RetargetBoneUVE kept = folded.bones[bone];
        std::string name = kept.name;
        for (int suffix = 1; names.contains(name); ++suffix) {
            name = kept.name + "_" + std::to_string(suffix);
        }
        names.insert(name);
        kept.name = name;
        kept.parent = kept.parent < 0 ? -1 : out.boneOfRigBone[static_cast<std::size_t>(kept.parent)];
        out.boneOfRigBone[bone] = static_cast<std::int32_t>(layout.bones.size());
        layout.bones.push_back(std::move(kept));
        world.push_back(rigWorld[bone]);
        out.referenceOfBone.push_back(-1);
        out.added.push_back(false);
    }
    const std::size_t count = layout.bones.size();

    // What each bone is, and what it points at, among the bones the rig has.
    std::vector<BoneKeyPartsUVE> keys(count);
    std::vector<std::int32_t> aims(count, -1);
    std::unordered_map<std::string, std::int32_t> byKey;
    for (std::size_t index = 0U; index < referenceCount; ++index) {
        if (out.added[index]) {
            continue;
        }
        keys[index] = SplitBoneKeyUVE(reference.info[index].key);
        byKey.emplace(reference.info[index].key, static_cast<std::int32_t>(index));
        std::int32_t aim = reference.info[index].aim;
        while (aim >= 0 && out.added[static_cast<std::size_t>(aim)]) {
            aim = reference.info[static_cast<std::size_t>(aim)].aim;
        }
        if (aim >= 0 && IsBelowUVE(layout, aim, static_cast<std::int32_t>(index))) {
            aims[index] = aim;
        }
    }

    // ---- The A-pose ---------------------------------------------------------------------------
    bool hadFeet = false;
    const float groundBefore = LowestFootUVE(keys, world, hadFeet);
    std::vector<Math::QuaternionUVE> rotationBefore(count);
    for (std::size_t index = 0U; index < count; ++index) {
        rotationBefore[index] = world[index].rotation;
    }
    const std::vector<std::optional<Math::Vector3UVE>> elbowAxes = ReadElbowAxesUVE(keys, aims, world);
    for (std::size_t index = 0U; index < referenceCount; ++index) {
        const std::int32_t aim = aims[index];
        if (aim < 0 || !TurnsToThePoseUVE(keys[index])) {
            continue;
        }
        const auto target = static_cast<std::size_t>(aim);
        const Math::Vector3UVE want = referenceWorld[target].position - referenceWorld[index].position;
        const Math::Vector3UVE now = world[target].position - world[index].position;
        RotateSubtreeUVE(layout, world, index, RotationBetweenUVE(now, want));
    }
    RollArmsUVE(layout, world, keys, aims, elbowAxes, byKey);
    for (std::size_t index = 0U; index < referenceCount; ++index) {
        if (keys[index].Is("foot") && keys[index].side != 'C') {
            RestoreRotationUVE(layout, world, index, rotationBefore[index]);
        }
    }
    bool hasFeet = false;
    const float groundAfter = LowestFootUVE(keys, world, hasFeet);
    if (hadFeet && hasFeet) {
        LiftUVE(layout, world, hips, groundBefore - groundAfter);
    }
    out.moveOfRigBone.resize(rig.bones.size());
    for (std::size_t bone = 0U; bone < rig.bones.size(); ++bone) {
        const WorldTransformUVE& posed = world[static_cast<std::size_t>(out.boneOfRigBone[bone])];
        RigidMoveUVE& move = out.moveOfRigBone[bone];
        move.rotation = NormalizedUVE(Math::MultiplyUVE(posed.rotation, InverseUVE(rigWorld[bone].rotation)));
        move.translation = posed.position - Math::RotateVectorUVE(move.rotation, rigWorld[bone].position);
    }

    // ---- Bones the rig lacks, fitted to its own limbs -------------------------------------------
    std::vector<bool> placed(count, true);
    for (std::size_t index = 0U; index < referenceCount; ++index) {
        placed[index] = !out.added[index];
    }
    const auto firstPlacedOnAim = [&](std::int32_t bone) {
        for (std::int32_t aim = reference.info[static_cast<std::size_t>(bone)].aim; aim >= 0;
             aim = reference.info[static_cast<std::size_t>(aim)].aim) {
            if (placed[static_cast<std::size_t>(aim)]) {
                return aim;
            }
        }
        return -1;
    };
    for (std::size_t index = 0U; index < referenceCount; ++index) {
        if (placed[index]) {
            continue;
        }
        const Math::Vector3UVE& at = referenceWorld[index].position;
        Math::Vector3UVE position{};
        const std::int32_t follows = reference.info[index].follows;
        std::int32_t above = reference.skeleton.bones[index].parent;
        while (above >= 0 && !placed[static_cast<std::size_t>(above)]) {
            above = reference.skeleton.bones[static_cast<std::size_t>(above)].parent;
        }
        if (follows >= 0 && placed[static_cast<std::size_t>(follows)]) {
            position = world[static_cast<std::size_t>(follows)].position; // an IK target on what it follows
        } else if (above < 0) {
            // Above everything the rig has (the root): as far from the hips as the humanoid's, scaled.
            const auto hipsIndex = static_cast<std::size_t>(hips);
            position = world[hipsIndex].position + (at - referenceWorld[hipsIndex].position) * out.heightScale;
        } else {
            // Placed against a segment of the rig's own: from the bone above to the next one down
            // this bone's chain (a spine bone between its neighbours, a metacarpal between hand and
            // knuckle), or along the bone above (a twist along the forearm). The humanoid's offset is
            // turned and scaled the way that segment differs from the humanoid's.
            const auto base = static_cast<std::size_t>(above);
            std::int32_t down = firstPlacedOnAim(static_cast<std::int32_t>(index));
            if (down < 0) {
                down = firstPlacedOnAim(above);
            }
            const Math::Vector3UVE offset = at - referenceWorld[base].position;
            position = world[base].position + offset * out.heightScale;
            if (down >= 0) {
                const Math::Vector3UVE referenceSegment = referenceWorld[static_cast<std::size_t>(down)].position - referenceWorld[base].position;
                const Math::Vector3UVE rigSegment = world[static_cast<std::size_t>(down)].position - world[base].position;
                const float referenceLength = Math::LengthUVE(referenceSegment);
                const float rigLength = Math::LengthUVE(rigSegment);
                if (referenceLength > 1e-6F && rigLength > 1e-6F) {
                    position = world[base].position + Math::RotateVectorUVE(RotationBetweenUVE(referenceSegment, rigSegment), offset) *
                                                          (rigLength / referenceLength);
                }
            }
        }
        world[index].position = position;
        placed[index] = true;
    }

    // ---- The humanoid's frames ---------------------------------------------------------------------
    for (std::size_t index = 0U; index < referenceCount; ++index) {
        world[index].rotation = referenceWorld[index].rotation;
        world[index].scale = 1.0F;
    }
    layout = SkeletonFromWorldUVE(layout, world);
    return out;
}

std::optional<RetargetSkeletonUVE> RigFromMeshUVE(const Asset::MeshAssetUVE& mesh, std::string* error) {
    if (mesh.joints.empty()) {
        SetErrorUVE(error, "the mesh has no skeleton");
        return std::nullopt;
    }
    RetargetSkeletonUVE rig;
    std::vector<WorldTransformUVE> world;
    for (std::size_t index = 0U; index < mesh.joints.size(); ++index) {
        const Asset::MeshJointUVE& joint = mesh.joints[index];
        if (joint.name.empty()) {
            SetErrorUVE(error, "joint " + std::to_string(index) + " has no name (import the model again)");
            return std::nullopt;
        }
        Math::Matrix4x4UVE bind{};
        WorldTransformUVE transform;
        if (!Math::TryInverseUVE(joint.inverseBindMatrix, bind) || !DecomposeUVE(bind, transform)) {
            SetErrorUVE(error, joint.name + "'s bind is not a rotation, translation and uniform scale");
            return std::nullopt;
        }
        transform.scale = 1.0F;
        RetargetBoneUVE bone;
        bone.name = joint.name;
        bone.parent = joint.parentIndex == Asset::kInvalidJointParentUVE ? -1 : static_cast<std::int32_t>(joint.parentIndex);
        rig.bones.push_back(std::move(bone));
        world.push_back(transform);
    }
    rig = SkeletonFromWorldUVE(rig, world);
    if (!IsRetargetSkeletonValidUVE(rig)) {
        SetErrorUVE(error, "the mesh's joints do not form a skeleton (unique names, parents first)");
        return std::nullopt;
    }
    return rig;
}

bool ConformMeshUVE(Asset::MeshAssetUVE& mesh, const ConformedRigUVE& conformed, std::string* error) {
    if (!mesh.IsSkinnedUVE() || mesh.joints.size() != conformed.boneOfRigBone.size() ||
        mesh.joints.size() != conformed.moveOfRigBone.size() || mesh.skinningInfluences.size() != mesh.vertices.size()) {
        SetErrorUVE(error, "the mesh and the conformed rig do not belong together");
        return false;
    }
    Asset::MeshAssetUVE result = mesh;
    for (std::size_t vertexIndex = 0U; vertexIndex < result.vertices.size(); ++vertexIndex) {
        Asset::MeshVertexUVE& vertex = result.vertices[vertexIndex];
        Asset::MeshSkinningInfluenceUVE& influence = result.skinningInfluences[vertexIndex];
        Math::Vector3UVE position{};
        Math::Vector3UVE normal{};
        Math::Vector3UVE tangent{};
        float total = 0.0F;
        for (std::size_t slot = 0U; slot < Asset::kMaxJointInfluencesUVE; ++slot) {
            const std::uint32_t joint = influence.joints[slot];
            if (joint >= conformed.moveOfRigBone.size()) {
                SetErrorUVE(error, "a vertex names a joint the mesh does not have");
                return false;
            }
            const float weight = influence.weights[slot];
            influence.joints[slot] = static_cast<std::uint32_t>(conformed.boneOfRigBone[joint]);
            if (weight <= 0.0F) {
                continue;
            }
            const RigidMoveUVE& move = conformed.moveOfRigBone[joint];
            position += move.ApplyUVE(vertex.position) * weight;
            normal += Math::RotateVectorUVE(move.rotation, vertex.normal) * weight;
            tangent += Math::RotateVectorUVE(move.rotation, vertex.tangent) * weight;
            total += weight;
        }
        if (total <= 0.0F) {
            continue;
        }
        vertex.position = position * (1.0F / total);
        if (Math::LengthSquaredUVE(normal) > 1e-12F) {
            vertex.normal = Math::NormalizeUVE(normal);
        }
        if (Math::LengthSquaredUVE(tangent) > 1e-12F) {
            vertex.tangent = Math::NormalizeUVE(tangent);
        }
    }
    const std::vector<WorldTransformUVE> world = ComputeWorldTransformsUVE(conformed.skeleton);
    result.joints.clear();
    for (std::size_t index = 0U; index < conformed.skeleton.bones.size(); ++index) {
        const RetargetBoneUVE& bone = conformed.skeleton.bones[index];
        Asset::MeshJointUVE joint;
        joint.name = bone.name;
        joint.parentIndex = bone.parent < 0 ? Asset::kInvalidJointParentUVE : static_cast<std::uint32_t>(bone.parent);
        const Math::QuaternionUVE inverse = InverseUVE(world[index].rotation);
        joint.inverseBindMatrix = Math::Matrix4x4UVE::ComposeTrsUVE(-Math::RotateVectorUVE(inverse, world[index].position), inverse,
                                                                    Math::Vector3UVE{1.0F, 1.0F, 1.0F});
        result.joints.push_back(std::move(joint));
    }
    if (!result.vertices.empty()) {
        Math::Vector3UVE low = result.vertices.front().position;
        Math::Vector3UVE high = low;
        for (const Asset::MeshVertexUVE& vertex : result.vertices) {
            low = Math::Vector3UVE{std::min(low.x, vertex.position.x), std::min(low.y, vertex.position.y),
                                   std::min(low.z, vertex.position.z)};
            high = Math::Vector3UVE{std::max(high.x, vertex.position.x), std::max(high.y, vertex.position.y),
                                    std::max(high.z, vertex.position.z)};
        }
        result.localBounds = Math::AabbUVE{low, high};
    }
    mesh = std::move(result);
    return true;
}

std::optional<Asset::AnimationClipAssetUVE> ConformClipUVE(const Asset::AnimationClipAssetUVE& clip, const RetargetSkeletonUVE& rig,
                                                          const ConformedRigUVE& conformed, const HumanoidReferenceUVE& reference,
                                                          std::string* error) {
    if (rig.bones.size() != conformed.boneOfRigBone.size() || rig.bones.size() != conformed.moveOfRigBone.size() ||
        !IsRetargetSkeletonValidUVE(rig)) {
        SetErrorUVE(error, "the clip's rig does not belong to the conformed rig");
        return std::nullopt;
    }
    if (!clip.IsSkeletalUVE()) {
        SetErrorUVE(error, "the clip moves no bones");
        return std::nullopt;
    }
    // Frames: the densest track's own times (an imported take samples every bone on the same frames).
    const auto densest = std::ranges::max_element(clip.bones, {}, [](const Asset::AnimationAssetBoneTrackUVE& track) {
        return track.samples.size();
    });
    std::vector<double> times;
    for (const Asset::AnimationAssetSampleUVE& sample : densest->samples) {
        times.push_back(sample.timeSeconds);
    }
    std::vector<const Asset::AnimationAssetBoneTrackUVE*> trackOfRigBone(rig.bones.size(), nullptr);
    for (const Asset::AnimationAssetBoneTrackUVE& track : clip.bones) {
        if (track.samples.empty()) {
            continue;
        }
        if (const std::int32_t bone = FindBoneUVE(rig, track.bone); bone >= 0) {
            trackOfRigBone[static_cast<std::size_t>(bone)] = &track;
        }
    }

    const RetargetSkeletonUVE& skeleton = conformed.skeleton;
    const std::size_t count = skeleton.bones.size();
    const std::vector<WorldTransformUVE> rigRest = ComputeWorldTransformsUVE(rig);
    const std::vector<WorldTransformUVE> conformedRest = ComputeWorldTransformsUVE(skeleton);
    // A rig bone's frame in the clip, turned into the conformed bone's: the clip's geometry stays
    // where it was, spoken of in the new frame.
    std::vector<std::int32_t> rigOfBone(count, -1);
    std::vector<Math::QuaternionUVE> toNewFrame(count);
    for (std::size_t bone = 0U; bone < rig.bones.size(); ++bone) {
        const auto index = static_cast<std::size_t>(conformed.boneOfRigBone[bone]);
        rigOfBone[index] = static_cast<std::int32_t>(bone);
        toNewFrame[index] = NormalizedUVE(Math::MultiplyUVE(
            Math::MultiplyUVE(InverseUVE(rigRest[bone].rotation), InverseUVE(conformed.moveOfRigBone[bone].rotation)),
            conformedRest[index].rotation));
    }
    std::vector<std::int32_t> follows(count, -1);
    for (std::size_t index = 0U; index < count && index < reference.info.size(); ++index) {
        if (conformed.referenceOfBone[index] >= 0) {
            follows[index] = reference.info[static_cast<std::size_t>(conformed.referenceOfBone[index])].follows;
        }
    }

    std::vector<Asset::AnimationAssetBoneTrackUVE> tracks(count);
    for (std::size_t index = 0U; index < count; ++index) {
        tracks[index].bone = skeleton.bones[index].name;
        tracks[index].samples.reserve(times.size());
    }
    RetargetSkeletonUVE posed = rig;
    std::vector<WorldTransformUVE> world(count);
    std::vector<bool> done(count);
    for (const double time : times) {
        for (std::size_t bone = 0U; bone < rig.bones.size(); ++bone) {
            if (const Asset::AnimationAssetBoneTrackUVE* const track = trackOfRigBone[bone]; track != nullptr) {
                const Asset::AnimationAssetPoseUVE pose = SampleTrackUVE(*track, time);
                posed.bones[bone].position = pose.position;
                posed.bones[bone].rotation = pose.rotation;
                posed.bones[bone].scale = pose.scale.x;
            }
        }
        const std::vector<WorldTransformUVE> source = ComputeWorldTransformsUVE(posed);
        std::fill(done.begin(), done.end(), false);
        // Bones the rig had follow the clip; added ones ride their parent; IK targets sit on what they
        // follow (which may come later in the order, hence the passes).
        for (bool progress = true; progress;) {
            progress = false;
            for (std::size_t index = 0U; index < count; ++index) {
                if (done[index]) {
                    continue;
                }
                const std::int32_t parent = skeleton.bones[index].parent;
                if (const std::int32_t bone = rigOfBone[index]; bone >= 0) {
                    const WorldTransformUVE& from = source[static_cast<std::size_t>(bone)];
                    world[index].position = from.position;
                    world[index].rotation = NormalizedUVE(Math::MultiplyUVE(from.rotation, toNewFrame[index]));
                } else if (follows[index] >= 0) {
                    if (!done[static_cast<std::size_t>(follows[index])]) {
                        continue;
                    }
                    world[index].position = world[static_cast<std::size_t>(follows[index])].position;
                    world[index].rotation = world[static_cast<std::size_t>(follows[index])].rotation;
                } else if (parent >= 0) {
                    if (!done[static_cast<std::size_t>(parent)]) {
                        continue;
                    }
                    const WorldTransformUVE& above = world[static_cast<std::size_t>(parent)];
                    world[index].position = above.position + Math::RotateVectorUVE(above.rotation, skeleton.bones[index].position);
                    world[index].rotation = NormalizedUVE(Math::MultiplyUVE(above.rotation, skeleton.bones[index].rotation));
                } else {
                    world[index] = conformedRest[index];
                }
                world[index].scale = 1.0F;
                done[index] = true;
                progress = true;
            }
        }
        const RetargetSkeletonUVE local = SkeletonFromWorldUVE(skeleton, world);
        for (std::size_t index = 0U; index < count; ++index) {
            Asset::AnimationAssetSampleUVE sample;
            sample.timeSeconds = time;
            sample.pose.position = local.bones[index].position;
            sample.pose.rotation = local.bones[index].rotation;
            tracks[index].samples.push_back(sample);
        }
    }
    for (Asset::AnimationAssetBoneTrackUVE& track : tracks) {
        const bool still = std::ranges::all_of(track.samples, [&track](const Asset::AnimationAssetSampleUVE& sample) {
            return SamePoseUVE(sample.pose, track.samples.front().pose);
        });
        if (still) {
            track.samples.resize(1U);
        }
    }
    Asset::AnimationClipAssetUVE result;
    result.clipId = clip.clipId;
    result.durationSeconds = clip.durationSeconds;
    result.samples = clip.samples;
    result.events = clip.events;
    result.bones = std::move(tracks);
    if (!Asset::IsAnimationClipAssetValidUVE(result)) {
        SetErrorUVE(error, "the conformed clip is not valid (too many bones or a non-finite pose)");
        return std::nullopt;
    }
    return result;
}

} // namespace UVE::Retarget
