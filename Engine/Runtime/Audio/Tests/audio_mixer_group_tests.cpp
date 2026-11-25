// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "audio_mixer_group.h"

#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Audio::Tests {
namespace {

TEST(AudioMixerGroupTest, SaturatingAddAudioMixerSourceCount_ClampsAtSizeMaximum) {
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    EXPECT_EQ(SaturatingAddAudioMixerSourceCount(maximum - 1U, 1U), maximum);
    EXPECT_EQ(SaturatingAddAudioMixerSourceCount(maximum - 1U, 2U), maximum);
    EXPECT_EQ(SaturatingAddAudioMixerSourceCount(4U, 3U), 7U);
}

TEST(AudioMixerGroupTest, EvaluateAudioMixParameters_ComposesFiniteFinalValues) {
    AudioMixParameters parameters;
    ASSERT_TRUE(EvaluateAudioMixParameters(0.8F, 1.5F, 0.5F, 2.0F, 0.25F, parameters));
    EXPECT_EQ(parameters, (AudioMixParameters{0.1F, 3.0F}));
    ASSERT_TRUE(EvaluateAudioMixParameters(4.0F, 1.0F, 1.0F, 1.0F, 1.0F, parameters));
    EXPECT_EQ(parameters, (AudioMixParameters{1.0F, 1.0F}));
}

TEST(AudioMixerGroupTest, EvaluateAudioMixParameters_RejectsInvalidInputsAtomically) {
    const AudioMixParameters original{0.25F, 1.5F};
    AudioMixParameters parameters = original;
    EXPECT_FALSE(EvaluateAudioMixParameters(-1.0F, 1.0F, 1.0F, 1.0F, 1.0F, parameters));
    EXPECT_EQ(parameters, original);
    EXPECT_FALSE(EvaluateAudioMixParameters(1.0F, 1.0F, 1.0F, 0.1F, 1.0F, parameters));
    EXPECT_EQ(parameters, original);
    EXPECT_FALSE(EvaluateAudioMixParameters(1.0F, 1.0F, 1.0F, 1.0F, 2.0F, parameters));
    EXPECT_EQ(parameters, original);
    EXPECT_FALSE(EvaluateAudioMixParameters(1.0F, std::numeric_limits<float>::max(),
                                                1.0F, 4.0F, 1.0F, parameters));
    EXPECT_EQ(parameters, original);
}

TEST(AudioMixerGroupTest, MasterAlwaysExistsAndInvalidDefinitionsAreRejected) {
    AudioMixerGroup mixer;

    EXPECT_TRUE(mixer.HasGroup(kMasterAudioMixerGroupName));
    EXPECT_FALSE(mixer.RemoveGroup(kMasterAudioMixerGroupName));
    EXPECT_FALSE(mixer.RegisterGroup("", 1.0F, 1.0F));
    EXPECT_FALSE(mixer.RegisterGroup("SFX", -0.1F, 1.0F));
    EXPECT_FALSE(mixer.RegisterGroup("SFX", 1.0F, 0.0F));
    EXPECT_TRUE(mixer.RegisterGroup("SFX", 0.5F, 1.5F));
    EXPECT_FALSE(mixer.RegisterGroup("SFX", 1.0F, 1.0F));
    EXPECT_TRUE(mixer.HasGroup("SFX"));
}

TEST(AudioMixerGroupTest, InvalidLookupNamesAreRejectedWithoutChangingDefaults) {
    AudioMixerGroup mixer;
    const std::string oversizedName(kMaximumAudioMixerGroupNameBytes + 1U, 'X');
    const std::string nulName = std::string("SFX") + '\0' + "Hidden";

    for (const std::string& name : {oversizedName, nulName}) {
        EXPECT_FALSE(mixer.HasGroup(name));
        EXPECT_FALSE(mixer.RemoveGroup(name));
        EXPECT_FALSE(mixer.SetGroupVolume(name, 0.5F));
        EXPECT_FALSE(mixer.SetGroupPitch(name, 1.5F));
        EXPECT_FALSE(mixer.AttachSource(name));
        EXPECT_FALSE(mixer.DetachSource(name));
        EXPECT_FLOAT_EQ(mixer.GetGroupVolume(name), 1.0F);
        EXPECT_FLOAT_EQ(mixer.GetGroupPitch(name), 1.0F);
        EXPECT_EQ(mixer.ResolveGroupName(name), kMasterAudioMixerGroupName);
    }
}

TEST(AudioMixerGroupTest, SourceRoutingAndDiagnosticsAreCopiedAndDeterministic) {
    AudioMixerGroup mixer;
    ASSERT_TRUE(mixer.RegisterGroup("Music", 0.75F, 0.8F));
    ASSERT_TRUE(mixer.RegisterGroup("SFX", 0.5F, 1.25F));
    ASSERT_TRUE(mixer.AttachSource("SFX"));
    ASSERT_TRUE(mixer.AttachSource("Music"));
    ASSERT_TRUE(mixer.AttachSource(kMasterAudioMixerGroupName));

    const AudioMixerDiagnostics diagnostics = mixer.GetDiagnostics();
    ASSERT_EQ(diagnostics.groupCount, 3U);
    ASSERT_EQ(diagnostics.inspectedGroupCount, 3U);
    ASSERT_EQ(diagnostics.routedSourceCount, 3U);
    ASSERT_FALSE(diagnostics.truncated);
    ASSERT_EQ(diagnostics.groups.size(), 3U);
    EXPECT_EQ(diagnostics.groups[0].name, kMasterAudioMixerGroupName);
    EXPECT_EQ(diagnostics.groups[1].name, "Music");
    EXPECT_EQ(diagnostics.groups[2].name, "SFX");
    EXPECT_EQ(diagnostics.groups[2].sourceCount, 1U);

    const AudioMixerDiagnostics capped = mixer.GetDiagnostics(2U);
    EXPECT_EQ(capped.groupCount, 3U);
    EXPECT_EQ(capped.inspectedGroupCount, 2U);
    EXPECT_EQ(capped.groups.size(), 2U);
    EXPECT_TRUE(capped.truncated);
    EXPECT_EQ(capped.routedSourceCount, 3U);

    EXPECT_FALSE(mixer.RemoveGroup("SFX"));
    EXPECT_TRUE(mixer.DetachSource("SFX"));
    EXPECT_TRUE(mixer.RemoveGroup("SFX"));
    EXPECT_EQ(mixer.ResolveGroupName("SFX"), kMasterAudioMixerGroupName);
}

TEST(AudioMixerGroupTest, MultiplierUpdatesRequireExistingGroupsAndValidRanges) {
    AudioMixerGroup mixer;
    ASSERT_TRUE(mixer.RegisterGroup("Voice", 1.0F, 1.0F));

    EXPECT_TRUE(mixer.SetGroupVolume("Voice", 0.25F));
    EXPECT_TRUE(mixer.SetGroupPitch("Voice", 2.0F));
    EXPECT_FLOAT_EQ(mixer.GetGroupVolume("Voice"), 0.25F);
    EXPECT_FLOAT_EQ(mixer.GetGroupPitch("Voice"), 2.0F);
    EXPECT_FALSE(mixer.SetGroupVolume("Voice", 1.1F));
    EXPECT_FALSE(mixer.SetGroupPitch("Voice", 0.1F));
    EXPECT_FALSE(mixer.SetGroupVolume("Missing", 0.5F));
    EXPECT_FLOAT_EQ(mixer.GetGroupVolume("Missing"), 1.0F);
    EXPECT_FLOAT_EQ(mixer.GetGroupPitch("Missing"), 1.0F);
}


// --- Bus graph: parent chains, mute/solo, cycle rejection, safe reparent-on-removal. ---

TEST(AudioMixerBusGraphTest, ParentChainComposesVolumeAndPitchDownTheTree) {
    AudioMixerGroup mixer;
    ASSERT_TRUE(mixer.RegisterGroup("Music", kMasterAudioMixerGroupName, 0.5F, 1.0F));
    ASSERT_TRUE(mixer.RegisterGroup("Stem", "Music", 0.5F, 2.0F));

    EXPECT_EQ(mixer.GetGroupParent("Stem"), "Music");
    EXPECT_EQ(mixer.GetGroupParent("Music"), kMasterAudioMixerGroupName);
    EXPECT_EQ(mixer.GetGroupParent(kMasterAudioMixerGroupName), "");
    EXPECT_EQ(mixer.GetGroupDepth(kMasterAudioMixerGroupName), 0U);
    EXPECT_EQ(mixer.GetGroupDepth("Music"), 1U);
    EXPECT_EQ(mixer.GetGroupDepth("Stem"), 2U);

    // Authored values stay exactly what the caller set; only the effective value composes.
    EXPECT_FLOAT_EQ(mixer.GetGroupVolume("Stem"), 0.5F);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("Music"), 0.5F);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("Stem"), 0.25F);
    EXPECT_FLOAT_EQ(mixer.GetEffectivePitch("Stem"), 2.0F);

    // Ducking the root ducks the whole tree, which is what a master fader is for.
    ASSERT_TRUE(mixer.SetGroupVolume(kMasterAudioMixerGroupName, 0.5F));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("Music"), 0.25F);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("Stem"), 0.125F);

    // Unknown names resolve to Master, exactly like source routing does.
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("Missing"), 0.5F);
    EXPECT_EQ(mixer.GetGroupParent("Missing"), "");
    EXPECT_EQ(mixer.GetGroupDepth("Missing"), 0U);
}

