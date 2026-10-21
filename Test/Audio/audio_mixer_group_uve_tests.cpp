// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/audio/audio_mixer_group_uve.h"

#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Audio::Tests {
namespace {

TEST(AudioMixerGroupUVETest, SaturatingAddAudioMixerSourceCountUVE_ClampsAtSizeMaximum) {
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    EXPECT_EQ(SaturatingAddAudioMixerSourceCountUVE(maximum - 1U, 1U), maximum);
    EXPECT_EQ(SaturatingAddAudioMixerSourceCountUVE(maximum - 1U, 2U), maximum);
    EXPECT_EQ(SaturatingAddAudioMixerSourceCountUVE(4U, 3U), 7U);
}

TEST(AudioMixerGroupUVETest, EvaluateAudioMixParametersUVE_ComposesFiniteFinalValues) {
    AudioMixParametersUVE parameters;
    ASSERT_TRUE(EvaluateAudioMixParametersUVE(0.8F, 1.5F, 0.5F, 2.0F, 0.25F, parameters));
    EXPECT_EQ(parameters, (AudioMixParametersUVE{0.1F, 3.0F}));
    ASSERT_TRUE(EvaluateAudioMixParametersUVE(4.0F, 1.0F, 1.0F, 1.0F, 1.0F, parameters));
    EXPECT_EQ(parameters, (AudioMixParametersUVE{1.0F, 1.0F}));
}

TEST(AudioMixerGroupUVETest, EvaluateAudioMixParametersUVE_RejectsInvalidInputsAtomically) {
    const AudioMixParametersUVE original{0.25F, 1.5F};
    AudioMixParametersUVE parameters = original;
    EXPECT_FALSE(EvaluateAudioMixParametersUVE(-1.0F, 1.0F, 1.0F, 1.0F, 1.0F, parameters));
    EXPECT_EQ(parameters, original);
    EXPECT_FALSE(EvaluateAudioMixParametersUVE(1.0F, 1.0F, 1.0F, 0.1F, 1.0F, parameters));
    EXPECT_EQ(parameters, original);
    EXPECT_FALSE(EvaluateAudioMixParametersUVE(1.0F, 1.0F, 1.0F, 1.0F, 2.0F, parameters));
    EXPECT_EQ(parameters, original);
    EXPECT_FALSE(EvaluateAudioMixParametersUVE(1.0F, std::numeric_limits<float>::max(),
                                                1.0F, 4.0F, 1.0F, parameters));
    EXPECT_EQ(parameters, original);
}

TEST(AudioMixerGroupUVETest, MasterAlwaysExistsAndInvalidDefinitionsAreRejected) {
    AudioMixerGroupUVE mixer;

    EXPECT_TRUE(mixer.HasGroupUVE(kMasterAudioMixerGroupNameUVE));
    EXPECT_FALSE(mixer.RemoveGroupUVE(kMasterAudioMixerGroupNameUVE));
    EXPECT_FALSE(mixer.RegisterGroupUVE("", 1.0F, 1.0F));
    EXPECT_FALSE(mixer.RegisterGroupUVE("SFX", -0.1F, 1.0F));
    EXPECT_FALSE(mixer.RegisterGroupUVE("SFX", 1.0F, 0.0F));
    EXPECT_TRUE(mixer.RegisterGroupUVE("SFX", 0.5F, 1.5F));
    EXPECT_FALSE(mixer.RegisterGroupUVE("SFX", 1.0F, 1.0F));
    EXPECT_TRUE(mixer.HasGroupUVE("SFX"));
}

TEST(AudioMixerGroupUVETest, InvalidLookupNamesAreRejectedWithoutChangingDefaults) {
    AudioMixerGroupUVE mixer;
    const std::string oversizedName(kMaximumAudioMixerGroupNameBytesUVE + 1U, 'X');
    const std::string nulName = std::string("SFX") + '\0' + "Hidden";

    for (const std::string& name : {oversizedName, nulName}) {
        EXPECT_FALSE(mixer.HasGroupUVE(name));
        EXPECT_FALSE(mixer.RemoveGroupUVE(name));
        EXPECT_FALSE(mixer.SetGroupVolumeUVE(name, 0.5F));
        EXPECT_FALSE(mixer.SetGroupPitchUVE(name, 1.5F));
        EXPECT_FALSE(mixer.AttachSourceUVE(name));
        EXPECT_FALSE(mixer.DetachSourceUVE(name));
        EXPECT_FLOAT_EQ(mixer.GetGroupVolumeUVE(name), 1.0F);
        EXPECT_FLOAT_EQ(mixer.GetGroupPitchUVE(name), 1.0F);
        EXPECT_EQ(mixer.ResolveGroupNameUVE(name), kMasterAudioMixerGroupNameUVE);
    }
}

TEST(AudioMixerGroupUVETest, SourceRoutingAndDiagnosticsAreCopiedAndDeterministic) {
    AudioMixerGroupUVE mixer;
    ASSERT_TRUE(mixer.RegisterGroupUVE("Music", 0.75F, 0.8F));
    ASSERT_TRUE(mixer.RegisterGroupUVE("SFX", 0.5F, 1.25F));
    ASSERT_TRUE(mixer.AttachSourceUVE("SFX"));
    ASSERT_TRUE(mixer.AttachSourceUVE("Music"));
    ASSERT_TRUE(mixer.AttachSourceUVE(kMasterAudioMixerGroupNameUVE));

    const AudioMixerDiagnosticsUVE diagnostics = mixer.GetDiagnosticsUVE();
    ASSERT_EQ(diagnostics.groupCount, 3U);
    ASSERT_EQ(diagnostics.inspectedGroupCount, 3U);
    ASSERT_EQ(diagnostics.routedSourceCount, 3U);
    ASSERT_FALSE(diagnostics.truncated);
    ASSERT_EQ(diagnostics.groups.size(), 3U);
    EXPECT_EQ(diagnostics.groups[0].name, kMasterAudioMixerGroupNameUVE);
    EXPECT_EQ(diagnostics.groups[1].name, "Music");
    EXPECT_EQ(diagnostics.groups[2].name, "SFX");
    EXPECT_EQ(diagnostics.groups[2].sourceCount, 1U);

    const AudioMixerDiagnosticsUVE capped = mixer.GetDiagnosticsUVE(2U);
    EXPECT_EQ(capped.groupCount, 3U);
    EXPECT_EQ(capped.inspectedGroupCount, 2U);
    EXPECT_EQ(capped.groups.size(), 2U);
    EXPECT_TRUE(capped.truncated);
    EXPECT_EQ(capped.routedSourceCount, 3U);

    EXPECT_FALSE(mixer.RemoveGroupUVE("SFX"));
    EXPECT_TRUE(mixer.DetachSourceUVE("SFX"));
    EXPECT_TRUE(mixer.RemoveGroupUVE("SFX"));
    EXPECT_EQ(mixer.ResolveGroupNameUVE("SFX"), kMasterAudioMixerGroupNameUVE);
}

TEST(AudioMixerGroupUVETest, MultiplierUpdatesRequireExistingGroupsAndValidRanges) {
    AudioMixerGroupUVE mixer;
    ASSERT_TRUE(mixer.RegisterGroupUVE("Voice", 1.0F, 1.0F));

    EXPECT_TRUE(mixer.SetGroupVolumeUVE("Voice", 0.25F));
    EXPECT_TRUE(mixer.SetGroupPitchUVE("Voice", 2.0F));
    EXPECT_FLOAT_EQ(mixer.GetGroupVolumeUVE("Voice"), 0.25F);
    EXPECT_FLOAT_EQ(mixer.GetGroupPitchUVE("Voice"), 2.0F);
    EXPECT_FALSE(mixer.SetGroupVolumeUVE("Voice", 1.1F));
    EXPECT_FALSE(mixer.SetGroupPitchUVE("Voice", 0.1F));
    EXPECT_FALSE(mixer.SetGroupVolumeUVE("Missing", 0.5F));
    EXPECT_FLOAT_EQ(mixer.GetGroupVolumeUVE("Missing"), 1.0F);
    EXPECT_FLOAT_EQ(mixer.GetGroupPitchUVE("Missing"), 1.0F);
}


// --- Bus graph: parent chains, mute/solo, cycle rejection, safe reparent-on-removal. ---

TEST(AudioMixerBusGraphUVETest, ParentChainComposesVolumeAndPitchDownTheTree) {
    AudioMixerGroupUVE mixer;
    ASSERT_TRUE(mixer.RegisterGroupUVE("Music", kMasterAudioMixerGroupNameUVE, 0.5F, 1.0F));
    ASSERT_TRUE(mixer.RegisterGroupUVE("Stem", "Music", 0.5F, 2.0F));

    EXPECT_EQ(mixer.GetGroupParentUVE("Stem"), "Music");
    EXPECT_EQ(mixer.GetGroupParentUVE("Music"), kMasterAudioMixerGroupNameUVE);
    EXPECT_EQ(mixer.GetGroupParentUVE(kMasterAudioMixerGroupNameUVE), "");
    EXPECT_EQ(mixer.GetGroupDepthUVE(kMasterAudioMixerGroupNameUVE), 0U);
    EXPECT_EQ(mixer.GetGroupDepthUVE("Music"), 1U);
    EXPECT_EQ(mixer.GetGroupDepthUVE("Stem"), 2U);

    // Authored values stay exactly what the caller set; only the effective value composes.
    EXPECT_FLOAT_EQ(mixer.GetGroupVolumeUVE("Stem"), 0.5F);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("Music"), 0.5F);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("Stem"), 0.25F);
    EXPECT_FLOAT_EQ(mixer.GetEffectivePitchUVE("Stem"), 2.0F);

    // Ducking the root ducks the whole tree, which is what a master fader is for.
    ASSERT_TRUE(mixer.SetGroupVolumeUVE(kMasterAudioMixerGroupNameUVE, 0.5F));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("Music"), 0.25F);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("Stem"), 0.125F);

    // Unknown names resolve to Master, exactly like source routing does.
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("Missing"), 0.5F);
    EXPECT_EQ(mixer.GetGroupParentUVE("Missing"), "");
    EXPECT_EQ(mixer.GetGroupDepthUVE("Missing"), 0U);
}

