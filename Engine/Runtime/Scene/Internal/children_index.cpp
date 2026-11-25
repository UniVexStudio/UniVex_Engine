// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "children_index.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "hierarchy_component.h"
#include "i_entity_manager.h"

namespace UVE::Scene {

void ChildrenIndex::Rebuild(IEntityManager& entityManager) {
    m_children.clear();
    // Order first, group second: one vector of (parent, order, entity) sorted globally keeps the
    // per-parent lists sorted without a sort per parent.
    std::vector<std::pair<Entity, std::pair<std::int64_t, Entity>>> links;
    entityManager.ForEach<HierarchyComponent>(
        [&links](const Entity entity, const HierarchyComponent& hierarchy) {
            links.emplace_back(hierarchy.parent, std::make_pair(hierarchy.siblingOrder, entity));
        });
    // Same tie-break as SceneGraph::GetChildren (sibling order, then entity index, then
    // generation), with the parent as the outermost key so each parent's run is contiguous.
    std::sort(links.begin(), links.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) {
            return CompareEntityHandleOrder(a.first, b.first);
        }
        return CompareSiblingHierarchyOrder(a.second.first, a.second.second, b.second.first,
                                               b.second.second);
    });
    for (const auto& [parent, ordered] : links) {
        m_children[parent].push_back(ordered.second);
    }
}

std::span<const Entity> ChildrenIndex::GetChildren(const Entity parent) const noexcept {
    const auto found = m_children.find(parent);
    if (found == m_children.end()) {
        return {};
    }
    return std::span<const Entity>{found->second};
}

bool ChildrenIndex::IsEmpty() const noexcept {
    return m_children.empty();
}

void ChildrenIndex::Clear() {
    m_children.clear();
}

} // namespace UVE::Scene
