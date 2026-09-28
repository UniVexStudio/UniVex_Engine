// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumAnimationGraphNodesUVE = 256U;
inline constexpr std::size_t kMaximumAnimationParametersUVE = 128U;
inline constexpr std::size_t kMaximumAnimationNodeInputsUVE = 32U;
inline constexpr std::size_t kMaximumAnimationTransitionsUVE = 128U;
inline constexpr std::size_t kMaximumAnimationNameBytesUVE = 128U;
/// LayeredBlend: most bone branches one node can name.
inline constexpr std::size_t kMaximumAnimationLayerBonesUVE = 256U;
/// A transition whose From is this leaves whichever state is active: an "any state" transition.
inline constexpr std::uint32_t kAnyAnimationStateUVE = 0xFFFFFFFFU;

/// One node of an animation graph. Every kind produces a pose from its inputs; the graph's single
/// Output node's pose is what the target shows.
enum class AnimationGraphNodeKindUVE : std::uint8_t {
    /// The result. Exactly one per graph, with one input.
    Output = 0,
    /// Plays a clip.
    Clip,
    /// Mixes two inputs by a weight: 0 is the first, 1 the second.
    Blend2,
    /// Places its inputs along a line at `points` and mixes the two either side of a parameter -
    /// walk at 1, jog at 3, run at 6, driven by speed.
    BlendSpace1D,
    /// Lays the second input's motion on top of the first, scaled by a weight: a breathing or
    /// recoil layer that works over any base.
    Additive,
    /// Plays the second input once over the first when a trigger fires, fading in and out: an
    /// attack, a wave, a hit reaction.
    OneShot,
    /// Runs its input faster or slower.
    TimeScale,
    /// Its inputs are states; transitions move between them on conditions, crossfading.
    StateMachine,
    /// Places its inputs on a plane at `points2D` and mixes them by how close a two-parameter
    /// position is to each - strafing by velocity X and Z, aiming by yaw and pitch.
    BlendSpace2D,
    /// Plays the input a parameter picks (a Bool 0/1, or a Float rounded to an index), fading when
    /// the pick changes: stance by weapon, idle by mood.
    Select,
    /// Lays the second input over the first on the bones under `bones` only (each named bone and
    /// everything below it), by a weight: shoot with the upper body while the legs run.
    LayeredBlend,
    /// Jumps its input to `value` seconds when its trigger fires, then plays on from there.
    TimeSeek,
};

enum class AnimationParameterTypeUVE : std::uint8_t {
    Float = 0,
    Bool,
    /// Set by a script or the Inspector; cleared by the first transition or one-shot that uses it.
    Trigger,
};

/// A named value the graph reads - speed, grounded, attack. Scripts and the Inspector set them;
/// nodes and transitions only read them.
struct AnimationParameterUVE final {
    std::string name;
    AnimationParameterTypeUVE type = AnimationParameterTypeUVE::Float;
    float value = 0.0F;

    [[nodiscard]] bool operator==(const AnimationParameterUVE&) const = default;
};

enum class AnimationConditionUVE : std::uint8_t {
    /// As soon as the From state is active.
    Always = 0,
    /// When the From state's animation reaches its end.
    AtEnd,
    ParameterGreater,
    ParameterLess,
    ParameterTrue,
    ParameterFalse,
    /// When the trigger parameter fires; the trigger is consumed.
    Triggered,
};

/// A move between two states of a StateMachine node. States are indices into the node's inputs.
struct AnimationTransitionUVE final {
    std::uint32_t fromState = kAnyAnimationStateUVE;
    std::uint32_t toState = 0U;
    AnimationConditionUVE condition = AnimationConditionUVE::Always;
    std::string parameter;
    float threshold = 0.0F;
    /// Crossfade length in seconds; 0 cuts.
    float fadeSeconds = 0.2F;

    [[nodiscard]] bool operator==(const AnimationTransitionUVE&) const = default;
};

struct AnimationGraphNodeUVE final {
    /// Unique within the graph and never 0; inputs refer to nodes by it.
    std::uint32_t id = 0U;
    AnimationGraphNodeKindUVE kind = AnimationGraphNodeKindUVE::Clip;
    std::string name;
    /// Where the graph editor draws it.
    Math::Vector2UVE position{};
    /// Child node ids, in the order the kind gives them meaning. 0 is an empty slot.
    std::vector<std::uint32_t> inputs;

    // Clip
    Asset::AssetGuidUVE clip{};
    bool loop = true;
    /// Clip playback rate, and TimeScale's rate when it has no parameter.
    float speed = 1.0F;

