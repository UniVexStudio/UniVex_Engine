// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The value operations every UVScript program runs on, interpreted or native, and the registry of
// native programs.

#include "uve/uvscript/uvscript_native_uve.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "uvscript_program_uve.h"

namespace UVE::UVScript::Native {
namespace {

[[nodiscard]] bool IsNumberUVE(const ValueUVE& v) noexcept {
    return std::holds_alternative<std::int64_t>(v) || std::holds_alternative<double>(v);
}

struct RegistryUVE final {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, ProgramTableUVE> tables;
};

/// Function-local, so registrations from static objects in other files never run before it exists.
[[nodiscard]] RegistryUVE& GetRegistryUVE() {
    static RegistryUVE registry;
    return registry;
}

/// Deep equality: two collections match when they are the same kind of collection with equal
/// items, however the items are stored. Numbers compare by value across int and float.
[[nodiscard]] bool DeepEqualUVE(const ValueUVE& a, const ValueUVE& b) {
    const auto* ca = std::get_if<std::shared_ptr<const CollectionValueUVE>>(&a);
    const auto* cb = std::get_if<std::shared_ptr<const CollectionValueUVE>>(&b);
    if (ca != nullptr || cb != nullptr) {
        if (ca == nullptr || cb == nullptr || (*ca)->kind != (*cb)->kind || (*ca)->items.size() != (*cb)->items.size()) {
            return false;
        }
        // Maps compare by content: insertion order never matters to `==`.
        if ((*ca)->kind == CollectionValueUVE::KindUVE::Map) {
            for (std::size_t i = 0U; i + 1U < (*ca)->items.size(); i += 2U) {
                bool found = false;
                for (std::size_t j = 0U; j + 1U < (*cb)->items.size(); j += 2U) {
                    if (DeepEqualUVE((*ca)->items[i], (*cb)->items[j]) &&
                        DeepEqualUVE((*ca)->items[i + 1U], (*cb)->items[j + 1U])) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return false;
                }
            }
            return true;
        }
        for (std::size_t i = 0U; i < (*ca)->items.size(); ++i) {
            if (!DeepEqualUVE((*ca)->items[i], (*cb)->items[i])) {
                return false;
            }
        }
        return true;
    }
    if (IsNumberUVE(a) && IsNumberUVE(b)) {
        return AsDoubleUVE(a) == AsDoubleUVE(b);
    }
    // A handle to no object (id 0) reads as `none`, so a lookup that missed compares true.
    if (std::holds_alternative<std::monostate>(a) && std::holds_alternative<ObjectRefUVE>(b)) {
        return std::get<ObjectRefUVE>(b).id == 0U;
    }
    if (std::holds_alternative<std::monostate>(b) && std::holds_alternative<ObjectRefUVE>(a)) {
        return std::get<ObjectRefUVE>(a).id == 0U;
    }
    return a == b;
}

[[nodiscard]] std::string CollectionNameUVE(const CollectionValueUVE::KindUVE kind) {
    switch (kind) {
        case CollectionValueUVE::KindUVE::List: return "a list";
        case CollectionValueUVE::KindUVE::Map: return "a map";
        case CollectionValueUVE::KindUVE::Tuple: return "a tuple";
    }
    return "a collection";
}

/// Where `key` sits in a map's items (the key slot of its pair), or nothing.
[[nodiscard]] std::optional<std::size_t> FindKeyUVE(const CollectionValueUVE& map, const ValueUVE& key) {
    for (std::size_t i = 0U; i + 1U < map.items.size(); i += 2U) {
        if (DeepEqualUVE(map.items[i], key)) {
            return i;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::int64_t AsIndexUVE(const ValueUVE& key, const char* what) {
    if (const auto* i = std::get_if<std::int64_t>(&key)) {
        return *i;
    }
    throw ErrorUVE{std::string{"an index into "} + what + " is a whole number"};
}

} // namespace

void RegisterNativeProgramUVE(const ProgramTableUVE& table) {
    RegistryUVE& registry = GetRegistryUVE();
    const std::scoped_lock lock(registry.mutex);
    registry.tables.insert_or_assign(table.fingerprint, table);
}

const ProgramTableUVE* FindNativeProgramUVE(const std::uint64_t fingerprint) noexcept {
    RegistryUVE& registry = GetRegistryUVE();
    const std::scoped_lock lock(registry.mutex);
    const auto it = registry.tables.find(fingerprint);
    return it == registry.tables.end() ? nullptr : &it->second;
}

double AsDoubleUVE(const ValueUVE& value) {
    if (const auto* i = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*i);
    }
    if (const auto* d = std::get_if<double>(&value)) {
        return *d;
    }
    throw ErrorUVE{"expected a number"};
}

Vec3ValueUVE AsVec3UVE(const ValueUVE& value) {
    if (const auto* vec = std::get_if<Vec3ValueUVE>(&value)) {
        return *vec;
    }
    throw ErrorUVE{"expected a vec3"};
}

ObjectRefUVE AsObjectRefUVE(const ValueUVE& value) {
    if (const auto* ref = std::get_if<ObjectRefUVE>(&value)) {
        return *ref;
    }
    throw ErrorUVE{"expected an object"};
}

bool AsBoolUVE(const ValueUVE& value) {
    if (const auto* b = std::get_if<bool>(&value)) {
        return *b;
    }
    throw ErrorUVE{"a value had an unexpected type"};
}

const std::string& AsStringUVE(const ValueUVE& value) {
    if (const auto* s = std::get_if<std::string>(&value)) {
        return *s;
    }
    throw ErrorUVE{"a value had an unexpected type"};
}

const CollectionValueUVE& AsCollectionUVE(const ValueUVE& value) {
    if (const auto* box = std::get_if<std::shared_ptr<const CollectionValueUVE>>(&value)) {
        return **box;
    }
    throw ErrorUVE{"expected a list, map or tuple"};
}

ValueUVE ArithmeticUVE(const ArithmeticOpUVE op, const ValueUVE& a, const ValueUVE& b) {
    const auto* ia = std::get_if<std::int64_t>(&a);
    const auto* ib = std::get_if<std::int64_t>(&b);
    if (ia != nullptr && ib != nullptr) {
        switch (op) {
            case ArithmeticOpUVE::Add: return *ia + *ib;
            case ArithmeticOpUVE::Sub: return *ia - *ib;
            case ArithmeticOpUVE::Mul: return *ia * *ib;
            case ArithmeticOpUVE::Mod:
                if (*ib == 0) {
                    throw ErrorUVE{"'%' by zero"};
                }
                return *ia % *ib;
            case ArithmeticOpUVE::Div:
                if (*ib == 0) {
                    throw ErrorUVE{"division by zero"};
                }
                return static_cast<double>(*ia) / static_cast<double>(*ib);
        }
    }
    if (IsNumberUVE(a) && IsNumberUVE(b)) {
        const double x = AsDoubleUVE(a);
        const double y = AsDoubleUVE(b);
        switch (op) {
            case ArithmeticOpUVE::Add: return x + y;
            case ArithmeticOpUVE::Sub: return x - y;
            case ArithmeticOpUVE::Mul: return x * y;
            case ArithmeticOpUVE::Div:
                if (y == 0.0) {
                    throw ErrorUVE{"division by zero"};
                }
                return x / y;
            case ArithmeticOpUVE::Mod:
                if (y == 0.0) {
                    throw ErrorUVE{"'%' by zero"};
                }
                return std::fmod(x, y);
        }
    }
    if (const auto* sa = std::get_if<std::string>(&a); sa != nullptr && op == ArithmeticOpUVE::Add) {
        return *sa + AsStringUVE(b);
    }
    if (const auto* la = std::get_if<std::shared_ptr<const CollectionValueUVE>>(&a);
        la != nullptr && (*la)->kind == CollectionValueUVE::KindUVE::List && op == ArithmeticOpUVE::Add) {
        const CollectionValueUVE& other = AsCollectionUVE(b);
        if (other.kind != CollectionValueUVE::KindUVE::List) {
            throw ErrorUVE{"a list only joins with another list"};
        }
        std::vector<ValueUVE> items = (*la)->items;
        items.insert(items.end(), other.items.begin(), other.items.end());
        return MakeListValueUVE(std::move(items));
    }
    if (std::holds_alternative<Vec3ValueUVE>(a) && std::holds_alternative<Vec3ValueUVE>(b)) {
        const Vec3ValueUVE u = AsVec3UVE(a);
        const Vec3ValueUVE v = AsVec3UVE(b);
        return op == ArithmeticOpUVE::Add ? Vec3ValueUVE{u.x + v.x, u.y + v.y, u.z + v.z}
                                          : Vec3ValueUVE{u.x - v.x, u.y - v.y, u.z - v.z};
    }
    if (std::holds_alternative<Vec3ValueUVE>(a)) {
        const Vec3ValueUVE u = AsVec3UVE(a);
        const double s = AsDoubleUVE(b);
        if (op == ArithmeticOpUVE::Div && s == 0.0) {
            throw ErrorUVE{"division by zero"};
        }
        return op == ArithmeticOpUVE::Mul ? Vec3ValueUVE{u.x * s, u.y * s, u.z * s}
                                          : Vec3ValueUVE{u.x / s, u.y / s, u.z / s};
    }
    const double s = AsDoubleUVE(a);
    const Vec3ValueUVE v = AsVec3UVE(b);
    return Vec3ValueUVE{v.x * s, v.y * s, v.z * s};
}

ValueUVE NegateUVE(ValueUVE value) {
    if (auto* i = std::get_if<std::int64_t>(&value)) {
        *i = -*i;
    } else if (auto* d = std::get_if<double>(&value)) {
        *d = -*d;
    } else if (auto* vec = std::get_if<Vec3ValueUVE>(&value)) {
        *vec = {-vec->x, -vec->y, -vec->z};
    }
    return value;
}

bool EqualUVE(const ValueUVE& a, const ValueUVE& b) {
    return DeepEqualUVE(a, b);
}

bool CompareUVE(const CompareOpUVE op, const ValueUVE& a, const ValueUVE& b) {
    const double x = AsDoubleUVE(a);
    const double y = AsDoubleUVE(b);
    switch (op) {
        case CompareOpUVE::Lt: return x < y;
        case CompareOpUVE::Le: return x <= y;
        case CompareOpUVE::Gt: return x > y;
        case CompareOpUVE::Ge: return x >= y;
    }
    return false;
}

ValueUVE GetComponentUVE(const ValueUVE& vector, const int component) {
    const Vec3ValueUVE v = AsVec3UVE(vector);
    return component == 0 ? v.x : component == 1 ? v.y : v.z;
}

ValueUVE SetComponentUVE(const ValueUVE& vector, const int component, const ValueUVE& value) {
    Vec3ValueUVE v = AsVec3UVE(vector);
    (component == 0 ? v.x : component == 1 ? v.y : v.z) = AsDoubleUVE(value);
    return v;
}

ValueUVE BuildListUVE(std::vector<ValueUVE>& stack, const std::size_t count) {
    std::vector<ValueUVE> items(stack.end() - static_cast<std::ptrdiff_t>(count), stack.end());
    stack.resize(stack.size() - count);
    return MakeListValueUVE(std::move(items));
}

ValueUVE BuildMapUVE(std::vector<ValueUVE>& stack, const std::size_t pairs) {
    std::vector<ValueUVE> items;
    items.reserve(pairs * 2U);
    for (std::size_t i = 0U; i < pairs; ++i) {
        const ValueUVE& key = stack[stack.size() - pairs * 2U + i * 2U];
        const ValueUVE& value = stack[stack.size() - pairs * 2U + i * 2U + 1U];
        if (!std::holds_alternative<bool>(key) && !IsNumberUVE(key) && !std::holds_alternative<std::string>(key)) {
            throw ErrorUVE{"map keys are int, str or bool"};
        }
        bool replaced = false;
        for (std::size_t slot = 0U; slot + 1U < items.size(); slot += 2U) {
            if (DeepEqualUVE(items[slot], key)) {
                items[slot + 1U] = value;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            items.push_back(key);
            items.push_back(value);
        }
    }
    stack.resize(stack.size() - pairs * 2U);
    return MakeMapValueUVE(std::move(items));
}

ValueUVE BuildTupleUVE(std::vector<ValueUVE>& stack, const std::size_t count) {
    std::vector<ValueUVE> items(stack.end() - static_cast<std::ptrdiff_t>(count), stack.end());
    stack.resize(stack.size() - count);
    return MakeTupleValueUVE(std::move(items));
}

ValueUVE GetIndexUVE(const ValueUVE& container, const ValueUVE& key) {
    const CollectionValueUVE& box = AsCollectionUVE(container);
    if (box.kind == CollectionValueUVE::KindUVE::Map) {
        if (const std::optional<std::size_t> slot = FindKeyUVE(box, key)) {
            return box.items[*slot + 1U];
        }
        throw ErrorUVE{"the map has no key '" + FormatValueUVE(key) + "'"};
    }
    const char* what = box.kind == CollectionValueUVE::KindUVE::List ? "a list" : "a tuple";
    const std::int64_t index = AsIndexUVE(key, what);
    if (index < 0 || static_cast<std::size_t>(index) >= box.items.size()) {
        throw ErrorUVE{"index " + std::to_string(index) + " is out of range for " + CollectionNameUVE(box.kind) +
                       " of " + std::to_string(box.items.size())};
    }
    return box.items[static_cast<std::size_t>(index)];
}

ValueUVE SetIndexUVE(const ValueUVE& container, const ValueUVE& key, const ValueUVE& value) {
    const CollectionValueUVE& box = AsCollectionUVE(container);
    if (box.kind == CollectionValueUVE::KindUVE::Tuple) {
        throw ErrorUVE{"a tuple cannot change - build a new one instead"};
    }
    if (box.kind == CollectionValueUVE::KindUVE::Map) {
        if (!std::holds_alternative<bool>(key) && !IsNumberUVE(key) && !std::holds_alternative<std::string>(key)) {
            throw ErrorUVE{"map keys are int, str or bool"};
        }
        std::vector<ValueUVE> items = box.items;
        if (const std::optional<std::size_t> slot = FindKeyUVE(box, key)) {
            items[*slot + 1U] = value;
        } else {
            items.push_back(key);
            items.push_back(value);
        }
        return MakeMapValueUVE(std::move(items));
    }
    const std::int64_t index = AsIndexUVE(key, "a list");
    if (index < 0 || static_cast<std::size_t>(index) >= box.items.size()) {
        throw ErrorUVE{"index " + std::to_string(index) + " is out of range for a list of " +
                       std::to_string(box.items.size())};
    }
    std::vector<ValueUVE> items = box.items;
    items[static_cast<std::size_t>(index)] = value;
    return MakeListValueUVE(std::move(items));
}

void UnpackUVE(std::vector<ValueUVE>& stack, const std::size_t count) {
    const CollectionValueUVE& box = AsCollectionUVE(stack.back());
    if (box.kind != CollectionValueUVE::KindUVE::Tuple) {
        throw ErrorUVE{"only a tuple unpacks into names, not " + CollectionNameUVE(box.kind)};
    }
    if (box.items.size() != count) {
        throw ErrorUVE{"this tuple holds " + std::to_string(box.items.size()) + ", not " + std::to_string(count)};
    }
    const std::vector<ValueUVE> items = box.items;
    stack.pop_back();
    stack.insert(stack.end(), items.begin(), items.end());
}

ValueUVE ConcatUVE(std::vector<ValueUVE>& stack, const std::size_t count) {
    std::string text;
    for (std::size_t i = stack.size() - count; i < stack.size(); ++i) {
        text += FormatValueUVE(stack[i]);
    }
    stack.resize(stack.size() - count);
    return text;
}

ValueUVE CallBuiltinUVE(UVScriptHostUVE& host, const int builtin, const std::span<const ValueUVE> a) {
    const bool allInt = std::ranges::all_of(a, [](const ValueUVE& v) { return std::holds_alternative<std::int64_t>(v); });
    const auto integer = [&a](const std::size_t i) { return std::get<std::int64_t>(a[i]); };
    switch (static_cast<BuiltinUVE>(builtin)) {
        case BuiltinUVE::Print: host.PrintUVE(FormatValueUVE(a[0])); return {};
        case BuiltinUVE::Str: return FormatValueUVE(a[0]);
        case BuiltinUVE::Sqrt: {
            const double x = AsDoubleUVE(a[0]);
            if (x < 0.0) {
                throw ErrorUVE{"sqrt of a negative number"};
            }
            return std::sqrt(x);
        }
        case BuiltinUVE::Sin: return std::sin(AsDoubleUVE(a[0]));
        case BuiltinUVE::Cos: return std::cos(AsDoubleUVE(a[0]));
        case BuiltinUVE::Floor: return std::floor(AsDoubleUVE(a[0]));
        case BuiltinUVE::Lerp: return AsDoubleUVE(a[0]) + (AsDoubleUVE(a[1]) - AsDoubleUVE(a[0])) * AsDoubleUVE(a[2]);
        case BuiltinUVE::Int: return static_cast<std::int64_t>(AsDoubleUVE(a[0]));
        case BuiltinUVE::Float: return AsDoubleUVE(a[0]);
        case BuiltinUVE::Abs:
            if (allInt) return std::abs(integer(0));
            return std::fabs(AsDoubleUVE(a[0]));
        case BuiltinUVE::Min:
            if (allInt) return std::min(integer(0), integer(1));
            return std::min(AsDoubleUVE(a[0]), AsDoubleUVE(a[1]));
        case BuiltinUVE::Max:
            if (allInt) return std::max(integer(0), integer(1));
            return std::max(AsDoubleUVE(a[0]), AsDoubleUVE(a[1]));
        case BuiltinUVE::Clamp:
            if (allInt) return std::clamp(integer(0), integer(1), std::max(integer(1), integer(2)));
            return std::clamp(AsDoubleUVE(a[0]), AsDoubleUVE(a[1]), std::max(AsDoubleUVE(a[1]), AsDoubleUVE(a[2])));
        case BuiltinUVE::Vec3: return Vec3ValueUVE{AsDoubleUVE(a[0]), AsDoubleUVE(a[1]), AsDoubleUVE(a[2])};
        case BuiltinUVE::Length: {
            if (const auto* box = std::get_if<std::shared_ptr<const CollectionValueUVE>>(&a[0])) {
                const std::size_t count =
                    (*box)->kind == CollectionValueUVE::KindUVE::Map ? (*box)->items.size() / 2U : (*box)->items.size();
                return static_cast<std::int64_t>(count);
            }
            if (const auto* text = std::get_if<std::string>(&a[0])) {
                return static_cast<std::int64_t>(text->size());
            }
            const Vec3ValueUVE v = AsVec3UVE(a[0]);
            return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        }
        case BuiltinUVE::Normalize: {
            const Vec3ValueUVE v = AsVec3UVE(a[0]);
            const double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            return length == 0.0 ? v : Vec3ValueUVE{v.x / length, v.y / length, v.z / length};
        }
        case BuiltinUVE::Push: {
            const CollectionValueUVE& box = AsCollectionUVE(a[0]);
            if (box.kind != CollectionValueUVE::KindUVE::List) {
                throw ErrorUVE{"push adds to a list, not " + CollectionNameUVE(box.kind)};
            }
            std::vector<ValueUVE> items = box.items;
            items.push_back(a[1]);
            return MakeListValueUVE(std::move(items));
        }
        case BuiltinUVE::Keys: {
            const CollectionValueUVE& box = AsCollectionUVE(a[0]);
            if (box.kind != CollectionValueUVE::KindUVE::Map) {
                throw ErrorUVE{"keys reads a map, not " + CollectionNameUVE(box.kind)};
            }
            std::vector<ValueUVE> keys;
            for (std::size_t i = 0U; i + 1U < box.items.size(); i += 2U) {
                keys.push_back(box.items[i]);
            }
            return MakeListValueUVE(std::move(keys));
        }
        case BuiltinUVE::Contains: {
            if (const auto* text = std::get_if<std::string>(&a[0])) {
                return text->find(AsStringUVE(a[1])) != std::string::npos;
            }
            const CollectionValueUVE& box = AsCollectionUVE(a[0]);
            if (box.kind == CollectionValueUVE::KindUVE::Map) {
                return FindKeyUVE(box, a[1]).has_value();
            }
            return std::ranges::any_of(box.items, [&a](const ValueUVE& item) { return DeepEqualUVE(item, a[1]); });
        }
        case BuiltinUVE::Remove: {
            const CollectionValueUVE& box = AsCollectionUVE(a[0]);
            if (box.kind == CollectionValueUVE::KindUVE::Tuple) {
                throw ErrorUVE{"a tuple cannot shrink - build a new one instead"};
            }
            if (box.kind == CollectionValueUVE::KindUVE::Map) {
                const std::optional<std::size_t> slot = FindKeyUVE(box, a[1]);
                if (!slot.has_value()) {
                    throw ErrorUVE{"the map has no key '" + FormatValueUVE(a[1]) + "'"};
                }
                std::vector<ValueUVE> items = box.items;
                items.erase(items.begin() + static_cast<std::ptrdiff_t>(*slot),
                            items.begin() + static_cast<std::ptrdiff_t>(*slot + 2U));
                return MakeMapValueUVE(std::move(items));
            }
            const std::int64_t index = AsIndexUVE(a[1], "a list");
            if (index < 0 || static_cast<std::size_t>(index) >= box.items.size()) {
                throw ErrorUVE{"index " + std::to_string(index) + " is out of range for a list of " +
                               std::to_string(box.items.size())};
            }
            std::vector<ValueUVE> items = box.items;
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
            return MakeListValueUVE(std::move(items));
        }
    }
    return {};
}

} // namespace UVE::UVScript::Native
