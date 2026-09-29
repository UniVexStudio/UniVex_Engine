// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/mesh_asset_uve.h"
#include "uve/retarget/retarget_humanoid_uve.h"

namespace UVE::Retarget::TestRigUVE {

using Math::QuaternionUVE;
using Math::Vector3UVE;

[[nodiscard]] inline QuaternionUVE AxisAngle(const Vector3UVE& axis, const float radians) {
    QuaternionUVE out{};
    EXPECT_TRUE(Math::TryMakeAxisAngleUVE(axis, radians, out));
    return out;
}

/// An auto-rigger's character in a T-pose, 1.1 times the humanoid's size: fewer bones (no root,
/// three spine bones, one neck bone, no twists, metacarpals or IK), arms level, and every bone
/// with a frame of its own that has nothing to do with the humanoid's.
struct TPoseRigUVE final {
    RetargetSkeletonUVE skeleton;
    std::vector<WorldTransformUVE> world;

    TPoseRigUVE() {
        const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
        const std::vector<WorldTransformUVE> referenceWorld = ComputeWorldTransformsUVE(reference.skeleton);
        const auto at = [&](const std::string& name) {
            return referenceWorld[static_cast<std::size_t>(FindBoneUVE(reference.skeleton, name))].position;
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
        int turn = 0;
        for (const auto& [name, parent, on] : bones) {
            Vector3UVE position = at(on);
            // Raise the arms from the humanoid's 45 degrees down to level, about the shoulder.
            const bool left = name.starts_with("Left");
            const bool arm = name.find("Arm") != std::string::npos || name.find("Hand") != std::string::npos;
            if (arm) {
                const Vector3UVE shoulder = at(left ? "UpperArm_L" : "UpperArm_R");
                position = shoulder + Math::RotateVectorUVE(AxisAngle({0.0F, 0.0F, 1.0F}, left ? 0.7853982F : -0.7853982F),
                                                            position - shoulder);
            }
            WorldTransformUVE transform;
            transform.position = position * 1.1F;
            transform.rotation = AxisAngle(Math::NormalizeUVE(Vector3UVE{0.3F, 1.0F, static_cast<float>(turn % 3)}),
                                           0.4F * static_cast<float>(turn));
            ++turn;
            RetargetBoneUVE bone;
            bone.name = name;
            bone.parent = parent.empty() ? -1 : FindBoneUVE(skeleton, parent);
            skeleton.bones.push_back(bone);
            world.push_back(transform);
        }
        skeleton = SkeletonFromWorldUVE(skeleton, world);
    }

    [[nodiscard]] Vector3UVE At(const std::string& name) const {
        return world[static_cast<std::size_t>(FindBoneUVE(skeleton, name))].position;
    }
};

/// The T-pose rig as a skinned mesh: one joint per bone (inverse binds from its frames) and one
/// vertex half way along the left upper arm, bound to it.
[[nodiscard]] inline Asset::MeshAssetUVE MakeMeshUVE(const TPoseRigUVE& rig) {
    Asset::MeshAssetUVE mesh;
    for (std::size_t index = 0U; index < rig.skeleton.bones.size(); ++index) {
        Asset::MeshJointUVE joint;
        joint.name = rig.skeleton.bones[index].name;
        joint.parentIndex = rig.skeleton.bones[index].parent < 0 ? Asset::kInvalidJointParentUVE
                                                                 : static_cast<std::uint32_t>(rig.skeleton.bones[index].parent);
        const QuaternionUVE inverse = [&] {
            QuaternionUVE out{};
            EXPECT_TRUE(Math::TryInverseUVE(rig.world[index].rotation, out));
            return out;
        }();
        joint.inverseBindMatrix = Math::Matrix4x4UVE::ComposeTrsUVE(-Math::RotateVectorUVE(inverse, rig.world[index].position), inverse,
                                                                    {1.0F, 1.0F, 1.0F});
        mesh.joints.push_back(joint);
    }
    Asset::MeshVertexUVE vertex;
    vertex.position = (rig.At("LeftArm") + rig.At("LeftForeArm")) * 0.5F;
    vertex.normal = {0.0F, 1.0F, 0.0F};
    mesh.vertices.push_back(vertex);
    mesh.indices = {0U, 0U, 0U};
    Asset::MeshSkinningInfluenceUVE influence;
    influence.joints[0] = static_cast<std::uint32_t>(FindBoneUVE(rig.skeleton, "LeftArm"));
    influence.weights[0] = 1.0F;
    mesh.skinningInfluences.push_back(influence);
    return mesh;
}

} // namespace UVE::Retarget::TestRigUVE
