// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "quaternion.h"
#include "retarget_brain.h"
#include "retarget_humanoid.h"
#include "retarget_match.h"
#include "retarget_test_rig.h"

namespace UVE::Retarget {
namespace {

/// A brain small enough to train in a test, the same way the engine's is trained.
[[nodiscard]] RigBrainConfig SmallConfig() {
    RigBrainConfig config;
    config.rigs = 60U;
    config.epochs = 5U;
    config.hidden = 48U;
    return config;
}

/// How many bones of held-out rigs it names right, over the bones that are the humanoid's core.
[[nodiscard]] float Accuracy(const RigBrain& brain, const std::vector<RigBrainSample>& rigs, const HumanoidReference& reference) {
    std::size_t right = 0U;
    std::size_t total = 0U;
    for (const RigBrainSample& sample : rigs) {
        const std::vector<BonePrediction> predictions = brain.Predict(sample.rig, reference);
        for (std::size_t bone = 0U; bone < predictions.size(); ++bone) {
            const std::int32_t truth = sample.referenceOfBone[bone];
            if (truth < 0 || reference.info[static_cast<std::size_t>(truth)].optional) {
                continue;
            }
            ++total;
            right += predictions[bone].reference == truth ? 1U : 0U;
        }
    }
    return total == 0U ? 0.0F : static_cast<float>(right) / static_cast<float>(total);
}

TEST(RetargetBrainTest, SyntheticRigsAreValidAndLabelled) {
    const HumanoidReference& reference = GetHumanoidReference();
    const std::vector<RigBrainSample> rigs = MakeSyntheticRigs(reference, 20U, 7U);
    ASSERT_EQ(rigs.size(), 20U);
    for (const RigBrainSample& sample : rigs) {
        EXPECT_TRUE(IsRetargetSkeletonValid(sample.rig));
        EXPECT_EQ(sample.referenceOfBone.size(), sample.rig.bones.size());
        EXPECT_GE(sample.rig.bones.size(), 20U) << "at least the body";
    }
    EXPECT_EQ(MakeSyntheticRigs(reference, 3U, 7U)[2].rig, MakeSyntheticRigs(reference, 3U, 7U)[2].rig) << "deterministic";
}

TEST(RetargetBrainTest, TrainingIsDeterministicAndRoundTrips) {
    const HumanoidReference& reference = GetHumanoidReference();
    RigBrainConfig config = SmallConfig();
    config.rigs = 12U;
    config.epochs = 2U;
    RigBrain a;
    RigBrain b;
    a.Train(reference, {}, config);
    b.Train(reference, {}, config);
    ASSERT_TRUE(a.IsTrained());
    EXPECT_EQ(a.Write(), b.Write());
    RigBrain read;
    ASSERT_TRUE(read.Read(a.Write(), reference));
    EXPECT_EQ(read.Write(), a.Write());
    EXPECT_FALSE(read.Read("{\"format\":\"something else\"}", reference));
}

TEST(RetargetBrainTest, NamesHeldOutRigsCoreBonesFromNamesAndShape) {
    const HumanoidReference& reference = GetHumanoidReference();
    RigBrain brain;
    brain.Train(reference, {}, SmallConfig());
    const std::vector<RigBrainSample> heldOut = MakeSyntheticRigs(reference, 30U, 0xBADC0DEU);
    const float accuracy = Accuracy(brain, heldOut, reference);
    std::printf("[ RigBrain ] held-out core accuracy %.1f%%\n", static_cast<double>(accuracy) * 100.0);
    EXPECT_GT(accuracy, 0.85F);
}

TEST(RetargetBrainTest, FindsTheBodyOfARigWhoseNamesSayNothing) {
    const HumanoidReference& reference = GetHumanoidReference();
    RigBrain brain;
    brain.Train(reference, {}, SmallConfig());
    // The test rig with every name replaced by a number.
    TestRig::TPoseRig rig;
    RetargetSkeleton anonymous = rig.skeleton;
    const HumanoidMatch byName = MatchHumanoid(rig.skeleton, reference);
    for (std::size_t bone = 0U; bone < anonymous.bones.size(); ++bone) {
        char text[16];
        std::snprintf(text, sizeof(text), "Bone%03zu", bone);
        anonymous.bones[bone].name = text;
    }
    const HumanoidMatch names = MatchHumanoid(anonymous, reference);
    EXPECT_EQ(names.Count(JointStatus::Good) + names.Count(JointStatus::Warning), 0U) << "names alone find nothing";
    const HumanoidMatch match = MatchHumanoidWithBrain(anonymous, reference, brain, 0.3F);
    std::size_t right = 0U;
    std::size_t total = 0U;
    for (std::size_t bone = 0U; bone < anonymous.bones.size(); ++bone) {
        if (byName.referenceOfBone[bone] < 0) {
            continue;
        }
        ++total;
        right += match.referenceOfBone[bone] == byName.referenceOfBone[bone] ? 1U : 0U;
    }
    std::printf("[ RigBrain ] anonymous rig: %zu of %zu bones found\n", right, total);
    EXPECT_GE(right * 10U, total * 7U) << "at least 70% of an unnamed body found from its shape";
}

/// The test rig with names that say nothing, stood the way `turn` stands it.
[[nodiscard]] RetargetSkeleton AnonymousTurned(const RetargetSkeleton& rig, const Math::Quaternion& turn) {
    RetargetSkeleton out = rig;
    for (std::size_t bone = 0U; bone < out.bones.size(); ++bone) {
        char text[16];
        std::snprintf(text, sizeof(text), "joint%zu", bone);
        out.bones[bone].name = text;
    }
    if (!out.bones.empty()) {
        // The root carries the turn; everything below follows it.
        out.bones.front().position = Math::RotateVector(turn, out.bones.front().position);
        out.bones.front().rotation = Math::Multiply(turn, out.bones.front().rotation);
    }
    return out;
}

TEST(RetargetBrainTest, FindsAnUnnamedBodyHoweverItsToolStoodIt) {
    const HumanoidReference& reference = GetHumanoidReference();
    const RigBrain& brain = GetDefaultRigBrain();
    TestRig::TPoseRig rig;
    const HumanoidMatch byName = MatchHumanoid(rig.skeleton, reference);
    Math::Quaternion zUp{};
    Math::Quaternion away{};
    ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{1.0F, 0.0F, 0.0F}, -1.5707963F, zUp));
    ASSERT_TRUE(Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F}, 3.1415927F, away));
    for (const Math::Quaternion& turn : {Math::Quaternion{}, zUp, away}) {
        const RetargetSkeleton anonymous = AnonymousTurned(rig.skeleton, turn);
        const HumanoidMatch match = MatchHumanoidWithBrain(anonymous, reference, brain, 0.3F);
        std::size_t right = 0U;
        std::size_t total = 0U;
        for (std::size_t bone = 0U; bone < anonymous.bones.size(); ++bone) {
            if (byName.referenceOfBone[bone] >= 0) {
                ++total;
                right += match.referenceOfBone[bone] == byName.referenceOfBone[bone] ? 1U : 0U;
            }
        }
        std::printf("[ RigBrain ] unnamed rig turned (%.2f %.2f %.2f %.2f): %zu of %zu\n", static_cast<double>(turn.x),
                    static_cast<double>(turn.y), static_cast<double>(turn.z), static_cast<double>(turn.w), right, total);
        EXPECT_GE(right * 10U, total * 7U) << "at least 70% of an unnamed body, however it stands";
    }
}

