// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "motion_montage_editing.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace UVE::Asset {
namespace {

void SortSections(MotionMontageAsset& montage) {
    std::stable_sort(montage.sections.begin(), montage.sections.end(),
                     [](const MontageSection& a, const MontageSection& b) { return a.time < b.time; });
}

[[nodiscard]] double ClampTime(const MotionMontageAsset& montage, const double time) noexcept {
    // A montage with no clip yet still has its first second to lay notifies out on.
    return std::clamp(std::isfinite(time) ? time : 0.0, 0.0, std::max(kMontageEmptyTimelineSeconds, MontageLength(montage)));
}

} // namespace

double MontageSegmentDuration(const MontageSegment& segment) noexcept {
    const double end = segment.clipEnd > 0.0 ? segment.clipEnd : segment.clipLength;
    const double span = std::max(0.0, end - segment.clipStart);
    return segment.rate > 0.0 ? span * static_cast<double>(std::max(1U, segment.loops)) / segment.rate : 0.0;
}

double MontageSegmentStart(const MotionMontageAsset& montage, const std::size_t index) noexcept {
    double start = 0.0;
    for (std::size_t i = 0U; i < index && i < montage.segments.size(); ++i) {
        start += MontageSegmentDuration(montage.segments[i]);
    }
    return start;
}

double MontageLength(const MotionMontageAsset& montage) noexcept {
    return MontageSegmentStart(montage, montage.segments.size());
}

std::optional<MontageClipTime> MapMontageTime(const MotionMontageAsset& montage, const double time) noexcept {
    if (montage.segments.empty()) {
        return std::nullopt;
    }
    double start = 0.0;
    for (std::size_t i = 0U; i < montage.segments.size(); ++i) {
        const MontageSegment& segment = montage.segments[i];
        const double duration = MontageSegmentDuration(segment);
        const bool last = i + 1U == montage.segments.size();
        if (time < start + duration || last) {
            const double end = segment.clipEnd > 0.0 ? segment.clipEnd : segment.clipLength;
            const double span = std::max(0.0, end - segment.clipStart);
            double local = std::clamp(time - start, 0.0, duration) * segment.rate;   // clip seconds into the loops
            if (span > 0.0 && local >= span) {
                // The very end of the last loop shows the clip's end, not its start again.
                local = local >= span * static_cast<double>(std::max(1U, segment.loops)) ? span : std::fmod(local, span);
            }
            return MontageClipTime{i, segment.clipStart + local};
        }
        start += duration;
    }
    return std::nullopt;
}

std::size_t AddMontageSegment(MotionMontageAsset& montage, const AssetGuid clip, const double clipLength,
                                 const std::size_t index) {
    MontageSegment segment;
    segment.clip = clip;
    segment.clipLength = std::max(0.0, clipLength);
    const std::size_t at = std::min(index, montage.segments.size());
    montage.segments.insert(montage.segments.begin() + static_cast<std::ptrdiff_t>(at), segment);
    if (montage.sections.empty()) {
        montage.sections.push_back(MontageSection{"Default", 0.0, {}});
    }
    return at;
}

