// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/animation_player_component_uve.h"
#include "uve/nodes/3d/animation_player_uve.h"
#include "uve/nodes/3d/skeleton_3d_uve.h"
#include "uve/retarget/retarget_humanoid_uve.h"
#include "uve/retarget/retarget_playback_uve.h"

namespace UVE::Scene {
namespace {

using Math::Vector3UVE;

/// A character conformed to the humanoid, `scale` times as big in every bone length.
[[nodiscard]] Skeleton3DNodeComponentUVE MakeCharacterUVE(const float scale) {
    const Retarget::HumanoidReferenceUVE& reference = Retarget::GetHumanoidReferenceUVE();
    Skeleton3DNodeComponentUVE skeleton;
    for (const Retarget::RetargetBoneUVE& bone : reference.skeleton.bones) {
        SkeletonBoneUVE out;
        out.name = bone.name;
        out.parentIndex = bone.parent;
        out.localPosition = bone.position * scale;
        out.localRotation = bone.rotation;
        skeleton.bones.push_back(std::move(out));
    }
    return skeleton;
}

/// A conformed clip made on the reference-sized rig: the hips travel forward, an upper arm turns,
/// and every track carries the rig's own translation (which is what must not stretch a bigger
/// or smaller character).
[[nodiscard]] Asset::AnimationClipAssetUVE MakeConformedClipUVE(const bool conformed) {
    const Retarget::HumanoidReferenceUVE& reference = Retarget::GetHumanoidReferenceUVE();
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "walk";
    clip.durationSeconds = 1.0;
    clip.conformed = conformed;
    for (const Retarget::RetargetBoneUVE& bone : reference.skeleton.bones) {
        clip.rest.push_back(Asset::AnimationAssetRestBoneUVE{bone.name, bone.parent, bone.position, bone.rotation,
                                                             Vector3UVE{1.0F, 1.0F, 1.0F}});
    }
    for (const char* name : {"Hips", "UpperArm_L", "ForeArm_L", "IKFoot_L", "Root"}) {
        const std::int32_t index = Retarget::FindBoneUVE(reference.skeleton, name);
        const Retarget::RetargetBoneUVE& bone = reference.skeleton.bones[static_cast<std::size_t>(index)];
        Asset::AnimationAssetBoneTrackUVE track;
        track.bone = name;
        for (int frame = 0; frame < 2; ++frame) {
            Asset::AnimationAssetSampleUVE sample;
            sample.timeSeconds = static_cast<double>(frame);
            sample.pose.position = bone.position;
            sample.pose.rotation = bone.rotation;
            if (std::string{name} == "Hips") {
                sample.pose.position = sample.pose.position + Vector3UVE{0.0F, 0.0F, 0.5F * static_cast<float>(frame)};
            }
            if (std::string{name} == "UpperArm_L") {
                Math::QuaternionUVE turn{};
                EXPECT_TRUE(Math::TryMakeAxisAngleUVE({0.0F, 0.0F, 1.0F}, 0.5F * static_cast<float>(frame), turn));
                sample.pose.rotation = Math::MultiplyUVE(bone.rotation, turn);
            }
            track.samples.push_back(sample);
        }
        clip.bones.push_back(std::move(track));
    }
    return clip;
}

[[nodiscard]] const SkeletonBonePoseUVE& PoseOfUVE(const Skeleton3DNodeComponentUVE& skeleton, const char* name) {
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        if (skeleton.bones[index].name == name) {
            return skeleton.pose[index];
        }
    }
    ADD_FAILURE() << name;
    return skeleton.pose.front();
}

[[nodiscard]] Skeleton3DNodeComponentUVE PlayUVE(Skeleton3DNodeComponentUVE skeleton, const Asset::AnimationClipAssetUVE& clip) {
    AnimationPlayerComponentUVE player;
    player.loopMode = AnimationLoopModeUVE::Once;
    player.isPlaying = true;
    EXPECT_TRUE(StepSkeletalAnimationPlayerUVE(player, clip, 1.0F, skeleton));
    return skeleton;
}

TEST(ConformedPlaybackUVETest, ABiggerCharacterKeepsItsOwnLimbsAndScalesOnlyWhatPlacesTheBody) {
    const Asset::AnimationClipAssetUVE clip = MakeConformedClipUVE(true);
    const Skeleton3DNodeComponentUVE character = MakeCharacterUVE(1.2F);
    const Skeleton3DNodeComponentUVE played = PlayUVE(character, clip);
    const Retarget::HumanoidReferenceUVE& reference = Retarget::GetHumanoidReferenceUVE();
    const auto restOf = [&](const char* name) {
        return reference.skeleton.bones[static_cast<std::size_t>(Retarget::FindBoneUVE(reference.skeleton, name))].position;
    };

    // The hips travelled 0.5 forward in the clip; on a 1.2 times taller character, 0.6.
    const SkeletonBonePoseUVE& hips = PoseOfUVE(played, "Hips");
    EXPECT_NEAR(hips.position.z, (restOf("Hips").z + 0.5F) * 1.2F, 1e-3F);
    EXPECT_NEAR(hips.position.y, restOf("Hips").y * 1.2F, 1e-3F);
    // The IK target follows the body's scale.
    EXPECT_NEAR(PoseOfUVE(played, "IKFoot_L").position.x, restOf("IKFoot_L").x * 1.2F, 1e-3F);
    // A limb keeps the character's own length: the clip's translation for it is ignored.
    const Vector3UVE ownForeArm = restOf("ForeArm_L") * 1.2F;
    const Vector3UVE foreArm = PoseOfUVE(played, "ForeArm_L").position;
    EXPECT_NEAR(foreArm.x, ownForeArm.x, 1e-5F);
    EXPECT_NEAR(foreArm.y, ownForeArm.y, 1e-5F);
    EXPECT_NEAR(foreArm.z, ownForeArm.z, 1e-5F);
    // Rotation still comes from the clip.
    const Math::QuaternionUVE& turned = PoseOfUVE(played, "UpperArm_L").rotation;
    const Math::QuaternionUVE& rest = reference.skeleton.bones[static_cast<std::size_t>(Retarget::FindBoneUVE(reference.skeleton, "UpperArm_L"))].rotation;
    EXPECT_GT(std::abs(turned.x - rest.x) + std::abs(turned.y - rest.y) + std::abs(turned.z - rest.z) + std::abs(turned.w - rest.w), 0.1F);
}

TEST(ConformedPlaybackUVETest, ACharacterTheSizeOfTheClipsRigIsPlayedAsAuthored) {
    const Asset::AnimationClipAssetUVE clip = MakeConformedClipUVE(true);
    const Skeleton3DNodeComponentUVE played = PlayUVE(MakeCharacterUVE(1.0F), clip);
    const Retarget::HumanoidReferenceUVE& reference = Retarget::GetHumanoidReferenceUVE();
    const Vector3UVE hipsRest = reference.skeleton.bones[static_cast<std::size_t>(Retarget::FindBoneUVE(reference.skeleton, "Hips"))].position;
    EXPECT_NEAR(PoseOfUVE(played, "Hips").position.z, hipsRest.z + 0.5F, 1e-3F);
}

TEST(ConformedPlaybackUVETest, AClipThatIsNotConformedIsPlayedExactlyAsBefore) {
    const Asset::AnimationClipAssetUVE clip = MakeConformedClipUVE(false);
    const Skeleton3DNodeComponentUVE played = PlayUVE(MakeCharacterUVE(1.2F), clip);
    const Retarget::HumanoidReferenceUVE& reference = Retarget::GetHumanoidReferenceUVE();
    const Vector3UVE foreArmRest = reference.skeleton.bones[static_cast<std::size_t>(Retarget::FindBoneUVE(reference.skeleton, "ForeArm_L"))].position;
    // The clip's own translation, unscaled, as it always was.
    EXPECT_NEAR(PoseOfUVE(played, "ForeArm_L").position.x, foreArmRest.x, 1e-5F);
    EXPECT_NEAR(PoseOfUVE(played, "Hips").position.z, reference.skeleton.bones[static_cast<std::size_t>(Retarget::FindBoneUVE(reference.skeleton, "Hips"))].position.z + 0.5F, 1e-3F);
}

TEST(ConformedPlaybackUVETest, ScrubbingUsesTheSameRuleAsPlaying) {
    const Asset::AnimationClipAssetUVE clip = MakeConformedClipUVE(true);
    Skeleton3DNodeComponentUVE scrubbed = MakeCharacterUVE(1.2F);
    ASSERT_TRUE(PoseSkeletonAtTimeUVE(clip, 1.0, scrubbed));
    const Skeleton3DNodeComponentUVE played = PlayUVE(MakeCharacterUVE(1.2F), clip);
    for (const char* name : {"Hips", "ForeArm_L", "IKFoot_L", "UpperArm_L"}) {
        const Vector3UVE a = PoseOfUVE(scrubbed, name).position;
        const Vector3UVE b = PoseOfUVE(played, name).position;
        EXPECT_NEAR(a.x, b.x, 1e-4F) << name;
        EXPECT_NEAR(a.y, b.y, 1e-4F) << name;
        EXPECT_NEAR(a.z, b.z, 1e-4F) << name;
    }
}

TEST(ConformedPlaybackUVETest, HipsHeightIsMeasuredAboveTheLowestFoot) {
    const Skeleton3DNodeComponentUVE character = MakeCharacterUVE(1.0F);
    std::vector<Retarget::RestBoneViewUVE> view;
    for (const SkeletonBoneUVE& bone : character.bones) {
        view.push_back(Retarget::RestBoneViewUVE{bone.name, bone.parentIndex, bone.localPosition, bone.localRotation});
    }
    EXPECT_NEAR(Retarget::ConformedHipsHeightUVE(view), Retarget::GetHumanoidReferenceUVE().hipsHeight, 1e-3F);
    EXPECT_FLOAT_EQ(Retarget::ConformedHipsHeightUVE({}), 0.0F);
    EXPECT_TRUE(Retarget::ConformedPlaybackUVE::DrivesTranslationUVE("Hips"));
    EXPECT_TRUE(Retarget::ConformedPlaybackUVE::DrivesTranslationUVE("IKHand_R"));
    EXPECT_TRUE(Retarget::ConformedPlaybackUVE::DrivesTranslationUVE("Root"));
    EXPECT_FALSE(Retarget::ConformedPlaybackUVE::DrivesTranslationUVE("ForeArm_L"));
    EXPECT_FALSE(Retarget::ConformedPlaybackUVE::DrivesTranslationUVE("Prop_R"));
}

} // namespace
} // namespace UVE::Scene
