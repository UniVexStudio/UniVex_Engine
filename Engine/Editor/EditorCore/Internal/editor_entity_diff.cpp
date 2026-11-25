// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_entity_diff.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

using Json = nlohmann::json;

/// One object of a payload: its tree path and its components by display type name.
struct Object final {
    std::string path;
    std::map<std::string, Json> components;
};

/// "ColliderComponent" -> "Collider"; other names as they are.
[[nodiscard]] std::string DisplayType(const std::string& type) {
    static constexpr std::string_view kSuffix = "Component";
    if (type.size() > kSuffix.size() && type.compare(type.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0) {
        return type.substr(0, type.size() - kSuffix.size());
    }
    return type;
}

/// Components that describe the tree or the editor, not the object: the tree is compared
/// through the paths; an instance tag is stripped when the entity is opened.
[[nodiscard]] bool IsStructural(const std::string& type) {
    return type == "HierarchyComponent" || type == "PrefabInstanceComponent" ||
           type == "EditorInternalEntityComponent";
}

[[nodiscard]] std::string ValueText(const Json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_float()) {
        char buffer[48];
        std::snprintf(buffer, sizeof buffer, "%.4g", value.get<double>());
        return buffer;
    }
    return value.dump();
}

/// The payload's objects in tree order (parents before children, siblings in file order).
[[nodiscard]] std::vector<Object> Objects(std::string_view text) {
    std::vector<Object> out;
    const Json doc = Json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("entities") || !doc["entities"].is_array()) {
        return out;
    }
    struct Node final {
        std::int64_t localId = -1;
        std::int64_t parent = -1;
        std::string name;
        const Json* components = nullptr;
    };
    std::vector<Node> nodes;
    for (const Json& entity : doc["entities"]) {
        if (!entity.is_object()) {
            continue;
        }
        Node node;
        node.localId = entity.value("localId", static_cast<std::int64_t>(-1));
        if (entity.contains("components") && entity["components"].is_object()) {
            node.components = &entity["components"];
            const Json& c = *node.components;
            if (c.contains("HierarchyComponent") && c["HierarchyComponent"].is_object()) {
                node.parent = c["HierarchyComponent"].value("parentLocalId", static_cast<std::int64_t>(-1));
            }
            if (c.contains("NameComponent") && c["NameComponent"].is_object()) {
                node.name = c["NameComponent"].value("name", std::string());
            }
        }
        if (node.name.empty()) {
            node.name = "Object";
        }
        nodes.push_back(std::move(node));
    }
    // Children by parent, in file order; walked from the roots so the order is the tree's.
    std::map<std::int64_t, std::vector<std::size_t>> children;
    std::vector<std::size_t> roots;
    std::set<std::int64_t> ids;
    for (const Node& node : nodes) {
        ids.insert(node.localId);
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].parent >= 0 && ids.count(nodes[i].parent) > 0 && nodes[i].parent != nodes[i].localId) {
            children[nodes[i].parent].push_back(i);
        } else {
            roots.push_back(i);
        }
    }
    std::set<std::size_t> visited;
    const auto walk = [&](const auto& self, const std::vector<std::size_t>& level, const std::string& prefix,
                          const bool isRootLevel) -> void {
        std::map<std::string, int> seen;
        for (const std::size_t index : level) {
            if (!visited.insert(index).second) {
                continue;
            }
            const Node& node = nodes[index];
            // The root is the entity itself: its name follows the file, so it is matched as the root.
            std::string name = isRootLevel ? std::string() : node.name;
            const int count = ++seen[name];
            if (count > 1) {
                name += " #" + std::to_string(count);
            }
            Object object;
            object.path = prefix.empty() ? (name.empty() ? std::string("(root)") : name)
                                         : prefix + "/" + name;
            if (isRootLevel && count > 1) {
                object.path = "(root)" + name;
            }
            if (node.components != nullptr) {
                for (auto it = node.components->begin(); it != node.components->end(); ++it) {
                    if (!IsStructural(it.key())) {
                        object.components.emplace(DisplayType(it.key()), it.value());
                    }
                }
            }
            const std::string childPrefix = object.path;
            out.push_back(std::move(object));
            const auto found = children.find(node.localId);
            if (found != children.end()) {
                self(self, found->second, childPrefix, false);
            }
        }
    };
    walk(walk, roots, std::string(), true);
    return out;
}

void DiffComponent(const std::string& object, const std::string& type, const Json& before, const Json& after,
                      std::vector<EntityDiffRow>& rows, const bool isRoot) {
    if (!before.is_object() || !after.is_object()) {
        if (before != after) {
            rows.push_back(EntityDiffRow{EntityDiffRow::Kind::Changed, object, type, ValueText(before),
                                            ValueText(after)});
        }
        return;
    }
    std::set<std::string> keys;
    for (auto it = before.begin(); it != before.end(); ++it) {
        keys.insert(it.key());
    }
    for (auto it = after.begin(); it != after.end(); ++it) {
        keys.insert(it.key());
    }
    for (const std::string& key : keys) {
        if (isRoot && type == "Name" && key == "name") {
            continue;
        }
        const bool inBefore = before.contains(key);
        const bool inAfter = after.contains(key);
        if (inBefore && inAfter && before[key] == after[key]) {
            continue;
        }
        rows.push_back(EntityDiffRow{EntityDiffRow::Kind::Changed, object, type + "." + key,
                                        inBefore ? ValueText(before[key]) : std::string("(none)"),
                                        inAfter ? ValueText(after[key]) : std::string("(none)")});
    }
}

} // namespace

std::vector<EntityDiffRow> DiffEntityPayloads(const std::string_view savedJson, const std::string_view currentJson) {
    std::vector<EntityDiffRow> rows;
    const std::vector<Object> saved = Objects(savedJson);
    const std::vector<Object> current = Objects(currentJson);
    if (saved.empty() && current.empty()) {
        return rows;
    }
    std::map<std::string, const Object*> savedByPath;
    for (const Object& object : saved) {
        savedByPath.emplace(object.path, &object);
    }
    std::set<std::string> matched;
    for (const Object& object : current) {
        const auto found = savedByPath.find(object.path);
        if (found == savedByPath.end()) {
            rows.push_back(EntityDiffRow{EntityDiffRow::Kind::Added, object.path, {}, {}, {}});
            continue;
        }
        matched.insert(object.path);
        const Object& before = *found->second;
        const bool isRoot = object.path == "(root)";
        for (const auto& [type, value] : object.components) {
            const auto was = before.components.find(type);
            if (was == before.components.end()) {
                rows.push_back(EntityDiffRow{EntityDiffRow::Kind::Added, object.path, type, {}, {}});
            } else {
                DiffComponent(object.path, type, was->second, value, rows, isRoot);
            }
        }
        for (const auto& [type, value] : before.components) {
            if (object.components.count(type) == 0) {
                rows.push_back(EntityDiffRow{EntityDiffRow::Kind::Removed, object.path, type, {}, {}});
            }
        }
    }
    for (const Object& object : saved) {
        if (matched.count(object.path) == 0) {
            rows.push_back(EntityDiffRow{EntityDiffRow::Kind::Removed, object.path, {}, {}, {}});
        }
    }
    return rows;
}

} // namespace UVE::Editor
