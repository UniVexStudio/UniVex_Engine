// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "character_3d.h"

#include "player_3d.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "mesh_component.h"
#include "primitive_mesh_component.h"
#include "rigid_3d_component.h"
#include "solid_body_component.h"
#include "i_entity_manager.h"
#include "scalar.h"
#include "abstract_physics_objects_3d.h"
#include "i_scene_graph.h"

namespace UVE::Scene {
namespace {

/// Below this a vector is too short to say anything about direction.
constexpr float kMinimumNormalizableLength = 1.0e-12F;

/// How many contacts one step pushes the body out of. A body genuinely buried in geometry needs a
/// teleport, not a fourth push - four is where "a few things overlapped at once" stops being a
/// believable situation and starts being a broken scene, and pushing further would launch the body
/// out of it at speed instead of telling the caller about it.
constexpr std::size_t kMaximumDepenetrationPasses = 4U;

/// The margin a floor probe lifts the body by before sweeping down, as a multiple of Safe Margin.
/// Two margins plus a hair: strictly more than the skin the body keeps, so the sweep starts clear
/// of the surface rather than exactly on it - the one position a sweep has no well-defined answer.
constexpr float kFloorProbeLiftMarginFactor = 2.0F;

// =================================================================================================
// Value helpers. Every one of these is deliberately small, total, and free of engine state: the
// mover is arithmetic over a handful of vectors, and keeping it that way is what makes a step
// deterministic and this file testable without a world.
// =================================================================================================

[[nodiscard]] bool IsUsableVector(const Math::Vector3& value) noexcept {
    return Math::IsFinite(value);
}

[[nodiscard]] float VectorLength(const Math::Vector3& value) noexcept {
    const float lengthSquared = Math::LengthSquared(value);
    if (!std::isfinite(lengthSquared) || lengthSquared < 0.0F) {
        return 0.0F;
    }
    return std::sqrt(lengthSquared);
}

/// `value` scaled to unit length, or nothing when it is too short to have a direction.
[[nodiscard]] std::optional<Math::Vector3> TryNormalize(const Math::Vector3& value) noexcept {
    const float lengthSquared = Math::LengthSquared(value);
    if (!IsUsableVector(value) || !std::isfinite(lengthSquared) ||
        lengthSquared <= kMinimumNormalizableLength) {
        return std::nullopt;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return value * inverseLength;
}

[[nodiscard]] float FiniteOrZero(const float value) noexcept {
    return std::isfinite(value) ? value : 0.0F;
}

/// The component of `value` perpendicular to `axis`: what is left of a motion once the part that
/// ran along `axis` is taken out. Used to measure horizontal speed without hardcoding a world up.
[[nodiscard]] Math::Vector3 Perpendicular(const Math::Vector3& value,
                                                const Math::Vector3& axis) noexcept {
    const float axisLengthSquared = Math::LengthSquared(axis);
    if (!std::isfinite(axisLengthSquared) || axisLengthSquared <= 0.0F) {
        return value;
    }
    const float along = Math::Dot(value, axis) / axisLengthSquared;
    if (!std::isfinite(along)) {
        return value;
    }
    return value - axis * along;
}

/// The signed length of `value` along a unit axis. Non-finite dots read as zero so a caller never
/// has to special-case NaN on top of "no component along this".
[[nodiscard]] float AlongUnit(const Math::Vector3& value, const Math::Vector3& unitAxis) noexcept {
    const float along = Math::Dot(value, unitAxis);
    return std::isfinite(along) ? along : 0.0F;
}

[[nodiscard]] bool IsFiniteState(const CharacterMotionState& state) noexcept {
    return IsUsableVector(state.velocity) && IsUsableVector(state.floorNormal) &&
           IsUsableVector(state.wallNormal) && IsUsableVector(state.ceilingNormal) &&
           IsUsableVector(state.lastMotion) &&
           IsUsableVector(state.realVelocity) && IsUsableVector(state.platformWorldPosition) &&
           IsUsableVector(state.platformVelocity) && std::isfinite(state.timeSinceOnFloor) &&
           std::isfinite(state.jumpBufferRemaining);
}

// =================================================================================================
// The resolved config: the authored settings after clamping, plus the one derived number the
// classification needs. Everything below works on this, never on the raw config, which is why no
// hand-edited number can make the mover misbehave - only behave like its nearest legal value.
// =================================================================================================

struct ResolvedConfig final {
    Math::Vector3 upDirection{0.0F, 1.0F, 0.0F};
    float floorMaxAngleDegrees = 45.0F;
    float wallMinSlideAngleDegrees = 15.0F;
    float safeMargin = 0.001F;
    std::size_t maxSlides = 8U;
    std::size_t maximumContacts = 64U;
    float floorSnapLength = 0.1F;
    float maxStepHeight = 0.3F;
    float minStepWidth = 0.02F;
    bool slideOnCeiling = true;
    bool floorBlockOnWall = false;
    bool floorStopOnSlope = true;
    bool floorConstantSpeed = false;
    bool detectFloor = true;
    CharacterPlatformLeaveMode platformOnLeave = CharacterPlatformLeaveMode::AddVelocity;
    float maximumPlatformSpeed = 50.0F;
    bool jumpedThisStep = false;
    bool clamped = false;
};

[[nodiscard]] float ClampFinite(const float value, const float minimum, const float maximum,
                                   const float fallback, bool& clamped) noexcept {
    if (!std::isfinite(value)) {
        clamped = true;
        return fallback;
    }
    if (value < minimum) {
        clamped = true;
        return minimum;
    }
    if (value > maximum) {
        clamped = true;
        return maximum;
    }
    return value;
}

[[nodiscard]] bool TryResolveConfig(const CharacterMotionConfig& config,
                                       ResolvedConfig& resolved) noexcept {
    const std::optional<Math::Vector3> up = TryNormalize(config.upDirection);
    if (!up.has_value()) {
        return false;
    }
    resolved.upDirection = *up;

    resolved.floorMaxAngleDegrees =
        ClampFinite(config.floorMaxAngleDegrees, 0.0F, 90.0F, 45.0F, resolved.clamped);
    resolved.wallMinSlideAngleDegrees =
        ClampFinite(config.wallMinSlideAngleDegrees, 0.0F, 90.0F, 15.0F, resolved.clamped);
    resolved.safeMargin = ClampFinite(config.safeMargin, 0.0F, 0.25F, 0.001F, resolved.clamped);
    resolved.floorSnapLength = ClampFinite(config.floorSnapLength, 0.0F, 10.0F, 0.1F, resolved.clamped);
    resolved.maxStepHeight = ClampFinite(config.maxStepHeight, 0.0F, 10.0F, 0.3F, resolved.clamped);
    resolved.minStepWidth = ClampFinite(config.minStepWidth, 0.0F, 1.0F, 0.02F, resolved.clamped);
    resolved.maximumPlatformSpeed =
        ClampFinite(config.maximumPlatformSpeed, 0.0F, 10000.0F, 50.0F, resolved.clamped);

    std::size_t maxSlides = config.maxSlides;
    if (maxSlides < 1U) {
        maxSlides = 1U;
        resolved.clamped = true;
    }
    if (maxSlides > Character3D::kMaximumSlides) {
        maxSlides = Character3D::kMaximumSlides;
        resolved.clamped = true;
    }
    resolved.maxSlides = maxSlides;

    std::size_t maximumContacts = config.maximumContacts;
    if (maximumContacts < 1U) {
        maximumContacts = 1U;
        resolved.clamped = true;
    }
    if (maximumContacts > Character3D::kMaximumContacts) {
        maximumContacts = Character3D::kMaximumContacts;
        resolved.clamped = true;
    }
    resolved.maximumContacts = maximumContacts;

    resolved.slideOnCeiling = config.slideOnCeiling;
    resolved.floorBlockOnWall = config.floorBlockOnWall;
    resolved.floorStopOnSlope = config.floorStopOnSlope;
    resolved.floorConstantSpeed = config.floorConstantSpeed;
    resolved.detectFloor = config.detectFloor;
    resolved.jumpedThisStep = config.jumpedThisStep;
    if (config.platformOnLeave > CharacterPlatformLeaveMode::AddUpwardVelocity) {
        resolved.platformOnLeave = CharacterPlatformLeaveMode::KeepVelocity;
        resolved.clamped = true;
    } else {
        resolved.platformOnLeave = config.platformOnLeave;
    }
    return true;
}

// =================================================================================================
// Surface classification.
// =================================================================================================

[[nodiscard]] CharacterSurfaceKind ClassifyNormal(const Math::Vector3& unitUp,
                                                        const ResolvedConfig& config,
                                                        const Math::Vector3& surfaceNormal,
                                                        float& outAngleDegrees) noexcept {
    const std::optional<Math::Vector3> normal = TryNormalize(surfaceNormal);
    if (!normal.has_value()) {
        return CharacterSurfaceKind::Invalid;
    }
    const float alignment = std::clamp(Math::Dot(*normal, unitUp), -1.0F, 1.0F);
    const float angleDegrees = Math::RadToDeg(std::acos(alignment));
    outAngleDegrees = angleDegrees;
    if (angleDegrees <= config.floorMaxAngleDegrees) {
        return config.detectFloor ? CharacterSurfaceKind::Floor : CharacterSurfaceKind::Wall;
    }
    if (angleDegrees >= 180.0F - config.floorMaxAngleDegrees) {
        return CharacterSurfaceKind::Ceiling;
    }
    return CharacterSurfaceKind::Wall;
}

/// A wall is slid along only when it is steep enough to be a wall rather than a steep ramp. Inside
/// the band between Floor Max Angle and Wall Min Slide Angle past it, the body stops: that band is
/// the difference between "you may climb this" and "you may not creep up this", and it is the only
/// reading of Wall Min Slide Angle that gives the setting a mechanical meaning.
[[nodiscard]] bool WallSlides(const ResolvedConfig& config, const float angleDegrees) noexcept {
    return angleDegrees >= config.floorMaxAngleDegrees + config.wallMinSlideAngleDegrees;
}

// =================================================================================================
// The move context: everything one step of MoveAndSlide touches, in one place, so the helpers
// below read as sentences about the body instead of parameter lists.
// =================================================================================================

struct MoveContext final {
    const ICharacterWorldQuery* world = nullptr;
    ResolvedConfig config{};
    CharacterMotionState moved{};
    CharacterMotionResult result{};
    Math::Vector3 center{};
    Math::Vector3 remaining{};
    Entity floorEntity = kInvalidEntity;
};

[[nodiscard]] float HorizontalSpeed(const MoveContext& context,
                                       const Math::Vector3& velocity) noexcept {
    return VectorLength(Perpendicular(velocity, context.config.upDirection));
}

/// Records a contact unless the caller's cap is already spent, in which case it counts the drop so
/// the caller can tell "nothing hit" from "I stopped listening".
void RecordCollision(MoveContext& context, const CharacterSlideCollision& collision) {
    // An entity is one surface as far as a report is concerned: a body sliding along a wall touches
    // that wall once, not once per slide. Re-touching an entity already in the report updates what
    // is known about it - the closest approach, and the fact that a step-up happened on it - rather
    // than spending another contact slot and another place for a caller to have to look at.
    for (CharacterSlideCollision& recorded : context.result.collisions) {
        if (recorded.entity != collision.entity) {
            continue;
        }
        if (collision.travel < recorded.travel) {
            recorded.position = collision.position;
            recorded.travel = collision.travel;
        }
        recorded.depth = std::max(recorded.depth, collision.depth);
        recorded.steppedUp = recorded.steppedUp || collision.steppedUp;
        if (collision.normal != Math::Vector3{}) {
            recorded.normal = collision.normal;
        }
        return;
    }
    if (context.result.collisions.size() >= context.config.maximumContacts) {
        ++context.result.droppedContactCount;
        context.result.contactsTruncated = true;
        return;
    }
    context.result.collisions.push_back(collision);
    ++context.result.contactCount;
}

/// Applies a displacement to the body being moved: one place that keeps `center`, `appliedMotion`
/// and the per-source breakdown consistent, so no helper can move the body without the result
/// accounting for it.
void ApplyMotion(MoveContext& context, const Math::Vector3& displacement,
                    Math::Vector3& accumulator) {
    context.center += displacement;
    context.result.appliedMotion += displacement;
    accumulator += displacement;
}

/// Registers what a contact means for the body's surface state. Last contact wins, which is what a
/// sliding body wants: the floor it ends a step standing on is the floor it reports, not the first
/// one it brushed on the way there.
void RegisterSurface(MoveContext& context, const Math::Vector3& normal,
                        const Entity entity) {
    float angleDegrees = 0.0F;
    switch (ClassifyNormal(context.config.upDirection, context.config, normal, angleDegrees)) {
        case CharacterSurfaceKind::Floor:
            context.result.onFloor = true;
            context.result.floorNormal = normal;
            context.result.floorAngleDegrees = angleDegrees;
            context.floorEntity = entity;
            break;
        case CharacterSurfaceKind::Wall:
            context.result.onWall = true;
            context.result.wallNormal = normal;
            break;
        case CharacterSurfaceKind::Ceiling:
            context.result.onCeiling = true;
            context.result.ceilingNormal = normal;
            break;
        case CharacterSurfaceKind::Invalid:
        default:
            break;
    }
}

/// Leaves a contact where the caller can read it, filling in the surface's own velocity so a
/// script standing on a moving platform sees how fast the thing under its feet is going.
void RecordSurfaceContact(MoveContext& context, const CharacterSlideCollision& hit,
                             const Math::Vector3& motion, const Math::Vector3& remainder,
                             const float travel, const float depth, const bool steppedUp) {
    CharacterSlideCollision collision;
    collision.entity = hit.entity;
    collision.normal = hit.normal;
    collision.position = hit.position;
    collision.colliderVelocity = context.world->GetSurfaceVelocity(hit.entity);
    collision.motion = motion;
    collision.remainder = remainder;
    collision.travel = travel;
    collision.depth = depth;
    collision.steppedUp = steppedUp;
    RegisterSurface(context, hit.normal, hit.entity);
    RecordCollision(context, collision);
}

// =================================================================================================
// Depenetration. A body that starts a step inside geometry is pushed out before it is moved,
// because a sweep that begins overlapping has no meaningful answer: "when do I hit this" is not a
// question about a body already past it.
// =================================================================================================

void Depenetrate(MoveContext& context) {
    if (context.config.safeMargin <= 0.0F) {
        // Without a skin there is nothing to push out to, and pushing to exactly zero would make
        // the next query degenerate again - so a zero margin means "no depenetration", not
        // "depenetrate until touching".
        return;
    }

    // Each pass asks the world about the working center, pushes the deepest overlap there, and
    // asks again. Ghost contacts from the pose we just left cannot keep pushing: that is what
    // GetOverlapsAt is for, and why a snapshot of the start pose is not enough.
    float remainingBudget = Character3D::kMaximumSingleDepenetrationDistance;
    for (std::size_t pass = 0U; pass < kMaximumDepenetrationPasses; ++pass) {
        if (remainingBudget <= Character3D::kMinimumMotionDistance) {
            break;
        }
        const std::vector<CharacterSlideCollision> overlaps =
            context.world->GetOverlapsAt(context.center);
        if (overlaps.empty()) {
            break;
        }

        std::vector<std::size_t> order;
        order.reserve(overlaps.size());
        for (std::size_t index = 0U; index < overlaps.size(); ++index) {
            order.push_back(index);
        }
        std::stable_sort(order.begin(), order.end(),
                         [&overlaps](const std::size_t lhs, const std::size_t rhs) {
                             if (overlaps[lhs].depth != overlaps[rhs].depth) {
                                 return overlaps[lhs].depth > overlaps[rhs].depth;
                             }
                             return overlaps[lhs].entity.index < overlaps[rhs].entity.index;
                         });

        bool pushed = false;
        for (const std::size_t index : order) {
            const CharacterSlideCollision& overlap = overlaps[index];
            const std::optional<Math::Vector3> direction = TryNormalize(overlap.normal);
            if (!direction.has_value() || !std::isfinite(overlap.depth) || overlap.depth <= 0.0F) {
                continue;
            }
            const Math::Vector3 remainder = context.result.depenetrationMotion;
            if (!pushed) {
                const float wanted = overlap.depth + context.config.safeMargin;
                if (!std::isfinite(wanted) || wanted <= 0.0F) {
                    continue;
                }
                const float pushDistance = std::min(wanted, remainingBudget);
                if (!std::isfinite(pushDistance) || pushDistance <= 0.0F) {
                    continue;
                }
                const Math::Vector3 push = *direction * pushDistance;
                ApplyMotion(context, push, context.result.depenetrationMotion);
                remainingBudget -= pushDistance;
                ++context.result.depenetrationPasses;
                context.result.depenetrated = true;
                RecordSurfaceContact(context, overlap, push, remainder, 0.0F, overlap.depth,
                                        false);
                pushed = true;
            } else {
                RecordSurfaceContact(context, overlap, Math::Vector3{}, remainder, 0.0F,
                                        overlap.depth, false);
            }
        }
        if (!pushed) {
            break;
        }
    }
}

// =================================================================================================
// Stepping up. The whole maneuver is three sweeps - lift clear, walk forward, drop onto what is
// there - and it is failure-atomic: any sweep that does not find what it must find leaves the
// center untouched, so a refused step costs a query and never a teleport.
// =================================================================================================

struct StepResult final {
    Math::Vector3 motion{};
    Math::Vector3 center{};
    CharacterSlideCollision landing{};
    /// Horizontal distance of the walk the step did not carry the body over - zero when it carried
    /// all of it, which is the usual case for a step squarely in the body's path.
    float leftoverHorizontal = 0.0F;
};

[[nodiscard]] std::optional<StepResult> TryStepUp(
    const MoveContext& context, const Math::Vector3& horizontalMotion,
    const CharacterSlideCollision& wallHit) {
    const ResolvedConfig& config = context.config;
    const Math::Vector3 up = config.upDirection;
    const float stepHeight = config.maxStepHeight;
    const float horizontalLength = VectorLength(horizontalMotion);
    if (stepHeight <= Character3D::kMinimumMotionDistance ||
        horizontalLength <= Character3D::kMinimumMotionDistance) {
        return std::nullopt;
    }
    const Math::Vector3 horizontalDirection = horizontalMotion * (1.0F / horizontalLength);

    // 1. Is there room to stand the body up by a step? A sweep that stops short means something is
    // in the way of the lift itself - a low ceiling, a shelf - and the step is refused.
    //
    // The face the body is pressed against is not in the way of its own lift: a body standing
    // against a low step is already touching it, and a query answered about the whole body would
    // report that touch as a hit the moment the lift starts. Rising along a surface is not the
    // surface blocking the rise, which is the same distinction the skin makes everywhere else.
    // Anything that is genuinely overhead - a different surface, or this one's ledge - still stops
    // the lift, and a step too tall to clear is refused by the forward probe below.
    const std::optional<CharacterSlideCollision> liftHit = context.world->Sweep(
        context.center, up * (stepHeight + config.safeMargin), config.safeMargin);
    if (liftHit.has_value()) {
        if (liftHit->entity != wallHit.entity) {
            return std::nullopt;
        }
        // Same body: the riser we are climbing is not in the way of its own lift. Its underside
        // is - a doorway lintel on the same mesh, a shelf that is this wall's own top - and a
        // ceiling-class hit is that underside. Honouring it is what stops a step from standing
        // the body up through a roof that happens to share an entity with the stair.
        float liftAngleDegrees = 0.0F;
        if (ClassifyNormal(up, config, liftHit->normal, liftAngleDegrees) ==
            CharacterSurfaceKind::Ceiling) {
            return std::nullopt;
        }
    }
    const Math::Vector3 liftedCenter = context.center + up * stepHeight;

    // 2. Walk forward at the lifted height. The probe reaches past the body's own motion on purpose:
    // whether a surface ahead is a step worth climbing is a question about the *geometry* - is there
    // a tread at least Min Step Width wide - not about how much of this step's walk is left. A body
    // walking straight into a stair spends its whole walk against the riser, and a step judged by
    // what is left of that walk would refuse every stair it exists to climb.
    const float probeDistance =
        std::max(horizontalLength + config.safeMargin, config.minStepWidth + config.safeMargin);
    const std::optional<CharacterSlideCollision> forwardHit = context.world->Sweep(
        liftedCenter, horizontalDirection * probeDistance, config.safeMargin);
    const float clearRun = forwardHit.has_value()
                               ? std::clamp(forwardHit->travel, 0.0F, probeDistance)
                               : probeDistance;
    if (clearRun + Character3D::kMinimumMotionDistance < config.minStepWidth) {
        // Something immediately in front of the lifted body: a wall to stop against, or a ridge too
        // narrow to stand on - not a step.
        return std::nullopt;
    }
    // How far the step actually carries the body: no further than it was walking, and no further
    // than the tread is clear.
    const float reach = std::min(horizontalLength, clearRun);
    const Math::Vector3 forwardCenter = liftedCenter + horizontalDirection * reach;

    // 3. Drop onto whatever is below the forward position. Nothing below within a step means a ledge
    // rather than a step, and walking off it is the walk's job, not the step's.
    const float dropLength = stepHeight + config.safeMargin * kFloorProbeLiftMarginFactor;
    const std::optional<CharacterSlideCollision> dropHit =
        context.world->Sweep(forwardCenter, -up * dropLength, config.safeMargin);
    if (!dropHit.has_value()) {
        return std::nullopt;
    }
    float dropAngleDegrees = 0.0F;
    if (ClassifyNormal(up, config, dropHit->normal, dropAngleDegrees) !=
        CharacterSurfaceKind::Floor) {
        return std::nullopt;
    }
    const float dropDistance = std::clamp(dropHit->travel, 0.0F, dropLength);

    StepResult step;
    step.center = forwardCenter - up * dropDistance;
    step.motion = step.center - context.center;
    step.landing = *dropHit;
    step.leftoverHorizontal = std::max(0.0F, horizontalLength - reach);
    return step;
}

// =================================================================================================
// Floor snapping. A body that was standing should keep standing when the floor steps down beneath
// it: without this, walking off a kerb throws the character into the air and the next step lands
// it, which reads as a stutter. The probe is what makes "was standing, still standing" a fact
// rather than an assumption.
// =================================================================================================

[[nodiscard]] float FloorProbeLiftFromMargin(const float safeMargin) noexcept {
    const float margin = std::max(0.0F, FiniteOrZero(safeMargin));
    return margin * kFloorProbeLiftMarginFactor + Character3D::kMinimumMotionDistance;
}

[[nodiscard]] float FloorProbeLift(const ResolvedConfig& config) noexcept {
    return FloorProbeLiftFromMargin(config.safeMargin);
}

void SnapToFloor(MoveContext& context) {
    const ResolvedConfig& config = context.config;
    if (context.result.onFloor || !config.detectFloor || !context.moved.grounded) {
        return;
    }
    if (config.jumpedThisStep || AlongUnit(context.moved.velocity, config.upDirection) > 0.0F) {
        // A jump is a decision to leave the floor, and a body on its way up is already gone.
        return;
    }

    const float probeDistance = std::max(config.floorSnapLength, Character3D::kMinimumFloorProbe);
    const std::optional<CharacterSlideCollision> hit = Character3D::ProbeFloor(
        *context.world, context.center, config.upDirection, config.floorMaxAngleDegrees,
        probeDistance, config.safeMargin);
    if (!hit.has_value()) {
        return;
    }
    context.result.onFloor = true;
    context.result.floorNormal = hit->normal;
    context.result.floorAngleDegrees =
        Character3D::SlopeAngleDegrees(hit->normal, config.upDirection);
    context.floorEntity = hit->entity;

    const float lift = FloorProbeLift(config);
    const float gap = std::max(0.0F, hit->travel - lift);
    Math::Vector3 snap{};
    float snapDistance = 0.0F;
    if (gap > Character3D::kMinimumMotionDistance) {
        snapDistance = std::min(gap, probeDistance);
        snap = -config.upDirection * snapDistance;
        ApplyMotion(context, snap, context.result.snapMotion);
        context.result.snapUsed = true;
    }
    // The probe answered "this is the floor" whether or not there was a gap to close. A script
    // reading collisions after a follow-down has to see that floor, not an empty list that looks
    // like "nothing underfoot".
    RecordSurfaceContact(context, *hit, snap, context.remaining, snapDistance, 0.0F, false);
}

/// The floor probe itself, shared by the snap above and by anything that just wants to ask "what is
/// under me" - an AI deciding whether a ledge is walkable, a placement tool drawing a footprint.
[[nodiscard]] std::optional<CharacterSlideCollision> ProbeFloorAt(
    const ICharacterWorldQuery& world, const Math::Vector3& center,
    const Math::Vector3& upDirection, const float floorMaxAngleDegrees, const float probeDistance,
    const float safeMargin) {
    const std::optional<Math::Vector3> up = TryNormalize(upDirection);
    if (!up.has_value() || !IsUsableVector(center) || !std::isfinite(probeDistance) ||
        !std::isfinite(safeMargin) || probeDistance < 0.0F) {
        return std::nullopt;
    }
    ResolvedConfig probeConfig;
    probeConfig.upDirection = *up;
    probeConfig.floorMaxAngleDegrees = std::clamp(FiniteOrZero(floorMaxAngleDegrees), 0.0F, 90.0F);

    const float lift = FloorProbeLiftFromMargin(safeMargin);
    const float sweepLength = probeDistance + lift;
    if (sweepLength <= Character3D::kMinimumMotionDistance) {
        return std::nullopt;
    }
    const std::optional<CharacterSlideCollision> hit =
        world.Sweep(center + *up * lift, -*up * sweepLength, std::max(safeMargin, 0.0F));
    if (!hit.has_value()) {
        return std::nullopt;
    }
    float angleDegrees = 0.0F;
    if (ClassifyNormal(*up, probeConfig, hit->normal, angleDegrees) !=
        CharacterSurfaceKind::Floor) {
        // Something is below the body, but it is not a floor - a wall's shoulder, the ceiling of a
        // crawlspace. Snapping to it would stand the body on a surface it cannot stand on.
        return std::nullopt;
    }
    return hit;
}

// =================================================================================================
// Riding platforms. A platform is not a velocity somebody published - it is a body whose movement
// carries its rider, so the velocity that matters is measured from where the platform was at the
// end of the last step to where it is now. That works for kinematic bodies, for scripted movers
// and for animation-driven ones alike, and it cannot be wrong the way a hand-set number can.
// =================================================================================================

void MeasurePlatformVelocity(MoveContext& context, const float deltaTimeSeconds) {
    context.moved.platformVelocity = Math::Vector3{};
    if (context.moved.platform == kInvalidEntity || !context.moved.grounded) {
        return;
    }
    const std::optional<Math::Vector3> platformCenter =
        context.world->TryGetEntityCenter(context.moved.platform);
    if (!platformCenter.has_value()) {
        // Destroyed, or no longer a body: there is nothing to ride and nothing to be left by.
        context.moved.platform = kInvalidEntity;
        context.moved.platformWorldPosition = Math::Vector3{};
        context.moved.hasPlatformWorldPosition = false;
        return;
    }
    if (context.moved.hasPlatformWorldPosition &&
        IsUsableVector(context.moved.platformWorldPosition)) {
        const Math::Vector3 measured =
            (*platformCenter - context.moved.platformWorldPosition) * (1.0F / deltaTimeSeconds);
        if (IsUsableVector(measured)) {
            context.moved.platformVelocity = measured;
        }
    }
    // Remembered even when the measurement was skipped, so the next step measures from here - and
    // a platform that has only just been stepped onto contributes nothing until then, because
    // there is no "where it was" to measure from.
    context.moved.platformWorldPosition = *platformCenter;
    context.moved.hasPlatformWorldPosition = true;
}

/// The platform's motion, or nothing when it is not something the body should be carried by: a
/// platform that teleported (a script writing a transform) must not fling its rider across the
/// level, and a stationary one has nothing to add.
[[nodiscard]] Math::Vector3 PlatformCarryVelocity(const MoveContext& context) {
    if (!context.moved.grounded || context.moved.platformVelocity == Math::Vector3{}) {
        return Math::Vector3{};
    }
    const float speed = VectorLength(context.moved.platformVelocity);
    if (!std::isfinite(speed) || speed > context.config.maximumPlatformSpeed) {
        return Math::Vector3{};
    }
    return context.moved.platformVelocity;
}

/// What the platform does to the body's velocity when the body leaves it. The direction of the
/// goodwill is one-way on purpose: a rising platform may throw the body up, a falling one never
/// throws it down, because "the lift went down and took my jump with it" is exactly the bug this
/// setting exists to avoid.
void ApplyPlatformLeave(MoveContext& context, const Math::Vector3& platformVelocity) {
    if (context.config.platformOnLeave == CharacterPlatformLeaveMode::KeepVelocity ||
        platformVelocity == Math::Vector3{}) {
        return;
    }
    if (context.config.platformOnLeave == CharacterPlatformLeaveMode::AddVelocity) {
        context.moved.velocity += platformVelocity;
        return;
    }
    const float upwardSpeed = Math::Dot(platformVelocity, context.config.upDirection);
    if (std::isfinite(upwardSpeed) && upwardSpeed > 0.0F) {
        context.moved.velocity += context.config.upDirection * upwardSpeed;
    }
}

// =================================================================================================
// The slide loop: move, hit, classify, respond, repeat. Every iteration either spends motion or
// stops, so the cap is a bound on work rather than a hope.
// =================================================================================================

void Slide(MoveContext& context) {
    const ResolvedConfig& config = context.config;
    for (std::size_t slide = 0U; slide < config.maxSlides; ++slide) {
        const float remainingLength = VectorLength(context.remaining);
        if (remainingLength <= Character3D::kMinimumMotionDistance) {
            context.remaining = Math::Vector3{};
            return;
        }
        const Math::Vector3 sweptMotion = context.remaining;
        const Math::Vector3 direction = sweptMotion * (1.0F / remainingLength);
        const std::optional<CharacterSlideCollision> hit =
            context.world->Sweep(context.center, sweptMotion, config.safeMargin);
        if (!hit.has_value()) {
            // Nothing in the way for the rest of the move: spend all of it and stop.
            ApplyMotion(context, context.remaining, context.result.slideMotion);
            context.remaining = Math::Vector3{};
            return;
        }

        const float travel = std::clamp(hit->travel, 0.0F, remainingLength);
        ApplyMotion(context, direction * travel, context.result.slideMotion);
        context.remaining -= direction * travel;
        ++context.result.slideCount;

        float angleDegrees = 0.0F;
        const CharacterSurfaceKind kind =
            ClassifyNormal(config.upDirection, config, hit->normal, angleDegrees);

        // A step is a walking convenience, not a second jump: only a body that was already standing,
        // was not jumping, and is not already moving up climbs one.
        //
        // What the step is asked to carry is what is left of this step's walk after the contact;
        // whether the surface ahead is a step at all is decided by TryStepUp from the geometry,
        // not from how much of the walk survived the riser.
        const Math::Vector3 stepReach = Perpendicular(context.remaining, config.upDirection);
        const bool canStep = kind == CharacterSurfaceKind::Wall &&
                             config.maxStepHeight > Character3D::kMinimumMotionDistance &&
                             context.moved.grounded && !config.jumpedThisStep &&
                             AlongUnit(context.moved.velocity, config.upDirection) <= 0.0F;
        if (canStep) {
            if (const std::optional<StepResult> step = TryStepUp(context, stepReach, *hit);
                step.has_value()) {
                context.center = step->center;
                context.result.appliedMotion += step->motion;
                context.result.stepMotion += step->motion;
                context.result.stepUpUsed = true;
                RecordSurfaceContact(context, step->landing, sweptMotion,
                                        Perpendicular(step->motion, config.upDirection),
                                        VectorLength(step->motion), 0.0F, true);
                // Whatever the step did not carry the body over is still the body's to walk, and
                // walking it from the tread is the next iteration's job - so a body that steps onto
                // the edge of a platform with motion left keeps moving, and one that steps its whole
                // distance stops on the tread instead of grinding into it.
                const float leftover = step->leftoverHorizontal;
                if (leftover <= Character3D::kMinimumMotionDistance) {
                    context.remaining = Math::Vector3{};
                    return;
                }
                const float reachLength = VectorLength(stepReach);
                if (reachLength > Character3D::kMinimumMotionDistance) {
                    context.remaining = stepReach * (leftover / reachLength);
                } else {
                    context.remaining = Math::Vector3{};
                }
                continue;
            }
        }

        RecordSurfaceContact(context, *hit, sweptMotion, context.remaining, travel, hit->depth,
                                false);

        if (kind == CharacterSurfaceKind::Ceiling && !config.slideOnCeiling) {
            // Slide On Ceiling off means the ceiling is a wall in the only way that matters: the
            // move ends against it, horizontal motion included.
            context.remaining = Math::Vector3{};
            context.result.blocked = true;
            return;
        }
        if (kind == CharacterSurfaceKind::Wall) {
            if (config.floorBlockOnWall && context.moved.grounded) {
                context.remaining = Math::Vector3{};
                context.result.blocked = true;
                return;
            }
            if (!WallSlides(config, angleDegrees)) {
                // The steep-ramp band: the body stops against it instead of creeping up it.
                context.remaining = Math::Vector3{};
                context.result.blocked = true;
                return;
            }
        }

        const float lengthBeforeProjection = VectorLength(context.remaining);
        Math::Vector3 projected =
            Character3D::SlideMotion(context.remaining, hit->normal);

        if (config.floorConstantSpeed && kind == CharacterSurfaceKind::Floor) {
            // Floor Constant Speed: this contact's remaining horizontal is restored after the
            // slope projects it, so a walk up a slope keeps the metres of the walk not yet spent
            // instead of slowing down, and a walk down one does not speed up. Restoring against
            // the original full-step horizontal would invent metres already travelled getting
            // here; restoring against this remaining is what keeps the whole step's horizontal
            // equal to the walk.
            const float horizontalBefore =
                VectorLength(Perpendicular(context.remaining, config.upDirection));
            const float horizontalAfter =
                VectorLength(Perpendicular(projected, config.upDirection));
            if (horizontalBefore > Character3D::kMinimumMotionDistance &&
                horizontalAfter > Character3D::kMinimumMotionDistance &&
                horizontalAfter < horizontalBefore) {
                const Math::Vector3 horizontalPart = Perpendicular(projected, config.upDirection);
                const Math::Vector3 alongPart = projected - horizontalPart;
                projected = alongPart + horizontalPart * (horizontalBefore / horizontalAfter);
            }
        }

        if (VectorLength(projected) < lengthBeforeProjection - Character3D::kMinimumMotionDistance) {
            context.result.blocked = true;
        }
        context.remaining = projected;
    }

    if (VectorLength(context.remaining) > Character3D::kMinimumMotionDistance) {
        // Out of slides with motion left over. The body stops here rather than creeping the rest of
        // the way on the next step, and the caller can see it in `blocked`.
        context.remaining = Math::Vector3{};
        context.result.blocked = true;
    }
}

// =================================================================================================
// What the surfaces under a body do to its velocity once the moving is over. These are the rules a
// player feels as "the character does not creep down ramps" and "a lift throws me up when I jump
// off it", and they are the mover's only writes to velocity - everything else is the intent step's.
// =================================================================================================

void ApplySurfaceVelocityRules(MoveContext& context) {
    const ResolvedConfig& config = context.config;
    if (!context.result.onFloor) {
        return;
    }
    const float standingSpeed = HorizontalSpeed(context, context.moved.velocity);
    if (!config.floorStopOnSlope || context.result.floorAngleDegrees <= 0.01F ||
        standingSpeed > Character3D::kStandingSpeed ||
        AlongUnit(context.moved.velocity, config.upDirection) >
            Character3D::kMinimumMotionDistance) {
        return;
    }
    // A body standing on a slope stays standing there. Its gravity is re-applied every step by the
    // intent step, and every step the mover takes the downhill part of it back out - which is what
    // "not sliding down a hill" means for a character that was not walking.
    context.moved.velocity = Math::Vector3{};
}

// =================================================================================================
// Platform bookkeeping at the end of a step: who is underfoot now, and what happens to a body that
// was left behind by something that used to be.
// =================================================================================================

void FinishPlatform(MoveContext& context, const Entity previousPlatform,
                       const Math::Vector3& previousPlatformVelocity, const bool wasOnFloor) {
    if (context.result.onFloor && context.floorEntity != kInvalidEntity) {
        // Standing on something: that thing is the platform, and the next step measures how far it
        // moved from where it is now.
        context.moved.platform = context.floorEntity;
        const std::optional<Math::Vector3> center =
            context.world->TryGetEntityCenter(context.floorEntity);
        context.moved.platformWorldPosition = center.has_value() ? *center : Math::Vector3{};
        context.moved.hasPlatformWorldPosition = center.has_value();
        context.result.platformVelocity = context.moved.platformVelocity;
        return;
    }

    if (wasOnFloor && previousPlatform != kInvalidEntity) {
        // Just left the platform behind. What it was doing when the body left is what it hands
        // over - that is the whole of platform_on_leave - and a platform that was teleported hands
        // over nothing, for the same reason it never carried the body in the first place: a script
        // writing a transform is not motion, and inheriting it would fling the jump across a level.
        const float handedOverSpeed = VectorLength(previousPlatformVelocity);
        const Math::Vector3 handedOverVelocity =
            std::isfinite(handedOverSpeed) && handedOverSpeed <= context.config.maximumPlatformSpeed
                ? previousPlatformVelocity
                : Math::Vector3{};
        ApplyPlatformLeave(context, handedOverVelocity);
        context.result.platformVelocity = handedOverVelocity;
        context.result.leftPlatform = true;
    }
    context.moved.platform = kInvalidEntity;
    context.moved.platformWorldPosition = Math::Vector3{};
    context.moved.platformVelocity = Math::Vector3{};
    context.moved.hasPlatformWorldPosition = false;
}

// =================================================================================================
// Writing the step's outcome back into the state. Kept as one function so the state can never be
// half-updated - a caller reading `grounded` and `floorNormal` sees two fields of the same answer.
// =================================================================================================

void CommitState(MoveContext& context, CharacterMotionState& state,
                    const float deltaTimeSeconds) {
    if (context.result.code != CharacterMoveCode::Moved) {
        return;
    }
    context.moved.grounded = context.result.onFloor;
    context.moved.onWall = context.result.onWall;
    context.moved.onCeiling = context.result.onCeiling;
    context.moved.floorNormal = context.result.onFloor ? context.result.floorNormal
                                                       : context.config.upDirection;
    context.moved.wallNormal = context.result.onWall ? context.result.wallNormal : Math::Vector3{};
    context.moved.ceilingNormal = context.result.onCeiling ? context.result.ceilingNormal
                                                           : Math::Vector3{};
    context.moved.lastMotion = context.result.appliedMotion;
    if (deltaTimeSeconds > 0.0F) {
        context.moved.realVelocity = context.result.appliedMotion * (1.0F / deltaTimeSeconds);
    }
    state = context.moved;
}

} // namespace

// =================================================================================================
// Public surface.
// =================================================================================================

CharacterSurfaceKind ClassifyCharacterSurface(const Math::Vector3& surfaceNormal,
                                                   const Math::Vector3& upDirection,
                                                   const float floorMaxAngleDegrees,
                                                   float& outAngleDegrees) noexcept {
    // A caller that cannot be answered is still answered about the angle: "no surface here" reads as
    // zero degrees rather than as whatever the caller last passed in.
    outAngleDegrees = 0.0F;
    const std::optional<Math::Vector3> up = TryNormalize(upDirection);
    if (!up.has_value()) {
        return CharacterSurfaceKind::Invalid;
    }
    ResolvedConfig config;
    config.upDirection = *up;
    config.floorMaxAngleDegrees = std::clamp(FiniteOrZero(floorMaxAngleDegrees), 0.0F, 90.0F);
    return ClassifyNormal(*up, config, surfaceNormal, outAngleDegrees);
}

float Character3D::SlopeAngleDegrees(const Math::Vector3& normal,
                                           const Math::Vector3& upDirection) noexcept {
    const std::optional<Math::Vector3> up = TryNormalize(upDirection);
    const std::optional<Math::Vector3> unitNormal = TryNormalize(normal);
    if (!up.has_value() || !unitNormal.has_value()) {
        return 0.0F;
    }
    const float alignment = std::clamp(Math::Dot(*unitNormal, *up), -1.0F, 1.0F);
    return Math::RadToDeg(std::acos(alignment));
}

Math::Vector3 Character3D::SlideMotion(const Math::Vector3& motion,
                                                const Math::Vector3& normal) noexcept {
    if (!IsUsableVector(motion) || !IsUsableVector(normal)) {
        return motion;
    }
    const float normalLengthSquared = Math::LengthSquared(normal);
    if (!std::isfinite(normalLengthSquared) || normalLengthSquared <= 0.0F) {
        return motion;
    }
    const float intoSurface = Math::Dot(motion, normal);
    if (!std::isfinite(intoSurface) || intoSurface >= 0.0F) {
        // Already leaving, or running along: there is nothing to take out.
        return motion;
    }
    return motion - normal * (intoSurface / normalLengthSquared);
}

std::optional<CharacterSlideCollision> Character3D::TestMove(
    const ICharacterWorldQuery& world, const Math::Vector3& motion, const float margin) {
    if (!IsUsableVector(motion) || !std::isfinite(margin) || margin < 0.0F) {
        return std::nullopt;
    }
    return world.Sweep(world.GetCenter(), motion, std::min(margin, 0.25F));
}

std::optional<CharacterSlideCollision> Character3D::ProbeFloor(
    const ICharacterWorldQuery& world, const Math::Vector3& center,
    const Math::Vector3& upDirection, const float floorMaxAngleDegrees, const float probeDistance,
    const float safeMargin) noexcept {
    return ProbeFloorAt(world, center, upDirection, floorMaxAngleDegrees, probeDistance, safeMargin);
}

CharacterMotionResult Character3D::MoveAndSlide(const ICharacterWorldQuery& world,
                                                        const CharacterMotionConfig& config,
                                                        CharacterMotionState& state,
                                                        const Math::Vector3& desiredMotion,
                                                        const float deltaTimeSeconds) {
    MoveContext context;
    context.world = &world;
    context.result.requestedMotion = desiredMotion;
    context.result.code = CharacterMoveCode::InvalidConfig;

    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F ||
        !IsUsableVector(desiredMotion)) {
        return context.result;
    }
    if (!TryResolveConfig(config, context.config)) {
        return context.result;
    }
    if (!IsFiniteState(state)) {
        context.result.code = CharacterMoveCode::InvalidState;
        return context.result;
    }
    context.moved = state;
    context.result.configClamped = context.config.clamped;

