// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "cinematic.h"

#include <limits>

#include <gtest/gtest.h>

namespace UVE::Scene::Tests {

namespace {

CinematicComponent MakeCinematic() {
    CinematicComponent cinematic;
    cinematic.durationSeconds = 10.0;
    return cinematic;
}

Entity MakeCamera(std::uint32_t id) {
    Entity camera;
    camera.index = id;
    camera.generation = 1U;
    return camera;
}

} // namespace

TEST(CinematicTest, Play_FiresTimeZeroKeysAndRestarts) {
    CinematicComponent cinematic = MakeCinematic();
    ASSERT_TRUE(AddCinematicEvent(cinematic, 0.0, "open"));
    ASSERT_TRUE(AddCinematicEvent(cinematic, 5.0, "later"));
    const CinematicPlayResult opened = PlayCinematic(cinematic);
    EXPECT_EQ(opened.firedEventIds, (std::vector<std::string>{"open"}));
    EXPECT_TRUE(opened.animationCues.empty());
    EXPECT_TRUE(opened.audioCues.empty());
    EXPECT_TRUE(cinematic.isPlaying);
    EXPECT_FALSE(cinematic.finished);
    EXPECT_DOUBLE_EQ(cinematic.currentTimeSeconds, 0.0);

    CinematicComponent broken;
    EXPECT_TRUE(PlayCinematic(broken).firedEventIds.empty());
}

TEST(CinematicTest, Step_FiresKeysInPassingOrder) {
    CinematicComponent cinematic = MakeCinematic();
    ASSERT_TRUE(AddCinematicEvent(cinematic, 3.0, "third"));
    ASSERT_TRUE(AddCinematicEvent(cinematic, 1.0, "first"));
    ASSERT_TRUE(AddCinematicEvent(cinematic, 2.0, "second"));
    static_cast<void>(PlayCinematic(cinematic));
    const CinematicStepResult result = StepCinematic(cinematic, 2.5F);
    EXPECT_EQ(result.firedEventIds, (std::vector<std::string>{"first", "second"}));
    EXPECT_DOUBLE_EQ(cinematic.currentTimeSeconds, 2.5);
    EXPECT_TRUE(cinematic.isPlaying);
}

TEST(CinematicTest, Step_OnceParksAtTheEnd) {
    CinematicComponent cinematic = MakeCinematic();
    ASSERT_TRUE(AddCinematicEvent(cinematic, 9.0, "near-end"));
    static_cast<void>(PlayCinematic(cinematic));
    const CinematicStepResult last = StepCinematic(cinematic, 50.0F);
    EXPECT_EQ(last.firedEventIds, (std::vector<std::string>{"near-end"}));
    EXPECT_TRUE(last.justFinished);
    EXPECT_FALSE(cinematic.isPlaying);
    EXPECT_TRUE(cinematic.finished);
    EXPECT_DOUBLE_EQ(cinematic.currentTimeSeconds, 10.0);
    EXPECT_TRUE(StepCinematic(cinematic, 1.0F).firedEventIds.empty());
}

TEST(CinematicTest, Step_LoopWrapsAndRefiresInOrder) {
    CinematicComponent cinematic = MakeCinematic();
    cinematic.durationSeconds = 2.0;
    cinematic.loopMode = CinematicLoopMode::Loop;
    ASSERT_TRUE(AddCinematicEvent(cinematic, 0.5, "early"));
    ASSERT_TRUE(AddCinematicEvent(cinematic, 1.5, "late"));
    static_cast<void>(PlayCinematic(cinematic));
    const CinematicStepResult first = StepCinematic(cinematic, 1.2F);
    EXPECT_EQ(first.firedEventIds, (std::vector<std::string>{"early"}));
    const CinematicStepResult wrapped = StepCinematic(cinematic, 1.6F);
    EXPECT_EQ(wrapped.firedEventIds, (std::vector<std::string>{"late", "early"}));
    EXPECT_NEAR(cinematic.currentTimeSeconds, 0.8, 1e-6);
    EXPECT_TRUE(cinematic.isPlaying);
    EXPECT_FALSE(wrapped.justFinished);
}

TEST(CinematicTest, Step_ResolvesCameraCuts) {
    CinematicComponent cinematic = MakeCinematic();
    const Entity camA = MakeCamera(11U);
    const Entity camB = MakeCamera(22U);
    ASSERT_TRUE(AddCinematicCut(cinematic, 0.0, camA));
    ASSERT_TRUE(AddCinematicCut(cinematic, 5.0, camB));
    static_cast<void>(PlayCinematic(cinematic));
    EXPECT_EQ(StepCinematic(cinematic, 3.0F).activeCamera, camA);
    EXPECT_EQ(StepCinematic(cinematic, 4.0F).activeCamera, camB);

    CinematicComponent noCuts = MakeCinematic();
    static_cast<void>(PlayCinematic(noCuts));
    EXPECT_EQ(StepCinematic(noCuts, 1.0F).activeCamera, kInvalidEntity);
}

TEST(CinematicTest, Add_InsertsSortedAndRejectsBadKeys) {
    CinematicComponent cinematic = MakeCinematic();
    EXPECT_FALSE(AddCinematicEvent(cinematic, -1.0, "x"));
    EXPECT_FALSE(AddCinematicEvent(cinematic, 11.0, "x"));
    EXPECT_FALSE(AddCinematicEvent(cinematic, 1.0, ""));
    EXPECT_FALSE(AddCinematicEvent(cinematic, 1.0, std::string(65U, 'x')));
    EXPECT_FALSE(AddCinematicCut(cinematic, 1.0, kInvalidEntity));
    EXPECT_FALSE(AddCinematicCut(cinematic, 11.0, MakeCamera(1U)));
    ASSERT_TRUE(AddCinematicEvent(cinematic, 5.0, "b"));
    ASSERT_TRUE(AddCinematicEvent(cinematic, 5.0, "c"));
    ASSERT_TRUE(AddCinematicEvent(cinematic, 1.0, "a"));
    ASSERT_EQ(cinematic.events.size(), 3U);
    EXPECT_EQ(cinematic.events[0].eventId, "a");
    EXPECT_EQ(cinematic.events[1].eventId, "b");
    EXPECT_EQ(cinematic.events[2].eventId, "c");
}

TEST(CinematicTest, Remove_RespectsBounds) {
    CinematicComponent cinematic = MakeCinematic();
    ASSERT_TRUE(AddCinematicEvent(cinematic, 1.0, "a"));
    EXPECT_FALSE(RemoveCinematicEvent(cinematic, 1U));
    EXPECT_TRUE(RemoveCinematicEvent(cinematic, 0U));
    EXPECT_FALSE(RemoveCinematicCut(cinematic, 0U));
}

TEST(CinematicTest, Step_NegativeSpeedRewindsWithoutFiring) {
    CinematicComponent cinematic = MakeCinematic();
    cinematic.speed = -1.0F;
    ASSERT_TRUE(AddCinematicEvent(cinematic, 4.0, "mid"));
    ASSERT_TRUE(AddCinematicCut(cinematic, 0.0, MakeCamera(7U)));
    static_cast<void>(PlayCinematic(cinematic));
    cinematic.currentTimeSeconds = 5.0;
    const CinematicStepResult result = StepCinematic(cinematic, 2.0F);
    EXPECT_TRUE(result.firedEventIds.empty());
    EXPECT_DOUBLE_EQ(cinematic.currentTimeSeconds, 3.0);
    EXPECT_EQ(result.activeCamera, MakeCamera(7U));
}

TEST(CinematicTest, Scrub_ResolvesCameraWithoutMoving) {
    CinematicComponent cinematic = MakeCinematic();
    ASSERT_TRUE(AddCinematicCut(cinematic, 2.0, MakeCamera(3U)));
    EXPECT_EQ(ActiveCinematicCamera(cinematic, 1.0), kInvalidEntity);
    EXPECT_EQ(ActiveCinematicCamera(cinematic, 2.0), MakeCamera(3U));
    EXPECT_EQ(ActiveCinematicCamera(cinematic, 9.0), MakeCamera(3U));
    EXPECT_EQ(ActiveCinematicCamera(cinematic, std::numeric_limits<double>::quiet_NaN()),
              kInvalidEntity);
    EXPECT_DOUBLE_EQ(cinematic.currentTimeSeconds, 0.0);
}

TEST(CinematicTest, Validity_RejectsBrokenTimelines) {
    CinematicComponent good = MakeCinematic();
    ASSERT_TRUE(AddCinematicEvent(good, 1.0, "a"));
    EXPECT_TRUE(IsCinematicComponentValid(good));

    CinematicComponent noDuration = good;
    noDuration.durationSeconds = 0.0;
    EXPECT_FALSE(IsCinematicComponentValid(noDuration));

    CinematicComponent badSpeed = good;
    badSpeed.speed = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsCinematicComponentValid(badSpeed));

