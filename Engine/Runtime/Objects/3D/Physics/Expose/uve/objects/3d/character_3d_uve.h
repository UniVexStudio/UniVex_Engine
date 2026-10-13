// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/gameplay/pawn_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/physics/character_body_motion_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Character3D: a body moved by its own code rather than by forces - a player, an NPC, an
/// enemy. Object3D > PhysicsObject3D > SolidBody3D > Character3D.
///
/// A new one is ready to walk: it comes with a person-sized capsule and the built-in movement on.
/// It moves once something possesses it - drop a Player3D for keyboard play, or drive its pawn
/// from any controller. No rigid body is attached - the controller owns all of its motion.
struct Character3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Character3D";

    /// Person-sized: 1.8 m tall, 0.8 m across.
    ColliderComponentUVE collider = MakeDefaultColliderUVE();
    CharacterControllerComponentUVE controller{};
    // Only the kind is authored: possession links and pawn input are runtime state. AI by
    // default, so a definition-built body never answers the keyboard unless something opts it in.
    ControllerKindUVE controllerKind = ControllerKindUVE::AI;

    [[nodiscard]] static ColliderComponentUVE MakeDefaultColliderUVE() noexcept {
        ColliderComponentUVE collider{};
        collider.shapeType = ColliderShapeTypeUVE::Capsule;
        collider.radius = 0.4F;
        collider.height = 1.8F;
        return collider;
    }
};

[[nodiscard]] bool IsCharacter3DObjectDefinitionValidUVE(const Character3DObjectDefinitionUVE& value) noexcept;

/// Applies the SolidBody3D base (and through it PhysicsObject3D and Object3D), then the collider
/// and the controller, each only where the entity does not already have one.
void ApplyCharacter3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                           const Character3DObjectDefinitionUVE& value);

/// Resolves `entity`'s movement intent for this step from possession: a mutually-possessed pawn
/// with finite input steers from that input, faced by the body's own yaw when the driving
/// controller is Player-kind (keyboard intent is look-relative; AI authors world-space intent
/// and passes through unrotated). Anything else - no pawn, no mutual link, non-finite input -
/// stands still: unpossessed means undriven.
[[nodiscard]] Physics::CharacterMotionInputUVE ResolveCharacterMotionUVE(
    IEntityManagerUVE& entityManager, EntityUVE entity);

// =================================================================================================
// The node runtime: where a Character3D actually walks.
//
// WHAT THIS IS FOR. Everything above is the authoring recipe - which components a Character3D is
// made of. What follows is the movement itself: one bounded, deterministic step of a character
// body through a world of colliders, with the rules a player feels - floors it stands on, slopes
// it does not slide down when standing still, steps it walks up without jumping, ceilings it
// slides along, moving platforms that carry it and let go of it honestly.
//
// WHY IT LIVES HERE AND NOT IN Engine/Runtime/Physics. The physics layer owns collision: which
// bodies overlap, along which axis, how deep (ICollisionSystemUVE, ShapeCastSystemUVE,
// CharacterControllerUVE). Those are *world* questions with one right answer. This file owns
// *character policy*: what counts as a floor for this body, when a step is worth climbing, what a
// platform's departure does to a velocity. Those are answers a game designer changes per game, and
// they belong to the node that represents the character, exactly as CharacterBody3D's rules
// belong to the node and not to the physics server behind it.
//
// The seam between the two is ICharacterWorldQueryUVE below: the mover asks the world a handful of
// questions and never learns how they are answered. Engine/Runtime/Physics answers them over the
// collision system (Physics::CharacterWorldQueryUVE); a test answers them over hand-written boxes;
// a future backend answers them over whatever replaces both, and this file does not change.
//
// The layer rule this respects: Engine/Runtime/Objects/* sits *below* Scene and Physics, so this
// header names no physics type at all. That is not an accident to be refactored away later - it is
// what lets a headless unit test move a character with no entity manager, no scene graph, and no
// collision system in sight.
// =================================================================================================

