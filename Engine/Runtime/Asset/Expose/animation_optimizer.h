// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "animation_clip_asset.h"

#include <cstddef>

namespace UVE::Asset {

/// How far an optimised clip may move a joint from where the original puts it, in metres in the
/// skeleton's world: `rootErrorMetres` at the root, growing by `depthGrowth` of it per level down
/// the hierarchy (a fingertip may stray more than the hips), never more than `maximumErrorMetres`.
struct AnimationOptimizeSettingsUVE final {
    double rootErrorMetres = 0.002;
    double depthGrowth = 0.75;
    double maximumErrorMetres = 0.015;
    /// Times the clip is checked at, spread over its length.
    std::size_t probes = 96U;
};

struct AnimationOptimizeResultUVE final {
    /// The clip was rewritten (fewer keys); false leaves it as it was.
    bool changed = false;
    std::size_t samplesBefore = 0U;
    std::size_t samplesAfter = 0U;
    /// The worst joint error found against its own budget (1 is exactly on budget).
    double worstBudgetRatio = 0.0;
    /// The worst joint error found, in metres.
    double worstErrorMetres = 0.0;
};

/// Rewrites `clip` with as few keys as it can keep within the settings' error: an error-driven
/// search over the codec's key reduction (AnimationClipCompression) that measures every joint in
/// the skeleton's world at each probe and keeps the strongest reduction that stays within every
/// joint's budget. An object clip (no rest skeleton) is checked on its own track. Events, the rest
/// skeleton and blend shapes are kept as they are. Leaves `clip` untouched when nothing smaller
/// passes, and when the clip is not valid.
[[nodiscard]] AnimationOptimizeResultUVE OptimizeAnimationClipUVE(AnimationClipAssetUVE& clip,
                                                                  const AnimationOptimizeSettingsUVE& settings = {});

/// The number of samples a clip stores (its object track and every bone track).
[[nodiscard]] std::size_t CountAnimationClipSamplesUVE(const AnimationClipAssetUVE& clip) noexcept;

} // namespace UVE::Asset
