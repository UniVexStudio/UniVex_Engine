#include "save_revision_policy.h"

namespace UVE::Save {

SaveSyncAction EvaluateSaveSyncAction(const SaveRevisionStatus status) noexcept {
    switch (status) {
        case SaveRevisionStatus::Unchanged:
            return SaveSyncAction::NoOp;
        case SaveRevisionStatus::LocalAhead:
            return SaveSyncAction::Upload;
        case SaveRevisionStatus::RemoteAhead:
            return SaveSyncAction::Download;
        case SaveRevisionStatus::Conflict:
            return SaveSyncAction::Conflict;
        case SaveRevisionStatus::Invalid:
        default:
            return SaveSyncAction::Invalid;
    }
}

SaveRevisionStatus EvaluateSaveRevision(
    const std::uint64_t baseRevision, const std::uint64_t localRevision,
    const std::uint64_t remoteRevision) noexcept {
    if (baseRevision == 0U || localRevision == 0U || remoteRevision == 0U) {
        return SaveRevisionStatus::Invalid;
    }
    if (localRevision == remoteRevision) {
        return SaveRevisionStatus::Unchanged;
    }
    if (localRevision != baseRevision && remoteRevision == baseRevision) {
        return SaveRevisionStatus::LocalAhead;
    }
    if (localRevision == baseRevision && remoteRevision != baseRevision) {
        return SaveRevisionStatus::RemoteAhead;
    }
    return SaveRevisionStatus::Conflict;
}

} // namespace UVE::Save
