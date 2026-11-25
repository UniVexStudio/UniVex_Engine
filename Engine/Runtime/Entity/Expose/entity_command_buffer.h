// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>

#include "entity.h"
#include "i_entity_manager.h"
#include "engine_assert.h"

namespace UVE::Scene {

/// A placeholder for an entity created by the command buffer: the create runs at playback, so
/// the handle does not exist yet at record time. Valid only with the buffer that issued it, and
/// resolvable (via `Resolve`) after that buffer plays back. `index` compares equal only for
/// placeholders from the same buffer.
struct DeferredEntity final {
    std::uint32_t index = 0U;

    [[nodiscard]] bool operator==(const DeferredEntity&) const = default;
};

/// Records structural changes (create / destroy / add / remove) during iteration and applies
/// them afterwards in record order.
///
/// WHY THIS EXISTS. Archetype storage migrates entities on structural change, so creating,
/// destroying, adding or removing *while a `ForEach` is running* invalidates the iteration.
/// The buffer is the deferred path: systems record what the iteration decided, finish the loop,
/// then `Playback`. Playback runs the commands in record order and clears the buffer, so a
/// buffer is record-once, play-once, then reusable.
///
/// HOW IT ERASES TYPES. Each command is a closure over its arguments — no variant-per-op, no
/// manual payload union. An `AddComponent<T>` stages a `T` by value (move-constructed into
/// the closure exactly as the manager's placement-new would construct it), and playback
/// move-constructs it into the live slot. `T` must be move-constructible, which every plain
/// component struct is.
///
/// DEFERRED ENTITIES. `CreateEntity` returns a placeholder immediately and creates the real
/// entity at playback; later commands in the same buffer may attach components to, or destroy,
/// the placeholder. Playback resolves placeholders in record order, so a placeholder is always
/// created before it is used — the only way to hold one is to have recorded its create first.
/// `Resolve` reads the mapping back after playback (until the next playback or `Clear`).
///
/// There is deliberately no `RemoveComponent` for placeholders: a to-be-created entity is
/// born component-less, so removing from one can never mean anything.
///
/// Thread-safety: not thread-safe — record and play from the scene thread, like the manager.
/// A buffer must not be played back while one of its own commands is running (commands never
/// re-enter the buffer, so this only bites on aliasing trickery).
class EntityCommandBuffer final {
public:
    EntityCommandBuffer() = default;
    EntityCommandBuffer(const EntityCommandBuffer&) = delete;
    EntityCommandBuffer& operator=(const EntityCommandBuffer&) = delete;
    EntityCommandBuffer(EntityCommandBuffer&&) noexcept = default;
    EntityCommandBuffer& operator=(EntityCommandBuffer&&) noexcept = default;

    /// Records a create; the entity exists after playback. The placeholder may be used by later
    /// commands in this buffer.
    DeferredEntity CreateEntity() {
        const DeferredEntity deferred{m_deferredCount++};
        m_commands.emplace_back(
            [deferred](IEntityManager& manager, std::vector<Entity>& mapping) {
                mapping.push_back(manager.CreateEntity());
                UVE_ASSERT(mapping.size() > deferred.index);
            });
        return deferred;
    }

    /// Records the destruction of a live entity.
    void DestroyEntity(const Entity entity) {
        m_commands.emplace_back(
            [entity](IEntityManager& manager, std::vector<Entity>&) {
                manager.DestroyEntity(entity);
            });
    }

    /// Records the destruction of a placeholder created earlier in this buffer.
    void DestroyEntity(const DeferredEntity deferred) {
        m_commands.emplace_back(
            [deferred](IEntityManager& manager, std::vector<Entity>& mapping) {
                manager.DestroyEntity(ResolveLocked(mapping, deferred));
            });
    }

