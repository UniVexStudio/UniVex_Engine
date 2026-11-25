// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "resource_dependency_graph.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace UVE::Asset {
namespace {

[[nodiscard]] bool IsValidHandle(const ResourceDependencyHandle handle) noexcept {
    return handle.guid != kInvalidAssetGuid && handle.generation != 0U;
}

[[nodiscard]] bool ContainsHandle(const std::vector<ResourceDependencyHandle>& handles,
                                     const ResourceDependencyHandle handle) noexcept {
    return std::find(handles.begin(), handles.end(), handle) != handles.end();
}

} // namespace

ResourceDependencyResult ResourceDependencyGraph::RegisterResource(const ResourceDependencyHandle handle) {
    if (!IsValidHandle(handle)) {
        return {ResourceDependencyCode::InvalidHandle,
                "Resource registration requires a non-zero GUID and generation."};
    }
    if (FindExact(handle) != nullptr) {
        return {ResourceDependencyCode::DuplicateResource,
                "Resource registration rejected an already registered handle."};
    }
    if (FindGuid(handle.guid) != nullptr) {
        return {ResourceDependencyCode::StaleGeneration,
                "Resource registration rejected a second generation for the same GUID."};
    }
    if (m_entries.size() >= kMaximumResources) {
        return {ResourceDependencyCode::CapacityExceeded,
                "Resource dependency graph capacity has been reached."};
    }
    m_entries.push_back(Entry{handle, {}});
    BumpGeneration();
    return {ResourceDependencyCode::Registered, "Resource handle was registered."};
}

ResourceDependencyResult ResourceDependencyGraph::SetDependencies(
    const ResourceDependencyHandle handle, std::vector<ResourceDependencyHandle> dependencies) {
    if (!IsValidHandle(handle)) {
        return {ResourceDependencyCode::InvalidHandle,
                "Dependency updates require a non-zero GUID and generation."};
    }
    Entry* source = FindExact(handle);
    if (source == nullptr) {
        return FindGuid(handle.guid) == nullptr
            ? ResourceDependencyResult{ResourceDependencyCode::UnknownDependency,
                                          "Dependency update referenced an unknown resource."}
            : ResourceDependencyResult{ResourceDependencyCode::StaleGeneration,
                                          "Dependency update used a stale resource generation."};
    }
    if (dependencies.size() > kMaximumDependenciesPerResource) {
        return {ResourceDependencyCode::CapacityExceeded,
                "Resource dependency count exceeds the bounded per-resource limit."};
    }
    for (std::size_t index = 0U; index < dependencies.size(); ++index) {
        const ResourceDependencyHandle dependency = dependencies[index];
        if (!IsValidHandle(dependency) || dependency == handle) {
            return {ResourceDependencyCode::InvalidHandle,
                    "Dependency updates reject invalid or self-referencing handles."};
        }
        if (std::find(dependencies.begin(), dependencies.begin() + static_cast<std::ptrdiff_t>(index), dependency) !=
            dependencies.begin() + static_cast<std::ptrdiff_t>(index)) {
            return {ResourceDependencyCode::DuplicateResource,
                    "Dependency updates reject duplicate dependency handles."};
        }
        if (FindExact(dependency) == nullptr) {
            return FindGuid(dependency.guid) == nullptr
                ? ResourceDependencyResult{ResourceDependencyCode::UnknownDependency,
                                              "Dependency update referenced an unknown dependency."}
                : ResourceDependencyResult{ResourceDependencyCode::StaleGeneration,
                                              "Dependency update referenced a stale dependency generation."};
        }
        std::vector<ResourceDependencyHandle> visited;
        if (Reaches(dependency, handle, visited)) {
            return {ResourceDependencyCode::CycleDetected,
                    "Dependency update would introduce a resource dependency cycle."};
        }
    }
    if (source->dependencies == dependencies) {
        return {ResourceDependencyCode::Updated, "Resource dependencies were unchanged."};
    }
    source->dependencies = std::move(dependencies);
    BumpGeneration();
    return {ResourceDependencyCode::Updated, "Resource dependencies were updated."};
}

ResourceDependencyInvalidationPlan ResourceDependencyGraph::GetDependentClosure(
    const ResourceDependencyHandle root, const std::size_t maximumDependents) const {
    ResourceDependencyInvalidationPlan plan;
    plan.graphGeneration = m_graphGeneration;
    plan.root = root;
    if (!IsValidHandle(root)) {
        plan.code = ResourceDependencyCode::InvalidHandle;
        plan.message = "Dependent closure requires a non-zero GUID and generation.";
        return plan;
    }
    if (FindExact(root) == nullptr) {
        plan.code = FindGuid(root.guid) == nullptr ? ResourceDependencyCode::UnknownDependency
                                                        : ResourceDependencyCode::StaleGeneration;
        plan.message = plan.code == ResourceDependencyCode::UnknownDependency
                           ? "Dependent closure referenced an unknown resource."
                           : "Dependent closure used a stale resource generation.";
        return plan;
    }

    std::vector<ResourceDependencyHandle> frontier{root};
    std::vector<ResourceDependencyHandle> discovered;
    while (!frontier.empty()) {
        std::vector<ResourceDependencyHandle> nextFrontier;
        for (const ResourceDependencyHandle current : frontier) {
            for (const Entry& entry : m_entries) {
                if (!ContainsHandle(entry.dependencies, current) ||
                    ContainsHandle(discovered, entry.handle) || ContainsHandle(nextFrontier, entry.handle) ||
                    entry.handle == root) {
                    continue;
                }
                nextFrontier.push_back(entry.handle);
            }
        }
        std::sort(nextFrontier.begin(), nextFrontier.end(), [](const ResourceDependencyHandle left,
                                                                const ResourceDependencyHandle right) {
            if (left.guid.value != right.guid.value) {
                return left.guid.value < right.guid.value;
            }
            return left.generation < right.generation;
        });
        discovered.insert(discovered.end(), nextFrontier.begin(), nextFrontier.end());
        frontier = std::move(nextFrontier);
    }

    plan.dependentsTruncated = discovered.size() > maximumDependents;
    if (plan.dependentsTruncated) {
        discovered.resize(maximumDependents);
    }
    plan.dependents = std::move(discovered);
    plan.code = ResourceDependencyCode::DependentClosureReady;
    plan.message = plan.dependentsTruncated ? "Dependent closure was bounded and truncated."
                                             : "Dependent closure was captured.";
    return plan;
}