    const Math::Vector3 center = world.GetCenter();
    if (!IsUsableVector(center)) {
        context.result.code = CharacterMoveCode::InvalidWorld;
        return context.result;
    }
    context.center = center;
    context.result.code = CharacterMoveCode::Moved;

    const bool wasOnFloor = state.grounded;
    const Entity previousPlatform = state.platform;

    // ---- What the platform underfoot did since the last step, and what that means for this one ---
    MeasurePlatformVelocity(context, deltaTimeSeconds);
    const Math::Vector3 platformVelocity = context.moved.platformVelocity;
    const Math::Vector3 carry = PlatformCarryVelocity(context) * deltaTimeSeconds;
    context.result.platformVelocity = platformVelocity;
    if (carry != Math::Vector3{}) {
        context.result.carriedByPlatform = true;
    }

    const Math::Vector3 motion = desiredMotion + carry;

    // ---- Push out of anything the body started inside of, then walk it ---------------------------
    Depenetrate(context);
    context.remaining = motion;
    Slide(context);
    SnapToFloor(context);
    // Numerical overlap after a slide or a snap is the same question as the one at the start of
    // the step, asked of the pose we actually ended in - not of the pose we started in.
    Depenetrate(context);
    ApplySurfaceVelocityRules(context);
    FinishPlatform(context, previousPlatform, platformVelocity, wasOnFloor);

