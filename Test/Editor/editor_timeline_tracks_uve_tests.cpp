// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "editor_timeline_tracks_uve.h"

namespace UVE::Editor {
namespace {

using RowKindUVE = TimelineRowKindUVE;

[[nodiscard]] Asset::AnimationAssetSampleUVE SampleUVE(const double seconds, const float z) {
    Asset::AnimationAssetSampleUVE sample;
    sample.timeSeconds = seconds;
    sample.pose.position = Math::Vector3UVE{0.0F, 0.0F, z};
    return sample;
}

/// One bone track with a key at 0 and 1 s.
[[nodiscard]] Asset::AnimationAssetBoneTrackUVE BoneTrackUVE(std::string bone) {
    Asset::AnimationAssetBoneTrackUVE track;
    track.bone = std::move(bone);
    track.samples = {SampleUVE(0.0, 0.0F), SampleUVE(1.0, 1.0F)};
    return track;
}

/// One blend-shape track whose keys drive `weightsOf(channels)` over two keys at 0 and 1 s.
[[nodiscard]] Asset::AnimationAssetMorphTrackUVE MorphTrackUVE(std::string node, const std::vector<float>& start,
                                                               const std::vector<float>& end) {
    Asset::AnimationAssetMorphTrackUVE track;
    track.node = std::move(node);
    track.samples = {Asset::AnimationAssetMorphWeightSampleUVE{0.0, start},
                     Asset::AnimationAssetMorphWeightSampleUVE{1.0, end}};
    return track;
}

[[nodiscard]] TimelineSkeletonUVE SkeletonUVE(std::vector<std::pair<std::string, int>> bones) {
    TimelineSkeletonUVE skeleton;
    skeleton.present = true;
    for (auto& bone : bones) {
        skeleton.bones.push_back(TimelineSkeletonUVE::BoneUVE{std::move(bone.first), bone.second});
    }
    return skeleton;
}

[[nodiscard]] std::vector<std::string> LabelsUVE(const std::vector<TimelineRowUVE>& rows) {
    std::vector<std::string> labels;
    for (const TimelineRowUVE& row : rows) {
        labels.push_back(row.label);
    }
    return labels;
}

const std::vector<std::string> kNoExpandedTracksUVE{};

TEST(EditorTimelineTracksUVETest, BoneClipShowsEventsTransformThenTheRig) {
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "walk";
    clip.durationSeconds = 1.0;
    clip.samples = {SampleUVE(0.0, 0.0F), SampleUVE(1.0, 1.0F)};
    // The clip's own order is Tail first: the skeleton's order must win.
    clip.bones = {BoneTrackUVE("Tail"), BoneTrackUVE("Hips")};
    const TimelineSkeletonUVE skeleton = SkeletonUVE({{"Hips", 0}, {"Tail", 1}});

    const std::vector<TimelineRowUVE> rows = BuildTimelineRowsUVE(clip, skeleton, {}, kNoExpandedTracksUVE);

    EXPECT_EQ(LabelsUVE(rows), (std::vector<std::string>{"Events", "Transform", "Hips", "Tail"}));
    EXPECT_EQ(rows[0].kind, RowKindUVE::Events);
    EXPECT_EQ(rows[1].kind, RowKindUVE::Transform);
    EXPECT_EQ(rows[1].track, "Transform");
    EXPECT_EQ(rows[2].kind, RowKindUVE::Bone);
    EXPECT_EQ(rows[2].depth, 0);
    EXPECT_EQ(rows[2].trackIndex, 1U) << "the index follows the clip's own list, not the row's";
    EXPECT_EQ(rows[3].depth, 1) << "a child bone is indented under its parent";
}

TEST(EditorTimelineTracksUVETest, FacialClipShowsOneRowPerBlendShapeAndNoTransformRow) {
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "lipsync";
    clip.durationSeconds = 1.0;
    clip.morphWeights = {MorphTrackUVE("Head", {0.0F, 0.0F}, {1.0F, 0.0F}),
                         MorphTrackUVE("Eyes", {0.0F, 0.0F}, {0.0F, 1.0F})};

    const std::vector<TimelineRowUVE> rows = BuildTimelineRowsUVE(clip, {}, {}, kNoExpandedTracksUVE);

    ASSERT_EQ(rows.size(), 3U) << "events, Head, Eyes";
    EXPECT_EQ(LabelsUVE(rows), (std::vector<std::string>{"Events", "Head", "Eyes"}));
    EXPECT_EQ(rows[1].kind, RowKindUVE::Morph);
    EXPECT_EQ(rows[1].track, "Head");
    EXPECT_EQ(rows[1].trackIndex, 0U);
    EXPECT_EQ(rows[1].channel, -1) << "a weight vector has no channels to open";
    EXPECT_EQ(rows[2].trackIndex, 1U);
}

TEST(EditorTimelineTracksUVETest, MixedClipKeepsBlendShapesBelowTheBones) {
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "talk";
    clip.durationSeconds = 1.0;
    clip.samples = {SampleUVE(0.0, 0.0F)};
    clip.bones = {BoneTrackUVE("Hips")};
    clip.morphWeights = {MorphTrackUVE("Head", {0.0F}, {1.0F})};

    const std::vector<std::string> opened{"Hips"};
    const std::vector<TimelineRowUVE> rows = BuildTimelineRowsUVE(clip, SkeletonUVE({{"Hips", 0}}), {}, opened);

    EXPECT_EQ(LabelsUVE(rows),
              (std::vector<std::string>{"Events", "Transform", "Hips", "Position", "Rotation", "Scale", "Head"}));
    EXPECT_EQ(rows[3].kind, RowKindUVE::BoneChannel);
    EXPECT_EQ(rows[3].channel, 0);
    EXPECT_EQ(rows[3].track, "Hips") << "a channel row answers to its track's name";
    EXPECT_EQ(rows[3].depth, 1);
    EXPECT_EQ(rows.back().kind, RowKindUVE::Morph) << "facial rows never sit above the rig";
}

TEST(EditorTimelineTracksUVETest, FilterHidesATrackWithItsChannelsButNeverTheClipsOwnMotion) {
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "talk";
    clip.durationSeconds = 1.0;
    clip.samples = {SampleUVE(0.0, 0.0F)};
    clip.bones = {BoneTrackUVE("Hips"), BoneTrackUVE("Head")};
    clip.morphWeights = {MorphTrackUVE("Face", {0.0F}, {1.0F})};

    const std::vector<std::string> opened{"Hips", "Head"};
    const std::vector<TimelineRowUVE> rows =
        BuildTimelineRowsUVE(clip, SkeletonUVE({{"Hips", 0}, {"Head", 1}}), "e", opened);

    // "e" matches Head and Face (case-insensitively), not Hips - and Transform is never filtered.
    EXPECT_EQ(LabelsUVE(rows), (std::vector<std::string>{"Events", "Transform", "Head", "Position", "Rotation",
                                                         "Scale", "Face"}));
}

TEST(EditorTimelineTracksUVETest, BoneWithoutATrackGetsNoRowAndNoSkeletonFallsBackToClipOrder) {
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "walk";
    clip.durationSeconds = 1.0;
    clip.bones = {BoneTrackUVE("Tail"), BoneTrackUVE("Hips")};

    // A rig that has neither of the clip's bones: nothing to show but the event row.
    const std::vector<TimelineRowUVE> unmatched =
        BuildTimelineRowsUVE(clip, SkeletonUVE({{"Root", 0}}), {}, kNoExpandedTracksUVE);
    EXPECT_EQ(LabelsUVE(unmatched), (std::vector<std::string>{"Events"}));

    // No skeleton at all: the clip's own order, flat, every track shown.
    const std::vector<TimelineRowUVE> flat = BuildTimelineRowsUVE(clip, {}, {}, kNoExpandedTracksUVE);
    EXPECT_EQ(LabelsUVE(flat), (std::vector<std::string>{"Events", "Tail", "Hips"}));
    EXPECT_EQ(flat[1].depth, 0);
    EXPECT_EQ(flat[2].depth, 0);
}

TEST(EditorTimelineTracksUVETest, OnlyTheFramesAShapeMovesOnAreKeys) {
    Asset::AnimationAssetMorphTrackUVE track;
    track.node = "Head";
    track.samples = {Asset::AnimationAssetMorphWeightSampleUVE{0.0, {0.0F, 0.0F}},
                     Asset::AnimationAssetMorphWeightSampleUVE{0.1, {0.0F, 0.0F}},
                     Asset::AnimationAssetMorphWeightSampleUVE{0.2, {0.0F, 0.0F}},
                     Asset::AnimationAssetMorphWeightSampleUVE{0.3, {0.2F, 0.0F}},
                     Asset::AnimationAssetMorphWeightSampleUVE{0.4, {0.2F, 0.0F}},
                     Asset::AnimationAssetMorphWeightSampleUVE{0.5, {0.2F, 0.0F}}};

    EXPECT_TRUE(ShowsMorphKeyUVE(track, 0U)) << "the first key always shows";
    EXPECT_FALSE(ShowsMorphKeyUVE(track, 1U)) << "held between two identical keys";
    EXPECT_TRUE(ShowsMorphKeyUVE(track, 2U)) << "the weight moves out of it";
    EXPECT_TRUE(ShowsMorphKeyUVE(track, 3U)) << "the weight moves into it";
    EXPECT_FALSE(ShowsMorphKeyUVE(track, 4U)) << "held between two identical keys";
    EXPECT_TRUE(ShowsMorphKeyUVE(track, 5U)) << "the last key always shows";
    EXPECT_FALSE(ShowsMorphKeyUVE(track, 6U)) << "past the end";

    Asset::AnimationAssetMorphTrackUVE single;
    single.samples = {Asset::AnimationAssetMorphWeightSampleUVE{0.0, {0.0F}}};
    EXPECT_TRUE(ShowsMorphKeyUVE(single, 0U)) << "a one-key track still shows where it sits";
}

/// A scene the resolve can walk: node names to entities, and each entity's blend shapes.
struct FakeSceneUVE final {
    std::vector<std::pair<std::string, Scene::EntityUVE>> nodes;
    std::vector<std::pair<Scene::EntityUVE, std::vector<std::string>>> shapes;

