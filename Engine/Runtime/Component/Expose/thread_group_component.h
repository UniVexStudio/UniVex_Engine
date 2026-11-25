// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

namespace UVE::Scene {

/// Which thread an entity's per-frame work is allowed to run on.
///
/// The engine already has the machinery to run work off the main thread - ThreadPool,
/// JobGraph and its counters, FrameScheduler. What it had no way to express is which work is
/// SAFE to move there, and that is not something a scheduler can infer: an entity that touches the
/// renderer, the window or the editor UI must stay on the main thread no matter how independent its
/// work looks. This is the author saying so.
enum class ThreadGroupMode : std::uint8_t {
    /// Take the parent's answer, or MainThread at the top of the hierarchy - the safe default, so
    /// nothing moves off the main thread by accident.
    Inherit = 0,
    /// Must run on the main thread.
    MainThread,
    /// May run on a worker. A promise by the author that this entity's work touches nothing
    /// main-thread-only and does not race its siblings.
    SubThread,
};

/// An entity's threading preference and its position in the group's ordering.
struct ThreadGroupComponent final {
    ThreadGroupMode mode = ThreadGroupMode::Inherit;

    /// Order within the resolved group. Work in one group runs in this order; work in different
    /// groups may overlap. Lower runs first, matching ProcessComponent::priority.
    std::int32_t order = 0;

    /// Derived: the mode resolved against ancestors. Written only by SceneGraph::Update.
    ///
    /// Note the asymmetry with ProcessComponent, and that it is deliberate: a child under a
    /// MainThread parent cannot escape to a worker, because the parent's constraint is a statement
    /// about shared state the child is part of. Pausing has no equivalent - a pause menu genuinely
    /// can run while its parent does not.
    ThreadGroupMode resolvedModeInHierarchy = ThreadGroupMode::MainThread;
};

[[nodiscard]] bool IsThreadGroupModeValid(ThreadGroupMode mode) noexcept;

[[nodiscard]] bool IsThreadGroupComponentValid(const ThreadGroupComponent& component) noexcept;

/// Resolves `mode` against the parent's already-resolved answer. Inherit passes through; SubThread
/// is honoured only where the parent also permits it.
[[nodiscard]] ThreadGroupMode ResolveThreadGroupMode(ThreadGroupMode mode,
                                                           ThreadGroupMode parentMode) noexcept;

/// Resolves and writes `component->resolvedModeInHierarchy` against `parentResolvedMode`
/// (passes through when `component` is null).
[[nodiscard]] ThreadGroupMode ResolveHierarchyThreadGroupMode(
    ThreadGroupComponent* component, ThreadGroupMode parentResolvedMode) noexcept;

/// True when `component` is non-null and its resolved hierarchy mode permits worker execution (`SubThread`).
[[nodiscard]] bool CanDispatchOnSubThread(const ThreadGroupComponent* component) noexcept;

/// Deterministic execution-order comparator (`order` ascending, then `tieBreak`).
[[nodiscard]] bool CompareThreadGroupExecutionOrder(const ThreadGroupComponent& lhs,
                                                       std::uint32_t lhsTieBreak,
                                                       const ThreadGroupComponent& rhs,
                                                       std::uint32_t rhsTieBreak) noexcept;

/// Sanitizes `component` so invalid enum values fall back to `Inherit` / `MainThread`.
[[nodiscard]] ThreadGroupComponent SanitizeThreadGroupComponent(
    const ThreadGroupComponent& component) noexcept;

} // namespace UVE::Scene