bool RemoveMontageSegment(MotionMontageAsset& montage, const std::size_t index) {
    if (index >= montage.segments.size()) {
        return false;
    }
    montage.segments.erase(montage.segments.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool MoveMontageSegment(MotionMontageAsset& montage, const std::size_t from, const std::size_t to) {
    if (from >= montage.segments.size() || to >= montage.segments.size()) {
        return false;
    }
    MontageSegment moved = montage.segments[from];
    montage.segments.erase(montage.segments.begin() + static_cast<std::ptrdiff_t>(from));
    montage.segments.insert(montage.segments.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    return true;
}

std::string MakeUniqueMontageSectionName(const MotionMontageAsset& montage, const std::string_view base) {
    const std::string root = base.empty() ? std::string("Section") : std::string(base);
    if (FindMontageSection(montage, root) == nullptr) {
        return root;
    }
    for (int n = 2;; ++n) {
        std::string candidate = root + " " + std::to_string(n);
        if (FindMontageSection(montage, candidate) == nullptr) {
            return candidate;
        }
    }
}

MontageSection* FindMontageSection(MotionMontageAsset& montage, const std::string_view name) noexcept {
    for (MontageSection& section : montage.sections) {
        if (section.name == name) {
            return &section;
        }
    }
    return nullptr;
}

const MontageSection* FindMontageSection(const MotionMontageAsset& montage, const std::string_view name) noexcept {
    for (const MontageSection& section : montage.sections) {
        if (section.name == name) {
            return &section;
        }
    }
    return nullptr;
}

std::string AddMontageSection(MotionMontageAsset& montage, const std::string_view name, const double time) {
    const std::string unique = MakeUniqueMontageSectionName(montage, name);
    const double at = ClampTime(montage, time);
    // The section it cuts into leads into it, the usual way of linking a new section after the one before.
    const MontageSection* const before = MontageSectionAt(montage, at);
    const std::string beforeName = before != nullptr ? before->name : std::string();
    std::string inherited = before != nullptr ? before->next : std::string();
    montage.sections.push_back(MontageSection{unique, at, inherited == beforeName ? std::string() : inherited});
    if (MontageSection* const previous = FindMontageSection(montage, beforeName);
        previous != nullptr && previous->next != previous->name) {
        previous->next = unique;
    }
    SortSections(montage);
    return unique;
}

bool RenameMontageSection(MotionMontageAsset& montage, const std::string_view from, const std::string_view to) {
    if (to.empty() || to.size() > kMaximumMontageNameBytes || FindMontageSection(montage, from) == nullptr ||
        (from != to && FindMontageSection(montage, to) != nullptr)) {
        return false;
    }
    const std::string oldName(from);
    for (MontageSection& section : montage.sections) {
        if (section.name == oldName) {
            section.name = std::string(to);
        }
        if (section.next == oldName) {
            section.next = std::string(to);
        }
    }
    return true;
}

bool RemoveMontageSection(MotionMontageAsset& montage, const std::string_view name) {
    const MontageSection* const found = FindMontageSection(montage, name);
    if (found == nullptr) {
        return false;
    }
    const std::string removed = found->name;
    const std::string ledTo = found->next == removed ? std::string() : found->next;
    montage.sections.erase(std::remove_if(montage.sections.begin(), montage.sections.end(),
                                          [&](const MontageSection& s) { return s.name == removed; }),
                           montage.sections.end());
    for (MontageSection& section : montage.sections) {
        if (section.next == removed) {
            section.next = ledTo;
        }
    }
    return true;
}

bool MoveMontageSection(MotionMontageAsset& montage, const std::string_view name, const double time) {
    MontageSection* const section = FindMontageSection(montage, name);
    if (section == nullptr) {
        return false;
    }
    section->time = ClampTime(montage, time);
    SortSections(montage);
    return true;
}

bool SetMontageSectionNext(MotionMontageAsset& montage, const std::string_view name, const std::string_view next) {
    MontageSection* const section = FindMontageSection(montage, name);
    if (section == nullptr || (!next.empty() && FindMontageSection(montage, next) == nullptr)) {
        return false;
    }
    section->next = std::string(next);
    return true;
}

const MontageSection* MontageSectionAt(const MotionMontageAsset& montage, const double time) noexcept {
    const MontageSection* found = nullptr;
    for (const MontageSection& section : montage.sections) {
        if (section.time <= time + 1e-9 && (found == nullptr || section.time >= found->time)) {
            found = &section;
        }
    }
    if (found == nullptr && !montage.sections.empty()) {
        found = &montage.sections.front();
    }
    return found;
}

double MontageSectionEnd(const MotionMontageAsset& montage, const std::string_view name) noexcept {
    const MontageSection* const section = FindMontageSection(montage, name);
    const double length = MontageLength(montage);
    if (section == nullptr) {
        return length;
    }
    double end = length;
    for (const MontageSection& other : montage.sections) {
        if (&other != section && other.time > section->time + 1e-9) {
            end = std::min(end, other.time);
        }
    }
    return end;
}

std::size_t AddMontageNotify(MotionMontageAsset& montage, const std::string_view name, const double time,
                                const std::uint32_t track) {
    montage.notifies.push_back(MontageNotify{name.empty() ? std::string("Notify") : std::string(name),
                                                ClampTime(montage, time), track});
    montage.notifyTracks = std::max(montage.notifyTracks, track + 1U);
    return montage.notifies.size() - 1U;
}

std::size_t AddMontageNotifyState(MotionMontageAsset& montage, const std::string_view name, const double begin,
                                     const double end, const std::uint32_t track) {
    const double b = ClampTime(montage, std::min(begin, end));
    const double e = ClampTime(montage, std::max(begin, end));
    montage.notifyStates.push_back(
        MontageNotifyState{name.empty() ? std::string("NotifyState") : std::string(name), b, e, track});
    montage.notifyTracks = std::max(montage.notifyTracks, track + 1U);
    return montage.notifyStates.size() - 1U;
}

void SetMontageCurveKey(MotionMontageAsset& montage, const std::string_view curve, const double time,
                           const float value) {
    auto it = std::find_if(montage.curves.begin(), montage.curves.end(),
                           [curve](const MontageCurve& c) { return c.name == curve; });
    if (it == montage.curves.end()) {
        montage.curves.push_back(MontageCurve{curve.empty() ? std::string("Curve") : std::string(curve), {}});
        it = montage.curves.end() - 1;
    }
    std::vector<MontageCurveKey>& keys = it->keys;
    for (MontageCurveKey& key : keys) {
        if (std::abs(key.time - time) < 0.001) {
            key.value = value;
            return;
        }
    }
    keys.push_back(MontageCurveKey{std::max(0.0, time), value});
    std::sort(keys.begin(), keys.end(), [](const MontageCurveKey& a, const MontageCurveKey& b) { return a.time < b.time; });
}

bool RemoveMontageNotifyTrack(MotionMontageAsset& montage, const std::uint32_t track) {
    if (montage.notifyTracks <= 1U || track >= montage.notifyTracks) {
        return false;
    }
    std::erase_if(montage.notifies, [track](const MontageNotify& n) { return n.track == track; });
    std::erase_if(montage.notifyStates, [track](const MontageNotifyState& s) { return s.track == track; });
    for (MontageNotify& n : montage.notifies) {
        n.track -= n.track > track ? 1U : 0U;
    }
    for (MontageNotifyState& s : montage.notifyStates) {
        s.track -= s.track > track ? 1U : 0U;
    }
    if (track < montage.notifyTrackNames.size()) {
        montage.notifyTrackNames.erase(montage.notifyTrackNames.begin() + static_cast<std::ptrdiff_t>(track));
    }
    --montage.notifyTracks;
    return true;
}

std::string MontageNotifyTrackName(const MotionMontageAsset& montage, const std::uint32_t track) {
    if (track < montage.notifyTrackNames.size() && !montage.notifyTrackNames[track].empty()) {
        return montage.notifyTrackNames[track];
    }
    return "Notify " + std::to_string(track + 1U);
}

void SetMontageNotifyTrackName(MotionMontageAsset& montage, const std::uint32_t track, const std::string_view name) {
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

float EvaluateMontageCurve(const MotionMontageAsset& montage, const std::string_view curve, const double time) noexcept {
    for (const MontageCurve& c : montage.curves) {
        if (c.name != curve || c.keys.empty()) {
            continue;
        }
        if (time <= c.keys.front().time) {
            return c.keys.front().value;
        }
        for (std::size_t i = 1U; i < c.keys.size(); ++i) {
            if (time <= c.keys[i].time) {
                const MontageCurveKey& a = c.keys[i - 1U];
                const MontageCurveKey& b = c.keys[i];
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
