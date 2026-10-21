// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_audio_mixer_uve.h"

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] Audio::AudioMixerGroupSnapshotUVE MakeGroupUVE(std::string name, std::string parent,
                                                            const std::size_t depth,
                                                            const std::size_t sourceCount) {
    Audio::AudioMixerGroupSnapshotUVE group;
    group.name = std::move(name);
    group.parent = std::move(parent);
    group.depth = depth;
    group.sourceCount = sourceCount;
    return group;
}

TEST(EditorAudioMixerUVETest, RowsFollowTheBusTreeAndCarryTheirOwnLevels) {
    Audio::AudioMixerDiagnosticsUVE diagnostics;
    diagnostics.groups.push_back(MakeGroupUVE("Master", "", 0U, 3U));
    diagnostics.groups.push_back(MakeGroupUVE("Music", "Master", 1U, 2U));
    diagnostics.groups.push_back(MakeGroupUVE("SFX", "Master", 1U, 1U));
    diagnostics.groups[1].effectiveVolume = 0.5F;
    diagnostics.routedSourceCount = 3U;

    const auto meterFor = [](const std::string_view name) {
        if (name == "Music") {
            return Audio::AudioMeterReadingUVE{0.5F, 0.25F};
        }
        if (name == "Master") {
            return Audio::AudioMeterReadingUVE{1.0F, 0.5F};
        }
        return Audio::kSilentAudioMeterUVE;
    };
    const EditorAudioMixerSnapshotUVE snapshot = BuildEditorAudioMixerSnapshotUVE(diagnostics, meterFor);

    ASSERT_EQ(snapshot.rows.size(), 3U);
    EXPECT_EQ(snapshot.rows[0].name, "Master");
    EXPECT_EQ(snapshot.rows[1].name, "Music");
    EXPECT_EQ(snapshot.rows[1].depth, 1U);
    EXPECT_FLOAT_EQ(snapshot.rows[1].effectiveVolume, 0.5F);
    EXPECT_NEAR(snapshot.rows[1].peak, 0.5F, 1.0e-6F);
    EXPECT_NEAR(snapshot.rows[1].peakDecibels, -6.0206F, 1.0e-3F);
    EXPECT_EQ(snapshot.routedSourceCount, 3U);

    // The lookup is asked per bus, so a meter can never be attributed to the wrong row.
    const EditorAudioMixerRowUVE* const sfx = snapshot.FindRowUVE("SFX");
    ASSERT_NE(sfx, nullptr);
    EXPECT_FLOAT_EQ(sfx->peak, 0.0F);
    EXPECT_EQ(sfx->levelText, "-inf dB");
    EXPECT_EQ(snapshot.FindRowUVE("NoSuchBus"), nullptr);
}

TEST(EditorAudioMixerUVETest, LevelTextIsReadableAtEveryScale) {
    EXPECT_EQ(FormatEditorAudioDecibelsUVE(0.0F), "-inf dB");
    EXPECT_EQ(FormatEditorAudioDecibelsUVE(-1.0F), "-inf dB");
    EXPECT_EQ(FormatEditorAudioDecibelsUVE(std::nanf("")), "-inf dB");
    EXPECT_EQ(FormatEditorAudioDecibelsUVE(1.0F), "+0.0 dB");
    EXPECT_EQ(FormatEditorAudioDecibelsUVE(0.5F), "-6.0 dB");
    EXPECT_EQ(FormatEditorAudioDecibelsUVE(0.1F), "-20.0 dB");
    // Past full scale keeps its sign, so clipping reads as clipping rather than as unity.
    EXPECT_EQ(FormatEditorAudioDecibelsUVE(2.0F), "+6.0 dB");
}

TEST(EditorAudioMixerUVETest, ABusThatCarriesSourcesButProducesSilenceIsFlagged) {
    Audio::AudioMixerDiagnosticsUVE diagnostics;
    diagnostics.groups.push_back(MakeGroupUVE("Master", "", 0U, 1U));
    diagnostics.groups.push_back(MakeGroupUVE("Voice", "Master", 1U, 1U));
    diagnostics.groups[1].muted = true;
    diagnostics.groups[1].effectiveVolume = 0.0F;

    const EditorAudioMixerSnapshotUVE snapshot =
        BuildEditorAudioMixerSnapshotUVE(diagnostics, [](std::string_view) { return Audio::kSilentAudioMeterUVE; });
    const EditorAudioMixerRowUVE* const voice = snapshot.FindRowUVE("Voice");
    ASSERT_NE(voice, nullptr);
    EXPECT_TRUE(voice->muted);
    EXPECT_TRUE(voice->isSilencedWithSources);

    // A silent bus with nothing routed to it is idle, not a problem: flagging it would teach the
    // reader to ignore the highlight.
    const EditorAudioMixerRowUVE* const master = snapshot.FindRowUVE("Master");
    ASSERT_NE(master, nullptr);
    EXPECT_FALSE(master->isSilencedWithSources);
}

