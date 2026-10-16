// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Tracks whether a 2D local-space bounding rectangle (`localRect`) overlaps the active `Camera2D`
/// view rectangle each frame, pulsing `enteredScreenThisFrame` and `exitedScreenThisFrame` on
/// transitions.
struct VisibleOnScreenNotifier2DComponentUVE final {
    Math::RectUVE localRect{Math::Vector2UVE{-10.0F, -10.0F}, Math::Vector2UVE{20.0F, 20.0F}};
    bool enabled = true;
    /// Runtime state, never persisted:
    bool isOnScreen = false;
    bool enteredScreenThisFrame = false;
    bool exitedScreenThisFrame = false;

    [[nodiscard]] bool operator==(const VisibleOnScreenNotifier2DComponentUVE&) const = default;
};

[[nodiscard]] bool IsVisibleOnScreenNotifier2DComponentValidUVE(
    const VisibleOnScreenNotifier2DComponentUVE& value) noexcept;

[[nodiscard]] VisibleOnScreenNotifier2DComponentUVE SanitizeVisibleOnScreenNotifier2DComponentUVE(
    const VisibleOnScreenNotifier2DComponentUVE& value) noexcept;

[[nodiscard]] Math::RectUVE ResolveVisibleOnScreenNotifier2DWorldRectUVE(
    const VisibleOnScreenNotifier2DComponentUVE& value,
    const Math::Vector2UVE& worldPosition2D) noexcept;

void StepVisibleOnScreenNotifier2DStateUVE(VisibleOnScreenNotifier2DComponentUVE& value,
                                           bool currentlyVisible) noexcept;

struct VisibleOnScreenNotifier2DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "VisibleOnScreenNotifier2D";
    static constexpr std::string_view defaultName = "VisibleOnScreenNotifier2D";
    VisibleOnScreenNotifier2DComponentUVE notifier{};

    [[nodiscard]] bool operator==(
        const VisibleOnScreenNotifier2DObjectDefinitionUVE&) const = default;
};

[[nodiscard]] bool IsVisibleOnScreenNotifier2DObjectDefinitionValidUVE(
    const VisibleOnScreenNotifier2DObjectDefinitionUVE& value) noexcept;

void ApplyVisibleOnScreenNotifier2DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, EntityUVE entity,
    const VisibleOnScreenNotifier2DObjectDefinitionUVE& value);

/// Evaluates all `VisibleOnScreenNotifier2DComponentUVE` entities against `activeViewRect`
/// (`nullptr` marks all notifiers off-screen) and returns the number of currently visible notifiers.
std::size_t UpdateVisibleOnScreenNotifier2DObjectsUVE(IEntityManagerUVE& entityManager,
                                                      const Math::RectUVE* activeViewRect);

} // namespace UVE::Scene
