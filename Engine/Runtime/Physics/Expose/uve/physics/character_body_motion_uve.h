// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/character_controller_component_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Physics {

/// One fixed step's player intent for a CharacterBody3D's built-in movement.
struct CharacterMotionInputUVE final {
    /// Horizontal steering on X and Z, each in [-1, 1], at most unit length.
    Math::Vector3UVE move{};
    /// Floating only: +1 rises, -1 sinks.
    float rise = 0.0F;
    /// Jump was pressed this step.
    bool jumpPressed = false;
};

/// The part of a CharacterBody3D's step that decides where it wants to go, before any collision:
/// steering (full on the floor or while floating, Air Control of it in the air), jumping (with the
/// Jump Buffer and Coyote Time windows) and gravity. Updates `c.velocity`, the buffer and the
/// coyote clock; returns true when it jumped this step. `gravityY` is the world's gravity along Y,
/// negative for down. Without built-in movement only gravity touches the velocity, so a script's
/// velocity is kept as it was set.
///
/// Kept apart from the move itself so the feel of the controls can be tested exactly, step by
/// step, with no world and no clock.
bool StepCharacterIntentUVE(Scene::CharacterControllerComponentUVE& c, const CharacterMotionInputUVE& input,
                            float gravityY, float deltaTimeSeconds) noexcept;

/// What one move found.
struct CharacterMoveOutcomeUVE final {
    bool onFloor = false;
    Math::Vector3UVE floorNormal{0.0F, 1.0F, 0.0F};
    bool hitCeiling = false;
};

/// The part of a step after the move: records the floor and ceiling, stops the upward velocity at
/// a ceiling (and all of it when Slide On Ceiling is off), stops the fall on the floor, and runs
/// the coyote clock while off the floor.
void FinishCharacterStepUVE(Scene::CharacterControllerComponentUVE& c, const CharacterMoveOutcomeUVE& outcome,
                            bool jumped, float deltaTimeSeconds) noexcept;

} // namespace UVE::Physics