    /// The parameter a Blend2 / BlendSpace1D / Additive weight, a TimeScale rate or a OneShot
    /// trigger reads. Empty uses `value`.
    std::string parameter;
    /// The weight or position used when there is no parameter.
    float value = 0.5F;
    /// BlendSpace1D: one ascending position per input.
    std::vector<float> points;
    /// BlendSpace2D: one position per input, anywhere on the plane.
    std::vector<Math::Vector2UVE> points2D;
    /// Blend Space 1D / 2D: the area its editor shows (1D uses X only). Points may sit outside it.
    Math::Vector2UVE areaMin{-1.0F, -1.0F};
    Math::Vector2UVE areaMax{1.0F, 1.0F};
    /// BlendSpace2D: the second axis's parameter, and its value when there is none.
    std::string parameterY;
    float valueY = 0.0F;
    /// LayeredBlend: the bones whose branches take the layer. Empty takes the whole body.
    std::vector<std::string> bones;
    /// Select: start the picked input over from its beginning when it is picked.
    bool restart = true;
    /// OneShot fade in and out, in seconds.
    float fadeSeconds = 0.2F;
    /// Blend / Blend Space / Additive: the inputs keep in step - the heaviest one leads, the others
    /// play at its phase, so a walk and a run blend with their feet together.
    bool sync = false;
    /// StateMachine: the state it starts in, and the moves between states.
    std::uint32_t entryState = 0U;
    std::vector<AnimationTransitionUVE> transitions;

    [[nodiscard]] bool operator==(const AnimationGraphNodeUVE&) const = default;
};

/// What a node remembers between steps. Kept beside the graph, index for index, and rebuilt
/// whenever the graph's shape changes; never saved.
struct AnimationGraphNodeStateUVE final {
    /// Clip: seconds into the clip.
    double timeSeconds = 0.0;
    /// StateMachine: the active state and the one fading out.
    std::uint32_t activeState = 0U;
    std::uint32_t previousState = kAnyAnimationStateUVE;
    float fadeElapsedSeconds = 0.0F;
    float fadeSeconds = 0.0F;
    /// OneShot: playing, and for how long.
    bool shotActive = false;
    float shotElapsedSeconds = 0.0F;
    bool started = false;
    /// How much this node counted in the last step's output, 0..1: what the editor shows on wires.
    float weight = 0.0F;
    /// StateMachine, inertialized transition: how far the pose that was showing is from the state
    /// just entered, per channel, fading out over `inertialSeconds`.
    std::vector<Math::Vector3UVE> inertialPosition;
    std::vector<Math::QuaternionUVE> inertialRotation;
    std::vector<Math::Vector3UVE> inertialScale;
    float inertialElapsedSeconds = 0.0F;
    float inertialSeconds = 0.0F;

    [[nodiscard]] bool operator==(const AnimationGraphNodeStateUVE&) const = default;
};

/// AnimationTree's own state: an animation graph evaluated every frame. What it moves, which
/// channels and whether it runs live in its AnimationMixer base (AnimationMixerComponentUVE).
struct AnimationTreeComponentUVE final {
    std::vector<AnimationParameterUVE> parameters;
    /// A new tree starts as Output fed by one Clip, so picking a clip is all it takes to play.
    std::vector<AnimationGraphNodeUVE> nodes = MakeDefaultAnimationGraphUVE();

    // ---- Runtime state, written by the tree; never saved ----------------------------------------
    std::vector<AnimationGraphNodeStateUVE> nodeStates;
    /// The state machine state names currently active, for the Inspector while playing.
    std::string activeStates;
    /// The root bone's ground travel over the last step, in the skeleton's space (root motion on).
    Math::Vector3UVE rootMotionDelta{};
    /// Clip events the last step passed, in the clips that counted most (weight at least a half).
    std::vector<std::string> firedEvents;

    [[nodiscard]] static std::vector<AnimationGraphNodeUVE> MakeDefaultAnimationGraphUVE() {
        AnimationGraphNodeUVE output;
        output.id = 1U;
        output.kind = AnimationGraphNodeKindUVE::Output;
        output.name = "Output";
        output.position = Math::Vector2UVE{320.0F, 0.0F};
        output.inputs = {2U};
        AnimationGraphNodeUVE clip;
        clip.id = 2U;
        clip.kind = AnimationGraphNodeKindUVE::Clip;
        clip.name = "Clip";
        return {output, clip};
    }

    /// Authored data only: runtime state is ignored.
    [[nodiscard]] bool HasSameSettingsUVE(const AnimationTreeComponentUVE& other) const {
        return parameters == other.parameters && nodes == other.nodes;
    }

    [[nodiscard]] bool operator==(const AnimationTreeComponentUVE&) const = default;
};

/// Why a graph is malformed, or empty when it is not: exactly one Output, unique non-zero ids, every
/// input a real node or an empty slot (0), no node used twice or in a cycle, kind-specific input
/// counts, ascending blend-space points, transitions between real states, unique parameter names,
/// and everything within its bounds. An empty slot or a parameter name nobody declared is allowed -
/// a graph half-built in the editor is still a graph - and simply reads as no pose / its default.
[[nodiscard]] std::string DescribeAnimationGraphProblemUVE(const AnimationTreeComponentUVE& component);

/// DescribeAnimationGraphProblemUVE finds nothing. False, too, if checking runs out of memory.
[[nodiscard]] bool IsAnimationTreeComponentValidUVE(const AnimationTreeComponentUVE& component) noexcept;

/// A fresh id one past the highest in use.
[[nodiscard]] std::uint32_t NextAnimationGraphNodeIdUVE(const std::vector<AnimationGraphNodeUVE>& nodes) noexcept;

} // namespace UVE::Scene
