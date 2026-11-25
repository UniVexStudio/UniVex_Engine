// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "entity.h"

namespace UVE::Editor {

/// One ordered editor-Inspector section. Both callbacks are invoked only on the editor main thread;
/// they capture no ownership and must route authored writes through Editor's validated command APIs.
struct InspectorDrawerEntry final {
    std::string id;
    std::function<bool(Scene::Entity)> isEligible;
    std::function<void(Scene::Entity)> draw;
};

/// An editor-local ordered collection of Inspector section callbacks. Registration is append-only and
/// rejects empty identifiers, duplicate identifiers, and incomplete entries so a future drawer cannot
/// silently replace or reorder existing Inspector behavior. DrawEligible() never mutates registry
/// membership; it invokes every eligible entry in successful registration order. The registry owns
/// callbacks only, not EngineServices, ECS components, UI toolkit state, or scene data.
///
/// Thread-safety: main-thread only, matching Editor and the ECS/UI services it composes.
class InspectorDrawerRegistry final {
public:
    InspectorDrawerRegistry() = default;

    InspectorDrawerRegistry(const InspectorDrawerRegistry&) = delete;
    InspectorDrawerRegistry& operator=(const InspectorDrawerRegistry&) = delete;

    /// Appends one complete unique entry. Returns false without mutation when `id` is empty or
    /// already registered, or when either callback is missing.
    [[nodiscard]] bool RegisterDrawer(InspectorDrawerEntry entry);

    /// Invokes each registered entry whose predicate accepts `entity`, in registration order.
    /// Callbacks receive exactly the supplied entity value. Entries registered from a callback are
    /// retained but intentionally deferred until a later DrawEligible() call. The registry performs
    /// no entity-lifetime or authoring-state checks itself; registered editor predicates and command
    /// methods own them.
    void DrawEligible(Scene::Entity entity) const;

    /// Draws only eligible sections whose stable identifiers contain `filter` (case-insensitive). Empty
    /// filters draw all eligible sections. This is presentation-only and never changes registry order.
    void DrawEligibleMatching(Scene::Entity entity, std::string_view filter) const;

    /// Returns copied stable identifiers for the drawers currently eligible for `entity`, in the
    /// same registration order used by DrawEligible(). This is presentation metadata only: it
    /// never invokes a drawer, exposes its callback, or transfers ECS/editor ownership.
    [[nodiscard]] std::vector<std::string> GetEligibleDrawerIds(Scene::Entity entity) const;

    /// Places a registered drawer under a class-chain heading. Consecutive eligible drawers sharing
    /// a heading are drawn under one banner, drawn by `SetGroupHeaderDrawer` just before the
    /// first of them. Returns false for an unknown drawer.
    [[nodiscard]] bool SetDrawerGroup(std::string_view id, std::string group);
    void SetGroupHeaderDrawer(std::function<void(const std::string&)> drawHeader);

    /// The headings the eligible drawers for `entity` are shown under, in order, each once per run.
    [[nodiscard]] std::vector<std::string> GetEligibleGroupHeaders(Scene::Entity entity) const;

    [[nodiscard]] std::size_t GetDrawerCount() const noexcept;
    [[nodiscard]] bool HasDrawer(std::string_view id) const noexcept;

private:
    std::vector<InspectorDrawerEntry> m_entries;
    /// Heading per entry, index for index with m_entries; empty for none.
    std::vector<std::string> m_groups;
    std::function<void(const std::string&)> m_drawGroupHeader;
};

} // namespace UVE::Editor