    CinematicComponent unsorted = good;
    unsorted.events.push_back(CinematicEventKey{0.5, "early-late"});
    EXPECT_FALSE(IsCinematicComponentValid(unsorted));

    CinematicComponent overfull = MakeCinematic();
    for (std::size_t index = 0U; index <= kMaximumCinematicEvents; ++index) {
        overfull.events.push_back(CinematicEventKey{1.0, "e" + std::to_string(index)});
    }
    EXPECT_FALSE(IsCinematicComponentValid(overfull));
}


TEST(CinematicTest, Sample_ClampsToTheFirstAndLastKeys) {
    CinematicComponent cinematic = MakeCinematic();
    ASSERT_TRUE(AddCinematicCameraKey(cinematic, 2.0, Math::Vector3{2.0F, 0.0F, 0.0F},
                                         Math::Quaternion{}));
    ASSERT_TRUE(AddCinematicCameraKey(cinematic, 8.0, Math::Vector3{8.0F, 0.0F, 0.0F},
                                         Math::Quaternion{}));
    const auto before = SampleCinematicCamera(cinematic, 0.0);
    ASSERT_TRUE(before.has_value());
    EXPECT_FLOAT_EQ(before->position.x, 2.0F);
    const auto after = SampleCinematicCamera(cinematic, 10.0);
    ASSERT_TRUE(after.has_value());
    EXPECT_FLOAT_EQ(after->position.x, 8.0F);

    CinematicComponent single = MakeCinematic();
    ASSERT_TRUE(AddCinematicCameraKey(single, 5.0, Math::Vector3{5.0F, 1.0F, 0.0F},
                                         Math::Quaternion{}));
    const auto held = SampleCinematicCamera(single, 9.0);
    ASSERT_TRUE(held.has_value());
    EXPECT_EQ(held->position, Math::Vector3(5.0F, 1.0F, 0.0F));
}

