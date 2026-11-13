// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "animation_montage_editing.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace UVE::Asset {
namespace {

void SortSectionsUVE(AnimationMontageAssetUVE& montage) {
    std::stable_sort(montage.sections.begin(), montage.sections.end(),
                     [](const MontageSectionUVE& a, const MontageSectionUVE& b) { return a.time < b.time; });
}

[[nodiscard]] double ClampTimeUVE(const AnimationMontageAssetUVE& montage, const double time) noexcept {
    // A montage with no clip yet still has its first second to lay notifies out on.
    return std::clamp(std::isfinite(time) ? time : 0.0, 0.0, std::max(kMontageEmptyTimelineSecondsUVE, MontageLengthUVE(montage)));
}

} // namespace

double MontageSegmentDurationUVE(const MontageSegmentUVE& segment) noexcept {
    const double end = segment.clipEnd > 0.0 ? segment.clipEnd : segment.clipLength;
    const double span = std::max(0.0, end - segment.clipStart);
    return segment.rate > 0.0 ? span * static_cast<double>(std::max(1U, segment.loops)) / segment.rate : 0.0;
}

double MontageSegmentStartUVE(const AnimationMontageAssetUVE& montage, const std::size_t index) noexcept {
    double start = 0.0;
    for (std::size_t i = 0U; i < index && i < montage.segments.size(); ++i) {
        start += MontageSegmentDurationUVE(montage.segments[i]);
    }
    return start;
}

double MontageLengthUVE(const AnimationMontageAssetUVE& montage) noexcept {
    return MontageSegmentStartUVE(montage, montage.segments.size());
}

std::optional<MontageClipTimeUVE> MapMontageTimeUVE(const AnimationMontageAssetUVE& montage, const double time) noexcept {
    if (montage.segments.empty()) {
        return std::nullopt;
    }
    double start = 0.0;
    for (std::size_t i = 0U; i < montage.segments.size(); ++i) {
        const MontageSegmentUVE& segment = montage.segments[i];
        const double duration = MontageSegmentDurationUVE(segment);
        const bool last = i + 1U == montage.segments.size();
        if (time < start + duration || last) {
            const double end = segment.clipEnd > 0.0 ? segment.clipEnd : segment.clipLength;
            const double span = std::max(0.0, end - segment.clipStart);
            double local = std::clamp(time - start, 0.0, duration) * segment.rate;   // clip seconds into the loops
            if (span > 0.0 && local >= span) {
                // The very end of the last loop shows the clip's end, not its start again.
                local = local >= span * static_cast<double>(std::max(1U, segment.loops)) ? span : std::fmod(local, span);
            }
            return MontageClipTimeUVE{i, segment.clipStart + local};
        }
        start += duration;
    }
    return std::nullopt;
}

std::size_t AddMontageSegmentUVE(AnimationMontageAssetUVE& montage, const AssetGuidUVE clip, const double clipLength,
                                 const std::size_t index) {
    MontageSegmentUVE segment;
    segment.clip = clip;
    segment.clipLength = std::max(0.0, clipLength);
    const std::size_t at = std::min(index, montage.segments.size());
    montage.segments.insert(montage.segments.begin() + static_cast<std::ptrdiff_t>(at), segment);
    if (montage.sections.empty()) {
        montage.sections.push_back(MontageSectionUVE{"Default", 0.0, {}});
    }
    return at;
}

