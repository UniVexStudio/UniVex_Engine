// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/audio/audio_mixer_group_uve.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "uve/audio/pcm_gain_effect_uve.h"

namespace UVE::Audio {

bool EvaluateAudioMixParametersUVE(const float sourceVolume, const float sourcePitch,
                                    const float groupVolume, const float groupPitch,
                                    const float attenuation, AudioMixParametersUVE& outParameters) noexcept {
    if (!std::isfinite(sourceVolume) || sourceVolume < 0.0F || !std::isfinite(sourcePitch) || sourcePitch < 0.0F ||
        !std::isfinite(groupVolume) || groupVolume < 0.0F || groupVolume > 1.0F || !std::isfinite(groupPitch) ||
        groupPitch < kMinimumAudioMixerPitchMultiplierUVE || groupPitch > kMaximumAudioMixerPitchMultiplierUVE ||
        !std::isfinite(attenuation) || attenuation < 0.0F || attenuation > 1.0F) {
        return false;
    }
    const float pitch = sourcePitch * groupPitch;
    const float gain = sourceVolume * groupVolume * attenuation;
    if (!std::isfinite(pitch) || !std::isfinite(gain)) return false;
    outParameters = AudioMixParametersUVE{std::clamp(gain, 0.0F, 1.0F), pitch};
    return true;
}

AudioMixerGroupUVE::GroupStateUVE AudioMixerGroupUVE::MakeGroupStateUVE(const float volumeMultiplier,
                                                                        const float pitchMultiplier,
                                                                        std::string parentName,
                                                                        const float sampleRateHz) {
    GroupStateUVE state;
    state.volumeMultiplier = volumeMultiplier;
    state.pitchMultiplier = pitchMultiplier;
    state.parent = std::move(parentName);
    state.inserts.SetSampleRateUVE(sampleRateHz);
    return state;
}

AudioMixerGroupUVE::AudioMixerGroupUVE() {
    m_groups.emplace(std::string(kMasterAudioMixerGroupNameUVE),
                     MakeGroupStateUVE(1.0F, 1.0F, std::string{}, m_sampleRate));
}

bool AudioMixerGroupUVE::IsValidNameUVE(const std::string_view name) noexcept {
    return !name.empty() && name.size() <= kMaximumAudioMixerGroupNameBytesUVE &&
           name.find('\0') == std::string_view::npos;
}

bool AudioMixerGroupUVE::IsValidVolumeMultiplierUVE(const float volumeMultiplier) noexcept {
    return std::isfinite(volumeMultiplier) && volumeMultiplier >= 0.0F && volumeMultiplier <= 1.0F;
}

bool AudioMixerGroupUVE::IsValidPitchMultiplierUVE(const float pitchMultiplier) noexcept {
    return std::isfinite(pitchMultiplier) && pitchMultiplier >= kMinimumAudioMixerPitchMultiplierUVE &&
           pitchMultiplier <= kMaximumAudioMixerPitchMultiplierUVE;
}

const AudioMixerGroupUVE::GroupStateUVE* AudioMixerGroupUVE::FindGroupUVE(const std::string_view name) const {
    if (!IsValidNameUVE(name)) return nullptr;
    const auto iterator = m_groups.find(name);
    return iterator == m_groups.end() ? nullptr : &iterator->second;
}

AudioMixerGroupUVE::GroupStateUVE* AudioMixerGroupUVE::FindGroupUVE(const std::string_view name) {
    if (!IsValidNameUVE(name)) return nullptr;
    const auto iterator = m_groups.find(name);
    return iterator == m_groups.end() ? nullptr : &iterator->second;
}

bool AudioMixerGroupUVE::IsDescendantOfUVE(const std::string_view name, const std::string_view ancestor) const {
    std::string_view current = name;
    // Parents are validated on insert and reparent, so the walk is bounded by the bus count; the cap
    // only exists so a future invariant break degrades into `false` instead of a hang.
    for (std::size_t hop = 0U; hop <= m_groups.size(); ++hop) {
        const GroupStateUVE* group = FindGroupUVE(current);
        if (group == nullptr || group->parent.empty()) return false;
        if (group->parent == ancestor) return true;
        current = group->parent;
    }
    return false;
}

bool AudioMixerGroupUVE::IsMutedOnPathUVE(const std::string_view name) const {
    std::string_view current = name;
    for (std::size_t hop = 0U; hop <= m_groups.size(); ++hop) {
        const GroupStateUVE* group = FindGroupUVE(current);
        if (group == nullptr) return false;
        if (group->muted) return true;
        if (group->parent.empty()) return false;
        current = group->parent;
    }
    return false;
}

bool AudioMixerGroupUVE::IsSoloedOnPathUVE(const std::string_view name) const {
    std::string_view current = name;
    for (std::size_t hop = 0U; hop <= m_groups.size(); ++hop) {
        const GroupStateUVE* group = FindGroupUVE(current);
        if (group == nullptr) return false;
        if (group->soloed) return true;
        if (group->parent.empty()) return false;
        current = group->parent;
    }
    return false;
}

bool AudioMixerGroupUVE::HasSoloInSubtreeUVE(const std::string_view name) const {
    for (const auto& [candidateName, candidateState] : m_groups) {
        if (!candidateState.soloed) continue;
        if (candidateName == name || IsDescendantOfUVE(candidateName, name)) return true;
    }
    return false;
}

bool AudioMixerGroupUVE::IsAnyGroupSoloedUVE() const {
    return std::any_of(m_groups.begin(), m_groups.end(),
                       [](const auto& entry) { return entry.second.soloed; });
}

float AudioMixerGroupUVE::ComposeVolumeUVE(const std::string_view name) const {
    float volume = 1.0F;
    std::string_view current = name;
    for (std::size_t hop = 0U; hop <= m_groups.size(); ++hop) {
        const GroupStateUVE* group = FindGroupUVE(current);
        if (group == nullptr) return volume;
        volume *= group->volumeMultiplier;
        if (group->parent.empty()) return volume;
        current = group->parent;
    }
    return volume;
}

float AudioMixerGroupUVE::ComposePitchUVE(const std::string_view name) const {
    float pitch = 1.0F;
    std::string_view current = name;
    for (std::size_t hop = 0U; hop <= m_groups.size(); ++hop) {
        const GroupStateUVE* group = FindGroupUVE(current);
        if (group == nullptr) break;
        pitch *= group->pitchMultiplier;
        if (group->parent.empty()) break;
        current = group->parent;
    }
    // Clamped to the range EvaluateAudioMixParametersUVE() accepts: an unclamped product from a deep
    // chain would fail validation and silence the voice instead of pitching it further.
    return std::clamp(pitch, kMinimumAudioMixerPitchMultiplierUVE, kMaximumAudioMixerPitchMultiplierUVE);
}

bool AudioMixerGroupUVE::IsGroupAudibleUVE(const std::string_view name) const {
    if (FindGroupUVE(name) == nullptr || IsMutedOnPathUVE(name)) return false;
    if (!IsAnyGroupSoloedUVE()) return true;
    // While something is soloed, a bus survives when it carries a soloed bus's audio (a soloed
    // descendant exists) or feeds one (a soloed ancestor exists). Everything else is silenced.
    return HasSoloInSubtreeUVE(name) || IsSoloedOnPathUVE(name);
}

bool AudioMixerGroupUVE::RegisterGroupUVE(const std::string_view name, const std::string_view parentName,
                                          const float volumeMultiplier, const float pitchMultiplier) {
    if (!IsValidNameUVE(name) || !IsValidVolumeMultiplierUVE(volumeMultiplier) ||
        !IsValidPitchMultiplierUVE(pitchMultiplier) || m_groups.size() >= kMaximumAudioMixerGroupsUVE ||
        m_groups.contains(name)) {
        return false;
    }
    const std::string_view resolvedParent =
        parentName.empty() ? kMasterAudioMixerGroupNameUVE : parentName;
    if (!IsValidNameUVE(resolvedParent) || !m_groups.contains(resolvedParent)) return false;
    m_groups.emplace(std::string(name), MakeGroupStateUVE(volumeMultiplier, pitchMultiplier,
                                                          std::string(resolvedParent), m_sampleRate));
    return true;
}

bool AudioMixerGroupUVE::RegisterGroupUVE(const std::string_view name, const float volumeMultiplier,
                                          const float pitchMultiplier) {
    return RegisterGroupUVE(name, kMasterAudioMixerGroupNameUVE, volumeMultiplier, pitchMultiplier);
}

bool AudioMixerGroupUVE::RemoveGroupUVE(const std::string_view name) {
    if (!IsValidNameUVE(name) || name == kMasterAudioMixerGroupNameUVE) {
        return false;
    }
    const auto iterator = m_groups.find(name);
    if (iterator == m_groups.end() || iterator->second.sourceCount != 0U) {
        return false;
    }
    // Copy the survivor first: erasing invalidates the iterator, and an orphaned subtree would leave
    // children pointing at a bus that no longer exists.
    const std::string survivingParent = iterator->second.parent;
    m_groups.erase(iterator);
    for (auto& [childName, childState] : m_groups) {
        if (childState.parent == name) childState.parent = survivingParent;
    }
    return true;
}

bool AudioMixerGroupUVE::SetGroupParentUVE(const std::string_view name, const std::string_view parentName) {
    if (!IsValidNameUVE(name) || name == kMasterAudioMixerGroupNameUVE) {
        return false;
    }
    const auto iterator = m_groups.find(name);
    if (iterator == m_groups.end()) return false;
    const std::string_view resolvedParent =
        parentName.empty() ? kMasterAudioMixerGroupNameUVE : parentName;
    if (!IsValidNameUVE(resolvedParent) || !m_groups.contains(resolvedParent)) return false;
    // Ask for the parent it already has: idempotent success, same as SetSourceMixerGroupUVE().
    if (iterator->second.parent == resolvedParent) return true;
    // Moving under itself, or under one of its own descendants, would make the tree a cycle. Rejected
    // here so every later walk stays finite.
    if (resolvedParent == name || IsDescendantOfUVE(resolvedParent, name)) return false;
    iterator->second.parent = std::string(resolvedParent);
    return true;
}

bool AudioMixerGroupUVE::HasGroupUVE(const std::string_view name) const {
    return FindGroupUVE(name) != nullptr;
}

std::string AudioMixerGroupUVE::ResolveGroupNameUVE(const std::string_view name) const {
    return FindGroupUVE(name) == nullptr ? std::string(kMasterAudioMixerGroupNameUVE) : std::string(name);
}

bool AudioMixerGroupUVE::SetGroupVolumeUVE(const std::string_view name, const float volumeMultiplier) {
    if (!IsValidVolumeMultiplierUVE(volumeMultiplier)) return false;
    GroupStateUVE* group = FindGroupUVE(name);
    if (group == nullptr) return false;
    group->volumeMultiplier = volumeMultiplier;
    return true;
}

bool AudioMixerGroupUVE::SetGroupPitchUVE(const std::string_view name, const float pitchMultiplier) {
    if (!IsValidPitchMultiplierUVE(pitchMultiplier)) return false;
    GroupStateUVE* group = FindGroupUVE(name);
    if (group == nullptr) return false;
    group->pitchMultiplier = pitchMultiplier;
    return true;
}

float AudioMixerGroupUVE::GetGroupVolumeUVE(const std::string_view name) const {
    const GroupStateUVE* group = FindGroupUVE(name);
    return group == nullptr ? 1.0F : group->volumeMultiplier;
}

float AudioMixerGroupUVE::GetGroupPitchUVE(const std::string_view name) const {
    const GroupStateUVE* group = FindGroupUVE(name);
    return group == nullptr ? 1.0F : group->pitchMultiplier;
}

bool AudioMixerGroupUVE::SetGroupMutedUVE(const std::string_view name, const bool muted) {
    GroupStateUVE* group = FindGroupUVE(name);
    if (group == nullptr) return false;
    group->muted = muted;
    return true;
}

bool AudioMixerGroupUVE::SetGroupSoloedUVE(const std::string_view name, const bool soloed) {
    GroupStateUVE* group = FindGroupUVE(name);
    if (group == nullptr) return false;
    group->soloed = soloed;
    return true;
}

bool AudioMixerGroupUVE::IsGroupMutedUVE(const std::string_view name) const {
    const GroupStateUVE* group = FindGroupUVE(name);
    return group != nullptr && group->muted;
}

bool AudioMixerGroupUVE::IsGroupSoloedUVE(const std::string_view name) const {
    const GroupStateUVE* group = FindGroupUVE(name);
    return group != nullptr && group->soloed;
}

std::string AudioMixerGroupUVE::GetGroupParentUVE(const std::string_view name) const {
    const GroupStateUVE* group = FindGroupUVE(name);
    return group == nullptr ? std::string{} : group->parent;
}

std::size_t AudioMixerGroupUVE::GetGroupDepthUVE(const std::string_view name) const {
    const GroupStateUVE* group = FindGroupUVE(name);
    std::size_t depth = 0U;
    while (group != nullptr && !group->parent.empty() && depth <= m_groups.size()) {
        ++depth;
        group = FindGroupUVE(group->parent);
    }
    return depth;
}

float AudioMixerGroupUVE::GetEffectiveVolumeUVE(const std::string_view name) const {
    // Unknown names resolve exactly like routing does, so a typo behaves like Master, which is where
    // that voice's audio actually went.
    const std::string_view resolved = FindGroupUVE(name) == nullptr ? kMasterAudioMixerGroupNameUVE : name;
    return IsGroupAudibleUVE(resolved) ? ComposeVolumeUVE(resolved) : 0.0F;
}

float AudioMixerGroupUVE::GetEffectivePitchUVE(const std::string_view name) const {
    const std::string_view resolved = FindGroupUVE(name) == nullptr ? kMasterAudioMixerGroupNameUVE : name;
    return ComposePitchUVE(resolved);
}

bool AudioMixerGroupUVE::AttachSourceUVE(const std::string_view name) {
    GroupStateUVE* group = FindGroupUVE(name);
    if (group == nullptr || group->sourceCount == std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    ++group->sourceCount;
    return true;
}

bool AudioMixerGroupUVE::DetachSourceUVE(const std::string_view name) {
    GroupStateUVE* group = FindGroupUVE(name);
    if (group == nullptr || group->sourceCount == 0U) {
        return false;
    }
    --group->sourceCount;
    return true;
}

bool AudioMixerGroupUVE::SetGroupInsertUVE(const std::string_view name, const std::size_t slot,
                                             const AudioInsertDescUVE& desc) {
    GroupStateUVE* group = FindGroupUVE(name);
    return group != nullptr && group->inserts.SetInsertUVE(slot, desc);
}

bool AudioMixerGroupUVE::GetGroupInsertUVE(const std::string_view name, const std::size_t slot,
                                            AudioInsertDescUVE& outDesc) const {
    const GroupStateUVE* group = FindGroupUVE(name);
    return group != nullptr && group->inserts.GetInsertUVE(slot, outDesc);
}

bool AudioMixerGroupUVE::ClearGroupInsertUVE(const std::string_view name, const std::size_t slot) {
    GroupStateUVE* group = FindGroupUVE(name);
    return group != nullptr && group->inserts.ClearInsertUVE(slot);
}

bool AudioMixerGroupUVE::SetSampleRateUVE(const float sampleRateHz) {
    if (!std::isfinite(sampleRateHz) || sampleRateHz < kMinimumAudioSampleRateUVE ||
        sampleRateHz > kMaximumAudioSampleRateUVE) {
        return false;
    }
    m_sampleRate = sampleRateHz;
    for (auto& entry : m_groups) entry.second.inserts.SetSampleRateUVE(sampleRateHz);
    return true;
}

float AudioMixerGroupUVE::GetSampleRateUVE() const noexcept { return m_sampleRate; }

bool AudioMixerGroupUVE::ProcessBusChainUVE(const std::string_view name, float* samples,
                                             const std::size_t frameCount) {
    GroupStateUVE* group = FindGroupUVE(name);
    if (group == nullptr) return false;
    if (samples == nullptr || frameCount == 0U) return true;
    // The bus fader, mute and solo are part of the chain: a muted bus produces silence even when it
    // carries a reverb tail, so the inserts see exactly the signal the device would have heard.
    const float volume = GetEffectiveVolumeUVE(name);
    for (std::size_t index = 0U; index < frameCount; ++index) samples[index] *= volume;
    group->inserts.ProcessInPlaceUVE(samples, frameCount);
    return true;
}

bool AudioMixerGroupUVE::ApplyBusChainUVE(const std::string_view name, const std::vector<float>& inputSamples,
                                            std::vector<float>& outputSamples) {
    if (FindGroupUVE(name) == nullptr) return false;
    try {
        outputSamples = inputSamples;
    } catch (...) {
        return false;
    }
    return ProcessBusChainUVE(name, outputSamples.data(), outputSamples.size());
}

AudioMixerDiagnosticsUVE AudioMixerGroupUVE::GetDiagnosticsUVE(const std::size_t maximumGroups) const {
    AudioMixerDiagnosticsUVE diagnostics;
    diagnostics.groupCount = m_groups.size();
    diagnostics.inspectedGroupCount = std::min(maximumGroups, m_groups.size());
    diagnostics.truncated = diagnostics.inspectedGroupCount < diagnostics.groupCount;
    diagnostics.anySoloed = IsAnyGroupSoloedUVE();
    diagnostics.groups.reserve(diagnostics.inspectedGroupCount);

    std::size_t inspected = 0U;
    for (const auto& [name, state] : m_groups) {
        diagnostics.routedSourceCount = SaturatingAddAudioMixerSourceCountUVE(
            diagnostics.routedSourceCount, state.sourceCount);
        if (inspected >= maximumGroups) {
            continue;
        }
        AudioMixerGroupSnapshotUVE snapshot;
        snapshot.name = name;
        snapshot.volumeMultiplier = state.volumeMultiplier;
        snapshot.pitchMultiplier = state.pitchMultiplier;
        snapshot.sourceCount = state.sourceCount;
        snapshot.parent = state.parent;
        snapshot.muted = state.muted;
        snapshot.soloed = state.soloed;
        snapshot.effectiveVolume = GetEffectiveVolumeUVE(name);
        snapshot.effectivePitch = GetEffectivePitchUVE(name);
        snapshot.depth = GetGroupDepthUVE(name);
        diagnostics.groups.push_back(std::move(snapshot));
        ++inspected;
    }
    return diagnostics;
}

} // namespace UVE::Audio
