// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "animation_montage.h"

#include "animation_driver_component.h"
#include "animation_montage_editing.h"
#include "animation_sequencer.h"
#include "quaternion.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace UVE::Scene {
namespace {

[[nodiscard]] double SectionStartUVE(const Asset::AnimationMontageAssetUVE& montage, const std::string_view name) noexcept {
    const Asset::MontageSectionUVE* const section = Asset::FindMontageSectionUVE(montage, name);
    return section != nullptr ? section->time : 0.0;
}

/// Raises the notifies in [from, to) on the way.
void CollectNotifiesUVE(const Asset::AnimationMontageAssetUVE& montage, MontagePlaybackUVE& playback, const double from,
                        const double to, std::vector<MontageEventUVE>& events) {
    if (!(to > from)) {
        return;
    }
    std::vector<const Asset::MontageNotifyUVE*> passed;
    for (const Asset::MontageNotifyUVE& notify : montage.notifies) {
        if (notify.time >= from && notify.time < to) {
            passed.push_back(&notify);
        }
    }
    std::stable_sort(passed.begin(), passed.end(),
                     [](const Asset::MontageNotifyUVE* a, const Asset::MontageNotifyUVE* b) { return a->time < b->time; });
    for (const Asset::MontageNotifyUVE* notify : passed) {
        if (playback.weight < notify->minWeight) {
            continue;   // below its trigger weight threshold (a blend in or out)
        }
        if (notify->triggerChance < 1.0F) {
            // splitmix64: the same playback rolls the same, so a replay is the same.
            std::uint64_t z = (playback.chanceSeed += 0x9E3779B97F4A7C15ULL);
            z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
            z ^= z >> 31U;
            const float roll = static_cast<float>(z >> 40U) / static_cast<float>(1ULL << 24U);
            if (roll >= notify->triggerChance) {
                continue;
            }
        }
        events.push_back(MontageEventUVE{MontageEventUVE::KindUVE::Notify, notify->name});
    }
}

/// Raises the begin / end of every notify state whose inside-ness changed.
void UpdateStatesUVE(const Asset::AnimationMontageAssetUVE& montage, MontagePlaybackUVE& playback,
                     std::vector<MontageEventUVE>& events) {
    playback.activeStates.resize(montage.notifyStates.size(), false);
    const bool running = playback.phase != MontagePhaseUVE::Stopped;
    for (std::size_t i = 0U; i < montage.notifyStates.size(); ++i) {
        const Asset::MontageNotifyStateUVE& state = montage.notifyStates[i];
        const bool inside = running && playback.time >= state.begin && playback.time < state.end;
        if (inside != playback.activeStates[i]) {
            playback.activeStates[i] = inside;
            events.push_back(MontageEventUVE{inside ? MontageEventUVE::KindUVE::NotifyBegin : MontageEventUVE::KindUVE::NotifyEnd,
                                             state.name});
        }
    }
}

void FinishUVE(const Asset::AnimationMontageAssetUVE& montage, MontagePlaybackUVE& playback,
               std::vector<MontageEventUVE>& events) {
    playback.phase = MontagePhaseUVE::Stopped;
    playback.weight = 0.0F;
    UpdateStatesUVE(montage, playback, events);   // every open state closes
    events.push_back(MontageEventUVE{MontageEventUVE::KindUVE::Ended, {}});
}

void BeginBlendOutUVE(const Asset::AnimationMontageAssetUVE& montage, MontagePlaybackUVE& playback,
                      std::vector<MontageEventUVE>& events) {
    if (playback.phase == MontagePhaseUVE::BlendOut || playback.phase == MontagePhaseUVE::Stopped) {
        return;
    }
    if (montage.blendOutSeconds <= 0.0) {
        FinishUVE(montage, playback, events);
        return;
    }
    playback.phase = MontagePhaseUVE::BlendOut;
    playback.phaseTime = 0.0;
}

} // namespace

