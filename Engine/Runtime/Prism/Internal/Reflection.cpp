// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Property Reflection & Multi-Object Details Implementation.
#include "prism/Reflection.h"

namespace prism {

MultiObjectValue ClassReflection::readMulti(const std::string& propName, const std::vector<void*>& objects) const {
  MultiObjectValue result;
  if (objects.empty()) return result;

  const PropertyDescriptor* desc = find(propName);
  if (!desc || !desc->getter) return result;

  std::any firstVal = desc->getter(objects[0]);
  for (size_t i = 1; i < objects.size(); i++) {
    if (!objects[i]) continue;
    std::any otherVal = desc->getter(objects[i]);
    if (desc->equals && !desc->equals(firstVal, otherVal)) {
      result.isMixed = true;
      result.value = {};
      return result;
    }
  }

  result.isMixed = false;
  result.value = firstVal;
  return result;
}

bool ClassReflection::writeMulti(const std::string& propName, const std::vector<void*>& objects,
                                 const std::any& newValue, TransactionService* tx) const {
  if (objects.empty()) return false;

  const PropertyDescriptor* desc = find(propName);
  if (!desc || !desc->setter) return false;

  if (tx) {
    struct ObjOldVal {
      void* obj;
      std::any val;
    };
    std::vector<ObjOldVal> oldValues;
    oldValues.reserve(objects.size());
    for (void* obj : objects) {
      if (obj && desc->getter) {
        oldValues.push_back({obj, desc->getter(obj)});
      }
    }

    auto setter = desc->setter;
    tx->push("Modify " + propName,
      [setter, objects, newValue] {
        for (void* o : objects) {
          if (o) setter(o, newValue);
        }
      },
      [setter, oldValues] {
        for (const auto& item : oldValues) {
          if (item.obj) setter(item.obj, item.val);
        }
      });
  }

  for (void* obj : objects) {
    if (obj) desc->setter(obj, newValue);
  }

  return true;
}

ReflectionRegistry& ReflectionRegistry::get() {
  static ReflectionRegistry registry;
  return registry;
}

} // namespace prism