TEST(RetargetBrainTest, NamesHeldOutRigsInEveryNamingStyle) {
    const HumanoidReference& reference = GetHumanoidReference();
    const RigBrain& brain = GetDefaultRigBrain();
    // Rigs it never saw (another seed): every naming style, stood every way, a little off the ideal.
    std::vector<RigBrainSample> named;
    std::vector<RigBrainSample> unnamed;
    for (RigBrainSample& sample : MakeSyntheticRigs(reference, 120U, 0x51DE5U)) {
        const std::string& first = sample.rig.bones.front().name;
        (first.starts_with("Bone") || first.starts_with("joint") ? unnamed : named).push_back(std::move(sample));
    }
    ASSERT_FALSE(named.empty());
    ASSERT_FALSE(unnamed.empty());
    const float byName = Accuracy(brain, named, reference);
    const float byShape = Accuracy(brain, unnamed, reference);
    std::printf("[ RigBrain ] held-out: named rigs %.1f%%, rigs whose names say nothing %.1f%%\n",
                static_cast<double>(byName) * 100.0, static_cast<double>(byShape) * 100.0);
    EXPECT_GT(byName, 0.97F);
    // About 0.70 from the network alone; Debug and Release round differently, so a margin.
    EXPECT_GT(byShape, 0.65F);
}

