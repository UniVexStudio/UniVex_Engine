// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "animation_clip_asset.h"
#include "mesh_asset.h"
#include "retarget_humanoid.h"

namespace UVE::Retarget::TestRig {

using Math::Quaternion;
using Math::Vector3;

[[nodiscard]] inline Quaternion AxisAngle(const Vector3& axis, const float radians) {
    Quaternion out{};
    EXPECT_TRUE(Math::TryMakeAxisAngle(axis, radians, out));
    return out;
}

/// An auto-rigger's character in a T-pose, 1.1 times the humanoid's size: fewer bones (no root,
/// three spine bones, one neck bone, no twists, metacarpals or IK), arms level, and every bone
/// with a frame of its own that has nothing to do with the humanoid's.
struct TPoseRig final {
    RetargetSkeleton skeleton;
    std::vector<WorldTransform> world;

    explicit TPoseRig(const Quaternion& turn = Quaternion{}) {
        const HumanoidReference& reference = GetHumanoidReference();
        const std::vector<WorldTransform> referenceWorld = ComputeWorldTransforms(reference.skeleton);
        const auto at = [&](const std::string& name) {
            return referenceWorld[static_cast<std::size_t>(FindBone(reference.skeleton, name))].position;
        };
        std::vector<std::tuple<std::string, std::string, std::string>> bones{
            {"Hips", "", "Hips"},          {"Spine", "Hips", "Spine1"},     {"Spine1", "Spine", "Spine3"},
            {"Spine2", "Spine1", "Spine5"}, {"Neck", "Spine2", "Neck1"},     {"Head", "Neck", "Head"},
            {"HeadTop_End", "Head", "Head"},
        };
        for (const std::string side : {"Left", "Right"}) {
            const std::string r = side == "Left" ? "_L" : "_R";
            bones.emplace_back(side + "Shoulder", "Spine2", "Clavicle" + r);
            bones.emplace_back(side + "Arm", side + "Shoulder", "UpperArm" + r);
            bones.emplace_back(side + "ForeArm", side + "Arm", "ForeArm" + r);
            bones.emplace_back(side + "Hand", side + "ForeArm", "Hand" + r);
            bones.emplace_back(side + "HandIndex1", side + "Hand", "Index1" + r);
            bones.emplace_back(side + "HandIndex2", side + "HandIndex1", "Index2" + r);
            bones.emplace_back(side + "HandPinky1", side + "Hand", "Pinky1" + r);
            bones.emplace_back(side + "UpLeg", "Hips", "Thigh" + r);
            bones.emplace_back(side + "Leg", side + "UpLeg", "Shin" + r);
            bones.emplace_back(side + "Foot", side + "Leg", "Foot" + r);
            bones.emplace_back(side + "ToeBase", side + "Foot", "Toe" + r);
        }
        int step = 0;
        for (const auto& [name, parent, on] : bones) {
            Vector3 position = at(on);
            // Raise the arms from the humanoid's 45 degrees down to level, about the shoulder.
            const bool left = name.starts_with("Left");
            const bool arm = name.find("Arm") != std::string::npos || name.find("Hand") != std::string::npos;
            if (arm) {
                const Vector3 shoulder = at(left ? "UpperArm_L" : "UpperArm_R");
                position = shoulder + Math::RotateVector(AxisAngle({0.0F, 0.0F, 1.0F}, left ? 0.7853982F : -0.7853982F),
                                                            position - shoulder);
            }
            WorldTransform transform;
            transform.position = Math::RotateVector(turn, position * 1.1F);
            transform.rotation = AxisAngle(Math::Normalize(Vector3{0.3F, 1.0F, static_cast<float>(step % 3)}),
                                           0.4F * static_cast<float>(step));
            transform.rotation = Math::Multiply(turn, transform.rotation);
            ++step;
            RetargetBone bone;
            bone.name = name;
            bone.parent = parent.empty() ? -1 : FindBone(skeleton, parent);
            skeleton.bones.push_back(bone);
            world.push_back(transform);
        }
        skeleton = SkeletonFromWorld(skeleton, world);
    }

    [[nodiscard]] Vector3 At(const std::string& name) const {
        return world[static_cast<std::size_t>(FindBone(skeleton, name))].position;
    }
};

/// A clip for the rig: the hips travel forward and the left forearm bends, every bone listed with
/// the rig as its rest skeleton (three frames, one second apart).
[[nodiscard]] inline Asset::AnimationClipAsset MakeClip(const TPoseRig& rig, const std::string& id) {
    Asset::AnimationClipAsset clip;
    clip.clipId = id;
    clip.durationSeconds = 2.0;
    for (const RetargetBone& bone : rig.skeleton.bones) {
        clip.rest.push_back(Asset::AnimationAssetRestBone{bone.name, bone.parent, bone.position, bone.rotation,
                                                             Vector3{1.0F, 1.0F, 1.0F}});
        Asset::AnimationAssetBoneTrack track;
        track.bone = bone.name;
        for (int frame = 0; frame < 3; ++frame) {
            Asset::AnimationAssetSample sample;
            sample.timeSeconds = static_cast<double>(frame);
            sample.pose.position = bone.position;
            sample.pose.rotation = bone.rotation;
            if (bone.name == "Hips") {
                sample.pose.position = sample.pose.position + Vector3{0.0F, -0.05F, 0.4F} * static_cast<float>(frame);
            }
            if (bone.name == "LeftForeArm") {
                sample.pose.rotation = Math::Multiply(bone.rotation, AxisAngle({0.0F, 1.0F, 0.0F}, 0.6F * static_cast<float>(frame)));
            }
            track.samples.push_back(sample);
        }
        clip.bones.push_back(std::move(track));
    }
    return clip;
}

/// The T-pose rig as a skinned mesh: one joint per bone (inverse binds from its frames) and one
/// vertex half way along the left upper arm, bound to it.
[[nodiscard]] inline Asset::MeshAsset MakeMesh(const TPoseRig& rig) {
    Asset::MeshAsset mesh;
    for (std::size_t index = 0U; index < rig.skeleton.bones.size(); ++index) {
        Asset::MeshJoint joint;
        joint.name = rig.skeleton.bones[index].name;
        joint.parentIndex = rig.skeleton.bones[index].parent < 0 ? Asset::kInvalidJointParent
                                                                 : static_cast<std::uint32_t>(rig.skeleton.bones[index].parent);
        const Quaternion inverse = [&] {
            Quaternion out{};
            EXPECT_TRUE(Math::TryInverse(rig.world[index].rotation, out));
            return out;
        }();
        joint.inverseBindMatrix = Math::Matrix4x4::ComposeTrs(-Math::RotateVector(inverse, rig.world[index].position), inverse,
                                                                    {1.0F, 1.0F, 1.0F});
        mesh.joints.push_back(joint);
    }
    Asset::MeshVertex vertex;
    vertex.position = (rig.At("LeftArm") + rig.At("LeftForeArm")) * 0.5F;
    vertex.normal = {0.0F, 1.0F, 0.0F};
    mesh.vertices.push_back(vertex);
    mesh.indices = {0U, 0U, 0U};
    Asset::MeshSkinningInfluence influence;
    influence.joints[0] = static_cast<std::uint32_t>(FindBone(rig.skeleton, "LeftArm"));
    influence.weights[0] = 1.0F;
    mesh.skinningInfluences.push_back(influence);
    return mesh;
}

} // namespace UVE::Retarget::TestRig