/// What one surface contact means to a character body, from the body's own up direction.
enum class CharacterSurfaceKindUVE : std::uint8_t {
    /// Walkable: the body can stand on it, jump from it, and slow to a stop on it.
    Floor = 0,
    /// Too steep to stand on and not overhead: the body slides along it instead of stopping.
    Wall,
    /// Overhead: the body stops rising, and slides along it when Slide On Ceiling is on.
    Ceiling,
    /// Degenerate input (a zero or non-finite normal) - never a real contact, always a refusal.
    Invalid,
};

/// Classifies `surfaceNormal` against `upDirection`. `floorMaxAngleDegrees` is measured from the
/// up axis: a normal within it is a floor, a normal within it of *straight down* is a ceiling, and
/// everything between is a wall. `outAngleDegrees` receives the angle between the normal and the
/// up axis (0 = flat floor, 90 = vertical wall, 180 = flat ceiling). Returns
/// CharacterSurfaceKindUVE::Invalid for a normal that cannot be classified, and writes 0 into
/// `outAngleDegrees` so a caller never reads a stale angle as if it belonged to this surface.
[[nodiscard]] CharacterSurfaceKindUVE ClassifyCharacterSurfaceUVE(
    const Math::Vector3UVE& surfaceNormal, const Math::Vector3UVE& upDirection,
    float floorMaxAngleDegrees, float& outAngleDegrees) noexcept;

/// Where the character's step finds its floor and what it does with it. Every field is validated
/// and clamped before use: a hand-edited or script-written value can never make the mover produce
/// a non-finite result, it can only make it behave like the nearest sane value.
struct CharacterMotionConfigUVE final {
    /// The body's own up. Defaults to world up because the engine's gravity is along -Y; a future
    /// gravity that is not vertical is why this is a parameter and not a constant.
    Math::Vector3UVE upDirection{0.0F, 1.0F, 0.0F};

    /// Steepest surface still walked on, measured from `upDirection`. Godot's Floor Max Angle.
    float floorMaxAngleDegrees = 45.0F;
    /// How far past Floor Max Angle a surface may lean and still be slid along. A contact between
    /// the floor limit and this band is a steep ramp rather than a wall: the body stops on it
    /// instead of creeping up it. This is Godot's Wall Min Slide Angle, given the only reading of
    /// it that has a mechanical meaning here. 0 slides on every wall.
    float wallMinSlideAngleDegrees = 15.0F;
    /// The skin the body keeps against surfaces. Motion stops this far short of a surface instead
    /// of touching it, and depenetration pushes out this far past a contact. Without a margin a
    /// body resting exactly on a floor is "touching" rather than overlapping, and the next sweep
    /// against that same floor is undefined - the margin is what makes resting contact a
    /// well-posed question. Godot's Safe Margin.
    float safeMargin = 0.001F;
    /// How many pieces one move is cut into at corners and walls. More is smoother and costs more.
    std::size_t maxSlides = 8U;
    /// Cap on the collision records kept for one move; the move still resolves, it just stops
    /// recording. Reported as CharacterMotionResultUVE::contactsTruncated.
    std::size_t maximumContacts = 64U;
    /// How far below its feet a body that was just standing looks for the floor again, so walking
    /// down a step follows it instead of launching the body off the edge. 0 disables the probe
    /// beyond the minimum needed to keep a body resting on a floor registered as resting on it.
    float floorSnapLength = 0.1F;
    /// Highest step walked up without jumping. 0 turns stepping off.
    float maxStepHeight = 0.3F;
    /// The least horizontal ground a step must gain to count as a step. A contact with less
    /// headroom in front of it is a wall, and the body stops against it rather than hopping.
    float minStepWidth = 0.02F;
    /// On hitting a ceiling, keep sliding along it. Off, the whole move stops there.
    bool slideOnCeiling = true;
    /// On the floor, stop at a wall instead of sliding along it - no wall-running, no corner
    /// slipping. Godot's Floor Block On Wall.
    bool floorBlockOnWall = false;
    /// Standing still on a slope stays standing still instead of creeping downhill. Godot's Floor
    /// Stop On Slope. Defaults on, matching CharacterControllerComponentUVE, so a body dropped
    /// onto a hill does not creep unless the author asked it to.
    bool floorStopOnSlope = true;
    /// Walking up or down a slope keeps the horizontal speed the walk started with, instead of
    /// slowing uphill and speeding downhill as the slope projects the motion. Godot's Floor
    /// Constant Speed.
    bool floorConstantSpeed = false;
    /// Whether floor-like surfaces hold the body up at all. Grounded characters say yes; a
    /// character in CharacterMotionModeUVE::Floating says no, and then every surface is a wall it
    /// slides along - which is that mode's documented contract, not an approximation of it.
    bool detectFloor = true;
    /// What the platform being left behind does to the body's velocity.
    CharacterPlatformLeaveModeUVE platformOnLeave = CharacterPlatformLeaveModeUVE::AddVelocity;
    /// The fastest a platform's motion is allowed to carry the body. A platform that teleports
    /// (a script setting a transform) would otherwise fling its rider across the level; past this,
    /// this step treats the platform as standing still. Metres per second.
    float maximumPlatformSpeed = 50.0F;
    /// True when the intent step jumped this step. A jump means the body is deliberately leaving
    /// the floor, so the floor probe does not pull it back down and a step is never climbed.
    bool jumpedThisStep = false;
};

