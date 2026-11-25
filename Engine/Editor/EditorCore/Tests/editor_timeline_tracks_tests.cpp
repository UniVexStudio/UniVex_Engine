// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "animation_clip_editing.h"
#include "editor_timeline_tracks.h"

namespace UVE::Editor {
namespace {

using RowKind = TimelineRowKind;

[[nodiscard]] Asset::AnimationAssetSample Sample(const double seconds, const float z) {
    Asset::AnimationAssetSample sample;
    sample.timeSeconds = seconds;
    sample.pose.position = Math::Vector3{0.0F, 0.0F, z};
    return sample;
}

/// One bone track with a key at 0 and 1 s.
[[nodiscard]] Asset::AnimationAssetBoneTrack BoneTrack(std::string bone) {
    Asset::AnimationAssetBoneTrack track;
    track.bone = std::move(bone);
    track.samples = {Sample(0.0, 0.0F), Sample(1.0, 1.0F)};
    return track;
}

/// One blend-shape track whose keys drive `weightsOf(channels)` over two keys at 0 and 1 s.
[[nodiscard]] Asset::AnimationAssetMorphTrack MorphTrack(std::string node, const std::vector<float>& start,
                                                               const std::vector<float>& end) {
    Asset::AnimationAssetMorphTrack track;
    track.node = std::move(node);
    track.samples = {Asset::AnimationAssetMorphWeightSample{0.0, start},
                     Asset::AnimationAssetMorphWeightSample{1.0, end}};
    return track;
}

[[nodiscard]] TimelineSkeleton Skeleton(std::vector<std::pair<std::string, int>> bones) {
    TimelineSkeleton skeleton;
    skeleton.present = true;
    for (auto& bone : bones) {
        skeleton.bones.push_back(TimelineSkeleton::Bone{std::move(bone.first), bone.second});
    }
    return skeleton;
}

[[nodiscard]] std::vector<std::string> Labels(const std::vector<TimelineRow>& rows) {
    std::vector<std::string> labels;
    for (const TimelineRow& row : rows) {
        labels.push_back(row.label);
    }
    return labels;
}

const std::vector<std::string> kNoExpandedTracks{};

TEST(EditorTimelineTracksTest, BoneClipShowsEventsTransformThenTheRig) {
    Asset::AnimationClipAsset clip;
    clip.clipId = "walk";
    clip.durationSeconds = 1.0;
    clip.samples = {Sample(0.0, 0.0F), Sample(1.0, 1.0F)};
    // The clip's own order is Tail first: the skeleton's order must win.
    clip.bones = {BoneTrack("Tail"), BoneTrack("Hips")};
    const TimelineSkeleton skeleton = Skeleton({{"Hips", 0}, {"Tail", 1}});

    const std::vector<TimelineRow> rows = BuildTimelineRows(clip, skeleton, {}, kNoExpandedTracks);

    EXPECT_EQ(Labels(rows), (std::vector<std::string>{"Events", "Transform", "Hips", "Tail"}));
    EXPECT_EQ(rows[0].kind, RowKind::Events);
    EXPECT_EQ(rows[1].kind, RowKind::Transform);
    EXPECT_EQ(rows[1].track, "Transform");
    EXPECT_EQ(rows[2].kind, RowKind::Bone);
    EXPECT_EQ(rows[2].depth, 0);
    EXPECT_EQ(rows[2].trackIndex, 1U) << "the index follows the clip's own list, not the row's";
    EXPECT_EQ(rows[3].depth, 1) << "a child bone is indented under its parent";
}

TEST(EditorTimelineTracksTest, FacialClipShowsOneRowPerBlendShapeAndNoTransformRow) {
    Asset::AnimationClipAsset clip;
    clip.clipId = "lipsync";
    clip.durationSeconds = 1.0;
    clip.morphWeights = {MorphTrack("Head", {0.0F, 0.0F}, {1.0F, 0.0F}),
                         MorphTrack("Eyes", {0.0F, 0.0F}, {0.0F, 1.0F})};

    const std::vector<TimelineRow> rows = BuildTimelineRows(clip, {}, {}, kNoExpandedTracks);

    ASSERT_EQ(rows.size(), 3U) << "events, Head, Eyes";
    EXPECT_EQ(Labels(rows), (std::vector<std::string>{"Events", "Head", "Eyes"}));
    EXPECT_EQ(rows[1].kind, RowKind::Morph);
    EXPECT_EQ(rows[1].track, "Head");
    EXPECT_EQ(rows[1].trackIndex, 0U);
    EXPECT_EQ(rows[1].channel, -1) << "a weight vector has no channels to open";
    EXPECT_EQ(rows[2].trackIndex, 1U);
}

TEST(EditorTimelineTracksTest, MixedClipKeepsBlendShapesBelowTheBones) {
    Asset::AnimationClipAsset clip;
    clip.clipId = "talk";
    clip.durationSeconds = 1.0;
    clip.samples = {Sample(0.0, 0.0F)};
    clip.bones = {BoneTrack("Hips")};
    clip.morphWeights = {MorphTrack("Head", {0.0F}, {1.0F})};

    const std::vector<std::string> opened{"Hips"};
    const std::vector<TimelineRow> rows = BuildTimelineRows(clip, Skeleton({{"Hips", 0}}), {}, opened);

    EXPECT_EQ(Labels(rows),
              (std::vector<std::string>{"Events", "Transform", "Hips", "Position", "Rotation", "Scale", "Head"}));
    EXPECT_EQ(rows[3].kind, RowKind::BoneChannel);
    EXPECT_EQ(rows[3].channel, 0);
    EXPECT_EQ(rows[3].track, "Hips") << "a channel row answers to its track's name";
    EXPECT_EQ(rows[3].depth, 1);
    EXPECT_EQ(rows.back().kind, RowKind::Morph) << "facial rows never sit above the rig";
}

TEST(EditorTimelineTracksTest, FilterHidesATrackWithItsChannelsButNeverTheClipsOwnMotion) {
    Asset::AnimationClipAsset clip;
    clip.clipId = "talk";
    clip.durationSeconds = 1.0;
    clip.samples = {Sample(0.0, 0.0F)};
    clip.bones = {BoneTrack("Hips"), BoneTrack("Head")};
    clip.morphWeights = {MorphTrack("Face", {0.0F}, {1.0F})};

    const std::vector<std::string> opened{"Hips", "Head"};
    const std::vector<TimelineRow> rows =
        BuildTimelineRows(clip, Skeleton({{"Hips", 0}, {"Head", 1}}), "e", opened);

    // "e" matches Head and Face (case-insensitively), not Hips - and Transform is never filtered.
    EXPECT_EQ(Labels(rows), (std::vector<std::string>{"Events", "Transform", "Head", "Position", "Rotation",
                                                         "Scale", "Face"}));
}

TEST(EditorTimelineTracksTest, BoneWithoutATrackGetsNoRowAndNoSkeletonFallsBackToClipOrder) {
    Asset::AnimationClipAsset clip;
    clip.clipId = "walk";
    clip.durationSeconds = 1.0;
    clip.bones = {BoneTrack("Tail"), BoneTrack("Hips")};

    // A rig that has neither of the clip's bones: nothing to show but the event row.
    const std::vector<TimelineRow> unmatched =
        BuildTimelineRows(clip, Skeleton({{"Root", 0}}), {}, kNoExpandedTracks);
    EXPECT_EQ(Labels(unmatched), (std::vector<std::string>{"Events"}));

    // No skeleton at all: the clip's own order, flat, every track shown.
    const std::vector<TimelineRow> flat = BuildTimelineRows(clip, {}, {}, kNoExpandedTracks);
    EXPECT_EQ(Labels(flat), (std::vector<std::string>{"Events", "Tail", "Hips"}));
    EXPECT_EQ(flat[1].depth, 0);
    EXPECT_EQ(flat[2].depth, 0);
}

TEST(EditorTimelineTracksTest, OnlyTheFramesAShapeMovesOnAreKeys) {
    Asset::AnimationAssetMorphTrack track;
    track.node = "Head";
    track.samples = {Asset::AnimationAssetMorphWeightSample{0.0, {0.0F, 0.0F}},
                     Asset::AnimationAssetMorphWeightSample{0.1, {0.0F, 0.0F}},
                     Asset::AnimationAssetMorphWeightSample{0.2, {0.0F, 0.0F}},
                     Asset::AnimationAssetMorphWeightSample{0.3, {0.2F, 0.0F}},
                     Asset::AnimationAssetMorphWeightSample{0.4, {0.2F, 0.0F}},
                     Asset::AnimationAssetMorphWeightSample{0.5, {0.2F, 0.0F}}};

    EXPECT_TRUE(ShowsMorphKey(track, 0U)) << "the first key always shows";
    EXPECT_FALSE(ShowsMorphKey(track, 1U)) << "held between two identical keys";
    EXPECT_TRUE(ShowsMorphKey(track, 2U)) << "the weight moves out of it";
    EXPECT_TRUE(ShowsMorphKey(track, 3U)) << "the weight moves into it";
    EXPECT_FALSE(ShowsMorphKey(track, 4U)) << "held between two identical keys";
    EXPECT_TRUE(ShowsMorphKey(track, 5U)) << "the last key always shows";
    EXPECT_FALSE(ShowsMorphKey(track, 6U)) << "past the end";

    Asset::AnimationAssetMorphTrack single;
    single.samples = {Asset::AnimationAssetMorphWeightSample{0.0, {0.0F}}};
    EXPECT_TRUE(ShowsMorphKey(single, 0U)) << "a one-key track still shows where it sits";
}

/// A scene the resolve can walk: node names to entities, and each entity's blend shapes.
struct FakeScene final {
    std::vector<std::pair<std::string, Scene::Entity>> nodes;
    std::vector<std::pair<Scene::Entity, std::vector<std::string>>> shapes;

