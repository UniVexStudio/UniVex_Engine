// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "entity.h"
#include "object_3d_common.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumInteractionAreaCandidates = 16U;

struct InteractionArea3DComponent final {
    Math::Vector3 halfExtents{1.0F, 1.0F, 1.0F};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::string interactionTag = "interactable";
    std::uint32_t maximumCandidates = 16U;
    bool enabled = true;
    Entity ignoreEntity = kInvalidEntity;
    std::array<Entity, kMaximumInteractionAreaCandidates> interactors =
        MakeEmptyEntityReferences<kMaximumInteractionAreaCandidates>();
    std::uint8_t interactorCount = 0U;
    bool interactorsTruncated = false;
    bool focusedByPrimaryInteractor = false;
};

[[nodiscard]] bool IsInteractionArea3DObjectComponentValid(const InteractionArea3DComponent& value) noexcept;

struct InteractionFocusCandidate final {
    Entity areaEntity{};
    float distanceSquared = 0.0F;
};

[[nodiscard]] std::size_t ResolveInteractionAreaCandidateCap(std::uint32_t authoredMaximumCandidates,
                                                                std::size_t storageBound) noexcept;

[[nodiscard]] std::optional<Entity> ResolvePrimaryInteractor(
    std::span<const Entity> interactorCandidates) noexcept;

[[nodiscard]] std::optional<Entity> ResolveInteractionFocus(
    std::span<const InteractionFocusCandidate> candidates) noexcept;

class InteractionArea3D final {
public:
    [[nodiscard]] static bool IsArmed(const InteractionArea3DComponent& area) noexcept;

    [[nodiscard]] static bool AcceptsInteractor(Entity areaEntity, const InteractionArea3DComponent& area,
                                                   Entity interactorEntity, std::uint32_t interactorLayer,
                                                   std::uint32_t interactorMask) noexcept;

    static void ClearInteractors(InteractionArea3DComponent& area) noexcept;
    static void CommitInteractors(InteractionArea3DComponent& area, const Entity* interactors,
                                     std::size_t interactorCount);

    [[nodiscard]] static const Entity* GetInteractor(const InteractionArea3DComponent& area,
                                                           std::size_t index) noexcept;
    [[nodiscard]] static bool HasInteractor(const InteractionArea3DComponent& area,
                                               Entity interactor) noexcept;

    [[nodiscard]] static std::size_t ResolveCandidateCap(std::uint32_t authoredMaximumCandidates,
                                                            std::size_t storageBound) noexcept;
    [[nodiscard]] static std::optional<Entity> ResolvePrimaryInteractor(
        std::span<const Entity> interactorCandidates) noexcept;
    [[nodiscard]] static std::optional<Entity> ResolveFocus(
        std::span<const InteractionFocusCandidate> candidates) noexcept;
};

} // namespace UVE::Scene