TEST(AudioMixerBusGraphUVETest, RegistrationRequiresAnExistingParentAndEmptyMeansMaster) {
    AudioMixerGroupUVE mixer;
    EXPECT_FALSE(mixer.RegisterGroupUVE("Stem", "Missing", 1.0F, 1.0F));
    EXPECT_FALSE(mixer.HasGroupUVE("Stem"));

    ASSERT_TRUE(mixer.RegisterGroupUVE("Stem", "", 1.0F, 1.0F));
    EXPECT_EQ(mixer.GetGroupParentUVE("Stem"), kMasterAudioMixerGroupNameUVE);
    EXPECT_EQ(mixer.GetGroupDepthUVE("Stem"), 1U);

    // The flat overload keeps its historical meaning: a child of Master.
    ASSERT_TRUE(mixer.RegisterGroupUVE("SFX", 0.5F, 1.0F));
    EXPECT_EQ(mixer.GetGroupParentUVE("SFX"), kMasterAudioMixerGroupNameUVE);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("SFX"), 0.5F);
}

TEST(AudioMixerBusGraphUVETest, CycleAttemptsAreRejectedAndLeaveTheTreeWalkable) {
    AudioMixerGroupUVE mixer;
    ASSERT_TRUE(mixer.RegisterGroupUVE("A", "Master"));
    ASSERT_TRUE(mixer.RegisterGroupUVE("B", "A"));
    ASSERT_TRUE(mixer.RegisterGroupUVE("C", "B"));
    EXPECT_EQ(mixer.GetGroupDepthUVE("C"), 3U);

    EXPECT_FALSE(mixer.SetGroupParentUVE("A", "A"));
    EXPECT_FALSE(mixer.SetGroupParentUVE("A", "B"));
    EXPECT_FALSE(mixer.SetGroupParentUVE("A", "C"));
    EXPECT_FALSE(mixer.SetGroupParentUVE(kMasterAudioMixerGroupNameUVE, "A"));
    EXPECT_FALSE(mixer.SetGroupParentUVE("A", "Missing"));
    EXPECT_EQ(mixer.GetGroupDepthUVE("C"), 3U);
    EXPECT_EQ(mixer.GetGroupParentUVE("A"), kMasterAudioMixerGroupNameUVE);

    // Idempotent set, matching SetSourceMixerGroupUVE's own no-op-success contract.
    EXPECT_TRUE(mixer.SetGroupParentUVE("A", kMasterAudioMixerGroupNameUVE));

    // A legal move still lands, and the old subtree collapses with it.
    EXPECT_TRUE(mixer.SetGroupParentUVE("C", "Master"));
    EXPECT_EQ(mixer.GetGroupDepthUVE("C"), 1U);
    EXPECT_EQ(mixer.GetGroupDepthUVE("B"), 2U);
}