bool StartMontageUVE(const Asset::AnimationMontageAssetUVE& montage, MontagePlaybackUVE& playback,
                     const std::string_view section) {
    if (montage.segments.empty() || Asset::MontageLengthUVE(montage) <= 0.0) {
        return false;
    }
    std::string start(section);
    if (start.empty()) {
        if (const Asset::MontageSectionUVE* const first = Asset::MontageSectionAtUVE(montage, 0.0)) {
            start = first->name;
        }
    } else if (Asset::FindMontageSectionUVE(montage, start) == nullptr) {
        return false;
    }
    const bool wasPlaying = playback.phase != MontagePhaseUVE::Stopped;
    playback.section = start;
    playback.time = SectionStartUVE(montage, start);
    playback.activeStates.assign(montage.notifyStates.size(), false);
    playback.phaseTime = 0.0;
    if (montage.blendInSeconds <= 0.0) {
        playback.phase = MontagePhaseUVE::Playing;
        playback.weight = 1.0F;
    } else {
        playback.phase = MontagePhaseUVE::BlendIn;
        // Restarted while it plays: the blend picks up from the weight it has, never pops to 0.
        playback.weight = wasPlaying ? playback.weight : 0.0F;
        playback.phaseTime = static_cast<double>(playback.weight) * montage.blendInSeconds;
    }
    return true;
}

bool JumpToMontageSectionUVE(const Asset::AnimationMontageAssetUVE& montage, MontagePlaybackUVE& playback,
                             const std::string_view section) {
    if (playback.phase == MontagePhaseUVE::Stopped || Asset::FindMontageSectionUVE(montage, section) == nullptr) {
        return false;
    }
    playback.section = std::string(section);
    playback.time = SectionStartUVE(montage, section);
    if (playback.phase == MontagePhaseUVE::BlendOut) {
        // A jump during the blend-out (a late combo input) brings the montage back in.
        playback.phase = MontagePhaseUVE::BlendIn;
        playback.phaseTime = static_cast<double>(playback.weight) * montage.blendInSeconds;
    }
    return true;
}

void StopMontageUVE(const Asset::AnimationMontageAssetUVE& montage, MontagePlaybackUVE& playback,
                    std::vector<MontageEventUVE>& events) {
    BeginBlendOutUVE(montage, playback, events);
}

void StepMontageUVE(const Asset::AnimationMontageAssetUVE& montage, MontagePlaybackUVE& playback, const double deltaSeconds,
                    std::vector<MontageEventUVE>& events) {
    if (playback.phase == MontagePhaseUVE::Stopped || !(deltaSeconds >= 0.0)) {
        return;
    }
    // Weight first: the blends run on real time, not on the montage's rate.
    playback.phaseTime += deltaSeconds;
    if (playback.phase == MontagePhaseUVE::BlendIn) {
        const double t = montage.blendInSeconds > 0.0 ? playback.phaseTime / montage.blendInSeconds : 1.0;
        playback.weight = Asset::MontageBlendCurveWeightUVE(montage.blendInCurve, t);
        if (t >= 1.0) {
            playback.phase = MontagePhaseUVE::Playing;
        }
    } else if (playback.phase == MontagePhaseUVE::BlendOut) {
        const double t = montage.blendOutSeconds > 0.0 ? playback.phaseTime / montage.blendOutSeconds : 1.0;
        playback.weight = 1.0F - Asset::MontageBlendCurveWeightUVE(montage.blendOutCurve, t);
        if (t >= 1.0) {
            FinishUVE(montage, playback, events);
            return;
        }
    }

    double from = playback.time;
    double to = from + deltaSeconds * std::max(0.0, montage.rateScale);
    // Through each section into the one it leads to; bounded so a zero-length loop cannot spin.
    for (int guard = 0; guard < 16; ++guard) {
        const double end = Asset::MontageSectionEndUVE(montage, playback.section);
        if (to < end) {
            break;
        }
        CollectNotifiesUVE(montage, playback, from, end, events);
        const Asset::MontageSectionUVE* const current = Asset::FindMontageSectionUVE(montage, playback.section);
        const std::string next = current != nullptr ? current->next : std::string();
        const Asset::MontageSectionUVE* const following = Asset::FindMontageSectionUVE(montage, next);
        if (following == nullptr) {
            // The last section: hold its end (blending out when Auto Blend Out says so).
            from = end;
            to = end;
            if (montage.autoBlendOut) {
                BeginBlendOutUVE(montage, playback, events);
            }
            break;
        }
        const double overflow = to - end;
        playback.section = following->name;
        from = following->time;
        to = following->time + overflow;
    }
    CollectNotifiesUVE(montage, playback, from, to, events);
    playback.time = to;

    // Auto Blend Out starts before the end of the section that ends the montage.
    if (montage.autoBlendOut && playback.phase != MontagePhaseUVE::BlendOut && playback.phase != MontagePhaseUVE::Stopped) {
        const Asset::MontageSectionUVE* const current = Asset::FindMontageSectionUVE(montage, playback.section);
        const bool final = current == nullptr || Asset::FindMontageSectionUVE(montage, current->next) == nullptr;
        const double lead = montage.blendOutTriggerSeconds >= 0.0 ? montage.blendOutTriggerSeconds : montage.blendOutSeconds;
        if (final && Asset::MontageSectionEndUVE(montage, playback.section) - playback.time <= lead) {
            BeginBlendOutUVE(montage, playback, events);
        }
    }
    if (playback.phase != MontagePhaseUVE::Stopped) {
        UpdateStatesUVE(montage, playback, events);
    }
}