TEST(CinematicTest, Sample_InterpolatesPositionLinearly) {
    CinematicComponent cinematic = MakeCinematic();
    ASSERT_TRUE(AddCinematicCameraKey(cinematic, 0.0, Math::Vector3{0.0F, 0.0F, 0.0F},
                                         Math::Quaternion{}));
    ASSERT_TRUE(AddCinematicCameraKey(cinematic, 10.0, Math::Vector3{10.0F, 0.0F, 0.0F},
                                         Math::Quaternion{}));
    const auto quarter = SampleCinematicCamera(cinematic, 2.5);
    ASSERT_TRUE(quarter.has_value());
    EXPECT_FLOAT_EQ(quarter->position.x, 2.5F);
    EXPECT_FLOAT_EQ(quarter->position.y, 0.0F);
    EXPECT_EQ(quarter->rotation, Math::Quaternion{});
}

TEST(CinematicTest, Sample_SlerpsRotationBetweenKeys) {
    CinematicComponent cinematic = MakeCinematic();
    ASSERT_TRUE(AddCinematicCameraKey(cinematic, 0.0, Math::Vector3{}, Math::Quaternion{}));
    // 90 degrees about Y.
    ASSERT_TRUE(AddCinematicCameraKey(cinematic, 10.0, Math::Vector3{},
                                         Math::Quaternion{0.0F, 0.70710678F, 0.0F, 0.70710678F}));
    const auto half = SampleCinematicCamera(cinematic, 5.0);
    ASSERT_TRUE(half.has_value());
    // 45 degrees about Y: (0, sin 22.5, 0, cos 22.5).
    EXPECT_NEAR(half->rotation.x, 0.0, 1e-5);
    EXPECT_NEAR(half->rotation.y, 0.38268343, 1e-5);
    EXPECT_NEAR(half->rotation.z, 0.0, 1e-5);
    EXPECT_NEAR(half->rotation.w, 0.92387953, 1e-5);
}