TEST(AudioMixerBusGraphUVETest, MuteSilencesTheWholeSubtreeWithoutTouchingAuthoredValues) {
    AudioMixerGroupUVE mixer;
    ASSERT_TRUE(mixer.RegisterGroupUVE("SFX", 0.5F, 1.0F));
    ASSERT_TRUE(mixer.RegisterGroupUVE("Gunshot", "SFX", 0.5F, 1.0F));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("Gunshot"), 0.25F);

    ASSERT_TRUE(mixer.SetGroupMutedUVE("SFX", true));
    EXPECT_TRUE(mixer.IsGroupMutedUVE("SFX"));
    EXPECT_FALSE(mixer.IsGroupMutedUVE("Gunshot"));
    EXPECT_FALSE(mixer.IsGroupAudibleUVE("SFX"));
    EXPECT_FALSE(mixer.IsGroupAudibleUVE("Gunshot"));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("Gunshot"), 0.0F);
    EXPECT_FLOAT_EQ(mixer.GetGroupVolumeUVE("Gunshot"), 0.5F);

    ASSERT_TRUE(mixer.SetGroupMutedUVE("SFX", false));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("Gunshot"), 0.25F);

    // Muting the root silences everything without erasing any authored multiplier.
    ASSERT_TRUE(mixer.SetGroupMutedUVE(kMasterAudioMixerGroupNameUVE, true));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("Gunshot"), 0.0F);
    EXPECT_FLOAT_EQ(mixer.GetGroupVolumeUVE(kMasterAudioMixerGroupNameUVE), 1.0F);
    EXPECT_FALSE(mixer.SetGroupMutedUVE("Missing", true));
}

