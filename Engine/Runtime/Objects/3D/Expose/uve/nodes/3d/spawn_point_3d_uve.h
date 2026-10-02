// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <span>
#include <string>

#include "uve/component/entity_uve.h"
#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

/// Authored state for a SpawnPoint3D: the place the player starts when Play is entered
/// (Unreal's PlayerStart role - Godot ships no built-in equivalent and leaves this to scripts,
/// which is exactly the hole this node fills).
///
/// `localPosition`/`localRotation` are NOT a shadow of the entity's transform: they are the
/// authored OFFSET from this node's own world pose, so a spawn point can sit on a floor seam
/// while the player appears one unit above and facing inward, without a dozen invisible child
/// nodes. The final spawn pose composes entity-world with that offset using the same
/// Transform-then-rotate convention as the scene sweep (node scale deliberately does NOT scale
/// the offset - an offset is a distance in metres, not a volume).
///
/// At play entry the editor resolves exactly one enabled spawn point deterministically (stable
/// content order, not whatever order the pool happens to iterate in), moves the player entity
/// there (the entity carrying a CharacterControllerComponentUVE), and a point authored with
/// `oneShot = true` disables itself after firing - so a checkpoint-style chain spends each
/// point once per session. All of it happens after the play-mode document snapshot is captured,
/// so leaving Play restores the player's authored pose and every spent one-shot untouched.
struct SpawnPoint3DNodeComponentUVE final {

    std::string spawnTag = "spawn";
    Math::Vector3UVE localPosition{};
    Math::QuaternionUVE localRotation{};
    bool enabled = true;
    bool oneShot = false;
};

[[nodiscard]] bool IsSpawnPoint3DNodeComponentValidUVE(const SpawnPoint3DNodeComponentUVE& value) noexcept;

/// One spawn point as the selection resolver sees it: already filtered for enabled+valid by the
/// caller (that filtering needs the ECS, which this file deliberately never touches).
struct SpawnPoint3DCandidateUVE final {
    EntityUVE entity = kInvalidEntityUVE;
    bool oneShot = false;
};

/// Deterministic play-entry selection: the enabled candidate that sorts FIRST in stable content
/// order ((index, generation) lexicographic - pool iteration order and creation timestamps are
/// both wrong answers on purpose, because a saved scene must spawn the same way every time on
/// every machine). Empty input yields no spawn; any rule richer than this (per-player tags,
/// teams) lands here as a new resolver when the gameplay data for it actually exists.
[[nodiscard]] std::optional<EntityUVE> ResolveSpawnPoint3DSelectionUVE(
    std::span<const SpawnPoint3DCandidateUVE> candidates) noexcept;

/// A composed pose: position + rotation. Frame-agnostic by design - the same shape answers both
/// "where in the world the player spawns" and "what local pose realises that under its parent".
struct SpawnPoint3DPoseUVE final {
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
};

/// Composes the spawn point's node world pose with its authored local offset (same convention
/// the scene sweep uses: rotate the offset by the node rotation, then add; rotation composes
/// via quaternion multiplication, then normalizes). Node scale is deliberately not applied -
/// see the component's doc. Any non-finite input, or a rotation too degenerate to normalize,
/// yields no pose rather than teleporting the player into a garbage transform.
[[nodiscard]] std::optional<SpawnPoint3DPoseUVE> ComposeSpawnPointPoseUVE(
    Math::Vector3UVE nodePosition, Math::QuaternionUVE nodeRotation, Math::Vector3UVE localPosition,
    Math::QuaternionUVE localRotation) noexcept;

/// The exact inverse of the scene sweep's parent composition
/// (`worldPos = parentPos + Rotate(parentRot, parentScale * localPos)`): recovers the LOCAL pose
/// a player must be written so its world pose lands on `spawnPose`, given its parent's world
/// TRS. Component-wise scale reciprocal makes this exact for any axis-aligned non-uniform
/// parent scale; a zero (or denormal) parent scale axis refuses the move, because a player
/// under a scaled-out ancestor has no meaningful local pose to solve for. With no parent at
/// all, call it with identity parent TRS and the world pose falls straight through.
[[nodiscard]] std::optional<SpawnPoint3DPoseUVE> ResolveSpawnPointPlayerLocalUVE(
    const SpawnPoint3DPoseUVE& spawnPose, Math::Vector3UVE parentPosition,
    Math::QuaternionUVE parentRotation, Math::Vector3UVE parentScale) noexcept;

} // namespace UVE::Scene
