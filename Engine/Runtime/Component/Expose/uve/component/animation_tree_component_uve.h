// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
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

inline constexpr std::size_t kMaximumAnimationGraphObjectsUVE = 256U;
inline constexpr std::size_t kMaximumAnimationParametersUVE = 128U;
inline constexpr std::size_t kMaximumAnimationObjectInputsUVE = 32U;
inline constexpr std::size_t kMaximumAnimationTransitionsUVE = 128U;
inline constexpr std::size_t kMaximumAnimationNameBytesUVE = 128U;
inline constexpr std::size_t kMaximumAnimationTransitionConditionsUVE = 16U;
/// LayeredBlend: most bone branches one object can name.
inline constexpr std::size_t kMaximumAnimationLayerBonesUVE = 256U;
/// A transition whose From is this leaves whichever state is active: an "any state" transition.
inline constexpr std::uint32_t kAnyAnimationStateUVE = 0xFFFFFFFFU;

/// One object of an animation graph. Every kind produces a pose from its inputs; the graph's single
/// Output object's pose is what the target shows.
enum class AnimationGraphObjectKindUVE : std::uint8_t {
    /// The result. Exactly one per graph, with one input.
    Output = 0,
    /// Plays a clip.
    Clip,
    /// Mixes two inputs by a weight: 0 is the first, 1 the second.
    Blend2,
    /// Places its animations (`blendPoints`) along a line and mixes the two either side of a
    /// parameter - walk at 1, jog at 3, run at 6, driven by speed. No inputs.
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
    /// Places its animations (`blendPoints`) on a plane, joins them into triangles, and mixes the
    /// corners of the triangle a two-parameter position is in - strafing by velocity X and Z,
    /// aiming by yaw and pitch. Needs three points that make a triangle before it plays.
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
/// objects and transitions only read them.
struct AnimationParameterUVE final {
    std::string name;
    AnimationParameterTypeUVE type = AnimationParameterTypeUVE::Float;
    float value = 0.0F;

    [[nodiscard]] bool operator==(const AnimationParameterUVE&) const = default;
};

enum class AnimationConditionUVE : std::uint8_t {
    /// Holds always. Kept so older saves read back; a transition with no conditions is the same.
    Always = 0,
    /// When the From state's animation reaches its end.
    AtEnd,
    ParameterGreater,
    ParameterLess,
    ParameterTrue,
    ParameterFalse,
    /// When the trigger parameter fires; the trigger is consumed once the transition is taken.
    Triggered,
};

/// One test a transition makes. A transition's tests must all hold for it to be taken.
struct AnimationTransitionConditionUVE final {
    AnimationConditionUVE condition = AnimationConditionUVE::ParameterTrue;
    std::string parameter;
    float threshold = 0.0F;

    [[nodiscard]] bool operator==(const AnimationTransitionConditionUVE&) const = default;
};

/// Where the state entered starts playing.
enum class AnimationTransitionStartUVE : std::uint8_t {
    /// From its beginning.
    Restart = 0,
    /// At the phase the state it leaves had reached, so a stride carries on through the change.
    InStep,
    /// Where it was left last time it played.
    Continue,
};

/// The shape of a crossfade's weight over its length. Inertialized transitions keep their own decay.
enum class AnimationTransitionCurveUVE : std::uint8_t {
    Linear = 0,
    EaseIn,
    EaseOut,
    EaseInOut,
};

/// A move between two states of a StateMachine object. States are indices into the object's inputs.
/// Transitions out of a state are tried in list order, so the first is the most important.
struct AnimationTransitionUVE final {
    std::uint32_t fromState = kAnyAnimationStateUVE;
    std::uint32_t toState = 0U;
    /// All must hold. None: the transition is taken as soon as it may be.
    std::vector<AnimationTransitionConditionUVE> conditions;
    /// When set (0..1), the From state must have played at least this far through its cycle first.
    float exitPhase = -1.0F;
    AnimationTransitionStartUVE start = AnimationTransitionStartUVE::Restart;
    /// Crossfade length in seconds; 0 cuts.
    float fadeSeconds = 0.2F;
    AnimationTransitionCurveUVE curve = AnimationTransitionCurveUVE::Linear;
    /// Whether another transition may take over while this one is still fading.
    bool interruptible = true;
    /// Off: kept, but never taken.
    bool enabled = true;

