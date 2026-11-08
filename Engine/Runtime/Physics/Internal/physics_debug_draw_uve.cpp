// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/physics_debug_draw_uve.h"

namespace UVE::Physics {

void GeneratePhysicsDebugDrawUVE(
    const MotionTrajectoryPredictionUVE& trajectory,
    const ActiveRagdollStateUVE& activeRagdoll,
    const ImpactAnalysisSummaryUVE& impactSummary,
    const PhysicsDebugCategoryUVE enabledCategories,
    PhysicsDebugBufferUVE& outBuffer) noexcept {
    // 1. Contacts
    if (HasCategoryUVE(enabledCategories, PhysicsDebugCategoryUVE::Contacts)) {
        for (const auto& c : impactSummary.contacts) {
            outBuffer.points.push_back(PhysicsDebugPointUVE{
                .position = c.contactPoint,
                .color = Math::Vector3UVE{1.0F, 0.2F, 0.2F},
                .size = 0.04F,
            });
            outBuffer.lines.push_back(PhysicsDebugLineUVE{
                .start = c.contactPoint,
                .end = c.contactPoint + c.contactNormal * 0.25F,
                .color = Math::Vector3UVE{1.0F, 0.4F, 0.2F},
            });
        }
    }

    // 2. Impulses
    if (HasCategoryUVE(enabledCategories, PhysicsDebugCategoryUVE::Impulses)) {
        if (impactSummary.HasImpactUVE()) {
            outBuffer.lines.push_back(PhysicsDebugLineUVE{
                .start = impactSummary.primaryContactPoint,
                .end = impactSummary.primaryContactPoint + impactSummary.totalImpulse * 0.005F,
                .color = Math::Vector3UVE{1.0F, 0.9F, 0.1F},
            });
        }
    }

    // 3. Trajectory
    if (HasCategoryUVE(enabledCategories, PhysicsDebugCategoryUVE::Trajectory)) {
        for (std::size_t i = 1U; i < trajectory.samples.size(); ++i) {
            outBuffer.lines.push_back(PhysicsDebugLineUVE{
                .start = trajectory.samples[i - 1U].position,
                .end = trajectory.samples[i].position,
                .color = Math::Vector3UVE{0.1F, 0.8F, 1.0F},
            });
        }
        for (const auto& hit : trajectory.predictedContacts) {
            outBuffer.points.push_back(PhysicsDebugPointUVE{
                .position = hit.contactPosition,
                .color = hit.isWall ? Math::Vector3UVE{1.0F, 0.1F, 0.5F} : Math::Vector3UVE{0.1F, 1.0F, 0.3F},
                .size = 0.06F,
            });
        }
    }

    // 4. Center of Mass & Velocity
    if (HasCategoryUVE(enabledCategories, PhysicsDebugCategoryUVE::CenterOfMass)) {
        outBuffer.points.push_back(PhysicsDebugPointUVE{
            .position = activeRagdoll.centerOfMass,
            .color = Math::Vector3UVE{1.0F, 1.0F, 0.0F},
            .size = 0.08F,
        });
        outBuffer.lines.push_back(PhysicsDebugLineUVE{
            .start = activeRagdoll.centerOfMass,
            .end = activeRagdoll.centerOfMass + activeRagdoll.centerOfMassVelocity * 0.2F,
            .color = Math::Vector3UVE{1.0F, 0.8F, 0.0F},
        });
    }

    // 5. Support Polygon & ZMP
    if (HasCategoryUVE(enabledCategories, PhysicsDebugCategoryUVE::SupportPolygon)) {
        const auto& poly = activeRagdoll.supportPolygon;
        const std::size_t n = poly.vertices.size();
        for (std::size_t i = 0U; i < n; ++i) {
            const auto& a = poly.vertices[i];
            const auto& b = poly.vertices[(i + 1U) % n];
            outBuffer.lines.push_back(PhysicsDebugLineUVE{
                .start = Math::Vector3UVE{a.x, 0.02F, a.y},
                .end = Math::Vector3UVE{b.x, 0.02F, b.y},
                .color = Math::Vector3UVE{0.2F, 1.0F, 0.2F},
            });
        }
        // Extrapolated Center of Mass (ZMP)
        outBuffer.points.push_back(PhysicsDebugPointUVE{
            .position = Math::Vector3UVE{activeRagdoll.extrapolatedCenterOfMass.x, 0.03F,
                                         activeRagdoll.extrapolatedCenterOfMass.y},
            .color = Math::Vector3UVE{1.0F, 0.0F, 1.0F},
            .size = 0.06F,
        });
    }

    // 6. Landing
    if (HasCategoryUVE(enabledCategories, PhysicsDebugCategoryUVE::Landing)) {
        if (trajectory.hasPredictedLanding) {
            outBuffer.points.push_back(PhysicsDebugPointUVE{
                .position = trajectory.predictedLandingPosition,
                .color = Math::Vector3UVE{0.0F, 1.0F, 0.8F},
                .size = 0.1F,
            });
            outBuffer.lines.push_back(PhysicsDebugLineUVE{
                .start = trajectory.predictedLandingPosition,
                .end = trajectory.predictedLandingPosition + trajectory.predictedLandingNormal * 0.5F,
                .color = Math::Vector3UVE{0.0F, 1.0F, 0.8F},
            });
        }
    }
}

} // namespace UVE::Physics
