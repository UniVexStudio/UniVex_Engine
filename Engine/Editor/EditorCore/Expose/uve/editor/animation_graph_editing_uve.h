// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "uve/component/animation_tree_component_uve.h"

namespace UVE::Editor {

/// The Anim Graph's editing operations on an AnimationGraph's objects, as pure functions so they are
/// testable without a window. Every one leaves a valid graph valid: it refuses what would break it
/// (a cycle, a second Output, deleting the Output) rather than repairing afterwards.

/// What a kind is called in the editor ("Blend Space" for BlendSpace1D...).
[[nodiscard]] const char* AnimationGraphKindLabelUVE(Scene::AnimationGraphObjectKindUVE kind) noexcept;

/// One line on what a kind does, for tooltips and the Add Object search.
[[nodiscard]] const char* AnimationGraphKindHelpUVE(Scene::AnimationGraphObjectKindUVE kind) noexcept;

/// What input `slot` of a kind means: "A"/"B", "Base"/"Layer", "State 2"...
[[nodiscard]] std::string AnimationGraphSlotLabelUVE(Scene::AnimationGraphObjectKindUVE kind, std::size_t slot);

/// Adds a object of `kind` at `position` with the empty input slots the kind needs (two for Blend,
/// Additive, One Shot, Select and Layered Blend; one for Time Scale, Time Seek and State Machine).
/// A Blend Space starts with no points and no inputs: its editor adds them. Returns its id, or 0
/// for Output (a graph has exactly one) or a full graph.
std::uint32_t AddAnimationGraphObjectUVE(std::vector<Scene::AnimationGraphObjectUVE>& objects,
                                       Scene::AnimationGraphObjectKindUVE kind, Math::Vector2UVE position);

/// True when `source` can feed input `slot` of `target`: both exist, the slot exists, the source is
/// not the Output nor the target itself, and the target is not already upstream of the source (the
/// wire would close a cycle).
[[nodiscard]] bool CanConnectAnimationGraphObjectsUVE(const std::vector<Scene::AnimationGraphObjectUVE>& objects,
                                                    std::uint32_t target, std::size_t slot, std::uint32_t source);

/// Wires `source` into input `slot` of `target`. A object feeds one slot only, so a source already
/// wired elsewhere moves here. Returns false, changing nothing, when CanConnect says no.
bool ConnectAnimationGraphObjectsUVE(std::vector<Scene::AnimationGraphObjectUVE>& objects, std::uint32_t target,
                                   std::size_t slot, std::uint32_t source);

/// Empties input `slot` of `target`. False when there is no such slot or it is already empty.
bool DisconnectAnimationGraphInputUVE(std::vector<Scene::AnimationGraphObjectUVE>& objects, std::uint32_t target,
                                      std::size_t slot);

/// Removes the objects (never the Output), emptying every slot that read them. Returns how many went.
std::size_t DeleteAnimationGraphObjectsUVE(std::vector<Scene::AnimationGraphObjectUVE>& objects,
                                         const std::vector<std::uint32_t>& ids);

/// Copies the objects (never the Output) with new ids, offset by `offset`. Wires between copied
/// objects follow the copies; wires to objects outside the set are left empty, since a object feeds one
/// slot only. Returns the new ids, in the order given.
std::vector<std::uint32_t> DuplicateAnimationGraphObjectsUVE(std::vector<Scene::AnimationGraphObjectUVE>& objects,
                                                           const std::vector<std::uint32_t>& ids,
                                                           Math::Vector2UVE offset);

/// Adds an empty input slot to a Select or a State Machine.
/// False for other kinds or at the input limit.
bool AddAnimationGraphInputSlotUVE(std::vector<Scene::AnimationGraphObjectUVE>& objects, std::uint32_t target);

/// Removes input `slot` of a Select or State Machine, keeping one. A State Machine's
/// transitions touching that state go, later states shift down, and the entry state stays in range.
bool RemoveAnimationGraphInputSlotUVE(std::vector<Scene::AnimationGraphObjectUVE>& objects, std::uint32_t target,
                                      std::size_t slot);

// ---- State Machines: states, their places in the state view, and transitions ------------------------

/// Where the state view draws state `slot`: where it was put, or a grid place when it never was.
[[nodiscard]] Math::Vector2UVE AnimationStatePositionUVE(const Scene::AnimationGraphObjectUVE& machine, std::size_t slot);

/// Puts state `slot` at `position` in the state view. False for a bad machine or slot.
bool SetAnimationStatePositionUVE(std::vector<Scene::AnimationGraphObjectUVE>& objects, std::uint32_t machine,
                                  std::size_t slot, Math::Vector2UVE position);

/// Adds a state at `position` in the view, played by a new Clip object wired into it (a new machine's
/// empty first state is used instead of adding one). Returns its slot, or nothing when full.
std::optional<std::size_t> AddAnimationStateUVE(std::vector<Scene::AnimationGraphObjectUVE>& objects, std::uint32_t machine,
                                                Math::Vector2UVE position);

/// Adds a transition `from` (a state, or kAnyAnimationStateUVE) `to` a state, last in priority and
/// with no conditions. Refuses a state to itself. Returns its index.
std::optional<std::size_t> AddAnimationTransitionUVE(std::vector<Scene::AnimationGraphObjectUVE>& objects,
                                                     std::uint32_t machine, std::uint32_t from, std::uint32_t to);

bool RemoveAnimationTransitionUVE(std::vector<Scene::AnimationGraphObjectUVE>& objects, std::uint32_t machine,
                                  std::size_t index);

/// Moves transition `index` to `newIndex` in the list: earlier is tried first.
bool MoveAnimationTransitionUVE(std::vector<Scene::AnimationGraphObjectUVE>& objects, std::uint32_t machine,
                                std::size_t index, std::size_t newIndex);

/// One line on what a transition waits for: "speed > 0.5 and grounded", "state finished, after 75%",
/// "always". "(off)" when it is switched off.
[[nodiscard]] std::string DescribeAnimationTransitionUVE(const Scene::AnimationTransitionUVE& transition);

// ---- Blend Spaces hold their animations as points, not graph inputs ----------------------------

/// Adds a point at `position` playing `clip` (empty: pick it later). A 1D space uses x only and
/// keeps its points rising, so the point lands in order. Returns its index, or nothing when
/// `space` is not a blend space, is full, or a point is already there.
std::optional<std::size_t> AddBlendSpacePointUVE(std::vector<Scene::AnimationGraphObjectUVE>& objects, std::uint32_t space,
                                                 Math::Vector2UVE position, Asset::AssetGuidUVE clip);

/// Removes point `slot`. False for a bad slot.
bool RemoveBlendSpacePointUVE(std::vector<Scene::AnimationGraphObjectUVE>& objects, std::uint32_t space, std::size_t slot);

/// Moves point `slot` to `position`. A 1D point stays between its neighbours; a 2D point may not
/// land on another. Returns false, changing nothing, when it cannot move there.
bool MoveBlendSpacePointUVE(std::vector<Scene::AnimationGraphObjectUVE>& objects, std::uint32_t space, std::size_t slot,
                            Math::Vector2UVE position);

} // namespace UVE::Editor
