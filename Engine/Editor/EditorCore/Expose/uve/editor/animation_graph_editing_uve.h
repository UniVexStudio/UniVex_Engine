// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "uve/component/animation_tree_component_uve.h"

namespace UVE::Editor {

/// The Anim Graph's editing operations on an AnimationTree's nodes, as pure functions so they are
/// testable without a window. Every one leaves a valid graph valid: it refuses what would break it
/// (a cycle, a second Output, deleting the Output) rather than repairing afterwards.

/// What a kind is called in the editor ("Blend Space" for BlendSpace1D...).
[[nodiscard]] const char* AnimationGraphKindLabelUVE(Scene::AnimationGraphNodeKindUVE kind) noexcept;

/// One line on what a kind does, for tooltips and the Add Node search.
[[nodiscard]] const char* AnimationGraphKindHelpUVE(Scene::AnimationGraphNodeKindUVE kind) noexcept;

/// What input `slot` of a kind means: "A"/"B", "Base"/"Layer", "State 2"...
[[nodiscard]] std::string AnimationGraphSlotLabelUVE(Scene::AnimationGraphNodeKindUVE kind, std::size_t slot);

/// Adds a node of `kind` at `position` with the empty input slots the kind needs (two for Blend,
/// Additive, One Shot and Blend Space, one for Time Scale and State Machine). Returns its id, or 0
/// for Output (a graph has exactly one) or a full graph.
std::uint32_t AddAnimationGraphNodeUVE(std::vector<Scene::AnimationGraphNodeUVE>& nodes,
                                       Scene::AnimationGraphNodeKindUVE kind, Math::Vector2UVE position);

/// True when `source` can feed input `slot` of `target`: both exist, the slot exists, the source is
/// not the Output nor the target itself, and the target is not already upstream of the source (the
/// wire would close a cycle).
[[nodiscard]] bool CanConnectAnimationGraphNodesUVE(const std::vector<Scene::AnimationGraphNodeUVE>& nodes,
                                                    std::uint32_t target, std::size_t slot, std::uint32_t source);

/// Wires `source` into input `slot` of `target`. A node feeds one slot only, so a source already
/// wired elsewhere moves here. Returns false, changing nothing, when CanConnect says no.
bool ConnectAnimationGraphNodesUVE(std::vector<Scene::AnimationGraphNodeUVE>& nodes, std::uint32_t target,
                                   std::size_t slot, std::uint32_t source);

/// Empties input `slot` of `target`. False when there is no such slot or it is already empty.
bool DisconnectAnimationGraphInputUVE(std::vector<Scene::AnimationGraphNodeUVE>& nodes, std::uint32_t target,
                                      std::size_t slot);

/// Removes the nodes (never the Output), emptying every slot that read them. Returns how many went.
std::size_t DeleteAnimationGraphNodesUVE(std::vector<Scene::AnimationGraphNodeUVE>& nodes,
                                         const std::vector<std::uint32_t>& ids);

/// Copies the nodes (never the Output) with new ids, offset by `offset`. Wires between copied
/// nodes follow the copies; wires to nodes outside the set are left empty, since a node feeds one
/// slot only. Returns the new ids, in the order given.
std::vector<std::uint32_t> DuplicateAnimationGraphNodesUVE(std::vector<Scene::AnimationGraphNodeUVE>& nodes,
                                                           const std::vector<std::uint32_t>& ids,
                                                           Math::Vector2UVE offset);

/// Adds an empty input slot to a Blend Space (with a point one past the last) or a State Machine.
/// False for other kinds or at the input limit.
bool AddAnimationGraphInputSlotUVE(std::vector<Scene::AnimationGraphNodeUVE>& nodes, std::uint32_t target);

/// Removes input `slot` of a Blend Space or State Machine, keeping at least one. A State Machine's
/// transitions touching that state go, later states shift down, and the entry state stays in range.
bool RemoveAnimationGraphInputSlotUVE(std::vector<Scene::AnimationGraphNodeUVE>& nodes, std::uint32_t target,
                                      std::size_t slot);

} // namespace UVE::Editor
