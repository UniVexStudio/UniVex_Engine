// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "motion_montage.h"

#include "animation_driver_component.h"
#include "motion_montage_editing.h"
#include "animation_sequencer.h"
#include "quaternion.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace UVE::Scene {
namespace {

[[nodiscard]] double SectionStart(const Asset::MotionMontageAsset& montage, const std::string_view name) noexcept {
    const Asset::MontageSection* const section = Asset::FindMontageSection(montage, name);
    return section != nullptr ? section->time : 0.0;
}

/// Raises the notifies in [from, to) on the way.
void CollectNotifies(const Asset::MotionMontageAsset& montage, MontagePlayback& playback, const double from,
                        const double to, std::vector<MontageEvent>& events) {
    if (!(to > from)) {
        return;
    }
    std::vector<const Asset::MontageNotify*> passed;
    for (const Asset::MontageNotify& notify : montage.notifies) {
        if (notify.time >= from && notify.time < to) {
            passed.push_back(&notify);
        }
    }
    std::stable_sort(passed.begin(), passed.end(),
                     [](const Asset::MontageNotify* a, const Asset::MontageNotify* b) { return a->time < b->time; });
    for (const Asset::MontageNotify* notify : passed) {
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
        events.push_back(MontageEvent{MontageEvent::Kind::Notify, notify->name});
    }
}

/// Raises the begin / end of every notify state whose inside-ness changed.
void UpdateStates(const Asset::MotionMontageAsset& montage, MontagePlayback& playback,
                     std::vector<MontageEvent>& events) {
    playback.activeStates.resize(montage.notifyStates.size(), false);
    const bool running = playback.phase != MontagePhase::Stopped;
    for (std::size_t i = 0U; i < montage.notifyStates.size(); ++i) {
        const Asset::MontageNotifyState& state = montage.notifyStates[i];
        const bool inside = running && playback.time >= state.begin && playback.time < state.end;
        if (inside != playback.activeStates[i]) {
            playback.activeStates[i] = inside;
            events.push_back(MontageEvent{inside ? MontageEvent::Kind::NotifyBegin : MontageEvent::Kind::NotifyEnd,
                                             state.name});
        }
    }
}

void Finish(const Asset::MotionMontageAsset& montage, MontagePlayback& playback,
               std::vector<MontageEvent>& events) {
    playback.phase = MontagePhase::Stopped;
    playback.weight = 0.0F;
    UpdateStates(montage, playback, events);   // every open state closes
    events.push_back(MontageEvent{MontageEvent::Kind::Ended, {}});
}

void BeginBlendOut(const Asset::MotionMontageAsset& montage, MontagePlayback& playback,
                      std::vector<MontageEvent>& events) {
    if (playback.phase == MontagePhase::BlendOut || playback.phase == MontagePhase::Stopped) {
        return;
    }
    if (montage.blendOutSeconds <= 0.0) {
        Finish(montage, playback, events);
        return;
    }
    playback.phase = MontagePhase::BlendOut;
    playback.phaseTime = 0.0;
}

} // namespace

bool StartMontage(const Asset::MotionMontageAsset& montage, MontagePlayback& playback,
                     const std::string_view section) {
    if (montage.segments.empty() || Asset::MontageLength(montage) <= 0.0) {
        return false;
    }
    std::string start(section);
    if (start.empty()) {
        if (const Asset::MontageSection* const first = Asset::MontageSectionAt(montage, 0.0)) {
            start = first->name;
        }
    } else if (Asset::FindMontageSection(montage, start) == nullptr) {
        return false;
    }
    const bool wasPlaying = playback.phase != MontagePhase::Stopped;
    playback.section = start;
    playback.time = SectionStart(montage, start);
    playback.activeStates.assign(montage.notifyStates.size(), false);
    playback.phaseTime = 0.0;
    if (montage.blendInSeconds <= 0.0) {
        playback.phase = MontagePhase::Playing;
        playback.weight = 1.0F;
    } else {
        playback.phase = MontagePhase::BlendIn;
        // Restarted while it plays: the blend picks up from the weight it has, never pops to 0.
        playback.weight = wasPlaying ? playback.weight : 0.0F;
        playback.phaseTime = static_cast<double>(playback.weight) * montage.blendInSeconds;
    }
    return true;
}

bool JumpToMontageSection(const Asset::MotionMontageAsset& montage, MontagePlayback& playback,
                             const std::string_view section) {
    if (playback.phase == MontagePhase::Stopped || Asset::FindMontageSection(montage, section) == nullptr) {
        return false;
    }
    playback.section = std::string(section);
    playback.time = SectionStart(montage, section);
    if (playback.phase == MontagePhase::BlendOut) {
        // A jump during the blend-out (a late combo input) brings the montage back in.
        playback.phase = MontagePhase::BlendIn;
        playback.phaseTime = static_cast<double>(playback.weight) * montage.blendInSeconds;
    }
    return true;
}

void StopMontage(const Asset::MotionMontageAsset& montage, MontagePlayback& playback,
                    std::vector<MontageEvent>& events) {
    BeginBlendOut(montage, playback, events);
}

