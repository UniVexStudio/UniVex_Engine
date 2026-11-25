// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "inspector_drawer_registry.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace UVE::Editor {

namespace {

[[nodiscard]] bool ContainsCaseInsensitive(const std::string_view text,
                                               const std::string_view query) noexcept {
    if (query.empty()) {
        return true;
    }
    const auto equalsCaseInsensitive = [](const char lhs, const char rhs) noexcept {
        return std::tolower(static_cast<unsigned char>(lhs)) ==
               std::tolower(static_cast<unsigned char>(rhs));
    };
    return std::search(text.begin(), text.end(), query.begin(), query.end(), equalsCaseInsensitive) != text.end();
}

} // namespace

bool InspectorDrawerRegistry::RegisterDrawer(InspectorDrawerEntry entry) {
    if (entry.id.empty() || !entry.isEligible || !entry.draw || HasDrawer(entry.id)) {
        return false;
    }
    m_entries.push_back(std::move(entry));
    m_groups.emplace_back();
    return true;
}

void InspectorDrawerRegistry::DrawEligible(const Scene::Entity entity) const {
    DrawEligibleMatching(entity, {});
}

void InspectorDrawerRegistry::DrawEligibleMatching(const Scene::Entity entity,
                                                           const std::string_view filter) const {
    const std::size_t drawerCount = m_entries.size();
    // Copied, not referenced: a drawer may register another drawer while it runs.
    std::string currentGroup;
    for (std::size_t index = 0U; index < drawerCount; ++index) {
        const InspectorDrawerEntry& entry = m_entries[index];
        if (entry.isEligible(entity) && ContainsCaseInsensitive(entry.id, filter)) {
            const std::string group = m_groups[index];
            if (!group.empty() && group != currentGroup && m_drawGroupHeader) {
                m_drawGroupHeader(group);
            }
            currentGroup = group;
            entry.draw(entity);
        }
    }
}

std::vector<std::string> InspectorDrawerRegistry::GetEligibleDrawerIds(const Scene::Entity entity) const {
    std::vector<std::string> identifiers;
    identifiers.reserve(m_entries.size());
    for (const InspectorDrawerEntry& entry : m_entries) {
        if (entry.isEligible(entity)) {
            identifiers.push_back(entry.id);
        }
    }
    return identifiers;
}

bool InspectorDrawerRegistry::SetDrawerGroup(const std::string_view id, std::string group) {
    for (std::size_t index = 0U; index < m_entries.size(); ++index) {
        if (m_entries[index].id == id) {
            m_groups[index] = std::move(group);
            return true;
        }
    }
    return false;
}

void InspectorDrawerRegistry::SetGroupHeaderDrawer(std::function<void(const std::string&)> drawHeader) {
    m_drawGroupHeader = std::move(drawHeader);
}

std::vector<std::string> InspectorDrawerRegistry::GetEligibleGroupHeaders(const Scene::Entity entity) const {
    std::vector<std::string> headers;
    std::string currentGroup;
    for (std::size_t index = 0U; index < m_entries.size(); ++index) {
        if (!m_entries[index].isEligible(entity)) {
            continue;
        }
        const std::string& group = m_groups[index];
        if (!group.empty() && group != currentGroup) {
            headers.push_back(group);
        }
        currentGroup = group;
    }
    return headers;
}

std::size_t InspectorDrawerRegistry::GetDrawerCount() const noexcept {
    return m_entries.size();
}

bool InspectorDrawerRegistry::HasDrawer(const std::string_view id) const noexcept {
    return std::any_of(m_entries.cbegin(), m_entries.cend(), [id](const InspectorDrawerEntry& entry) {
        return entry.id == id;
    });
}

} // namespace UVE::Editor
