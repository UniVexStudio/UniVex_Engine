#pragma once

#include <cstdint>

namespace UVE::Save {

enum class SaveSyncAction : std::uint8_t {
    Invalid = 0,
    NoOp,
    Upload,
    Download,
    Conflict,
};

enum class SaveRevisionStatus : std::uint8_t {
    Invalid = 0,
    Unchanged,
    LocalAhead,
    RemoteAhead,
    Conflict,
};

/// Maps caller-owned revision status to a sync direction without performing cloud I/O.
[[nodiscard]] SaveSyncAction EvaluateSaveSyncAction(SaveRevisionStatus status) noexcept;

/// Classifies caller-owned base/local/remote save revisions without cloud or merge ownership.
[[nodiscard]] SaveRevisionStatus EvaluateSaveRevision(
    std::uint64_t baseRevision, std::uint64_t localRevision,
    std::uint64_t remoteRevision) noexcept;

} // namespace UVE::Save