bool RemoveMontageSegmentUVE(AnimationMontageAssetUVE& montage, const std::size_t index) {
    if (index >= montage.segments.size()) {
        return false;
    }
    montage.segments.erase(montage.segments.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool MoveMontageSegmentUVE(AnimationMontageAssetUVE& montage, const std::size_t from, const std::size_t to) {
    if (from >= montage.segments.size() || to >= montage.segments.size()) {
        return false;
    }
    MontageSegmentUVE moved = montage.segments[from];
    montage.segments.erase(montage.segments.begin() + static_cast<std::ptrdiff_t>(from));
    montage.segments.insert(montage.segments.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    return true;
}

std::string MakeUniqueMontageSectionNameUVE(const AnimationMontageAssetUVE& montage, const std::string_view base) {
    const std::string root = base.empty() ? std::string("Section") : std::string(base);
    if (FindMontageSectionUVE(montage, root) == nullptr) {
        return root;
    }
    for (int n = 2;; ++n) {
        std::string candidate = root + " " + std::to_string(n);
        if (FindMontageSectionUVE(montage, candidate) == nullptr) {
            return candidate;
        }
    }
}

MontageSectionUVE* FindMontageSectionUVE(AnimationMontageAssetUVE& montage, const std::string_view name) noexcept {
    for (MontageSectionUVE& section : montage.sections) {
        if (section.name == name) {
            return &section;
        }
    }
    return nullptr;
}

const MontageSectionUVE* FindMontageSectionUVE(const AnimationMontageAssetUVE& montage, const std::string_view name) noexcept {
    for (const MontageSectionUVE& section : montage.sections) {
        if (section.name == name) {
            return &section;
        }
    }
    return nullptr;
}

std::string AddMontageSectionUVE(AnimationMontageAssetUVE& montage, const std::string_view name, const double time) {
    const std::string unique = MakeUniqueMontageSectionNameUVE(montage, name);
    const double at = ClampTimeUVE(montage, time);
    // The section it cuts into leads into it, the usual way of linking a new section after the one before.
    const MontageSectionUVE* const before = MontageSectionAtUVE(montage, at);
    const std::string beforeName = before != nullptr ? before->name : std::string();
    std::string inherited = before != nullptr ? before->next : std::string();
    montage.sections.push_back(MontageSectionUVE{unique, at, inherited == beforeName ? std::string() : inherited});
    if (MontageSectionUVE* const previous = FindMontageSectionUVE(montage, beforeName);
        previous != nullptr && previous->next != previous->name) {
        previous->next = unique;
    }
    SortSectionsUVE(montage);
    return unique;
}

bool RenameMontageSectionUVE(AnimationMontageAssetUVE& montage, const std::string_view from, const std::string_view to) {
    if (to.empty() || to.size() > kMaximumMontageNameBytesUVE || FindMontageSectionUVE(montage, from) == nullptr ||
        (from != to && FindMontageSectionUVE(montage, to) != nullptr)) {
        return false;
    }
    const std::string oldName(from);
    for (MontageSectionUVE& section : montage.sections) {
        if (section.name == oldName) {
            section.name = std::string(to);
        }
        if (section.next == oldName) {
            section.next = std::string(to);
        }
    }
    return true;
}

bool RemoveMontageSectionUVE(AnimationMontageAssetUVE& montage, const std::string_view name) {
    const MontageSectionUVE* const found = FindMontageSectionUVE(montage, name);
    if (found == nullptr) {
        return false;
    }
    const std::string removed = found->name;
    const std::string ledTo = found->next == removed ? std::string() : found->next;
    montage.sections.erase(std::remove_if(montage.sections.begin(), montage.sections.end(),
                                          [&](const MontageSectionUVE& s) { return s.name == removed; }),
                           montage.sections.end());
    for (MontageSectionUVE& section : montage.sections) {
        if (section.next == removed) {
            section.next = ledTo;
        }
    }
    return true;
}

bool MoveMontageSectionUVE(AnimationMontageAssetUVE& montage, const std::string_view name, const double time) {
    MontageSectionUVE* const section = FindMontageSectionUVE(montage, name);
    if (section == nullptr) {
        return false;
    }
    section->time = ClampTimeUVE(montage, time);
    SortSectionsUVE(montage);
    return true;
}

bool SetMontageSectionNextUVE(AnimationMontageAssetUVE& montage, const std::string_view name, const std::string_view next) {
    MontageSectionUVE* const section = FindMontageSectionUVE(montage, name);
    if (section == nullptr || (!next.empty() && FindMontageSectionUVE(montage, next) == nullptr)) {
        return false;
    }
    section->next = std::string(next);
    return true;
}

const MontageSectionUVE* MontageSectionAtUVE(const AnimationMontageAssetUVE& montage, const double time) noexcept {
    const MontageSectionUVE* found = nullptr;
    for (const MontageSectionUVE& section : montage.sections) {
        if (section.time <= time + 1e-9 && (found == nullptr || section.time >= found->time)) {
            found = &section;
        }
    }
    if (found == nullptr && !montage.sections.empty()) {
        found = &montage.sections.front();
    }
    return found;
}

double MontageSectionEndUVE(const AnimationMontageAssetUVE& montage, const std::string_view name) noexcept {
    const MontageSectionUVE* const section = FindMontageSectionUVE(montage, name);
    const double length = MontageLengthUVE(montage);
    if (section == nullptr) {
        return length;
    }
    double end = length;
    for (const MontageSectionUVE& other : montage.sections) {
        if (&other != section && other.time > section->time + 1e-9) {
            end = std::min(end, other.time);
        }
    }
    return end;
}

std::size_t AddMontageNotifyUVE(AnimationMontageAssetUVE& montage, const std::string_view name, const double time,
                                const std::uint32_t track) {
    montage.notifies.push_back(MontageNotifyUVE{name.empty() ? std::string("Notify") : std::string(name),
                                                ClampTimeUVE(montage, time), track});
    montage.notifyTracks = std::max(montage.notifyTracks, track + 1U);
    return montage.notifies.size() - 1U;
}

std::size_t AddMontageNotifyStateUVE(AnimationMontageAssetUVE& montage, const std::string_view name, const double begin,
                                     const double end, const std::uint32_t track) {
    const double b = ClampTimeUVE(montage, std::min(begin, end));
    const double e = ClampTimeUVE(montage, std::max(begin, end));
    montage.notifyStates.push_back(
        MontageNotifyStateUVE{name.empty() ? std::string("NotifyState") : std::string(name), b, e, track});
    montage.notifyTracks = std::max(montage.notifyTracks, track + 1U);
    return montage.notifyStates.size() - 1U;
}

void SetMontageCurveKeyUVE(AnimationMontageAssetUVE& montage, const std::string_view curve, const double time,
                           const float value) {
    auto it = std::find_if(montage.curves.begin(), montage.curves.end(),
                           [curve](const MontageCurveUVE& c) { return c.name == curve; });
    if (it == montage.curves.end()) {
        montage.curves.push_back(MontageCurveUVE{curve.empty() ? std::string("Curve") : std::string(curve), {}});
        it = montage.curves.end() - 1;
    }
    std::vector<MontageCurveKeyUVE>& keys = it->keys;
    for (MontageCurveKeyUVE& key : keys) {
        if (std::abs(key.time - time) < 0.001) {
            key.value = value;
            return;
        }
    }
    keys.push_back(MontageCurveKeyUVE{std::max(0.0, time), value});
    std::sort(keys.begin(), keys.end(), [](const MontageCurveKeyUVE& a, const MontageCurveKeyUVE& b) { return a.time < b.time; });
}

bool RemoveMontageNotifyTrackUVE(AnimationMontageAssetUVE& montage, const std::uint32_t track) {
    if (montage.notifyTracks <= 1U || track >= montage.notifyTracks) {
        return false;
    }
    std::erase_if(montage.notifies, [track](const MontageNotifyUVE& n) { return n.track == track; });
    std::erase_if(montage.notifyStates, [track](const MontageNotifyStateUVE& s) { return s.track == track; });
    for (MontageNotifyUVE& n : montage.notifies) {
        n.track -= n.track > track ? 1U : 0U;
    }
    for (MontageNotifyStateUVE& s : montage.notifyStates) {
        s.track -= s.track > track ? 1U : 0U;
    }
    if (track < montage.notifyTrackNames.size()) {
        montage.notifyTrackNames.erase(montage.notifyTrackNames.begin() + static_cast<std::ptrdiff_t>(track));
    }
    --montage.notifyTracks;
    return true;
}

std::string MontageNotifyTrackNameUVE(const AnimationMontageAssetUVE& montage, const std::uint32_t track) {
    if (track < montage.notifyTrackNames.size() && !montage.notifyTrackNames[track].empty()) {
        return montage.notifyTrackNames[track];
    }
    return "Notify " + std::to_string(track + 1U);
}

void SetMontageNotifyTrackNameUVE(AnimationMontageAssetUVE& montage, const std::uint32_t track, const std::string_view name) {
    if (track >= 64U) {
        return;
    }
    if (montage.notifyTrackNames.size() <= track) {
        montage.notifyTrackNames.resize(track + 1U);
    }
    montage.notifyTrackNames[track] = std::string(name);
    while (!montage.notifyTrackNames.empty() && montage.notifyTrackNames.back().empty()) {
        montage.notifyTrackNames.pop_back();
    }
}

float EvaluateMontageCurveUVE(const AnimationMontageAssetUVE& montage, const std::string_view curve, const double time) noexcept {
    for (const MontageCurveUVE& c : montage.curves) {
        if (c.name != curve || c.keys.empty()) {
            continue;
        }
        if (time <= c.keys.front().time) {
            return c.keys.front().value;
        }
        for (std::size_t i = 1U; i < c.keys.size(); ++i) {
            if (time <= c.keys[i].time) {
                const MontageCurveKeyUVE& a = c.keys[i - 1U];
                const MontageCurveKeyUVE& b = c.keys[i];
                const double span = b.time - a.time;
                const double t = span > 0.0 ? (time - a.time) / span : 1.0;
                return static_cast<float>(a.value + (b.value - a.value) * t);
            }
        }
        return c.keys.back().value;
    }
    return 0.0F;
}

} // namespace UVE::Asset