std::vector<SkeletonBonePoseUVE> PoseMontageUVE(const Asset::AnimationMontageAssetUVE& montage, const double time,
                                                const AnimationClipResolverUVE& clipFor, const Skeleton3DComponentUVE& skeleton) {
    if (skeleton.bones.empty() || !clipFor) {
        return {};
    }
    const std::optional<Asset::MontageClipTimeUVE> at = Asset::MapMontageTimeUVE(montage, time);
    if (!at.has_value()) {
        return {};
    }
    const Asset::AnimationClipAssetUVE* const clip = clipFor(montage.segments[at->segment].clip);
    if (clip == nullptr || !clip->IsSkeletalUVE()) {
        return {};
    }
    Skeleton3DComponentUVE posed = skeleton;
    posed.pose.clear();
    AnimationDriverComponentUVE mixer{};
    if (montage.rootMotion != Asset::MontageRootMotionUVE::Off) {
        mixer.rootMotion = AnimationRootMotionModeUVE::InPlace;
    }
    if (!PoseSkeletonAtTimeUVE(*clip, at->clipTime, posed, mixer) || posed.pose.size() != skeleton.bones.size()) {
        return {};
    }
    return posed.pose;
}

bool BlendBonePosesUVE(Skeleton3DComponentUVE& skeleton, const std::vector<SkeletonBonePoseUVE>& target, const float weight) {
    if (!(weight > 0.0F) || skeleton.bones.empty() || target.size() != skeleton.bones.size()) {
        return false;
    }
    // What the skeleton holds now: the pose the drivers wrote this pass, else its rest.
    std::vector<SkeletonBonePoseUVE> base = skeleton.pose;
    if (base.size() != skeleton.bones.size()) {
        base.resize(skeleton.bones.size());
        for (std::size_t i = 0U; i < skeleton.bones.size(); ++i) {
            base[i] = SkeletonBonePoseUVE{skeleton.bones[i].localPosition, skeleton.bones[i].localRotation,
                                          skeleton.bones[i].localScale};
        }
    }
    const float w = std::clamp(weight, 0.0F, 1.0F);
    skeleton.pose.resize(base.size());
    for (std::size_t i = 0U; i < base.size(); ++i) {
        const SkeletonBonePoseUVE& a = base[i];
        const SkeletonBonePoseUVE& b = target[i];
        SkeletonBonePoseUVE out;
        out.position = a.position + (b.position - a.position) * w;
        out.scale = a.scale + (b.scale - a.scale) * w;
        if (!Math::TrySlerpUVE(a.rotation, b.rotation, w, out.rotation)) {
            out.rotation = w >= 0.5F ? b.rotation : a.rotation;
        }
        skeleton.pose[i] = out;
    }
    return true;
}

bool BlendMontagePoseUVE(const Asset::AnimationMontageAssetUVE& montage, const double time, const float weight,
                         const AnimationClipResolverUVE& clipFor, Skeleton3DComponentUVE& skeleton) {
    if (!(weight > 0.0F)) {
        return false;
    }
    return BlendBonePosesUVE(skeleton, PoseMontageUVE(montage, time, clipFor, skeleton), weight);
}

} // namespace UVE::Scene