    /// Records attaching a `T` (constructed from `args`) to a live entity. The payload is
    /// staged by value at record time and moved into the live slot at playback.
    template <typename T, typename... TArgs>
    void AddComponent(const Entity entity, TArgs&&... args) {
        static_assert(std::is_move_constructible_v<T>,
                      "EntityCommandBuffer stages component payloads by value; T must be move-constructible.");
        T payload(std::forward<TArgs>(args)...);
        m_commands.emplace_back(
            [entity, held = std::move(payload)](IEntityManager& manager, std::vector<Entity>&) mutable {
                manager.AddComponent<T>(entity, std::move(held));
            });
    }

    /// Records attaching a `T` (constructed from `args`) to a placeholder created earlier in
    /// this buffer.
    template <typename T, typename... TArgs>
    void AddComponent(const DeferredEntity deferred, TArgs&&... args) {
        static_assert(std::is_move_constructible_v<T>,
                      "EntityCommandBuffer stages component payloads by value; T must be move-constructible.");
        T payload(std::forward<TArgs>(args)...);
        m_commands.emplace_back(
            [deferred, held = std::move(payload)](IEntityManager& manager,
                                                  std::vector<Entity>& mapping) mutable {
                manager.AddComponent<T>(ResolveLocked(mapping, deferred), std::move(held));
            });
    }

    /// Records removing `T` from a live entity. Same asserts as the manager's remove, deferred
    /// to playback.
    template <typename T>
    void RemoveComponent(const Entity entity) {
        m_commands.emplace_back(
            [entity](IEntityManager& manager, std::vector<Entity>&) {
                manager.RemoveComponent<T>(entity);
            });
    }

    /// Runs every recorded command in record order, then clears the buffer (commands and the
    /// placeholder count). The placeholder→entity mapping survives until the next playback or
    /// `Clear`, so `Resolve` stays readable right after this returns.
    void Playback(IEntityManager& manager) {
        m_resolved.clear();
        m_resolved.reserve(m_deferredCount);
        // Non-const iteration: move_only_function's call operator is non-const unless the
        // signature says otherwise, and playback already takes the buffer mutably.
        for (auto& command : m_commands) {
            command(manager, m_resolved);
        }
        m_commands.clear();
        m_deferredCount = 0U;
    }

    /// The live entity a placeholder became at playback. Debug builds assert the placeholder
    /// came from this buffer's most recent playback; call only after `Playback` and before
    /// the next playback or `Clear`.
    [[nodiscard]] Entity Resolve(const DeferredEntity deferred) const {
        UVE_ASSERT(deferred.index < m_resolved.size());
        return deferred.index < m_resolved.size() ? m_resolved[deferred.index] : kInvalidEntity;
    }

    /// Recorded-but-unplayed command count.
    [[nodiscard]] std::size_t GetCommandCount() const noexcept {
        return m_commands.size();
    }

    /// True when nothing is recorded.
    [[nodiscard]] bool IsEmpty() const noexcept {
        return m_commands.empty();
    }

    /// Discards every recorded command plus the playback mapping, without running anything.
    void Clear() {
        m_commands.clear();
        m_deferredCount = 0U;
        m_resolved.clear();
    }

private:
    /// Resolves a placeholder against the in-playback mapping. Debug builds assert the create
    /// ran before this command (record order guarantees it); release builds return the invalid
    /// entity rather than reading out of bounds, and the manager then fails closed per its own
    /// release contract (assert-free no-op for destroy, false for queries).
    [[nodiscard]] static Entity ResolveLocked(const std::vector<Entity>& mapping,
                                                    const DeferredEntity deferred) {
        UVE_ASSERT(deferred.index < mapping.size());
        return deferred.index < mapping.size() ? mapping[deferred.index] : kInvalidEntity;
    }

    // move_only_function, not function: a move-only payload stages fine and must not demand a copy.
    std::vector<std::move_only_function<void(IEntityManager&, std::vector<Entity>&)>> m_commands;
    std::uint32_t m_deferredCount = 0U;
    std::vector<Entity> m_resolved;
};

} // namespace UVE::Scene
