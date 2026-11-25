// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "asset_guid.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UVE::Asset {

struct ResourceDependencyHandle final {
    AssetGuid guid;
    std::uint64_t generation = 0U;

    [[nodiscard]] bool operator==(const ResourceDependencyHandle&) const noexcept = default;
};

struct ResourceDependencyEntry final {
    ResourceDependencyHandle handle;
    std::vector<ResourceDependencyHandle> dependencies;

    [[nodiscard]] bool operator==(const ResourceDependencyEntry&) const = default;
};

struct ResourceDependencySnapshot final {
    std::uint64_t graphGeneration = 0U;
    bool entriesTruncated = false;
    std::vector<ResourceDependencyEntry> entries;

    [[nodiscard]] bool operator==(const ResourceDependencySnapshot&) const = default;
};

enum class ResourceDependencyCode : std::uint8_t {
    Registered = 0,
    Updated,
    Removed,
    InvalidHandle,
    DuplicateResource,
    StaleGeneration,
    UnknownDependency,
    CycleDetected,
    HasDependents,
    CapacityExceeded,
    DependentClosureReady,
};

struct ResourceDependencyResult final {
    ResourceDependencyCode code = ResourceDependencyCode::InvalidHandle;
    std::string message;

    [[nodiscard]] bool IsApplied() const noexcept {
        return code == ResourceDependencyCode::Registered ||
               code == ResourceDependencyCode::Updated ||
               code == ResourceDependencyCode::Removed;
    }
};

/// A copied, deterministic breadth-first list of every registered resource that transitively
/// depends on `root`. The root is excluded from `dependents`; consumers invalidate the returned
/// dependents before reprocessing the changed root. The graph generation identifies the snapshot
/// revision and callers must discard the plan if their graph has moved since capture.
struct ResourceDependencyInvalidationPlan final {
    ResourceDependencyCode code = ResourceDependencyCode::InvalidHandle;
    std::string message;
    std::uint64_t graphGeneration = 0U;
    ResourceDependencyHandle root{};
    bool dependentsTruncated = false;
    std::vector<ResourceDependencyHandle> dependents;

    [[nodiscard]] bool IsReady() const noexcept {
        return code == ResourceDependencyCode::DependentClosureReady;
    }
};

class ResourceDependencyGraph final {
public:
    static constexpr std::size_t kMaximumResources = 4096U;
    static constexpr std::size_t kMaximumDependenciesPerResource = 128U;

    ResourceDependencyGraph() = default;
    ResourceDependencyGraph(const ResourceDependencyGraph&) = delete;
    ResourceDependencyGraph& operator=(const ResourceDependencyGraph&) = delete;

    [[nodiscard]] ResourceDependencyResult RegisterResource(ResourceDependencyHandle handle);
    [[nodiscard]] ResourceDependencyResult SetDependencies(
        ResourceDependencyHandle handle, std::vector<ResourceDependencyHandle> dependencies);
    [[nodiscard]] ResourceDependencyResult RemoveResource(ResourceDependencyHandle handle);
    /// Returns a copied breadth-first reverse-dependent closure. `maximumDependents` bounds the
    /// result; a true `dependentsTruncated` flag means additional dependents exist beyond the copy.
    [[nodiscard]] ResourceDependencyInvalidationPlan GetDependentClosure(
        ResourceDependencyHandle root, std::size_t maximumDependents = kMaximumResources) const;
    [[nodiscard]] bool HasResource(ResourceDependencyHandle handle) const noexcept;
    [[nodiscard]] ResourceDependencySnapshot GetSnapshot() const;

private:
    struct Entry final {
        ResourceDependencyHandle handle;
        std::vector<ResourceDependencyHandle> dependencies;
    };

    [[nodiscard]] Entry* FindExact(ResourceDependencyHandle handle) noexcept;
    [[nodiscard]] const Entry* FindExact(ResourceDependencyHandle handle) const noexcept;
    [[nodiscard]] const Entry* FindGuid(AssetGuid guid) const noexcept;
    [[nodiscard]] bool Reaches(ResourceDependencyHandle start, ResourceDependencyHandle target,
                                  std::vector<ResourceDependencyHandle>& visited) const noexcept;
    [[nodiscard]] ResourceDependencyResult ValidateHandle(ResourceDependencyHandle handle) const noexcept;
    void BumpGeneration() noexcept;

    std::vector<Entry> m_entries;
    std::uint64_t m_graphGeneration = 0U;
};

} // namespace UVE::Asset
