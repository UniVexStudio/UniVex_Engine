// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "motion_montage_asset.h"

#include "logging_macros.h"
#include "uve_file_envelope.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace UVE::Asset {
namespace {

[[nodiscard]] bool Finite(const double value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool NameOk(const std::string& name) noexcept {
    return !name.empty() && name.size() <= kMaximumMontageNameBytes;
}

} // namespace

float MontageBlendCurveWeight(const MontageBlendCurve curve, const double t) noexcept {
    const float x = static_cast<float>(std::clamp(t, 0.0, 1.0));
    switch (curve) {
        case MontageBlendCurve::EaseIn: return x * x;
        case MontageBlendCurve::EaseOut: return 1.0F - (1.0F - x) * (1.0F - x);
        case MontageBlendCurve::EaseInOut: return x * x * (3.0F - 2.0F * x);
        case MontageBlendCurve::Linear: break;
    }
    return x;
}

bool IsMotionMontageAssetValid(const MotionMontageAsset& montage) noexcept {
    if (montage.slot.size() > kMaximumMontageNameBytes || montage.segments.size() > kMaximumMontageSegments ||
        montage.sections.size() > kMaximumMontageSections ||
        montage.notifies.size() + montage.notifyStates.size() > kMaximumMontageNotifies ||
        montage.curves.size() > kMaximumMontageCurves || montage.notifyTracks > 64U || montage.notifyTrackNames.size() > 64U) {
        return false;
    }
    for (const std::string& name : montage.notifyTrackNames) {
        if (name.size() > kMaximumMontageNameBytes) {
            return false;
        }
    }
    if (!Finite(montage.blendInSeconds) || montage.blendInSeconds < 0.0 || !Finite(montage.blendOutSeconds) ||
        montage.blendOutSeconds < 0.0 || !Finite(montage.blendOutTriggerSeconds) || !Finite(montage.rateScale) ||
        montage.rateScale <= 0.0 || montage.rootMotion > MontageRootMotion::ApplyToTarget ||
        montage.blendInCurve > MontageBlendCurve::EaseInOut || montage.blendOutCurve > MontageBlendCurve::EaseInOut) {
        return false;
    }
    for (const MontageSegment& segment : montage.segments) {
        if (!Finite(segment.clipStart) || !Finite(segment.clipEnd) || !Finite(segment.rate) ||
            !Finite(segment.clipLength) || segment.clipStart < 0.0 || segment.rate <= 0.0 || segment.loops == 0U ||
            segment.loops > 1000U || segment.clipLength < 0.0) {
            return false;
        }
    }
    std::set<std::string> names;
    for (const MontageSection& section : montage.sections) {
        if (!NameOk(section.name) || !Finite(section.time) || section.time < 0.0 ||
            section.next.size() > kMaximumMontageNameBytes || !names.insert(section.name).second) {
            return false;
        }
    }
    for (const MontageNotify& notify : montage.notifies) {
        if (!NameOk(notify.name) || !Finite(notify.time) || notify.time < 0.0 || !std::isfinite(notify.triggerChance) ||
            notify.triggerChance < 0.0F || notify.triggerChance > 1.0F || !std::isfinite(notify.minWeight) ||
            notify.minWeight < 0.0F || notify.minWeight > 1.0F || notify.colour > 0xFFFFFFU) {
            return false;
        }
    }
    for (const MontageNotifyState& state : montage.notifyStates) {
        if (!NameOk(state.name) || !Finite(state.begin) || !Finite(state.end) || state.begin < 0.0 ||
            state.end < state.begin) {
            return false;
        }
    }
    for (const MontageCurve& curve : montage.curves) {
        if (!NameOk(curve.name) || curve.keys.size() > kMaximumMontageCurveKeys) {
            return false;
        }
        for (const MontageCurveKey& key : curve.keys) {
            if (!Finite(key.time) || !std::isfinite(key.value)) {
                return false;
            }
        }
    }
    return true;
}

bool SaveMotionMontageAsset(const MotionMontageAsset& montage, const std::filesystem::path& path) {
    if (!IsMotionMontageAssetValid(montage)) {
        UVE_ERROR("MotionMontageAsset: rejected an invalid montage before writing {}", path.string());
        return false;
    }
    const MotionMontageAsset defaults{};
    nlohmann::json payload;
    payload["format"] = "uve-montage-v1";
    if (montage.slot != defaults.slot) {
        payload["slot"] = montage.slot;
    }
    if (montage.previewEntity != kInvalidAssetGuid) {
        payload["previewEntity"] = montage.previewEntity.value;
    }
    nlohmann::json segments = nlohmann::json::array();
    for (const MontageSegment& segment : montage.segments) {
        nlohmann::json s{{"clip", segment.clip.value}, {"clipStart", segment.clipStart}, {"clipEnd", segment.clipEnd},
                         {"clipLength", segment.clipLength}};
        if (segment.rate != 1.0) {
            s["rate"] = segment.rate;
        }
        if (segment.loops != 1U) {
            s["loops"] = segment.loops;
        }
        segments.push_back(std::move(s));
    }
    payload["segments"] = std::move(segments);
    nlohmann::json sections = nlohmann::json::array();
    for (const MontageSection& section : montage.sections) {
        sections.push_back(nlohmann::json{{"name", section.name}, {"time", section.time}, {"next", section.next}});
    }
    payload["sections"] = std::move(sections);
    nlohmann::json notifies = nlohmann::json::array();
    for (const MontageNotify& notify : montage.notifies) {
        nlohmann::json entry{{"name", notify.name}, {"time", notify.time}, {"track", notify.track}};
        if (notify.triggerChance != 1.0F) {
            entry["chance"] = notify.triggerChance;
        }
        if (notify.minWeight != 0.0F) {
            entry["minWeight"] = notify.minWeight;
        }
        if (notify.colour != 0U) {
            entry["colour"] = notify.colour;
        }
        notifies.push_back(std::move(entry));
    }
    payload["notifies"] = std::move(notifies);
    nlohmann::json states = nlohmann::json::array();
    for (const MontageNotifyState& state : montage.notifyStates) {
        states.push_back(nlohmann::json{{"name", state.name}, {"begin", state.begin}, {"end", state.end}, {"track", state.track}});
    }
    payload["notifyStates"] = std::move(states);
    if (!montage.curves.empty()) {
        nlohmann::json curves = nlohmann::json::array();
        for (const MontageCurve& curve : montage.curves) {
            nlohmann::json keys = nlohmann::json::array();
            for (const MontageCurveKey& key : curve.keys) {
                keys.push_back(nlohmann::json::array({key.time, key.value}));
            }
            curves.push_back(nlohmann::json{{"name", curve.name}, {"keys", std::move(keys)}});
        }
        payload["curves"] = std::move(curves);
    }
    if (montage.notifyTracks != defaults.notifyTracks) {
        payload["notifyTracks"] = montage.notifyTracks;
    }
    if (!montage.notifyTrackNames.empty()) {
        payload["trackNames"] = montage.notifyTrackNames;
    }
    if (montage.blendInSeconds != defaults.blendInSeconds) {
        payload["blendIn"] = montage.blendInSeconds;
    }
    if (montage.blendOutSeconds != defaults.blendOutSeconds) {
        payload["blendOut"] = montage.blendOutSeconds;
    }
    if (montage.blendInCurve != defaults.blendInCurve) {
        payload["blendInCurve"] = static_cast<std::uint8_t>(montage.blendInCurve);
    }
    if (montage.blendOutCurve != defaults.blendOutCurve) {
        payload["blendOutCurve"] = static_cast<std::uint8_t>(montage.blendOutCurve);
    }
    if (montage.blendOutTriggerSeconds != defaults.blendOutTriggerSeconds) {
        payload["blendOutTrigger"] = montage.blendOutTriggerSeconds;
    }
    if (montage.autoBlendOut != defaults.autoBlendOut) {
        payload["autoBlendOut"] = montage.autoBlendOut;
    }
    if (montage.rateScale != defaults.rateScale) {
        payload["rateScale"] = montage.rateScale;
    }
    if (montage.rootMotion != defaults.rootMotion) {
        payload["rootMotion"] = static_cast<std::uint8_t>(montage.rootMotion);
    }
    const std::string text = payload.dump();
    const auto* const bytes = reinterpret_cast<const std::byte*>(text.data());
    return WriteUveFile(path, AssetKind::MotionMontage, std::vector<std::byte>(bytes, bytes + text.size()));
}

bool LoadMotionMontageAsset(const std::filesystem::path& path, MotionMontageAsset& outMontage) {
    const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(path);
    if (!file.has_value()) {
        return false;
    }
    if (file->first.assetType != AssetKind::MotionMontage) {
        UVE_ERROR("MotionMontageAsset: \"{}\" is not a montage file", path.string());
        return false;
    }
    const std::string text(reinterpret_cast<const char*>(file->second.data()), file->second.size());
    MotionMontageAsset montage;
    try {
        const nlohmann::json payload = nlohmann::json::parse(text);
        if (payload.value("format", std::string{}) != "uve-montage-v1") {
            UVE_ERROR("MotionMontageAsset: \"{}\" has an unknown format", path.string());
            return false;
        }
        montage.slot = payload.value("slot", montage.slot);
        montage.previewEntity = AssetGuid{payload.value("previewEntity", std::uint64_t{0})};
        for (const nlohmann::json& s : payload.value("segments", nlohmann::json::array())) {
            MontageSegment segment;
            segment.clip = AssetGuid{s.at("clip").get<std::uint64_t>()};
            segment.clipStart = s.value("clipStart", 0.0);
            segment.clipEnd = s.value("clipEnd", 0.0);
            segment.clipLength = s.value("clipLength", 0.0);
            segment.rate = s.value("rate", 1.0);
            segment.loops = s.value("loops", 1U);
            montage.segments.push_back(segment);
        }
        for (const nlohmann::json& s : payload.value("sections", nlohmann::json::array())) {
            montage.sections.push_back(MontageSection{s.at("name").get<std::string>(), s.value("time", 0.0),
                                                         s.value("next", std::string{})});
        }
        for (const nlohmann::json& n : payload.value("notifies", nlohmann::json::array())) {
            MontageNotify notify{n.at("name").get<std::string>(), n.value("time", 0.0), n.value("track", 0U)};
            notify.triggerChance = n.value("chance", 1.0F);
            notify.minWeight = n.value("minWeight", 0.0F);
            notify.colour = n.value("colour", 0U);
            montage.notifies.push_back(std::move(notify));
        }
        for (const nlohmann::json& n : payload.value("notifyStates", nlohmann::json::array())) {
            montage.notifyStates.push_back(MontageNotifyState{n.at("name").get<std::string>(), n.value("begin", 0.0),
                                                                 n.value("end", 0.0), n.value("track", 0U)});
        }
        for (const nlohmann::json& c : payload.value("curves", nlohmann::json::array())) {
            MontageCurve curve;
            curve.name = c.at("name").get<std::string>();
            for (const nlohmann::json& key : c.value("keys", nlohmann::json::array())) {
                curve.keys.push_back(MontageCurveKey{key.at(0).get<double>(), key.at(1).get<float>()});
            }
            montage.curves.push_back(std::move(curve));
        }
        montage.notifyTracks = payload.value("notifyTracks", montage.notifyTracks);
        montage.notifyTrackNames = payload.value("trackNames", std::vector<std::string>{});
        montage.blendInSeconds = payload.value("blendIn", montage.blendInSeconds);
        montage.blendOutSeconds = payload.value("blendOut", montage.blendOutSeconds);
        montage.blendInCurve = static_cast<MontageBlendCurve>(payload.value("blendInCurve", std::uint8_t{0}));
        montage.blendOutCurve = static_cast<MontageBlendCurve>(payload.value("blendOutCurve", std::uint8_t{0}));
        montage.blendOutTriggerSeconds = payload.value("blendOutTrigger", montage.blendOutTriggerSeconds);
        montage.autoBlendOut = payload.value("autoBlendOut", montage.autoBlendOut);
        montage.rateScale = payload.value("rateScale", montage.rateScale);
        montage.rootMotion = static_cast<MontageRootMotion>(payload.value("rootMotion", std::uint8_t{0}));
    } catch (const nlohmann::json::exception& error) {
        UVE_ERROR("MotionMontageAsset: failed to read \"{}\": {}", path.string(), error.what());
        return false;
    }
    if (!IsMotionMontageAssetValid(montage)) {
        UVE_ERROR("MotionMontageAsset: \"{}\" holds out-of-range values", path.string());
        return false;
    }
    outMontage = std::move(montage);
    return true;
}

} // namespace UVE::Asset