TEST(AudioMixerBusGraphTest, RegistrationRequiresAnExistingParentAndEmptyMeansMaster) {
    AudioMixerGroup mixer;
    EXPECT_FALSE(mixer.RegisterGroup("Stem", "Missing", 1.0F, 1.0F));
    EXPECT_FALSE(mixer.HasGroup("Stem"));

    ASSERT_TRUE(mixer.RegisterGroup("Stem", "", 1.0F, 1.0F));
    EXPECT_EQ(mixer.GetGroupParent("Stem"), kMasterAudioMixerGroupName);
    EXPECT_EQ(mixer.GetGroupDepth("Stem"), 1U);

    // The flat overload keeps its historical meaning: a child of Master.
    ASSERT_TRUE(mixer.RegisterGroup("SFX", 0.5F, 1.0F));
    EXPECT_EQ(mixer.GetGroupParent("SFX"), kMasterAudioMixerGroupName);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("SFX"), 0.5F);
}

TEST(AudioMixerBusGraphTest, CycleAttemptsAreRejectedAndLeaveTheTreeWalkable) {
    AudioMixerGroup mixer;
    ASSERT_TRUE(mixer.RegisterGroup("A", "Master"));
    ASSERT_TRUE(mixer.RegisterGroup("B", "A"));
    ASSERT_TRUE(mixer.RegisterGroup("C", "B"));
    EXPECT_EQ(mixer.GetGroupDepth("C"), 3U);

    EXPECT_FALSE(mixer.SetGroupParent("A", "A"));
    EXPECT_FALSE(mixer.SetGroupParent("A", "B"));
    EXPECT_FALSE(mixer.SetGroupParent("A", "C"));
    EXPECT_FALSE(mixer.SetGroupParent(kMasterAudioMixerGroupName, "A"));
    EXPECT_FALSE(mixer.SetGroupParent("A", "Missing"));
    EXPECT_EQ(mixer.GetGroupDepth("C"), 3U);
    EXPECT_EQ(mixer.GetGroupParent("A"), kMasterAudioMixerGroupName);

    // Idempotent set, matching SetSourceMixerGroup's own no-op-success contract.
    EXPECT_TRUE(mixer.SetGroupParent("A", kMasterAudioMixerGroupName));

    // A legal move still lands, and the old subtree collapses with it.
    EXPECT_TRUE(mixer.SetGroupParent("C", "Master"));
    EXPECT_EQ(mixer.GetGroupDepth("C"), 1U);
    EXPECT_EQ(mixer.GetGroupDepth("B"), 2U);
}