TEST(EditorAudioMixerUVETest, ClippingIsFlaggedFromThePeakAlone) {
    Audio::AudioMixerDiagnosticsUVE diagnostics;
    diagnostics.groups.push_back(MakeGroupUVE("Master", "", 0U, 2U));

    const EditorAudioMixerSnapshotUVE quiet = BuildEditorAudioMixerSnapshotUVE(
        diagnostics, [](std::string_view) { return Audio::AudioMeterReadingUVE{1.0F, 4.0F}; });
    ASSERT_EQ(quiet.rows.size(), 1U);
    EXPECT_FALSE(quiet.rows[0].isClipping); // exactly full scale is loud, not clipped
    EXPECT_FLOAT_EQ(quiet.rows[0].levelNormalized, 1.0F);

    const EditorAudioMixerSnapshotUVE loud = BuildEditorAudioMixerSnapshotUVE(
        diagnostics, [](std::string_view) { return Audio::AudioMeterReadingUVE{1.5F, 1.0F}; });
    ASSERT_EQ(loud.rows.size(), 1U);
    EXPECT_TRUE(loud.rows[0].isClipping);
    EXPECT_FLOAT_EQ(loud.rows[0].levelNormalized, 1.0F); // the bar saturates even though the peak does not
}

TEST(EditorAudioMixerUVETest, OnlyTheFourRegisteredBusesPersistTheirVolume) {
    EXPECT_EQ(EditorAudioBusVolumeSettingIdUVE("Master"), "audio.buses.masterVolume");
    EXPECT_EQ(EditorAudioBusVolumeSettingIdUVE("Music"), "audio.buses.musicVolume");
    EXPECT_EQ(EditorAudioBusVolumeSettingIdUVE("SFX"), "audio.buses.sfxVolume");
    EXPECT_EQ(EditorAudioBusVolumeSettingIdUVE("Voice"), "audio.buses.voiceVolume");
    // A bus the project does not know is session-only, and the row says so rather than pretending.
    EXPECT_TRUE(EditorAudioBusVolumeSettingIdUVE("Footsteps").empty());

    Audio::AudioMixerDiagnosticsUVE diagnostics;
    diagnostics.groups.push_back(MakeGroupUVE("Master", "", 0U, 0U));
    diagnostics.groups.push_back(MakeGroupUVE("Footsteps", "Master", 1U, 0U));
    const EditorAudioMixerSnapshotUVE snapshot =
        BuildEditorAudioMixerSnapshotUVE(diagnostics, [](std::string_view) { return Audio::kSilentAudioMeterUVE; });
    ASSERT_EQ(snapshot.rows.size(), 2U);
    EXPECT_TRUE(snapshot.rows[0].isVolumePersistent);
    EXPECT_FALSE(snapshot.rows[1].isVolumePersistent);
}

TEST(EditorAudioMixerUVETest, TruncatedDiagnosticsAreReportedNotHidden) {
    Audio::AudioMixerDiagnosticsUVE diagnostics;
    diagnostics.truncated = true;
    diagnostics.anySoloed = true;
    const EditorAudioMixerSnapshotUVE snapshot =
        BuildEditorAudioMixerSnapshotUVE(diagnostics, [](std::string_view) { return Audio::kSilentAudioMeterUVE; });
    EXPECT_TRUE(snapshot.truncated);
    EXPECT_TRUE(snapshot.anySoloed);
    EXPECT_TRUE(snapshot.rows.empty());
}

