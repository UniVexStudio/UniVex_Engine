// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>

#include "transform_component.h"
#include "entity.h"

namespace UVE::Editor {

/// The externally observable lifecycle state of one editor-owned transform tool transaction.
/// Previewing always represents a single selected document entity; it never owns ECS storage,
/// rendering data, input routing, or Undo/Redo history.
enum class EditorToolSessionPhase {
    Idle,
    Previewing,
};

/// The most recent terminal outcome recorded by an editor transform tool session. RestoreFailed
/// is deliberately distinct from Cancelled: it means the editor cleared its transient ownership but
/// could not prove the preview transform was restored, so callers must not claim atomic rollback.
enum class EditorToolSessionOutcome {
    None,
    Committed,
    CompletedWithoutChange,
    Cancelled,
    /// The live transform no longer matched this session's last successful preview, so the
    /// captured baseline was intentionally not restored over an external mutation.
    ExternalTransformConflict,
    RestoreFailed,
    Rejected,
};

/// Immutable tool family captured at session begin. It prevents a mode switch from reinterpreting
/// a pointer gesture that is already previewing a transform.
enum class EditorToolSessionMode {
    Translate,
    Rotate,
    Scale,
};

/// A copied transaction baseline and final preview value. Editor consumes this value to apply
/// scene-graph mutations and create its existing TransformHistoryEntry; the session itself
/// remains independent of ECS, history, and UI ownership.
struct EditorToolSessionSnapshot final {
    Scene::Entity entity = Scene::kInvalidEntity;
    EditorToolSessionMode mode = EditorToolSessionMode::Translate;
    Scene::TransformComponent baselineTransform{};
    Scene::TransformComponent lastAppliedTransform{};
    bool baselineDirty = false;
};

/// EditorToolSession owns the explicit begin/preview/commit/cancel state machine for one
/// transform-tool gesture. A re-entrant Begin while Previewing is an intentional no-op rejection:
/// the existing session remains untouched until it commits or cancels through its own terminal path.
class EditorToolSession final {
public:
    [[nodiscard]] bool Begin(Scene::Entity entity, EditorToolSessionMode mode,
                                const Scene::TransformComponent& baselineTransform,
                                bool baselineDirty) noexcept;

    /// Records a transform only after Editor has successfully applied it through SceneGraph.
    /// Preview updates never create history entries or terminal outcomes.
    [[nodiscard]] bool RecordPreviewApplied(
        const Scene::TransformComponent& appliedTransform) noexcept;

    /// Ends a valid preview for history processing. The returned snapshot is copied before the
    /// phase becomes Idle, so Editor can safely record one history transaction without a second
    /// transform write.
    [[nodiscard]] std::optional<EditorToolSessionSnapshot> Commit(bool changed) noexcept;

    /// Ends the active preview only when `currentTransform` still equals the last transform this
    /// session successfully applied. A mismatch is an external live-transform conflict: the session
    /// clears with ExternalTransformConflict and returns no baseline, preventing a stale restore
    /// from overwriting another editor/runtime mutation.
    [[nodiscard]] std::optional<EditorToolSessionSnapshot> Cancel(
        const Scene::TransformComponent& currentTransform) noexcept;

    /// Clears an active session when its target is deleted or cannot be read safely. It does not
    /// attempt a restore and records Cancelled because no live transform was available to compare.
    void Discard() noexcept;
    void MarkRestoreFailed() noexcept;

    void MarkRejected() noexcept;

    [[nodiscard]] EditorToolSessionPhase GetPhase() const noexcept;
    [[nodiscard]] EditorToolSessionOutcome GetLastOutcome() const noexcept;
    [[nodiscard]] const std::optional<EditorToolSessionSnapshot>& GetSnapshot() const noexcept;

private:
    std::optional<EditorToolSessionSnapshot> m_snapshot;
    EditorToolSessionOutcome m_lastOutcome = EditorToolSessionOutcome::None;
};

} // namespace UVE::Editor