    context.result.finalCenter = context.center;
    CommitState(context, state, deltaTimeSeconds);
    return context.result;
}

// =================================================================================================
// The component bridge. CharacterControllerComponent is where the engine keeps a character's
// runtime state; CharacterMotionState is the value the mover works on. These two functions are
// the whole of that translation, kept in one place so the two can never disagree about a field.
// =================================================================================================

CharacterMotionState MakeCharacterMotionState(const CharacterControllerComponent& controller) noexcept {
    CharacterMotionState state;
    state.velocity = controller.velocity;
    state.grounded = controller.grounded;
    state.onWall = controller.onWall;
    state.onCeiling = controller.isOnCeiling;
    state.floorNormal = controller.floorNormal;
    state.wallNormal = controller.wallNormal;
    state.ceilingNormal = controller.ceilingNormal;
    state.timeSinceOnFloor = controller.timeSinceOnFloor;
    state.jumpBufferRemaining = controller.jumpBufferRemaining;
    state.lastMotion = controller.lastMotion;
    state.realVelocity = controller.realVelocity;
    state.platform = controller.platform;
    state.platformWorldPosition = controller.platformWorldPosition;
    state.platformVelocity = controller.platformVelocity;
    state.hasPlatformWorldPosition = controller.hasPlatformWorldPosition;
    return state;
}

void StoreCharacterMotionState(CharacterControllerComponent& controller,
                                  const CharacterMotionState& state) noexcept {
    controller.velocity = state.velocity;
    controller.grounded = state.grounded;
    controller.onWall = state.onWall;
    controller.isOnCeiling = state.onCeiling;
    controller.floorNormal = state.floorNormal;
    controller.wallNormal = state.wallNormal;
    controller.ceilingNormal = state.ceilingNormal;
    controller.timeSinceOnFloor = state.timeSinceOnFloor;
    controller.jumpBufferRemaining = state.jumpBufferRemaining;
    controller.lastMotion = state.lastMotion;
    controller.realVelocity = state.realVelocity;
    controller.platform = state.platform;
    controller.platformWorldPosition = state.platformWorldPosition;
    controller.platformVelocity = state.platformVelocity;
    controller.hasPlatformWorldPosition = state.hasPlatformWorldPosition;
}

CharacterMotionConfig MakeCharacterMotionConfig(
    const CharacterControllerComponent& controller) noexcept {
    CharacterMotionConfig config;
    config.upDirection = controller.upDirection;
    config.floorMaxAngleDegrees = controller.floorMaxAngleDegrees;
    config.wallMinSlideAngleDegrees = controller.wallMinSlideAngleDegrees;
    config.safeMargin = controller.safeMargin;
    config.maxSlides = static_cast<std::size_t>(controller.maxSlides);
    config.floorSnapLength = controller.floorSnapLength;
    config.maxStepHeight = controller.maxStepHeight;
    config.minStepWidth = controller.minStepWidth;
    config.maximumContacts = static_cast<std::size_t>(controller.maximumContacts);
    config.floorBlockOnWall = controller.floorBlockOnWall;
    config.slideOnCeiling = controller.slideOnCeiling;
    config.floorStopOnSlope = controller.floorStopOnSlope;
    config.floorConstantSpeed = controller.floorConstantSpeed;
    config.platformOnLeave = controller.platformOnLeave;
    config.maximumPlatformSpeed = controller.maximumPlatformSpeed;
    // A floating body has no floor to stand on and no step to climb: that mode's whole contract is
    // that every surface is a wall it slides along.
    const bool grounded = controller.motionMode != CharacterMotionMode::Floating;
    config.detectFloor = grounded;
    if (!grounded) {
        config.floorSnapLength = 0.0F;
        config.maxStepHeight = 0.0F;
        config.floorStopOnSlope = false;
    }
    return config;
}

// =================================================================================================
// The node definition. Unchanged in behavior from before the mover existed: a recipe that applies
// the SolidBody3D base, then the body's collider and controller, each only where missing.
// =================================================================================================

bool IsLegacyCharacterCapsule(const ColliderComponent& collider) noexcept {
    // Compared with a tolerance rather than ==: these numbers survive a round trip through the
    // scene file as text, and a value written as 1.8 can come back as the nearest float to it. The
    // tolerance is far below anything an author types (a millimetre), so a capsule they sized
    // themselves is not mistaken for this one.
    constexpr float kTolerance = 1.0e-3F;
    return collider.shapeType == ColliderShapeType::Capsule &&
           std::fabs(collider.radius - Character3DObjectDefinition::MakeDefaultCollider().radius) <=
               kTolerance &&
           std::fabs(collider.height - Character3DObjectDefinition::MakeDefaultCollider().height) <=
               kTolerance;
}

bool IsCharacter3DObjectDefinitionValid(const Character3DObjectDefinition& value) noexcept {
    ControllerComponent authoredController;
    authoredController.kind = value.controllerKind;
    // No collider is a valid character: it is a controller waiting for the author to give it a
    // body, which the Outliner flags. A collider that IS given must be a real one.
    return (!value.collider.has_value() || IsColliderComponentValid(*value.collider)) &&
           IsCharacterControllerComponentValid(value.controller) &&
           IsControllerComponentValid(authoredController);
}

void ApplyCharacter3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                         const Character3DObjectDefinition& value) {
    if (!entityManager.IsAlive(entity)) {
        return;
    }
    ApplySolidBody3DBase(entityManager, entity, Character3DObjectDefinition::defaultName);
    // Only a definition that names a shape gets one. The default names none, so a Character3D
    // authored from the object menu arrives with a controller and no body, and adding the
    // Collider3D the author wants is the first shape it ever has rather than a second one
    // overlapping a capsule it was born with.
    if (value.collider.has_value() && !entityManager.HasComponent<ColliderComponent>(entity)) {
        entityManager.AddComponent<ColliderComponent>(entity, *value.collider);
    }
    if (!entityManager.HasComponent<CharacterControllerComponent>(entity)) {
        entityManager.AddComponent<CharacterControllerComponent>(entity, value.controller);
    }
    if (!entityManager.HasComponent<PawnComponent>(entity)) {
        entityManager.AddComponent<PawnComponent>(entity, PawnComponent{});
    }
    if (!entityManager.HasComponent<ControllerComponent>(entity)) {
        ControllerComponent possession;
        possession.kind = value.controllerKind;
        entityManager.AddComponent<ControllerComponent>(entity, possession);
    }
}

