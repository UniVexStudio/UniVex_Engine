// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Scene {

/// The shared state of BoneModifier3D, the abstract base of every node that adjusts a skeleton's
/// pose after animation has posed it - look-at, IK, spring bones. No node is a BoneModifier3D on
/// its own; its kinds carry this component and add their own.
struct BoneModifierComponentUVE final {
    /// Off skips the modifier entirely, as if it were not there.
    bool active = true;
    /// How much of the modifier's result is blended over the incoming pose: 0 leaves the pose
    /// untouched, 1 applies it fully.
    float influence = 1.0F;

    [[nodiscard]] bool operator==(const BoneModifierComponentUVE&) const = default;
};

/// Influence must be finite and within [0, 1].
[[nodiscard]] bool IsBoneModifierComponentValidUVE(const BoneModifierComponentUVE& component) noexcept;

} // namespace UVE::Scene
