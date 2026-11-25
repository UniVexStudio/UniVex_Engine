// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <functional>
#include <typeindex>
#include <utility>
#include <vector>

#include "engine_assert.h"
#include "component_type_info.h"
#include "component_access.h"
#include "world_transform_component.h"
#include "entity.h"
#include "entity_view.h"

namespace UVE::Scene {

namespace Detail {

/// Expands the type-erased `pointers` vector back into the `TComponents...` parameter pack, in
/// order, before invoking the caller's strongly-typed callback. A standard
/// std::index_sequence-based unpack — not exotic, just the mechanical way to turn a runtime
/// vector<void*> back into a compile-time-known pack of typed references.
template <typename... TComponents, typename TFunc, std::size_t... Is>
void InvokeForEachCallback(TFunc& callback, Entity entity, const std::vector<void*>& pointers,
                               std::index_sequence<Is...>) {
    callback(entity, *static_cast<TComponents*>(pointers[Is])...);
}

} // namespace Detail

/// IEntityManager is the archetype ECS interface: entity lifecycle (create/destroy),
/// component attachment, and multi-component queries. Mirrors IEventSystem's exact shape
/// (engine/events/include/uve/events/i_event_system_uve.h) — public, non-virtual member
/// templates for compile-time type safety at call sites, routing through protected pure-virtual
/// type-erased primitives a concrete implementation provides. Component types are identified by
/// std::type_index(typeid(T)), the same mechanism IEventSystem already uses, rather than a
/// new global type-id registry (see docs/CODING_STANDARDS.md).
/// Thread-safety: not thread-safe — every method (including ForEach) must be called only
/// from the main/scene thread, matching EngineCore's own single-threaded contract. A future
/// increment could add parallel ForEach iteration via IThreadPool over already-stable
/// archetypes without changing this interface.
class IEntityManager {
public:
    virtual ~IEntityManager() = default;

    /// Creates a new, component-less entity and publishes EntityCreatedEvent.
    [[nodiscard]] virtual Entity CreateEntity() = 0;

    /// Destroys `entity`, destroying every component it owns and publishing EntityDestroyedEvent.
    /// Debug builds assert that the entity is alive; release builds return without mutation for a
    /// stale or invalid handle.
    virtual void DestroyEntity(Entity entity) = 0;

    /// True iff `entity`'s generation matches the current occupant of its index slot.
    [[nodiscard]] virtual bool IsAlive(Entity entity) const noexcept = 0;

    /// Number of currently-alive entities.
    [[nodiscard]] virtual std::size_t GetEntityCount() const noexcept = 0;

    /// Returns the std::type_index of every component `entity` currently has, in unspecified
    /// order. Debug builds assert that the entity is alive; release builds return an empty vector
    /// for a stale or invalid handle. The generic, runtime-enumerable counterpart to
    /// HasComponent<T>()/GetComponent<T>() — used by SceneSerializer to walk an
    /// entity's full component set without knowing the types in advance.
    [[nodiscard]] virtual std::vector<std::type_index> GetComponentTypes(Entity entity) const = 0;

    /// Attaches a `T` (constructed from `args`) to `entity`, migrating it to the archetype
    /// {entity's current components} ∪ {T}. Asserts `entity` is alive and does not already have
    /// a `T`. Returns a reference to the newly-constructed component (valid until the next
    /// structural change involving `entity` or any entity sharing its archetype/chunk).
    /// Component types are plain, non-polymorphic object types with no shared base — an earlier
    /// draft had every component type derive from an empty marker base, but that broke portable
    /// placement-construction: an empty base class forces stricter brace-elision rules in
    /// aggregate/list-initialization that GCC and Clang disagree on for a single,
    /// non-brace-enclosed initializer, i.e. exactly this call copy-constructing `T` from a single
    /// existing value — confirmed by direct testing across both compilers this project targets.
    template <typename T, typename... TArgs>
    PublicComponent<T>& AddComponent(Entity entity, TArgs&&... args) {
        void* const slot = AddComponentErased(entity, std::type_index(typeid(T)), MakeComponentTypeInfo<T>());
        return *(::new (slot) T(std::forward<TArgs>(args)...));
    }

    /// Runtime-typed counterpart to AddComponent<T>(): attaches a component of type
    /// `componentType` (described by `typeInfo`, whose size/alignment/construct/move/destroy
    /// must match the type) to `entity`, default-constructed in its archetype slot. The caller
    /// fills the slot through the returned pointer (a metadata entry's assignInstance from a
    /// factory-built instance, today). Same asserts as the typed form: `entity` is alive and
    /// does not already have the type. What lets generic deserialization attach a component it
    /// only knows by std::type_index.
    [[nodiscard]] void* AddComponent(Entity entity, std::type_index componentType,
                                        const ComponentTypeInfo& typeInfo) {
        // Runtime-derived caches are not generic authoring/serialization destinations.
        if (componentType == std::type_index(typeid(WorldTransformComponent))) return nullptr;
        void* const slot = AddComponentErased(entity, componentType, typeInfo);
        typeInfo.constructDefault(slot);
        return slot;
    }

    /// Removes `T` from `entity`, migrating it to the new archetype (entity's current
    /// components minus `T`). Asserts `entity` is alive and currently has a `T`.
    template <typename T>
    void RemoveComponent(Entity entity) {
        RemoveComponentErased(entity, std::type_index(typeid(T)));
    }

    /// True iff a live `entity` currently has a `T`. Debug builds assert that the entity is alive;
    /// release builds return false for a stale or invalid handle.
    template <typename T>
    [[nodiscard]] bool HasComponent(Entity entity) const {
        return HasComponentErased(entity, std::type_index(typeid(T)));
    }

