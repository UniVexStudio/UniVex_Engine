// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_interest.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace UVE::Network {
namespace {

// Positions the grid can hold: finite, and close enough that the floor-to-cell math stays
// inside an int even for the smallest allowed cell (1e6 / 1e-3 = 1e9 < 2^31).
[[nodiscard]] bool ValidPosition(const Math::Vector3& position) noexcept {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
           std::fabs(position.x) <= kInterestMaximumCoordinate &&
           std::fabs(position.y) <= kInterestMaximumCoordinate &&
           std::fabs(position.z) <= kInterestMaximumCoordinate;
}

// Squared distance, so verdicts never pass through a square root: the comparisons stay exact
// for the clean radii the tests (and level designers) use.
[[nodiscard]] float DistanceSquared(const Math::Vector3& a,
                                       const Math::Vector3& b) noexcept {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// Order inside a cell means nothing, so removal swaps with the back: O(cell population).
void EraseFromCell(std::vector<NetworkEntityId>& cell, const NetworkEntityId netId) {
    const auto found = std::find(cell.begin(), cell.end(), netId);
    if (found != cell.end()) {
        *found = cell.back();
        cell.pop_back();
    }
}

} // namespace

NetworkInterest::NetworkInterest(const InterestConfig& config) noexcept
    : m_cellSize((std::isfinite(config.cellSize) && config.cellSize >= kInterestMinimumCellSize)
                     ? config.cellSize
                     : kInterestDefaultCellSize),
      m_hysteresisMargin((std::isfinite(config.hysteresisMargin) && config.hysteresisMargin >= 0.0F)
                             ? config.hysteresisMargin
                             : 0.0F) {}

bool NetworkInterest::AddViewer(const InterestViewerId viewer, const float radius,
                                      const Math::Vector3& position) {
    if (viewer == kInvalidInterestViewer || !std::isfinite(radius) || radius <= 0.0F ||
        !ValidPosition(position) || m_viewers.find(viewer) != m_viewers.end()) {
        return false;
    }
    m_viewers.emplace(viewer, ViewerRecord{radius, position});
    return true;
}

std::vector<InterestViewerId> NetworkInterest::Viewers() const {
    std::vector<InterestViewerId> viewers;
    viewers.reserve(m_viewers.size());
    for (const auto& [viewer, _] : m_viewers) {
        viewers.push_back(viewer);
    }
    std::sort(viewers.begin(), viewers.end());
    return viewers;
}

std::optional<Math::Vector3> NetworkInterest::ViewerPosition(
    const InterestViewerId viewer) const noexcept {
    const auto found = m_viewers.find(viewer);
    if (found == m_viewers.end()) {
        return std::nullopt;
    }
    return found->second.position;
}

std::optional<Math::Vector3> NetworkInterest::EntityPosition(
    const NetworkEntityId netId) const noexcept {
    const auto entity = m_entities.find(netId);
    if (entity == m_entities.end()) {
        return std::nullopt;
    }
    return entity->second.position;
}

bool NetworkInterest::RemoveViewer(const InterestViewerId viewer) noexcept {
    if (m_viewers.erase(viewer) == 0U) {
        return false;
    }
    SweepViewerPairs(viewer);
    return true;
}

bool NetworkInterest::AddEntity(const NetworkEntityId netId,
                                      const Math::Vector3& position,
                                      const bool alwaysRelevant, const InterestTier tier) {
    if (!ValidPosition(position) || m_entities.find(netId) != m_entities.end()) {
        return false;
    }
    m_entities.emplace(netId, EntityRecord{position, alwaysRelevant, tier});
    const CellKey cell = CellOf(position);
    m_entityCells.emplace(netId, cell);
    m_cells[cell].push_back(netId);
    return true;
}

bool NetworkInterest::RemoveEntity(const NetworkEntityId netId) noexcept {
    const auto entity = m_entities.find(netId);
    if (entity == m_entities.end()) {
        return false;
    }
    const auto placed = m_entityCells.find(netId);
    if (placed != m_entityCells.end()) {
        const auto cell = m_cells.find(placed->second);
        if (cell != m_cells.end()) {
            EraseFromCell(cell->second, netId);
            if (cell->second.empty()) {
                m_cells.erase(cell);
            }
        }
        m_entityCells.erase(placed);
    }
    m_entities.erase(entity);
    SweepEntityPairs(netId);
    return true;
}

bool NetworkInterest::UpdateViewer(const InterestViewerId viewer,
                                         const Math::Vector3& position) noexcept {
    const auto found = m_viewers.find(viewer);
    if (found == m_viewers.end() || !ValidPosition(position)) {
        return false;
    }
    found->second.position = position;
    return true;
}

bool NetworkInterest::SetEntityTier(const NetworkEntityId netId,
                                          const InterestTier tier) noexcept {
    const auto entity = m_entities.find(netId);
    if (entity == m_entities.end()) {
        return false;
    }
    entity->second.tier = tier;
    return true;
}

std::optional<InterestTier> NetworkInterest::EntityTier(
    const NetworkEntityId netId) const noexcept {
    const auto entity = m_entities.find(netId);
    if (entity == m_entities.end()) {
        return std::nullopt;
    }
    return entity->second.tier;
}

bool NetworkInterest::SetEntityRate(const NetworkEntityId netId,
                                          const std::uint8_t everyN) noexcept {
    const auto entity = m_entities.find(netId);
    if (entity == m_entities.end()) {
        return false;
    }
    entity->second.rateOverride = everyN;
    return true;
}

std::optional<std::uint8_t> NetworkInterest::EntityRate(
    const NetworkEntityId netId) const noexcept {
    const auto entity = m_entities.find(netId);
    if (entity == m_entities.end()) {
        return std::nullopt;
    }
    return entity->second.rateOverride;
}

bool NetworkInterest::UpdateEntity(const NetworkEntityId netId,
                                         const Math::Vector3& position) noexcept {
    const auto entity = m_entities.find(netId);
    if (entity == m_entities.end() || !ValidPosition(position)) {
        return false;
    }
    entity->second.position = position;
    const auto placed = m_entityCells.find(netId);
    if (placed == m_entityCells.end()) {
        return true;
    }
    const CellKey next = CellOf(position);
    if (placed->second == next) {
        return true;
    }
    const auto old = m_cells.find(placed->second);
    if (old != m_cells.end()) {
        EraseFromCell(old->second, netId);
        if (old->second.empty()) {
            m_cells.erase(old);
        }
    }
    placed->second = next;
    m_cells[next].push_back(netId);
    return true;
}

bool NetworkInterest::IsRelevant(const InterestViewerId viewer,
                                       const NetworkEntityId netId) {
    ++m_stats.queries;
    const auto foundViewer = m_viewers.find(viewer);
    const auto foundEntity = m_entities.find(netId);
    if (foundViewer == m_viewers.end() || foundEntity == m_entities.end()) {
        ++m_stats.unknownIds;
        ++m_stats.relevant;
        return true;
    }
    // The clock advances for every known pair - relevant, culled, or always - since each
    // query is one send tick seen. Throttling is orthogonal to relevance: always-relevant
    // skips the distance, never the rate.
    const bool sendTick = Throttle(viewer, netId);
    const bool verdict =
        foundEntity->second.alwaysRelevant ||
        Judge(viewer, netId,
                 DistanceSquared(foundViewer->second.position, foundEntity->second.position),
                 foundViewer->second.radius);
    if (!verdict) {
        ++m_stats.culled;
        return false;
    }
    if (!sendTick) {
        ++m_stats.throttled;
        return false;
    }
    ++m_stats.relevant;
    return true;
}

std::vector<NetworkEntityId> NetworkInterest::RelevantEntities(
    const InterestViewerId viewer) {
    ++m_stats.refreshes;
    const auto foundViewer = m_viewers.find(viewer);
    if (foundViewer == m_viewers.end()) {
        ++m_stats.unknownIds;
        std::vector<NetworkEntityId> all;
        all.reserve(m_entities.size());
        for (const auto& [netId, _] : m_entities) {
            all.push_back(netId);
        }
        std::sort(all.begin(), all.end());
        return all;
    }
    // Occupied cells only, gated by cell-vs-sphere overlap: the work scales with where
    // entities ARE, never with the radius cubed - a huge radius visits every occupied cell
    // and judges truly, instead of iterating millions of empty ones. Dense-thousands scale
    // would iterate the sphere's cell range instead; that swap is noted, not needed yet.
    const float reach = foundViewer->second.radius + m_hysteresisMargin;
    std::vector<NetworkEntityId> relevant;
    // Always-relevant entities first, flat: they live anywhere - including cells the overlap
    // gate below would rightly skip for distance-judged entities - so the grid never sees
    // them at all. Their sets are tiny (a player, a boss); the scan costs nothing.
    for (const auto& [netId, record] : m_entities) {
        if (record.alwaysRelevant && Throttle(viewer, netId)) {
            relevant.push_back(netId);
        }
    }
    for (const auto& [cell, members] : m_cells) {
        if (!CellOverlaps(cell, foundViewer->second.position, reach)) {
            continue;
        }
        for (const NetworkEntityId netId : members) {
            const auto entity = m_entities.find(netId);
            if (entity == m_entities.end() || entity->second.alwaysRelevant) {
                continue;
            }
            // Both advance always (no short-circuit): bulk and single queries share one
            // clock per pair, so a bulk refresh lands on the same phase as single queries.
            const bool judged = Judge(
                viewer, netId,
                DistanceSquared(foundViewer->second.position, entity->second.position),
                foundViewer->second.radius);
            const bool sendTick = Throttle(viewer, netId);
            if (judged && sendTick) {
                relevant.push_back(netId);
            }
        }
    }
    std::sort(relevant.begin(), relevant.end());
    return relevant;
}

NetworkInterest::CellKey NetworkInterest::CellOf(
    const Math::Vector3& position) const noexcept {
    return CellKey{static_cast<int>(std::floor(position.x / m_cellSize)),
                      static_cast<int>(std::floor(position.y / m_cellSize)),
                      static_cast<int>(std::floor(position.z / m_cellSize))};
}

bool NetworkInterest::CellOverlaps(const CellKey& cell, const Math::Vector3& center,
                                         const float radius) const noexcept {
    // Closest point on the cell box to the sphere center, in doubles: at extreme coordinates
    // float box math would drift by hundreds of units, and a wrongly skipped cell would cull
    // a relevant entity. The verdict for visited entities still uses the true float distance.
    const double size = static_cast<double>(m_cellSize);
    const double reach = static_cast<double>(radius);
    const double cx = static_cast<double>(center.x);
    const double cy = static_cast<double>(center.y);
    const double cz = static_cast<double>(center.z);
    const double loX = static_cast<double>(std::get<0>(cell)) * size;
    const double loY = static_cast<double>(std::get<1>(cell)) * size;
    const double loZ = static_cast<double>(std::get<2>(cell)) * size;
    const double closestX = std::clamp(cx, loX, loX + size);
    const double closestY = std::clamp(cy, loY, loY + size);
    const double closestZ = std::clamp(cz, loZ, loZ + size);
    const double dx = cx - closestX;
    const double dy = cy - closestY;
    const double dz = cz - closestZ;
    return dx * dx + dy * dy + dz * dz <= reach * reach;
}

void NetworkInterest::SweepViewerPairs(const InterestViewerId viewer) noexcept {
    for (auto pair = m_relevantPairs.begin(); pair != m_relevantPairs.end();) {
        pair = pair->first == viewer ? m_relevantPairs.erase(pair) : std::next(pair);
    }
    for (auto tick = m_sendTicks.begin(); tick != m_sendTicks.end();) {
        tick = tick->first.first == viewer ? m_sendTicks.erase(tick) : std::next(tick);
    }
}

void NetworkInterest::SweepEntityPairs(const NetworkEntityId netId) noexcept {
    for (auto pair = m_relevantPairs.begin(); pair != m_relevantPairs.end();) {
        pair = pair->second == netId ? m_relevantPairs.erase(pair) : std::next(pair);
    }
    for (auto tick = m_sendTicks.begin(); tick != m_sendTicks.end();) {
        tick = tick->first.second == netId ? m_sendTicks.erase(tick) : std::next(tick);
    }
}

bool NetworkInterest::Throttle(const InterestViewerId viewer,
                                     const NetworkEntityId netId) noexcept {
    const auto entity = m_entities.find(netId);
    if (entity == m_entities.end()) {
        return true;
    }
    const std::uint8_t divisor = entity->second.rateOverride != 0U
                                     ? entity->second.rateOverride
                                     : TierDivisor(entity->second.tier);
    const std::pair<InterestViewerId, NetworkEntityId> pair{viewer, netId};
    const std::uint64_t tick = m_sendTicks[pair]++;
    return (tick + static_cast<std::uint64_t>(netId)) % divisor == 0U;
}

bool NetworkInterest::Judge(const InterestViewerId viewer, const NetworkEntityId netId,
                                  const float distanceSquared, const float radius) noexcept {
    const std::pair<InterestViewerId, NetworkEntityId> pair{viewer, netId};
    if (distanceSquared <= radius * radius) {
        m_relevantPairs.insert(pair);
        return true;
    }
    const float outer = radius + m_hysteresisMargin;
    if (distanceSquared > outer * outer) {
        m_relevantPairs.erase(pair);
        return false;
    }
    return m_relevantPairs.find(pair) != m_relevantPairs.end();
}

} // namespace UVE::Network
