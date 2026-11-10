// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Reactive & Dynamic Attribute<T>.
// Holds either a constant value or a dynamic getter callback (polled lazily).
#pragma once
#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace prism {

template <typename T>
class Attribute {
public:
  using ValueType = T;
  using GetterType = std::function<T()>;

  // Default constructor: default value
  Attribute() : m_value(T{}), m_isBound(false) {}

  // Construct from literal / value
  Attribute(T value) : m_value(std::move(value)), m_isBound(false) {}

  // Construct from C string literal (for Attribute<std::string>)
  template <typename U>
    requires std::is_same_v<T, std::string> && std::is_convertible_v<U, std::string_view>
  Attribute(U str) : m_value(std::string(str)), m_isBound(false) {}

  // Construct from invocable / lambda getter
  template <typename F>
    requires (!std::is_same_v<std::decay_t<F>, Attribute>) &&
             (!std::is_convertible_v<F, T>) &&
             std::is_invocable_r_v<T, F>
  Attribute(F&& getter) : m_getter(std::forward<F>(getter)), m_isBound(true) {}

  // Create bound attribute helper
  static Attribute create(GetterType getter) {
    Attribute attr;
    attr.m_getter = std::move(getter);
    attr.m_isBound = true;
    return attr;
  }

  // Value assignment
  Attribute& operator=(T value) {
    m_value = std::move(value);
    m_getter = nullptr;
    m_isBound = false;
    return *this;
  }

  // Getter assignment
  Attribute& operator=(GetterType getter) {
    m_getter = std::move(getter);
    m_isBound = static_cast<bool>(m_getter);
    return *this;
  }

  // Evaluates and returns the current value
  T get() const {
    if (m_isBound && m_getter) {
      return m_getter();
    }
    return m_value;
  }

  // Implicit conversion to T
  operator T() const {
    return get();
  }

  // True if bound to a dynamic getter
  bool isBound() const {
    return m_isBound;
  }

  // Set constant value
  void set(T value) {
    m_value = std::move(value);
    m_getter = nullptr;
    m_isBound = false;
  }

  // Bind dynamic getter
  void bind(GetterType getter) {
    m_getter = std::move(getter);
    m_isBound = static_cast<bool>(m_getter);
  }

private:
  T m_value{};
  GetterType m_getter{};
  bool m_isBound = false;
};

// Helper macro/function for binding attributes: attr = bindAttr([] { return ...; });
template <typename F>
auto bindAttr(F&& fn) {
  using ReturnType = std::invoke_result_t<F>;
  return Attribute<ReturnType>::create(std::forward<F>(fn));
}

} // namespace prism