    /// Runtime-typed counterpart to HasComponent<T>(), for a caller that only learns the type
    /// at runtime. GetComponentPointer() below asserts that the component is present, so it is
    /// not a presence test; this is, and it is what a caller asks first.
    [[nodiscard]] bool HasComponent(Entity entity, std::type_index componentType) const {
        return IsAlive(entity) && HasComponentErased(entity, componentType);
    }

    /// Returns a reference to `entity`'s `T`. Asserts `entity` is alive and currently has one.
    template <typename T>
    [[nodiscard]] PublicComponent<T>& GetComponent(Entity entity) {
        return *static_cast<PublicComponent<T>*>(GetComponentErased(entity, std::type_index(typeid(T))));
    }

    /// Scene-graph authority is required for mutable live derived caches. The access key cannot
    /// be constructed by ordinary consumers; the type-erased lookup stays implementation-private.
    template <typename T>
    [[nodiscard]] T& GetSceneGraphComponent(Entity entity, SceneGraphWriteAccess) {
        return *static_cast<T*>(GetComponentErased(entity, std::type_index(typeid(T))));
    }

    /// Const overload of GetComponent<T>().
    template <typename T>
    [[nodiscard]] const T& GetComponent(Entity entity) const {
        return *static_cast<const T*>(
            const_cast<IEntityManager*>(this)->GetComponentErased(entity, std::type_index(typeid(T))));
    }

    /// Returns a type-erased pointer to `entity`'s component of type `componentType`. Debug builds
    /// assert that the entity is alive and currently has the component; release builds return
    /// nullptr for a stale entity or absent component. The runtime-typed counterpart to
    /// GetComponent<T>(), used together with GetComponentTypes() by SceneSerializer to
    /// read every component of an entity whose types are only known at runtime.
    [[nodiscard]] void* GetComponentPointer(Entity entity, std::type_index componentType) {
        if (!IsAlive(entity)) {
            UVE_ASSERT(IsAlive(entity));
            return nullptr;
        }
        if (!HasComponentErased(entity, componentType)) {
            UVE_ASSERT(HasComponentErased(entity, componentType));
            return nullptr;
        }
        if (componentType == std::type_index(typeid(WorldTransformComponent))) return nullptr;
        return GetComponentErased(entity, componentType);
    }

    /// Const erased reads include derived caches; mutable erased access above rejects them.
    [[nodiscard]] const void* GetComponentPointer(Entity entity, std::type_index componentType) const {
        if (!IsAlive(entity) || !HasComponentErased(entity, componentType)) {
            UVE_ASSERT(IsAlive(entity) && HasComponentErased(entity, componentType));
            return nullptr;
        }
        return const_cast<IEntityManager*>(this)->GetComponentErased(entity, componentType);
    }

    /// Invokes `callback(Entity, TComponents&...)` once for every entity whose current
    /// archetype contains every one of `TComponents...` (it may contain others too). Iteration
    /// order is unspecified beyond "every matching entity exactly once."
    template <typename... TComponents, typename TFunc>
    void ForEach(TFunc&& callback) {
        const std::vector<std::type_index> types{std::type_index(typeid(TComponents))...};
        ForEachErased(types, [&callback](Entity entity, const std::vector<void*>& pointers) {
            Detail::InvokeForEachCallback<PublicComponent<TComponents>...>(
                callback, entity, pointers, std::index_sequence_for<TComponents...>{});
        });
    }

    /// Same stable-archetype iteration rule, but mutable cache references require scene-graph authority.
    template <typename... TComponents, typename TFunc>
    void ForEachSceneGraph(SceneGraphWriteAccess, TFunc&& callback) {
        const std::vector<std::type_index> types{std::type_index(typeid(TComponents))...};
        ForEachErased(types, [&callback](Entity entity, const std::vector<void*>& pointers) {
            Detail::InvokeForEachCallback<TComponents...>(
                callback, entity, pointers, std::index_sequence_for<TComponents...>{});
        });
    }

    /// Collects the entities currently carrying every one of `TComponents...` into an iterable
    /// `EntityView` (range-for over `entry.entity` plus live `entry.Get<T>()` references).
    /// Same matches as `ForEach`, snapshotted once: re-call to re-query, and record
    /// structural changes through `EntityCommandBuffer` rather than making them mid-iteration.
    template <typename... TComponents>
    [[nodiscard]] EntityView<TComponents...> View() {
        std::vector<typename EntityView<TComponents...>::Row> rows;
        const std::vector<std::type_index> types{std::type_index(typeid(TComponents))...};
        ForEachErased(types, [&rows](const Entity entity, const std::vector<void*>& pointers) {
            rows.push_back(Detail::MakeViewRow<TComponents...>(
                entity, pointers, std::index_sequence_for<TComponents...>{}));
        });
        return EntityView<TComponents...>{std::move(rows)};
    }

protected:
    [[nodiscard]] virtual void* AddComponentErased(Entity entity, std::type_index componentType,
                                                    const ComponentTypeInfo& typeInfo) = 0;
    virtual void RemoveComponentErased(Entity entity, std::type_index componentType) = 0;
    [[nodiscard]] virtual bool HasComponentErased(Entity entity, std::type_index componentType) const = 0;
    [[nodiscard]] virtual void* GetComponentErased(Entity entity, std::type_index componentType) = 0;

    /// `callback` receives, for every matching entity, its Entity and a vector of raw
    /// component pointers in the same order as `componentTypes`.
    virtual void ForEachErased(
        const std::vector<std::type_index>& componentTypes,
        const std::function<void(Entity, const std::vector<void*>&)>& callback) = 0;
};

} // namespace UVE::Scene
