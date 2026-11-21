// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "origin_rebase.h"

#include <chrono>
#include <cmath>
#include <fstream>
#include <utility>

#include "hierarchy_component.h"
#include "transform_component.h"
#include "world_transform_component.h"

namespace UVE::World {

bool TryRebaseWorldOriginUVE(Scene::IEntityManagerUVE& entityManager, Math::Vector3UVE& inOutCameraLocalPosition,
                             OriginRebaseStateUVE& state) {
    if (state.rebaseThresholdMeters <= 0.0F) {
        return false;
    }
    const float distSq = inOutCameraLocalPosition.x * inOutCameraLocalPosition.x +
                         inOutCameraLocalPosition.y * inOutCameraLocalPosition.y +
                         inOutCameraLocalPosition.z * inOutCameraLocalPosition.z;
    const float threshSq = state.rebaseThresholdMeters * state.rebaseThresholdMeters;
    if (distSq < threshSq) {
        return false;
    }

    const Math::Vector3UVE shift{
        std::floor(inOutCameraLocalPosition.x),
        std::floor(inOutCameraLocalPosition.y),
        std::floor(inOutCameraLocalPosition.z),
    };
    if (shift.x == 0.0F && shift.y == 0.0F && shift.z == 0.0F) {
        return false;
    }

    entityManager.ForEachUVE<Scene::TransformComponentUVE>(
        [&](const Scene::EntityUVE entity, Scene::TransformComponentUVE& transform) {
            bool isRoot = true;
            if (entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(entity)) {
                const auto& hierarchy = entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(entity);
                isRoot = (hierarchy.parent == Scene::kInvalidEntityUVE);
            }
            if (isRoot) {
                transform.localPosition = transform.localPosition - shift;
            }
        });

    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE>(
        [&](const Scene::EntityUVE, const Scene::WorldTransformComponentUVE& worldTransform) {
            worldTransform.dirty = true;
        });

    inOutCameraLocalPosition = inOutCameraLocalPosition - shift;
    state.worldOriginOffset[0] += static_cast<double>(shift.x);
    state.worldOriginOffset[1] += static_cast<double>(shift.y);
    state.worldOriginOffset[2] += static_cast<double>(shift.z);
    ++state.rebaseCount;
    return true;
}

void AsyncWorldStreamQueueUVE::RequestLoadUVE(std::string cellKey, std::filesystem::path sourcePath) {
    m_pending.push_back(std::async(std::launch::async,
                                   [key = std::move(cellKey), path = std::move(sourcePath)]() mutable {
                                       StreamedCellPayloadUVE payload{};
                                       payload.cellKey = std::move(key);
                                       payload.sourcePath = std::move(path);
                                       std::ifstream in(payload.sourcePath, std::ios::binary | std::ios::ate);
                                       if (!in) {
                                           return payload;
                                       }
                                       const std::streamsize size = in.tellg();
                                       if (size < 0) {
                                           return payload;
                                       }
                                       in.seekg(0, std::ios::beg);
                                       payload.rawBytes.resize(static_cast<std::size_t>(size));
                                       if (size == 0 ||
                                           in.read(reinterpret_cast<char*>(payload.rawBytes.data()), size)) {
                                           payload.succeeded = true;
                                       }
                                       return payload;
                                   }));
}

std::vector<StreamedCellPayloadUVE> AsyncWorldStreamQueueUVE::PollCompletedUVE(const std::size_t maxCommits) {
    std::vector<StreamedCellPayloadUVE> ready;
    if (maxCommits == 0U) {
        return ready;
    }
    for (auto it = m_pending.begin(); it != m_pending.end() && ready.size() < maxCommits;) {
        if (it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            ready.push_back(it->get());
            it = m_pending.erase(it);
        } else {
            ++it;
        }
    }
    return ready;
}

} // namespace UVE::World
