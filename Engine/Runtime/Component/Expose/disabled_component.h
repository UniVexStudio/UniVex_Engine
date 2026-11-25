// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Scene {

/// Disables an entity's simulation. Presence is the whole state: an entity carrying this tag is
/// skipped by gameplay, physics, AI and particle systems, while an entity without it simulates
/// normally.
///
/// WHY A TAG AND NOT A FLAG. Enabled is the overwhelmingly common case, so absence meaning
/// "enabled" keeps every existing document, every runtime-created entity and every archetype
/// layout working unchanged — the flag is only paid for (one archetype bit, one byte) by
/// entities that actually use it. A tag also composes with everything a bool field cannot:
/// queries can match it, prefabs can override it, and toggling it migrates the entity to an
/// archetype the disabled-skipping systems never visit.
///
/// NOT VISIBILITY. `VisibilityComponent` answers "is it drawn" (render-only, with hierarchy
/// inheritance); this answers "is it simulated". A disabled entity keeps its transform updated
/// by the scene graph — the editor can still move it — but no gameplay system advances it.
/// Each system opts in by checking `IsEntityEnabled`; the manager never filters implicitly,
/// because implicit filtering would change what every existing `ForEach` visits.
///
/// Serialized as an empty object under "DisabledComponent" (presence is the data); the
/// serializer's hand-written branch is next to `VisibilityComponent`'s.
///
/// Thread-safety: value type; safe to copy and move freely with no shared state.
struct DisabledComponent final {
};

/// Always true: a tag has no fields and therefore no invalid state. Present so the component has
/// the same validate seam every other component has - a caller writing generic code should not
/// have to special-case which components can be checked.
[[nodiscard]] bool IsDisabledComponentValid(const DisabledComponent& component) noexcept;

/// True when the tag pointer is non-null (the entity carries the disabled simulation tag).
[[nodiscard]] bool IsDisabledTagPresent(const DisabledComponent* component) noexcept;

/// True when the entity is enabled for simulation (`component == nullptr`).
[[nodiscard]] bool ShouldSimulateEntity(const DisabledComponent* component) noexcept;

/// True when gameplay, AI, physics, and particle systems should skip the entity.
[[nodiscard]] bool ShouldSkipEntitySimulation(const DisabledComponent* component) noexcept;

/// Scene-graph transform propagation always runs even when `DisabledComponent` is present.
[[nodiscard]] bool ShouldPropagateTransformWhenDisabled(
    const DisabledComponent* component) noexcept;

} // namespace UVE::Scene
