// univex/integration/WorldUveEntitySource.h (private to this module - not under include/)
// -----------------------------------------------------------------------
// Implements univex::integration::IEntityTransformSourceUVE by wrapping a
// real Engine/Runtime/World::WorldUVE. This is the one file in this module
// allowed to depend on the host engine (UVE::*) - univex_viewport_core and
// univex_viewport_gl themselves stay engine-agnostic, per the module's own
// design (see the top-level README). Lives outside include/ because it is
// an adapter for one specific host engine, not part of the viewport
// module's own reusable public surface.
// -----------------------------------------------------------------------
#pragma once

#include "univex/integration/EntityTransformSource.h"

#include "uve/world/world_uve.h"

namespace univex::integration {

class WorldUveEntitySource final : public IEntityTransformSourceUVE {
public:
    // Takes a non-const WorldUVE&: IEntityManagerUVE::ForEachUVE() is a
    // non-const method (its underlying archetype storage isn't declared
    // const-callable), even though this adapter's own read-only intent is
    // fully expressed by GetEntityTransformsUVE() itself being const - the
    // reference member's constness is independent of that.
    explicit WorldUveEntitySource(UVE::World::WorldUVE& world) : world_(world) {}

    [[nodiscard]] std::vector<EntityTransformUVE> GetEntityTransformsUVE() const override;

private:
    UVE::World::WorldUVE& world_;
};

} // namespace univex::integration
