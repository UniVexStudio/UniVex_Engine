// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_tool_session.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] bool AreTransformsEqual(const Scene::TransformComponent& lhs,
                                         const Scene::TransformComponent& rhs) noexcept {
    return lhs.localPosition.x == rhs.localPosition.x && lhs.localPosition.y == rhs.localPosition.y &&
           lhs.localPosition.z == rhs.localPosition.z && lhs.localRotation.x == rhs.localRotation.x &&
           lhs.localRotation.y == rhs.localRotation.y && lhs.localRotation.z == rhs.localRotation.z &&
           lhs.localRotation.w == rhs.localRotation.w && lhs.localScale.x == rhs.localScale.x &&
           lhs.localScale.y == rhs.localScale.y && lhs.localScale.z == rhs.localScale.z;
}

} // namespace

bool EditorToolSession::Begin(const Scene::Entity entity, const EditorToolSessionMode mode,
                                    const Scene::TransformComponent& baselineTransform,
                                    const bool baselineDirty) noexcept {
    if (m_snapshot.has_value()) {
        return false;
    }
    if (entity == Scene::kInvalidEntity) {
        m_lastOutcome = EditorToolSessionOutcome::Rejected;
        return false;
    }

    m_snapshot = EditorToolSessionSnapshot{
        entity,
        mode,
        baselineTransform,
        baselineTransform,
        baselineDirty,
    };
    m_lastOutcome = EditorToolSessionOutcome::None;
    return true;
}

bool EditorToolSession::RecordPreviewApplied(
    const Scene::TransformComponent& appliedTransform) noexcept {
    if (!m_snapshot.has_value()) {
        m_lastOutcome = EditorToolSessionOutcome::Rejected;
        return false;
    }

    m_snapshot->lastAppliedTransform = appliedTransform;
    return true;
}

std::optional<EditorToolSessionSnapshot> EditorToolSession::Commit(const bool changed) noexcept {
    if (!m_snapshot.has_value()) {
        m_lastOutcome = EditorToolSessionOutcome::Rejected;
        return std::nullopt;
    }

    const EditorToolSessionSnapshot completedSnapshot = *m_snapshot;
    m_snapshot.reset();
    m_lastOutcome = changed ? EditorToolSessionOutcome::Committed
                            : EditorToolSessionOutcome::CompletedWithoutChange;
    return completedSnapshot;
}

std::optional<EditorToolSessionSnapshot> EditorToolSession::Cancel(
    const Scene::TransformComponent& currentTransform) noexcept {
    if (!m_snapshot.has_value()) {
        return std::nullopt;
    }

    const EditorToolSessionSnapshot cancelledSnapshot = *m_snapshot;
    m_snapshot.reset();
    if (!AreTransformsEqual(currentTransform, cancelledSnapshot.lastAppliedTransform)) {
        m_lastOutcome = EditorToolSessionOutcome::ExternalTransformConflict;
        return std::nullopt;
    }

    m_lastOutcome = EditorToolSessionOutcome::Cancelled;
    return cancelledSnapshot;
}

void EditorToolSession::Discard() noexcept {
    if (!m_snapshot.has_value()) {
        return;
    }
    m_snapshot.reset();
    m_lastOutcome = EditorToolSessionOutcome::Cancelled;
}

void EditorToolSession::MarkRestoreFailed() noexcept {
    m_lastOutcome = EditorToolSessionOutcome::RestoreFailed;
}

void EditorToolSession::MarkRejected() noexcept {
    m_lastOutcome = EditorToolSessionOutcome::Rejected;
}

EditorToolSessionPhase EditorToolSession::GetPhase() const noexcept {
    return m_snapshot.has_value() ? EditorToolSessionPhase::Previewing : EditorToolSessionPhase::Idle;
}

EditorToolSessionOutcome EditorToolSession::GetLastOutcome() const noexcept {
    return m_lastOutcome;
}

const std::optional<EditorToolSessionSnapshot>& EditorToolSession::GetSnapshot() const noexcept {
    return m_snapshot;
}

} // namespace UVE::Editor