/// The part of a character that survives from one step to the next: velocity, what it was standing
/// on, and what the last move did. CharacterControllerComponentUVE carries exactly these fields
/// (its runtime-state block), and MakeCharacterMotionStateUVE/StoreCharacterMotionStateUVE bridge
/// the two - the component is storage, this is the value the mover works on.
struct CharacterMotionStateUVE final {
    /// Metres per second, world space. The mover writes this only for Floor Stop On Slope (a
    /// standing body is stopped) and for platform leave (the platform's gift). Floor Constant
    /// Speed reshapes this step's remaining motion, not the stored velocity; everything else is
    /// the caller's.
    Math::Vector3UVE velocity{};
    /// Standing on a floor-like surface after the last move.
    bool grounded = false;
    /// Touching a wall-like surface after the last move.
    bool onWall = false;
    /// Touching a ceiling-like surface after the last move.
    bool onCeiling = false;
    /// The floor's normal while on it; straight up otherwise. Only meaningful while `grounded`.
    Math::Vector3UVE floorNormal{0.0F, 1.0F, 0.0F};
    /// The last wall's normal, zero when no wall was met. Only meaningful while `onWall`.
    Math::Vector3UVE wallNormal{};
    /// The last ceiling's normal, zero when no ceiling was met. Only meaningful while `onCeiling`.
    Math::Vector3UVE ceilingNormal{};
    /// Seconds since the body was last on a floor (drives the caller's coyote time).
    float timeSinceOnFloor = 0.0F;
    /// Seconds a buffered jump has left to happen (drives the caller's jump buffer).
    float jumpBufferRemaining = 0.0F;
    /// What the last move actually moved the body by, after slides, steps and snapping.
    Math::Vector3UVE lastMotion{};
    /// lastMotion divided by that step's duration: the body's real speed, which is not `velocity`
    /// whenever a wall or a ceiling stopped part of the motion.
    Math::Vector3UVE realVelocity{};
    /// The platform the body is standing on, or kInvalidEntityUVE.
    EntityUVE platform{};
    /// That platform's world position at the end of the last step - the reference the next step
    /// measures its motion against, because a moving platform is a thing whose *movement* is the
    /// velocity that matters, not a number it happens to publish.
    Math::Vector3UVE platformWorldPosition{};
    /// The platform velocity measured this step. Zero while not riding one.
    Math::Vector3UVE platformVelocity{};
    /// Whether `platformWorldPosition` is a position the last step actually observed. A platform
    /// standing at the world origin is a real platform, so "nothing measured yet" cannot be
    /// written as a zero vector - it needs its own bit.
    bool hasPlatformWorldPosition = false;
};

