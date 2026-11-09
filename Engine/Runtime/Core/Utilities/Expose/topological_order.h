// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace UVE::Utilities {
enum class TopologicalOrderCodeUVE : std::uint8_t { Ordered = 0, UnknownDependency, Cycle };
/// Index-based dependency planning shared by frame scheduling and render graphs. Edges name
/// prerequisites, not dependents. Earliest inserted ready node wins; duplicate edges are
/// idempotent. Empty graphs are valid. Invalid input leaves output untouched and invokes no work.
[[nodiscard]] inline TopologicalOrderCodeUVE
BuildTopologicalOrderUVE(std::span<const std::vector<std::uint32_t>> dependencies,
                         std::vector<std::uint32_t> &outOrder) {
    if (dependencies.size() > std::numeric_limits<std::uint32_t>::max())
        return TopologicalOrderCodeUVE::UnknownDependency;
    for (const auto &edges : dependencies)
        for (auto edge : edges)
            if (edge >= dependencies.size())
                return TopologicalOrderCodeUVE::UnknownDependency;
    std::vector<std::uint32_t> order;
    order.reserve(dependencies.size());
    std::vector<bool> completed(dependencies.size(), false);
    while (order.size() < dependencies.size()) {
        bool progressed = false;
        for (std::size_t index = 0U; index < dependencies.size(); ++index) {
            if (completed[index])
                continue;
            if (std::all_of(dependencies[index].begin(), dependencies[index].end(),
                            [&](auto prerequisite) { return completed[prerequisite]; })) {
                completed[index] = true;
                order.push_back(static_cast<std::uint32_t>(index));
                progressed = true;
                break;
            }
        }
        if (!progressed)
            return TopologicalOrderCodeUVE::Cycle;
    }
    outOrder = std::move(order);
    return TopologicalOrderCodeUVE::Ordered;
}
} // namespace UVE::Utilities