void StepMontage(const Asset::MotionMontageAsset& montage, MontagePlayback& playback, const double deltaSeconds,
                    std::vector<MontageEvent>& events) {
    if (playback.phase == MontagePhase::Stopped || !(deltaSeconds >= 0.0)) {
        return;
    }
    // Weight first: the blends run on real time, not on the montage's rate.
    playback.phaseTime += deltaSeconds;
    if (playback.phase == MontagePhase::BlendIn) {
        const double t = montage.blendInSeconds > 0.0 ? playback.phaseTime / montage.blendInSeconds : 1.0;
        playback.weight = Asset::MontageBlendCurveWeight(montage.blendInCurve, t);
        if (t >= 1.0) {
            playback.phase = MontagePhase::Playing;
        }
    } else if (playback.phase == MontagePhase::BlendOut) {
        const double t = montage.blendOutSeconds > 0.0 ? playback.phaseTime / montage.blendOutSeconds : 1.0;
        playback.weight = 1.0F - Asset::MontageBlendCurveWeight(montage.blendOutCurve, t);
        if (t >= 1.0) {
            Finish(montage, playback, events);
            return;
        }
    }

    double from = playback.time;
    double to = from + deltaSeconds * std::max(0.0, montage.rateScale);
    // Through each section into the one it leads to; bounded so a zero-length loop cannot spin.
    for (int guard = 0; guard < 16; ++guard) {
        const double end = Asset::MontageSectionEnd(montage, playback.section);
        if (to < end) {
            break;
        }
        CollectNotifies(montage, playback, from, end, events);
        const Asset::MontageSection* const current = Asset::FindMontageSection(montage, playback.section);
        const std::string next = current != nullptr ? current->next : std::string();
        const Asset::MontageSection* const following = Asset::FindMontageSection(montage, next);
        if (following == nullptr) {
            // The last section: hold its end (blending out when Auto Blend Out says so).
            from = end;
            to = end;
            if (montage.autoBlendOut) {
                BeginBlendOut(montage, playback, events);
            }
            break;
        }
        const double overflow = to - end;
        playback.section = following->name;
        from = following->time;
        to = following->time + overflow;
    }
    CollectNotifies(montage, playback, from, to, events);
    playback.time = to;

    // Auto Blend Out starts before the end of the section that ends the montage.
    if (montage.autoBlendOut && playback.phase != MontagePhase::BlendOut && playback.phase != MontagePhase::Stopped) {
        const Asset::MontageSection* const current = Asset::FindMontageSection(montage, playback.section);
        const bool final = current == nullptr || Asset::FindMontageSection(montage, current->next) == nullptr;
        const double lead = montage.blendOutTriggerSeconds >= 0.0 ? montage.blendOutTriggerSeconds : montage.blendOutSeconds;
        if (final && Asset::MontageSectionEnd(montage, playback.section) - playback.time <= lead) {
            BeginBlendOut(montage, playback, events);
        }
    }
    if (playback.phase != MontagePhase::Stopped) {
        UpdateStates(montage, playback, events);
    }
}

std::vector<SkeletonBonePose> PoseMontage(const Asset::MotionMontageAsset& montage, const double time,
                                                const AnimationClipResolver& clipFor, const Skeleton3DComponent& skeleton) {
    if (skeleton.bones.empty() || !clipFor) {
        return {};
    }
    const std::optional<Asset::MontageClipTime> at = Asset::MapMontageTime(montage, time);
    if (!at.has_value()) {
        return {};
    }
    const Asset::AnimationClipAsset* const clip = clipFor(montage.segments[at->segment].clip);
    if (clip == nullptr || !clip->IsSkeletal()) {
        return {};
    }
    Skeleton3DComponent posed = skeleton;
    posed.pose.clear();
    AnimationDriverComponent mixer{};
    if (montage.rootMotion != Asset::MontageRootMotion::Off) {
        mixer.rootMotion = AnimationRootMotionMode::InPlace;
    }
    if (!PoseSkeletonAtTime(*clip, at->clipTime, posed, mixer) || posed.pose.size() != skeleton.bones.size()) {
        return {};
    }
    return posed.pose;
}

bool BlendBonePoses(Skeleton3DComponent& skeleton, const std::vector<SkeletonBonePose>& target, const float weight) {
    if (!(weight > 0.0F) || skeleton.bones.empty() || target.size() != skeleton.bones.size()) {
        return false;
    }
    // What the skeleton holds now: the pose the drivers wrote this pass, else its rest.
    std::vector<SkeletonBonePose> base = skeleton.pose;
    if (base.size() != skeleton.bones.size()) {
        base.resize(skeleton.bones.size());
        for (std::size_t i = 0U; i < skeleton.bones.size(); ++i) {
            base[i] = SkeletonBonePose{skeleton.bones[i].localPosition, skeleton.bones[i].localRotation,
                                          skeleton.bones[i].localScale};
        }
    }
    const float w = std::clamp(weight, 0.0F, 1.0F);
    skeleton.pose.resize(base.size());
    for (std::size_t i = 0U; i < base.size(); ++i) {
        const SkeletonBonePose& a = base[i];
        const SkeletonBonePose& b = target[i];
        SkeletonBonePose out;
        out.position = a.position + (b.position - a.position) * w;
        out.scale = a.scale + (b.scale - a.scale) * w;
        if (!Math::TrySlerp(a.rotation, b.rotation, w, out.rotation)) {
            out.rotation = w >= 0.5F ? b.rotation : a.rotation;
        }
        skeleton.pose[i] = out;
    }
    return true;
}

bool BlendMontagePose(const Asset::MotionMontageAsset& montage, const double time, const float weight,
                         const AnimationClipResolver& clipFor, Skeleton3DComponent& skeleton) {
    if (!(weight > 0.0F)) {
        return false;
    }
    return BlendBonePoses(skeleton, PoseMontage(montage, time, clipFor, skeleton), weight);
}

} // namespace UVE::Scene
