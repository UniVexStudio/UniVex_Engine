// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <span>
#include <unordered_map>
#include <vector>

#include "entity.h"

namespace UVE::Scene {

class IEntityManager;

/// A derived parent→children lookup built from `HierarchyComponent` links: one O(n) rebuild,
/// then O(1) child-list queries until the hierarchy changes again.
///
/// WHY THIS EXISTS NEXT TO GetChildren. The scene graph deliberately records the relationship
/// once (the parent link) and `SceneGraph::GetChildren` scans for it, which is always
/// correct and costs O(n) per call. A caller asking for many parents' children against one
/// hierarchy state — the outliner walking every expanded node, a bulk save, a sibling-order pass
/// — pays that scan per parent. The index pays it once per rebuild and serves every query from
/// the map. Same answers (same sibling ordering), opposite cost shape.
///
/// FRESHNESS IS THE CALLER'S. The index snapshots the hierarchy at `Rebuild` and does not
/// observe the manager: a reparent, a hierarchy add/remove or a destroyed entity after the
/// rebuild is invisible until the next one. `SceneGraph` owns one and exposes
/// `RebuildChildrenIndex` for callers that want the graph to hold it; nothing rebuilds
/// implicitly, because an O(n) pass hidden inside a query is exactly the cost this type exists
/// to make explicit.
///
/// Thread-safety: not thread-safe — rebuild and query only from the scene thread, like the
/// manager it reads.
class ChildrenIndex final {
public:
    ChildrenIndex() = default;

    /// Rebuilds the whole map from the manager's current hierarchy links, discarding whatever
    /// the previous rebuild recorded. Each parent's list is sorted exactly as
    /// `SceneGraph::GetChildren` sorts: sibling order first, entity index and generation
    /// breaking ties, so indexed answers are identical to scanned ones.
    void Rebuild(IEntityManager& entityManager);

    /// The children of `parent` as of the last rebuild, in sibling order. A parent with no
    /// children — or one the index never saw — yields an empty span. The span borrows the
    /// index: it dies with the next rebuild, clear, or index destruction.
    [[nodiscard]] std::span<const Entity> GetChildren(Entity parent) const noexcept;

    /// True when no rebuild has recorded anything (fresh index, or cleared).
    [[nodiscard]] bool IsEmpty() const noexcept;

    /// Drops every recorded list. The next query answers empty until a rebuild.
    void Clear();

private:
    std::unordered_map<Entity, std::vector<Entity>> m_children;
};

} // namespace UVE::Scene
