// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

namespace UVE::Scene {

/// Which thread an entity's per-frame work is allowed to run on.
///
/// The engine already has the machinery to run work off the main thread - ThreadPoolUVE,
/// JobGraphUVE and its counters, FrameSchedulerUVE. What it had no way to express is which work is
/// SAFE to move there, and that is not something a scheduler can infer: an entity that touches the
/// renderer, the window or Dear ImGui must stay on the main thread no matter how independent its
/// work looks. This is the author saying so.
enum class ThreadGroupModeUVE : std::uint8_t {
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
struct ThreadGroupComponentUVE final {
    ThreadGroupModeUVE mode = ThreadGroupModeUVE::Inherit;

    /// Order within the resolved group. Work in one group runs in this order; work in different
    /// groups may overlap. Lower runs first, matching ProcessComponentUVE::priority.
    std::int32_t order = 0;

    /// Derived: the mode resolved against ancestors. Written only by SceneGraphUVE::UpdateUVE.
    ///
    /// Note the asymmetry with ProcessComponentUVE, and that it is deliberate: a child under a
    /// MainThread parent cannot escape to a worker, because the parent's constraint is a statement
    /// about shared state the child is part of. Pausing has no equivalent - a pause menu genuinely
    /// can run while its parent does not.
    ThreadGroupModeUVE resolvedModeInHierarchy = ThreadGroupModeUVE::MainThread;
};

[[nodiscard]] bool IsThreadGroupComponentValidUVE(const ThreadGroupComponentUVE& component) noexcept;

/// Resolves `mode` against the parent's already-resolved answer. Inherit passes through; SubThread
/// is honoured only where the parent also permits it.
[[nodiscard]] ThreadGroupModeUVE ResolveThreadGroupModeUVE(ThreadGroupModeUVE mode,
                                                           ThreadGroupModeUVE parentMode) noexcept;

} // namespace UVE::Scene
