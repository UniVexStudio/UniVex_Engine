// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/asset/asset_guid_uve.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UVE::Asset {

struct ResourceDependencyHandleUVE final {
    AssetGuidUVE guid;
    std::uint64_t generation = 0U;

    [[nodiscard]] bool operator==(const ResourceDependencyHandleUVE&) const noexcept = default;
};

struct ResourceDependencyEntryUVE final {
    ResourceDependencyHandleUVE handle;
    std::vector<ResourceDependencyHandleUVE> dependencies;

    [[nodiscard]] bool operator==(const ResourceDependencyEntryUVE&) const = default;
};

struct ResourceDependencySnapshotUVE final {
    std::uint64_t graphGeneration = 0U;
    bool entriesTruncated = false;
    std::vector<ResourceDependencyEntryUVE> entries;

    [[nodiscard]] bool operator==(const ResourceDependencySnapshotUVE&) const = default;
};

enum class ResourceDependencyCodeUVE : std::uint8_t {
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

struct ResourceDependencyResultUVE final {
    ResourceDependencyCodeUVE code = ResourceDependencyCodeUVE::InvalidHandle;
    std::string message;

    [[nodiscard]] bool IsAppliedUVE() const noexcept {
        return code == ResourceDependencyCodeUVE::Registered ||
               code == ResourceDependencyCodeUVE::Updated ||
               code == ResourceDependencyCodeUVE::Removed;
    }
};

/// A copied, deterministic breadth-first list of every registered resource that transitively
/// depends on `root`. The root is excluded from `dependents`; consumers invalidate the returned
/// dependents before reprocessing the changed root. The graph generation identifies the snapshot
/// revision and callers must discard the plan if their graph has moved since capture.
struct ResourceDependencyInvalidationPlanUVE final {
    ResourceDependencyCodeUVE code = ResourceDependencyCodeUVE::InvalidHandle;
    std::string message;
    std::uint64_t graphGeneration = 0U;
    ResourceDependencyHandleUVE root{};
    bool dependentsTruncated = false;
    std::vector<ResourceDependencyHandleUVE> dependents;

    [[nodiscard]] bool IsReadyUVE() const noexcept {
        return code == ResourceDependencyCodeUVE::DependentClosureReady;
    }
};

class ResourceDependencyGraphUVE final {
public:
    static constexpr std::size_t kMaximumResourcesUVE = 4096U;
    static constexpr std::size_t kMaximumDependenciesPerResourceUVE = 128U;

    ResourceDependencyGraphUVE() = default;
    ResourceDependencyGraphUVE(const ResourceDependencyGraphUVE&) = delete;
    ResourceDependencyGraphUVE& operator=(const ResourceDependencyGraphUVE&) = delete;

    [[nodiscard]] ResourceDependencyResultUVE RegisterResourceUVE(ResourceDependencyHandleUVE handle);
    [[nodiscard]] ResourceDependencyResultUVE SetDependenciesUVE(
        ResourceDependencyHandleUVE handle, std::vector<ResourceDependencyHandleUVE> dependencies);
    [[nodiscard]] ResourceDependencyResultUVE RemoveResourceUVE(ResourceDependencyHandleUVE handle);
    /// Returns a copied breadth-first reverse-dependent closure. `maximumDependents` bounds the
    /// result; a true `dependentsTruncated` flag means additional dependents exist beyond the copy.
    [[nodiscard]] ResourceDependencyInvalidationPlanUVE GetDependentClosureUVE(
        ResourceDependencyHandleUVE root, std::size_t maximumDependents = kMaximumResourcesUVE) const;
    [[nodiscard]] bool HasResourceUVE(ResourceDependencyHandleUVE handle) const noexcept;
    [[nodiscard]] ResourceDependencySnapshotUVE GetSnapshotUVE() const;

private:
    struct EntryUVE final {
        ResourceDependencyHandleUVE handle;
        std::vector<ResourceDependencyHandleUVE> dependencies;
    };

    [[nodiscard]] EntryUVE* FindExactUVE(ResourceDependencyHandleUVE handle) noexcept;
    [[nodiscard]] const EntryUVE* FindExactUVE(ResourceDependencyHandleUVE handle) const noexcept;
    [[nodiscard]] const EntryUVE* FindGuidUVE(AssetGuidUVE guid) const noexcept;
    [[nodiscard]] bool ReachesUVE(ResourceDependencyHandleUVE start, ResourceDependencyHandleUVE target,
                                  std::vector<ResourceDependencyHandleUVE>& visited) const noexcept;
    [[nodiscard]] ResourceDependencyResultUVE ValidateHandleUVE(ResourceDependencyHandleUVE handle) const noexcept;
    void BumpGenerationUVE() noexcept;

    std::vector<EntryUVE> m_entries;
    std::uint64_t m_graphGeneration = 0U;
};

} // namespace UVE::Asset
