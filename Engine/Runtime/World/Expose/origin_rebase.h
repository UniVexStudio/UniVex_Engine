// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

#include "i_entity_manager.h"
#include "vector3.h"

namespace UVE::World {

/// Double-precision floating-origin state keeping single-precision rendering/physics coordinates
/// centered near (0,0,0) across kilometer-scale worlds without precision jitter.
struct OriginRebaseStateUVE final {
    std::array<double, 3> worldOriginOffset{0.0, 0.0, 0.0};
    float rebaseThresholdMeters = 2048.0F;
    std::uint64_t rebaseCount = 0U;

    [[nodiscard]] std::array<double, 3> LocalToWorldDoubleUVE(const Math::Vector3UVE& localPos) const noexcept {
        return {
            worldOriginOffset[0] + static_cast<double>(localPos.x),
            worldOriginOffset[1] + static_cast<double>(localPos.y),
            worldOriginOffset[2] + static_cast<double>(localPos.z),
        };
    }

    [[nodiscard]] Math::Vector3UVE WorldDoubleToLocalUVE(const std::array<double, 3>& worldPos) const noexcept {
        return Math::Vector3UVE{
            static_cast<float>(worldPos[0] - worldOriginOffset[0]),
            static_cast<float>(worldPos[1] - worldOriginOffset[1]),
            static_cast<float>(worldPos[2] - worldOriginOffset[2]),
        };
    }
};

/// When `cameraLocalPosition` exceeds `state.rebaseThresholdMeters` from the origin, shifts every
/// root entity's TransformComponentUVE and WorldTransformComponentUVE by `-shift` and accumulates
/// `+shift` into `state.worldOriginOffset`. Returns true when a rebase occurred.
[[nodiscard]] bool TryRebaseWorldOriginUVE(Scene::IEntityManagerUVE& entityManager,
                                           Math::Vector3UVE& inOutCameraLocalPosition,
                                           OriginRebaseStateUVE& state);

/// Asynchronous disk-backed world-cell / sub-level streaming queue: reads & pre-parses cell
/// payloads on background threads and delivers ready payloads to the main thread under a per-tick
/// budget so large-world streaming never hitches frame time.
struct StreamedCellPayloadUVE final {
    std::string cellKey;
    std::filesystem::path sourcePath;
    std::vector<std::uint8_t> rawBytes;
    bool succeeded = false;
};

class AsyncWorldStreamQueueUVE final {
public:
    void RequestLoadUVE(std::string cellKey, std::filesystem::path sourcePath);
    /// Drains up to `maxCommits` completed async loads without blocking.
    [[nodiscard]] std::vector<StreamedCellPayloadUVE> PollCompletedUVE(std::size_t maxCommits = 4U);
    [[nodiscard]] std::size_t PendingCountUVE() const noexcept { return m_pending.size(); }

private:
    std::vector<std::future<StreamedCellPayloadUVE>> m_pending;
};

} // namespace UVE::World
