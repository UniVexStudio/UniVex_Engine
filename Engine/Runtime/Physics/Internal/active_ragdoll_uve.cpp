// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/active_ragdoll_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Physics {
namespace {

constexpr float kEpsilon = 1.0e-5F;

[[nodiscard]] float Cross2DUVE(const Math::Vector2UVE& a, const Math::Vector2UVE& b) noexcept {
    return a.x * b.y - a.y * b.x;
}

[[nodiscard]] float DistancePointToSegmentUVE(
    const Math::Vector2UVE& p, const Math::Vector2UVE& a, const Math::Vector2UVE& b) noexcept {
    const Math::Vector2UVE ab = b - a;
    const float abLenSq = ab.x * ab.x + ab.y * ab.y;
    if (abLenSq <= kEpsilon) {
        const Math::Vector2UVE d = p - a;
        return std::sqrt(d.x * d.x + d.y * d.y);
    }
    const Math::Vector2UVE ap = p - a;
    const float t = std::clamp((ap.x * ab.x + ap.y * ab.y) / abLenSq, 0.0F, 1.0F);
    const Math::Vector2UVE proj = a + ab * t;
    const Math::Vector2UVE delta = p - proj;
    return std::sqrt(delta.x * delta.x + delta.y * delta.y);
}

} // namespace

bool SupportPolygonUVE::ContainsPointUVE(const Math::Vector2UVE& point) const noexcept {
    if (vertices.size() < 3U) {
        return false;
    }
    bool positive = false;
    bool negative = false;
    const std::size_t n = vertices.size();
    for (std::size_t i = 0U; i < n; ++i) {
        const Math::Vector2UVE& a = vertices[i];
        const Math::Vector2UVE& b = vertices[(i + 1U) % n];
        const float cross = Cross2DUVE(b - a, point - a);
        if (cross > kEpsilon) positive = true;
        if (cross < -kEpsilon) negative = true;
        if (positive && negative) return false;
    }
    return true;
}

float SupportPolygonUVE::SignedDistanceToBoundaryUVE(const Math::Vector2UVE& point) const noexcept {
    if (vertices.empty()) {
        return -10.0F;
    }
    if (vertices.size() == 1U) {
        const Math::Vector2UVE d = point - vertices[0];
        return -std::sqrt(d.x * d.x + d.y * d.y);
    }
    float minDistance = 1.0e9F;
    const std::size_t n = vertices.size();
    for (std::size_t i = 0U; i < n; ++i) {
        const Math::Vector2UVE& a = vertices[i];
        const Math::Vector2UVE& b = vertices[(i + 1U) % n];
        const float dist = DistancePointToSegmentUVE(point, a, b);
        minDistance = std::min(minDistance, dist);
    }
    const bool inside = ContainsPointUVE(point);
    return inside ? minDistance : -minDistance;
}

SupportPolygonUVE ComputeSupportPolygonUVE(
    const std::span<const RagdollContactUVE> contacts) noexcept {
    SupportPolygonUVE poly;
    std::vector<Math::Vector2UVE> pts;
    pts.reserve(contacts.size());

    for (const RagdollContactUVE& c : contacts) {
        if (c.normal.y >= 0.5F) { // Ground contact
            pts.push_back(Math::Vector2UVE{c.point.x, c.point.z});
        }
    }

    if (pts.empty()) {
        return poly;
    }
    if (pts.size() <= 2U) {
        poly.vertices = pts;
        Math::Vector2UVE sum{};
        for (const auto& p : pts) {
            sum.x += p.x;
            sum.y += p.y;
        }
        poly.centroid = sum * (1.0F / static_cast<float>(pts.size()));
        return poly;
    }

    // Monotone Chain 2D Convex Hull
    std::sort(pts.begin(), pts.end(), [](const Math::Vector2UVE& a, const Math::Vector2UVE& b) {
        if (std::abs(a.x - b.x) > kEpsilon) return a.x < b.x;
        return a.y < b.y;
    });
    // Remove duplicates
    pts.erase(std::unique(pts.begin(), pts.end(), [](const Math::Vector2UVE& a, const Math::Vector2UVE& b) {
        return std::abs(a.x - b.x) <= kEpsilon && std::abs(a.y - b.y) <= kEpsilon;
    }), pts.end());

    if (pts.size() <= 2U) {
        poly.vertices = pts;
        Math::Vector2UVE sum{};
        for (const auto& p : pts) {
            sum.x += p.x;
            sum.y += p.y;
        }
        poly.centroid = sum * (1.0F / static_cast<float>(pts.size()));
        return poly;
    }

    std::vector<Math::Vector2UVE> hull;
    hull.reserve(pts.size() * 2U);

    // Lower hull
    for (const auto& p : pts) {
        while (hull.size() >= 2U && Cross2DUVE(hull.back() - hull[hull.size() - 2U], p - hull.back()) <= 0.0F) {
            hull.pop_back();
        }
        hull.push_back(p);
    }
    // Upper hull
    const std::size_t lowerCount = hull.size();
    for (std::size_t i = pts.size() - 1U; i > 0U; --i) {
        const auto& p = pts[i - 1U];
        while (hull.size() > lowerCount && Cross2DUVE(hull.back() - hull[hull.size() - 2U], p - hull.back()) <= 0.0F) {
            hull.pop_back();
        }
        hull.push_back(p);
    }
    hull.pop_back(); // Remove last duplicate vertex

    poly.vertices = hull;

    // Centroid and Area via Shoelace Formula
    float area2 = 0.0F;
    Math::Vector2UVE centroidSum{};
    const std::size_t hn = poly.vertices.size();
    for (std::size_t i = 0U; i < hn; ++i) {
        const Math::Vector2UVE& a = poly.vertices[i];
        const Math::Vector2UVE& b = poly.vertices[(i + 1U) % hn];
        const float cross = a.x * b.y - b.x * a.y;
        area2 += cross;
        centroidSum.x += (a.x + b.x) * cross;
        centroidSum.y += (a.y + b.y) * cross;
    }
    poly.area = std::abs(area2) * 0.5F;
    if (std::abs(area2) > kEpsilon) {
        poly.centroid = centroidSum * (1.0F / (3.0F * area2));
    } else {
        Math::Vector2UVE sum{};
        for (const auto& v : poly.vertices) {
            sum.x += v.x;
            sum.y += v.y;
        }
        poly.centroid = sum * (1.0F / std::max(1.0F, static_cast<float>(poly.vertices.size())));
    }

    return poly;
}

