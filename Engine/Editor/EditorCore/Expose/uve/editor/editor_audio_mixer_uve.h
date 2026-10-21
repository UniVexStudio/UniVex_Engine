// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "uve/audio/audio_meter_uve.h"
#include "uve/audio/audio_mixer_group_uve.h"
#include "uve/component/audio_source_component_uve.h"

namespace UVE::Editor {

/// The audio authoring tool's model: what a mixer row says, how a bus's level is shown, and what
/// the attenuation curve looks like.
///
/// WHY THIS IS ITS OWN FILE, the same reason editor_render_stats_uve.h is: the numbers, the
/// formatting and the "is this worth flagging" judgement are the parts that can be wrong, and none
/// of them need ImGui. The dock draws what this returns, so a mixer bug is reproducible in a unit
/// test rather than only by opening the editor and watching a bar.

/// One row of the mixer tree, ordered so the tree reads top to bottom.
struct EditorAudioMixerRowUVE final {
    std::string name;
    std::string parent;
    std::size_t depth = 0U;
    /// The bus's own authored multiplier, in [0, 1].
    float volume = 1.0F;
    /// What a source routed here actually receives: ancestors, mute and solo folded in.
    float effectiveVolume = 1.0F;
    bool muted = false;
    bool soloed = false;
    std::size_t sourceCount = 0U;

    /// Measured level of the voices routed to this bus or any bus under it.
    float peak = 0.0F;
    float rms = 0.0F;
    /// `peak` on the meter's own dBFS scale, and the same thing as a 0..1 bar - both computed by
    /// audio_meter_uve.h so the panel and every other consumer agree.
    float peakDecibels = Audio::kAudioMeterFloorDbUVE;
    float levelNormalized = 0.0F;
    /// The text a panel prints: "-inf dB", "-12.3 dB".
    std::string levelText;

    /// Worth a highlight in the UI. Two cases, both of which mean "this row is not doing what the
    /// level suggests": the bus carries sources but produces silence (muted, soloed-out, or a zero
    /// multiplier), or its peak is past full scale and would clip.
    bool isSilencedWithSources = false;
    bool isClipping = false;

    /// True when this bus's volume is backed by a project setting, i.e. an edit persists. Buses a
    /// project registers at runtime have no setting, so their volume is session-only.
    bool isVolumePersistent = false;
};

struct EditorAudioMixerSnapshotUVE final {
    std::vector<EditorAudioMixerRowUVE> rows;
    bool anySoloed = false;
    std::size_t routedSourceCount = 0U;
    /// The diagnostics hit their bus cap and stopped describing the tree.
    bool truncated = false;