TEST(AudioMixerBusGraphUVETest, SoloKeepsOnlyTheSoloedChainAudible) {
    AudioMixerGroupUVE mixer;
    ASSERT_TRUE(mixer.RegisterGroupUVE("Music", 0.5F, 1.0F));
    ASSERT_TRUE(mixer.RegisterGroupUVE("Vocals", "Music", 0.5F, 1.0F));
    ASSERT_TRUE(mixer.RegisterGroupUVE("SFX", 0.5F, 1.0F));

    ASSERT_TRUE(mixer.SetGroupSoloedUVE("Vocals", true));
    EXPECT_TRUE(mixer.IsAnyGroupSoloedUVE());
    EXPECT_TRUE(mixer.IsGroupSoloedUVE("Vocals"));

    // Soloed itself, feeding a soloed bus, or carrying one: all audible.
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("Vocals"), 0.25F);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("Music"), 0.5F);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE(kMasterAudioMixerGroupNameUVE), 1.0F);
    EXPECT_FALSE(mixer.IsGroupAudibleUVE("SFX"));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("SFX"), 0.0F);

    // Solo one level up: the whole subtree keeps playing.
    ASSERT_TRUE(mixer.SetGroupSoloedUVE("Vocals", false));
    ASSERT_TRUE(mixer.SetGroupSoloedUVE("Music", true));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("Vocals"), 0.25F);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("SFX"), 0.0F);

    // Mute still wins over solo on the muted path.
    ASSERT_TRUE(mixer.SetGroupMutedUVE("Vocals", true));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("Vocals"), 0.0F);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("Music"), 0.5F);

    ASSERT_TRUE(mixer.SetGroupSoloedUVE("Music", false));
    EXPECT_FALSE(mixer.IsAnyGroupSoloedUVE());
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("SFX"), 0.5F);
}