TEST(CinematicTest, Sample_EmptyTrackAndBadTimeReturnNullopt) {
    const CinematicComponent bare = MakeCinematic();
    EXPECT_FALSE(SampleCinematicCamera(bare, 1.0).has_value());
    CinematicComponent cinematic = MakeCinematic();
    ASSERT_TRUE(AddCinematicCameraKey(cinematic, 1.0, Math::Vector3{}, Math::Quaternion{}));
    EXPECT_FALSE(
        SampleCinematicCamera(cinematic, std::numeric_limits<double>::quiet_NaN()).has_value());
}

TEST(CinematicTest, Add_NormalizesRotationAndRejectsBadKeys) {
    CinematicComponent cinematic = MakeCinematic();
    ASSERT_TRUE(AddCinematicCameraKey(cinematic, 1.0, Math::Vector3{},
                                         Math::Quaternion{0.0F, 0.0F, 0.0F, 2.0F}));
    EXPECT_EQ(cinematic.cameraKeys.front().rotation, Math::Quaternion{});
    EXPECT_FALSE(AddCinematicCameraKey(cinematic, 1.0, Math::Vector3{},
                                          Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(AddCinematicCameraKey(
        cinematic, 1.0, Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
        Math::Quaternion{}));
    EXPECT_FALSE(AddCinematicCameraKey(cinematic, -1.0, Math::Vector3{}, Math::Quaternion{}));
    EXPECT_FALSE(AddCinematicCameraKey(cinematic, 11.0, Math::Vector3{}, Math::Quaternion{}));

    CinematicComponent full = MakeCinematic();
    for (std::size_t index = 0U; index < kMaximumCinematicCameraKeys; ++index) {
        ASSERT_TRUE(AddCinematicCameraKey(full, static_cast<double>(index) * 0.1,
                                             Math::Vector3{}, Math::Quaternion{}));
    }
    EXPECT_FALSE(
        AddCinematicCameraKey(full, 9.9, Math::Vector3{}, Math::Quaternion{}));
}

TEST(CinematicTest, RemoveCameraKey_RespectsBounds) {
    CinematicComponent cinematic = MakeCinematic();
    ASSERT_TRUE(AddCinematicCameraKey(cinematic, 1.0, Math::Vector3{}, Math::Quaternion{}));
    EXPECT_FALSE(RemoveCinematicCameraKey(cinematic, 1U));
    EXPECT_TRUE(RemoveCinematicCameraKey(cinematic, 0U));
    EXPECT_TRUE(cinematic.cameraKeys.empty());
}

TEST(CinematicTest, Step_CarriesTheSampledCameraPose) {
    CinematicComponent cinematic = MakeCinematic();
    ASSERT_TRUE(AddCinematicCameraKey(cinematic, 0.0, Math::Vector3{0.0F, 0.0F, 0.0F},
                                         Math::Quaternion{}));
    ASSERT_TRUE(AddCinematicCameraKey(cinematic, 10.0, Math::Vector3{10.0F, 0.0F, 0.0F},
                                         Math::Quaternion{}));
    static_cast<void>(PlayCinematic(cinematic));
    const CinematicStepResult result = StepCinematic(cinematic, 2.5F);
    ASSERT_TRUE(result.cameraPose.has_value());
    EXPECT_FLOAT_EQ(result.cameraPose->position.x, 2.5F);

    CinematicComponent bare = MakeCinematic();
    static_cast<void>(PlayCinematic(bare));
    EXPECT_FALSE(StepCinematic(bare, 1.0F).cameraPose.has_value());
}

TEST(CinematicTest, Write_PosesTransformAndMirrorsEuler) {
    TransformComponent transform;
    CinematicCameraPose pose;
    pose.position = Math::Vector3{1.0F, 2.0F, 3.0F};
    pose.rotation = Math::Quaternion{0.0F, 0.70710678F, 0.0F, 0.70710678F};
    WriteCinematicCameraPose(pose, transform);
    EXPECT_EQ(transform.localPosition, pose.position);
    // The writer re-normalizes, so the stored rotation matches to float dust, not bitwise.
    EXPECT_NEAR(transform.localRotation.x, pose.rotation.x, 1e-6);
    EXPECT_NEAR(transform.localRotation.y, pose.rotation.y, 1e-6);
    EXPECT_NEAR(transform.localRotation.z, pose.rotation.z, 1e-6);
    EXPECT_NEAR(transform.localRotation.w, pose.rotation.w, 1e-6);
    // The mirrored Euler angles rebuild the same rotation: the Inspector shows what is on screen.
    // The quaternion-Euler-quaternion detour costs ~5e-4 here - the same price the animation
    // writer pays every frame, and a fraction of a pixel on screen.
    ASSERT_TRUE(TrySyncRotationFromEuler(transform));
    EXPECT_NEAR(transform.localRotation.x, pose.rotation.x, 1e-3);
    EXPECT_NEAR(transform.localRotation.y, pose.rotation.y, 1e-3);
    EXPECT_NEAR(transform.localRotation.z, pose.rotation.z, 1e-3);
    EXPECT_NEAR(transform.localRotation.w, pose.rotation.w, 1e-3);

    const Math::Quaternion rebuilt = transform.localRotation;
    CinematicCameraPose degenerate;
    degenerate.position = Math::Vector3{9.0F, 9.0F, 9.0F};
    degenerate.rotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F};
    WriteCinematicCameraPose(degenerate, transform);
    EXPECT_EQ(transform.localPosition, degenerate.position);
    EXPECT_EQ(transform.localRotation, rebuilt);
}

TEST(CinematicTest, Validity_RejectsBrokenCameraTracks) {
    CinematicComponent unsorted = MakeCinematic();
    ASSERT_TRUE(AddCinematicCameraKey(unsorted, 5.0, Math::Vector3{}, Math::Quaternion{}));
    unsorted.cameraKeys.push_back(CinematicCameraKey{1.0, Math::Vector3{}, Math::Quaternion{}});
    EXPECT_FALSE(IsCinematicComponentValid(unsorted));

    CinematicComponent denormal = MakeCinematic();
    denormal.cameraKeys.push_back(
        CinematicCameraKey{1.0, Math::Vector3{}, Math::Quaternion{0.0F, 0.0F, 0.0F, 2.0F}});
    EXPECT_FALSE(IsCinematicComponentValid(denormal));

    CinematicComponent badPosition = MakeCinematic();
    badPosition.cameraKeys.push_back(CinematicCameraKey{
        1.0, Math::Vector3{std::numeric_limits<float>::infinity(), 0.0F, 0.0F},
        Math::Quaternion{}});
    EXPECT_FALSE(IsCinematicComponentValid(badPosition));
}


TEST(CinematicTest, Play_FiresTimeZeroAnimationAndAudioCues) {
    CinematicComponent cinematic = MakeCinematic();
    ASSERT_TRUE(AddCinematicAnimationKey(cinematic, 0.0, MakeCamera(5U), Asset::AssetGuid{77U}));
    ASSERT_TRUE(AddCinematicAnimationKey(cinematic, 5.0, MakeCamera(6U), Asset::AssetGuid{}));
    ASSERT_TRUE(AddCinematicAudioKey(cinematic, 0.0, "sfx/open.uvaudio", 0.5F));
    ASSERT_TRUE(AddCinematicAudioKey(cinematic, 5.0, "sfx/later.uvaudio", 1.0F));
    const CinematicPlayResult opened = PlayCinematic(cinematic);
    ASSERT_EQ(opened.animationCues.size(), 1U);
    EXPECT_EQ(opened.animationCues.front().target, MakeCamera(5U));
    EXPECT_EQ(opened.animationCues.front().clip, Asset::AssetGuid{77U});
    ASSERT_EQ(opened.audioCues.size(), 1U);
    EXPECT_EQ(opened.audioCues.front().audioAssetPath, "sfx/open.uvaudio");
    EXPECT_FLOAT_EQ(opened.audioCues.front().volume, 0.5F);
}

TEST(CinematicTest, Step_CollectsAnimationAndAudioCuesInOrder) {
    CinematicComponent cinematic = MakeCinematic();
    ASSERT_TRUE(AddCinematicAnimationKey(cinematic, 3.0, MakeCamera(3U), Asset::AssetGuid{}));
    ASSERT_TRUE(AddCinematicAnimationKey(cinematic, 1.0, MakeCamera(1U), Asset::AssetGuid{9U}));
    ASSERT_TRUE(AddCinematicAudioKey(cinematic, 2.0, "sfx/mid.uvaudio", 0.75F));
    static_cast<void>(PlayCinematic(cinematic));
    const CinematicStepResult result = StepCinematic(cinematic, 2.5F);
    ASSERT_EQ(result.firedAnimationCues.size(), 1U);
    EXPECT_EQ(result.firedAnimationCues.front().target, MakeCamera(1U));
    EXPECT_EQ(result.firedAnimationCues.front().clip, Asset::AssetGuid{9U});
    ASSERT_EQ(result.firedAudioCues.size(), 1U);
    EXPECT_EQ(result.firedAudioCues.front().audioAssetPath, "sfx/mid.uvaudio");
}

TEST(CinematicTest, Add_RejectsBadAnimationAndAudioKeys) {
    CinematicComponent cinematic = MakeCinematic();
    EXPECT_FALSE(AddCinematicAnimationKey(cinematic, 1.0, kInvalidEntity, Asset::AssetGuid{}));
    EXPECT_FALSE(
        AddCinematicAnimationKey(cinematic, 11.0, MakeCamera(1U), Asset::AssetGuid{}));
    EXPECT_FALSE(AddCinematicAudioKey(cinematic, 1.0, "", 1.0F));
    EXPECT_FALSE(AddCinematicAudioKey(cinematic, 1.0, std::string(257U, 'x'), 1.0F));
    EXPECT_FALSE(AddCinematicAudioKey(cinematic, 1.0, "sfx/x.uvaudio", -1.0F));
    EXPECT_FALSE(AddCinematicAudioKey(cinematic, 1.0, "sfx/x.uvaudio",
                                         std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(AddCinematicAudioKey(cinematic, 11.0, "sfx/x.uvaudio", 1.0F));
    ASSERT_TRUE(AddCinematicAudioKey(cinematic, 5.0, "sfx/b.uvaudio", 1.0F));
    ASSERT_TRUE(AddCinematicAudioKey(cinematic, 1.0, "sfx/a.uvaudio", 1.0F));
    EXPECT_EQ(cinematic.audioKeys.front().audioAssetPath, "sfx/a.uvaudio");
}

TEST(CinematicTest, RemoveAnimationAndAudioKey_RespectsBounds) {
    CinematicComponent cinematic = MakeCinematic();
    ASSERT_TRUE(
        AddCinematicAnimationKey(cinematic, 1.0, MakeCamera(1U), Asset::AssetGuid{}));
    ASSERT_TRUE(AddCinematicAudioKey(cinematic, 1.0, "sfx/a.uvaudio", 1.0F));
    EXPECT_FALSE(RemoveCinematicAnimationKey(cinematic, 1U));
    EXPECT_TRUE(RemoveCinematicAnimationKey(cinematic, 0U));
    EXPECT_FALSE(RemoveCinematicAudioKey(cinematic, 1U));
    EXPECT_TRUE(RemoveCinematicAudioKey(cinematic, 0U));
}

TEST(CinematicTest, Validity_RejectsBrokenCueTracks) {
    CinematicComponent badTarget = MakeCinematic();
    badTarget.animationKeys.push_back(CinematicAnimationKey{1.0, kInvalidEntity, Asset::AssetGuid{}});
    EXPECT_FALSE(IsCinematicComponentValid(badTarget));

    CinematicComponent badPath = MakeCinematic();
    badPath.audioKeys.push_back(CinematicAudioKey{1.0, "", 1.0F});
    EXPECT_FALSE(IsCinematicComponentValid(badPath));

    CinematicComponent badVolume = MakeCinematic();
    badVolume.audioKeys.push_back(CinematicAudioKey{1.0, "sfx/a.uvaudio", -0.5F});
    EXPECT_FALSE(IsCinematicComponentValid(badVolume));

    CinematicComponent unsorted = MakeCinematic();
    ASSERT_TRUE(AddCinematicAudioKey(unsorted, 5.0, "sfx/a.uvaudio", 1.0F));
    unsorted.audioKeys.push_back(CinematicAudioKey{1.0, "sfx/b.uvaudio", 1.0F});
    EXPECT_FALSE(IsCinematicComponentValid(unsorted));
}


TEST(CinematicTest, Add_SpatialFormValidatesDistancesAndPosition) {
    CinematicComponent cinematic = MakeCinematic();
    CinematicAudioKey spatial;
    spatial.timeSeconds = 2.0;
    spatial.audioAssetPath = "sfx/door.uvaudio";
    spatial.volume = 0.8F;
    spatial.spatial = true;
    spatial.position = Math::Vector3{1.0F, 2.0F, 3.0F};
    spatial.minDistance = 2.0F;
    spatial.maxDistance = 40.0F;
    ASSERT_TRUE(AddCinematicAudioKey(cinematic, spatial));
    EXPECT_EQ(cinematic.audioKeys.front(), spatial);

    CinematicAudioKey flatMin = spatial;
    flatMin.minDistance = 0.0F;
    EXPECT_FALSE(AddCinematicAudioKey(cinematic, flatMin));

    CinematicAudioKey inverted = spatial;
    inverted.maxDistance = inverted.minDistance;
    EXPECT_FALSE(AddCinematicAudioKey(cinematic, inverted));

    CinematicAudioKey lost = spatial;
    lost.position = Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    EXPECT_FALSE(AddCinematicAudioKey(cinematic, lost));

    // The short form stays 2D with the default band.
    ASSERT_TRUE(AddCinematicAudioKey(cinematic, 4.0, "sfx/ui.uvaudio", 1.0F));
    EXPECT_FALSE(cinematic.audioKeys.back().spatial);
    EXPECT_FLOAT_EQ(cinematic.audioKeys.back().maxDistance, 25.0F);
}

TEST(CinematicTest, Step_CarriesSpatialCueFields) {
    CinematicComponent cinematic = MakeCinematic();
    CinematicAudioKey spatial;
    spatial.timeSeconds = 2.0;
    spatial.audioAssetPath = "sfx/door.uvaudio";
    spatial.volume = 0.8F;
    spatial.spatial = true;
    spatial.position = Math::Vector3{1.0F, 2.0F, 3.0F};
    spatial.minDistance = 2.0F;
    spatial.maxDistance = 40.0F;
    ASSERT_TRUE(AddCinematicAudioKey(cinematic, spatial));
    static_cast<void>(PlayCinematic(cinematic));
    const CinematicStepResult result = StepCinematic(cinematic, 2.5F);
    ASSERT_EQ(result.firedAudioCues.size(), 1U);
    const CinematicAudioCue& cue = result.firedAudioCues.front();
    EXPECT_TRUE(cue.spatial);
    EXPECT_EQ(cue.position, Math::Vector3(1.0F, 2.0F, 3.0F));
    EXPECT_FLOAT_EQ(cue.minDistance, 2.0F);
    EXPECT_FLOAT_EQ(cue.maxDistance, 40.0F);
}

TEST(CinematicTest, Validity_RejectsBadDistanceBands) {
    CinematicComponent badMin = MakeCinematic();
    badMin.audioKeys.push_back(CinematicAudioKey{1.0, "sfx/a.uvaudio", 1.0F, true,
                                                    Math::Vector3{}, 0.0F, 25.0F});
    EXPECT_FALSE(IsCinematicComponentValid(badMin));

    CinematicComponent inverted = MakeCinematic();
    inverted.audioKeys.push_back(CinematicAudioKey{1.0, "sfx/a.uvaudio", 1.0F, true,
                                                      Math::Vector3{}, 10.0F, 10.0F});
    EXPECT_FALSE(IsCinematicComponentValid(inverted));
}

} // namespace UVE::Scene::Tests
