// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "blueprint_nodes.h"

namespace UVE::Blueprint::Nodes {

/// Canonical node domains matching the Blueprint/Nodes architecture.
enum class BlueprintNodeDomainUVE : std::uint8_t {
    Event = 0,
    Flow,
    Logic,
    Math,
    Variable,
    Function,
    Input,
    Transform,
    Physics,
    Collision,
    Animation,
    Rendering,
    Material,
    Camera,
    Audio,
    Particle,
    AI,
    Navigation,
    Gameplay,
    Timeline,
    Data,
    String,
    Collection,
    Networking,
    UserInterface,
    Debug,
    Utility,
};

[[nodiscard]] std::string_view BlueprintNodeDomainNameUVE(BlueprintNodeDomainUVE domain) noexcept;

/// Classifies any registered Blueprint node type into its canonical BlueprintNodeDomainUVE.
[[nodiscard]] BlueprintNodeDomainUVE ClassifyBlueprintNodeDomainUVE(const Asset::BlueprintNodeTypeUVE& node) noexcept;

/// Returns all built-in Blueprint catalog nodes that belong to `domain`.
[[nodiscard]] std::vector<const Asset::BlueprintNodeTypeUVE*> GetBlueprintNodesByDomainUVE(
    BlueprintNodeDomainUVE domain);

/// Query filter for palette/search lookups across the Blueprint node catalog.
struct BlueprintNodeQueryUVE final {
    std::string searchText;
    std::optional<BlueprintNodeDomainUVE> domain;
    std::optional<Asset::BlueprintGraphKindUVE> graphKind;
    /// When set, nodes requiring a component not present in this set are excluded.
    std::optional<std::set<std::string>> availableComponents;
};

/// Searches the built-in Blueprint node catalog with optional domain, graph kind, component, and text filters.
[[nodiscard]] std::vector<const Asset::BlueprintNodeTypeUVE*> SearchBlueprintNodesUVE(
    const BlueprintNodeQueryUVE& query);

/// Returns the number of built-in nodes per domain across the entire catalog.
[[nodiscard]] std::map<BlueprintNodeDomainUVE, std::size_t> GetBlueprintNodeDomainCountsUVE();

} // namespace UVE::Blueprint::Nodes