Entity ResolveCharacterShapeEntity(IEntityManager& entityManager,
                                           ISceneGraph& sceneGraph, const Entity character) {
    if (character == kInvalidEntity || !entityManager.IsAlive(character)) {
        return kInvalidEntity;
    }
    // A collider authored on the character itself is the body. Checked first, so an author who
    // built the shape directly never has it replaced by something below it.
    if (entityManager.HasComponent<ColliderComponent>(character)) {
        return character;
    }
    // Otherwise the first child that is a shape and nothing else. "Nothing else" is the whole
    // test, and it has two halves.
    //
    // A body of its own is not a shape: a Rigid3D, a Static3D or another Character3D also carries
    // a collider, but each of them is a neighbour standing next to this one, not a description of
    // it.
    //
    // And neither is something the author can see. Every mesh this engine makes - a Box, Sphere or
    // Plane primitive, a MeshInstance3D given a collider - carries a collider of its own, because a
    // mesh you can see is something you can hit. Adopting that collider made the mesh a character's
    // body by accident: a body with no Collider3D anywhere was held up by the shape of the model it
    // wears, with nothing in the Inspector to show for it. A shape a character is built from is a
    // Collider3D, which draws nothing.
    for (const Entity child : sceneGraph.GetChildren(entityManager, character)) {
        if (!entityManager.HasComponent<ColliderComponent>(child)) {
            continue;
        }
        if (entityManager.HasComponent<SolidBodyComponent>(child) ||
            entityManager.HasComponent<Rigid3DComponent>(child) ||
            entityManager.HasComponent<CharacterControllerComponent>(child)) {
            continue;
        }
        if (entityManager.HasComponent<PrimitiveMeshComponent>(child) ||
            entityManager.HasComponent<MeshComponent>(child)) {
            continue;
        }
        return child;
    }
    return kInvalidEntity;
}