    /// The bus every mix ends in, or null when the snapshot is empty.
    [[nodiscard]] const EditorAudioMixerRowUVE* FindRowUVE(std::string_view busName) const noexcept {
        for (const EditorAudioMixerRowUVE& row : rows) {
            if (row.name == busName) {
                return &row;
            }
        }
        return nullptr;
    }
};

/// A linear amplitude as the text a mixer prints. Silence is "-inf", not "-96.0": the floor is an
/// implementation detail of the meter, while "no signal" is what an engineer wants to read.
[[nodiscard]] std::string FormatEditorAudioDecibelsUVE(float linear);

/// The project-setting id that persists `busName`'s volume, or an empty view when the bus has none.
/// Master/Music/SFX/Voice are the four the project registers; anything else is session-only until
/// the mixer tree itself becomes authored document data.
[[nodiscard]] std::string_view EditorAudioBusVolumeSettingIdUVE(std::string_view busName) noexcept;

/// Builds the mixer table from the audio system's own bus diagnostics plus a meter lookup.
///
/// The lookup is a callable `AudioMeterReadingUVE(std::string_view)`, passed as a template parameter
/// rather than a span parallel to `diagnostics.groups`: a span would silently mis-attribute every
/// meter the day the diagnostics' name ordering changed, and a mis-ordered mixer is worse than no
/// mixer because it looks right.
template <typename MeterLookupUVE>
[[nodiscard]] EditorAudioMixerSnapshotUVE BuildEditorAudioMixerSnapshotUVE(
    const Audio::AudioMixerDiagnosticsUVE& diagnostics, MeterLookupUVE&& meterLookup) {
    EditorAudioMixerSnapshotUVE snapshot;
    snapshot.anySoloed = diagnostics.anySoloed;
    snapshot.routedSourceCount = diagnostics.routedSourceCount;
    snapshot.truncated = diagnostics.truncated;
    snapshot.rows.reserve(diagnostics.groups.size());
    for (const Audio::AudioMixerGroupSnapshotUVE& group : diagnostics.groups) {
        EditorAudioMixerRowUVE row;
        row.name = group.name;
        row.parent = group.parent;
        row.depth = group.depth;
        row.volume = group.volumeMultiplier;
        row.effectiveVolume = group.effectiveVolume;
        row.muted = group.muted;
        row.soloed = group.soloed;
        row.sourceCount = group.sourceCount;

        const Audio::AudioMeterReadingUVE meter = meterLookup(std::string_view{group.name});
        row.peak = meter.peak;
        row.rms = meter.rms;
        row.peakDecibels = Audio::LinearAudioLevelToDecibelsUVE(meter.peak);
        row.levelNormalized = Audio::NormalizedAudioMeterLevelUVE(meter.peak);
        row.levelText = FormatEditorAudioDecibelsUVE(meter.peak);
        row.isClipping = Audio::IsAudioMeterClippingUVE(meter);
        row.isSilencedWithSources = row.sourceCount > 0U && row.effectiveVolume <= 0.0F;
        row.isVolumePersistent = !EditorAudioBusVolumeSettingIdUVE(row.name).empty();
        snapshot.rows.push_back(std::move(row));
    }
    return snapshot;
}

/// One point of the attenuation curve the tool previews. `gain` is what
/// Scene::ComputeAudioSourceDistanceAttenuationUVE() returns at `distance`, so the preview is the
/// engine's own curve rather than a second implementation that can drift away from it.
struct EditorAudioAttenuationPointUVE final {
    float distance = 0.0F;
    float gain = 1.0F;
};

/// How far the curve is drawn past the source's own max distance. A curve that stops exactly at the
/// falloff point reads as if it were cut off; a quarter more shows the tail it settles into.
inline constexpr float kEditorAudioAttenuationTailUVE = 0.25F;
/// Enough points that a curve drawn across a few hundred pixels has no visible facets, few enough
/// that the graph is rebuilt every frame without caring.
inline constexpr std::size_t kEditorAudioAttenuationSampleCountUVE = 64U;

/// The distance the curve is drawn out to: maxDistance plus the tail, and never zero, so a source
/// mid-edit (a max distance temporarily below its min) still produces a drawable range.
[[nodiscard]] float ComputeEditorAudioAttenuationExtentUVE(
    const Scene::AudioSourceComponentUVE& source) noexcept;

/// Samples the engine's attenuation curve across the drawn range. A 2D source is flat at 1.0 - it
/// is not attenuated at all - which the preview shows rather than hiding the graph.
[[nodiscard]] std::vector<EditorAudioAttenuationPointUVE> BuildEditorAudioAttenuationCurveUVE(
    const Scene::AudioSourceComponentUVE& source,
    std::size_t sampleCount = kEditorAudioAttenuationSampleCountUVE);

/// The y-values a graph widget plots: the curve in decibels, floored at the meter floor so a long
/// tail does not stretch the graph to -inf.
[[nodiscard]] std::vector<float> BuildEditorAudioAttenuationDecibelCurveUVE(
    const Scene::AudioSourceComponentUVE& source,
    std::size_t sampleCount = kEditorAudioAttenuationSampleCountUVE);

/// Where the source's authored min and max distances sit along the drawn range, as 0..1 positions
/// for a graph's vertical markers. Clamped, because mid-drag values can be outside the range that
/// was used to build the samples.
struct EditorAudioAttenuationMarkersUVE final {
    float minNormalized = 0.0F;
    float maxNormalized = 1.0F;
};

[[nodiscard]] EditorAudioAttenuationMarkersUVE ComputeEditorAudioAttenuationMarkersUVE(
    const Scene::AudioSourceComponentUVE& source) noexcept;

} // namespace UVE::Editor
