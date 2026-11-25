// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "audio_mixer_group.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "pcm_gain_effect.h"

namespace UVE::Audio {

bool EvaluateAudioMixParameters(const float sourceVolume, const float sourcePitch,
                                    const float groupVolume, const float groupPitch,
                                    const float attenuation, AudioMixParameters& outParameters) noexcept {
    if (!std::isfinite(sourceVolume) || sourceVolume < 0.0F || !std::isfinite(sourcePitch) || sourcePitch < 0.0F ||
        !std::isfinite(groupVolume) || groupVolume < 0.0F || groupVolume > 1.0F || !std::isfinite(groupPitch) ||
        groupPitch < kMinimumAudioMixerPitchMultiplier || groupPitch > kMaximumAudioMixerPitchMultiplier ||
        !std::isfinite(attenuation) || attenuation < 0.0F || attenuation > 1.0F) {
        return false;
    }
    const float pitch = sourcePitch * groupPitch;
    const float gain = sourceVolume * groupVolume * attenuation;
    if (!std::isfinite(pitch) || !std::isfinite(gain)) return false;
    outParameters = AudioMixParameters{std::clamp(gain, 0.0F, 1.0F), pitch};
    return true;
}

AudioMixerGroup::GroupState AudioMixerGroup::MakeGroupState(const float volumeMultiplier,
                                                                        const float pitchMultiplier,
                                                                        std::string parentName,
                                                                        const float sampleRateHz) {
    GroupState state;
    state.volumeMultiplier = volumeMultiplier;
    state.pitchMultiplier = pitchMultiplier;
    state.parent = std::move(parentName);
    state.inserts.SetSampleRate(sampleRateHz);
    return state;
}

AudioMixerGroup::AudioMixerGroup() {
    m_groups.emplace(std::string(kMasterAudioMixerGroupName),
                     MakeGroupState(1.0F, 1.0F, std::string{}, m_sampleRate));
}

bool AudioMixerGroup::IsValidName(const std::string_view name) noexcept {
    return !name.empty() && name.size() <= kMaximumAudioMixerGroupNameBytes &&
           name.find('\0') == std::string_view::npos;
}

bool AudioMixerGroup::IsValidVolumeMultiplier(const float volumeMultiplier) noexcept {
    return std::isfinite(volumeMultiplier) && volumeMultiplier >= 0.0F && volumeMultiplier <= 1.0F;
}

bool AudioMixerGroup::IsValidPitchMultiplier(const float pitchMultiplier) noexcept {
    return std::isfinite(pitchMultiplier) && pitchMultiplier >= kMinimumAudioMixerPitchMultiplier &&
           pitchMultiplier <= kMaximumAudioMixerPitchMultiplier;
}

const AudioMixerGroup::GroupState* AudioMixerGroup::FindGroup(const std::string_view name) const {
    if (!IsValidName(name)) return nullptr;
    const auto iterator = m_groups.find(name);
    return iterator == m_groups.end() ? nullptr : &iterator->second;
}

AudioMixerGroup::GroupState* AudioMixerGroup::FindGroup(const std::string_view name) {
    if (!IsValidName(name)) return nullptr;
    const auto iterator = m_groups.find(name);
    return iterator == m_groups.end() ? nullptr : &iterator->second;
}

bool AudioMixerGroup::IsDescendantOf(const std::string_view name, const std::string_view ancestor) const {
    std::string_view current = name;
    // Parents are validated on insert and reparent, so the walk is bounded by the bus count; the cap
    // only exists so a future invariant break degrades into `false` instead of a hang.
    for (std::size_t hop = 0U; hop <= m_groups.size(); ++hop) {
        const GroupState* group = FindGroup(current);
        if (group == nullptr || group->parent.empty()) return false;
        if (group->parent == ancestor) return true;
        current = group->parent;
    }
    return false;
}

bool AudioMixerGroup::IsMutedOnPath(const std::string_view name) const {
    std::string_view current = name;
    for (std::size_t hop = 0U; hop <= m_groups.size(); ++hop) {
        const GroupState* group = FindGroup(current);
        if (group == nullptr) return false;
        if (group->muted) return true;
        if (group->parent.empty()) return false;
        current = group->parent;
    }
    return false;
}

bool AudioMixerGroup::IsSoloedOnPath(const std::string_view name) const {
    std::string_view current = name;
    for (std::size_t hop = 0U; hop <= m_groups.size(); ++hop) {
        const GroupState* group = FindGroup(current);
        if (group == nullptr) return false;
        if (group->soloed) return true;
        if (group->parent.empty()) return false;
        current = group->parent;
    }
    return false;
}

bool AudioMixerGroup::HasSoloInSubtree(const std::string_view name) const {
    for (const auto& [candidateName, candidateState] : m_groups) {
        if (!candidateState.soloed) continue;
        if (candidateName == name || IsDescendantOf(candidateName, name)) return true;
    }
    return false;
}

bool AudioMixerGroup::IsAnyGroupSoloed() const {
    return std::any_of(m_groups.begin(), m_groups.end(),
                       [](const auto& entry) { return entry.second.soloed; });
}

float AudioMixerGroup::ComposeVolume(const std::string_view name) const {
    float volume = 1.0F;
    std::string_view current = name;
    for (std::size_t hop = 0U; hop <= m_groups.size(); ++hop) {
        const GroupState* group = FindGroup(current);
        if (group == nullptr) return volume;
        volume *= group->volumeMultiplier;
        if (group->parent.empty()) return volume;
        current = group->parent;
    }
    return volume;
}

float AudioMixerGroup::ComposePitch(const std::string_view name) const {
    float pitch = 1.0F;
    std::string_view current = name;
    for (std::size_t hop = 0U; hop <= m_groups.size(); ++hop) {
        const GroupState* group = FindGroup(current);
        if (group == nullptr) break;
        pitch *= group->pitchMultiplier;
        if (group->parent.empty()) break;
        current = group->parent;
    }
    // Clamped to the range EvaluateAudioMixParameters() accepts: an unclamped product from a deep
    // chain would fail validation and silence the voice instead of pitching it further.
    return std::clamp(pitch, kMinimumAudioMixerPitchMultiplier, kMaximumAudioMixerPitchMultiplier);
}

bool AudioMixerGroup::IsGroupAudible(const std::string_view name) const {
    if (FindGroup(name) == nullptr || IsMutedOnPath(name)) return false;
    if (!IsAnyGroupSoloed()) return true;
    // While something is soloed, a bus survives when it carries a soloed bus's audio (a soloed
    // descendant exists) or feeds one (a soloed ancestor exists). Everything else is silenced.
    return HasSoloInSubtree(name) || IsSoloedOnPath(name);
}

bool AudioMixerGroup::RegisterGroup(const std::string_view name, const std::string_view parentName,
                                          const float volumeMultiplier, const float pitchMultiplier) {
    if (!IsValidName(name) || !IsValidVolumeMultiplier(volumeMultiplier) ||
        !IsValidPitchMultiplier(pitchMultiplier) || m_groups.size() >= kMaximumAudioMixerGroups ||
        m_groups.contains(name)) {
        return false;
    }
    const std::string_view resolvedParent =
        parentName.empty() ? kMasterAudioMixerGroupName : parentName;
    if (!IsValidName(resolvedParent) || !m_groups.contains(resolvedParent)) return false;
    m_groups.emplace(std::string(name), MakeGroupState(volumeMultiplier, pitchMultiplier,
                                                          std::string(resolvedParent), m_sampleRate));
    return true;
}

bool AudioMixerGroup::RegisterGroup(const std::string_view name, const float volumeMultiplier,
                                          const float pitchMultiplier) {
    return RegisterGroup(name, kMasterAudioMixerGroupName, volumeMultiplier, pitchMultiplier);
}

bool AudioMixerGroup::RemoveGroup(const std::string_view name) {
    if (!IsValidName(name) || name == kMasterAudioMixerGroupName) {
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

bool AudioMixerGroup::SetGroupParent(const std::string_view name, const std::string_view parentName) {
    if (!IsValidName(name) || name == kMasterAudioMixerGroupName) {
        return false;
    }
    const auto iterator = m_groups.find(name);
    if (iterator == m_groups.end()) return false;
    const std::string_view resolvedParent =
        parentName.empty() ? kMasterAudioMixerGroupName : parentName;
    if (!IsValidName(resolvedParent) || !m_groups.contains(resolvedParent)) return false;
    // Ask for the parent it already has: idempotent success, same as SetSourceMixerGroup().
    if (iterator->second.parent == resolvedParent) return true;
    // Moving under itself, or under one of its own descendants, would make the tree a cycle. Rejected
    // here so every later walk stays finite.
    if (resolvedParent == name || IsDescendantOf(resolvedParent, name)) return false;
    iterator->second.parent = std::string(resolvedParent);
    return true;
}

bool AudioMixerGroup::HasGroup(const std::string_view name) const {
    return FindGroup(name) != nullptr;
}

std::string AudioMixerGroup::ResolveGroupName(const std::string_view name) const {
    return FindGroup(name) == nullptr ? std::string(kMasterAudioMixerGroupName) : std::string(name);
}

bool AudioMixerGroup::SetGroupVolume(const std::string_view name, const float volumeMultiplier) {
    if (!IsValidVolumeMultiplier(volumeMultiplier)) return false;
    GroupState* group = FindGroup(name);
    if (group == nullptr) return false;
    group->volumeMultiplier = volumeMultiplier;
    return true;
}

bool AudioMixerGroup::SetGroupPitch(const std::string_view name, const float pitchMultiplier) {
    if (!IsValidPitchMultiplier(pitchMultiplier)) return false;
    GroupState* group = FindGroup(name);
    if (group == nullptr) return false;
    group->pitchMultiplier = pitchMultiplier;
    return true;
}

float AudioMixerGroup::GetGroupVolume(const std::string_view name) const {
    const GroupState* group = FindGroup(name);
    return group == nullptr ? 1.0F : group->volumeMultiplier;
}

float AudioMixerGroup::GetGroupPitch(const std::string_view name) const {
    const GroupState* group = FindGroup(name);
    return group == nullptr ? 1.0F : group->pitchMultiplier;
}

bool AudioMixerGroup::SetGroupMuted(const std::string_view name, const bool muted) {
    GroupState* group = FindGroup(name);
    if (group == nullptr) return false;
    group->muted = muted;
    return true;
}

bool AudioMixerGroup::SetGroupSoloed(const std::string_view name, const bool soloed) {
    GroupState* group = FindGroup(name);
    if (group == nullptr) return false;
    group->soloed = soloed;
    return true;
}

bool AudioMixerGroup::IsGroupMuted(const std::string_view name) const {
    const GroupState* group = FindGroup(name);
    return group != nullptr && group->muted;
}

bool AudioMixerGroup::IsGroupSoloed(const std::string_view name) const {
    const GroupState* group = FindGroup(name);
    return group != nullptr && group->soloed;
}

std::string AudioMixerGroup::GetGroupParent(const std::string_view name) const {
    const GroupState* group = FindGroup(name);
    return group == nullptr ? std::string{} : group->parent;
}

std::size_t AudioMixerGroup::GetGroupDepth(const std::string_view name) const {
    const GroupState* group = FindGroup(name);
    std::size_t depth = 0U;
    while (group != nullptr && !group->parent.empty() && depth <= m_groups.size()) {
        ++depth;
        group = FindGroup(group->parent);
    }
    return depth;
}

float AudioMixerGroup::GetEffectiveVolume(const std::string_view name) const {
    // Unknown names resolve exactly like routing does, so a typo behaves like Master, which is where
    // that voice's audio actually went.
    const std::string_view resolved = FindGroup(name) == nullptr ? kMasterAudioMixerGroupName : name;
    return IsGroupAudible(resolved) ? ComposeVolume(resolved) : 0.0F;
}

float AudioMixerGroup::GetEffectivePitch(const std::string_view name) const {
    const std::string_view resolved = FindGroup(name) == nullptr ? kMasterAudioMixerGroupName : name;
    return ComposePitch(resolved);
}

bool AudioMixerGroup::AttachSource(const std::string_view name) {
    GroupState* group = FindGroup(name);
    if (group == nullptr || group->sourceCount == std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    ++group->sourceCount;
    return true;
}

bool AudioMixerGroup::DetachSource(const std::string_view name) {
    GroupState* group = FindGroup(name);
    if (group == nullptr || group->sourceCount == 0U) {
        return false;
    }
    --group->sourceCount;
    return true;
}

bool AudioMixerGroup::SetGroupInsert(const std::string_view name, const std::size_t slot,
                                             const AudioInsertDesc& desc) {
    GroupState* group = FindGroup(name);
    return group != nullptr && group->inserts.SetInsert(slot, desc);
}

bool AudioMixerGroup::GetGroupInsert(const std::string_view name, const std::size_t slot,
                                            AudioInsertDesc& outDesc) const {
    const GroupState* group = FindGroup(name);
    return group != nullptr && group->inserts.GetInsert(slot, outDesc);
}

bool AudioMixerGroup::ClearGroupInsert(const std::string_view name, const std::size_t slot) {
    GroupState* group = FindGroup(name);
    return group != nullptr && group->inserts.ClearInsert(slot);
}

bool AudioMixerGroup::SetSampleRate(const float sampleRateHz) {
    if (!std::isfinite(sampleRateHz) || sampleRateHz < kMinimumAudioSampleRate ||
        sampleRateHz > kMaximumAudioSampleRate) {
        return false;
    }
    m_sampleRate = sampleRateHz;
    for (auto& entry : m_groups) entry.second.inserts.SetSampleRate(sampleRateHz);
    return true;
}

float AudioMixerGroup::GetSampleRate() const noexcept { return m_sampleRate; }

bool AudioMixerGroup::ProcessBusChain(const std::string_view name, float* samples,
                                             const std::size_t frameCount) {
    GroupState* group = FindGroup(name);
    if (group == nullptr) return false;
    if (samples == nullptr || frameCount == 0U) return true;
    // The bus fader, mute and solo are part of the chain: a muted bus produces silence even when it
    // carries a reverb tail, so the inserts see exactly the signal the device would have heard.
    const float volume = GetEffectiveVolume(name);
    for (std::size_t index = 0U; index < frameCount; ++index) samples[index] *= volume;
    group->inserts.ProcessInPlace(samples, frameCount);
    return true;
}

bool AudioMixerGroup::ApplyBusChain(const std::string_view name, const std::vector<float>& inputSamples,
                                            std::vector<float>& outputSamples) {
    if (FindGroup(name) == nullptr) return false;
    try {
        outputSamples = inputSamples;
    } catch (...) {
        return false;
    }
    return ProcessBusChain(name, outputSamples.data(), outputSamples.size());
}

AudioMixerDiagnostics AudioMixerGroup::GetDiagnostics(const std::size_t maximumGroups) const {
    AudioMixerDiagnostics diagnostics;
    diagnostics.groupCount = m_groups.size();
    diagnostics.inspectedGroupCount = std::min(maximumGroups, m_groups.size());
    diagnostics.truncated = diagnostics.inspectedGroupCount < diagnostics.groupCount;
    diagnostics.anySoloed = IsAnyGroupSoloed();
    diagnostics.groups.reserve(diagnostics.inspectedGroupCount);

    std::size_t inspected = 0U;
    for (const auto& [name, state] : m_groups) {
        diagnostics.routedSourceCount = SaturatingAddAudioMixerSourceCount(
            diagnostics.routedSourceCount, state.sourceCount);
        if (inspected >= maximumGroups) {
            continue;
        }
        AudioMixerGroupSnapshot snapshot;
        snapshot.name = name;
        snapshot.volumeMultiplier = state.volumeMultiplier;
        snapshot.pitchMultiplier = state.pitchMultiplier;
        snapshot.sourceCount = state.sourceCount;
        snapshot.parent = state.parent;
        snapshot.muted = state.muted;
        snapshot.soloed = state.soloed;
        snapshot.effectiveVolume = GetEffectiveVolume(name);
        snapshot.effectivePitch = GetEffectivePitch(name);
        snapshot.depth = GetGroupDepth(name);
        diagnostics.groups.push_back(std::move(snapshot));
        ++inspected;
    }
    return diagnostics;
}

} // namespace UVE::Audio
