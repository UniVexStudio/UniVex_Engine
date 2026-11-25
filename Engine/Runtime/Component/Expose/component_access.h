// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <type_traits>

namespace UVE::Scene {
struct WorldTransformComponent;
class ISceneGraph;

/// Derived world poses are read-only through ordinary live ECS access. Component construction
/// still accepts value snapshots (including diagnostic fixtures); this is an ownership boundary,
/// not protection against deliberate const_cast or raw-memory corruption. Authored components
/// retain their existing mutable access. The dirty bit remains an explicit invalidation request.
template <typename T>
using PublicComponent = std::conditional_t<std::is_same_v<std::remove_cv_t<T>, WorldTransformComponent>,
                                              std::add_const_t<T>, T>;

/// Only the scene graph can request mutable live derived-cache access. No public factory exists.
class SceneGraphWriteAccess final {
    friend class ISceneGraph;
    SceneGraphWriteAccess() = default;
};
} // namespace UVE::Scene
