// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "retarget_humanoid.h"
#include "retarget_match.h"
#include "retarget_skeleton.h"

namespace UVE::Retarget {

/// The Rig Brain: the engine's own small neural network that says which humanoid bone each bone
/// of a rig is, from its name and its shape together. Names catch most rigs (MatchHumanoidUVE);
/// the brain is for the rest - bones called "Bone012", names in another language or another
/// tool's habit, a rig whose names lie - where where a bone sits, how long it is, what hangs off
/// it and which way it points say what it is.
///
/// Everything here is written for the engine: a multi-layer perceptron (ReLU, softmax, cross
/// entropy, Adam), trained on rigs generated from the humanoid reference itself (proportions,
/// rest poses from A to T, naming styles, bones left out, extra bones added) and on the rigs a
/// project has had mapped by hand. Training is deterministic: the same seed and data give the
/// same weights. It predicts a mapping; it never invents skin weights or moves a mesh.
struct RigBrainConfigUVE final {
    std::uint32_t seed = 0x5EED1234U;
    /// Generated rigs to learn from (each about 60-160 bones).
    std::size_t rigs = 160U;
    std::size_t epochs = 6U;
    std::size_t hidden = 64U;
    float learningRate = 0.003F;
};

/// One rig and, per bone, the humanoid bone it is (-1: none of them, a bone of its own).
struct RigBrainSampleUVE final {
    RetargetSkeletonUVE rig;
    std::vector<std::int32_t> referenceOfBone;
};

/// What the brain thinks one bone is.
struct BonePredictionUVE final {
    /// A reference bone, or -1 for "none of the humanoid's".
    std::int32_t reference = -1;
    float confidence = 0.0F;
};

class RigBrainUVE final {
public:
    /// Hashed letter triples and words of the bone's name.
    static constexpr std::size_t kNameFeaturesUVE = 128U;
    /// Its side, where it sits, its length and direction, its place in the hierarchy.
    static constexpr std::size_t kShapeFeaturesUVE = 24U;
    static constexpr std::size_t kFeaturesUVE = kNameFeaturesUVE + kShapeFeaturesUVE;

    [[nodiscard]] bool IsTrainedUVE() const noexcept { return !m_layers.empty(); }
    /// The reference's bones plus one class for "none".
    [[nodiscard]] std::size_t ClassCountUVE() const noexcept { return m_classes; }

    /// Learns from generated rigs and `extra` (the project's own mapped rigs, weighted up).
    void TrainUVE(const HumanoidReferenceUVE& reference, const std::vector<RigBrainSampleUVE>& extra = {},
                  const RigBrainConfigUVE& config = {});
    /// Per bone of `rig`: the humanoid bone it most likely is. Each humanoid bone is given to one
    /// bone at most (the surest first).
    [[nodiscard]] std::vector<BonePredictionUVE> PredictUVE(const RetargetSkeletonUVE& rig, const HumanoidReferenceUVE& reference) const;

    /// The weights as text (a `.uvbrain` file), and back. Read refuses a brain for another reference.
    [[nodiscard]] std::string WriteUVE() const;
    [[nodiscard]] bool ReadUVE(std::string_view text, const HumanoidReferenceUVE& reference);

private:
    struct LayerUVE {
        std::size_t inputs = 0U;
        std::size_t outputs = 0U;
        std::vector<float> weights;   // outputs x inputs, row-major
        std::vector<float> biases;
    };
    [[nodiscard]] std::vector<float> ForwardUVE(const float* features, std::vector<std::vector<float>>* activations) const;

    std::vector<LayerUVE> m_layers;
    std::size_t m_classes = 0U;
    std::size_t m_referenceBones = 0U;
};

/// Per bone of `rig`, its RigBrainUVE::kFeaturesUVE features, one row after another.
[[nodiscard]] std::vector<float> RigBoneFeaturesUVE(const RetargetSkeletonUVE& rig, const HumanoidReferenceUVE& reference);

/// `count` rigs made from the reference: proportions, rest poses between the A-pose and a
/// T-pose, naming styles (including names that say nothing), bones left out, bones added.
[[nodiscard]] std::vector<RigBrainSampleUVE> MakeSyntheticRigsUVE(const HumanoidReferenceUVE& reference, std::size_t count,
                                                                std::uint32_t seed);

/// MatchHumanoidUVE, with the brain finding the bones names could not: a bone the names left
/// unpaired is given the humanoid bone the brain is surest of (at least `minimumConfidence`, and
/// only one the names did not find). Everything is then checked as usual; a pair the brain made
/// with less than 85% confidence is a Warning, so it is looked at.
[[nodiscard]] HumanoidMatchUVE MatchHumanoidWithBrainUVE(const RetargetSkeletonUVE& rig, const HumanoidReferenceUVE& reference,
                                                        const RigBrainUVE& brain, float minimumConfidence = 0.7F);

/// A project's mapped rigs (what it learned from), as text (`Saved/RigBrain/samples.uvbrain`), and back.
[[nodiscard]] std::string WriteRigBrainSamplesUVE(const std::vector<RigBrainSampleUVE>& samples);
[[nodiscard]] std::vector<RigBrainSampleUVE> ReadRigBrainSamplesUVE(std::string_view text);

/// The brain the engine ships with: trained on first use from generated rigs only (deterministic).
[[nodiscard]] const RigBrainUVE& GetDefaultRigBrainUVE();
/// Starts training it in the background (an editor window that will want it soon); returns at once.
void WarmDefaultRigBrainUVE();
/// Whether it is trained (GetDefaultRigBrainUVE would not wait).
[[nodiscard]] bool IsDefaultRigBrainReadyUVE() noexcept;

} // namespace UVE::Retarget