TEST(RetargetBrainTest, LearnsFromTheProjectsOwnRigs) {
    const HumanoidReference& reference = GetHumanoidReference();
    // A studio's own naming the generated rigs never use.
    TestRig::TPoseRig rig;
    const HumanoidMatch byName = MatchHumanoid(rig.skeleton, reference);
    RigBrainSample own;
    own.rig = rig.skeleton;
    for (std::size_t bone = 0U; bone < own.rig.bones.size(); ++bone) {
        own.rig.bones[bone].name = "zq" + std::to_string(bone * 7U % 31U) + "_kx";
    }
    own.referenceOfBone = byName.referenceOfBone;
    const auto score = [&](const RigBrain& brain) {
        const std::vector<BonePrediction> predictions = brain.Predict(own.rig, reference);
        std::size_t right = 0U;
        for (std::size_t bone = 0U; bone < predictions.size(); ++bone) {
            right += own.referenceOfBone[bone] >= 0 && predictions[bone].reference == own.referenceOfBone[bone] ? 1U : 0U;
        }
        return right;
    };
    RigBrain before;
    before.Train(reference, {}, SmallConfig());
    RigBrain after;
    after.Train(reference, {own}, SmallConfig());
    EXPECT_GT(score(after), score(before));
}


TEST(RetargetBrainTest, TheEnginesOwnBrainIsTrainedOnFirstUse) {
    const HumanoidReference& reference = GetHumanoidReference();
    const RigBrain& brain = GetDefaultRigBrain();
    ASSERT_TRUE(brain.IsTrained());
    EXPECT_EQ(brain.ClassCount(), reference.skeleton.bones.size() + 1U);
    const float accuracy = Accuracy(brain, MakeSyntheticRigs(reference, 30U, 0xFEEDU), reference);
    std::printf("[ RigBrain ] engine brain held-out core accuracy %.1f%%\n", static_cast<double>(accuracy) * 100.0);
    EXPECT_GT(accuracy, 0.92F);
}


TEST(RetargetBrainTest, AProjectsSamplesRoundTrip) {
    const std::vector<RigBrainSample> samples = MakeSyntheticRigs(GetHumanoidReference(), 2U, 3U);
    const std::vector<RigBrainSample> read = ReadRigBrainSamples(WriteRigBrainSamples(samples));
    ASSERT_EQ(read.size(), samples.size());
    for (std::size_t i = 0U; i < samples.size(); ++i) {
        EXPECT_EQ(read[i].referenceOfBone, samples[i].referenceOfBone);
        ASSERT_EQ(read[i].rig.bones.size(), samples[i].rig.bones.size());
        EXPECT_EQ(read[i].rig.bones.back().name, samples[i].rig.bones.back().name);
    }
    EXPECT_TRUE(ReadRigBrainSamples("not json").empty());
}

} // namespace
} // namespace UVE::Retarget
