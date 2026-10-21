// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Audio {

inline constexpr std::size_t kMaximumAudioMixerGroupsUVE = 32U;
inline constexpr std::size_t kMaximumAudioMixerGroupNameBytesUVE = 64U;
inline constexpr float kMinimumAudioMixerPitchMultiplierUVE = 0.25F;
inline constexpr float kMaximumAudioMixerPitchMultiplierUVE = 4.0F;
inline constexpr std::string_view kMasterAudioMixerGroupNameUVE = "Master";

/// Fixed insert-slot count per bus. Slots run in ascending index order on the audio path, and the
/// bus's own effective volume always runs before them.
inline constexpr std::size_t kMaximumAudioBusGainSlotsUVE = 4U;

[[nodiscard]] constexpr std::size_t SaturatingAddAudioMixerSourceCountUVE(
    const std::size_t current, const std::size_t added) noexcept {
    return added > std::numeric_limits<std::size_t>::max() - current
               ? std::numeric_limits<std::size_t>::max()
               : current + added;
}

struct AudioMixParametersUVE final {
    float gain = 1.0F;
    float pitch = 1.0F;
    [[nodiscard]] bool operator==(const AudioMixParametersUVE&) const noexcept = default;
};

/// Evaluates finite source/group/attenuation inputs into final device-facing gain and pitch.
/// It performs no device I/O, voice mutation, mixer routing, streaming, or backend selection.
[[nodiscard]] bool EvaluateAudioMixParametersUVE(
    float sourceVolume, float sourcePitch, float groupVolume, float groupPitch,
    float attenuation, AudioMixParametersUVE& outParameters) noexcept;

struct AudioMixerGroupSnapshotUVE final {
    std::string name;
    /// The bus's OWN authored multipliers, before any ancestor is applied.
    float volumeMultiplier = 1.0F;
    float pitchMultiplier = 1.0F;
    std::size_t sourceCount = 0U;
    /// The bus this one feeds into; empty only on Master, which is the root.
    std::string parent;
    /// Muting a bus silences it and its whole subtree without touching its authored multipliers.
    bool muted = false;
    /// While any bus is soloed, buses outside every soloed chain are silent.
    bool soloed = false;
    /// What a source routed here actually receives: this bus's own multipliers composed with every
    /// ancestor's, with mute and solo folded in. A silenced bus reports 0, not its authored value.
    float effectiveVolume = 1.0F;
    float effectivePitch = 1.0F;
    /// Distance from the root, for a tree view: Master is 0, its children 1.
    std::size_t depth = 0U;

    [[nodiscard]] bool operator==(const AudioMixerGroupSnapshotUVE&) const noexcept = default;
};

struct AudioMixerDiagnosticsUVE final {
    std::size_t groupCount = 0U;
    std::size_t inspectedGroupCount = 0U;
    /// Total routed source count; aggregation saturates at std::size_t maximum.
    std::size_t routedSourceCount = 0U;
    bool truncated = false;
    /// True while at least one bus is soloed, which is what makes effectiveVolume zero for the rest
    /// of the tree - the answer to "why is my sound silent" without reading every solo flag.
    bool anySoloed = false;
    /// Deterministic and name-ordered, which also makes the snapshot order independent of insertion
    /// order and therefore reproducible across runs and platforms.
    std::vector<AudioMixerGroupSnapshotUVE> groups;

    [[nodiscard]] bool operator==(const AudioMixerDiagnosticsUVE&) const noexcept = default;
};

