// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

namespace UVE::Scene {

/// Whether an entity's per-frame work runs while the simulation is paused.
///
/// WHY PER-ENTITY RATHER THAN ONE GLOBAL SWITCH. Pausing is never all-or-nothing. A pause menu has
/// to keep animating and responding to input while everything it is drawn over stops; a slow-motion
/// effect stops the world and not the camera; a death cam keeps running after the player's own
/// controller has been disabled. With only a global flag, every one of those needs its own
/// hand-written "am I allowed to run right now" check, and each one gets it slightly wrong.
enum class ProcessModeUVE : std::uint8_t {
    /// Take the parent's answer, or Pausable at the top of the hierarchy. The default, so a whole
    /// subtree is switched by its root rather than node by node.
    Inherit = 0,
    /// Runs while the simulation is running, stops while it is paused. What almost everything in a
    /// level wants.
    Pausable,
    /// Runs only while paused. A pause menu, a confirmation dialog, an inspector overlay.
    WhenPaused,
    /// Runs regardless. Anything that must survive a pause: the pause controller itself, a
    /// networking heartbeat, a telemetry sampler.
    Always,
    /// Never runs, paused or not, without being detached or destroyed. The reversible way to take
    /// something out of the simulation while keeping it authored, selected and inspectable.
    Disabled,
};

/// Whether an entity's work runs this frame, and in what order relative to its siblings.
///
/// Ordering lives here with the pause mode because both answer the same question - when does this
/// entity's work happen - and separating them would make an author set two components to describe
/// one intent. Lower values run first, which matches how every other priority in the engine reads.
struct ProcessComponentUVE final {
    ProcessModeUVE mode = ProcessModeUVE::Inherit;

    /// Frame-update order. Ties are broken by hierarchy order, so an unset priority never makes
    /// execution order arbitrary.
    std::int32_t priority = 0;

    /// Fixed-step order, kept separate because the two schedules genuinely differ: a controller
    /// that must read input before anything else each frame usually has to run AFTER the physics
    /// that moved the world it is reacting to.
    std::int32_t physicsPriority = 0;

    /// Derived: this entity's mode resolved against its ancestors, answering "does it run right
    /// now" for the current simulation state. Written only by SceneGraphUVE::UpdateUVE, exactly
    /// like VisibilityComponentUVE::visibleInHierarchy - authoring it would be overwritten on the
    /// next update, and persisting it would restore an answer computed for a different state.
    ProcessModeUVE resolvedModeInHierarchy = ProcessModeUVE::Pausable;

    // There is deliberately no cached "is it running right now" field. That answer depends on the
    // simulation's paused state, which changes without the hierarchy changing, so a cached copy
    // would be stale exactly when it matters and would force the scene graph to know about
    // pausing. Consumers call IsProcessingUVE(resolvedModeInHierarchy, paused) instead - one
    // function, no stale state, and the rule still exists in only one place.
};

/// Plain values with no representable invalid state; present so the component matches the
/// validator convention every other one follows and the serializer can register it unchanged.
[[nodiscard]] bool IsProcessComponentValidUVE(const ProcessComponentUVE& component) noexcept;

/// Resolves `mode` against the answer already resolved for the parent. Inherit passes the parent's
/// answer through unchanged; anything else replaces it. Shared by the scene graph and its tests so
/// the rule exists once.
[[nodiscard]] ProcessModeUVE ResolveProcessModeUVE(ProcessModeUVE mode, ProcessModeUVE parentMode) noexcept;

/// Whether a resolved mode runs while the simulation is in the given state.
[[nodiscard]] bool IsProcessingUVE(ProcessModeUVE resolvedMode, bool simulationPaused) noexcept;

} // namespace UVE::Scene
