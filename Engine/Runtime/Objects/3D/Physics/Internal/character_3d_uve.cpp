// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/character_3d_uve.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_physics_objects_3d_uve.h"

namespace UVE::Scene {
namespace {

constexpr float kPiUVE = 3.14159265358979323846F;
constexpr float kRadiansPerDegreeUVE = kPiUVE / 180.0F;
constexpr float kDegreesPerRadianUVE = 180.0F / kPiUVE;

/// Below this a vector is too short to say anything about direction.
constexpr float kMinimumNormalizableLengthUVE = 1.0e-12F;

/// How many contacts one step pushes the body out of. A body genuinely buried in geometry needs a
/// teleport, not a fourth push - four is where "a few things overlapped at once" stops being a
/// believable situation and starts being a broken scene, and pushing further would launch the body
/// out of it at speed instead of telling the caller about it.
constexpr std::size_t kMaximumDepenetrationPassesUVE = 4U;

/// The margin a floor probe lifts the body by before sweeping down, as a multiple of Safe Margin.
/// Two margins plus a hair: strictly more than the skin the body keeps, so the sweep starts clear
/// of the surface rather than exactly on it - the one position a sweep has no well-defined answer.
constexpr float kFloorProbeLiftMarginFactorUVE = 2.0F;

// =================================================================================================
// Value helpers. Every one of these is deliberately small, total, and free of engine state: the
// mover is arithmetic over a handful of vectors, and keeping it that way is what makes a step
// deterministic and this file testable without a world.
// =================================================================================================

[[nodiscard]] bool IsUsableVectorUVE(const Math::Vector3UVE& value) noexcept {
    return Math::IsFiniteUVE(value);
}

[[nodiscard]] float VectorLengthUVE(const Math::Vector3UVE& value) noexcept {
    const float lengthSquared = Math::LengthSquaredUVE(value);
    if (!std::isfinite(lengthSquared) || lengthSquared < 0.0F) {
        return 0.0F;
    }
    return std::sqrt(lengthSquared);
}

/// `value` scaled to unit length, or nothing when it is too short to have a direction.
[[nodiscard]] std::optional<Math::Vector3UVE> TryNormalizeUVE(const Math::Vector3UVE& value) noexcept {
    const float lengthSquared = Math::LengthSquaredUVE(value);
    if (!IsUsableVectorUVE(value) || !std::isfinite(lengthSquared) ||
        lengthSquared <= kMinimumNormalizableLengthUVE) {
        return std::nullopt;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return value * inverseLength;
}

[[nodiscard]] float FiniteOrZeroUVE(const float value) noexcept {
    return std::isfinite(value) ? value : 0.0F;
}

/// The component of `value` perpendicular to `axis`: what is left of a motion once the part that
/// ran along `axis` is taken out. Used to measure horizontal speed without hardcoding a world up.
[[nodiscard]] Math::Vector3UVE PerpendicularUVE(const Math::Vector3UVE& value,
                                                const Math::Vector3UVE& axis) noexcept {
    const float axisLengthSquared = Math::LengthSquaredUVE(axis);
    if (!std::isfinite(axisLengthSquared) || axisLengthSquared <= 0.0F) {
        return value;
    }
    const float along = Math::DotUVE(value, axis) / axisLengthSquared;
    if (!std::isfinite(along)) {
        return value;
    }
    return value - axis * along;
}

/// The signed length of `value` along a unit axis. Non-finite dots read as zero so a caller never
/// has to special-case NaN on top of "no component along this".
[[nodiscard]] float AlongUnitUVE(const Math::Vector3UVE& value, const Math::Vector3UVE& unitAxis) noexcept {
    const float along = Math::DotUVE(value, unitAxis);
    return std::isfinite(along) ? along : 0.0F;
}

[[nodiscard]] bool IsFiniteStateUVE(const CharacterMotionStateUVE& state) noexcept {
    return IsUsableVectorUVE(state.velocity) && IsUsableVectorUVE(state.floorNormal) &&
           IsUsableVectorUVE(state.wallNormal) && IsUsableVectorUVE(state.ceilingNormal) &&
           IsUsableVectorUVE(state.lastMotion) &&
           IsUsableVectorUVE(state.realVelocity) && IsUsableVectorUVE(state.platformWorldPosition) &&
           IsUsableVectorUVE(state.platformVelocity) && std::isfinite(state.timeSinceOnFloor) &&
           std::isfinite(state.jumpBufferRemaining);
}

// =================================================================================================
// The resolved config: the authored settings after clamping, plus the one derived number the
// classification needs. Everything below works on this, never on the raw config, which is why no
// hand-edited number can make the mover misbehave - only behave like its nearest legal value.
// =================================================================================================

struct ResolvedConfigUVE final {
    Math::Vector3UVE upDirection{0.0F, 1.0F, 0.0F};
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
    CharacterPlatformLeaveModeUVE platformOnLeave = CharacterPlatformLeaveModeUVE::AddVelocity;
    float maximumPlatformSpeed = 50.0F;
    bool jumpedThisStep = false;
    bool clamped = false;
};

[[nodiscard]] float ClampFiniteUVE(const float value, const float minimum, const float maximum,
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

[[nodiscard]] bool TryResolveConfigUVE(const CharacterMotionConfigUVE& config,
                                       ResolvedConfigUVE& resolved) noexcept {
    const std::optional<Math::Vector3UVE> up = TryNormalizeUVE(config.upDirection);
    if (!up.has_value()) {
        return false;
    }
    resolved.upDirection = *up;

    resolved.floorMaxAngleDegrees =
        ClampFiniteUVE(config.floorMaxAngleDegrees, 0.0F, 90.0F, 45.0F, resolved.clamped);
    resolved.wallMinSlideAngleDegrees =
        ClampFiniteUVE(config.wallMinSlideAngleDegrees, 0.0F, 90.0F, 15.0F, resolved.clamped);
    resolved.safeMargin = ClampFiniteUVE(config.safeMargin, 0.0F, 0.25F, 0.001F, resolved.clamped);
    resolved.floorSnapLength = ClampFiniteUVE(config.floorSnapLength, 0.0F, 10.0F, 0.1F, resolved.clamped);
    resolved.maxStepHeight = ClampFiniteUVE(config.maxStepHeight, 0.0F, 10.0F, 0.3F, resolved.clamped);
    resolved.minStepWidth = ClampFiniteUVE(config.minStepWidth, 0.0F, 1.0F, 0.02F, resolved.clamped);
    resolved.maximumPlatformSpeed =
        ClampFiniteUVE(config.maximumPlatformSpeed, 0.0F, 10000.0F, 50.0F, resolved.clamped);

    std::size_t maxSlides = config.maxSlides;
    if (maxSlides < 1U) {
        maxSlides = 1U;
        resolved.clamped = true;
    }
    if (maxSlides > Character3DUVE::kMaximumSlidesUVE) {
        maxSlides = Character3DUVE::kMaximumSlidesUVE;
        resolved.clamped = true;
    }
    resolved.maxSlides = maxSlides;

    std::size_t maximumContacts = config.maximumContacts;
    if (maximumContacts < 1U) {
        maximumContacts = 1U;
        resolved.clamped = true;
    }
    if (maximumContacts > Character3DUVE::kMaximumContactsUVE) {
        maximumContacts = Character3DUVE::kMaximumContactsUVE;
        resolved.clamped = true;
    }
    resolved.maximumContacts = maximumContacts;

    resolved.slideOnCeiling = config.slideOnCeiling;
    resolved.floorBlockOnWall = config.floorBlockOnWall;
    resolved.floorStopOnSlope = config.floorStopOnSlope;
    resolved.floorConstantSpeed = config.floorConstantSpeed;
    resolved.detectFloor = config.detectFloor;
    resolved.jumpedThisStep = config.jumpedThisStep;
    if (config.platformOnLeave > CharacterPlatformLeaveModeUVE::AddUpwardVelocity) {
        resolved.platformOnLeave = CharacterPlatformLeaveModeUVE::KeepVelocity;
        resolved.clamped = true;
    } else {
        resolved.platformOnLeave = config.platformOnLeave;
    }
    return true;
}

// =================================================================================================
// Surface classification.
// =================================================================================================

[[nodiscard]] CharacterSurfaceKindUVE ClassifyNormalUVE(const Math::Vector3UVE& unitUp,
                                                        const ResolvedConfigUVE& config,
                                                        const Math::Vector3UVE& surfaceNormal,
                                                        float& outAngleDegrees) noexcept {
    const std::optional<Math::Vector3UVE> normal = TryNormalizeUVE(surfaceNormal);
    if (!normal.has_value()) {
        return CharacterSurfaceKindUVE::Invalid;
    }
    const float alignment = std::clamp(Math::DotUVE(*normal, unitUp), -1.0F, 1.0F);
    const float angleDegrees = std::acos(alignment) * kDegreesPerRadianUVE;
    outAngleDegrees = angleDegrees;
    if (angleDegrees <= config.floorMaxAngleDegrees) {
        return config.detectFloor ? CharacterSurfaceKindUVE::Floor : CharacterSurfaceKindUVE::Wall;
    }
    if (angleDegrees >= 180.0F - config.floorMaxAngleDegrees) {
        return CharacterSurfaceKindUVE::Ceiling;
    }
    return CharacterSurfaceKindUVE::Wall;
}

/// A wall is slid along only when it is steep enough to be a wall rather than a steep ramp. Inside
/// the band between Floor Max Angle and Wall Min Slide Angle past it, the body stops: that band is
/// the difference between "you may climb this" and "you may not creep up this", and it is the only
/// reading of Wall Min Slide Angle that gives the setting a mechanical meaning.
[[nodiscard]] bool WallSlidesUVE(const ResolvedConfigUVE& config, const float angleDegrees) noexcept {
    return angleDegrees >= config.floorMaxAngleDegrees + config.wallMinSlideAngleDegrees;
}

// =================================================================================================
// The move context: everything one step of MoveAndSlideUVE touches, in one place, so the helpers
// below read as sentences about the body instead of parameter lists.
// =================================================================================================

struct MoveContextUVE final {
    const ICharacterWorldQueryUVE* world = nullptr;
    ResolvedConfigUVE config{};
    CharacterMotionStateUVE moved{};
    CharacterMotionResultUVE result{};
    Math::Vector3UVE center{};
    Math::Vector3UVE remaining{};
    EntityUVE floorEntity = kInvalidEntityUVE;
};

[[nodiscard]] float HorizontalSpeedUVE(const MoveContextUVE& context,
                                       const Math::Vector3UVE& velocity) noexcept {
    return VectorLengthUVE(PerpendicularUVE(velocity, context.config.upDirection));
}

/// Records a contact unless the caller's cap is already spent, in which case it counts the drop so
/// the caller can tell "nothing hit" from "I stopped listening".
void RecordCollisionUVE(MoveContextUVE& context, const CharacterSlideCollisionUVE& collision) {
    // An entity is one surface as far as a report is concerned: a body sliding along a wall touches
    // that wall once, not once per slide. Re-touching an entity already in the report updates what
    // is known about it - the closest approach, and the fact that a step-up happened on it - rather
    // than spending another contact slot and another place for a caller to have to look at.
    for (CharacterSlideCollisionUVE& recorded : context.result.collisions) {
        if (recorded.entity != collision.entity) {
            continue;
        }
        if (collision.travel < recorded.travel) {
            recorded.position = collision.position;
            recorded.travel = collision.travel;
        }
        recorded.depth = std::max(recorded.depth, collision.depth);
        recorded.steppedUp = recorded.steppedUp || collision.steppedUp;
        if (collision.normal != Math::Vector3UVE{}) {
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
void ApplyMotionUVE(MoveContextUVE& context, const Math::Vector3UVE& displacement,
                    Math::Vector3UVE& accumulator) {
    context.center += displacement;
    context.result.appliedMotion += displacement;
    accumulator += displacement;
}

/// Registers what a contact means for the body's surface state. Last contact wins, which is what a
/// sliding body wants: the floor it ends a step standing on is the floor it reports, not the first
/// one it brushed on the way there.
void RegisterSurfaceUVE(MoveContextUVE& context, const Math::Vector3UVE& normal,
                        const EntityUVE entity) {
    float angleDegrees = 0.0F;
    switch (ClassifyNormalUVE(context.config.upDirection, context.config, normal, angleDegrees)) {
        case CharacterSurfaceKindUVE::Floor:
            context.result.onFloor = true;
            context.result.floorNormal = normal;
            context.result.floorAngleDegrees = angleDegrees;
            context.floorEntity = entity;
            break;
        case CharacterSurfaceKindUVE::Wall:
            context.result.onWall = true;
            context.result.wallNormal = normal;
            break;
        case CharacterSurfaceKindUVE::Ceiling:
            context.result.onCeiling = true;
            context.result.ceilingNormal = normal;
            break;
        case CharacterSurfaceKindUVE::Invalid:
        default:
            break;
    }
}

/// Leaves a contact where the caller can read it, filling in the surface's own velocity so a
/// script standing on a moving platform sees how fast the thing under its feet is going.
void RecordSurfaceContactUVE(MoveContextUVE& context, const CharacterSlideCollisionUVE& hit,
                             const Math::Vector3UVE& motion, const Math::Vector3UVE& remainder,
                             const float travel, const float depth, const bool steppedUp) {
    CharacterSlideCollisionUVE collision;
    collision.entity = hit.entity;
    collision.normal = hit.normal;
    collision.position = hit.position;
    collision.colliderVelocity = context.world->GetSurfaceVelocityUVE(hit.entity);
    collision.motion = motion;
    collision.remainder = remainder;
    collision.travel = travel;
    collision.depth = depth;
    collision.steppedUp = steppedUp;
    RegisterSurfaceUVE(context, hit.normal, hit.entity);
    RecordCollisionUVE(context, collision);
}

// =================================================================================================
// Depenetration. A body that starts a step inside geometry is pushed out before it is moved,
// because a sweep that begins overlapping has no meaningful answer: "when do I hit this" is not a
// question about a body already past it.
// =================================================================================================

void DepenetrateUVE(MoveContextUVE& context) {
    if (context.config.safeMargin <= 0.0F) {
        // Without a skin there is nothing to push out to, and pushing to exactly zero would make
        // the next query degenerate again - so a zero margin means "no depenetration", not
        // "depenetrate until touching".
        return;
    }

    // Each pass asks the world about the working center, pushes the deepest overlap there, and
    // asks again. Ghost contacts from the pose we just left cannot keep pushing: that is what
    // GetOverlapsAtUVE is for, and why a snapshot of the start pose is not enough.
    float remainingBudget = Character3DUVE::kMaximumSingleDepenetrationDistanceUVE;
    for (std::size_t pass = 0U; pass < kMaximumDepenetrationPassesUVE; ++pass) {
        if (remainingBudget <= Character3DUVE::kMinimumMotionDistanceUVE) {
            break;
        }
        const std::vector<CharacterSlideCollisionUVE> overlaps =
            context.world->GetOverlapsAtUVE(context.center);
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
            const CharacterSlideCollisionUVE& overlap = overlaps[index];
            const std::optional<Math::Vector3UVE> direction = TryNormalizeUVE(overlap.normal);
            if (!direction.has_value() || !std::isfinite(overlap.depth) || overlap.depth <= 0.0F) {
                continue;
            }
            const Math::Vector3UVE remainder = context.result.depenetrationMotion;
            if (!pushed) {
                const float wanted = overlap.depth + context.config.safeMargin;
                if (!std::isfinite(wanted) || wanted <= 0.0F) {
                    continue;
                }
                const float pushDistance = std::min(wanted, remainingBudget);
                if (!std::isfinite(pushDistance) || pushDistance <= 0.0F) {
                    continue;
                }
                const Math::Vector3UVE push = *direction * pushDistance;
                ApplyMotionUVE(context, push, context.result.depenetrationMotion);
                remainingBudget -= pushDistance;
                ++context.result.depenetrationPasses;
                context.result.depenetrated = true;
                RecordSurfaceContactUVE(context, overlap, push, remainder, 0.0F, overlap.depth,
                                        false);
                pushed = true;
            } else {
                RecordSurfaceContactUVE(context, overlap, Math::Vector3UVE{}, remainder, 0.0F,
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

struct StepResultUVE final {
    Math::Vector3UVE motion{};
    Math::Vector3UVE center{};
    CharacterSlideCollisionUVE landing{};
    /// Horizontal distance of the walk the step did not carry the body over - zero when it carried
    /// all of it, which is the usual case for a step squarely in the body's path.
    float leftoverHorizontal = 0.0F;
};

[[nodiscard]] std::optional<StepResultUVE> TryStepUpUVE(
    const MoveContextUVE& context, const Math::Vector3UVE& horizontalMotion,
    const CharacterSlideCollisionUVE& wallHit) {
    const ResolvedConfigUVE& config = context.config;
    const Math::Vector3UVE up = config.upDirection;
    const float stepHeight = config.maxStepHeight;
    const float horizontalLength = VectorLengthUVE(horizontalMotion);
    if (stepHeight <= Character3DUVE::kMinimumMotionDistanceUVE ||
        horizontalLength <= Character3DUVE::kMinimumMotionDistanceUVE) {
        return std::nullopt;
    }
    const Math::Vector3UVE horizontalDirection = horizontalMotion * (1.0F / horizontalLength);

    // 1. Is there room to stand the body up by a step? A sweep that stops short means something is
    // in the way of the lift itself - a low ceiling, a shelf - and the step is refused.
    //
    // The face the body is pressed against is not in the way of its own lift: a body standing
    // against a low step is already touching it, and a query answered about the whole body would
    // report that touch as a hit the moment the lift starts. Rising along a surface is not the
    // surface blocking the rise, which is the same distinction the skin makes everywhere else.
    // Anything that is genuinely overhead - a different surface, or this one's ledge - still stops
    // the lift, and a step too tall to clear is refused by the forward probe below.
    const std::optional<CharacterSlideCollisionUVE> liftHit = context.world->SweepUVE(
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
        if (ClassifyNormalUVE(up, config, liftHit->normal, liftAngleDegrees) ==
            CharacterSurfaceKindUVE::Ceiling) {
            return std::nullopt;
        }
    }
    const Math::Vector3UVE liftedCenter = context.center + up * stepHeight;

    // 2. Walk forward at the lifted height. The probe reaches past the body's own motion on purpose:
    // whether a surface ahead is a step worth climbing is a question about the *geometry* - is there
    // a tread at least Min Step Width wide - not about how much of this step's walk is left. A body
    // walking straight into a stair spends its whole walk against the riser, and a step judged by
    // what is left of that walk would refuse every stair it exists to climb.
    const float probeDistance =
        std::max(horizontalLength + config.safeMargin, config.minStepWidth + config.safeMargin);
    const std::optional<CharacterSlideCollisionUVE> forwardHit = context.world->SweepUVE(
        liftedCenter, horizontalDirection * probeDistance, config.safeMargin);
    const float clearRun = forwardHit.has_value()
                               ? std::clamp(forwardHit->travel, 0.0F, probeDistance)
                               : probeDistance;
    if (clearRun + Character3DUVE::kMinimumMotionDistanceUVE < config.minStepWidth) {
        // Something immediately in front of the lifted body: a wall to stop against, or a ridge too
        // narrow to stand on - not a step.
        return std::nullopt;
    }
    // How far the step actually carries the body: no further than it was walking, and no further
    // than the tread is clear.
    const float reach = std::min(horizontalLength, clearRun);
    const Math::Vector3UVE forwardCenter = liftedCenter + horizontalDirection * reach;

    // 3. Drop onto whatever is below the forward position. Nothing below within a step means a ledge
    // rather than a step, and walking off it is the walk's job, not the step's.
    const float dropLength = stepHeight + config.safeMargin * kFloorProbeLiftMarginFactorUVE;
    const std::optional<CharacterSlideCollisionUVE> dropHit =
        context.world->SweepUVE(forwardCenter, -up * dropLength, config.safeMargin);
    if (!dropHit.has_value()) {
        return std::nullopt;
    }
    float dropAngleDegrees = 0.0F;
    if (ClassifyNormalUVE(up, config, dropHit->normal, dropAngleDegrees) !=
        CharacterSurfaceKindUVE::Floor) {
        return std::nullopt;
    }
    const float dropDistance = std::clamp(dropHit->travel, 0.0F, dropLength);

    StepResultUVE step;
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

[[nodiscard]] float FloorProbeLiftFromMarginUVE(const float safeMargin) noexcept {
    const float margin = std::max(0.0F, FiniteOrZeroUVE(safeMargin));
    return margin * kFloorProbeLiftMarginFactorUVE + Character3DUVE::kMinimumMotionDistanceUVE;
}

[[nodiscard]] float FloorProbeLiftUVE(const ResolvedConfigUVE& config) noexcept {
    return FloorProbeLiftFromMarginUVE(config.safeMargin);
}

void SnapToFloorUVE(MoveContextUVE& context) {
    const ResolvedConfigUVE& config = context.config;
    if (context.result.onFloor || !config.detectFloor || !context.moved.grounded) {
        return;
    }
    if (config.jumpedThisStep || AlongUnitUVE(context.moved.velocity, config.upDirection) > 0.0F) {
        // A jump is a decision to leave the floor, and a body on its way up is already gone.
        return;
    }

    const float probeDistance = std::max(config.floorSnapLength, Character3DUVE::kMinimumFloorProbeUVE);
    const std::optional<CharacterSlideCollisionUVE> hit = Character3DUVE::ProbeFloorUVE(
        *context.world, context.center, config.upDirection, config.floorMaxAngleDegrees,
        probeDistance, config.safeMargin);
    if (!hit.has_value()) {
        return;
    }
    context.result.onFloor = true;
    context.result.floorNormal = hit->normal;
    context.result.floorAngleDegrees =
        Character3DUVE::SlopeAngleDegreesUVE(hit->normal, config.upDirection);
    context.floorEntity = hit->entity;

    const float lift = FloorProbeLiftUVE(config);
    const float gap = std::max(0.0F, hit->travel - lift);
    Math::Vector3UVE snap{};
    float snapDistance = 0.0F;
    if (gap > Character3DUVE::kMinimumMotionDistanceUVE) {
        snapDistance = std::min(gap, probeDistance);
        snap = -config.upDirection * snapDistance;
        ApplyMotionUVE(context, snap, context.result.snapMotion);
        context.result.snapUsed = true;
    }
    // The probe answered "this is the floor" whether or not there was a gap to close. A script
    // reading collisions after a follow-down has to see that floor, not an empty list that looks
    // like "nothing underfoot".
    RecordSurfaceContactUVE(context, *hit, snap, context.remaining, snapDistance, 0.0F, false);
}

/// The floor probe itself, shared by the snap above and by anything that just wants to ask "what is
/// under me" - an AI deciding whether a ledge is walkable, a placement tool drawing a footprint.
[[nodiscard]] std::optional<CharacterSlideCollisionUVE> ProbeFloorAtUVE(
    const ICharacterWorldQueryUVE& world, const Math::Vector3UVE& center,
    const Math::Vector3UVE& upDirection, const float floorMaxAngleDegrees, const float probeDistance,
    const float safeMargin) {
    const std::optional<Math::Vector3UVE> up = TryNormalizeUVE(upDirection);
    if (!up.has_value() || !IsUsableVectorUVE(center) || !std::isfinite(probeDistance) ||
        !std::isfinite(safeMargin) || probeDistance < 0.0F) {
        return std::nullopt;
    }
    ResolvedConfigUVE probeConfig;
    probeConfig.upDirection = *up;
    probeConfig.floorMaxAngleDegrees = std::clamp(FiniteOrZeroUVE(floorMaxAngleDegrees), 0.0F, 90.0F);

    const float lift = FloorProbeLiftFromMarginUVE(safeMargin);
    const float sweepLength = probeDistance + lift;
    if (sweepLength <= Character3DUVE::kMinimumMotionDistanceUVE) {
        return std::nullopt;
    }
    const std::optional<CharacterSlideCollisionUVE> hit =
        world.SweepUVE(center + *up * lift, -*up * sweepLength, std::max(safeMargin, 0.0F));
    if (!hit.has_value()) {
        return std::nullopt;
    }
    float angleDegrees = 0.0F;
    if (ClassifyNormalUVE(*up, probeConfig, hit->normal, angleDegrees) !=
        CharacterSurfaceKindUVE::Floor) {
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

void MeasurePlatformVelocityUVE(MoveContextUVE& context, const float deltaTimeSeconds) {
    context.moved.platformVelocity = Math::Vector3UVE{};
    if (context.moved.platform == kInvalidEntityUVE || !context.moved.grounded) {
        return;
    }
    const std::optional<Math::Vector3UVE> platformCenter =
        context.world->TryGetEntityCenterUVE(context.moved.platform);
    if (!platformCenter.has_value()) {
        // Destroyed, or no longer a body: there is nothing to ride and nothing to be left by.
        context.moved.platform = kInvalidEntityUVE;
        context.moved.platformWorldPosition = Math::Vector3UVE{};
        context.moved.hasPlatformWorldPosition = false;
        return;
    }
    if (context.moved.hasPlatformWorldPosition &&
        IsUsableVectorUVE(context.moved.platformWorldPosition)) {
        const Math::Vector3UVE measured =
            (*platformCenter - context.moved.platformWorldPosition) * (1.0F / deltaTimeSeconds);
        if (IsUsableVectorUVE(measured)) {
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
[[nodiscard]] Math::Vector3UVE PlatformCarryVelocityUVE(const MoveContextUVE& context) {
    if (!context.moved.grounded || context.moved.platformVelocity == Math::Vector3UVE{}) {
        return Math::Vector3UVE{};
    }
    const float speed = VectorLengthUVE(context.moved.platformVelocity);
    if (!std::isfinite(speed) || speed > context.config.maximumPlatformSpeed) {
        return Math::Vector3UVE{};
    }
    return context.moved.platformVelocity;
}

/// What the platform does to the body's velocity when the body leaves it. The direction of the
/// goodwill is one-way on purpose: a rising platform may throw the body up, a falling one never
/// throws it down, because "the lift went down and took my jump with it" is exactly the bug this
/// setting exists to avoid.
void ApplyPlatformLeaveUVE(MoveContextUVE& context, const Math::Vector3UVE& platformVelocity) {
    if (context.config.platformOnLeave == CharacterPlatformLeaveModeUVE::KeepVelocity ||
        platformVelocity == Math::Vector3UVE{}) {
        return;
    }
    if (context.config.platformOnLeave == CharacterPlatformLeaveModeUVE::AddVelocity) {
        context.moved.velocity += platformVelocity;
        return;
    }
    const float upwardSpeed = Math::DotUVE(platformVelocity, context.config.upDirection);
    if (std::isfinite(upwardSpeed) && upwardSpeed > 0.0F) {
        context.moved.velocity += context.config.upDirection * upwardSpeed;
    }
}

// =================================================================================================
// The slide loop: move, hit, classify, respond, repeat. Every iteration either spends motion or
// stops, so the cap is a bound on work rather than a hope.
// =================================================================================================

void SlideUVE(MoveContextUVE& context) {
    const ResolvedConfigUVE& config = context.config;
    for (std::size_t slide = 0U; slide < config.maxSlides; ++slide) {
        const float remainingLength = VectorLengthUVE(context.remaining);
        if (remainingLength <= Character3DUVE::kMinimumMotionDistanceUVE) {
            context.remaining = Math::Vector3UVE{};
            return;
        }
        const Math::Vector3UVE sweptMotion = context.remaining;
        const Math::Vector3UVE direction = sweptMotion * (1.0F / remainingLength);
        const std::optional<CharacterSlideCollisionUVE> hit =
            context.world->SweepUVE(context.center, sweptMotion, config.safeMargin);
        if (!hit.has_value()) {
            // Nothing in the way for the rest of the move: spend all of it and stop.
            ApplyMotionUVE(context, context.remaining, context.result.slideMotion);
            context.remaining = Math::Vector3UVE{};
            return;
        }

        const float travel = std::clamp(hit->travel, 0.0F, remainingLength);
        ApplyMotionUVE(context, direction * travel, context.result.slideMotion);
        context.remaining -= direction * travel;
        ++context.result.slideCount;

        float angleDegrees = 0.0F;
        const CharacterSurfaceKindUVE kind =
            ClassifyNormalUVE(config.upDirection, config, hit->normal, angleDegrees);

        // A step is a walking convenience, not a second jump: only a body that was already standing,
        // was not jumping, and is not already moving up climbs one.
        //
        // What the step is asked to carry is what is left of this step's walk after the contact;
        // whether the surface ahead is a step at all is decided by TryStepUpUVE from the geometry,
        // not from how much of the walk survived the riser.
        const Math::Vector3UVE stepReach = PerpendicularUVE(context.remaining, config.upDirection);
        const bool canStep = kind == CharacterSurfaceKindUVE::Wall &&
                             config.maxStepHeight > Character3DUVE::kMinimumMotionDistanceUVE &&
                             context.moved.grounded && !config.jumpedThisStep &&
                             AlongUnitUVE(context.moved.velocity, config.upDirection) <= 0.0F;
        if (canStep) {
            if (const std::optional<StepResultUVE> step = TryStepUpUVE(context, stepReach, *hit);
                step.has_value()) {
                context.center = step->center;
                context.result.appliedMotion += step->motion;
                context.result.stepMotion += step->motion;
                context.result.stepUpUsed = true;
                RecordSurfaceContactUVE(context, step->landing, sweptMotion,
                                        PerpendicularUVE(step->motion, config.upDirection),
                                        VectorLengthUVE(step->motion), 0.0F, true);
                // Whatever the step did not carry the body over is still the body's to walk, and
                // walking it from the tread is the next iteration's job - so a body that steps onto
                // the edge of a platform with motion left keeps moving, and one that steps its whole
                // distance stops on the tread instead of grinding into it.
                const float leftover = step->leftoverHorizontal;
                if (leftover <= Character3DUVE::kMinimumMotionDistanceUVE) {
                    context.remaining = Math::Vector3UVE{};
                    return;
                }
                const float reachLength = VectorLengthUVE(stepReach);
                if (reachLength > Character3DUVE::kMinimumMotionDistanceUVE) {
                    context.remaining = stepReach * (leftover / reachLength);
                } else {
                    context.remaining = Math::Vector3UVE{};
                }
                continue;
            }
        }

        RecordSurfaceContactUVE(context, *hit, sweptMotion, context.remaining, travel, hit->depth,
                                false);

        if (kind == CharacterSurfaceKindUVE::Ceiling && !config.slideOnCeiling) {
            // Slide On Ceiling off means the ceiling is a wall in the only way that matters: the
            // move ends against it, horizontal motion included.
            context.remaining = Math::Vector3UVE{};
            context.result.blocked = true;
            return;
        }
        if (kind == CharacterSurfaceKindUVE::Wall) {
            if (config.floorBlockOnWall && context.moved.grounded) {
                context.remaining = Math::Vector3UVE{};
                context.result.blocked = true;
                return;
            }
            if (!WallSlidesUVE(config, angleDegrees)) {
                // The steep-ramp band: the body stops against it instead of creeping up it.
                context.remaining = Math::Vector3UVE{};
                context.result.blocked = true;
                return;
            }
        }

        const float lengthBeforeProjection = VectorLengthUVE(context.remaining);
        Math::Vector3UVE projected =
            Character3DUVE::SlideMotionUVE(context.remaining, hit->normal);

        if (config.floorConstantSpeed && kind == CharacterSurfaceKindUVE::Floor) {
            // Floor Constant Speed: this contact's remaining horizontal is restored after the
            // slope projects it, so a walk up a slope keeps the metres of the walk not yet spent
            // instead of slowing down, and a walk down one does not speed up. Restoring against
            // the original full-step horizontal would invent metres already travelled getting
            // here; restoring against this remaining is what keeps the whole step's horizontal
            // equal to the walk.
            const float horizontalBefore =
                VectorLengthUVE(PerpendicularUVE(context.remaining, config.upDirection));
            const float horizontalAfter =
                VectorLengthUVE(PerpendicularUVE(projected, config.upDirection));
            if (horizontalBefore > Character3DUVE::kMinimumMotionDistanceUVE &&
                horizontalAfter > Character3DUVE::kMinimumMotionDistanceUVE &&
                horizontalAfter < horizontalBefore) {
                const Math::Vector3UVE horizontalPart = PerpendicularUVE(projected, config.upDirection);
                const Math::Vector3UVE alongPart = projected - horizontalPart;
                projected = alongPart + horizontalPart * (horizontalBefore / horizontalAfter);
            }
        }

        if (VectorLengthUVE(projected) < lengthBeforeProjection - Character3DUVE::kMinimumMotionDistanceUVE) {
            context.result.blocked = true;
        }
        context.remaining = projected;
    }

    if (VectorLengthUVE(context.remaining) > Character3DUVE::kMinimumMotionDistanceUVE) {
        // Out of slides with motion left over. The body stops here rather than creeping the rest of
        // the way on the next step, and the caller can see it in `blocked`.
        context.remaining = Math::Vector3UVE{};
        context.result.blocked = true;
    }
}

// =================================================================================================
// What the surfaces under a body do to its velocity once the moving is over. These are the rules a
// player feels as "the character does not creep down ramps" and "a lift throws me up when I jump
// off it", and they are the mover's only writes to velocity - everything else is the intent step's.
// =================================================================================================

void ApplySurfaceVelocityRulesUVE(MoveContextUVE& context) {
    const ResolvedConfigUVE& config = context.config;
    if (!context.result.onFloor) {
        return;
    }
    const float standingSpeed = HorizontalSpeedUVE(context, context.moved.velocity);
    if (!config.floorStopOnSlope || context.result.floorAngleDegrees <= 0.01F ||
        standingSpeed > Character3DUVE::kStandingSpeedUVE ||
        AlongUnitUVE(context.moved.velocity, config.upDirection) >
            Character3DUVE::kMinimumMotionDistanceUVE) {
        return;
    }
    // A body standing on a slope stays standing there. Its gravity is re-applied every step by the
    // intent step, and every step the mover takes the downhill part of it back out - which is what
    // "not sliding down a hill" means for a character that was not walking.
    context.moved.velocity = Math::Vector3UVE{};
}

// =================================================================================================
// Platform bookkeeping at the end of a step: who is underfoot now, and what happens to a body that
// was left behind by something that used to be.
// =================================================================================================

void FinishPlatformUVE(MoveContextUVE& context, const EntityUVE previousPlatform,
                       const Math::Vector3UVE& previousPlatformVelocity, const bool wasOnFloor) {
    if (context.result.onFloor && context.floorEntity != kInvalidEntityUVE) {
        // Standing on something: that thing is the platform, and the next step measures how far it
        // moved from where it is now.
        context.moved.platform = context.floorEntity;
        const std::optional<Math::Vector3UVE> center =
            context.world->TryGetEntityCenterUVE(context.floorEntity);
        context.moved.platformWorldPosition = center.has_value() ? *center : Math::Vector3UVE{};
        context.moved.hasPlatformWorldPosition = center.has_value();
        context.result.platformVelocity = context.moved.platformVelocity;
        return;
    }

    if (wasOnFloor && previousPlatform != kInvalidEntityUVE) {
        // Just left the platform behind. What it was doing when the body left is what it hands
        // over - that is the whole of platform_on_leave - and a platform that was teleported hands
        // over nothing, for the same reason it never carried the body in the first place: a script
        // writing a transform is not motion, and inheriting it would fling the jump across a level.
        const float handedOverSpeed = VectorLengthUVE(previousPlatformVelocity);
        const Math::Vector3UVE handedOverVelocity =
            std::isfinite(handedOverSpeed) && handedOverSpeed <= context.config.maximumPlatformSpeed
                ? previousPlatformVelocity
                : Math::Vector3UVE{};
        ApplyPlatformLeaveUVE(context, handedOverVelocity);
        context.result.platformVelocity = handedOverVelocity;
        context.result.leftPlatform = true;
    }
    context.moved.platform = kInvalidEntityUVE;
    context.moved.platformWorldPosition = Math::Vector3UVE{};
    context.moved.platformVelocity = Math::Vector3UVE{};
    context.moved.hasPlatformWorldPosition = false;
}

// =================================================================================================
// Writing the step's outcome back into the state. Kept as one function so the state can never be
// half-updated - a caller reading `grounded` and `floorNormal` sees two fields of the same answer.
// =================================================================================================

void CommitStateUVE(MoveContextUVE& context, CharacterMotionStateUVE& state,
                    const float deltaTimeSeconds) {
    if (context.result.code != CharacterMoveCodeUVE::Moved) {
        return;
    }
    context.moved.grounded = context.result.onFloor;
    context.moved.onWall = context.result.onWall;
    context.moved.onCeiling = context.result.onCeiling;
    context.moved.floorNormal = context.result.onFloor ? context.result.floorNormal
                                                       : context.config.upDirection;
    context.moved.wallNormal = context.result.onWall ? context.result.wallNormal : Math::Vector3UVE{};
    context.moved.ceilingNormal = context.result.onCeiling ? context.result.ceilingNormal
                                                           : Math::Vector3UVE{};
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

CharacterSurfaceKindUVE ClassifyCharacterSurfaceUVE(const Math::Vector3UVE& surfaceNormal,
                                                   const Math::Vector3UVE& upDirection,
                                                   const float floorMaxAngleDegrees,
                                                   float& outAngleDegrees) noexcept {
    // A caller that cannot be answered is still answered about the angle: "no surface here" reads as
    // zero degrees rather than as whatever the caller last passed in.
    outAngleDegrees = 0.0F;
    const std::optional<Math::Vector3UVE> up = TryNormalizeUVE(upDirection);
    if (!up.has_value()) {
        return CharacterSurfaceKindUVE::Invalid;
    }
    ResolvedConfigUVE config;
    config.upDirection = *up;
    config.floorMaxAngleDegrees = std::clamp(FiniteOrZeroUVE(floorMaxAngleDegrees), 0.0F, 90.0F);
    return ClassifyNormalUVE(*up, config, surfaceNormal, outAngleDegrees);
}

float Character3DUVE::SlopeAngleDegreesUVE(const Math::Vector3UVE& normal,
                                           const Math::Vector3UVE& upDirection) noexcept {
    const std::optional<Math::Vector3UVE> up = TryNormalizeUVE(upDirection);
    const std::optional<Math::Vector3UVE> unitNormal = TryNormalizeUVE(normal);
    if (!up.has_value() || !unitNormal.has_value()) {
        return 0.0F;
    }
    const float alignment = std::clamp(Math::DotUVE(*unitNormal, *up), -1.0F, 1.0F);
    return std::acos(alignment) * kDegreesPerRadianUVE;
}

Math::Vector3UVE Character3DUVE::SlideMotionUVE(const Math::Vector3UVE& motion,
                                                const Math::Vector3UVE& normal) noexcept {
    if (!IsUsableVectorUVE(motion) || !IsUsableVectorUVE(normal)) {
        return motion;
    }
    const float normalLengthSquared = Math::LengthSquaredUVE(normal);
    if (!std::isfinite(normalLengthSquared) || normalLengthSquared <= 0.0F) {
        return motion;
    }
    const float intoSurface = Math::DotUVE(motion, normal);
    if (!std::isfinite(intoSurface) || intoSurface >= 0.0F) {
        // Already leaving, or running along: there is nothing to take out.
        return motion;
    }
    return motion - normal * (intoSurface / normalLengthSquared);
}

std::optional<CharacterSlideCollisionUVE> Character3DUVE::TestMoveUVE(
    const ICharacterWorldQueryUVE& world, const Math::Vector3UVE& motion, const float margin) {
    if (!IsUsableVectorUVE(motion) || !std::isfinite(margin) || margin < 0.0F) {
        return std::nullopt;
    }
    return world.SweepUVE(world.GetCenterUVE(), motion, std::min(margin, 0.25F));
}

std::optional<CharacterSlideCollisionUVE> Character3DUVE::ProbeFloorUVE(
    const ICharacterWorldQueryUVE& world, const Math::Vector3UVE& center,
    const Math::Vector3UVE& upDirection, const float floorMaxAngleDegrees, const float probeDistance,
    const float safeMargin) noexcept {
    return ProbeFloorAtUVE(world, center, upDirection, floorMaxAngleDegrees, probeDistance, safeMargin);
}

CharacterMotionResultUVE Character3DUVE::MoveAndSlideUVE(const ICharacterWorldQueryUVE& world,
                                                        const CharacterMotionConfigUVE& config,
                                                        CharacterMotionStateUVE& state,
                                                        const Math::Vector3UVE& desiredMotion,
                                                        const float deltaTimeSeconds) {
    MoveContextUVE context;
    context.world = &world;
    context.result.requestedMotion = desiredMotion;
    context.result.code = CharacterMoveCodeUVE::InvalidConfig;

    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F ||
        !IsUsableVectorUVE(desiredMotion)) {
        return context.result;
    }
    if (!TryResolveConfigUVE(config, context.config)) {
        return context.result;
    }
    if (!IsFiniteStateUVE(state)) {
        context.result.code = CharacterMoveCodeUVE::InvalidState;
        return context.result;
    }
    context.moved = state;
    context.result.configClamped = context.config.clamped;

    const Math::Vector3UVE center = world.GetCenterUVE();
    if (!IsUsableVectorUVE(center)) {
        context.result.code = CharacterMoveCodeUVE::InvalidWorld;
        return context.result;
    }
    context.center = center;
    context.result.code = CharacterMoveCodeUVE::Moved;

    const bool wasOnFloor = state.grounded;
    const EntityUVE previousPlatform = state.platform;

    // ---- What the platform underfoot did since the last step, and what that means for this one ---
    MeasurePlatformVelocityUVE(context, deltaTimeSeconds);
    const Math::Vector3UVE platformVelocity = context.moved.platformVelocity;
    const Math::Vector3UVE carry = PlatformCarryVelocityUVE(context) * deltaTimeSeconds;
    context.result.platformVelocity = platformVelocity;
    if (carry != Math::Vector3UVE{}) {
        context.result.carriedByPlatform = true;
    }

    const Math::Vector3UVE motion = desiredMotion + carry;

    // ---- Push out of anything the body started inside of, then walk it ---------------------------
    DepenetrateUVE(context);
    context.remaining = motion;
    SlideUVE(context);
    SnapToFloorUVE(context);
    // Numerical overlap after a slide or a snap is the same question as the one at the start of
    // the step, asked of the pose we actually ended in - not of the pose we started in.
    DepenetrateUVE(context);
    ApplySurfaceVelocityRulesUVE(context);
    FinishPlatformUVE(context, previousPlatform, platformVelocity, wasOnFloor);

    context.result.finalCenter = context.center;
    CommitStateUVE(context, state, deltaTimeSeconds);
    return context.result;
}

// =================================================================================================
// The component bridge. CharacterControllerComponentUVE is where the engine keeps a character's
// runtime state; CharacterMotionStateUVE is the value the mover works on. These two functions are
// the whole of that translation, kept in one place so the two can never disagree about a field.
// =================================================================================================

CharacterMotionStateUVE MakeCharacterMotionStateUVE(const CharacterControllerComponentUVE& controller) noexcept {
    CharacterMotionStateUVE state;
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

void StoreCharacterMotionStateUVE(CharacterControllerComponentUVE& controller,
                                  const CharacterMotionStateUVE& state) noexcept {
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

CharacterMotionConfigUVE MakeCharacterMotionConfigUVE(
    const CharacterControllerComponentUVE& controller) noexcept {
    CharacterMotionConfigUVE config;
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
    const bool grounded = controller.motionMode != CharacterMotionModeUVE::Floating;
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

bool IsCharacter3DObjectDefinitionValidUVE(const Character3DObjectDefinitionUVE& value) noexcept {
    return IsColliderComponentValidUVE(value.collider) &&
           IsCharacterControllerComponentValidUVE(value.controller);
}

void ApplyCharacter3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                         const Character3DObjectDefinitionUVE& value) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    ApplySolidBody3DBaseUVE(entityManager, entity, Character3DObjectDefinitionUVE::defaultName);
    if (!entityManager.HasComponentUVE<ColliderComponentUVE>(entity)) {
        entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
    }
    if (!entityManager.HasComponentUVE<CharacterControllerComponentUVE>(entity)) {
        entityManager.AddComponentUVE<CharacterControllerComponentUVE>(entity, value.controller);
    }
}

} // namespace UVE::Scene