/// Owns the bounded mixer BUS TREE above the audio device. A "mixer group" is one bus: it has a
/// parent (Master when not given), its own volume and pitch multipliers, mute and solo switches,
/// fixed-order gain insert slots, and a count of the sources routed to it.
///
/// The effective values a source receives are the product of its bus's multipliers and every
/// ancestor's up to Master - so ducking Master ducks everything, and ducking "Music" ducks only what
/// is under it. Mute silences a bus and its whole subtree. Solo follows the rule every hardware and
/// host mixer uses: while anything is soloed, a bus stays audible when it is soloed, sits UNDER a
/// soloed bus (its audio flows into the soloed one), or sits ABOVE one (its gain multiplies into it);
/// everything else is silenced. Authoring values are never overwritten by mute or solo, so a muted
/// bus keeps the multipliers it will have when it is unmuted.
///
/// The API this class shipped with (RegisterGroupUVE and friends with no parent argument) keeps its
/// exact meaning: those buses are children of Master, and with no other multiplier in the tree the
/// effective value equals the authored one.
///
/// Bounded and validated: at most `kMaximumAudioMixerGroupsUVE` buses, names of at most
/// `kMaximumAudioMixerGroupNameBytesUVE` bytes without embedded NULs, volume multipliers in [0, 1],
/// pitch multipliers in [0.25, 4]. Removing a bus that still has sources attached is refused because
/// the caller still owns those voices; removing one without sources reparents its children to its own
/// parent, so the tree never keeps a dangling edge. Reparenting is refused when it would make a bus
/// its own ancestor. Master always exists, cannot be removed, and cannot be reparented.
///
/// Thread-safety: not thread-safe, matching the rest of the audio module's main-thread contract.
class AudioMixerGroupUVE final {
public:
    AudioMixerGroupUVE();

    /// Registers `name` under `parentName`; an empty parentName means Master. False when the name is
    /// invalid or taken, either multiplier is out of range, the parent does not exist, or the bus cap
    /// is already reached.
    [[nodiscard]] bool RegisterGroupUVE(std::string_view name, std::string_view parentName,
                                        float volumeMultiplier = 1.0F, float pitchMultiplier = 1.0F);
    /// Registers under Master, the behaviour this overload has always had.
    [[nodiscard]] bool RegisterGroupUVE(std::string_view name, float volumeMultiplier = 1.0F,
                                        float pitchMultiplier = 1.0F);

    /// Refuses Master, unknown buses, and buses with routed sources; otherwise reparents children
    /// onto this bus's parent and erases it.
    [[nodiscard]] bool RemoveGroupUVE(std::string_view name);

    /// Moves `name` under `parentName`; an empty parentName means Master. Refused for Master itself,
    /// for an unknown bus or parent, and for any move that would create a cycle - rejected, never a
    /// crash or a hang on a later walk. Asking for the parent a bus already has succeeds, matching
    /// SetSourceMixerGroupUVE()'s own idempotent contract.
    [[nodiscard]] bool SetGroupParentUVE(std::string_view name, std::string_view parentName);

    [[nodiscard]] bool HasGroupUVE(std::string_view name) const;
    /// Names a bus that exists; anything else resolves to Master, which is also how routing treats
    /// an unknown source group.
    [[nodiscard]] std::string ResolveGroupNameUVE(std::string_view name) const;

    [[nodiscard]] bool SetGroupVolumeUVE(std::string_view name, float volumeMultiplier);
    [[nodiscard]] bool SetGroupPitchUVE(std::string_view name, float pitchMultiplier);
    /// The bus's OWN authored multipliers, as set through the two calls above - not what a source
    /// hears. Use GetEffectiveVolumeUVE()/GetEffectivePitchUVE() for that. Unknown names report 1.
    [[nodiscard]] float GetGroupVolumeUVE(std::string_view name) const;
    [[nodiscard]] float GetGroupPitchUVE(std::string_view name) const;

    [[nodiscard]] bool SetGroupMutedUVE(std::string_view name, bool muted);
    [[nodiscard]] bool SetGroupSoloedUVE(std::string_view name, bool soloed);
    [[nodiscard]] bool IsGroupMutedUVE(std::string_view name) const;
    [[nodiscard]] bool IsGroupSoloedUVE(std::string_view name) const;
    /// True while at least one bus is soloed.
    [[nodiscard]] bool IsAnyGroupSoloedUVE() const;

    /// The parent's name; empty on Master, which is the root. Unknown names report empty.
    [[nodiscard]] std::string GetGroupParentUVE(std::string_view name) const;
    /// Distance from the root: Master 0, its children 1. Unknown names report 0.
    [[nodiscard]] std::size_t GetGroupDepthUVE(std::string_view name) const;