Physics::CharacterMotionInput ResolveCharacterMotion(IEntityManager& entityManager,
                                                           const Entity entity) {
    Physics::CharacterMotionInput motion{};
    const Entity driver = FindPawnController(entityManager, entity);
    if (driver != kInvalidEntity) {
        const PawnComponent& pawn = entityManager.GetComponent<PawnComponent>(entity);
        if (IsPawnComponentValid(pawn)) {
            motion.move = pawn.input.move;
            motion.rise = pawn.input.rise;
            motion.jumpPressed = pawn.input.jumpPressed;
            if (entityManager.GetComponent<ControllerComponent>(driver).kind ==
                    ControllerKind::Player &&
                entityManager.HasComponent<TransformComponent>(entity)) {
                motion.move = FaceMoveFromLook(
                    entityManager.GetComponent<TransformComponent>(entity).localRotation,
                    pawn.input.move);
            }
        }
    }
    // A script's steering (Add Movement Input, Jump) adds to the player's, in world space.
    if (entityManager.HasComponent<CharacterControllerComponent>(entity)) {
        const CharacterControllerComponent& body = entityManager.GetComponent<CharacterControllerComponent>(entity);
        if (std::isfinite(body.scriptMove.x) && std::isfinite(body.scriptMove.z) &&
            (body.scriptMove.x != 0.0F || body.scriptMove.z != 0.0F)) {
            Math::Vector3 move{motion.move.x + body.scriptMove.x, 0.0F, motion.move.z + body.scriptMove.z};
            const float length = std::sqrt(move.x * move.x + move.z * move.z);
            if (length > 1.0F) {
                move = Math::Vector3{move.x / length, 0.0F, move.z / length};
            }
            motion.move = move;
        }
        motion.jumpPressed = motion.jumpPressed || body.scriptJump;
    }
    return motion;
}

} // namespace UVE::Scene