/// One contact a move resolved against, in the shape a script wants to read it: what was hit,
/// where, along which normal, how much motion was spent on it and what was left.
struct CharacterSlideCollisionUVE final {
    EntityUVE entity{};
    /// Unit surface normal, pointing *toward the body* - the direction the body was pushed.
    Math::Vector3UVE normal{};
    /// World-space point on the body's shape where the contact happened.
    Math::Vector3UVE position{};
    /// The contacted body's own velocity, when it has one (a moving platform or a rigid body);
    /// zero for static world geometry.
    Math::Vector3UVE colliderVelocity{};
    /// The motion that reached this contact, before it was cut short.
    Math::Vector3UVE motion{};
    /// What was left of the move when the contact was recorded.
    Math::Vector3UVE remainder{};
    /// Distance actually travelled before the contact.
    float travel = 0.0F;
    /// How far the body still had to go into the surface when it was resolved. Zero for a sweep
    /// that stopped short of it, positive for one resolved out of real penetration.
    float depth = 0.0F;
    /// True when this contact was resolved by climbing over it instead of stopping against it.
    bool steppedUp = false;
};

enum class CharacterMoveCodeUVE : std::uint8_t {
    Moved = 0,
    /// The config or one of its numbers was unusable (non-finite, or no up direction).
    InvalidConfig,
    /// The state carried non-finite values; the move refused rather than propagating them.
    InvalidState,
    /// The world could not say where the body is.
    InvalidWorld,
};

/// What one Character3DUVE::MoveAndSlideUVE() did. `code == Moved` means the move ran to
/// completion; every other code means nothing moved and the reason is in `code`.
struct CharacterMotionResultUVE final {
    CharacterMoveCodeUVE code = CharacterMoveCodeUVE::InvalidConfig;

    /// What the caller asked for, before contacts, platforms or snapping had their say.
    Math::Vector3UVE requestedMotion{};
    /// What the body actually moved by: the sum of every slide, step and snap.
    Math::Vector3UVE appliedMotion{};
    /// The part of `appliedMotion` that was ordinary sliding rather than a step or a snap.
    Math::Vector3UVE slideMotion{};
    /// The part of `appliedMotion` that came from climbing a step.
    Math::Vector3UVE stepMotion{};
    /// The part that came from following the floor down (negative along up) - zero when no snap
    /// happened.
    Math::Vector3UVE snapMotion{};
    /// The part that came from pushing the body out of geometry it was already inside.
    Math::Vector3UVE depenetrationMotion{};
    /// Where the body ends up: the start center plus `appliedMotion`.
    Math::Vector3UVE finalCenter{};

    bool onFloor = false;
    bool onWall = false;
    bool onCeiling = false;
    Math::Vector3UVE floorNormal{0.0F, 1.0F, 0.0F};
    Math::Vector3UVE wallNormal{};
    Math::Vector3UVE ceilingNormal{};
    /// The floor's angle from up, in degrees (0 = flat). Only meaningful while `onFloor`.
    float floorAngleDegrees = 0.0F;
    /// True when the move was stopped short by something.
    bool blocked = false;
    /// True when the move climbed at least one step.
    bool stepUpUsed = false;
    /// True when the floor probe moved the body down onto the floor.
    bool snapUsed = false;
    /// True when the body was pushed out of geometry it started inside.
    bool depenetrated = false;
    /// How many push-out passes the move used (0 when the body started free-standing).
    std::size_t depenetrationPasses = 0U;
    /// True when the body was carried by a moving platform this step.
    bool carriedByPlatform = false;
    /// True when a value in `config` was outside the range the mover can honour and was clamped to
    /// the nearest legal value, so a caller can tell a typo from a design choice.
    bool configClamped = false;
    /// True when the body left a platform this step, so `platformVelocity` is the velocity that
    /// was handed to it (and already applied to the body's velocity when the mode says so).
    bool leftPlatform = false;
    /// The measured velocity of the platform being ridden or just left.
    Math::Vector3UVE platformVelocity{};

    /// Number of slide iterations the move took.
    std::size_t slideCount = 0U;
    /// Number of contacts resolved (each slide resolves at most one, a step or a depenetration
    /// may add more).
    std::size_t contactCount = 0U;
    /// How many contacts were dropped because `maximumContacts` was reached.
    std::size_t droppedContactCount = 0U;
    /// True when contacts were dropped, so a caller that cares can raise the cap.
    bool contactsTruncated = false;

    /// The contacts in the order they were resolved.
    std::vector<CharacterSlideCollisionUVE> collisions;

