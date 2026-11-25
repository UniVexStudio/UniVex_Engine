// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "i_light_system.h"

namespace UVE::Render {

/// LightSystem is the concrete, engine-standard implementation of ILightSystem. Deliberately
/// stateless (no members, no PIMPL) — mirrors CameraSystem's/MeshRenderer's precedent for
/// utility services that always take the IEntityManager& they operate on as an explicit
/// parameter rather than owning a reference to one.
class LightSystem final : public ILightSystem {
public:
    [[nodiscard]] LightList ExtractActiveLights(Scene::IEntityManager& entityManager) const override;

    [[nodiscard]] LightList ExtractActiveLightsForView(
        Scene::IEntityManager& entityManager,
        const Math::Vector3& viewPosition) const override;
};

} // namespace UVE::Render
