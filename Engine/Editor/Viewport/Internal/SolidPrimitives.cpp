// Internal/SolidPrimitives.cpp - see univex/gizmo/SolidPrimitives.h.
#include "univex/gizmo/SolidPrimitives.h"

#include <array>
#include <cmath>
#include <numbers>

namespace univex::gizmo {
namespace {

using univex::math::Cross;
using univex::math::Dot;
using univex::math::Length;
using univex::math::Normalize;

constexpr float kTwoPi = 2.f * std::numbers::pi_v<float>;

void BasisUVE(const Vec3& axis, Vec3& u, Vec3& v) {
    const Vec3 helper = std::fabs(axis.y) < 0.98f ? Vec3{0.f, 1.f, 0.f} : Vec3{1.f, 0.f, 0.f};
    u = Normalize(Cross(helper, axis));
    v = Normalize(Cross(axis, u));
}

/// Adds one lit triangle unless its outward normal (the side `outward` points to) faces away.
void PushUVE(GizmoMesh& mesh, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& outward, const Vec3& color,
             float alpha, const std::optional<Vec3>& cullToward) {
    if (cullToward.has_value() && Dot(outward, *cullToward) > 0.f) {
        return; // faces away from the eye (cullToward points from the eye into the scene)
    }
    GizmoTriangle triangle{a, b, c, color, alpha};
    triangle.lit = 1.f;
    mesh.triangles.push_back(triangle);
}

} // namespace

void AddSolidCylinderUVE(GizmoMesh& mesh, const Vec3& a, const Vec3& b, float radius, int segments, const Vec3& color,
                         float alpha, std::optional<Vec3> cullToward) {
    const Vec3 along = b - a;
    if (Length(along) < 1e-6f || segments < 3) return;
    const Vec3 axis = Normalize(along);
    Vec3 u, v;
    BasisUVE(axis, u, v);
    for (int i = 0; i < segments; ++i) {
        const float t0 = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
        const float t1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(segments);
        const Vec3 d0 = u * std::cos(t0) + v * std::sin(t0);
        const Vec3 d1 = u * std::cos(t1) + v * std::sin(t1);
        const Vec3 outward = Normalize(d0 + d1);
        PushUVE(mesh, a + d0 * radius, b + d0 * radius, b + d1 * radius, outward, color, alpha, cullToward);
        PushUVE(mesh, a + d0 * radius, b + d1 * radius, a + d1 * radius, outward, color, alpha, cullToward);
    }
}

void AddSolidConeUVE(GizmoMesh& mesh, const Vec3& base, const Vec3& tip, float radius, int segments, const Vec3& color,
                     float alpha, std::optional<Vec3> cullToward) {
    const Vec3 along = tip - base;
    const float height = Length(along);
    if (height < 1e-6f || segments < 3) return;
    const Vec3 axis = along / height;
    Vec3 u, v;
    BasisUVE(axis, u, v);
    for (int i = 0; i < segments; ++i) {
        const float t0 = kTwoPi * static_cast<float>(i) / static_cast<float>(segments);
        const float t1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(segments);
        const Vec3 d0 = u * std::cos(t0) + v * std::sin(t0);
        const Vec3 d1 = u * std::cos(t1) + v * std::sin(t1);
        const Vec3 p0 = base + d0 * radius;
        const Vec3 p1 = base + d1 * radius;
        // The side's outward normal leans toward the tip by the cone's slope.
        const Vec3 side = Normalize(Normalize(d0 + d1) * height + axis * radius);
        PushUVE(mesh, p0, tip, p1, side, color, alpha, cullToward);
        PushUVE(mesh, base, p1, p0, axis * -1.f, color, alpha, cullToward);
    }
}

void AddSolidSphereUVE(GizmoMesh& mesh, const Vec3& center, float radius, int rings, int segments, const Vec3& color,
                       float alpha, std::optional<Vec3> cullToward) {
    if (rings < 2 || segments < 3) return;
    const auto point = [&](int ring, int segment) {
        const float polar = std::numbers::pi_v<float> * static_cast<float>(ring) / static_cast<float>(rings);
        const float azimuth = kTwoPi * static_cast<float>(segment) / static_cast<float>(segments);
        return Vec3{std::sin(polar) * std::cos(azimuth), std::cos(polar), std::sin(polar) * std::sin(azimuth)};
    };
    for (int ring = 0; ring < rings; ++ring) {
        for (int segment = 0; segment < segments; ++segment) {
            const Vec3 n00 = point(ring, segment);
            const Vec3 n01 = point(ring, segment + 1);
            const Vec3 n10 = point(ring + 1, segment);
            const Vec3 n11 = point(ring + 1, segment + 1);
            const Vec3 outward = Normalize(n00 + n01 + n10 + n11);
            if (ring > 0) {
                PushUVE(mesh, center + n00 * radius, center + n01 * radius, center + n11 * radius, outward, color, alpha,
                        cullToward);
            }
            if (ring + 1 < rings) {
                PushUVE(mesh, center + n00 * radius, center + n11 * radius, center + n10 * radius, outward, color, alpha,
                        cullToward);
            }
        }
    }
}

void AddSolidBoxUVE(GizmoMesh& mesh, const Vec3& center, float size, const Vec3& color, float alpha) {
    const float h = size * 0.5f;
    struct Face {
        Vec3 n, u, v;
    };
    const std::array<Face, 6> faces = {{
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {1, 0, 0}, {0, 1, 0}},
    }};
    for (const Face& f : faces) {
        const Vec3 c = center + f.n * h;
        const Vec3 p0 = c - f.u * h - f.v * h;
        const Vec3 p1 = c + f.u * h - f.v * h;
        const Vec3 p2 = c + f.u * h + f.v * h;
        const Vec3 p3 = c - f.u * h + f.v * h;
        PushUVE(mesh, p0, p1, p2, f.n, color, alpha, std::nullopt);
        PushUVE(mesh, p0, p2, p3, f.n, color, alpha, std::nullopt);
    }
}

} // namespace univex::gizmo