TEST(AudioMixerBusGraphTest, MuteSilencesTheWholeSubtreeWithoutTouchingAuthoredValues) {
    AudioMixerGroup mixer;
    ASSERT_TRUE(mixer.RegisterGroup("SFX", 0.5F, 1.0F));
    ASSERT_TRUE(mixer.RegisterGroup("Gunshot", "SFX", 0.5F, 1.0F));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("Gunshot"), 0.25F);

    ASSERT_TRUE(mixer.SetGroupMuted("SFX", true));
    EXPECT_TRUE(mixer.IsGroupMuted("SFX"));
    EXPECT_FALSE(mixer.IsGroupMuted("Gunshot"));
    EXPECT_FALSE(mixer.IsGroupAudible("SFX"));
    EXPECT_FALSE(mixer.IsGroupAudible("Gunshot"));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("Gunshot"), 0.0F);
    EXPECT_FLOAT_EQ(mixer.GetGroupVolume("Gunshot"), 0.5F);

    ASSERT_TRUE(mixer.SetGroupMuted("SFX", false));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("Gunshot"), 0.25F);

    // Muting the root silences everything without erasing any authored multiplier.
    ASSERT_TRUE(mixer.SetGroupMuted(kMasterAudioMixerGroupName, true));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("Gunshot"), 0.0F);
    EXPECT_FLOAT_EQ(mixer.GetGroupVolume(kMasterAudioMixerGroupName), 1.0F);
    EXPECT_FALSE(mixer.SetGroupMuted("Missing", true));
}

