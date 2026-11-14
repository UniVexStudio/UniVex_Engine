// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Animation, Parametric Curves, Springs, and Harmonic Oscillators.
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>

namespace prism {

enum class CurveEase : uint8_t {
  Linear,
  QuadIn,
  QuadOut,
  QuadInOut,
  CubicIn,
  CubicOut,
  CubicInOut,
  ExpoOut,
  BounceOut,
  ElasticOut,
};

inline float evaluateCurve(float t, CurveEase ease = CurveEase::QuadOut) {
  t = std::clamp(t, 0.0f, 1.0f);
  switch (ease) {
  case CurveEase::Linear:
    return t;
  case CurveEase::QuadIn:
    return t * t;
  case CurveEase::QuadOut:
    return t * (2.0f - t);
  case CurveEase::QuadInOut:
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
  case CurveEase::CubicIn:
    return t * t * t;
  case CurveEase::CubicOut: {
    float f = t - 1.0f;
    return f * f * f + 1.0f;
  }
  case CurveEase::CubicInOut:
    return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
  case CurveEase::ExpoOut:
    return (t >= 1.0f) ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
  case CurveEase::BounceOut: {
    if (t < (1.0f / 2.75f)) {
      return 7.5625f * t * t;
    } else if (t < (2.0f / 2.75f)) {
      float f = t - (1.5f / 2.75f);
      return 7.5625f * f * f + 0.75f;
    } else if (t < (2.5f / 2.75f)) {
      float f = t - (2.25f / 2.75f);
      return 7.5625f * f * f + 0.9375f;
    } else {
      float f = t - (2.625f / 2.75f);
      return 7.5625f * f * f + 0.984375f;
    }
  }
  case CurveEase::ElasticOut: {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    constexpr float p = 0.3f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t - p / 4.0f) * (2.0f * 3.14159265f) / p) + 1.0f;
  }
  }
  return t;
}

// Manages playback of an animation timeline
class CurveSequence {
public:
  CurveSequence() = default;
  CurveSequence(float durationSec, CurveEase ease = CurveEase::QuadOut, bool loop = false)
      : m_duration(std::max(0.001f, durationSec)), m_ease(ease), m_loop(loop) {}

  void play() { m_playing = true; m_reverse = false; }
  void reverse() { m_playing = true; m_reverse = true; }
  void pause() { m_playing = false; }
  void jumpToEnd() { m_time = m_duration; m_playing = false; }
  void jumpToStart() { m_time = 0.0f; m_playing = false; }

  bool tick(float dt) {
    if (!m_playing) return false;
    if (m_reverse) {
      m_time -= dt;
      if (m_time <= 0.0f) {
        if (m_loop) {
          m_time += m_duration;
        } else {
          m_time = 0.0f;
          m_playing = false;
        }
      }
    } else {
      m_time += dt;
      if (m_time >= m_duration) {
        if (m_loop) {
          m_time -= m_duration;
        } else {
          m_time = m_duration;
          m_playing = false;
        }
      }
    }
    return true;
  }

  float progress() const { return std::clamp(m_time / m_duration, 0.0f, 1.0f); }
  float value() const { return evaluateCurve(progress(), m_ease); }
  bool isPlaying() const { return m_playing; }
  bool isAtEnd() const { return m_time >= m_duration; }
  bool isAtStart() const { return m_time <= 0.0f; }

  void setDuration(float d) { m_duration = std::max(0.001f, d); }
  void setEase(CurveEase e) { m_ease = e; }
  void setLoop(bool l) { m_loop = l; }

private:
  float m_duration = 0.25f;
  float m_time = 0.0f;
  CurveEase m_ease = CurveEase::QuadOut;
  bool m_playing = false;
  bool m_reverse = false;
  bool m_loop = false;
};

// Damped spring simulation (ideal for fluid AAA UI physics)
class SpringSimulation {
public:
  SpringSimulation(float stiffness = 200.0f, float damping = 20.0f, float mass = 1.0f)
      : m_stiffness(stiffness), m_damping(damping), m_mass(mass) {}

  void reset(float initialPos = 0.0f) {
    m_pos = initialPos;
    m_target = initialPos;
    m_vel = 0.0f;
  }

  void setTarget(float target) {
    m_target = target;
  }

  bool update(float dt) {
    if (dt <= 0.0f) return false;
    // Sub-step for numerical stability
    int steps = std::clamp(int(dt / 0.005f) + 1, 1, 8);
    float subDt = dt / float(steps);

    for (int i = 0; i < steps; i++) {
      float displacement = m_pos - m_target;
      float springForce = -m_stiffness * displacement;
      float dampingForce = -m_damping * m_vel;
      float acceleration = (springForce + dampingForce) / m_mass;
      m_vel += acceleration * subDt;
      m_pos += m_vel * subDt;
    }

    // Settling check
    if (std::abs(m_pos - m_target) < 1e-4f && std::abs(m_vel) < 1e-3f) {
      m_pos = m_target;
      m_vel = 0.0f;
      return false; // Settled
    }
    return true; // Still active
  }

  float position() const { return m_pos; }
  float velocity() const { return m_vel; }
  float target() const { return m_target; }
  bool isSettled() const { return m_pos == m_target && m_vel == 0.0f; }

private:
  float m_stiffness = 200.0f;
  float m_damping = 20.0f;
  float m_mass = 1.0f;
  float m_pos = 0.0f;
  float m_target = 0.0f;
  float m_vel = 0.0f;
};

} // namespace prism
