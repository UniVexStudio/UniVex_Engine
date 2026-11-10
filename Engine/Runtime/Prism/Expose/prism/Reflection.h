// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Property Reflection & Multi-Object Details System.
// Enables type-safe property registration, multi-object editing, and automatic UI binding.
#pragma once
#include "Core.h"
#include "Transaction.h"
#include <any>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace prism {

enum class PropertyType {
  Float,
  Int,
  Bool,
  String,
  Color,
  Vec3,
  Enum
};

struct ReflectedVec3 {
  float x = 0.0f, y = 0.0f, z = 0.0f;
  bool operator==(const ReflectedVec3& o) const {
    return x == o.x && y == o.y && z == o.z;
  }
};

struct PropertyDescriptor {
  std::string name;
  std::string category = "General";
  std::string tooltip;
  PropertyType type = PropertyType::Float;

  float minVal = 0.0f;
  float maxVal = 100.0f;
  float step = 1.0f;
  int decimals = 2;
  std::string suffix;
  std::vector<std::string> enumOptions;

  std::function<std::any(const void*)> getter;
  std::function<void(void*, const std::any&)> setter;
  std::function<bool(const std::any&, const std::any&)> equals;
};

struct MultiObjectValue {
  bool isMixed = false;
  std::any value;

  template <typename T>
  T get(T fallback = T{}) const {
    if (isMixed || !value.has_value()) return fallback;
    try {
      return std::any_cast<T>(value);
    } catch (...) {
      return fallback;
    }
  }
};

class ClassReflection {
public:
  explicit ClassReflection(std::string className) : m_className(std::move(className)) {}

  const std::string& className() const { return m_className; }

  ClassReflection& addProperty(PropertyDescriptor desc) {
    m_properties.push_back(std::move(desc));
    return *this;
  }

  // Float Property Helper
  template <typename Class>
  ClassReflection& addFloat(std::string name, std::string category, float Class::* member,
                            float minV = 0.0f, float maxV = 1.0f, float step = 0.01f, std::string tip = "") {
    PropertyDescriptor d;
    d.name = std::move(name);
    d.category = std::move(category);
    d.tooltip = std::move(tip);
    d.type = PropertyType::Float;
    d.minVal = minV;
    d.maxVal = maxV;
    d.step = step;
    d.getter = [member](const void* obj) -> std::any {
      return static_cast<const Class*>(obj)->*member;
    };
    d.setter = [member](void* obj, const std::any& val) {
      static_cast<Class*>(obj)->*member = std::any_cast<float>(val);
    };
    d.equals = [](const std::any& a, const std::any& b) {
      return std::any_cast<float>(a) == std::any_cast<float>(b);
    };
    return addProperty(std::move(d));
  }

  // Bool Property Helper
  template <typename Class>
  ClassReflection& addBool(std::string name, std::string category, bool Class::* member, std::string tip = "") {
    PropertyDescriptor d;
    d.name = std::move(name);
    d.category = std::move(category);
    d.tooltip = std::move(tip);
    d.type = PropertyType::Bool;
    d.getter = [member](const void* obj) -> std::any {
      return static_cast<const Class*>(obj)->*member;
    };
    d.setter = [member](void* obj, const std::any& val) {
      static_cast<Class*>(obj)->*member = std::any_cast<bool>(val);
    };
    d.equals = [](const std::any& a, const std::any& b) {
      return std::any_cast<bool>(a) == std::any_cast<bool>(b);
    };
    return addProperty(std::move(d));
  }

  // String Property Helper
  template <typename Class>
  ClassReflection& addString(std::string name, std::string category, std::string Class::* member, std::string tip = "") {
    PropertyDescriptor d;
    d.name = std::move(name);
    d.category = std::move(category);
    d.tooltip = std::move(tip);
    d.type = PropertyType::String;
    d.getter = [member](const void* obj) -> std::any {
      return static_cast<const Class*>(obj)->*member;
    };
    d.setter = [member](void* obj, const std::any& val) {
      static_cast<Class*>(obj)->*member = std::any_cast<std::string>(val);
    };
    d.equals = [](const std::any& a, const std::any& b) {
      return std::any_cast<std::string>(a) == std::any_cast<std::string>(b);
    };
    return addProperty(std::move(d));
  }

  // Color Property Helper
  template <typename Class>
  ClassReflection& addColor(std::string name, std::string category, Color Class::* member, std::string tip = "") {
    PropertyDescriptor d;
    d.name = std::move(name);
    d.category = std::move(category);
    d.tooltip = std::move(tip);
    d.type = PropertyType::Color;
    d.getter = [member](const void* obj) -> std::any {
      return static_cast<const Class*>(obj)->*member;
    };
    d.setter = [member](void* obj, const std::any& val) {
      static_cast<Class*>(obj)->*member = std::any_cast<Color>(val);
    };
    d.equals = [](const std::any& a, const std::any& b) {
      return std::any_cast<Color>(a) == std::any_cast<Color>(b);
    };
    return addProperty(std::move(d));
  }

  const std::vector<PropertyDescriptor>& properties() const { return m_properties; }

  const PropertyDescriptor* find(const std::string& name) const {
    for (const auto& p : m_properties) {
      if (p.name == name) return &p;
    }
    return nullptr;
  }

  // Aggregate property across multiple selected objects
  MultiObjectValue readMulti(const std::string& propName, const std::vector<void*>& objects) const;

  // Apply property value to multiple selected objects with optional atomic Undo recording
  bool writeMulti(const std::string& propName, const std::vector<void*>& objects,
                  const std::any& newValue, TransactionService* tx = nullptr) const;

private:
  std::string m_className;
  std::vector<PropertyDescriptor> m_properties;
};

// Global Reflection Registry
class ReflectionRegistry {
public:
  static ReflectionRegistry& get();

  ClassReflection& registerClass(std::string name) {
    auto [it, inserted] = m_classes.emplace(name, ClassReflection(name));
    return it->second;
  }

  ClassReflection* findClass(const std::string& name) {
    auto it = m_classes.find(name);
    return it != m_classes.end() ? &it->second : nullptr;
  }

private:
  std::unordered_map<std::string, ClassReflection> m_classes;
};

} // namespace prism
