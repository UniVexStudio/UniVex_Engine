// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "retarget_conform.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>

#include "retarget_keys.h"
#include "retarget_pose.h"

namespace UVE::Retarget {
namespace {

void SetError(std::string* error, std::string text) {
    if (error != nullptr) {
        *error = std::move(text);
    }
}

[[nodiscard]] Math::Quaternion Inverse(const Math::Quaternion& value) noexcept {
    Math::Quaternion out{};
    return Math::TryInverse(value, out) ? out : Math::Quaternion{};
}

[[nodiscard]] Math::Quaternion Normalized(const Math::Quaternion& value) noexcept {
    Math::Quaternion out{};
    return Math::TryNormalize(value, out) ? out : Math::Quaternion{};
}

/// Bones that turn to the A-pose on a conformed rig: the limbs and fingers. Hips, spine, neck,
/// head, clavicles, metacarpals, the thumb's base and the feet keep the rig's own shape.
[[nodiscard]] bool TurnsToThePose(const BoneKeyParts& key) {
    if (key.Is("upperarm") || key.Is("forearm") || key.Is("hand") || key.Is("thigh") || key.Is("shin")) {
        return true;
    }
    return key.IsFinger() && key.Number() > 0 && !(key.words[0] == "thumb" && key.Number() == 1);
}

/// The rotation (column vectors) of a matrix whose 3x3 part is a rotation times a uniform scale.
[[nodiscard]] bool Decompose(const Math::Matrix4x4& matrix, WorldTransform& out) {
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
    const float scale = (scales[0] + scales[1] + scales[2]) / 3.0F;
    if (std::abs(scales[0] - scale) > 0.15F * scale || std::abs(scales[1] - scale) > 0.15F * scale ||
        std::abs(scales[2] - scale) > 0.15F * scale) {
        return false; // strongly non-uniform
    }
    const float determinant = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) -
                              r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                              r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (determinant < 0.85F || determinant > 1.15F) {
        return false; // mirrored or heavily sheared
    }
    // Orthonormalize columns via Gram-Schmidt so minor FBX bind-matrix drift produces a pure rotation.
    const float dot01 = r[0][0] * r[0][1] + r[1][0] * r[1][1] + r[2][0] * r[2][1];
    r[0][1] -= dot01 * r[0][0];
    r[1][1] -= dot01 * r[1][0];
    r[2][1] -= dot01 * r[2][0];
    const float len1 = std::sqrt(r[0][1] * r[0][1] + r[1][1] * r[1][1] + r[2][1] * r[2][1]);
    if (len1 > 1e-6F) {
        r[0][1] /= len1;
        r[1][1] /= len1;
        r[2][1] /= len1;
    }
    r[0][2] = r[1][0] * r[2][1] - r[2][0] * r[1][1];
    r[1][2] = r[2][0] * r[0][1] - r[0][0] * r[2][1];
    r[2][2] = r[0][0] * r[1][1] - r[1][0] * r[0][1];
    Math::Quaternion q{};
    const float trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0.0F) {
        const float s = std::sqrt(trace + 1.0F) * 2.0F;
        q = Math::Quaternion{(r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, 0.25F * s};
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const float s = std::sqrt(1.0F + r[0][0] - r[1][1] - r[2][2]) * 2.0F;
        q = Math::Quaternion{0.25F * s, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s, (r[2][1] - r[1][2]) / s};
    } else if (r[1][1] > r[2][2]) {
        const float s = std::sqrt(1.0F + r[1][1] - r[0][0] - r[2][2]) * 2.0F;
        q = Math::Quaternion{(r[0][1] + r[1][0]) / s, 0.25F * s, (r[1][2] + r[2][1]) / s, (r[0][2] - r[2][0]) / s};
    } else {
        const float s = std::sqrt(1.0F + r[2][2] - r[0][0] - r[1][1]) * 2.0F;
        q = Math::Quaternion{(r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, 0.25F * s, (r[1][0] - r[0][1]) / s};
    }
    out.position = Math::Vector3{matrix.m[0][3], matrix.m[1][3], matrix.m[2][3]};
    out.rotation = Normalized(q);
    out.scale = scale;
    return true;
}

[[nodiscard]] Asset::AnimationAssetPose SampleTrack(const Asset::AnimationAssetBoneTrack& track, const double time) {
    const std::vector<Asset::AnimationAssetSample>& samples = track.samples;
    if (samples.empty()) {
        return {};
    }
    if (samples.size() == 1U || time <= samples.front().timeSeconds) {
        return samples.front().pose;
    }
    if (time >= samples.back().timeSeconds) {
        return samples.back().pose;
    }
    const auto after = std::ranges::upper_bound(samples, time, {}, &Asset::AnimationAssetSample::timeSeconds);
    const Asset::AnimationAssetSample& b = *after;
    const Asset::AnimationAssetSample& a = *(after - 1);
    const double span = b.timeSeconds - a.timeSeconds;
    const float t = span > 0.0 ? static_cast<float>((time - a.timeSeconds) / span) : 0.0F;
    Asset::AnimationAssetPose pose;
    pose.position = a.pose.position + (b.pose.position - a.pose.position) * t;
    pose.scale = a.pose.scale + (b.pose.scale - a.pose.scale) * t;
    if (!Math::TrySlerp(a.pose.rotation, b.pose.rotation, t, pose.rotation)) {
        pose.rotation = a.pose.rotation;
    }
    return pose;
}

[[nodiscard]] bool SamePose(const Asset::AnimationAssetPose& a, const Asset::AnimationAssetPose& b) {
    constexpr float kTolerance = 1e-6F;
    const auto near = [](const float x, const float y) { return std::abs(x - y) <= kTolerance; };
    return near(a.position.x, b.position.x) && near(a.position.y, b.position.y) && near(a.position.z, b.position.z) &&
           near(a.rotation.x, b.rotation.x) && near(a.rotation.y, b.rotation.y) && near(a.rotation.z, b.rotation.z) &&
           near(a.rotation.w, b.rotation.w);
}

/// A rotation and a translation as one dual quaternion, for blending moves without pinching.
struct DualQuaternion final {
    Math::Quaternion real{0.0F, 0.0F, 0.0F, 0.0F};
    Math::Quaternion dual{0.0F, 0.0F, 0.0F, 0.0F};
};

[[nodiscard]] DualQuaternion ToDual(const RigidMove& move) {
    const Math::Quaternion translation{move.translation.x, move.translation.y, move.translation.z, 0.0F};
    const Math::Quaternion product = Math::Multiply(translation, move.rotation);
    return DualQuaternion{move.rotation, Math::Quaternion{0.5F * product.x, 0.5F * product.y, 0.5F * product.z, 0.5F * product.w}};
}

} // namespace

namespace {

/// Drops the optional bones conforming added (the correctives the rig lacked) together with every
/// index that points at them, keeping any bone of the rig's own that sits under one.
void PruneAddedBones(ConformedRig& out, const HumanoidReference& reference, const bool optional, const bool body) {
    const std::size_t count = out.skeleton.bones.size();
    std::vector<bool> drop(count, false);
    for (std::size_t index = 0U; index < count; ++index) {
        const std::int32_t ref = index < out.referenceOfBone.size() ? out.referenceOfBone[index] : -1;
        if (index >= out.added.size() || !out.added[index] || ref < 0 ||
            static_cast<std::size_t>(ref) >= reference.info.size()) {
            continue;
        }
        const HumanoidBoneInfo& info = reference.info[static_cast<std::size_t>(ref)];
        const bool isBody = info.kind == HumanoidBoneKind::Body || info.kind == HumanoidBoneKind::Finger ||
                            info.kind == HumanoidBoneKind::Twist;
        drop[index] = (optional && info.optional) || (body && !info.optional && isBody);
    }
    // A kept bone keeps every bone above it.
    for (std::size_t index = 0U; index < count; ++index) {
        if (drop[index]) {
            continue;
        }
        std::size_t hops = 0U;
        for (std::int32_t above = out.skeleton.bones[index].parent;
             above >= 0 && static_cast<std::size_t>(above) < count && hops < count;
             above = out.skeleton.bones[static_cast<std::size_t>(above)].parent, ++hops) {
            drop[static_cast<std::size_t>(above)] = false;
        }
    }
    std::vector<std::int32_t> newIndex(count, -1);
    std::int32_t next = 0;
    for (std::size_t index = 0U; index < count; ++index) {
        if (!drop[index]) {
            newIndex[index] = next++;
        }
    }
    if (static_cast<std::size_t>(next) == count) {
        return;
    }
    const auto prune = [&](RetargetSkeleton& skeleton) {
        RetargetSkeleton kept;
        for (std::size_t index = 0U; index < count; ++index) {
            if (drop[index]) {
                continue;
            }
            RetargetBone bone = skeleton.bones[index];
            bone.parent = (bone.parent >= 0 && static_cast<std::size_t>(bone.parent) < count)
                              ? newIndex[static_cast<std::size_t>(bone.parent)]
                              : -1;
            kept.bones.push_back(std::move(bone));
        }
        skeleton = std::move(kept);
    };
    prune(out.skeleton);
    if (out.restSkeleton.bones.size() == count) {
        prune(out.restSkeleton);
    }
    std::vector<std::int32_t> referenceOfBone;
    std::vector<bool> added;
    for (std::size_t index = 0U; index < count; ++index) {
        if (!drop[index]) {
            referenceOfBone.push_back(index < out.referenceOfBone.size() ? out.referenceOfBone[index] : -1);
            added.push_back(index < out.added.size() ? out.added[index] : false);
        }
    }
    out.referenceOfBone = std::move(referenceOfBone);
    out.added = std::move(added);
    for (std::int32_t& at : out.boneOfRigBone) {
        at = (at >= 0 && static_cast<std::size_t>(at) < count) ? newIndex[static_cast<std::size_t>(at)] : -1;
    }
}

} // namespace

std::optional<ConformedRig> ConformSkeleton(const RetargetSkeleton& rig, const HumanoidMatch& match,
                                                  const HumanoidReference& reference, std::string* error,
                                                  const ConformOptions& options) {
    const std::size_t referenceCount = reference.skeleton.bones.size();
    if (!IsRetargetSkeletonValid(rig) || match.referenceOfBone.size() != rig.bones.size() ||
        match.joints.size() != referenceCount || reference.info.size() != referenceCount) {
        SetError(error, "the rig is malformed or was not matched against this reference");
        return std::nullopt;
    }
    std::int32_t hips = -1;
    for (std::size_t index = 0U; index < referenceCount; ++index) {
        if (reference.info[index].key == "C|hips") {
            hips = static_cast<std::int32_t>(index);
        }
    }
    if (hips < 0 || match.joints[static_cast<std::size_t>(hips)].bone < 0) {
        SetError(error, "the rig has no hips");
        return std::nullopt;
    }
    const RetargetSkeleton folded = FoldScale(rig);
    const std::vector<WorldTransform> rigWorld = ComputeWorldTransforms(folded);
    const std::vector<WorldTransform> referenceWorld = ComputeWorldTransforms(reference.skeleton);

    // ---- Layout: the humanoid's bones, then the rig's own ------------------------------------
    ConformedRig out;
    out.heightScale = match.heightScale > 0.0F ? match.heightScale : 1.0F;
    RetargetSkeleton& layout = out.skeleton;
    layout.bones = reference.skeleton.bones;
    std::vector<WorldTransform> world(referenceCount);
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
    for (const RetargetBone& bone : layout.bones) {
        names.insert(bone.name);
    }
    for (std::size_t bone = 0U; bone < rig.bones.size(); ++bone) {
        if (out.boneOfRigBone[bone] >= 0) {
            continue;
        }
        RetargetBone kept = folded.bones[bone];
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

    // Stand the rig up +Y facing +Z first, so every direction below means what the reference's do.
    out.orientation = OrientationOfRig(rigWorld, match, reference);
    for (WorldTransform& transform : world) {
        transform.position = Math::RotateVector(out.orientation, transform.position);
        transform.rotation = Normalized(Math::Multiply(out.orientation, transform.rotation));
    }

    // What each bone is, and what it points at, among the bones the rig has.
    std::vector<BoneKeyParts> keys(count);
    std::vector<std::int32_t> aims(count, -1);
    std::unordered_map<std::string, std::int32_t> byKey;
    for (std::size_t index = 0U; index < referenceCount; ++index) {
        if (out.added[index]) {
            continue;
        }
        keys[index] = SplitBoneKey(reference.info[index].key);
        byKey.emplace(reference.info[index].key, static_cast<std::int32_t>(index));
        std::int32_t aim = reference.info[index].aim;
        while (aim >= 0 && out.added[static_cast<std::size_t>(aim)]) {
            aim = reference.info[static_cast<std::size_t>(aim)].aim;
        }
        if (aim >= 0 && IsBelow(layout, aim, static_cast<std::int32_t>(index))) {
            aims[index] = aim;
        }
    }

    // ---- The A-pose ---------------------------------------------------------------------------
    bool hadFeet = false;
    const float groundBefore = LowestFoot(keys, world, hadFeet);
    std::vector<Math::Quaternion> rotationBefore(count);
    for (std::size_t index = 0U; index < count; ++index) {
        rotationBefore[index] = world[index].rotation;
    }
    const std::vector<std::optional<Math::Vector3>> elbowAxes = ReadElbowAxes(keys, aims, world);
    const auto aimAll = [&] {
        for (std::size_t index = 0U; index < referenceCount; ++index) {
            const std::int32_t aim = aims[index];
            if (aim < 0 || !TurnsToThePose(keys[index])) {
                continue;
            }
            const auto target = static_cast<std::size_t>(aim);
            const Math::Vector3 want = referenceWorld[target].position - referenceWorld[index].position;
            const Math::Vector3 now = world[target].position - world[index].position;
            RotateSubtree(layout, world, index, RotationBetween(now, want));
        }
    };
    // The A-pose bends the elbows forward, so rolling an upper arm swings its forearm: aim, roll,
    // and aim again until the limbs lie the reference's way with the roll kept.
    aimAll();
    for (int pass = 0; pass < 3; ++pass) {
        RollArms(layout, world, keys, aims, elbowAxes, byKey);
        aimAll();
    }
    for (std::size_t index = 0U; index < referenceCount; ++index) {
        if (keys[index].Is("foot") && keys[index].side != 'C') {
            RestoreRotation(layout, world, index, rotationBefore[index]);
        }
    }
    bool hasFeet = false;
    const float groundAfter = LowestFoot(keys, world, hasFeet);
    if (hadFeet && hasFeet) {
        Lift(layout, world, hips, groundBefore - groundAfter);
    }
    out.moveOfRigBone.resize(rig.bones.size());
    for (std::size_t bone = 0U; bone < rig.bones.size(); ++bone) {
        const WorldTransform& posed = world[static_cast<std::size_t>(out.boneOfRigBone[bone])];
        RigidMove& move = out.moveOfRigBone[bone];
        move.rotation = Normalized(Math::Multiply(posed.rotation, Inverse(rigWorld[bone].rotation)));
        move.translation = posed.position - Math::RotateVector(move.rotation, rigWorld[bone].position);
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
        const Math::Vector3& at = referenceWorld[index].position;
        Math::Vector3 position{};
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
            const Math::Vector3 offset = at - referenceWorld[base].position;
            position = world[base].position + offset * out.heightScale;
            if (down >= 0) {
                const Math::Vector3 referenceSegment = referenceWorld[static_cast<std::size_t>(down)].position - referenceWorld[base].position;
                const Math::Vector3 rigSegment = world[static_cast<std::size_t>(down)].position - world[base].position;
                const float referenceLength = Math::Length(referenceSegment);
                const float rigLength = Math::Length(rigSegment);
                if (referenceLength > 1e-6F && rigLength > 1e-6F) {
                    position = world[base].position + Math::RotateVector(RotationBetween(referenceSegment, rigSegment), offset) *
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
    layout = SkeletonFromWorld(layout, world);

    // ---- The same bones at the rig's own rest ------------------------------------------------------
    // Each bone taken back the way its rig bone came into the A-pose (an added bone rides the
    // nearest rig bone above it; an IK target sits on what it follows), then stood up like the rig:
    // the geometry the mesh was made in, spoken of in the humanoid's frames.
    {
        const std::size_t count = layout.bones.size();
        std::vector<std::int32_t> rigOfBone(count, -1);
        for (std::size_t bone = 0U; bone < out.boneOfRigBone.size(); ++bone) {
            if (const std::int32_t at = out.boneOfRigBone[bone]; at >= 0) {
                rigOfBone[static_cast<std::size_t>(at)] = static_cast<std::int32_t>(bone);
            }
        }
        const std::int32_t hipsRig = rigOfBone[static_cast<std::size_t>(hips)];
        const std::vector<WorldTransform> posed = ComputeWorldTransforms(layout);
        std::vector<WorldTransform> rest(count);
        for (std::size_t index = 0U; index < count; ++index) {
            std::int32_t carrier = -1;
            for (std::int32_t above = static_cast<std::int32_t>(index); above >= 0 && carrier < 0;
                 above = layout.bones[static_cast<std::size_t>(above)].parent) {
                carrier = rigOfBone[static_cast<std::size_t>(above)];
            }
            if (carrier < 0) {
                carrier = hipsRig;
            }
            const RigidMove& move = out.moveOfRigBone[static_cast<std::size_t>(carrier)];
            const Math::Quaternion back = Inverse(move.rotation);
            const Math::Quaternion standUp = out.orientation;
            const Math::Vector3 unposed = Math::RotateVector(back, posed[index].position - move.translation);
            rest[index].position = Math::RotateVector(standUp, unposed);
            rest[index].rotation = Normalized(Math::Multiply(standUp, Math::Multiply(back, posed[index].rotation)));
            rest[index].scale = 1.0F;
        }
        for (std::size_t index = 0U; index < count; ++index) {
            const std::int32_t ref = out.referenceOfBone[index];
            if (ref >= 0 && out.added[index] && reference.info[static_cast<std::size_t>(ref)].follows >= 0) {
                rest[index] = rest[static_cast<std::size_t>(reference.info[static_cast<std::size_t>(ref)].follows)];
            }
        }
        out.restSkeleton = SkeletonFromWorld(layout, rest);
    }

    // ---- Only what was asked for -----------------------------------------------------------------
    const bool pruneBody = options.addBodyBonesOnlyWhenLacking && !IsRigVeryLacking(match, reference, options.lackingFraction);
    if (!options.includeOptional || pruneBody) {
        PruneAddedBones(out, reference, !options.includeOptional, pruneBody);
    }
    return out;
}

BodyBoneCount CountBodyBones(const HumanoidMatch& match, const HumanoidReference& reference) {
    BodyBoneCount count;
    for (std::size_t ref = 0U; ref < reference.info.size() && ref < match.joints.size(); ++ref) {
        const HumanoidBoneInfo& info = reference.info[ref];
        if (info.optional) {
            continue;
        }
        const bool missing = match.joints[ref].status == JointStatus::Missing;
        if (info.kind == HumanoidBoneKind::Root || info.kind == HumanoidBoneKind::IK || info.kind == HumanoidBoneKind::Socket) {
            count.missingHelpers += missing ? 1U : 0U;
            continue;
        }
        if (info.kind == HumanoidBoneKind::Body || info.kind == HumanoidBoneKind::Finger || info.kind == HumanoidBoneKind::Twist) {
            ++count.total;
            count.missing += missing ? 1U : 0U;
        }
    }
    return count;
}

bool IsRigVeryLacking(const HumanoidMatch& match, const HumanoidReference& reference, const float lackingFraction) {
    const BodyBoneCount count = CountBodyBones(match, reference);
    return count.total > 0U && static_cast<float>(count.missing) > lackingFraction * static_cast<float>(count.total);
}

std::optional<RetargetSkeleton> RigFromMesh(const Asset::MeshAsset& mesh, std::string* error) {
    if (mesh.joints.empty()) {
        SetError(error, "the mesh has no skeleton");
        return std::nullopt;
    }
    RetargetSkeleton rig;
    std::vector<WorldTransform> world;
    for (std::size_t index = 0U; index < mesh.joints.size(); ++index) {
        const Asset::MeshJoint& joint = mesh.joints[index];
        if (joint.name.empty()) {
            SetError(error, "joint " + std::to_string(index) + " has no name (import the model again)");
            return std::nullopt;
        }
        Math::Matrix4x4 bind{};
        WorldTransform transform;
        if (!Math::TryInverse(joint.inverseBindMatrix, bind) || !Decompose(bind, transform)) {
            SetError(error, joint.name + "'s bind is not a rotation, translation and uniform scale");
            return std::nullopt;
        }
        transform.scale = 1.0F;
        RetargetBone bone;
        bone.name = joint.name;
        bone.parent = joint.parentIndex == Asset::kInvalidJointParent ? -1 : static_cast<std::int32_t>(joint.parentIndex);
        rig.bones.push_back(std::move(bone));
        world.push_back(transform);
    }
    rig = SkeletonFromWorld(rig, world);
    if (!IsRetargetSkeletonValid(rig)) {
        SetError(error, "the mesh's joints do not form a skeleton (unique names, parents first)");
        return std::nullopt;
    }
    return rig;
}

bool ConformMesh(Asset::MeshAsset& mesh, const ConformedRig& conformed, std::string* error, const SkinBlend blend,
                    const ConformMeshMode mode) {
    if (!mesh.IsSkinned() || mesh.joints.size() != conformed.boneOfRigBone.size() ||
        mesh.joints.size() != conformed.moveOfRigBone.size() || mesh.skinningInfluences.size() != mesh.vertices.size()) {
        SetError(error, "the mesh and the conformed rig do not belong together");
        return false;
    }
    Asset::MeshAsset result = mesh;
    const bool keepRest = mode == ConformMeshMode::KeepRestPose;
    if (keepRest && conformed.restSkeleton.bones.size() != conformed.skeleton.bones.size()) {
        SetError(error, "the conformed rig has no rest skeleton");
        return false;
    }
    const Math::Quaternion standUp = conformed.orientation;
    const bool upright = standUp.x == 0.0F && standUp.y == 0.0F && standUp.z == 0.0F;
    for (std::size_t vertexIndex = 0U; vertexIndex < result.vertices.size(); ++vertexIndex) {
        if (keepRest) {
            // Only re-pointed at the conformed bones, and stood up when the rig lay another way.
            Asset::MeshSkinningInfluence& influence = result.skinningInfluences[vertexIndex];
            for (std::size_t slot = 0U; slot < Asset::kMaxJointInfluences; ++slot) {
                const std::uint32_t joint = influence.joints[slot];
                if (joint >= conformed.boneOfRigBone.size()) {
                    SetError(error, "a vertex names a joint the mesh does not have");
                    return false;
                }
                const std::int32_t mapped = conformed.boneOfRigBone[joint];
                if (mapped < 0 || static_cast<std::size_t>(mapped) >= conformed.skeleton.bones.size()) {
                    SetError(error, "a vertex references an unmapped conformed bone");
                    return false;
                }
                influence.joints[slot] = static_cast<std::uint32_t>(mapped);
            }
            if (!upright) {
                Asset::MeshVertex& vertex = result.vertices[vertexIndex];
                vertex.position = Math::RotateVector(standUp, vertex.position);
                vertex.normal = Math::RotateVector(standUp, vertex.normal);
                vertex.tangent = Math::RotateVector(standUp, vertex.tangent);
            }
            continue;
        }
        Asset::MeshVertex& vertex = result.vertices[vertexIndex];
        Asset::MeshSkinningInfluence& influence = result.skinningInfluences[vertexIndex];
        Math::Vector3 position{};
        Math::Vector3 normal{};
        Math::Vector3 tangent{};
        float total = 0.0F;
        DualQuaternion blended;
        const Math::Quaternion* first = nullptr;
        for (std::size_t slot = 0U; slot < Asset::kMaxJointInfluences; ++slot) {
            const std::uint32_t joint = influence.joints[slot];
            if (joint >= conformed.moveOfRigBone.size() || joint >= conformed.boneOfRigBone.size()) {
                SetError(error, "a vertex names a joint the mesh does not have");
                return false;
            }
            const float weight = influence.weights[slot];
            const std::int32_t mapped = conformed.boneOfRigBone[joint];
            if (mapped < 0 || static_cast<std::size_t>(mapped) >= conformed.skeleton.bones.size()) {
                if (weight > 0.0F) {
                    SetError(error, "a vertex references an unmapped conformed bone");
                    return false;
                }
                influence.joints[slot] = 0U;
                continue;
            }
            influence.joints[slot] = static_cast<std::uint32_t>(mapped);
            if (weight <= 0.0F) {
                continue;
            }
            const RigidMove& move = conformed.moveOfRigBone[joint];
            total += weight;
            if (blend == SkinBlend::Linear) {
                position += move.Apply(vertex.position) * weight;
                normal += Math::RotateVector(move.rotation, vertex.normal) * weight;
                tangent += Math::RotateVector(move.rotation, vertex.tangent) * weight;
                continue;
            }
            // Dual quaternions: each move enters on the same side of the sphere as the first, so two
            // rotations that are the same turn do not cancel.
            const DualQuaternion dual = ToDual(move);
            if (first == nullptr) {
                first = &move.rotation;
            }
            const float sign = (dual.real.x * first->x + dual.real.y * first->y + dual.real.z * first->z + dual.real.w * first->w) < 0.0F
                                   ? -weight
                                   : weight;
            blended.real = Math::Quaternion{blended.real.x + sign * dual.real.x, blended.real.y + sign * dual.real.y,
                                               blended.real.z + sign * dual.real.z, blended.real.w + sign * dual.real.w};
            blended.dual = Math::Quaternion{blended.dual.x + sign * dual.dual.x, blended.dual.y + sign * dual.dual.y,
                                               blended.dual.z + sign * dual.dual.z, blended.dual.w + sign * dual.dual.w};
        }
        if (total <= 0.0F) {
            continue;
        }
        if (blend == SkinBlend::DualQuaternion) {
            const float length = std::sqrt(blended.real.x * blended.real.x + blended.real.y * blended.real.y +
                                           blended.real.z * blended.real.z + blended.real.w * blended.real.w);
            if (!(length > 1e-8F)) {
                continue;
            }
            const float scale = 1.0F / length;
            const Math::Quaternion real{blended.real.x * scale, blended.real.y * scale, blended.real.z * scale, blended.real.w * scale};
            const Math::Quaternion dual{blended.dual.x * scale, blended.dual.y * scale, blended.dual.z * scale, blended.dual.w * scale};
            const Math::Quaternion conjugate{-real.x, -real.y, -real.z, real.w};
            const Math::Quaternion shift = Math::Multiply(dual, conjugate);
            vertex.position = Math::RotateVector(real, vertex.position) + Math::Vector3{2.0F * shift.x, 2.0F * shift.y, 2.0F * shift.z};
            vertex.normal = Math::Normalize(Math::RotateVector(real, vertex.normal));
            vertex.tangent = Math::Normalize(Math::RotateVector(real, vertex.tangent));
            continue;
        }
        vertex.position = position * (1.0F / total);
        if (Math::LengthSquared(normal) > 1e-12F) {
            vertex.normal = Math::Normalize(normal);
        }
        if (Math::LengthSquared(tangent) > 1e-12F) {
            vertex.tangent = Math::Normalize(tangent);
        }
    }
    const std::vector<WorldTransform> world = ComputeWorldTransforms(keepRest ? conformed.restSkeleton : conformed.skeleton);
    result.joints.clear();
    for (std::size_t index = 0U; index < conformed.skeleton.bones.size(); ++index) {
        const RetargetBone& bone = conformed.skeleton.bones[index];
        Asset::MeshJoint joint;
        joint.name = bone.name;
        joint.parentIndex = bone.parent < 0 ? Asset::kInvalidJointParent : static_cast<std::uint32_t>(bone.parent);
        const Math::Quaternion inverse = Inverse(world[index].rotation);
        joint.inverseBindMatrix = Math::Matrix4x4::ComposeTrs(-Math::RotateVector(inverse, world[index].position), inverse,
                                                                    Math::Vector3{1.0F, 1.0F, 1.0F});
        result.joints.push_back(std::move(joint));
    }
    if (!result.vertices.empty()) {
        Math::Vector3 low = result.vertices.front().position;
        Math::Vector3 high = low;
        for (const Asset::MeshVertex& vertex : result.vertices) {
            low = Math::Vector3{std::min(low.x, vertex.position.x), std::min(low.y, vertex.position.y),
                                   std::min(low.z, vertex.position.z)};
            high = Math::Vector3{std::max(high.x, vertex.position.x), std::max(high.y, vertex.position.y),
                                    std::max(high.z, vertex.position.z)};
        }
        result.localBounds = Math::Aabb{low, high};
    }
    mesh = std::move(result);
    return true;
}

MeshDistortion MeasureMeshDistortion(const Asset::MeshAsset& before, const Asset::MeshAsset& after) {
    MeshDistortion result;
    if (before.vertices.size() != after.vertices.size() || before.indices != after.indices) {
        return result;
    }
    std::size_t edges = 0U;
    std::size_t changed = 0U;
    const auto measure = [&](const std::uint32_t a, const std::uint32_t b) {
        if (a >= before.vertices.size() || b >= before.vertices.size()) {
            return;
        }
        const float was = Math::Length(before.vertices[a].position - before.vertices[b].position);
        const float now = Math::Length(after.vertices[a].position - after.vertices[b].position);
        if (!(was > 1e-9F)) {
            return;
        }
        const float change = std::abs(now / was - 1.0F);
        ++edges;
        changed += change > 0.1F ? 1U : 0U;
        result.maximumEdgeChange = std::max(result.maximumEdgeChange, change);
    };
    for (std::size_t index = 0U; index + 2U < before.indices.size(); index += 3U) {
        measure(before.indices[index], before.indices[index + 1U]);
        measure(before.indices[index + 1U], before.indices[index + 2U]);
        measure(before.indices[index + 2U], before.indices[index]);
    }
    result.fractionOverTenPercent = edges > 0U ? static_cast<float>(changed) / static_cast<float>(edges) : 0.0F;
    return result;
}

std::optional<Asset::AnimationClipAsset> ConformClip(const Asset::AnimationClipAsset& clip, const RetargetSkeleton& rig,
                                                          const ConformedRig& conformed, const HumanoidReference& reference,
                                                          std::string* error) {
    if (rig.bones.size() != conformed.boneOfRigBone.size() || rig.bones.size() != conformed.moveOfRigBone.size() ||
        !IsRetargetSkeletonValid(rig)) {
        SetError(error, "the clip's rig does not belong to the conformed rig");
        return std::nullopt;
    }
    if (!clip.IsSkeletal()) {
        SetError(error, "the clip moves no bones");
        return std::nullopt;
    }
    // Frames: every time any track has a key. Tracks with their own key times (a reduced clip) keep
    // every key: sampling only the densest track's times joined the hips' bob by straight lines
    // between another bone's keys and lowered it.
    std::vector<double> times;
    for (const Asset::AnimationAssetBoneTrack& track : clip.bones) {
        for (const Asset::AnimationAssetSample& sample : track.samples) {
            times.push_back(sample.timeSeconds);
        }
    }
    std::ranges::sort(times);
    std::vector<double> unique;
    unique.reserve(times.size());
    for (const double t : times) {
        if (unique.empty() || t - unique.back() > 1.0e-4) {
            unique.push_back(t);
        }
    }
    times = std::move(unique);
    std::vector<const Asset::AnimationAssetBoneTrack*> trackOfRigBone(rig.bones.size(), nullptr);
    for (const Asset::AnimationAssetBoneTrack& track : clip.bones) {
        if (track.samples.empty()) {
            continue;
        }
        if (const std::int32_t bone = FindBone(rig, track.bone);
            bone >= 0 && static_cast<std::size_t>(bone) < trackOfRigBone.size()) {
            trackOfRigBone[static_cast<std::size_t>(bone)] = &track;
        }
    }

    const RetargetSkeleton& skeleton = conformed.skeleton;
    const std::size_t count = skeleton.bones.size();
    const std::vector<WorldTransform> rigRest = ComputeWorldTransforms(rig);
    const std::vector<WorldTransform> conformedRest = ComputeWorldTransforms(skeleton);
    // A rig bone's frame in the clip, turned into the conformed bone's: the clip's geometry stays
    // where it was, spoken of in the new frame.
    std::vector<std::int32_t> rigOfBone(count, -1);
    std::vector<Math::Quaternion> toNewFrame(count);
    for (std::size_t bone = 0U; bone < rig.bones.size(); ++bone) {
        const std::int32_t mapped = conformed.boneOfRigBone[bone];
        if (mapped < 0 || static_cast<std::size_t>(mapped) >= count) {
            continue;
        }
        const auto index = static_cast<std::size_t>(mapped);
        rigOfBone[index] = static_cast<std::int32_t>(bone);
        toNewFrame[index] = Normalized(Math::Multiply(
            Math::Multiply(Inverse(rigRest[bone].rotation), Inverse(conformed.moveOfRigBone[bone].rotation)),
            conformedRest[index].rotation));
    }
    // Reference indices to this skeleton's (they differ once optional bones were left out).
    std::vector<std::int32_t> boneOfReference(reference.info.size(), -1);
    for (std::size_t index = 0U; index < count && index < conformed.referenceOfBone.size(); ++index) {
        if (const std::int32_t ref = conformed.referenceOfBone[index]; ref >= 0 && static_cast<std::size_t>(ref) < boneOfReference.size()) {
            boneOfReference[static_cast<std::size_t>(ref)] = static_cast<std::int32_t>(index);
        }
    }
    std::vector<std::int32_t> follows(count, -1);
    for (std::size_t index = 0U; index < count && index < conformed.referenceOfBone.size(); ++index) {
        if (const std::int32_t ref = conformed.referenceOfBone[index];
            ref >= 0 && static_cast<std::size_t>(ref) < reference.info.size()) {
            const std::int32_t followed = reference.info[static_cast<std::size_t>(ref)].follows;
            follows[index] = (followed >= 0 && static_cast<std::size_t>(followed) < boneOfReference.size())
                                 ? boneOfReference[static_cast<std::size_t>(followed)]
                                 : -1;
        }
    }

    std::vector<Asset::AnimationAssetBoneTrack> tracks(count);
    for (std::size_t index = 0U; index < count; ++index) {
        tracks[index].bone = skeleton.bones[index].name;
        tracks[index].samples.reserve(times.size());
    }
    RetargetSkeleton posed = rig;
    std::vector<WorldTransform> world(count);
    std::vector<bool> done(count);
    for (const double time : times) {
        for (std::size_t bone = 0U; bone < rig.bones.size(); ++bone) {
            if (const Asset::AnimationAssetBoneTrack* const track = trackOfRigBone[bone]; track != nullptr) {
                const Asset::AnimationAssetPose pose = SampleTrack(*track, time);
                posed.bones[bone].position = pose.position;
                posed.bones[bone].rotation = pose.rotation;
                posed.bones[bone].scale = pose.scale.x;
            }
        }
        const std::vector<WorldTransform> source = ComputeWorldTransforms(posed);
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
                if (const std::int32_t bone = rigOfBone[index];
                    bone >= 0 && static_cast<std::size_t>(bone) < source.size()) {
                    const WorldTransform& from = source[static_cast<std::size_t>(bone)];
                    world[index].position = Math::RotateVector(conformed.orientation, from.position);
                    world[index].rotation = Normalized(
                        Math::Multiply(conformed.orientation, Math::Multiply(from.rotation, toNewFrame[index])));
                } else if (follows[index] >= 0 && static_cast<std::size_t>(follows[index]) < count) {
                    if (!done[static_cast<std::size_t>(follows[index])]) {
                        continue;
                    }
                    world[index].position = world[static_cast<std::size_t>(follows[index])].position;
                    world[index].rotation = world[static_cast<std::size_t>(follows[index])].rotation;
                } else if (parent >= 0 && static_cast<std::size_t>(parent) < count) {
                    if (!done[static_cast<std::size_t>(parent)]) {
                        continue;
                    }
                    const WorldTransform& above = world[static_cast<std::size_t>(parent)];
                    world[index].position = above.position + Math::RotateVector(above.rotation, skeleton.bones[index].position);
                    world[index].rotation = Normalized(Math::Multiply(above.rotation, skeleton.bones[index].rotation));
                } else {
                    world[index] = conformedRest[index];
                }
                world[index].scale = 1.0F;
                done[index] = true;
                progress = true;
            }
        }
        const RetargetSkeleton local = SkeletonFromWorld(skeleton, world);
        for (std::size_t index = 0U; index < count; ++index) {
            Asset::AnimationAssetSample sample;
            sample.timeSeconds = time;
            sample.pose.position = local.bones[index].position;
            sample.pose.rotation = local.bones[index].rotation;
            tracks[index].samples.push_back(sample);
        }
    }
    for (Asset::AnimationAssetBoneTrack& track : tracks) {
        const bool still = std::ranges::all_of(track.samples, [&track](const Asset::AnimationAssetSample& sample) {
            return SamePose(sample.pose, track.samples.front().pose);
        });
        if (still) {
            track.samples.resize(1U);
        }
    }
    Asset::AnimationClipAsset result;
    result.clipId = clip.clipId;
    result.durationSeconds = clip.durationSeconds;
    result.samples = clip.samples;
    result.events = clip.events;
    result.bones = std::move(tracks);
    if (!Asset::IsAnimationClipAssetValid(result)) {
        SetError(error, "the conformed clip is not valid (too many bones or a non-finite pose)");
        return std::nullopt;
    }
    return result;
}

} // namespace UVE::Retarget
