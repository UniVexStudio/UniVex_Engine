// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The value operations every UVScript program runs on, interpreted or native, and the registry of
// native programs.

#include "blueprint_native.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "blueprint_program.h"

namespace UVE::Blueprint::Native {
namespace {

[[nodiscard]] bool IsNumber(const Value& v) noexcept {
    return std::holds_alternative<std::int64_t>(v) || std::holds_alternative<double>(v);
}

struct Registry final {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, ProgramTable> tables;
};

/// Function-local, so registrations from static objects in other files never run before it exists.
[[nodiscard]] Registry& GetRegistry() {
    static Registry registry;
    return registry;
}

/// Deep equality: two collections match when they are the same kind of collection with equal
/// items, however the items are stored. Numbers compare by value across int and float.
[[nodiscard]] bool DeepEqual(const Value& a, const Value& b) {
    const auto* ca = std::get_if<std::shared_ptr<const CollectionValue>>(&a);
    const auto* cb = std::get_if<std::shared_ptr<const CollectionValue>>(&b);
    if (ca != nullptr || cb != nullptr) {
        if (ca == nullptr || cb == nullptr || (*ca)->kind != (*cb)->kind || (*ca)->items.size() != (*cb)->items.size()) {
            return false;
        }
        // Maps compare by content: insertion order never matters to `==`.
        if ((*ca)->kind == CollectionValue::Kind::Map) {
            for (std::size_t i = 0U; i + 1U < (*ca)->items.size(); i += 2U) {
                bool found = false;
                for (std::size_t j = 0U; j + 1U < (*cb)->items.size(); j += 2U) {
                    if (DeepEqual((*ca)->items[i], (*cb)->items[j]) &&
                        DeepEqual((*ca)->items[i + 1U], (*cb)->items[j + 1U])) {
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
            if (!DeepEqual((*ca)->items[i], (*cb)->items[i])) {
                return false;
            }
        }
        return true;
    }
    if (IsNumber(a) && IsNumber(b)) {
        return AsDouble(a) == AsDouble(b);
    }
    // A handle to no object (id 0) reads as `none`, so a lookup that missed compares true.
    if (std::holds_alternative<std::monostate>(a) && std::holds_alternative<ObjectRef>(b)) {
        return std::get<ObjectRef>(b).id == 0U;
    }
    if (std::holds_alternative<std::monostate>(b) && std::holds_alternative<ObjectRef>(a)) {
        return std::get<ObjectRef>(a).id == 0U;
    }
    return a == b;
}

[[nodiscard]] std::string CollectionName(const CollectionValue::Kind kind) {
    switch (kind) {
        case CollectionValue::Kind::List: return "a list";
        case CollectionValue::Kind::Map: return "a map";
        case CollectionValue::Kind::Tuple: return "a tuple";
    }
    return "a collection";
}

/// Where `key` sits in a map's items (the key slot of its pair), or nothing.
[[nodiscard]] std::optional<std::size_t> FindKey(const CollectionValue& map, const Value& key) {
    for (std::size_t i = 0U; i + 1U < map.items.size(); i += 2U) {
        if (DeepEqual(map.items[i], key)) {
            return i;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::int64_t AsIndex(const Value& key, const char* what) {
    if (const auto* i = std::get_if<std::int64_t>(&key)) {
        return *i;
    }
    throw Error{std::string{"an index into "} + what + " is a whole number"};
}

} // namespace

void RegisterNativeProgram(const ProgramTable& table) {
    Registry& registry = GetRegistry();
    const std::scoped_lock lock(registry.mutex);
    registry.tables.insert_or_assign(table.fingerprint, table);
}

const ProgramTable* FindNativeProgram(const std::uint64_t fingerprint) noexcept {
    Registry& registry = GetRegistry();
    const std::scoped_lock lock(registry.mutex);
    const auto it = registry.tables.find(fingerprint);
    return it == registry.tables.end() ? nullptr : &it->second;
}

double AsDouble(const Value& value) {
    if (const auto* i = std::get_if<std::int64_t>(&value)) {
        return static_cast<double>(*i);
    }
    if (const auto* d = std::get_if<double>(&value)) {
        return *d;
    }
    throw Error{"expected a number"};
}

Vec3Value AsVec3(const Value& value) {
    if (const auto* vec = std::get_if<Vec3Value>(&value)) {
        return *vec;
    }
    throw Error{"expected a vec3"};
}

ObjectRef AsObjectRef(const Value& value) {
    if (const auto* ref = std::get_if<ObjectRef>(&value)) {
        return *ref;
    }
    throw Error{"expected an object"};
}

bool AsBool(const Value& value) {
    if (const auto* b = std::get_if<bool>(&value)) {
        return *b;
    }
    throw Error{"a value had an unexpected type"};
}

const std::string& AsString(const Value& value) {
    if (const auto* s = std::get_if<std::string>(&value)) {
        return *s;
    }
    throw Error{"a value had an unexpected type"};
}

const CollectionValue& AsCollection(const Value& value) {
    if (const auto* box = std::get_if<std::shared_ptr<const CollectionValue>>(&value)) {
        return **box;
    }
    throw Error{"expected a list, map or tuple"};
}

Value Arithmetic(const ArithmeticOp op, const Value& a, const Value& b) {
    const auto* ia = std::get_if<std::int64_t>(&a);
    const auto* ib = std::get_if<std::int64_t>(&b);
    if (ia != nullptr && ib != nullptr) {
        switch (op) {
            case ArithmeticOp::Add: return *ia + *ib;
            case ArithmeticOp::Sub: return *ia - *ib;
            case ArithmeticOp::Mul: return *ia * *ib;
            case ArithmeticOp::Mod:
                if (*ib == 0) {
                    throw Error{"'%' by zero"};
                }
                return *ia % *ib;
            case ArithmeticOp::Div:
                if (*ib == 0) {
                    throw Error{"division by zero"};
                }
                return static_cast<double>(*ia) / static_cast<double>(*ib);
        }
    }
    if (IsNumber(a) && IsNumber(b)) {
        const double x = AsDouble(a);
        const double y = AsDouble(b);
        switch (op) {
            case ArithmeticOp::Add: return x + y;
            case ArithmeticOp::Sub: return x - y;
            case ArithmeticOp::Mul: return x * y;
            case ArithmeticOp::Div:
                if (y == 0.0) {
                    throw Error{"division by zero"};
                }
                return x / y;
            case ArithmeticOp::Mod:
                if (y == 0.0) {
                    throw Error{"'%' by zero"};
                }
                return std::fmod(x, y);
        }
    }
    if (const auto* sa = std::get_if<std::string>(&a); sa != nullptr && op == ArithmeticOp::Add) {
        return *sa + AsString(b);
    }
    if (const auto* la = std::get_if<std::shared_ptr<const CollectionValue>>(&a);
        la != nullptr && (*la)->kind == CollectionValue::Kind::List && op == ArithmeticOp::Add) {
        const CollectionValue& other = AsCollection(b);
        if (other.kind != CollectionValue::Kind::List) {
            throw Error{"a list only joins with another list"};
        }
        std::vector<Value> items = (*la)->items;
        items.insert(items.end(), other.items.begin(), other.items.end());
        return MakeListValue(std::move(items));
    }
    if (std::holds_alternative<Vec3Value>(a) && std::holds_alternative<Vec3Value>(b)) {
        const Vec3Value u = AsVec3(a);
        const Vec3Value v = AsVec3(b);
        return op == ArithmeticOp::Add ? Vec3Value{u.x + v.x, u.y + v.y, u.z + v.z}
                                          : Vec3Value{u.x - v.x, u.y - v.y, u.z - v.z};
    }
    if (std::holds_alternative<Vec3Value>(a)) {
        const Vec3Value u = AsVec3(a);
        const double s = AsDouble(b);
        if (op == ArithmeticOp::Div && s == 0.0) {
            throw Error{"division by zero"};
        }
        return op == ArithmeticOp::Mul ? Vec3Value{u.x * s, u.y * s, u.z * s}
                                          : Vec3Value{u.x / s, u.y / s, u.z / s};
    }
    const double s = AsDouble(a);
    const Vec3Value v = AsVec3(b);
    return Vec3Value{v.x * s, v.y * s, v.z * s};
}

Value Negate(Value value) {
    if (auto* i = std::get_if<std::int64_t>(&value)) {
        *i = -*i;
    } else if (auto* d = std::get_if<double>(&value)) {
        *d = -*d;
    } else if (auto* vec = std::get_if<Vec3Value>(&value)) {
        *vec = {-vec->x, -vec->y, -vec->z};
    }
    return value;
}

bool Equal(const Value& a, const Value& b) {
    return DeepEqual(a, b);
}

bool Compare(const CompareOp op, const Value& a, const Value& b) {
    const double x = AsDouble(a);
    const double y = AsDouble(b);
    switch (op) {
        case CompareOp::Lt: return x < y;
        case CompareOp::Le: return x <= y;
        case CompareOp::Gt: return x > y;
        case CompareOp::Ge: return x >= y;
    }
    return false;
}

Value GetComponent(const Value& vector, const int component) {
    const Vec3Value v = AsVec3(vector);
    return component == 0 ? v.x : component == 1 ? v.y : v.z;
}

Value SetComponent(const Value& vector, const int component, const Value& value) {
    Vec3Value v = AsVec3(vector);
    (component == 0 ? v.x : component == 1 ? v.y : v.z) = AsDouble(value);
    return v;
}

Value BuildList(std::vector<Value>& stack, const std::size_t count) {
    std::vector<Value> items(stack.end() - static_cast<std::ptrdiff_t>(count), stack.end());
    stack.resize(stack.size() - count);
    return MakeListValue(std::move(items));
}

Value BuildMap(std::vector<Value>& stack, const std::size_t pairs) {
    std::vector<Value> items;
    items.reserve(pairs * 2U);
    for (std::size_t i = 0U; i < pairs; ++i) {
        const Value& key = stack[stack.size() - pairs * 2U + i * 2U];
        const Value& value = stack[stack.size() - pairs * 2U + i * 2U + 1U];
        if (!std::holds_alternative<bool>(key) && !IsNumber(key) && !std::holds_alternative<std::string>(key)) {
            throw Error{"map keys are int, str or bool"};
        }
        bool replaced = false;
        for (std::size_t slot = 0U; slot + 1U < items.size(); slot += 2U) {
            if (DeepEqual(items[slot], key)) {
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
    return MakeMapValue(std::move(items));
}

Value BuildTuple(std::vector<Value>& stack, const std::size_t count) {
    std::vector<Value> items(stack.end() - static_cast<std::ptrdiff_t>(count), stack.end());
    stack.resize(stack.size() - count);
    return MakeTupleValue(std::move(items));
}

Value GetIndex(const Value& container, const Value& key) {
    const CollectionValue& box = AsCollection(container);
    if (box.kind == CollectionValue::Kind::Map) {
        if (const std::optional<std::size_t> slot = FindKey(box, key)) {
            return box.items[*slot + 1U];
        }
        throw Error{"the map has no key '" + FormatValue(key) + "'"};
    }
    const char* what = box.kind == CollectionValue::Kind::List ? "a list" : "a tuple";
    const std::int64_t index = AsIndex(key, what);
    if (index < 0 || static_cast<std::size_t>(index) >= box.items.size()) {
        throw Error{"index " + std::to_string(index) + " is out of range for " + CollectionName(box.kind) +
                       " of " + std::to_string(box.items.size())};
    }
    return box.items[static_cast<std::size_t>(index)];
}

Value SetIndex(const Value& container, const Value& key, const Value& value) {
    const CollectionValue& box = AsCollection(container);
    if (box.kind == CollectionValue::Kind::Tuple) {
        throw Error{"a tuple cannot change - build a new one instead"};
    }
    if (box.kind == CollectionValue::Kind::Map) {
        if (!std::holds_alternative<bool>(key) && !IsNumber(key) && !std::holds_alternative<std::string>(key)) {
            throw Error{"map keys are int, str or bool"};
        }
        std::vector<Value> items = box.items;
        if (const std::optional<std::size_t> slot = FindKey(box, key)) {
            items[*slot + 1U] = value;
        } else {
            items.push_back(key);
            items.push_back(value);
        }
        return MakeMapValue(std::move(items));
    }
    const std::int64_t index = AsIndex(key, "a list");
    if (index < 0 || static_cast<std::size_t>(index) >= box.items.size()) {
        throw Error{"index " + std::to_string(index) + " is out of range for a list of " +
                       std::to_string(box.items.size())};
    }
    std::vector<Value> items = box.items;
    items[static_cast<std::size_t>(index)] = value;
    return MakeListValue(std::move(items));
}

void Unpack(std::vector<Value>& stack, const std::size_t count) {
    const CollectionValue& box = AsCollection(stack.back());
    if (box.kind != CollectionValue::Kind::Tuple) {
        throw Error{"only a tuple unpacks into names, not " + CollectionName(box.kind)};
    }
    if (box.items.size() != count) {
        throw Error{"this tuple holds " + std::to_string(box.items.size()) + ", not " + std::to_string(count)};
    }
    const std::vector<Value> items = box.items;
    stack.pop_back();
    stack.insert(stack.end(), items.begin(), items.end());
}

Value Concat(std::vector<Value>& stack, const std::size_t count) {
    std::string text;
    for (std::size_t i = stack.size() - count; i < stack.size(); ++i) {
        text += FormatValue(stack[i]);
    }
    stack.resize(stack.size() - count);
    return text;
}

Value CallBuiltin(UVScriptHost& host, const int builtin, const std::span<const Value> a) {
    const bool allInt = std::ranges::all_of(a, [](const Value& v) { return std::holds_alternative<std::int64_t>(v); });
    const auto integer = [&a](const std::size_t i) { return std::get<std::int64_t>(a[i]); };
    switch (static_cast<Builtin>(builtin)) {
        case Builtin::Print: host.Print(FormatValue(a[0])); return {};
        case Builtin::Str: return FormatValue(a[0]);
        case Builtin::Sqrt: {
            const double x = AsDouble(a[0]);
            if (x < 0.0) {
                throw Error{"sqrt of a negative number"};
            }
            return std::sqrt(x);
        }
        case Builtin::Sin: return std::sin(AsDouble(a[0]));
        case Builtin::Cos: return std::cos(AsDouble(a[0]));
        case Builtin::Floor: return std::floor(AsDouble(a[0]));
        case Builtin::Lerp: return AsDouble(a[0]) + (AsDouble(a[1]) - AsDouble(a[0])) * AsDouble(a[2]);
        case Builtin::Int: return static_cast<std::int64_t>(AsDouble(a[0]));
        case Builtin::Float: return AsDouble(a[0]);
        case Builtin::Abs:
            if (allInt) return std::abs(integer(0));
            return std::fabs(AsDouble(a[0]));
        case Builtin::Min:
            if (allInt) return std::min(integer(0), integer(1));
            return std::min(AsDouble(a[0]), AsDouble(a[1]));
        case Builtin::Max:
            if (allInt) return std::max(integer(0), integer(1));
            return std::max(AsDouble(a[0]), AsDouble(a[1]));
        case Builtin::Clamp:
            if (allInt) return std::clamp(integer(0), integer(1), std::max(integer(1), integer(2)));
            return std::clamp(AsDouble(a[0]), AsDouble(a[1]), std::max(AsDouble(a[1]), AsDouble(a[2])));
        case Builtin::Vec3: return Vec3Value{AsDouble(a[0]), AsDouble(a[1]), AsDouble(a[2])};
        case Builtin::Length: {
            if (const auto* box = std::get_if<std::shared_ptr<const CollectionValue>>(&a[0])) {
                const std::size_t count =
                    (*box)->kind == CollectionValue::Kind::Map ? (*box)->items.size() / 2U : (*box)->items.size();
                return static_cast<std::int64_t>(count);
            }
            if (const auto* text = std::get_if<std::string>(&a[0])) {
                return static_cast<std::int64_t>(text->size());
            }
            const Vec3Value v = AsVec3(a[0]);
            return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        }
        case Builtin::Normalize: {
            const Vec3Value v = AsVec3(a[0]);
            const double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            return length == 0.0 ? v : Vec3Value{v.x / length, v.y / length, v.z / length};
        }
        case Builtin::Push: {
            const CollectionValue& box = AsCollection(a[0]);
            if (box.kind != CollectionValue::Kind::List) {
                throw Error{"push adds to a list, not " + CollectionName(box.kind)};
            }
            std::vector<Value> items = box.items;
            items.push_back(a[1]);
            return MakeListValue(std::move(items));
        }
        case Builtin::Keys: {
            const CollectionValue& box = AsCollection(a[0]);
            if (box.kind != CollectionValue::Kind::Map) {
                throw Error{"keys reads a map, not " + CollectionName(box.kind)};
            }
            std::vector<Value> keys;
            for (std::size_t i = 0U; i + 1U < box.items.size(); i += 2U) {
                keys.push_back(box.items[i]);
            }
            return MakeListValue(std::move(keys));
        }
        case Builtin::Contains: {
            if (const auto* text = std::get_if<std::string>(&a[0])) {
                return text->find(AsString(a[1])) != std::string::npos;
            }
            const CollectionValue& box = AsCollection(a[0]);
            if (box.kind == CollectionValue::Kind::Map) {
                return FindKey(box, a[1]).has_value();
            }
            return std::ranges::any_of(box.items, [&a](const Value& item) { return DeepEqual(item, a[1]); });
        }
        case Builtin::Remove: {
            const CollectionValue& box = AsCollection(a[0]);
            if (box.kind == CollectionValue::Kind::Tuple) {
                throw Error{"a tuple cannot shrink - build a new one instead"};
            }
            if (box.kind == CollectionValue::Kind::Map) {
                const std::optional<std::size_t> slot = FindKey(box, a[1]);
                if (!slot.has_value()) {
                    throw Error{"the map has no key '" + FormatValue(a[1]) + "'"};
                }
                std::vector<Value> items = box.items;
                items.erase(items.begin() + static_cast<std::ptrdiff_t>(*slot),
                            items.begin() + static_cast<std::ptrdiff_t>(*slot + 2U));
                return MakeMapValue(std::move(items));
            }
            const std::int64_t index = AsIndex(a[1], "a list");
            if (index < 0 || static_cast<std::size_t>(index) >= box.items.size()) {
                throw Error{"index " + std::to_string(index) + " is out of range for a list of " +
                               std::to_string(box.items.size())};
            }
            std::vector<Value> items = box.items;
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
            return MakeListValue(std::move(items));
        }
    }
    return {};
}

} // namespace UVE::Blueprint::Native