TEST(EditorAudioAttenuationUVETest, TheCurveIsTheEnginesOwnFalloff) {
    Scene::AudioSourceComponentUVE source;
    source.spatial = true;
    source.minDistance = 2.0F;
    source.maxDistance = 10.0F;
    source.attenuationCurve = Scene::AudioAttenuationCurveUVE::Linear;

    const std::vector<EditorAudioAttenuationPointUVE> points = BuildEditorAudioAttenuationCurveUVE(source, 11U);
    ASSERT_EQ(points.size(), 11U);
    EXPECT_FLOAT_EQ(points.front().distance, 0.0F);
    EXPECT_FLOAT_EQ(points.front().gain, 1.0F); // inside minDistance is full volume
    // Each sample must equal what the engine would compute for that distance, bit for bit: the
    // preview is not allowed to be a second implementation of the curve.
    for (const EditorAudioAttenuationPointUVE& point : points) {
        EXPECT_FLOAT_EQ(point.gain, Scene::ComputeAudioSourceDistanceAttenuationUVE(source, point.distance));
    }
    const auto atMax = points.cbegin() + 8; // 8/10 of the drawn range is exactly maxDistance
    EXPECT_NEAR(atMax->distance, 10.0F, 1.0e-4F);
    EXPECT_FLOAT_EQ(atMax->gain, 0.0F);
}

TEST(EditorAudioAttenuationUVETest, TheDrawnRangeIncludesTheTailPastMaxDistance) {
    Scene::AudioSourceComponentUVE source;
    source.maxDistance = 20.0F;
    EXPECT_NEAR(ComputeEditorAudioAttenuationExtentUVE(source), 25.0F, 1.0e-5F);
    EXPECT_NEAR(ComputeEditorAudioAttenuationMarkersUVE(source).maxNormalized, 0.8F, 1.0e-5F);

    // A source mid-edit, with a max distance temporarily below its min, still draws something
    // sane instead of dividing by zero or collapsing the graph.
    source.maxDistance = 0.0F;
    source.minDistance = 5.0F;
    EXPECT_GT(ComputeEditorAudioAttenuationExtentUVE(source), 0.0F);
    const EditorAudioAttenuationMarkersUVE markers = ComputeEditorAudioAttenuationMarkersUVE(source);
    EXPECT_GE(markers.minNormalized, 0.0F);
    EXPECT_LE(markers.maxNormalized, 1.0F);
    EXPECT_LE(markers.minNormalized, markers.maxNormalized);

    Scene::AudioSourceComponentUVE nan;
    nan.maxDistance = std::nanf("");
    EXPECT_TRUE(std::isfinite(ComputeEditorAudioAttenuationExtentUVE(nan)));
    EXPECT_GT(ComputeEditorAudioAttenuationExtentUVE(nan), 0.0F);
}

TEST(EditorAudioAttenuationUVETest, AtwoDimensionalSourceDrawsFlatAndTheDecibelCurveIsFloored) {
    Scene::AudioSourceComponentUVE flat;
    flat.spatial = false;
    for (const EditorAudioAttenuationPointUVE& point : BuildEditorAudioAttenuationCurveUVE(flat, 8U)) {
        EXPECT_FLOAT_EQ(point.gain, 1.0F);
    }

    Scene::AudioSourceComponentUVE falling;
    falling.minDistance = 1.0F;
    falling.maxDistance = 10.0F;
    const std::vector<float> decibels = BuildEditorAudioAttenuationDecibelCurveUVE(falling, 32U);
    ASSERT_EQ(decibels.size(), 32U);
    for (std::size_t index = 1U; index < decibels.size(); ++index) {
        EXPECT_LE(decibels[index], decibels[index - 1U] + 1.0e-4F) << "decibel curve rose at " << index;
    }
    EXPECT_FLOAT_EQ(decibels.front(), 0.0F);
    // The tail is zero gain, which is -inf as a number: the graph clamps it to the meter's floor so
    // one silent sample cannot stretch the whole plot.
    EXPECT_FLOAT_EQ(decibels.back(), Audio::kAudioMeterFloorDbUVE);
}

TEST(EditorAudioAttenuationUVETest, DegenerateSampleCountsReturnNoPoints) {
    Scene::AudioSourceComponentUVE source;
    EXPECT_TRUE(BuildEditorAudioAttenuationCurveUVE(source, 0U).empty());
    EXPECT_TRUE(BuildEditorAudioAttenuationCurveUVE(source, 1U).empty());
    EXPECT_EQ(BuildEditorAudioAttenuationCurveUVE(source, 2U).size(), 2U);
}

} // namespace
} // namespace UVE::Editor::Tests