    [[nodiscard]] bool IsAcceptedUVE() const noexcept {
        return code == CharacterMoveCodeUVE::Moved;
    }
    [[nodiscard]] bool IsOnFloorOnlyUVE() const noexcept {
        return onFloor && !onWall && !onCeiling;
    }
    [[nodiscard]] bool IsOnWallOnlyUVE() const noexcept {
        return onWall && !onFloor && !onCeiling;
    }
    [[nodiscard]] bool IsOnCeilingOnlyUVE() const noexcept {
        return onCeiling && !onFloor && !onWall;
    }
    [[nodiscard]] std::size_t GetCollisionCountUVE() const noexcept {
        return collisions.size();
    }
    /// The `index`-th contact, or nullptr past the end - indexing never asserts here because a
    /// script asking for a contact that was truncated is a normal script mistake, not a bug.
    [[nodiscard]] const CharacterSlideCollisionUVE* GetCollisionUVE(std::size_t index) const noexcept {
        return index < collisions.size() ? &collisions[index] : nullptr;
    }
};

/// The world, as far as a character is concerned. Deliberately a handful of questions - where am
/// I, what am I inside of at a pose, what do I hit if I move, and how is that thing moving -
/// because those are the only facts a kinematic character needs, and every physics backend can
/// answer them.
///
/// Implementations must be side-effect free with respect to the world: a query describes it, it
/// never moves anything. Sweeps are conservative (they may report a hit the exact shape would
/// miss, which costs a slide, never a tunnel). Overlaps at a pose use the same shape the sweep
/// uses, so a recovery and a sweep cannot disagree about whether that pose is free.
class ICharacterWorldQueryUVE {
public:
    virtual ~ICharacterWorldQueryUVE() = default;

    /// Where the body is now, at the center of its collision shape.
    [[nodiscard]] virtual Math::Vector3UVE GetCenterUVE() const = 0;

    /// Everything the body's shape would penetrate if its center were `center`, with the surface
    /// normal pointing toward the body and the depth it would be inside by. Empty when that pose
    /// is free-standing. This is the question depenetration asks after a push, when the working
    /// center has left the pose `GetCenterUVE()` names.
    [[nodiscard]] virtual std::vector<CharacterSlideCollisionUVE> GetOverlapsAtUVE(
        const Math::Vector3UVE& center) const = 0;

    /// `GetOverlapsAtUVE(GetCenterUVE())`: what the body is inside of where it is now.
    [[nodiscard]] std::vector<CharacterSlideCollisionUVE> GetOverlapsUVE() const {
        return GetOverlapsAtUVE(GetCenterUVE());
    }

    /// The earliest hit of the body's shape swept from `center` along `motion`, or nothing when
    /// the path is clear. `margin` is the skin to keep: an implementation stops the reported hit
    /// short of the surface by that much, so the caller can move there and still have a
    /// well-defined query next time. `time` in the hit is measured along `motion` in [0, 1].
    [[nodiscard]] virtual std::optional<CharacterSlideCollisionUVE> SweepUVE(
        const Math::Vector3UVE& center, const Math::Vector3UVE& motion, float margin) const = 0;

    /// The velocity of `entity`'s surface - a rigid body's velocity, a platform's target velocity,
    /// zero for static geometry and for anything unknown.
    [[nodiscard]] virtual Math::Vector3UVE GetSurfaceVelocityUVE(EntityUVE entity) const = 0;

    /// Where `entity` is now, or nothing when it is gone or is not a body. Used to measure how far
    /// a ridden platform moved since the last step.
    [[nodiscard]] virtual std::optional<Math::Vector3UVE> TryGetEntityCenterUVE(EntityUVE entity) const = 0;
};