TEST(AudioMixerBusGraphUVETest, RemovingABusReparentsItsChildrenOntoItsOwnParent) {
    AudioMixerGroupUVE mixer;
    ASSERT_TRUE(mixer.RegisterGroupUVE("Music", 0.5F, 1.0F));
    ASSERT_TRUE(mixer.RegisterGroupUVE("Stem", "Music", 0.5F, 1.0F));
    ASSERT_TRUE(mixer.RegisterGroupUVE("Leaf", "Stem", 0.5F, 1.0F));
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("Leaf"), 0.125F);
    ASSERT_TRUE(mixer.AttachSourceUVE("Leaf"));

    ASSERT_TRUE(mixer.RemoveGroupUVE("Stem"));
    EXPECT_FALSE(mixer.HasGroupUVE("Stem"));
    EXPECT_EQ(mixer.GetGroupParentUVE("Leaf"), "Music");
    EXPECT_EQ(mixer.GetGroupDepthUVE("Leaf"), 2U);
    EXPECT_FLOAT_EQ(mixer.GetEffectiveVolumeUVE("Leaf"), 0.25F);

    // The routed source survived the reparent: only the removed stage disappeared.
    const AudioMixerDiagnosticsUVE diagnostics = mixer.GetDiagnosticsUVE();
    EXPECT_EQ(diagnostics.routedSourceCount, 1U);

    // A bus with its own sources is still refused, so a live voice is never orphaned.
    EXPECT_FALSE(mixer.RemoveGroupUVE("Leaf"));
    ASSERT_TRUE(mixer.DetachSourceUVE("Leaf"));
    EXPECT_TRUE(mixer.RemoveGroupUVE("Leaf"));
}

TEST(AudioMixerBusGraphUVETest, BusTableIsCappedIncludingMaster) {
    AudioMixerGroupUVE mixer;
    for (std::size_t index = 0U; index + 1U < kMaximumAudioMixerGroupsUVE; ++index) {
        ASSERT_TRUE(mixer.RegisterGroupUVE("Bus" + std::to_string(index)));
    }
    EXPECT_EQ(mixer.GetDiagnosticsUVE().groupCount, kMaximumAudioMixerGroupsUVE);
    EXPECT_FALSE(mixer.RegisterGroupUVE("OneTooMany"));
    EXPECT_EQ(mixer.GetDiagnosticsUVE().groupCount, kMaximumAudioMixerGroupsUVE);
}