TEST(AudioMixerBusGraphTest, SoloKeepsOnlyTheSoloedChainAudible) {
    AudioMixerGroup mixer;
    ASSERT_TRUE(mixer.RegisterGroup("Music", 0.5F, 1.0F));
    ASSERT_TRUE(mixer.RegisterGroup("Vocals", "Music", 0.5F, 1.0F));
    ASSERT_TRUE(mixer.RegisterGroup("SFX", 0.5F, 1.0F));

    ASSERT_TRUE(mixer.SetGroupSoloed("Vocals", true));
    EXPECT_TRUE(mixer.IsAnyGroupSoloed());
    EXPECT_TRUE(mixer.IsGroupSoloed("Vocals"));

    // Soloed itself, feeding a soloed bus, or carrying one: all audible.
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("Vocals"), 0.25F);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("Music"), 0.5F);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume(kMasterAudioMixerGroupName), 1.0F);
    EXPECT_FALSE(mixer.IsGroupAudible("SFX"));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("SFX"), 0.0F);

    // Solo one level up: the whole subtree keeps playing.
    ASSERT_TRUE(mixer.SetGroupSoloed("Vocals", false));
    ASSERT_TRUE(mixer.SetGroupSoloed("Music", true));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("Vocals"), 0.25F);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("SFX"), 0.0F);

    // Mute still wins over solo on the muted path.
    ASSERT_TRUE(mixer.SetGroupMuted("Vocals", true));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("Vocals"), 0.0F);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("Music"), 0.5F);

    ASSERT_TRUE(mixer.SetGroupSoloed("Music", false));
    EXPECT_FALSE(mixer.IsAnyGroupSoloed());
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("SFX"), 0.5F);
}

TEST(AudioMixerBusGraphTest, RemovingABusReparentsItsChildrenOntoItsOwnParent) {
    AudioMixerGroup mixer;
    ASSERT_TRUE(mixer.RegisterGroup("Music", 0.5F, 1.0F));
    ASSERT_TRUE(mixer.RegisterGroup("Stem", "Music", 0.5F, 1.0F));
    ASSERT_TRUE(mixer.RegisterGroup("Leaf", "Stem", 0.5F, 1.0F));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("Leaf"), 0.125F);
    ASSERT_TRUE(mixer.AttachSource("Leaf"));

    ASSERT_TRUE(mixer.RemoveGroup("Stem"));
    EXPECT_FALSE(mixer.HasGroup("Stem"));
    EXPECT_EQ(mixer.GetGroupParent("Leaf"), "Music");
    EXPECT_EQ(mixer.GetGroupDepth("Leaf"), 2U);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolume("Leaf"), 0.25F);

    // The routed source survived the reparent: only the removed stage disappeared.
    const AudioMixerDiagnostics diagnostics = mixer.GetDiagnostics();
    EXPECT_EQ(diagnostics.routedSourceCount, 1U);

    // A bus with its own sources is still refused, so a live voice is never orphaned.
    EXPECT_FALSE(mixer.RemoveGroup("Leaf"));
    ASSERT_TRUE(mixer.DetachSource("Leaf"));
    EXPECT_TRUE(mixer.RemoveGroup("Leaf"));
}

TEST(AudioMixerBusGraphTest, BusTableIsCappedIncludingMaster) {
    AudioMixerGroup mixer;
    for (std::size_t index = 0U; index + 1U < kMaximumAudioMixerGroups; ++index) {
        ASSERT_TRUE(mixer.RegisterGroup("Bus" + std::to_string(index)));
    }
    EXPECT_EQ(mixer.GetDiagnostics().groupCount, kMaximumAudioMixerGroups);
    EXPECT_FALSE(mixer.RegisterGroup("OneTooMany"));
    EXPECT_EQ(mixer.GetDiagnostics().groupCount, kMaximumAudioMixerGroups);
}

