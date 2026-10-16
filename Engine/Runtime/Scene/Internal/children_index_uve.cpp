// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/children_index_uve.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "uve/component/hierarchy_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

void ChildrenIndexUVE::RebuildUVE(IEntityManagerUVE& entityManager) {
    m_children.clear();
    // Order first, group second: one vector of (parent, order, entity) sorted globally keeps the
    // per-parent lists sorted without a sort per parent.
    std::vector<std::pair<EntityUVE, std::pair<std::int64_t, EntityUVE>>> links;
    entityManager.ForEachUVE<HierarchyComponentUVE>(
        [&links](const EntityUVE entity, const HierarchyComponentUVE& hierarchy) {
            links.emplace_back(hierarchy.parent, std::make_pair(hierarchy.siblingOrder, entity));
        });
    // Same tie-break as SceneGraphUVE::GetChildrenUVE (sibling order, then entity index, then
    // generation), with the parent as the outermost key so each parent's run is contiguous.
    std::sort(links.begin(), links.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) {
            return CompareEntityHandleOrderUVE(a.first, b.first);
        }
        return CompareSiblingHierarchyOrderUVE(a.second.first, a.second.second, b.second.first,
                                               b.second.second);
    });
    for (const auto& [parent, ordered] : links) {
        m_children[parent].push_back(ordered.second);
    }
}

std::span<const EntityUVE> ChildrenIndexUVE::GetChildrenUVE(const EntityUVE parent) const noexcept {
    const auto found = m_children.find(parent);
    if (found == m_children.end()) {
        return {};
    }
    return std::span<const EntityUVE>{found->second};
}

bool ChildrenIndexUVE::IsEmptyUVE() const noexcept {
    return m_children.empty();
}

void ChildrenIndexUVE::ClearUVE() {
    m_children.clear();
}

} // namespace UVE::Scene