    [[nodiscard]] TimelineMorphSceneUVE MakeUVE() const {
        TimelineMorphSceneUVE scene;
        scene.entityForNode = [this](const std::string_view node) -> std::optional<Scene::EntityUVE> {
            for (const auto& entry : nodes) {
                if (entry.first == node) {
                    return entry.second;
                }
            }
            return std::nullopt;
        };
        scene.targetNames = [this](const Scene::EntityUVE entity) -> std::span<const std::string> {
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

TEST(EditorTimelineTracksUVETest, ResolvesEachBlendShapeTrackOntoTheObjectItsNodeNames) {
    const Scene::EntityUVE head{3U, 1U};
    const Scene::EntityUVE eyes{4U, 1U};
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "lipsync";
    clip.durationSeconds = 1.0;
    clip.morphWeights = {MorphTrackUVE("Head", {0.0F, 0.0F}, {1.0F, 0.5F}),
                         MorphTrackUVE("Eyes", {0.0F, 0.0F}, {0.4F, 0.0F})};
    FakeSceneUVE scene;
    scene.nodes = {{"Head", head}, {"Eyes", eyes}};
    scene.shapes = {{head, {"jawOpen", "smile"}}, {eyes, {"blinkLeft", "blinkRight"}}};

    const TimelineMorphPreviewUVE preview = ResolveTimelineMorphPreviewUVE(clip, 0.25, scene.MakeUVE());

    EXPECT_EQ(preview.unresolvedTracks, 0U);
    ASSERT_EQ(preview.writes.size(), 2U);
    EXPECT_EQ(preview.writes[0].entity, head);
    ASSERT_EQ(preview.writes[0].weights.size(), 2U);
    EXPECT_EQ(preview.writes[0].weights[0].target, "jawOpen");
    EXPECT_NEAR(preview.writes[0].weights[0].weight, 0.25F, 1e-6F) << "linear between the two keys";
    EXPECT_NEAR(preview.writes[0].weights[1].weight, 0.125F, 1e-6F);
    EXPECT_NEAR(preview.writes[1].weights[0].weight, 0.1F, 1e-6F);

    // Past the last key the last key holds: a clip that ends does not relax a face.
    const TimelineMorphPreviewUVE ended = ResolveTimelineMorphPreviewUVE(clip, 2.0, scene.MakeUVE());
    ASSERT_EQ(ended.writes.size(), 2U);
    EXPECT_NEAR(ended.writes[0].weights[0].weight, 1.0F, 1e-6F);
    EXPECT_NEAR(ended.writes[1].weights[0].weight, 0.4F, 1e-6F);
}

TEST(EditorTimelineTracksUVETest, SkipsAndCountsTracksItCannotPlay) {
    const Scene::EntityUVE head{3U, 1U};
    const Scene::EntityUVE unshaped{5U, 1U};
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "lipsync";
    clip.durationSeconds = 1.0;
    clip.morphWeights = {
        MorphTrackUVE("Head", {0.0F, 0.0F}, {1.0F, 1.0F}),  ///< plays
        MorphTrackUVE("Missing", {0.0F}, {1.0F}),           ///< names no object
        MorphTrackUVE("NoShapes", {0.0F}, {1.0F}),          ///< the object's mesh has no shapes
        MorphTrackUVE("WrongCount", {0.0F}, {1.0F}),        ///< 1 weight, 2 shapes
    };
    clip.morphWeights.push_back(Asset::AnimationAssetMorphTrackUVE{"NoKeys", {}});
    FakeSceneUVE scene;
    scene.nodes = {{"Head", head}, {"NoShapes", unshaped}, {"WrongCount", head}, {"NoKeys", head}};
    scene.shapes = {{head, {"jawOpen", "smile"}}, {unshaped, {}}};

    const TimelineMorphPreviewUVE preview = ResolveTimelineMorphPreviewUVE(clip, 0.5, scene.MakeUVE());

    ASSERT_EQ(preview.writes.size(), 1U) << "one playable track, and it still plays";
    EXPECT_EQ(preview.writes[0].entity, head);
    EXPECT_EQ(preview.unresolvedTracks, 4U) << "no object, no shapes, wrong count, no keys";
}

TEST(EditorTimelineTracksUVETest, ASceneWithNoAnswersResolvesToNothingAndCountsEveryTrack) {
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "lipsync";
    clip.durationSeconds = 1.0;
    clip.morphWeights = {MorphTrackUVE("Head", {0.0F}, {1.0F})};

    const TimelineMorphPreviewUVE preview = ResolveTimelineMorphPreviewUVE(clip, 0.5, TimelineMorphSceneUVE{});

    EXPECT_TRUE(preview.writes.empty());
    EXPECT_EQ(preview.unresolvedTracks, 1U);
}

} // namespace
} // namespace UVE::Editor