    [[nodiscard]] TimelineMorphScene Make() const {
        TimelineMorphScene scene;
        scene.entityForNode = [this](const std::string_view node) -> std::optional<Scene::Entity> {
            for (const auto& entry : nodes) {
                if (entry.first == node) {
                    return entry.second;
                }
            }
            return std::nullopt;
        };
        scene.targetNames = [this](const Scene::Entity entity) -> std::span<const std::string> {
            for (const auto& entry : shapes) {
                if (entry.first == entity) {
                    return entry.second;
                }
            }
            return {};
        };
        return scene;
    }
};

TEST(EditorTimelineTracksTest, ResolvesEachBlendShapeTrackOntoTheObjectItsNodeNames) {
    const Scene::Entity head{3U, 1U};
    const Scene::Entity eyes{4U, 1U};
    Asset::AnimationClipAsset clip;
    clip.clipId = "lipsync";
    clip.durationSeconds = 1.0;
    clip.morphWeights = {MorphTrack("Head", {0.0F, 0.0F}, {1.0F, 0.5F}),
                         MorphTrack("Eyes", {0.0F, 0.0F}, {0.4F, 0.0F})};
    FakeScene scene;
    scene.nodes = {{"Head", head}, {"Eyes", eyes}};
    scene.shapes = {{head, {"jawOpen", "smile"}}, {eyes, {"blinkLeft", "blinkRight"}}};

    const TimelineMorphPreview preview = ResolveTimelineMorphPreview(clip, 0.25, scene.Make());

    EXPECT_EQ(preview.unresolvedTracks, 0U);
    ASSERT_EQ(preview.writes.size(), 2U);
    EXPECT_EQ(preview.writes[0].entity, head);
    ASSERT_EQ(preview.writes[0].weights.size(), 2U);
    EXPECT_EQ(preview.writes[0].weights[0].target, "jawOpen");
    EXPECT_NEAR(preview.writes[0].weights[0].weight, 0.25F, 1e-6F) << "linear between the two keys";
    EXPECT_NEAR(preview.writes[0].weights[1].weight, 0.125F, 1e-6F);
    EXPECT_NEAR(preview.writes[1].weights[0].weight, 0.1F, 1e-6F);

    // Past the last key the last key holds: a clip that ends does not relax a face.
    const TimelineMorphPreview ended = ResolveTimelineMorphPreview(clip, 2.0, scene.Make());
    ASSERT_EQ(ended.writes.size(), 2U);
    EXPECT_NEAR(ended.writes[0].weights[0].weight, 1.0F, 1e-6F);
    EXPECT_NEAR(ended.writes[1].weights[0].weight, 0.4F, 1e-6F);
}

TEST(EditorTimelineTracksTest, SkipsAndCountsTracksItCannotPlay) {
    const Scene::Entity head{3U, 1U};
    const Scene::Entity unshaped{5U, 1U};
    Asset::AnimationClipAsset clip;
    clip.clipId = "lipsync";
    clip.durationSeconds = 1.0;
    clip.morphWeights = {
        MorphTrack("Head", {0.0F, 0.0F}, {1.0F, 1.0F}),  ///< plays
        MorphTrack("Missing", {0.0F}, {1.0F}),           ///< names no object
        MorphTrack("NoShapes", {0.0F}, {1.0F}),          ///< the object's mesh has no shapes
        MorphTrack("WrongCount", {0.0F}, {1.0F}),        ///< 1 weight, 2 shapes
    };
    clip.morphWeights.push_back(Asset::AnimationAssetMorphTrack{"NoKeys", {}});
    FakeScene scene;
    scene.nodes = {{"Head", head}, {"NoShapes", unshaped}, {"WrongCount", head}, {"NoKeys", head}};
    scene.shapes = {{head, {"jawOpen", "smile"}}, {unshaped, {}}};

    const TimelineMorphPreview preview = ResolveTimelineMorphPreview(clip, 0.5, scene.Make());

    ASSERT_EQ(preview.writes.size(), 1U) << "one playable track, and it still plays";
    EXPECT_EQ(preview.writes[0].entity, head);
    EXPECT_EQ(preview.unresolvedTracks, 4U) << "no object, no shapes, wrong count, no keys";
}

TEST(EditorTimelineTracksTest, ASceneWithNoAnswersResolvesToNothingAndCountsEveryTrack) {
    Asset::AnimationClipAsset clip;
    clip.clipId = "lipsync";
    clip.durationSeconds = 1.0;
    clip.morphWeights = {MorphTrack("Head", {0.0F}, {1.0F})};

    const TimelineMorphPreview preview = ResolveTimelineMorphPreview(clip, 0.5, TimelineMorphScene{});

    EXPECT_TRUE(preview.writes.empty());
    EXPECT_EQ(preview.unresolvedTracks, 1U);
}

TEST(EditorTimelineTracksTest, OtherObjectsGetTheirOwnRowsAndAnswerByTheirPrefixedName) {
    Asset::AnimationClipAsset clip;
    clip.clipId = "door";
    clip.durationSeconds = 1.0;
    clip.samples = {Sample(0.0, 0.0F), Sample(1.0, 1.0F)};
    // A bone and an object may share a name: the object's row answers to the prefixed one.
    clip.bones = {BoneTrack("Hinge")};
    clip.objects = {BoneTrack("Hinge"), BoneTrack("Handle")};
    clip.objects[1].samples.back().pose.position.z = 5.0F;
    const std::string handle = std::string(1, kClipObjectTrackPrefix) + "Handle";
    const std::vector<std::string> expanded{handle};

    const std::vector<TimelineRow> rows = BuildTimelineRows(clip, TimelineSkeleton{}, {}, expanded);

    EXPECT_EQ(Labels(rows), (std::vector<std::string>{"Events", "Transform", "Hinge", "Hinge", "Handle", "Position",
                                                          "Rotation", "Scale"}));
    EXPECT_EQ(rows[2].track, "Hinge");
    EXPECT_EQ(rows[3].track, std::string(1, kClipObjectTrackPrefix) + "Hinge");
    EXPECT_EQ(rows[4].track, handle);
    EXPECT_EQ(rows[5].track, handle) << "a channel row carries its object's identity";

    const auto* const samples = FindClipTrackSamples(clip, handle);
    ASSERT_NE(samples, nullptr);
    EXPECT_FLOAT_EQ(samples->back().pose.position.z, 5.0F);
    EXPECT_EQ(FindClipTrackSamples(clip, "Handle"), nullptr) << "no bone of that name";
    EXPECT_TRUE(InsertClipKey(clip, handle, 0.5, 30.0));
    EXPECT_EQ(clip.objects[1].samples.size(), 3U);
}

} // namespace
} // namespace UVE::Editor
