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
/// of a rig is, from its name and its shape together. Names catch most rigs (MatchHumanoid);
/// the brain is for the rest - bones called "Bone012", names in another language or another
/// tool's habit, a rig whose names lie - where where a bone sits, how long it is, what hangs off
/// it and which way it points say what it is.
///
/// Everything here is written for the engine: a multi-layer perceptron (ReLU, softmax, cross
/// entropy, Adam), trained on rigs generated from the humanoid reference itself (proportions,
/// rest poses from A to T, naming styles, bones left out, extra bones added) and on the rigs a
/// project has had mapped by hand. Training is deterministic: the same seed and data give the
/// same weights. It predicts a mapping; it never invents skin weights or moves a mesh.
struct RigBrainConfig final {
    std::uint32_t seed = 0x5EED1234U;
    /// Generated rigs to learn from (each about 60-160 bones).
    std::size_t rigs = 260U;
    std::size_t epochs = 10U;
    std::size_t hidden = 128U;
    float learningRate = 0.003F;
};

/// One rig and, per bone, the humanoid bone it is (-1: none of them, a bone of its own).
struct RigBrainSample final {
    RetargetSkeleton rig;
    std::vector<std::int32_t> referenceOfBone;
};

/// What the brain thinks one bone is.
struct BonePrediction final {
    /// A reference bone, or -1 for "none of the humanoid's".
    std::int32_t reference = -1;
    float confidence = 0.0F;
};

class RigBrain final {
public:
    /// Hashed letter triples and words of the bone's name.
    static constexpr std::size_t kNameFeatures = 128U;
    /// Its side, where it sits, its length and direction, its place in the hierarchy, and the same
    /// of its parent, its siblings and the chain it is on.
    static constexpr std::size_t kShapeFeatures = 40U;
    static constexpr std::size_t kFeatures = kNameFeatures + kShapeFeatures;

    [[nodiscard]] bool IsTrained() const noexcept { return !m_layers.empty(); }
    /// The reference's bones plus one class for "none".
    [[nodiscard]] std::size_t ClassCount() const noexcept { return m_classes; }

    /// Learns from generated rigs and `extra` (the project's own mapped rigs, weighted up).
    void Train(const HumanoidReference& reference, const std::vector<RigBrainSample>& extra = {},
                  const RigBrainConfig& config = {});
    /// Per bone of `rig`: the humanoid bone it most likely is. Each humanoid bone is given to one
    /// bone at most (the surest first).
    [[nodiscard]] std::vector<BonePrediction> Predict(const RetargetSkeleton& rig, const HumanoidReference& reference) const;

    /// The weights as text (a `.uvbrain` file), and back. Read refuses a brain for another reference.
    [[nodiscard]] std::string Write() const;
    [[nodiscard]] bool Read(std::string_view text, const HumanoidReference& reference);

private:
    struct Layer {
        std::size_t inputs = 0U;
        std::size_t outputs = 0U;
        std::vector<float> weights;   // outputs x inputs, row-major
        std::vector<float> biases;
    };
    [[nodiscard]] std::vector<float> Forward(const float* features, std::vector<std::vector<float>>* activations) const;

    std::vector<Layer> m_layers;
    std::size_t m_classes = 0U;
    std::size_t m_referenceBones = 0U;
};

/// Per bone of `rig`, its RigBrain::kFeatures features, one row after another.
[[nodiscard]] std::vector<float> RigBoneFeatures(const RetargetSkeleton& rig, const HumanoidReference& reference);

/// `count` rigs made from the reference: proportions, rest poses between the A-pose and a
/// T-pose, naming styles (including names that say nothing), bones left out, bones added.
[[nodiscard]] std::vector<RigBrainSample> MakeSyntheticRigs(const HumanoidReference& reference, std::size_t count,
                                                                std::uint32_t seed);

/// MatchHumanoid, with the brain finding the bones names could not: a bone the names left
/// unpaired is given the humanoid bone the brain is surest of (at least `minimumConfidence`, and
/// only one the names did not find, where the hierarchy agrees). Then the chains are followed: a
/// found bone with one body bone left to find below it and one rig bone left below it pairs them. Everything is then checked as usual; a pair the brain made
/// with less than 85% confidence is a Warning, so it is looked at.
[[nodiscard]] HumanoidMatch MatchHumanoidWithBrain(const RetargetSkeleton& rig, const HumanoidReference& reference,
                                                        const RigBrain& brain, float minimumConfidence = 0.7F);

/// A project's mapped rigs (what it learned from), as text (`Saved/RigBrain/samples.uvbrain`), and back.
[[nodiscard]] std::string WriteRigBrainSamples(const std::vector<RigBrainSample>& samples);
[[nodiscard]] std::vector<RigBrainSample> ReadRigBrainSamples(std::string_view text);

/// The brain the engine ships with: trained on first use from generated rigs only (deterministic).
[[nodiscard]] const RigBrain& GetDefaultRigBrain();
/// Starts training it in the background (an editor window that will want it soon); returns at once.
void WarmDefaultRigBrain();
/// Whether it is trained (GetDefaultRigBrain would not wait).
[[nodiscard]] bool IsDefaultRigBrainReady() noexcept;

} // namespace UVE::Retarget