    [[nodiscard]] bool operator==(const AnimationTransitionUVE&) const = default;
};

/// How a Blend Space turns its position into what plays.
enum class AnimationBlendModeUVE : std::uint8_t {
    /// Mixes the points around the position: the two either side on a line, the three corners of
    /// the triangle it is in on a plane.
    Blend = 0,
    /// Plays the nearest point alone. When another point becomes clearly nearer it takes over,
    /// handed over across `fadeSeconds` (inertialized or crossfaded, as the mixer's transitions are).
    Nearest,
    /// Nearest, and the point taking over starts at the phase the last one had, so a stride carries
    /// on through the switch.
    NearestInStep,
};

/// One animation of a Blend Space, where it sits. A blend space holds its animations itself, the
/// way a Clip holds its clip: they are not graph inputs, so the graph shows the space as one box
/// and its editor shows the points.
struct AnimationBlendPointUVE final {
    /// On the line (1D uses x only) or the plane.
    Math::Vector2UVE position{};
    Asset::AssetGuidUVE clip{};
    float speed = 1.0F;
    bool loop = true;

    [[nodiscard]] bool operator==(const AnimationBlendPointUVE&) const = default;
};

struct AnimationGraphObjectUVE final {
    /// Unique within the graph and never 0; inputs refer to objects by it.
    std::uint32_t id = 0U;
    AnimationGraphObjectKindUVE kind = AnimationGraphObjectKindUVE::Clip;
    std::string name;
    /// Where the graph editor draws it.
    Math::Vector2UVE position{};
    /// Child object ids, in the order the kind gives them meaning. 0 is an empty slot.
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
    /// Blend Space 1D / 2D: its animations. A 1D space keeps them in rising x.
    std::vector<AnimationBlendPointUVE> blendPoints;
    /// Blend Space 1D / 2D: the area its editor shows (1D uses X only). Points may sit outside it.
    Math::Vector2UVE areaMin{-1.0F, -1.0F};
    Math::Vector2UVE areaMax{1.0F, 1.0F};
    /// BlendSpace2D: the second axis's parameter, and its value when there is none.
    std::string parameterY;
    float valueY = 0.0F;
    /// Blend Space 1D / 2D: how the position picks what plays.
    AnimationBlendModeUVE blendMode = AnimationBlendModeUVE::Blend;
    /// Blend Space 1D / 2D: how long the position takes to follow its parameters, as the time to
    /// close half the gap (a critically damped spring, so it never overshoots). 0 follows at once.
    float smoothingSeconds = 0.0F;
    /// LayeredBlend: the bones whose branches take the layer. Empty takes the whole body.
    std::vector<std::string> bones;
    /// Select: start the picked input over from its beginning when it is picked.
    bool restart = true;
    /// OneShot fade in and out, Select's fade, and a Nearest blend space's hand-over, in seconds.
    float fadeSeconds = 0.2F;
    /// Blend / Blend Space / Additive: the inputs keep in step - the heaviest one leads, the others
    /// play at its phase, so a walk and a run blend with their feet together.
    bool sync = false;
    /// StateMachine: the state it starts in, and the moves between states.
    std::uint32_t entryState = 0U;
    std::vector<AnimationTransitionUVE> transitions;
    /// StateMachine: where its state view draws each state (by slot), and its Entry and Any boxes.
    /// Fewer positions than states is fine: the rest are laid out on a grid.
    std::vector<Math::Vector2UVE> statePositions;
    Math::Vector2UVE entryPosition{-240.0F, 0.0F};
    Math::Vector2UVE anyPosition{-240.0F, 120.0F};

    [[nodiscard]] bool operator==(const AnimationGraphObjectUVE&) const = default;
};

/// What a object remembers between steps. Kept beside the graph, index for index, and rebuilt
/// whenever the graph's shape changes; never saved.
struct AnimationGraphObjectStateUVE final {
    /// Clip: seconds into the clip.
    double timeSeconds = 0.0;
    /// StateMachine: the active state and the one fading out.
    std::uint32_t activeState = 0U;
    std::uint32_t previousState = kAnyAnimationStateUVE;
    float fadeElapsedSeconds = 0.0F;
    float fadeSeconds = 0.0F;
    /// StateMachine: the curve of the crossfade under way, whether it may be interrupted, the last
    /// transition taken (an index into `transitions`, kAnyAnimationStateUVE for none) and how long
    /// the active state has been active.
    AnimationTransitionCurveUVE fadeCurve = AnimationTransitionCurveUVE::Linear;
    bool fadeInterruptible = true;
    std::uint32_t lastTransition = kAnyAnimationStateUVE;
    float stateSeconds = 0.0F;
    /// OneShot: playing, and for how long.
    bool shotActive = false;
    float shotElapsedSeconds = 0.0F;
    bool started = false;
    /// Blend Space: each point's place in its clip, and whether it has played yet.
    std::vector<double> pointTimes;
    std::vector<bool> pointStarted;
    /// Blend Space: the smoothed position and how fast it is moving; `blendAtSet` once it has one.
    Math::Vector2UVE blendAt{};
    Math::Vector2UVE blendVelocity{};
    bool blendAtSet = false;
    /// Blend Space: how much each point counted in the last step (what the editor shows).
    std::vector<float> pointWeights;
    /// Blend Space, Nearest: the point playing; kAnyAnimationStateUVE before the first step.
    std::uint32_t nearestPoint = kAnyAnimationStateUVE;
    /// Blend Space 2D: its triangles (point indices), and the points they were made from.
    std::vector<std::array<std::uint32_t, 3>> triangles;
    std::vector<Math::Vector2UVE> triangulatedPoints;
    /// How much this object counted in the last step's output, 0..1: what the editor shows on wires.
    float weight = 0.0F;
    /// StateMachine, inertialized transition: how far the pose that was showing is from the state
    /// just entered, per channel, fading out over `inertialSeconds`.
    std::vector<Math::Vector3UVE> inertialPosition;
    std::vector<Math::QuaternionUVE> inertialRotation;
    std::vector<Math::Vector3UVE> inertialScale;
    float inertialElapsedSeconds = 0.0F;
    float inertialSeconds = 0.0F;