void ActiveRagdollControllerUVE::UpdateBalanceAndPostureUVE(
    RagdollSolverUVE& solver,
    const std::span<const RagdollContactUVE> contacts,
    const Math::QuaternionUVE& rootOrientation,
    const float deltaTimeSeconds) noexcept {
    const Math::Vector3UVE com = solver.ComputeCenterOfMassUVE();
    const Math::Vector3UVE comVel = solver.ComputeCenterOfMassVelocityUVE();
    m_state.centerOfMass = com;
    m_state.centerOfMassVelocity = comVel;

    // Zero-Moment Point / Extrapolated Center of Mass on XZ plane
    const float h = std::max(0.1F, m_config.nominalComHeight);
    const float omega0 = std::sqrt(9.81F / h);
    const Math::Vector2UVE comXZ{com.x, com.z};
    const Math::Vector2UVE comVelXZ{comVel.x, comVel.z};
    const Math::Vector2UVE xcom = comXZ + comVelXZ * (1.0F / omega0);
    m_state.extrapolatedCenterOfMass = xcom;

    m_state.supportPolygon = ComputeSupportPolygonUVE(contacts);
    const float margin = m_state.supportPolygon.SignedDistanceToBoundaryUVE(xcom);
    m_state.stabilityMargin = margin;

    const Math::Vector3UVE up = Math::RotateVectorUVE(rootOrientation, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    const float uprightness = up.y;

    // Evaluate balance state
    if (uprightness < 0.35F || com.y < h * 0.45F || margin < -m_config.maxStepReach) {
        m_state.balanceState = BalanceStateUVE::UnrecoverableFall;
    } else if (uprightness < 0.75F || margin < -0.10F) {
        m_state.balanceState = BalanceStateUVE::CorrectiveStepping;
        m_state.correctiveStepTarget = Math::Vector3UVE{xcom.x, com.y - h, xcom.y};
    } else if (uprightness < 0.92F || margin < 0.05F) {
        m_state.balanceState = BalanceStateUVE::Disturbed;
    } else {
        m_state.balanceState = BalanceStateUVE::Stable;
    }

    // Adapt active blend weight and joint drives
    if (m_state.balanceState == BalanceStateUVE::UnrecoverableFall) {
        m_state.activeBlendWeight = std::max(0.0F, m_state.activeBlendWeight - m_config.fallRelaxationRate * deltaTimeSeconds);
    } else {
        m_state.activeBlendWeight = std::min(1.0F, m_state.activeBlendWeight + 2.0F * deltaTimeSeconds);
    }

    const float weight = m_state.activeBlendWeight;
    float currentKp = m_config.stiffnessMinor;
    float currentKd = m_config.dampingMinor;

    switch (m_state.balanceState) {
    case BalanceStateUVE::Stable:
        currentKp = m_config.stiffnessMinor * weight;
        currentKd = m_config.dampingMinor * weight;
        break;
    case BalanceStateUVE::Disturbed:
        currentKp = m_config.stiffnessMinor * weight;
        currentKd = m_config.dampingMinor * weight;
        // Upper-body spine compensation: lean slightly opposite to CoM horizontal velocity
        m_state.spineRestorativeTorque = Math::Vector3UVE{-comVel.z, 0.0F, comVel.x} * (m_config.spineCompensationGain * 100.0F);
        break;
    case BalanceStateUVE::CorrectiveStepping:
        currentKp = m_config.stiffnessModerate * weight;
        currentKd = m_config.dampingModerate * weight;
        break;
    case BalanceStateUVE::UnrecoverableFall:
        currentKp = m_config.stiffnessSevere * weight;
        currentKd = m_config.dampingSevere * weight;
        break;
    }

    // Apply stiffness and damping to all driven joints in solver
    const std::size_t jointCount = solver.GetJointCountUVE();
    for (std::size_t i = 0U; i < jointCount; ++i) {
        RagdollJointUVE& joint = solver.GetJointUVE(i);
        if (joint.driveEnabled) {
            joint.springStiffness = currentKp;
            joint.damping = currentKd;
        }
    }
}

void ActiveRagdollControllerUVE::SetJointTargetPoseUVE(
    RagdollSolverUVE& solver,
    const std::size_t jointIndex,
    const Math::QuaternionUVE& targetRelativeRotation) noexcept {
    if (jointIndex >= solver.GetJointCountUVE()) {
        return;
    }
    RagdollJointUVE& joint = solver.GetJointUVE(jointIndex);
    joint.driveEnabled = true;
    joint.targetRotation = targetRelativeRotation;
}

void ActiveRagdollControllerUVE::ReleaseAllJointDrivesUVE(RagdollSolverUVE& solver) noexcept {
    const std::size_t jointCount = solver.GetJointCountUVE();
    for (std::size_t i = 0U; i < jointCount; ++i) {
        solver.GetJointUVE(i).driveEnabled = false;
    }
    m_state.activeBlendWeight = 0.0F;
    m_state.balanceState = BalanceStateUVE::UnrecoverableFall;
}

} // namespace UVE::Physics
