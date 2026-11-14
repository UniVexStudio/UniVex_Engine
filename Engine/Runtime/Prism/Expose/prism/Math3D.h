// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - minimal 3D math for viewport tools (gizmos). Row-major 4x4, column vectors: p' = M * p.
// Hosts with their own math types convert at the boundary (Vec3 is 3 floats, Mat4 is 16 floats, row-major).
#pragma once
#include "Core.h"
#include <cmath>

namespace prism {

struct Vec3 {
  float x = 0, y = 0, z = 0;
  Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
  float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
  float dot(Vec3 o) const { return x * o.x + y * o.y + z * o.z; }
  Vec3 cross(Vec3 o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
  float length() const { return std::sqrt(dot(*this)); }
  Vec3 normalized() const { float l = length(); return l > 1e-8f ? *this * (1.f / l) : Vec3{0, 1, 0}; }
};

struct Mat4 {
  float m[16];
  static Mat4 identity();
  static Mat4 translation(Vec3 t);
  static Mat4 scaling(Vec3 s);
  static Mat4 rotationDeg(Vec3 eulerDeg); // Rz * Ry * Rx
  Mat4 operator*(const Mat4& o) const;
  Vec3 point(Vec3 p) const;
  Vec3 dir(Vec3 d) const;
  Mat4 inverse() const;
};

} // namespace prism