TEST(AudioMixerBusGraphUVETest, InsertsRunInFixedSlotOrderAfterTheEffectiveVolume) {
    AudioMixerGroupUVE mixer;
    ASSERT_TRUE(mixer.RegisterGroupUVE("Voice", 0.5F, 1.0F));

    AudioInsertDescUVE gain;
    gain.kind = AudioInsertKindUVE::Gain;
    gain.params.gain = 4.0F;

    AudioInsertDescUVE trim;
    trim.kind = AudioInsertKindUVE::Gain;
    trim.params.gain = 0.1F;

    ASSERT_TRUE(mixer.SetGroupInsertUVE("Voice", 0U, gain));
    ASSERT_TRUE(mixer.SetGroupInsertUVE("Voice", 1U, trim));

    // 0.9 * 0.5 (bus) = 0.45, * 4 (slot 0) = 1.8, * 0.1 (slot 1) = 0.18. Nothing clips between the
    // stages - only the chain's final clamp exists - so the intermediate 1.8 survives to be scaled.
    std::vector<float> output;
    ASSERT_TRUE(mixer.ApplyBusChainUVE("Voice", std::vector<float>{0.9F}, output));
    ASSERT_EQ(output.size(), 1U);
    EXPECT_FLOAT_EQ(output[0], 0.18F);

    // A limiter and a gain are not commutative, which is exactly how the fixed order shows itself.
    AudioInsertDescUVE limiter;
    limiter.kind = AudioInsertKindUVE::Limiter;
    limiter.params.thresholdDb = -6.0F;
    limiter.params.releaseMs = 50.0F;
    AudioInsertDescUVE boost;
    boost.kind = AudioInsertKindUVE::Gain;
    boost.params.gain = 2.0F;

    ASSERT_TRUE(mixer.SetSampleRateUVE(kDefaultAudioSampleRateUVE));
    ASSERT_TRUE(mixer.SetGroupInsertUVE("Voice", 0U, limiter));
    ASSERT_TRUE(mixer.SetGroupInsertUVE("Voice", 1U, boost));
    ASSERT_TRUE(mixer.ApplyBusChainUVE("Voice", std::vector<float>{1.6F}, output));
    ASSERT_EQ(output.size(), 1U);
    // Bus 0.5 -> 0.8; limiter holds it at -6 dB (0.5011); the +6 dB gain doubles it back past unity.
    EXPECT_FLOAT_EQ(output[0], 1.0F);

    ASSERT_TRUE(mixer.SetGroupInsertUVE("Voice", 0U, boost));
    ASSERT_TRUE(mixer.SetGroupInsertUVE("Voice", 1U, limiter));
    ASSERT_TRUE(mixer.ApplyBusChainUVE("Voice", std::vector<float>{1.6F}, output));
    ASSERT_EQ(output.size(), 1U);
    // Boost first: 0.8 * 2 = 1.6, and now the limiter is the last thing that touches it.
    EXPECT_FLOAT_EQ(output[0], 0.50118724F);
}

TEST(AudioMixerBusGraphUVETest, InsertConfigurationValidatesSlotBusAndDescription) {
    AudioMixerGroupUVE mixer;
    ASSERT_TRUE(mixer.RegisterGroupUVE("Voice", 1.0F, 1.0F));

    AudioInsertDescUVE gain;
    gain.kind = AudioInsertKindUVE::Gain;
    gain.params.gain = 0.5F;

    AudioInsertDescUVE invalidGain = gain;
    invalidGain.params.gain = -1.0F;

    AudioInsertDescUVE read;
    EXPECT_FALSE(mixer.SetGroupInsertUVE("Missing", 0U, gain));
    EXPECT_FALSE(mixer.SetGroupInsertUVE("Voice", kMaximumAudioBusInsertsUVE, gain));
    EXPECT_FALSE(mixer.SetGroupInsertUVE("Voice", 0U, invalidGain));
    EXPECT_FALSE(mixer.GetGroupInsertUVE("Voice", 0U, read));
    EXPECT_FALSE(mixer.GetGroupInsertUVE("Missing", 0U, read));

    ASSERT_TRUE(mixer.SetGroupInsertUVE("Voice", 0U, gain));
    ASSERT_TRUE(mixer.GetGroupInsertUVE("Voice", 0U, read));
    EXPECT_EQ(read, gain);
    EXPECT_FALSE(mixer.GetGroupInsertUVE("Voice", kMaximumAudioBusInsertsUVE, read));

    EXPECT_TRUE(mixer.ClearGroupInsertUVE("Voice", 0U));
    EXPECT_FALSE(mixer.GetGroupInsertUVE("Voice", 0U, read));
    EXPECT_FALSE(mixer.ClearGroupInsertUVE("Voice", kMaximumAudioBusInsertsUVE));
    EXPECT_FALSE(mixer.ClearGroupInsertUVE("Missing", 0U));
}

TEST(AudioMixerBusGraphUVETest, SampleRateIsValidatedBeforeAnyInsertUsesIt) {
    AudioMixerGroupUVE mixer;
    EXPECT_FLOAT_EQ(mixer.GetSampleRateUVE(), kDefaultAudioSampleRateUVE);
    EXPECT_FALSE(mixer.SetSampleRateUVE(0.0F));
    EXPECT_FALSE(mixer.SetSampleRateUVE(kMaximumAudioSampleRateUVE * 2.0F));
    EXPECT_FLOAT_EQ(mixer.GetSampleRateUVE(), kDefaultAudioSampleRateUVE);
    EXPECT_TRUE(mixer.SetSampleRateUVE(44100.0F));
    EXPECT_FLOAT_EQ(mixer.GetSampleRateUVE(), 44100.0F);
}

