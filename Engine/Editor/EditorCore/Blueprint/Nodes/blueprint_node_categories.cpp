// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_node_categories.h"

#include <algorithm>
#include <cctype>

namespace UVE::Blueprint::Nodes {
namespace {

[[nodiscard]] std::string ToLowerCopy(const std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

[[nodiscard]] bool ContainsCaseInsensitive(const std::string_view haystack, const std::string& needleLower) {
    if (needleLower.empty()) {
        return true;
    }
    const std::string hayLower = ToLowerCopy(haystack);
    return hayLower.find(needleLower) != std::string::npos;
}

} // namespace

std::string_view BlueprintNodeDomainName(const BlueprintNodeDomain domain) noexcept {
    using D = BlueprintNodeDomain;
    switch (domain) {
        case D::Event: return "Event";
        case D::Flow: return "Flow";
        case D::Logic: return "Logic";
        case D::Math: return "Math";
        case D::Variable: return "Variable";
        case D::Function: return "Function";
        case D::Input: return "Input";
        case D::Transform: return "Transform";
        case D::Physics: return "Physics";
        case D::Collision: return "Collision";
        case D::Animation: return "Animation";
        case D::Rendering: return "Rendering";
        case D::Material: return "Material";
        case D::Camera: return "Camera";
        case D::Audio: return "Audio";
        case D::Particle: return "Particle";
        case D::AI: return "AI";
        case D::Navigation: return "Navigation";
        case D::Gameplay: return "Gameplay";
        case D::Timeline: return "Timeline";
        case D::Data: return "Data";
        case D::String: return "String";
        case D::Collection: return "Collection";
        case D::Networking: return "Networking";
        case D::UserInterface: return "UserInterface";
        case D::Debug: return "Debug";
        case D::Utility: return "Utility";
    }
    return "Utility";
}

BlueprintNodeDomain ClassifyBlueprintNodeDomain(const Asset::BlueprintNodeType& node) noexcept {
    using D = BlueprintNodeDomain;
    const std::string_view c = node.category;
    const std::string_view k = node.key;

    if (c.starts_with("Networking")) return D::Networking;
    if (c.starts_with("User Interface")) return D::UserInterface;
    if (c == "Events") {
        if (k.find("collision") != std::string_view::npos || k.find("overlap") != std::string_view::npos) {
            return D::Collision;
        }
        return D::Event;
    }
    if (c == "Flow Control") return D::Flow;
    if (c == "Variables") return D::Variable;
    if (c == "Math|Boolean" || k.starts_with("bool_") || k.starts_with("not_") || k.starts_with("and_") ||
        k.starts_with("or_")) {
        return D::Logic;
    }
    if (c.starts_with("Math")) return D::Math;
    if (c.starts_with("Input")) return D::Input;
    if (c.starts_with("Transform")) return D::Transform;
    if (c.starts_with("Physics|Trace") || k.find("trace") != std::string_view::npos ||
        k.find("overlap") != std::string_view::npos) {
        return D::Collision;
    }
    if (c.starts_with("Physics") || c == "Character") return D::Physics;
    if (c.starts_with("Animation")) return D::Animation;
    if (c == "Rendering|Material") return D::Material;
    if (c == "Rendering|Camera" || k.find("camera") != std::string_view::npos ||
        k.find("field_of_view") != std::string_view::npos) {
        return D::Camera;
    }
    if (c.starts_with("Rendering")) return D::Rendering;
    if (c.starts_with("Audio")) return D::Audio;
    if (c.starts_with("Effects") || k.find("particle") != std::string_view::npos) return D::Particle;
    if (c == "AI" || c.starts_with("AI|")) {
        if (k.find("move_to") != std::string_view::npos || k.find("nav") != std::string_view::npos) {
            return D::Navigation;
        }
        return D::AI;
    }
    if (c.starts_with("Gameplay") || c == "World" || c == "Game" || c.starts_with("Game|") || c == "Objects") {
        return D::Gameplay;
    }
    if (c.starts_with("Save") || c.starts_with("Data")) return D::Data;
    if (c.starts_with("String") || c.starts_with("Text")) return D::String;
    if (c.starts_with("Array") || c.starts_with("Collection")) return D::Collection;
    if (k.find("timer") != std::string_view::npos || k.find("delay") != std::string_view::npos) return D::Timeline;
    if (k == "print" || k.starts_with("debug_") || c.starts_with("Debug")) return D::Debug;
    if (c.starts_with("Function")) return D::Function;
    return D::Utility;
}

std::vector<const Asset::BlueprintNodeType*> GetBlueprintNodesByDomain(const BlueprintNodeDomain domain) {
    BlueprintNodeQuery query;
    query.domain = domain;
    return SearchBlueprintNodes(query);
}

std::vector<const Asset::BlueprintNodeType*> SearchBlueprintNodes(const BlueprintNodeQuery& query) {
    const std::string needle = ToLowerCopy(query.searchText);
    std::vector<const Asset::BlueprintNodeType*> matches;
    for (const Asset::BlueprintNodeType& node : Asset::GetBlueprintNodeTypes()) {
        if (query.domain.has_value() && ClassifyBlueprintNodeDomain(node) != *query.domain) {
            continue;
        }
        if (query.graphKind.has_value() && !Asset::IsBlueprintNodeForGraph(node, *query.graphKind)) {
            continue;
        }
        if (query.availableComponents.has_value() && !node.needs.empty() &&
            !query.availableComponents->contains(node.needs)) {
            continue;
        }
        if (!needle.empty()) {
            const bool hit = ContainsCaseInsensitive(node.title, needle) ||
                             ContainsCaseInsensitive(node.key, needle) ||
                             ContainsCaseInsensitive(node.category, needle) ||
                             ContainsCaseInsensitive(node.keywords, needle) ||
                             ContainsCaseInsensitive(node.tooltip, needle);
            if (!hit) {
                continue;
            }
        }
        matches.push_back(&node);
    }
    return matches;
}

std::map<BlueprintNodeDomain, std::size_t> GetBlueprintNodeDomainCounts() {
    std::map<BlueprintNodeDomain, std::size_t> counts;
    for (const Asset::BlueprintNodeType& node : Asset::GetBlueprintNodeTypes()) {
        ++counts[ClassifyBlueprintNodeDomain(node)];
    }
    return counts;
}

} // namespace UVE::Blueprint::Nodes