TEST(AudioMixerBusGraphTest, InsertsRunInFixedSlotOrderAfterTheEffectiveVolume) {
    AudioMixerGroup mixer;
    ASSERT_TRUE(mixer.RegisterGroup("Voice", 0.5F, 1.0F));

    AudioInsertDesc gain;
    gain.kind = AudioInsertKind::Gain;
    gain.params.gain = 4.0F;

    AudioInsertDesc trim;
    trim.kind = AudioInsertKind::Gain;
    trim.params.gain = 0.1F;

    ASSERT_TRUE(mixer.SetGroupInsert("Voice", 0U, gain));
    ASSERT_TRUE(mixer.SetGroupInsert("Voice", 1U, trim));

    // 0.9 * 0.5 (bus) = 0.45, * 4 (slot 0) = 1.8, * 0.1 (slot 1) = 0.18. Nothing clips between the
    // stages - only the chain's final clamp exists - so the intermediate 1.8 survives to be scaled.
    std::vector<float> output;
    ASSERT_TRUE(mixer.ApplyBusChain("Voice", std::vector<float>{0.9F}, output));
    ASSERT_EQ(output.size(), 1U);
    EXPECT_FLOAT_EQ(output[0], 0.18F);

    // A limiter and a gain are not commutative, which is exactly how the fixed order shows itself.
    AudioInsertDesc limiter;
    limiter.kind = AudioInsertKind::Limiter;
    limiter.params.thresholdDb = -6.0F;
    limiter.params.releaseMs = 50.0F;
    AudioInsertDesc boost;
    boost.kind = AudioInsertKind::Gain;
    boost.params.gain = 2.0F;

    ASSERT_TRUE(mixer.SetSampleRate(kDefaultAudioSampleRate));
    ASSERT_TRUE(mixer.SetGroupInsert("Voice", 0U, limiter));
    ASSERT_TRUE(mixer.SetGroupInsert("Voice", 1U, boost));
    ASSERT_TRUE(mixer.ApplyBusChain("Voice", std::vector<float>{1.6F}, output));
    ASSERT_EQ(output.size(), 1U);
    // Bus 0.5 -> 0.8; limiter holds it at -6 dB (0.5011); the +6 dB gain doubles it back past unity.
    EXPECT_FLOAT_EQ(output[0], 1.0F);

    ASSERT_TRUE(mixer.SetGroupInsert("Voice", 0U, boost));
    ASSERT_TRUE(mixer.SetGroupInsert("Voice", 1U, limiter));
    ASSERT_TRUE(mixer.ApplyBusChain("Voice", std::vector<float>{1.6F}, output));
    ASSERT_EQ(output.size(), 1U);
    // Boost first: 0.8 * 2 = 1.6, and now the limiter is the last thing that touches it.
    EXPECT_FLOAT_EQ(output[0], 0.50118724F);
}

TEST(AudioMixerBusGraphTest, InsertConfigurationValidatesSlotBusAndDescription) {
    AudioMixerGroup mixer;
    ASSERT_TRUE(mixer.RegisterGroup("Voice", 1.0F, 1.0F));

    AudioInsertDesc gain;
    gain.kind = AudioInsertKind::Gain;
    gain.params.gain = 0.5F;

    AudioInsertDesc invalidGain = gain;
    invalidGain.params.gain = -1.0F;

    AudioInsertDesc read;
    EXPECT_FALSE(mixer.SetGroupInsert("Missing", 0U, gain));
    EXPECT_FALSE(mixer.SetGroupInsert("Voice", kMaximumAudioBusInserts, gain));
    EXPECT_FALSE(mixer.SetGroupInsert("Voice", 0U, invalidGain));
    EXPECT_FALSE(mixer.GetGroupInsert("Voice", 0U, read));
    EXPECT_FALSE(mixer.GetGroupInsert("Missing", 0U, read));

    ASSERT_TRUE(mixer.SetGroupInsert("Voice", 0U, gain));
    ASSERT_TRUE(mixer.GetGroupInsert("Voice", 0U, read));
    EXPECT_EQ(read, gain);
    EXPECT_FALSE(mixer.GetGroupInsert("Voice", kMaximumAudioBusInserts, read));

    EXPECT_TRUE(mixer.ClearGroupInsert("Voice", 0U));
    EXPECT_FALSE(mixer.GetGroupInsert("Voice", 0U, read));
    EXPECT_FALSE(mixer.ClearGroupInsert("Voice", kMaximumAudioBusInserts));
    EXPECT_FALSE(mixer.ClearGroupInsert("Missing", 0U));
}