/// The Character3D mover. One instance is never needed: every entry point is static, takes the
/// world and the body's state, and returns what happened. The state struct is the only thing that
/// has to survive between calls, and the engine keeps it in the entity's own component.
class Character3DUVE final {
public:
    static constexpr std::size_t kMaximumSlidesUVE = 32U;
    static constexpr std::size_t kMaximumContactsUVE = 64U;
    /// A body that starts this deep inside geometry gets one push-out pass per step; deeper and
    /// the move still reports one, leaving the caller to teleport it rather than hoping.
    static constexpr float kMaximumSingleDepenetrationDistanceUVE = 2.0F;
    /// Below this, a motion is nothing: skipping it keeps the mover from spending slides on the
    /// last float bits of a projected vector.
    static constexpr float kMinimumMotionDistanceUVE = 1.0e-5F;
    /// A body that was standing is always probed this far, whatever Floor Snap Length says, so
    /// resting on a floor stays registered as resting on it from one step to the next.
    static constexpr float kMinimumFloorProbeUVE = 0.01F;
    /// Speeds below this count as standing still for Floor Stop On Slope.
    static constexpr float kStandingSpeedUVE = 0.1F;

    /// Moves the body by `desiredMotion` (velocity times the step's duration), sliding along what
    /// it hits, climbing steps, following a floor down, and riding platforms, then writes the
    /// body's new state back into `state`. Never applies gravity, steering or jumping: those are
    /// the caller's intent step, and this is the consequence.
    ///
    /// Deterministic: the same world, config and state always produce the same result, because
    /// contact order comes from the implementation's own stable ordering and every tie is broken
    /// by entity index rather than by iteration order.
    [[nodiscard]] static CharacterMotionResultUVE MoveAndSlideUVE(
        const ICharacterWorldQueryUVE& world, const CharacterMotionConfigUVE& config,
        CharacterMotionStateUVE& state, const Math::Vector3UVE& desiredMotion,
        float deltaTimeSeconds);

    /// One sweep, no sliding: what the body would hit moving by `motion`. This is Godot's
    /// test_move - a script asking "can I get there from here?" without moving anything.
    [[nodiscard]] static std::optional<CharacterSlideCollisionUVE> TestMoveUVE(
        const ICharacterWorldQueryUVE& world, const Math::Vector3UVE& motion, float margin);

    /// The floor under `center`, found by lifting the body by the margin and sweeping down. The
    /// lift is what makes this work for a body already resting on the floor: a sweep that starts
    /// exactly touching a surface has no defined answer, one that starts a skin above it does.
    /// Returns nothing when the nearest thing below is not floor-like within `probeDistance`.
    [[nodiscard]] static std::optional<CharacterSlideCollisionUVE> ProbeFloorUVE(
        const ICharacterWorldQueryUVE& world, const Math::Vector3UVE& center,
        const Math::Vector3UVE& upDirection, float floorMaxAngleDegrees, float probeDistance,
        float safeMargin) noexcept;

    /// `motion` with everything pointing into `normal` removed: what a body slides along a surface
    /// with. Returns `motion` unchanged for a degenerate normal or a motion already leaving it.
    [[nodiscard]] static Math::Vector3UVE SlideMotionUVE(const Math::Vector3UVE& motion,
                                                         const Math::Vector3UVE& normal) noexcept;

    /// The angle between `normal` and `upDirection` in degrees, 0 when the normal cannot be
    /// measured. Exposed because a script that wants to know how steep the floor is should not
    /// have to reinvent acos, and because the classification above is defined in these terms.
    [[nodiscard]] static float SlopeAngleDegreesUVE(const Math::Vector3UVE& normal,
                                                    const Math::Vector3UVE& upDirection) noexcept;
};

/// The mover's working state from the component the engine stores it in. Missing platform
/// bookkeeping (a component loaded from a file, or one just added) comes back as "no platform",
/// never as a stale reference - the platform is re-established by standing on something.
[[nodiscard]] CharacterMotionStateUVE MakeCharacterMotionStateUVE(
    const CharacterControllerComponentUVE& controller) noexcept;

/// Writes a moved state back into the component. Only the runtime block is touched; authored
/// settings are the author's.
void StoreCharacterMotionStateUVE(CharacterControllerComponentUVE& controller,
                                  const CharacterMotionStateUVE& state) noexcept;

/// The mover's settings from the component's authored values, all of them clamped into the ranges
/// the mover can honour: an angle past vertical, a negative margin or a zero slide count behave
/// like the nearest legal value instead of failing the move.
[[nodiscard]] CharacterMotionConfigUVE MakeCharacterMotionConfigUVE(
    const CharacterControllerComponentUVE& controller) noexcept;

} // namespace UVE::Scene