    [[nodiscard]] bool operator==(const AnimationGraphObjectStateUVE&) const = default;
};

/// AnimationTree's own state: an animation graph evaluated every frame. What it moves, which
/// channels and whether it runs live in its AnimationMixer base (AnimationMixerComponentUVE).
struct AnimationTreeComponentUVE final {
    std::vector<AnimationParameterUVE> parameters;
    /// A new tree starts as Output fed by one Clip, so picking a clip is all it takes to play.
    std::vector<AnimationGraphObjectUVE> objects = MakeDefaultAnimationGraphUVE();

    // ---- Runtime state, written by the tree; never saved ----------------------------------------
    std::vector<AnimationGraphObjectStateUVE> objectStates;
    /// The state machine state names currently active, for the Inspector while playing.
    std::string activeStates;
    /// The root bone's ground travel over the last step, in the skeleton's space (root motion on).
    Math::Vector3UVE rootMotionDelta{};
    /// Clip events the last step passed, in the clips that counted most (weight at least a half).
    std::vector<std::string> firedEvents;

    [[nodiscard]] static std::vector<AnimationGraphObjectUVE> MakeDefaultAnimationGraphUVE() {
        AnimationGraphObjectUVE output;
        output.id = 1U;
        output.kind = AnimationGraphObjectKindUVE::Output;
        output.name = "Output";
        output.position = Math::Vector2UVE{320.0F, 0.0F};
        output.inputs = {2U};
        AnimationGraphObjectUVE clip;
        clip.id = 2U;
        clip.kind = AnimationGraphObjectKindUVE::Clip;
        clip.name = "Clip";
        return {output, clip};
    }

    /// Authored data only: runtime state is ignored.
    [[nodiscard]] bool HasSameSettingsUVE(const AnimationTreeComponentUVE& other) const {
        return parameters == other.parameters && objects == other.objects;
    }

    [[nodiscard]] bool operator==(const AnimationTreeComponentUVE&) const = default;
};

/// Why a graph is malformed, or empty when it is not: exactly one Output, unique non-zero ids, every
/// input a real object or an empty slot (0), no object used twice or in a cycle, kind-specific input
/// counts, ascending blend-space points, transitions between real states, unique parameter names,
/// and everything within its bounds. An empty slot or a parameter name nobody declared is allowed -
/// a graph half-built in the editor is still a graph - and simply reads as no pose / its default.
[[nodiscard]] std::string DescribeAnimationGraphProblemUVE(const AnimationTreeComponentUVE& component);

/// DescribeAnimationGraphProblemUVE finds nothing. False, too, if checking runs out of memory.
[[nodiscard]] bool IsAnimationTreeComponentValidUVE(const AnimationTreeComponentUVE& component) noexcept;

/// Older graphs fed a Blend Space's points through input slots. Moves each Clip on such a slot into
/// the space's own points (its clip, speed and loop) and removes the Clip object; any other object on a
/// slot is left in the graph, unconnected, and its point keeps its place with no animation.
/// Returns how many inputs could not be folded in (0 when all were Clips or empty).
std::size_t MigrateBlendSpaceInputsUVE(std::vector<AnimationGraphObjectUVE>& objects,
                                       const std::vector<std::vector<Math::Vector2UVE>>& legacyPositions);

/// A fresh id one past the highest in use.
[[nodiscard]] std::uint32_t NextAnimationGraphObjectIdUVE(const std::vector<AnimationGraphObjectUVE>& objects) noexcept;

} // namespace UVE::Scene
