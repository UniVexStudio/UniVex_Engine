// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace UVE::Save {

inline constexpr std::uint32_t kCurrentSavePayloadSchemaVersion = 1U;
inline constexpr std::size_t kMaximumSaveNameBytes = 128U;

/// Metadata section of a `.uvsave` file: timestamp, engine version, playtime — the spec's
/// "Metadata (timestamp, version, playtime)" (Part 17). Carries engine version as four raw
/// uint32 fields rather than reusing Core::Version: engine/save sits below engine/core in the
/// dependency graph (EngineCore owns/constructs SaveGameSystem, never the reverse), so this
/// module cannot include anything from engine/core without a circular link dependency. A caller
/// that has a Core::Version (e.g. EngineCore::GetEngineVersion()) copies its fields in by
/// hand before calling Save().
/// Thread-safety: value type; safe to copy/move/compare freely.
struct GameStateMetadata {
    /// Nonnegative seconds since the Unix epoch, UTC. Overwritten by SaveGameSystem::Save()
    /// itself (never trusts a caller-supplied value), so every save file's timestamp is trustworthy
    /// regardless of what a caller passes in; decoded negative timestamps are rejected.
    std::int64_t savedAtUnixSeconds = 0;

    /// The engine build that wrote this save. Read but not validated this increment — no old
    /// save format exists yet to migrate from.
    std::uint32_t engineVersionMajor = 0;
    std::uint32_t engineVersionMinor = 0;
    std::uint32_t engineVersionPatch = 0;
    std::uint32_t engineVersionBuild = 0;

    /// The `.uvsave` payload layout version this save was written with (independent of
    /// `engineVersion*` above). SaveGameSystem writes the current
    /// `kCurrentSavePayloadSchemaVersion`; Load dispatches this value through the bounded
    /// migration seam before scene deserialization.
    std::uint32_t payloadSchemaVersion = kCurrentSavePayloadSchemaVersion;

    /// Total elapsed gameplay seconds at the moment of this save. CheckpointManager fills this
    /// from its own GetTotalPlaytimeSeconds() for auto-saves/checkpoints; a caller driving
    /// SaveGameSystem directly for a numbered slot supplies whatever it tracks.
    double playtimeSeconds = 0.0;

    /// The slot this save was written to. Overwritten by SaveGameSystem::Save() itself
    /// (like savedAtUnixSeconds), so GetSaveMetadata()'s returned value is always
    /// self-consistent with the slot it was read from, even if a caller passes a stale value.
    int slotIndex = 0;

    /// Optional player-facing label (e.g. "Before the Dragon Fight"). Empty by default. Never
    /// interpreted by SaveGameSystem itself — purely round-tripped for UI display. The copied
    /// UTF-8/byte string is capped by `kMaximumSaveNameBytes` and may not contain NUL bytes.
    std::string saveName;
};

} // namespace UVE::Save