TEST(AudioMixerBusGraphUVETest, BusChainFailsOnUnknownBusAndScrubsNonFiniteSamples) {
    AudioMixerGroupUVE mixer;
    ASSERT_TRUE(mixer.RegisterGroupUVE("Voice", 0.5F, 1.0F));

    const std::vector<float> sentinel{0.125F};
    std::vector<float> output = sentinel;
    EXPECT_FALSE(mixer.ApplyBusChainUVE("Missing", std::vector<float>{0.5F}, output));
    EXPECT_EQ(output, sentinel);

    // A poisoned sample must not reach the device: it is replaced by silence, not propagated.
    std::vector<float> poisoned{std::numeric_limits<float>::quiet_NaN(), 0.5F};
    ASSERT_TRUE(mixer.ApplyBusChainUVE("Voice", poisoned, output));
    ASSERT_EQ(output.size(), 2U);
    EXPECT_FLOAT_EQ(output[0], 0.0F);
    EXPECT_FLOAT_EQ(output[1], 0.25F);

    // A muted bus silences the samples themselves, not just the device-facing params.
    ASSERT_TRUE(mixer.SetGroupMutedUVE("Voice", true));
    ASSERT_TRUE(mixer.ApplyBusChainUVE("Voice", std::vector<float>{0.8F}, output));
    ASSERT_EQ(output.size(), 1U);
    EXPECT_FLOAT_EQ(output[0], 0.0F);
    ASSERT_TRUE(mixer.SetGroupMutedUVE("Voice", false));

    // Zero frames is a valid no-op, an unknown bus is still an error.
    float untouched = 0.75F;
    EXPECT_TRUE(mixer.ProcessBusChainUVE("Voice", &untouched, 0U));
    EXPECT_FLOAT_EQ(untouched, 0.75F);
    EXPECT_FALSE(mixer.ProcessBusChainUVE("Missing", &untouched, 1U));
}

TEST(AudioMixerBusGraphUVETest, DiagnosticsExposeTreeShapeAndEffectiveValues) {
    AudioMixerGroupUVE mixer;
    ASSERT_TRUE(mixer.RegisterGroupUVE("Music", 0.5F, 1.0F));
    ASSERT_TRUE(mixer.RegisterGroupUVE("Stem", "Music", 0.5F, 2.0F));
    ASSERT_TRUE(mixer.SetGroupMutedUVE("Stem", true));

    const AudioMixerDiagnosticsUVE diagnostics = mixer.GetDiagnosticsUVE();
    EXPECT_FALSE(diagnostics.anySoloed);
    ASSERT_EQ(diagnostics.groups.size(), 3U);
    // Name order is the existing deterministic contract and it is unchanged by the tree.
    EXPECT_EQ(diagnostics.groups[0].name, kMasterAudioMixerGroupNameUVE);
    EXPECT_EQ(diagnostics.groups[1].name, "Music");
    EXPECT_EQ(diagnostics.groups[2].name, "Stem");

    const AudioMixerGroupSnapshotUVE& stem = diagnostics.groups[2];
    EXPECT_EQ(stem.parent, "Music");
    EXPECT_EQ(stem.depth, 2U);
    EXPECT_TRUE(stem.muted);
    EXPECT_FALSE(stem.soloed);
    EXPECT_FLOAT_EQ(stem.volumeMultiplier, 0.5F);
    EXPECT_FLOAT_EQ(stem.effectiveVolume, 0.0F);
    EXPECT_FLOAT_EQ(stem.pitchMultiplier, 2.0F);
    EXPECT_FLOAT_EQ(stem.effectivePitch, 2.0F);

    ASSERT_TRUE(mixer.SetGroupSoloedUVE("Music", true));
    EXPECT_TRUE(mixer.GetDiagnosticsUVE().anySoloed);
}

} // namespace
} // namespace UVE::Audio::Tests
