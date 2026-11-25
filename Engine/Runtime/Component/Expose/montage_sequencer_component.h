// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "asset_guid.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace UVE::Scene {

/// Where a montage is in its playback.
enum class MontagePhase : std::uint8_t {
    Stopped = 0,
    BlendIn,
    Playing,
    BlendOut,
};

/// A montage's live playback (runtime only, never saved).
struct MontagePlayback final {
    MontagePhase phase = MontagePhase::Stopped;
    double time = 0.0;              ///< montage seconds
    std::string section;            ///< the section playing
    double phaseTime = 0.0;         ///< seconds into the blend in / out
    float weight = 0.0F;            ///< how much of the pose is the montage's
    std::vector<bool> activeStates; ///< per notify state: inside it now
    std::uint64_t chanceSeed = 0x9E3779B97F4A7C15ULL; ///< drives notify trigger chances (deterministic)
};

/// What a script asked of the slot; the engine carries it out on its next animation pass.
enum class MontageRequest : std::uint8_t {
    None = 0,
    Play,
    JumpTo,
    Stop,
};

/// MontageSequencer: the montages a character (or any object with a skeleton under it) can play - its
/// attacks, reloads, emotes. A script plays one by its file name: `montage.play("Attack")` plays
/// the montage file Attack.uvmontage set here. While it plays, its pose is blended over what the
/// character's AnimationSequencer / AnimationGraph poses, and its notifies reach the script.
struct MontageSequencerComponent final {
    Asset::AssetGuid montage1;
    Asset::AssetGuid montage2;
    Asset::AssetGuid montage3;
    Asset::AssetGuid montage4;
    Asset::AssetGuid montage5;
    Asset::AssetGuid montage6;
    Asset::AssetGuid montage7;
    Asset::AssetGuid montage8;
    /// A `.uvmontagelib` this player is linked to: its montages play by name as the slots' do.
    Asset::AssetGuid libraryRef;

    // ---- Runtime state; never saved -------------------------------------------------------------
    MontageRequest request = MontageRequest::None;
    std::string requestMontage;   ///< Play: the montage's name (file stem)
    std::string requestSection;   ///< Play / JumpTo: the section (empty: the first)
    Asset::AssetGuid playing;  ///< the montage playing
    std::string playingName;
    MontagePlayback playback;
    /// The playing montage's curves at the playhead, for `montage.curve(name)`.
    std::vector<std::pair<std::string, float>> curveValues;

    [[nodiscard]] Asset::AssetGuid MontageAt(const std::size_t index) const noexcept {
        const Asset::AssetGuid all[] = {montage1, montage2, montage3, montage4, montage5, montage6, montage7, montage8};
        return index < 8U ? all[index] : Asset::AssetGuid{};
    }
    [[nodiscard]] bool IsPlaying() const noexcept { return playback.phase != MontagePhase::Stopped; }

    /// Authored data only: a playing slot equals its saved self.
    [[nodiscard]] bool operator==(const MontageSequencerComponent& other) const noexcept {
        return montage1 == other.montage1 && montage2 == other.montage2 && montage3 == other.montage3 &&
               montage4 == other.montage4 && montage5 == other.montage5 && montage6 == other.montage6 &&
               montage7 == other.montage7 && montage8 == other.montage8 && libraryRef == other.libraryRef;
    }
};

} // namespace UVE::Scene
