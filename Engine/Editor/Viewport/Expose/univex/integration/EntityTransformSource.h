// univex/integration/EntityTransformSource.h
// -----------------------------------------------------------------------
// The narrow interface this viewport module depends on to draw "whatever
// entities exist in a host engine" as proxy cubes, instead of the demo's
// single hardcoded cube. Deliberately engine-agnostic: plain floats only,
// no dependency on any specific engine's Entity/Component/World types.
//
// This mirrors Unreal's FEditorViewportClient pattern (verified against
// the real Unreal Engine source): the viewport-owning side declares a
// narrow accessor for "the world it's rendering" rather than reaching
// into the host engine's internals directly. A concrete host engine (e.g.
// UniVex's own Engine/Runtime/World::WorldUVE) implements this interface
// via its own adapter, kept out of this module so univex_viewport_gl and
// univex_viewport_app_support keep their existing "links nothing but GL"
// property for anyone dropping this module into a different engine.
// -----------------------------------------------------------------------
#pragma once

#include <vector>

namespace univex::integration {

// A minimal, host-engine-agnostic snapshot of one entity's world transform.
// Deliberately position + uniform scale only for this first integration
// slice - no rotation yet, since the reference proxy geometry (a cube) is
// symmetric enough that omitting rotation doesn't hide any real bug and
// wiring quaternion-to-matrix conversion is separable follow-up work, not
// part of proving the integration itself.
struct EntityTransformUVE {
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float uniformScale = 1.0F;
};

class IEntityTransformSourceUVE {
public:
    virtual ~IEntityTransformSourceUVE() = default;

    // Returns one EntityTransformUVE per renderable entity currently in the
    // host world. Called once per frame; implementations own their own
    // caching/iteration cost.
    [[nodiscard]] virtual std::vector<EntityTransformUVE> GetEntityTransformsUVE() const = 0;
};

} // namespace univex::integration