ResourceDependencyResult ResourceDependencyGraph::RemoveResource(const ResourceDependencyHandle handle) {
    Entry* entry = FindExact(handle);
    if (entry == nullptr) {
        return FindGuid(handle.guid) == nullptr
            ? ResourceDependencyResult{ResourceDependencyCode::UnknownDependency,
                                          "Resource removal referenced an unknown handle."}
            : ResourceDependencyResult{ResourceDependencyCode::StaleGeneration,
                                          "Resource removal used a stale generation."};
    }
    const bool hasDependents = std::any_of(m_entries.begin(), m_entries.end(), [handle](const Entry& candidate) {
        return candidate.handle != handle && ContainsHandle(candidate.dependencies, handle);
    });
    if (hasDependents) {
        return {ResourceDependencyCode::HasDependents,
                "Resource removal is blocked while another resource depends on this handle."};
    }
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [handle](const Entry& candidate) {
        return candidate.handle == handle;
    }), m_entries.end());
    BumpGeneration();
    return {ResourceDependencyCode::Removed, "Resource handle was removed."};
}

bool ResourceDependencyGraph::HasResource(const ResourceDependencyHandle handle) const noexcept {
    return FindExact(handle) != nullptr;
}

ResourceDependencySnapshot ResourceDependencyGraph::GetSnapshot() const {
    ResourceDependencySnapshot snapshot;
    snapshot.graphGeneration = m_graphGeneration;
    snapshot.entries.reserve(m_entries.size());
    for (const Entry& entry : m_entries) {
        snapshot.entries.push_back(ResourceDependencyEntry{entry.handle, entry.dependencies});
    }
    std::sort(snapshot.entries.begin(), snapshot.entries.end(), [](const auto& left, const auto& right) {
        if (left.handle.guid.value != right.handle.guid.value) {
            return left.handle.guid.value < right.handle.guid.value;
        }
        return left.handle.generation < right.handle.generation;
    });
    return snapshot;
}

ResourceDependencyGraph::Entry* ResourceDependencyGraph::FindExact(const ResourceDependencyHandle handle) noexcept {
    const auto iterator = std::find_if(m_entries.begin(), m_entries.end(), [handle](const Entry& entry) {
        return entry.handle == handle;
    });
    return iterator == m_entries.end() ? nullptr : &*iterator;
}

const ResourceDependencyGraph::Entry* ResourceDependencyGraph::FindExact(
    const ResourceDependencyHandle handle) const noexcept {
    const auto iterator = std::find_if(m_entries.cbegin(), m_entries.cend(), [handle](const Entry& entry) {
        return entry.handle == handle;
    });
    return iterator == m_entries.cend() ? nullptr : &*iterator;
}

const ResourceDependencyGraph::Entry* ResourceDependencyGraph::FindGuid(
    const AssetGuid guid) const noexcept {
    const auto iterator = std::find_if(m_entries.cbegin(), m_entries.cend(), [guid](const Entry& entry) {
        return entry.handle.guid == guid;
    });
    return iterator == m_entries.cend() ? nullptr : &*iterator;
}

bool ResourceDependencyGraph::Reaches(const ResourceDependencyHandle start, const ResourceDependencyHandle target,
                                            std::vector<ResourceDependencyHandle>& visited) const noexcept {
    if (start == target) {
        return true;
    }
    if (ContainsHandle(visited, start)) {
        return false;
    }
    visited.push_back(start);
    const Entry* entry = FindExact(start);
    if (entry == nullptr) {
        return false;
    }
    return std::any_of(entry->dependencies.begin(), entry->dependencies.end(),
                       [this, target, &visited](const ResourceDependencyHandle dependency) {
                           return Reaches(dependency, target, visited);
                       });
}

ResourceDependencyResult ResourceDependencyGraph::ValidateHandle(const ResourceDependencyHandle handle) const noexcept {
    if (!IsValidHandle(handle)) {
        return {ResourceDependencyCode::InvalidHandle, "Resource handle is invalid."};
    }
    return {ResourceDependencyCode::Registered, "Resource handle is valid."};
}

void ResourceDependencyGraph::BumpGeneration() noexcept {
    if (m_graphGeneration < std::numeric_limits<std::uint64_t>::max()) {
        ++m_graphGeneration;
    }
}

} // namespace UVE::Asset