TEST(AudioMixerBusGraphTest, SampleRateIsValidatedBeforeAnyInsertUsesIt) {
    AudioMixerGroup mixer;
    EXPECT_FLOAT_EQ(mixer.GetSampleRate(), kDefaultAudioSampleRate);
    EXPECT_FALSE(mixer.SetSampleRate(0.0F));
    EXPECT_FALSE(mixer.SetSampleRate(kMaximumAudioSampleRate * 2.0F));
    EXPECT_FLOAT_EQ(mixer.GetSampleRate(), kDefaultAudioSampleRate);
    EXPECT_TRUE(mixer.SetSampleRate(44100.0F));
    EXPECT_FLOAT_EQ(mixer.GetSampleRate(), 44100.0F);
}

TEST(AudioMixerBusGraphTest, BusChainFailsOnUnknownBusAndScrubsNonFiniteSamples) {
    AudioMixerGroup mixer;
    ASSERT_TRUE(mixer.RegisterGroup("Voice", 0.5F, 1.0F));

    const std::vector<float> sentinel{0.125F};
    std::vector<float> output = sentinel;
    EXPECT_FALSE(mixer.ApplyBusChain("Missing", std::vector<float>{0.5F}, output));
    EXPECT_EQ(output, sentinel);

    // A poisoned sample must not reach the device: it is replaced by silence, not propagated.
    std::vector<float> poisoned{std::numeric_limits<float>::quiet_NaN(), 0.5F};
    ASSERT_TRUE(mixer.ApplyBusChain("Voice", poisoned, output));
    ASSERT_EQ(output.size(), 2U);
    EXPECT_FLOAT_EQ(output[0], 0.0F);
    EXPECT_FLOAT_EQ(output[1], 0.25F);

    // A muted bus silences the samples themselves, not just the device-facing params.
    ASSERT_TRUE(mixer.SetGroupMuted("Voice", true));
    ASSERT_TRUE(mixer.ApplyBusChain("Voice", std::vector<float>{0.8F}, output));
    ASSERT_EQ(output.size(), 1U);
    EXPECT_FLOAT_EQ(output[0], 0.0F);
    ASSERT_TRUE(mixer.SetGroupMuted("Voice", false));

    // Zero frames is a valid no-op, an unknown bus is still an error.
    float untouched = 0.75F;
    EXPECT_TRUE(mixer.ProcessBusChain("Voice", &untouched, 0U));
    EXPECT_FLOAT_EQ(untouched, 0.75F);
    EXPECT_FALSE(mixer.ProcessBusChain("Missing", &untouched, 1U));
}

TEST(AudioMixerBusGraphTest, DiagnosticsExposeTreeShapeAndEffectiveValues) {
    AudioMixerGroup mixer;
    ASSERT_TRUE(mixer.RegisterGroup("Music", 0.5F, 1.0F));
    ASSERT_TRUE(mixer.RegisterGroup("Stem", "Music", 0.5F, 2.0F));
    ASSERT_TRUE(mixer.SetGroupMuted("Stem", true));

    const AudioMixerDiagnostics diagnostics = mixer.GetDiagnostics();
    EXPECT_FALSE(diagnostics.anySoloed);
    ASSERT_EQ(diagnostics.groups.size(), 3U);
    // Name order is the existing deterministic contract and it is unchanged by the tree.
    EXPECT_EQ(diagnostics.groups[0].name, kMasterAudioMixerGroupName);
    EXPECT_EQ(diagnostics.groups[1].name, "Music");
    EXPECT_EQ(diagnostics.groups[2].name, "Stem");

    const AudioMixerGroupSnapshot& stem = diagnostics.groups[2];
    EXPECT_EQ(stem.parent, "Music");
    EXPECT_EQ(stem.depth, 2U);
    EXPECT_TRUE(stem.muted);
    EXPECT_FALSE(stem.soloed);
    EXPECT_FLOAT_EQ(stem.volumeMultiplier, 0.5F);
    EXPECT_FLOAT_EQ(stem.effectiveVolume, 0.0F);
    EXPECT_FLOAT_EQ(stem.pitchMultiplier, 2.0F);
    EXPECT_FLOAT_EQ(stem.effectivePitch, 2.0F);

    ASSERT_TRUE(mixer.SetGroupSoloed("Music", true));
    EXPECT_TRUE(mixer.GetDiagnostics().anySoloed);
}

} // namespace
} // namespace UVE::Audio::Tests
