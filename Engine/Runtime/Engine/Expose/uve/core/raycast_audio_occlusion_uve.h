// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/audio/audio_occlusion_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Physics {
class IRaycastSystemUVE;
} // namespace UVE::Physics

namespace UVE::Core {

/// Physics-driven occlusion: the concrete IAudioOcclusionProviderUVE that turns the raycast system
/// into an occlusion answer.
///
/// It lives in uve_core rather than uve_audio because the dependency only points one way - audio
/// declares the narrow query interface it needs, and the module that already knows both physics and
/// audio supplies the implementation. Putting it in uve_audio would make the audio library depend on
/// physics, and putting it in uve_physics would make physics depend on audio; neither is true.
///
/// The query is deliberately not a single centre ray. One ray gives a hard oracle that flips between
/// "clear" and "fully blocked" as a source crosses a doorway edge, which sounds like a switch. Three
/// rays - to each ear and to the head centre - give a blocked fraction in thirds, so a source sliding
/// out from behind a wall fades instead of popping. The system's own smoothing turns that into a
/// continuous curve.
///
/// Cost: three raycasts per source per query, and the audio system only queries at
/// kAudioOcclusionQueryIntervalSecondsUVE (10 Hz), so a scene with fifty occluded sources costs at
/// most 1500 raycasts a second - two orders of magnitude below a frame's render work.
///
/// Thread-safety: read-only once constructed. RaycastUVE() performs no mutation, so the provider is
/// safe to call from the engine thread alongside the rest of the audio update; the borrows it takes
/// (entity manager, raycast system) must outlive it.
class RaycastAudioOcclusionUVE final : public Audio::IAudioOcclusionProviderUVE {
public:
    RaycastAudioOcclusionUVE(Scene::IEntityManagerUVE& entityManager,
                             const Physics::IRaycastSystemUVE& raycastSystem) noexcept;

    /// Distance in metres either side of the listener centre that the ear rays start from. 0.1 m is
    /// roughly an adult head; it is a constant rather than a knob because a source that is blocked
    /// for one ear and clear for the other is exactly the partial occlusion this is meant to model.
    static constexpr float kEarOffsetMetersUVE = 0.1F;

    /// Shorter than any audible path: a source sitting on the listener has nothing between them, and
    /// tracing a zero-length ray would only ask the physics system about an undefined direction.
    static constexpr float kMinimumTraceDistanceMetersUVE = 0.01F;

    [[nodiscard]] float QueryOcclusionUVE(const Audio::AudioOcclusionQueryUVE& query) const override;

private:
    Scene::IEntityManagerUVE& m_entityManager;
    const Physics::IRaycastSystemUVE& m_raycastSystem;
};

} // namespace UVE::Core
