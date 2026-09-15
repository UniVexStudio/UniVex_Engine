// univex/integration/EntityManagerEntitySource.h (private to this module - not under include/)
// -----------------------------------------------------------------------
// Implements univex::integration::IEntityTransformSourceUVE directly over a
// UVE::Scene::IEntityManagerUVE&, for host engines whose entity manager
// isn't owned by a UVE::World::WorldUVE (WorldUVE constructs its own
// internal EntityManagerUVE from an allocator + event system - it doesn't
// wrap an externally-owned one, so it can't be pointed at EngineCoreUVE's
// real, already-live entity manager). EngineCoreUVE/EngineServicesUVE own
// their own IEntityManagerUVE& directly - this adapter reads that one.
//
// Otherwise identical in shape to WorldUveEntitySource.cpp - only the
// source of the IEntityManagerUVE& differs.
// -----------------------------------------------------------------------
#pragma once

#include "univex/integration/EntityTransformSource.h"

#include "uve/scene/i_entity_manager_uve.h"

namespace univex::integration {

class EntityManagerEntitySource final : public IEntityTransformSourceUVE {
public:
    explicit EntityManagerEntitySource(UVE::Scene::IEntityManagerUVE& entityManager)
        : entityManager_(entityManager) {}

    [[nodiscard]] std::vector<EntityTransformUVE> GetEntityTransformsUVE() const override;

private:
    UVE::Scene::IEntityManagerUVE& entityManager_;
};

} // namespace univex::integration