    /// The multiplier a source routed to `name` actually receives, with every ancestor composed and
    /// mute/solo folded in. Unknown names resolve to Master, exactly like routing does.
    [[nodiscard]] float GetEffectiveVolumeUVE(std::string_view name) const;
    /// The pitch multiplier a source routed to `name` receives, composed up the tree and clamped into
    /// [0.25, 4] - the range the device-facing evaluator accepts - so a deep chain can never push a
    /// voice out of range and silence it.
    [[nodiscard]] float GetEffectivePitchUVE(std::string_view name) const;
    /// False when a solo elsewhere silences this bus, or when this bus or one of its ancestors is
    /// muted.
    [[nodiscard]] bool IsGroupAudibleUVE(std::string_view name) const;

    [[nodiscard]] bool AttachSourceUVE(std::string_view name);
    [[nodiscard]] bool DetachSourceUVE(std::string_view name);

    /// Sets one fixed-order gain insert on a bus. Slots default to unity; gains must be finite and
    /// non-negative. False for an unknown bus or an out-of-range slot index.
    [[nodiscard]] bool SetGroupGainSlotUVE(std::string_view name, std::size_t slot, float gain);
    /// Reads one insert; false for an unknown bus or out-of-range slot index.
    [[nodiscard]] bool GetGroupGainSlotUVE(std::string_view name, std::size_t slot, float& outGain) const;
    /// Restores every insert on a bus to unity; false for an unknown bus.
    [[nodiscard]] bool ClearGroupGainSlotsUVE(std::string_view name);

    /// Runs copied PCM through a bus's audio path: its effective volume (ancestors, mute and solo
    /// included) first, then each authored insert slot in ascending index order, through the shared
    /// bounded gain chain. Unity stages are skipped. It never mutates mixer state, so it is safe to
    /// call for many buses per frame; false is returned for an unknown bus, oversized input, or
    /// non-finite samples, and outputSamples is left untouched on failure.
    [[nodiscard]] bool ApplyBusGainChainUVE(std::string_view name, const std::vector<float>& inputSamples,
                                            std::vector<float>& outputSamples) const;

    [[nodiscard]] AudioMixerDiagnosticsUVE GetDiagnosticsUVE(
        std::size_t maximumGroups = kMaximumAudioMixerGroupsUVE) const;

private:
    struct GroupStateUVE final {
        float volumeMultiplier = 1.0F;
        float pitchMultiplier = 1.0F;
        std::size_t sourceCount = 0U;
        /// Empty only on Master, the tree's root.
        std::string parent;
        bool muted = false;
        bool soloed = false;
        /// Fixed-order inserts; unity by default, filled by MakeGroupStateUVE().
        std::array<float, kMaximumAudioBusGainSlotsUVE> gainSlots{};
    };

    /// Builds one bus with unity inserts, so no caller can accidentally create a silent stage.
    [[nodiscard]] static GroupStateUVE MakeGroupStateUVE(float volumeMultiplier, float pitchMultiplier,
                                                         std::string parentName);

    [[nodiscard]] static bool IsValidNameUVE(std::string_view name) noexcept;
    [[nodiscard]] static bool IsValidVolumeMultiplierUVE(float volumeMultiplier) noexcept;
    [[nodiscard]] static bool IsValidPitchMultiplierUVE(float pitchMultiplier) noexcept;

    [[nodiscard]] const GroupStateUVE* FindGroupUVE(std::string_view name) const;
    [[nodiscard]] GroupStateUVE* FindGroupUVE(std::string_view name);
    /// True when walking up from `name` reaches `ancestor`. Hop-capped so a broken invariant can
    /// never turn a walk into a hang.
    [[nodiscard]] bool IsDescendantOfUVE(std::string_view name, std::string_view ancestor) const;
    /// True when `name` or any ancestor is muted.
    [[nodiscard]] bool IsMutedOnPathUVE(std::string_view name) const;
    /// True when `name` or any ancestor is soloed.
    [[nodiscard]] bool IsSoloedOnPathUVE(std::string_view name) const;
    /// True when any bus in the subtree rooted at `name`, including `name`, is soloed.
    [[nodiscard]] bool HasSoloInSubtreeUVE(std::string_view name) const;
    /// The bus's own multipliers composed with every ancestor's, BEFORE mute and solo.
    [[nodiscard]] float ComposeVolumeUVE(std::string_view name) const;
    [[nodiscard]] float ComposePitchUVE(std::string_view name) const;

    std::map<std::string, GroupStateUVE, std::less<>> m_groups;
};

} // namespace UVE::Audio
