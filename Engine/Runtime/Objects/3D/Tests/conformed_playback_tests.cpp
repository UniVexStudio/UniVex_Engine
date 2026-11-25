// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "animation_clip_asset.h"
#include "animation_sequencer_component.h"
#include "animation_sequencer.h"
#include "skeleton_3d.h"
#include "retarget_humanoid.h"
#include "retarget_playback.h"

namespace UVE::Scene {
namespace {

using Math::Vector3;

/// A character conformed to the humanoid, `scale` times as big in every bone length.
[[nodiscard]] Skeleton3DComponent MakeCharacter(const float scale) {
    const Retarget::HumanoidReference& reference = Retarget::GetHumanoidReference();
    Skeleton3DComponent skeleton;
    for (const Retarget::RetargetBone& bone : reference.skeleton.bones) {
        SkeletonBone out;
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
[[nodiscard]] Asset::AnimationClipAsset MakeConformedClip(const bool conformed) {
    const Retarget::HumanoidReference& reference = Retarget::GetHumanoidReference();
    Asset::AnimationClipAsset clip;
    clip.clipId = "walk";
    clip.durationSeconds = 1.0;
    clip.conformed = conformed;
    for (const Retarget::RetargetBone& bone : reference.skeleton.bones) {
        clip.rest.push_back(Asset::AnimationAssetRestBone{bone.name, bone.parent, bone.position, bone.rotation,
                                                             Vector3{1.0F, 1.0F, 1.0F}});
    }
    for (const char* name : {"Hips", "UpperArm_L", "ForeArm_L", "IKFoot_L", "Root"}) {
        const std::int32_t index = Retarget::FindBone(reference.skeleton, name);
        const Retarget::RetargetBone& bone = reference.skeleton.bones[static_cast<std::size_t>(index)];
        Asset::AnimationAssetBoneTrack track;
        track.bone = name;
        for (int frame = 0; frame < 2; ++frame) {
            Asset::AnimationAssetSample sample;
            sample.timeSeconds = static_cast<double>(frame);
            sample.pose.position = bone.position;
            sample.pose.rotation = bone.rotation;
            if (std::string{name} == "Hips") {
                sample.pose.position = sample.pose.position + Vector3{0.0F, 0.0F, 0.5F * static_cast<float>(frame)};
            }
            if (std::string{name} == "UpperArm_L") {
                Math::Quaternion turn{};
                EXPECT_TRUE(Math::TryMakeAxisAngle({0.0F, 0.0F, 1.0F}, 0.5F * static_cast<float>(frame), turn));
                sample.pose.rotation = Math::Multiply(bone.rotation, turn);
            }
            track.samples.push_back(sample);
        }
        clip.bones.push_back(std::move(track));
    }
    return clip;
}

[[nodiscard]] const SkeletonBonePose& PoseOf(const Skeleton3DComponent& skeleton, const char* name) {
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        if (skeleton.bones[index].name == name) {
            return skeleton.pose[index];
        }
    }
    ADD_FAILURE() << name;
    return skeleton.pose.front();
}

[[nodiscard]] Skeleton3DComponent Play(Skeleton3DComponent skeleton, const Asset::AnimationClipAsset& clip) {
    AnimationSequencerComponent player;
    player.loopMode = AnimationLoopMode::Once;
    player.isPlaying = true;
    EXPECT_TRUE(StepSkeletalAnimationSequencer(player, clip, 1.0F, skeleton));
    return skeleton;
}

TEST(ConformedPlaybackTest, ABiggerCharacterKeepsItsOwnLimbsAndScalesOnlyWhatPlacesTheBody) {
    const Asset::AnimationClipAsset clip = MakeConformedClip(true);
    const Skeleton3DComponent character = MakeCharacter(1.2F);
    const Skeleton3DComponent played = Play(character, clip);
    const Retarget::HumanoidReference& reference = Retarget::GetHumanoidReference();
    const auto restOf = [&](const char* name) {
        return reference.skeleton.bones[static_cast<std::size_t>(Retarget::FindBone(reference.skeleton, name))].position;
    };

    // The hips travelled 0.5 forward in the clip; on a 1.2 times taller character, 0.6.
    const SkeletonBonePose& hips = PoseOf(played, "Hips");
    EXPECT_NEAR(hips.position.z, (restOf("Hips").z + 0.5F) * 1.2F, 1e-3F);
    EXPECT_NEAR(hips.position.y, restOf("Hips").y * 1.2F, 1e-3F);
    // The IK target follows the body's scale.
    EXPECT_NEAR(PoseOf(played, "IKFoot_L").position.x, restOf("IKFoot_L").x * 1.2F, 1e-3F);
    // A limb keeps the character's own length: the clip's translation for it is ignored.
    const Vector3 ownForeArm = restOf("ForeArm_L") * 1.2F;
    const Vector3 foreArm = PoseOf(played, "ForeArm_L").position;
    EXPECT_NEAR(foreArm.x, ownForeArm.x, 1e-5F);
    EXPECT_NEAR(foreArm.y, ownForeArm.y, 1e-5F);
    EXPECT_NEAR(foreArm.z, ownForeArm.z, 1e-5F);
    // Rotation still comes from the clip.
    const Math::Quaternion& turned = PoseOf(played, "UpperArm_L").rotation;
    const Math::Quaternion& rest = reference.skeleton.bones[static_cast<std::size_t>(Retarget::FindBone(reference.skeleton, "UpperArm_L"))].rotation;
    EXPECT_GT(std::abs(turned.x - rest.x) + std::abs(turned.y - rest.y) + std::abs(turned.z - rest.z) + std::abs(turned.w - rest.w), 0.1F);
}

TEST(ConformedPlaybackTest, ACharacterTheSizeOfTheClipsRigIsPlayedAsAuthored) {
    const Asset::AnimationClipAsset clip = MakeConformedClip(true);
    const Skeleton3DComponent played = Play(MakeCharacter(1.0F), clip);
    const Retarget::HumanoidReference& reference = Retarget::GetHumanoidReference();
    const Vector3 hipsRest = reference.skeleton.bones[static_cast<std::size_t>(Retarget::FindBone(reference.skeleton, "Hips"))].position;
    EXPECT_NEAR(PoseOf(played, "Hips").position.z, hipsRest.z + 0.5F, 1e-3F);
}

TEST(ConformedPlaybackTest, AClipThatIsNotConformedIsPlayedExactlyAsBefore) {
    const Asset::AnimationClipAsset clip = MakeConformedClip(false);
    const Skeleton3DComponent played = Play(MakeCharacter(1.2F), clip);
    const Retarget::HumanoidReference& reference = Retarget::GetHumanoidReference();
    const Vector3 foreArmRest = reference.skeleton.bones[static_cast<std::size_t>(Retarget::FindBone(reference.skeleton, "ForeArm_L"))].position;
    // The clip's own translation, unscaled, as it always was.
    EXPECT_NEAR(PoseOf(played, "ForeArm_L").position.x, foreArmRest.x, 1e-5F);
    EXPECT_NEAR(PoseOf(played, "Hips").position.z, reference.skeleton.bones[static_cast<std::size_t>(Retarget::FindBone(reference.skeleton, "Hips"))].position.z + 0.5F, 1e-3F);
}

TEST(ConformedPlaybackTest, ScrubbingUsesTheSameRuleAsPlaying) {
    const Asset::AnimationClipAsset clip = MakeConformedClip(true);
    Skeleton3DComponent scrubbed = MakeCharacter(1.2F);
    ASSERT_TRUE(PoseSkeletonAtTime(clip, 1.0, scrubbed));
    const Skeleton3DComponent played = Play(MakeCharacter(1.2F), clip);
    for (const char* name : {"Hips", "ForeArm_L", "IKFoot_L", "UpperArm_L"}) {
        const Vector3 a = PoseOf(scrubbed, name).position;
        const Vector3 b = PoseOf(played, name).position;
        EXPECT_NEAR(a.x, b.x, 1e-4F) << name;
        EXPECT_NEAR(a.y, b.y, 1e-4F) << name;
        EXPECT_NEAR(a.z, b.z, 1e-4F) << name;
    }
}

TEST(ConformedPlaybackTest, HipsHeightIsMeasuredAboveTheLowestFoot) {
    const Skeleton3DComponent character = MakeCharacter(1.0F);
    std::vector<Retarget::RestBoneView> view;
    for (const SkeletonBone& bone : character.bones) {
        view.push_back(Retarget::RestBoneView{bone.name, bone.parentIndex, bone.localPosition, bone.localRotation});
    }
    EXPECT_NEAR(Retarget::ConformedHipsHeight(view), Retarget::GetHumanoidReference().hipsHeight, 1e-3F);
    EXPECT_FLOAT_EQ(Retarget::ConformedHipsHeight({}), 0.0F);
    EXPECT_TRUE(Retarget::ConformedPlayback::DrivesTranslation("Hips"));
    EXPECT_TRUE(Retarget::ConformedPlayback::DrivesTranslation("IKHand_R"));
    EXPECT_TRUE(Retarget::ConformedPlayback::DrivesTranslation("Root"));
    EXPECT_FALSE(Retarget::ConformedPlayback::DrivesTranslation("ForeArm_L"));
    EXPECT_FALSE(Retarget::ConformedPlayback::DrivesTranslation("Prop_R"));
}

} // namespace
} // namespace UVE::Scene
