// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "uve/component/entity_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumInteractionAreaCandidatesUVE = 16U;

struct InteractionArea3DComponentUVE final {
    Math::Vector3UVE halfExtents{1.0F, 1.0F, 1.0F};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::string interactionTag = "interactable";
    std::uint32_t maximumCandidates = 16U;
    bool enabled = true;
    EntityUVE ignoreEntity = kInvalidEntityUVE;
    std::array<EntityUVE, kMaximumInteractionAreaCandidatesUVE> interactors =
        MakeEmptyEntityReferencesUVE<kMaximumInteractionAreaCandidatesUVE>();
    std::uint8_t interactorCount = 0U;
    bool interactorsTruncated = false;
    bool focusedByPrimaryInteractor = false;
};

[[nodiscard]] bool IsInteractionArea3DObjectComponentValidUVE(const InteractionArea3DComponentUVE& value) noexcept;

struct InteractionFocusCandidateUVE final {
    EntityUVE areaEntity{};
    float distanceSquared = 0.0F;
};

[[nodiscard]] std::size_t ResolveInteractionAreaCandidateCapUVE(std::uint32_t authoredMaximumCandidates,
                                                                std::size_t storageBound) noexcept;

[[nodiscard]] std::optional<EntityUVE> ResolvePrimaryInteractorUVE(
    std::span<const EntityUVE> interactorCandidates) noexcept;

[[nodiscard]] std::optional<EntityUVE> ResolveInteractionFocusUVE(
    std::span<const InteractionFocusCandidateUVE> candidates) noexcept;

class InteractionArea3DUVE final {
public:
    [[nodiscard]] static bool IsArmedUVE(const InteractionArea3DComponentUVE& area) noexcept;

    [[nodiscard]] static bool AcceptsInteractorUVE(EntityUVE areaEntity, const InteractionArea3DComponentUVE& area,
                                                   EntityUVE interactorEntity, std::uint32_t interactorLayer,
                                                   std::uint32_t interactorMask) noexcept;

    static void ClearInteractorsUVE(InteractionArea3DComponentUVE& area) noexcept;
    static void CommitInteractorsUVE(InteractionArea3DComponentUVE& area, const EntityUVE* interactors,
                                     std::size_t interactorCount);

    [[nodiscard]] static const EntityUVE* GetInteractorUVE(const InteractionArea3DComponentUVE& area,
                                                           std::size_t index) noexcept;
    [[nodiscard]] static bool HasInteractorUVE(const InteractionArea3DComponentUVE& area,
                                               EntityUVE interactor) noexcept;

    [[nodiscard]] static std::size_t ResolveCandidateCapUVE(std::uint32_t authoredMaximumCandidates,
                                                            std::size_t storageBound) noexcept;
    [[nodiscard]] static std::optional<EntityUVE> ResolvePrimaryInteractorUVE(
        std::span<const EntityUVE> interactorCandidates) noexcept;
    [[nodiscard]] static std::optional<EntityUVE> ResolveFocusUVE(
        std::span<const InteractionFocusCandidateUVE> candidates) noexcept;
};

} // namespace UVE::Scene
